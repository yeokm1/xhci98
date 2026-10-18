/*
 * test_quirks.c - host-side unit tests for xhciqual/quirks.c, the quirk
 * table and its "quirks:" lines (roadmap-hcd task 38.10, design record 20
 * section 6.5).
 *
 * The expected ids are record 20 section 3.1's "Controllers" column, quirk by
 * quirk, with the Linux flag and the driver's registry value each must print.
 * Also: the vendor-wide rows (VIA 1106, Zhaoxin 1D17, AMD 1022) match every
 * device of their vendor and are never what quirk_find() returns; at most one
 * per vendor; no flag on both an exact row and its vendor's row (it would
 * print twice); no duplicate exact rows; the neighbours of a listed id and
 * the same device under another vendor do not match; and the HCIVERSION
 * floor note appears for a listed part below 0.96 only.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework, no DOS dependencies.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../qual.h"

static char out[8192];
static int failures;
static int checks;

static void cap(const char *fmt, ...)
{
    va_list ap;
    size_t used;

    used = strlen(out);
    va_start(ap, fmt);
    vsprintf(out + used, fmt, ap);
    va_end(ap);
}

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL line %d: %s\n", line, what);
        printf("  output was: %s", out[0] ? out : "(empty)\n");
    }
}

static int has(const char *needle)
{
    return strstr(out, needle) != 0;
}

/* Every line fits the 80-column DOS screen (79 characters and the newline),
 * since the console pager counts rows by newlines. */
static int lines_fit(void)
{
    const char *s = out;
    int col = 0;

    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            col = 0;
        } else if (++col > 79) {
            return 0;
        }
    }
    return 1;
}

static void run(u16 vid, u16 did, int hciver_ok, u16 hciversion)
{
    out[0] = '\0';
    quirk_report(vid, did, hciver_ok, hciversion, cap);
    CHECK(lines_fit(), "every quirks line fits 79 columns");
}

/* One quirk taken by the driver: its flag, the value and Linux flag the
 * line must name, and its ids (record 20 section 3.1). An id with device
 * QUIRK_ANY_DID is a vendor-wide entry. */
typedef struct {
    u32 flag;
    const char *value;
    const char *linux_flag;
    u16 ids[10][2];
    int nids;
} TAKEN;

