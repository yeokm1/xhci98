/*
 * test_log.c - the optional log ring (src/xhci_log.c), roadmap task 11-V.7.
 *
 * Pure vectors over XHCI_LOG. Everything that decides anything about the log
 * lives in that file precisely so it can be driven here with no file system, no
 * registry and no IRQL: the append, the wrap and which byte it loses, the
 * record cap, the flush decision, the drain's oldest-first ordering across a
 * wrap, and the two accounting identities.
 *
 * The half this suite deliberately does **not** cover is named rather than
 * implied: the `KeGetCurrentIrql` measurement, the two registry reads and the
 * `DbgPrint` emission are in src/xhci_dispatch.c and are exercised through
 * test_init.c's stand-ins.
 *
 * *(This paragraph also named `ZwCreateFile`/`ZwWriteFile` and asked whether
 * the NT path form reaches a file system on Windows 98. Task 13-L.2 retired the
 * ring-0 file sink and those three imports left the binary with
 * it, so the question has no subject any more: the file a user gets is written
 * by `XHCISNAP` in ring 3, and the ring this suite drives is what it reads.)*
 *
 * Two identities are enforced at the end, both in the shape task 7b-A.1.0
 * established for a row set:
 *
 *   - every XhciLogAppend call increments exactly one of `Appends` or
 *     `Suppressed`, so an append path that silently returned would show up as a
 *     shortfall rather than as a passing vector;
 *   - the bytes are conserved: what the drains handed back plus what the ring
 *     still holds plus what was dropped equals what the appends put in.
 *
 * Both are routed through one wrapper each, so a vector written later cannot
 * opt out of them, and both have a never-reset twin asserted once at the end so
 * an identity that never saw a call cannot pass as a net over nothing.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_compat.h"
#include "../src/xhci_log.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond) ? 1 : 0, (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_log.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got %lu, want %lu)\n",
               "test_log.c", line, what, got, want);
    }
}

/* ------------------------------------------------------------------ */
/* The accounting nets                                                 */
/* ------------------------------------------------------------------ */

static XHCI_LOG log;

/* Every append this file makes goes through here, and every byte every drain
 * returns is added to `drained`. Nothing else may call XhciLogAppend or
 * XhciLogDrain - that is what makes the identities below a net rather than a
 * spot check. */
static unsigned long appendCalls;
static unsigned long appendCallsEver;
static unsigned long drained;
static unsigned long drainedEver;

static void appendOne(const char *label, ULONG value, ULONG hasValue)
{
    appendCalls++;
    appendCallsEver++;
    XhciLogAppend(&log, label, value, hasValue);
}

static ULONG drainInto(UCHAR *out, ULONG capacity)
{
    ULONG n;

    n = XhciLogDrain(&log, out, capacity);
    drained += n;
    drainedEver += n;
    return n;
}

/*
 * The whole ring, through repeated takes, which is what the driver's flush
 * does. Written as a loop over a deliberately small chunk rather than one big
 * call: the driver cannot afford a page-sized stack local (MSVC emits a
 * __chkstk the DDK's driver libraries do not provide) and this is the shape
 * that has to keep working.
 */
static ULONG drainAll(UCHAR *out, ULONG capacity, ULONG chunkSize)
{
    ULONG total;
    ULONG n;

    total = 0;
    for (;;) {
        ULONG want = chunkSize;
        if (total + want > capacity) {
            want = capacity - total;
        }
        if (want == 0) {
            break;
        }
        n = drainInto(out + total, want);
        if (n == 0) {
            break;
        }
        total += n;
    }
    return total;
}

/*
 * `Appends + Suppressed` must equal the calls made. XhciLogFlushBegin appends
 * three records of its own, so a vector that flushes has to declare them - the
 * `extra` argument - rather than the net quietly tolerating an unexplained
 * surplus.
 */
static void checkAppendIdentity(unsigned long extra, const char *where)
{
    check_eq_impl((unsigned long)log.Appends + log.Suppressed,
                  appendCalls + extra, where, __LINE__);
}

static void resetLog(void)
{
    ULONG i;
    UCHAR *p;

    p = (UCHAR *)&log;
    for (i = 0; i < sizeof(log); i++) {
        p[i] = 0;
    }
    appendCalls = 0;
    drained = 0;
    log.Enabled = 1;
    /*
     * **Two switches now, because task 13-L.2 separated them.** `Enabled` is
     * the recording gate and `DebugViewEnabled` is the publication one, and a
     * vector that wants to reach the flush needs both - which is exactly the
     * distinction the vectors below exist to hold. Each one that is about a
     * gate sets that gate itself.
     */
    log.DebugViewEnabled = 1;
}

/* ------------------------------------------------------------------ */
/* Vectors                                                             */
/* ------------------------------------------------------------------ */

/*
 * The formatter, against digits typed out here rather than recomputed through
 * the same shift the code uses - the test_ctx.c rule.
 */
static void testHex32(void)
{
    UCHAR out[9];
    ULONG i;

    for (i = 0; i < 9; i++) {
        out[i] = (UCHAR)'?';
    }

    XhciLogHex32(out, 0x0);
    CHECK(out[0] == '0' && out[7] == '0', "hex32 of zero is eight zeros");

    XhciLogHex32(out, 0xDEADBEEF);
    CHECK(out[0] == 'D' && out[1] == 'E' && out[2] == 'A' && out[3] == 'D' &&
          out[4] == 'B' && out[5] == 'E' && out[6] == 'E' && out[7] == 'F',
          "hex32 of DEADBEEF");

    XhciLogHex32(out, 0x00000001);
    CHECK(out[7] == '1' && out[6] == '0', "hex32 is right-aligned");

    /* Upper case, and never a lower-case 'a'-'f' - a log read on a guest with
     * a case-folding search is not the place to be clever. */
    XhciLogHex32(out, 0xABCDEF01);
    CHECK(out[0] == 'A' && out[4] == 'E', "hex32 digits are upper case");

    /* Exactly eight bytes touched: a ninth would run off a caller's buffer. */
    CHECK(out[8] == (UCHAR)'?', "hex32 writes exactly eight bytes");
}

/* One record, byte for byte, so the record's shape - what XHCISNAP reads out
 * of the ring - is pinned rather than described. */
static void testRecordShape(void)
{
    UCHAR out[64];
    ULONG n;

    resetLog();
    appendOne("ab", 0x1234, 1);

    n = drainInto(out, sizeof(out));
    CHECK_EQ(n, 13, "label(2) + '=' + 8 hex + CRLF");
    CHECK(out[0] == 'a' && out[1] == 'b' && out[2] == '=', "label then '='");
    CHECK(out[3] == '0' && out[9] == '3' && out[10] == '4', "value digits");
    CHECK(out[11] == '\r' && out[12] == '\n', "record ends CRLF");
    CHECK_EQ(log.Appends, 1, "one append counted");
    CHECK_EQ(log.Truncated, 0, "nothing truncated");
    checkAppendIdentity(0, "record shape");

    /* Without a value: label and CRLF only, no stray '='. */
    resetLog();
    appendOne("xy", 0xFFFFFFFF, 0);
    n = drainInto(out, sizeof(out));
    CHECK_EQ(n, 4, "label(2) + CRLF");
    CHECK(out[2] == '\r' && out[3] == '\n', "no separator without a value");
    checkAppendIdentity(0, "record shape, valueless");
}

/*
 * The switch gates the append itself, not just the flush. The counter that
 * proves it is `Suppressed`, and the ring must be untouched - a disabled log
 * that still wrote bytes would be 4 KB of the extension being mutated on every
 * DPC of an ordinary machine.
 */
static void testSwitchOff(void)
{
    UCHAR out[64];

    resetLog();
    log.Enabled = 0;

    appendOne("nope", 1, 1);
    appendOne("also.nope", 2, 1);

    CHECK_EQ(log.Appends, 0, "nothing appended while off");
    CHECK_EQ(log.Suppressed, 2, "both calls counted as suppressed");
    CHECK_EQ(log.Used, 0, "the ring is untouched");
    CHECK_EQ(log.Head, 0, "the head did not move");
    CHECK_EQ(drainInto(out, sizeof(out)), 0, "nothing to drain");
    checkAppendIdentity(0, "switch off");

    /*
     * **The flush's verdict here is EMPTY, not DISABLED, and the change is task
     * 13-L.2's whole point.** Recording being off does not make publication
     * off: the two are separate gates with separate owners, and a machine at
     * level 0 with the sink on reaches the flush and hands over whatever the
     * counter block put in the ring. Here nothing has, because this vector
     * never raised `Publishing` - so the honest verdict is that there is
     * nothing to publish rather than that nobody is listening.
     */
    CHECK_EQ(XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP),
             XHCI_LOG_FLUSH_EMPTY, "recording off leaves nothing to publish");
    CHECK_EQ(log.FlushesRefusedState, 1, "state refusal counted");
    CHECK_EQ(log.FlushesRefusedIrql, 0,
             "a state refusal is not an IRQL refusal");

    /* DISABLED is the sink's verdict and belongs to the sink alone. */
    log.DebugViewEnabled = 0;
    CHECK_EQ(XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP),
             XHCI_LOG_FLUSH_DISABLED, "no sink is what DISABLED means now");
    CHECK_EQ(log.FlushesRefusedState, 2, "and is counted the same way");
}

/*
 * A label longer than the cap is truncated **and loses its value**, because a
 * shortened label followed by a number reads as a complete record naming
 * something else.
 */