static const TAKEN taken[] = {
    { QF_BROKEN_MSI, "XhciMsiAvoid", "XHCI_BROKEN_MSI",
      { { 0x1B73, 0x1000 }, { 0x1B73, 0x1400 } }, 2 },
    { QF_STREAMS_OFF, "XhciStreamsOff", "XHCI_BROKEN_STREAMS",
      { { 0x1B73, 0x1000 }, { 0x1B73, 0x1009 }, { 0x1B6F, 0x7023 },
        { 0x1B6F, 0x7052 }, { 0x1106, 0x3432 }, { 0x1B21, 0x1042 },
        { 0x17CD, 0x0200 } }, 7 },
    { QF_RESUME_REINIT, "XhciResumeReinit", "XHCI_RESET_ON_RESUME",
      { { 0x1022, 0x15E5 }, { 0x1B6F, 0x7023 }, { 0x1B6F, 0x7052 },
        { 0x1912, 0x0015 }, { 0x1106, QUIRK_ANY_DID }, { 0x1DB7, 0xDC27 },
        { 0x1B21, 0x3042 }, { 0x1D17, 0x9202 } }, 8 },
    { QF_SHUTDOWN_EHCI, "XhciIntelShutdownToEhci", "XHCI_SPURIOUS_REBOOT",
      { { 0x8086, 0x1E31 }, { 0x8086, 0x9C31 }, { 0x8086, 0x9CB1 } }, 3 },
    { QF_SHUTDOWN_RESET, "XhciShutdownReset", "XHCI_SPURIOUS_WAKEUP",
      { { 0x8086, 0x9C31 }, { 0x8086, 0x9CB1 } }, 2 },
    { QF_SUSPEND_DELAY, "XhciSuspendHaltDelay", "XHCI_SUSPEND_DELAY",
      { { 0x1022, 0x145C }, { 0x1022, 0x15E0 }, { 0x1022, 0x15E1 },
        { 0x1022, 0x43BB } }, 4 },
    { QF_SLOW_SUSPEND, "XhciSuspendHaltTimeout", "XHCI_SLOW_SUSPEND",
      { { 0x1B73, 0x1000 } }, 1 },
    { QF_ASM_FLOWCTL, "XhciAsmediaFlowControl",
      "XHCI_ASMEDIA_MODIFY_FLOWCONTROL",
      { { 0x1B21, 0x1142 } }, 1 },
    { QF_INTERVAL7, "XhciTiIntervalCap", "XHCI_LIMIT_ENDPOINT_INTERVAL_7",
      { { 0x104C, 0x8241 } }, 1 },
    { QF_INTERVAL9, "XhciIntervalCap", "XHCI_LIMIT_ENDPOINT_INTERVAL_9",
      { { 0x1002, 0x7316 } }, 1 },
    { QF_INTERVAL_VENDOR, "XhciIntervalCap",
      "XHCI_LIMIT_ENDPOINT_INTERVAL_9",
      { { 0x1022, QUIRK_ANY_DID } }, 1 },
    { QF_STUCK_LINK, "XhciStuckLinkReset", "XHCI_MISSING_CAS",
      { { 0x8086, 0x22B5 }, { 0x8086, 0x9D2F }, { 0x8086, 0xA12F },
        { 0x8086, 0x5AA8 }, { 0x8086, 0x19D0 } }, 5 },
    { QF_RING_PAD, "XhciStreamRingPad", "XHCI_TRB_OVERFETCH",
      { { 0x1106, 0x3483 }, { 0x1D17, 0x9202 }, { 0x1D17, 0x9203 } }, 3 },
    { QF_CTRL_NOOP, "XhciControlLinkNoOp", "XHCI_ETRON_HOST",
      { { 0x1B6F, 0x7023 }, { 0x1B6F, 0x7052 } }, 2 },
    { QF_ISOCH_SPURIOUS, "XhciIsochSpuriousSuccess", "XHCI_ETRON_HOST",
      { { 0x1B6F, 0x7023 }, { 0x1B6F, 0x7052 } }, 2 },
    { QF_SPARSE_CLEAR, "XhciAmdSuspendBitClear", "XHCI_DISABLE_SPARSE",
      { { 0x1022, 0x15E5 } }, 1 }
};

#define NTAKEN ((int)(sizeof(taken) / sizeof(taken[0])))

/* The row an id names: the exact row, or for QUIRK_ANY_DID the vendor's. */
static const QUIRK *row_of(u16 vid, u16 did)
{
    return did == QUIRK_ANY_DID ? quirk_find_vendor(vid) : quirk_find(vid, did);
}

static int listed(const TAKEN *t, u16 vid, u16 did)
{
    int i;

    for (i = 0; i < t->nids; i++) {
        if (t->ids[i][0] == vid && t->ids[i][1] == did)
            return 1;
    }
    return 0;
}

static void test_taken_lists(void)
{
    int t, i;
    const QUIRK *q;
    char what[160];

    for (t = 0; t < NTAKEN; t++) {
        const TAKEN *k = &taken[t];

        /* Every listed id carries the flag and prints the value and the
         * Linux flag. A vendor-wide id is run as a device of that vendor no
         * row names (0x0001). */
        for (i = 0; i < k->nids; i++) {
            u16 vid = k->ids[i][0];
            u16 did = k->ids[i][1];

            q = row_of(vid, did);
            sprintf(what, "%s: %04X:%04X has a row with the flag",
                    k->value, vid, did);
            CHECK(q != 0 && (q->flags & k->flag) != 0, what);
            run(vid, did == QUIRK_ANY_DID ? 0x0001 : did, 1, 0x0100);
            sprintf(what, "%s: %04X:%04X prints the value and %s",
                    k->value, vid, did, k->linux_flag);
            CHECK(has(k->value) && has(k->linux_flag), what);
        }

        /* And no row outside the list carries it: the list is Linux's,
         * exactly. */
        for (i = 0; (q = quirk_row(i)) != 0; i++) {
            if ((q->flags & k->flag) == 0)
                continue;
            sprintf(what, "%s: row %04X:%04X carries it but is not listed",
                    k->value, q->vid, q->did);
            CHECK(listed(k, q->vid, q->did), what);
        }
    }
}

static void test_rows(void)
{
    int i, j;
    const QUIRK *q, *r;
    char what[160];

    for (i = 0; (q = quirk_row(i)) != 0; i++) {
        /* Every row's lines, with the floor note, through the width check. */
        run(q->vid, q->did == QUIRK_ANY_DID ? 0x0001 : q->did, 1, 0x0095);
        sprintf(what, "row %04X:%04X prints its name", q->vid, q->did);
        CHECK(has(q->name), what);
        for (j = i + 1; (r = quirk_row(j)) != 0; j++) {
            sprintf(what, "row %04X:%04X appears once", q->vid, q->did);
            CHECK(!(q->vid == r->vid && q->did == r->did), what);
        }
        if (q->did == QUIRK_ANY_DID) {
            sprintf(what, "quirk_find never returns vendor row %04X",
                    q->vid);
            CHECK(quirk_find(q->vid, QUIRK_ANY_DID) == 0, what);
            CHECK(quirk_find_vendor(q->vid) == q, "vendor row is found");
        } else {
            const QUIRK *v = quirk_find_vendor(q->vid);

            sprintf(what, "row %04X:%04X is what quirk_find returns",
                    q->vid, q->did);
            CHECK(quirk_find(q->vid, q->did) == q, what);
            if (v != 0) {
                sprintf(what, "row %04X:%04X shares no flag with its "
                        "vendor's row", q->vid, q->did);
                CHECK((q->flags & v->flags) == 0, what);
            }
        }
    }
    CHECK(quirk_find_vendor(0x1106) != 0, "VIA has a vendor-wide row");
    CHECK(quirk_find_vendor(0x1D17) != 0, "Zhaoxin has a vendor-wide row");
    CHECK(quirk_find_vendor(0x1022) != 0, "AMD has a vendor-wide row");
    CHECK(quirk_find_vendor(0x8086) == 0, "Intel has no vendor-wide row "
          "(C7's 'not in the quirk table' note keys on exact rows)");
}