static void testTruncation(void)
{
    static char longLabel[XHCI_LOG_MAX_RECORD + 20];
    UCHAR out[256];
    ULONG i;
    ULONG n;

    for (i = 0; i < sizeof(longLabel) - 1; i++) {
        longLabel[i] = 'L';
    }
    longLabel[sizeof(longLabel) - 1] = '\0';

    resetLog();
    appendOne(longLabel, 0xAAAAAAAA, 1);

    CHECK_EQ(log.Truncated, 1, "the truncation is counted");
    CHECK_EQ(log.Appends, 1, "a truncated record is still a record");

    n = drainInto(out, sizeof(out));
    CHECK_EQ(n, XHCI_LOG_MAX_RECORD + 2, "capped label plus CRLF");
    CHECK(out[XHCI_LOG_MAX_RECORD] == '\r' &&
          out[XHCI_LOG_MAX_RECORD + 1] == '\n',
          "a truncated record still ends CRLF");
    for (i = 0; i < XHCI_LOG_MAX_RECORD; i++) {
        if (out[i] != 'L') {
            break;
        }
    }
    CHECK_EQ(i, XHCI_LOG_MAX_RECORD, "no '=' survived the truncation");
    checkAppendIdentity(0, "truncation");

    /* A label of exactly the cap is not truncated: the boundary belongs to the
     * complete record, or every maximum-length label loses its value forever. */
    resetLog();
    longLabel[XHCI_LOG_MAX_RECORD] = '\0';
    appendOne(longLabel, 0x11111111, 1);
    CHECK_EQ(log.Truncated, 0, "a label of exactly the cap is intact");
    n = drainInto(out, sizeof(out));
    CHECK_EQ(n, XHCI_LOG_MAX_RECORD + 1 + 8 + 2, "cap + '=' + hex + CRLF");
    checkAppendIdentity(0, "truncation boundary");
}

/*
 * The wrap, and the whole design question in it: **the oldest byte is the one
 * that goes**. A ring that kept the oldest would answer "what happened at
 * start" when the user is asking "what happened just now".
 */
static void testWrapKeepsTheNewest(void)
{
    static UCHAR out[XHCI_LOG_RING_BYTES];
    ULONG n;
    ULONG i;
    ULONG records;

    resetLog();

    /* "r=XXXXXXXX\r\n" is 12 bytes, so this overruns the ring several times
     * over and the last value written is 0x000001FF. */
    records = (XHCI_LOG_RING_BYTES / 12) + 100;
    for (i = 0; i < records; i++) {
        appendOne("r", i, 1);
    }

    CHECK_EQ(log.Used, XHCI_LOG_RING_BYTES, "the ring is full, not overfull");
    CHECK(log.BytesDropped > 0, "the overrun is counted");
    CHECK_EQ(log.BytesDropped, records * 12 - XHCI_LOG_RING_BYTES,
             "exactly the bytes that did not fit");

    n = drainAll(out, sizeof(out), 256);
    CHECK_EQ(n, XHCI_LOG_RING_BYTES, "a full ring drains whole");

    /* The last twelve bytes are the newest record, and its value is the last
     * one appended - hand-computed, not recomputed through the formatter. */
    CHECK(out[n - 2] == '\r' && out[n - 1] == '\n',
          "the drain ends at a record boundary");
    CHECK(out[n - 12] == 'r' && out[n - 11] == '=',
          "the newest record survived the wrap");
    CHECK(out[n - 3] == (UCHAR)"0123456789ABCDEF"[(records - 1) & 0xF],
          "and it is the last value appended");
    checkAppendIdentity(0, "wrap");
}

/*
 * The drain reassembles across the wrap in the right order. Written as its own
 * vector because the modular start index is the one piece of arithmetic here
 * that a plain "it came back full" check cannot see.
 */
/*
 * The drain is a FIFO **take**, not a whole-ring copy: a short buffer gets the
 * OLDEST bytes and the rest stays for the next call. That is what lets the
 * driver's flush be a loop over a 256-byte stack local instead of a page-sized
 * one - which the DDK build refuses to link, because MSVC emits a `__chkstk`
 * probe for a local that big and the driver libraries have no such symbol.
 */
static void testChunkedDrainIsFifo(void)
{
    static UCHAR out[XHCI_LOG_RING_BYTES];
    ULONG droppedBefore;
    ULONG n;
    ULONG i;

    resetLog();
    for (i = 0; i < 3; i++) {
        appendOne("b", i, 1);
    }
    CHECK_EQ(log.Used, 36, "three twelve-byte records");
    droppedBefore = log.BytesDropped;

    n = drainInto(out, 12);
    CHECK_EQ(n, 12, "a short take gets one record");
    CHECK(out[0] == 'b', "and starts at a record boundary");
    CHECK(out[9] == '0', "the OLDEST record, not the newest");
    CHECK_EQ(log.Used, 24, "the rest stays in the ring");
    CHECK_EQ(log.BytesDropped, droppedBefore,
             "a chunk is not a loss and must not touch BytesDropped");

    n = drainInto(out, 12);
    CHECK(out[9] == '1', "the next take continues in order");
    n = drainInto(out, 12);
    CHECK(out[9] == '2', "and the last one ends it");
    CHECK_EQ(log.Used, 0, "the ring is empty when the takes stop");
    CHECK_EQ(drainInto(out, 12), 0, "an empty ring takes 0 - the loop's exit");
    checkAppendIdentity(0, "chunked drain");

    /* An append between two takes lands after what has been taken - the
     * interleaving the driver's flush allows by dropping the lock per chunk. */
    resetLog();
    appendOne("x", 1, 1);
    appendOne("x", 2, 1);
    (void)drainInto(out, 12);
    appendOne("x", 3, 1);
    n = drainAll(out, sizeof(out), 12);
    CHECK_EQ(n, 24, "both remaining records came back");
    CHECK(out[9] == '2' && out[21] == '3', "in append order");
    checkAppendIdentity(0, "interleaved append");
}

/*
 * The drain reassembles across the wrap in the right order. Written as its own
 * vector because the modular start index is the one piece of arithmetic here
 * that a plain "it came back full" check cannot see.
 */
static void testDrainOrderAcrossWrap(void)
{
    static UCHAR out[XHCI_LOG_RING_BYTES];
    ULONG n;
    ULONG i;
    ULONG records;

    resetLog();

    /* Overrun so the oldest surviving byte is *not* at index 0, then take the
     * whole thing back in small chunks. */
    records = (XHCI_LOG_RING_BYTES / 12) + 7;
    for (i = 0; i < records; i++) {
        appendOne("w", i, 1);
    }
    CHECK_EQ(log.Used, XHCI_LOG_RING_BYTES, "the ring is full");
    CHECK(log.Head != 0, "and the head has moved off zero");

    n = drainAll(out, sizeof(out), 13);
    CHECK_EQ(n, XHCI_LOG_RING_BYTES, "a chunked drain returns the whole ring");

    /* Records are twelve bytes and the ring is a multiple of four, not of
     * twelve, so the oldest surviving record is a partial one - the drain must
     * return it as it stands rather than skipping to a boundary. What is
     * checkable end to end is that every twelfth byte from the first complete
     * record onward is a 'w'. */
    for (i = XHCI_LOG_RING_BYTES % 12; i + 12 <= n; i += 12) {
        if (out[i] != 'w' || out[i + 1] != '=') {
            break;
        }
    }
    CHECK(i + 12 > n, "every record after the first came back intact and aligned");
    CHECK(out[n - 3] == (UCHAR)"0123456789ABCDEF"[(records - 1) & 0xF],
          "and the last record is the last value appended");
    checkAppendIdentity(0, "drain order");
}

/*
 * The flush decision, which is the whole of what the pure core knows about
 * flushing. Three outcomes and each is a different reading on a target.
 */
static void testFlushDecision(void)
{
    static UCHAR out[XHCI_LOG_RING_BYTES];
    ULONG n;

    resetLog();
    /* **The flush is gated on the SINK, not on `Enabled`** (task 13-L.2): the
     * ladder says level 0 publishes the counter block, so a machine at level 0
     * with the sink on must reach the flush and hand over what the counter
     * block put in the ring. Testing `Enabled` here would have made level 0
     * publish nothing at all. */
    log.DebugViewEnabled = 0;
    appendOne("recorded but unpublished", 1, 1);
    CHECK_EQ(XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP),
             XHCI_LOG_FLUSH_DISABLED, "no sink means nothing to publish");
    CHECK_EQ(log.FlushesRefusedState, 1, "counted as a state refusal");

    resetLog();
    log.DebugViewEnabled = 1;
    CHECK_EQ(XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP),
             XHCI_LOG_FLUSH_EMPTY, "an empty ring has nothing to flush");
    CHECK_EQ(log.FlushesRefusedState, 1, "counted as a state refusal");
    CHECK_EQ(log.Used, 0, "and the reason record was NOT appended");

    appendOne("something", 1, 1);
    CHECK_EQ(XhciLogFlushBegin(&log, XHCI_LOG_REASON_FAILURE),
             XHCI_LOG_FLUSH_GO, "a non-empty ring with a sink flushes");

    /* The three records XhciLogFlushBegin adds are the last thing in the
     * drain, so a torn hand-over still ends at a record boundary. */
    n = drainInto(out, sizeof(out));
    CHECK(n > 12, "the reason records are in the drain");
    CHECK(out[0] == 's', "the caller's record is still first");
    CHECK(out[n - 2] == '\r' && out[n - 1] == '\n', "ends at a boundary");
    checkAppendIdentity(3, "flush decision");

    /* The IRQL refusal is a different counter from every other refusal, because
     * it is the one that says something about the platform rather than about
     * this driver's state - batch 11-V reads it on its own. */
    XhciLogFlushRefusedIrql(&log);
    CHECK_EQ(log.FlushesRefusedIrql, 1, "IRQL refusals are counted apart");
    CHECK_EQ(log.FlushesRefusedState, 1, "and did not move the state counter");
}

/*
 * **`Publishing` is the one bypass of the recording switch, and it belongs to
 * the flush.** The ladder says level 0 publishes the counter block, so the
 * counter block has to reach the ring on a machine whose ordinary producers are
 * off - which is every machine at the default.
 */