static void test_matching(void)
{
    /* Neighbours and other vendors' same device numbers. */
    CHECK(quirk_find(0x1B73, 0x1001) == 0, "1B73:1001 is not listed");
    CHECK(quirk_find(0x1B73, 0x0FFF) == 0, "1B73:0FFF is not listed");
    CHECK(quirk_find(0x104C, 0x8240) == 0, "104C:8240 is not listed");
    CHECK(quirk_find(0x104C, 0x8242) == 0, "104C:8242 is not listed");
    CHECK(quirk_find(0x1022, 0x7316) == 0, "1022:7316 is not listed");
    CHECK(quirk_find(0x1002, 0x7317) == 0, "1002:7317 is not listed");
    CHECK(quirk_find(0x8087, 0x1E31) == 0, "8087:1E31 is not listed");
    CHECK(quirk_find(0x1D18, 0x9202) == 0, "1D18:9202 is not listed");
    CHECK(quirk_find(0x1D17, 0x9201) == 0, "1D17:9201 has no exact row");
    CHECK(quirk_find_vendor(0x1107) == 0, "1107 has no vendor-wide row");
    CHECK(quirk_find_vendor(0x1105) == 0, "1105 has no vendor-wide row");

    /* Q3's every 1106: any VIA device. */
    run(0x1106, 0x1234, 1, 0x0100);
    CHECK(has("VIA (every 1106 xHCI)") && has("XhciResumeReinit"),
          "an unlisted VIA device prints the vendor-wide resume quirk");
    CHECK(!has("none known"), "an unlisted VIA device is not 'none known'");

    /* Both rows reported, the exact one first. */
    run(0x1106, 0x3432, 1, 0x0100);
    CHECK(has("VIA Labs VL800") && has("XhciStreamsOff") &&
          has("XhciResumeReinit"), "VL800 prints both rows");
    CHECK(strstr(out, "VIA Labs VL800") < strstr(out, "VIA (every 1106"),
          "the exact row comes first");

    /* Zhaoxin: covered vendor bit, and 9202's own two. */
    run(0x1D17, 0x9202, 1, 0x0100);
    CHECK(has("XHCI_ZHAOXIN_HOST") && has("XhciStreamRingPad") &&
          has("XhciResumeReinit"), "Zhaoxin 9202 prints its rows");

    /* AMD: every 1022 id gets the interval cap's reach and Q19's note. */
    run(0x1022, 0x43F7, 1, 0x0100);
    CHECK(has("AMD xHCI 43F7") && has("caps every AMD one") &&
          has("XHCI_AMD_PLL_FIX") && has("not taken"),
          "an AMD id prints the vendor-wide lines");
    run(0x1022, 0x15E5, 1, 0x0100);
    CHECK(has("XhciAmdSuspendBitClear") && has("XhciResumeReinit"),
          "Raven2 prints its quirks");

    /* Q18 named as not taken on Panther Point. */
    run(0x8086, 0x1E31, 1, 0x0100);
    CHECK(has("XHCI_SW_BW_CHECKING") && has("not taken"),
          "1E31 names XHCI_SW_BW_CHECKING as not taken");
    CHECK(has("XhciIntelShutdownToEhci"), "1E31 prints Q4");

    /* Q5's default stated. */
    run(0x8086, 0x9C31, 1, 0x0100);
    CHECK(has("XhciShutdownReset") && has("default 0, off"),
          "9C31 prints Q5 with its default off");

    /* Nothing known. */
    run(0x1234, 0x5678, 1, 0x0095);
    CHECK(has("quirks: none known for this VID/DID"), "unknown id");
    CHECK(!has("floor"), "no floor note for an unlisted part");
}

static void test_floor(void)
{
    run(0x1B21, 0x1042, 1, 0x0095);
    CHECK(has("HCIVERSION 0.95 is below xhci98's floor 0.96"),
          "a listed part at 0.95 is below the floor");
    CHECK(has("refused at every XhciAccept096"),
          "the note says no registry value starts it");
    run(0x1B21, 0x1042, 1, 0x0096);
    CHECK(!has("floor"), "ASM1042 at 0.96 is at the floor: no note");
    run(0x1B21, 0x1042, 1, 0x0099);
    CHECK(!has("floor"), "a listed part between 0.96 and 1.0: no note");
    run(0x1B21, 0x1042, 1, 0x0100);
    CHECK(!has("floor"), "a listed part at 1.00 gets no floor note");
    run(0x1B21, 0x1042, 1, 0x0110);
    CHECK(!has("floor"), "a listed part at 1.10 gets no floor note");
    run(0x1B21, 0x1042, 0, 0x0000);
    CHECK(!has("floor"), "HCIVERSION not read: nothing said of the floor");
    run(0x1106, 0x1234, 1, 0x0090);
    CHECK(has("below xhci98's floor"),
          "a vendor-wide match below the floor gets the note");
}

int main(void)
{
    test_taken_lists();
    test_rows();
    test_matching();
    test_floor();

    printf("test_quirks: %d checks, %d failed\n", checks, failures);
    return failures;
}