static void testPublishingBypassesTheSwitch(void)
{
    resetLog();
    log.Enabled = 0;
    log.DebugViewEnabled = 1;

    appendOne("an ordinary producer", 1, 1);
    CHECK_EQ(log.Used, 0, "at level 0 an ordinary append produces nothing");
    CHECK_EQ(log.Suppressed, 1, "and is suppressed");

    log.Publishing = 1;
    appendOne("the counter block", 2, 1);
    CHECK(log.Used > 0, "but the counter block reaches the ring");
    CHECK_EQ(log.Suppressed, 1, "and is not counted as suppressed");

    log.Publishing = 0;
    appendOne("an ordinary producer again", 3, 1);
    CHECK_EQ(log.Suppressed, 2, "the bypass ends when the flag is lowered");
    checkAppendIdentity(0, "publishing bypass");
}

/*
 * A failed hand-over must **not** leave the bytes in the ring. A sink that
 * took nothing would otherwise turn a bounded ring into a permanently full one
 * that drops every later record - which is the state a user would be trying to
 * diagnose. *(Named `testFailedWriteEmptiesTheRing` and worded around "the file
 * cannot be created" until the post-Phase 13 review rounds; the file sink left and the
 * property is the ring's, not any sink's.)*
 */
static void testFailedHandoverEmptiesTheRing(void)
{
    static UCHAR out[XHCI_LOG_RING_BYTES];
    ULONG n;

    resetLog();
    appendOne("before", 1, 1);
    CHECK_EQ(XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP),
             XHCI_LOG_FLUSH_GO, "flush goes");
    n = drainAll(out, sizeof(out), 256);
    CHECK(n > 0, "bytes were handed to the writer");

    /* A hand-over that took nothing reports 0 bytes taken - `bytes` is what
     * landed, not what was drained and offered. The surviving sink cannot
     * report a failure, but the accounting still has to be right for one:
     * a sink that can fail is the kind of thing this driver has had before. */
    XhciLogFlushEnd(&log, 0, 0);
    CHECK_EQ(log.Flushes, 0, "a failed hand-over is not a flush");
    CHECK_EQ(log.FlushFailures, 1, "it is a failure");
    CHECK_EQ(log.FlushBytes, 0, "and moved no bytes");
    CHECK_EQ(log.Used, 0, "the ring is empty whatever the sink said");

    /* The next record still fits, which is the property that matters. */
    appendOne("after", 2, 1);
    CHECK(log.Used > 0, "a later record is still accepted");
    CHECK_EQ(log.BytesDropped, 0, "and nothing was dropped to make room");
    checkAppendIdentity(3, "failed write");

    /* A successful one moves both counters. */
    resetLog();
    appendOne("ok", 1, 1);
    (void)XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP);
    n = drainInto(out, sizeof(out));
    XhciLogFlushEnd(&log, n, 1);
    CHECK_EQ(log.Flushes, 1, "a successful hand-over is a flush");
    CHECK_EQ(log.FlushBytes, n, "and its bytes are recorded");
    CHECK_EQ(log.FlushFailures, 0, "with no failure");
    checkAppendIdentity(3, "successful write");

    /* A SHORT hand-over is the third outcome, and it is neither of the first
     * two: a failure whose bytes nonetheless reached the sink. Counting them is
     * what keeps FlushBytes comparable with what was OFFERED in this controller
     * lifetime - not with whatever the sink accumulates, which outlives the
     * counter. Reporting 0 here
     * would describe a truncated log as an absent one, and reporting `n` would
     * describe it as a whole one. */
    resetLog();
    appendOne("short", 1, 1);
    (void)XhciLogFlushBegin(&log, XHCI_LOG_REASON_STOP);
    n = drainInto(out, sizeof(out));
    CHECK(n > 8, "the vector needs more bytes than the writer will take");
    XhciLogFlushEnd(&log, 8, 0);
    CHECK_EQ(log.Flushes, 0, "a short hand-over is not a completed flush");
    CHECK_EQ(log.FlushFailures, 1, "it is a failure");
    CHECK_EQ(log.FlushBytes, 8, "and exactly what landed is recorded");
    checkAppendIdentity(3, "short write");
}

/* NULL arguments answer rather than fault. Every one of these is unreachable
 * from src/ today; they are here because this is a public surface. */
static void testNullSafety(void)
{
    UCHAR out[16];

    XhciLogAppend(NULL, "x", 0, 1);
    CHECK_EQ(XhciLogFlushBegin(NULL, XHCI_LOG_REASON_STOP),
             XHCI_LOG_FLUSH_DISABLED, "a NULL log cannot flush");
    CHECK_EQ(XhciLogDrain(NULL, out, sizeof(out)), 0, "a NULL log drains 0");
    XhciLogFlushRefusedIrql(NULL);
    XhciLogFlushEnd(NULL, 1, 1);
    CHECK_EQ(XhciLogApplySwitches(NULL, 3, 1), 0,
             "a NULL log applies nothing");

    resetLog();
    appendOne(NULL, 0, 1);
    CHECK_EQ(log.Appends, 0, "a NULL label appends nothing");
    CHECK_EQ(log.Suppressed, 0, "and is not a suppression either");
    CHECK_EQ(XhciLogDrain(&log, NULL, 16), 0, "a NULL buffer drains 0");
    CHECK_EQ(XhciLogDrain(&log, out, 0), 0, "a zero capacity drains 0");
    /* The one call this file makes that legitimately increments neither
     * counter, so the net is told about it. */
    appendCalls--;
}


/* ------------------------------------------------------------------ */
/* The two switches (roadmap task 13-L.2, as amended)                   */
/* ------------------------------------------------------------------ */

/*
 * **The vector this whole task exists for.** `Enabled` used to be
 * `FileEnabled || DebugViewEnabled`, so naming a sink was what switched
 * recording on - and on Windows 98 neither sink can honestly be selected, which
 * left the ring empty on the one operating system it was built for. Here the
 * ring fills at XHCI_LOG_VERBOSITY_RING with **no sink selected at all**, and
 * that property cannot be checked anywhere else: on a guest it is invisible,
 * and on metal it needs a bench.
 */
static void testVerbosityIsTheRecordingSwitch(void)
{
    resetLog();
    log.Enabled = 0;

    CHECK_EQ(XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_OFF, 0), 0,
             "level 0 records nothing");
    CHECK_EQ(log.Enabled, 0, "and Enabled says so");
    appendOne("at level zero", 1, 1);
    CHECK_EQ(log.Used, 0, "so an append at level 0 produces no bytes");
    CHECK_EQ(log.Suppressed, 1, "and is counted as suppressed");

    /*
     * **The rung the snapshot-value merge exists to preserve.** The channel is
     * engaged here and the ring is still off, which is the configuration a
     * naive merge - "1 = ring on" - would have deleted, and the one a bench
     * wants while the append sites' cost on Windows 98 metal is unmeasured.
     */
    resetLog();
    log.Enabled = 0;
    CHECK_EQ(XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_COUNTERS, 0), 0,
             "the counter rung engages the channel and records nothing");
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_COUNTERS,
             "and the level itself is applied");
    appendOne("at the counter rung", 2, 1);
    CHECK_EQ(log.Used, 0, "an append there produces no bytes either");
    CHECK_EQ(log.Suppressed, 1, "and is counted as suppressed");

    /* No sink is named anywhere in this call. That is the point of it. */
    CHECK_EQ(XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_RING, 0), 1,
             "the ring rung records, with no sink selected at all");
    CHECK_EQ(log.DebugViewEnabled, 0, "and no sink IS selected");
    appendOne("at the ring rung", 3, 1);
    CHECK(log.Used > 0, "so an append there produces bytes");

    /* And the sink on its own does NOT switch recording on, which is the
     * inverse of the old defect and just as important: a user who set only
     * XhciLogDebugView must not get a ring they did not ask for. */
    resetLog();
    log.Enabled = 0;
    CHECK_EQ(XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_OFF, 1), 0,
             "a sink alone does not switch recording on");
    CHECK_EQ(log.DebugViewEnabled, 1, "though the sink itself is on");
    appendOne("sink only", 4, 1);
    CHECK_EQ(log.Used, 0, "and the ring stays empty");
    checkAppendIdentity(0, "verbosity is the recording switch");
}

/*
 * Every level, and the three that record. The ladder is a superset chain, so
 * the only thing `Enabled` may do across it is switch on once and stay on.
 */
static void testVerbosityLadder(void)
{
    ULONG level;

    for (level = 0; level <= XHCI_LOG_VERBOSITY_MAX; level++) {
        resetLog();
        log.Enabled = 0;
        (void)XhciLogApplySwitches(&log, level, 0);
        CHECK_EQ(log.Verbosity, level, "the level is applied");
        CHECK_EQ(log.VerbosityRead, level, "and is what was read");
        CHECK_EQ(log.VerbosityRefused, 0, "an in-range level is not refused");
        check_eq_impl(log.Enabled,
                      (level >= XHCI_LOG_VERBOSITY_RING) ? 1UL : 0UL,
                      "recording is on at the ring rung and above", __LINE__);
    }
    /*
     * **The top of the ladder is asserted rather than assumed.** A merge that
     * shifted every rung and left `_MAX` behind would give a driver that
     * refuses the level its own tool offers, and every loop above would still
     * pass because they all run to `_MAX`.
     */
    CHECK_EQ(XHCI_LOG_VERBOSITY_MAX, XHCI_LOG_VERBOSITY_FULL,
             "the ladder's top rung is its maximum");
    CHECK_EQ(XHCI_LOG_VERBOSITY_MAX, 4,
             "and the range is 0-4 since the snapshot-value merge");
    CHECK_EQ(XHCI_LOG_VERBOSITY_DEFAULT, XHCI_LOG_VERBOSITY_OFF,
             "the default is OFF, so a refusal shuts the channel");
    checkAppendIdentity(0, "verbosity ladder");
}

/*
 * **Refused, not clamped**, and the refusal is recorded. A value of 7 is not
 * "the highest level" - it is a value this driver does not understand, and
 * treating it as the top would put kernel addresses in a dump on the strength
 * of a typo. Both numbers survive, so a reader can always tell which happened.
 */
static void testVerbosityOutOfRangeIsRefused(void)
{
    resetLog();
    log.Enabled = 0;
    (void)XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_MAX + 1, 0);
    CHECK_EQ(log.VerbosityRefused, 1, "one above the top is refused");
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_DEFAULT,
             "and the default is applied");
    CHECK_EQ(log.VerbosityRead, XHCI_LOG_VERBOSITY_MAX + 1,
             "while what was read survives");
    CHECK_EQ(log.Enabled, 0, "so a refused level does not record");
    /*
     * **And it does not open the channel either**, which is new since the
     * merge and is the direction a refusal must fail in: the fallback is
     * XHCI_LOG_VERBOSITY_OFF, so a mistyped level leaves the door shut rather
     * than opening it at some rung nobody asked for.
     */
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_OFF,
             "a refused level leaves the read channel shut");

    resetLog();
    log.Enabled = 0;
    (void)XhciLogApplySwitches(&log, 0xFFFFFFFFUL, 0);
    CHECK_EQ(log.VerbosityRefused, 1, "and so is every value above the top");
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_DEFAULT, "same fallback");
    CHECK_EQ(log.VerbosityRead, 0xFFFFFFFFUL, "same record of what was read");

    /* The refusal must not stick: a later start with a good value clears it,
     * because usbport zeroes the extension between starts and a stale flag
     * would describe the previous boot. */
    (void)XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_RING, 0);
    CHECK_EQ(log.VerbosityRefused, 0, "a good value clears the refusal");
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_RING, "and applies");
    checkAppendIdentity(0, "verbosity out of range");
}

/*
 * **The read channel's consent is rung 0 of the same ladder** since `0.0.0.6`,
 * and the property that has to survive the merge is that consent and recording
 * are still separately observable: rung 1 consents without recording, and the
 * DebugView sink moves neither.
 *
 * *(This vector was `testSnapshotSwitchIsIndependent` and asserted that the
 * highest level did NOT open the channel, which was the whole content of the
 * separate value. Its subject survives inverted: the level is now the only
 * thing that opens it, and what must stay independent is the sink.)*
 */
static void testChannelConsentIsRungZero(void)
{
    ULONG level;

    resetLog();
    log.Enabled = 0;

    for (level = 0; level <= XHCI_LOG_VERBOSITY_MAX; level++) {
        (void)XhciLogApplySwitches(&log, level, 0);
        /* `Verbosity == OFF` is exactly what `xhciPassThru` tests, so this is
         * the pure-core half of the driver's gate rather than a restatement of
         * the level. */
        check_eq_impl((log.Verbosity == XHCI_LOG_VERBOSITY_OFF) ? 1UL : 0UL,
                      (level == 0) ? 1UL : 0UL,
                      "the channel is shut at rung 0 and open above it",
                      __LINE__);
    }

    /* The sink does not open the channel, and the channel does not select a
     * sink. Two switches, two subjects - which is what the merge kept. */
    (void)XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_OFF, 1);
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_OFF,
             "a sink alone does not open the channel");
    CHECK_EQ(log.DebugViewEnabled, 1, "though the sink itself is on");

    (void)XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_COUNTERS, 0);
    CHECK_EQ(log.DebugViewEnabled, 0, "and the channel does not select a sink");
    CHECK_EQ(log.Enabled, 0, "nor switch recording on at the counter rung");
    checkAppendIdentity(0, "channel consent");
}

/*
 * **A kernel address is a top-rung record, and the driver is what enforces
 * that.** The ladder's boundary between 3 and 4 is addresses, and it is a
 * PUBLICATION line: `XHCISNAP`'s plain-text companion is built out of the ring
 * and is the file a stranger pastes into a public issue. Two records written as
 * soon as recording was on - the `USBPORT_RESOURCES` pointer and the mapped
 * register base - carried exactly what that boundary is drawn around, while the
 * release notes and the generated readme both promised there were none. A Codex
 * review of commit `85c0072` found the documents and the code disagreeing; this
 * vector is what makes the code the one that is right.
 *
 * *(The boundary was between 2 and 3 until the snapshot-value merge shifted every
 * rung by one. The behaviour asserted here is unchanged under a new number,
 * which is exactly why it is written against the constants.)*
 */
static void testAddressRecordsNeedTheTopRung(void)
{
    ULONG level;

    for (level = 0; level <= XHCI_LOG_VERBOSITY_MAX; level++) {
        resetLog();
        log.Enabled = 0;
        (void)XhciLogApplySwitches(&log, level, 0);

        XhciLogAppendAddress(&log, "start", 0x8054C000UL);
        appendCalls++;

        if (level >= XHCI_LOG_VERBOSITY_FULL) {
            CHECK(log.Used > 0, "the top rung records an address");
            CHECK_EQ(log.Appends, 1, "as an ordinary append");
            CHECK_EQ(log.Suppressed, 0, "and not as a suppression");
        } else {
            check_eq_impl(log.Used, 0,
                          "no level below the top records an address",
                          __LINE__);
            check_eq_impl(log.Appends, 0, "nothing was appended", __LINE__);
            check_eq_impl(log.Suppressed, 1,
                          "and the refusal is accounted for", __LINE__);
        }
        checkAppendIdentity(0, "address tier");
    }

    /*
     * **`Publishing` does not open it either**, and that is worth pinning: the
     * flush raises that flag to get the counter block past the recording
     * switch, and a bypass that also let addresses through would move the
     * boundary without anyone choosing to.
     */
    resetLog();
    log.Enabled = 0;
    (void)XhciLogApplySwitches(&log, XHCI_LOG_VERBOSITY_RING, 0);
    log.Publishing = 1;
    XhciLogAppendAddress(&log, "start", 0x8054C000UL);
    appendCalls++;
    log.Publishing = 0;
    CHECK_EQ(log.Used, 0, "the flush's bypass does not open the address tier");
    CHECK_EQ(log.Suppressed, 1, "the refusal still counts");

    XhciLogAppendAddress(NULL, "x", 1);
    checkAppendIdentity(0, "address tier under Publishing");
}

/*
 * **An absent value is 0 and is not an error.** The driver cannot tell absent
 * from unreadable from a real zero - usbport collapses every registry failure
 * to one code - and it does not need to, because all three mean off. What has
 * to survive is the distinction for a *reader*, and the two `SwitchStatus`
 * fields are what carry it: SUCCESS beside a value of 0 is a zero somebody set,
 * any other status beside 0 is nothing found.
 *
 * **This test assigns `SwitchRead` by hand and does not exercise the reader**,
 * so it says nothing about that field's meaning; `test_init.c`'s
 * `test_log_two_values` drives the real path and shows even the
 * missing-service case leaving it at 1. *(This comment credited `SwitchRead`
 * with the distinction until the post-Phase 13 review rounds.)*
 */
static void testAbsentValuesAreOff(void)
{
    resetLog();
    log.Enabled = 0;

    /* Two zeroes is what a machine whose INF never ran produces, and what a
     * packet with a NULL registry service produces, and what a machine whose
     * owner set nothing produces. All three start normally with everything
     * off. */
    CHECK_EQ(XhciLogApplySwitches(&log, 0, 0), 0, "everything off");
    CHECK_EQ(log.Verbosity, XHCI_LOG_VERBOSITY_OFF,
             "no read channel and no recording - rung 0 is both");
    CHECK_EQ(log.VerbosityRefused, 0, "and absent is not a refusal");
    CHECK_EQ(log.DebugViewEnabled, 0, "no sink");

    /* SwitchRead is not this function's to set - it belongs to the read, which
     * is next door in xhci_dispatch.c - so applying switches must leave it
     * exactly as it was. That is what makes "the driver read nothing" a true
     * statement in a dump rather than an inference. */
    CHECK_EQ(log.SwitchRead, 0, "applying switches does not claim a read");
    log.SwitchRead = 1;
    (void)XhciLogApplySwitches(&log, 0, 0);
    CHECK_EQ(log.SwitchRead, 1, "and does not clear one either");
    checkAppendIdentity(0, "absent values");
}

static void testErrorBudget(void)
{
    XHCI_LOG log;
    ULONG i;
    ULONG allowed;

    memset(&log, 0, sizeof(log));
    allowed = 0;
    for (i = 0; i < XHCI_LOG_ERROR_BUDGET + 6; i++) {
        allowed += XhciLogErrorBudget(&log, 6);
    }
    CHECK_EQ(allowed, XHCI_LOG_ERROR_BUDGET,
             "a code is worth exactly its budget in records");

    CHECK_EQ(XhciLogErrorBudget(&log, 7), 1,
             "and a different code still has its own");

    /* The vendor-defined ranges fold onto one slot rather than getting 64
     * budgets or being dropped. */
    memset(&log, 0, sizeof(log));
    allowed = 0;
    for (i = 0; i < XHCI_LOG_ERROR_BUDGET + 2; i++) {
        allowed += XhciLogErrorBudget(&log, 192 + i);
    }
    CHECK_EQ(allowed, XHCI_LOG_ERROR_BUDGET,
             "every vendor code shares one budget, so a controller inventing "
             "codes is one class of surprise and not sixty-four");

    CHECK_EQ(XhciLogErrorBudget(NULL, 6), 0, "a NULL log records nothing");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    testHex32();
    testRecordShape();
    testSwitchOff();
    testTruncation();
    testWrapKeepsTheNewest();
    testChunkedDrainIsFifo();
    testDrainOrderAcrossWrap();
    testFlushDecision();
    testPublishingBypassesTheSwitch();
    testFailedHandoverEmptiesTheRing();
    testNullSafety();
    testVerbosityIsTheRecordingSwitch();
    testVerbosityLadder();
    testVerbosityOutOfRangeIsRefused();
    testChannelConsentIsRungZero();
    testAbsentValuesAreOff();
    testAddressRecordsNeedTheTopRung();
    testErrorBudget();

    /*
     * The never-reset twins. An identity that never saw a call passes as a net
     * over nothing, which is exactly the shape task 7b-A.1.0's sweep found
     * scoring zero failures.
     */
    CHECK(appendCallsEver > 100, "the append identity measured real appends");
    CHECK(drainedEver > XHCI_LOG_RING_BYTES,
          "the drain accounting measured a full ring's worth and more");

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
