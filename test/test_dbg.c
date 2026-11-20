/*
 * test_dbg.c - the qemu flavour's trace line builder (src/xhci_dbg.c).
 *
 * The file is compiled here as the qemu flavour compiles it, with DBG and
 * XHCI_DBG_LIVE, but without XHCI_DBG_E9: the port writes and their line
 * flag are the guest's to show (docs/contributing/build-and-test.md, "Whole
 * lines on the debug console"), and what is driven here is what decides the
 * bytes - the four line builders, the split of a long line and its
 * continuation mark. Each finished physical line reaches XhciDbgHostSink
 * below instead of DbgPrint.
 *
 * Three properties are held:
 *
 *   - a line that fits (the prefix and the text at most 126 characters) is
 *     byte for byte what the builder before the split made, which is
 *     reproduced here as refLine, for every length that fits and for each
 *     of the four entry points;
 *   - a longer line is never cut short: it comes out as pieces of at most
 *     127 bytes with their newline, each beginning with the prefix, every
 *     piece but the last ending in the mark, every piece but the first
 *     beginning with the prefix and the mark, and the pieces, marks taken
 *     off, are the whole line;
 *   - every physical line ends in exactly one newline and holds no other;
 *   - a word dump, for every label length across the split and every count
 *     from none to nine words, is row for row what a formatter of its own
 *     makes: a row that fits byte for byte, a longer one in pieces.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#define DBG 1
#define XHCI_DBG_LIVE 1

#include <stdio.h>
#include <string.h>
#include "../src/xhci_dbg.c"
#include "test_harness.h"

#define SINK_MAX 64

static char sinkLine[SINK_MAX][XHCI_DBG_LINE_MAX + 1];
static ULONG sinkLen[SINK_MAX];
static ULONG sinkCount;
static ULONG sinkOverflow;

VOID XhciDbgHostSink(const char *line, ULONG len)
{
    if (sinkCount >= SINK_MAX || len > XHCI_DBG_LINE_MAX) {
        sinkOverflow++;
        return;
    }
    memcpy(sinkLine[sinkCount], line, len);
    sinkLine[sinkCount][len] = '\0';
    sinkLen[sinkCount] = len;
    sinkCount++;
}

static void sinkReset(void)
{
    sinkCount = 0;
    sinkOverflow = 0;
}

/* ------------------------------------------------------------------ */
/* The builder before the split, as it was                             */
/* ------------------------------------------------------------------ */

static ULONG refAppend(char *buf, ULONG len, const char *s)
{
    while (*s != '\0' && len < (XHCI_DBG_LINE_MAX - 1)) {
        buf[len] = *s;
        len++;
        s++;
    }
    return len;
}

static ULONG refHex(char *buf, ULONG len, ULONG value, ULONG digits)
{
    ULONG shift;

    shift = digits * 4;
    while (shift > 0 && len < (XHCI_DBG_LINE_MAX - 1)) {
        shift -= 4;
        buf[len] = "0123456789ABCDEF"[(value >> shift) & 0xF];
        len++;
    }
    return len;
}

/* The old XhciDbgText line, or with `hasValue` its XhciDbgValue line. */
static ULONG refLine(char *buf, const char *msg, int hasValue, ULONG value)
{
    ULONG len;

    len = refAppend(buf, 0, "xhci98: ");
    len = refAppend(buf, len, msg);
    if (hasValue) {
        len = refAppend(buf, len, "=");
        len = refHex(buf, len, value, 8);
    }
    len = refAppend(buf, len, "\n");
    buf[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Checks over what the sink holds                                     */
/* ------------------------------------------------------------------ */

/* Every physical line: the prefix, one newline at its end and no other,
 * at most 127 bytes. */
static void checkPhysical(const char *what)
{
    ULONG i;
    ULONG j;
    char msg[160];

    CHECK_EQ(sinkOverflow, 0, "the sink kept every line");
    for (i = 0; i < sinkCount; i++) {
        sprintf(msg, "%s: line %lu", what, i);
        CHECK(sinkLen[i] >= 9 && sinkLen[i] <= 127, msg);
        CHECK(strncmp(sinkLine[i], "xhci98: ", 8) == 0, msg);
        CHECK(sinkLine[i][sinkLen[i] - 1] == '\n', msg);
        CHECK(strlen(sinkLine[i]) == sinkLen[i], msg);
        for (j = 0; j + 1 < sinkLen[i]; j++) {
            if (sinkLine[i][j] == '\n') {
                CHECK(0, msg);
                break;
            }
        }
        for (j = 1; j + 8 <= sinkLen[i]; j++) {
            if (strncmp(&sinkLine[i][j], "xhci98: ", 8) == 0) {
                CHECK(0, msg);
                break;
            }
        }
    }
}

/* The pieces from `first`, marks off, against the whole line `want` (its
 * newline not counted); returns the number of pieces it took. */
static ULONG checkPieces(ULONG first, const char *want, const char *what)
{
    static char got[1024];
    ULONG n;
    ULONG i;
    ULONG len;
    char msg[160];

    n = 0;
    got[0] = '\0';
    for (i = first; i < sinkCount; i++) {
        const char *t;

        sprintf(msg, "%s: piece %lu", what, n);
        t = sinkLine[i];
        len = sinkLen[i];
        if (n > 0) {
            CHECK(len >= 10 && t[8] == '\\', msg);
            t += 9;
            len -= 9;
        }
        if (len >= 2 && t[len - 2] == '\\') {
            CHECK(sinkLen[i] == 127, msg);
            strncat(got, t, len - 2);
            n++;
            continue;
        }
        strncat(got, t, len - 1);
        n++;
        break;
    }
    sprintf(msg, "%s: whole line rebuilt", what);
    CHECK(strcmp(got, want) == 0, msg);
    return n;
}

/* ------------------------------------------------------------------ */
/* Vectors                                                             */
/* ------------------------------------------------------------------ */

static char text[600];

static void makeText(ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        text[i] = (char)('a' + (i % 26));
        if (i % 7 == 6) {
            text[i] = ' ';
        }
    }
    text[n] = '\0';
}

static void testTextAndValue(void)
{
    char ref[XHCI_DBG_LINE_MAX + 1];
    char want[700];
    char what[64];
    ULONG n;
    ULONG len;
    ULONG pieces;

    for (n = 0; n <= 500; n++) {
        makeText(n);

        sinkReset();
        XhciDbgText(text);
        sprintf(what, "text %lu", n);
        checkPhysical(what);
        if (8 + n <= XHCI_DBG_TEXT_MAX) {
            len = refLine(ref, text, 0, 0);
            CHECK_EQ(sinkCount, 1, what);
            CHECK(sinkLen[0] == len && strcmp(sinkLine[0], ref) == 0, what);
        } else {
            sprintf(want, "xhci98: %s", text);
            pieces = checkPieces(0, want, what);
            CHECK_EQ(pieces, sinkCount, what);
            CHECK(pieces >= 2, what);
        }

        sinkReset();
        XhciDbgValue(text, 0x1234ABCDUL);
        sprintf(what, "value %lu", n);
        checkPhysical(what);
        if (8 + n + 9 <= XHCI_DBG_TEXT_MAX) {
            len = refLine(ref, text, 1, 0x1234ABCDUL);
            CHECK_EQ(sinkCount, 1, what);
            CHECK(sinkLen[0] == len && strcmp(sinkLine[0], ref) == 0, what);
        } else {
            sprintf(want, "xhci98: %s=1234ABCD", text);
            pieces = checkPieces(0, want, what);
            CHECK_EQ(pieces, sinkCount, what);
        }
    }

    /* The split's defined point: 126 characters fit, the 127th splits
     * after the 125th. */
    makeText(118);
    sinkReset();
    XhciDbgText(text);
    CHECK_EQ(sinkCount, 1, "126 characters are one line");
    CHECK_EQ(sinkLen[0], 127, "126 characters and the newline");
    makeText(119);
    sinkReset();
    XhciDbgText(text);
    CHECK_EQ(sinkCount, 2, "127 characters are two lines");
    CHECK_EQ(sinkLen[0], 127, "the first: 125 characters, the mark, the newline");
    CHECK(sinkLine[0][125] == '\\' && sinkLine[0][126] == '\n', "the mark ends the first");
    {
        char rest[16];

        sprintf(rest, "xhci98: \\%c%c\n", text[117], text[118]);
        CHECK(strcmp(sinkLine[1], rest) == 0, "the rest: the prefix, the mark, two characters");
        CHECK(strncmp(sinkLine[0] + 8, text, 117) == 0, "the first: the text's first 117 characters");
    }
}

static void testCallback(void)
{
    char want[200];
    ULONG n;

    for (n = 0; n <= 120; n++) {
        char what[64];

        makeText(n);
        sinkReset();
        XhciDbgCallback(text, 1, 0xFFFFFFFFUL, 0x80000000UL);
        sprintf(what, "callback %lu", n);
        checkPhysical(what);
        sprintf(want, "xhci98: cb %s irql=FF a=00000001 b=FFFFFFFF c=80000000", text);
        if (strlen(want) <= XHCI_DBG_TEXT_MAX) {
            CHECK_EQ(sinkCount, 1, what);
            CHECK(strncmp(sinkLine[0], want, strlen(want)) == 0 &&
                  strcmp(sinkLine[0] + strlen(want), "\n") == 0, what);
        } else {
            CHECK_EQ(checkPieces(0, want, what), sinkCount, what);
        }
    }
}

static void testWords(void)
{
    ULONG w[9];
    ULONG count;
    ULONG i;

    for (i = 0; i < 9; i++) {
        w[i] = 0x11111111UL * (i + 1);
    }
    for (count = 0; count <= 9; count++) {
        char what[64];

        sinkReset();
        XhciDbgWords("hcd: label", w, count);
        sprintf(what, "words %lu", count);
        checkPhysical(what);
        CHECK_EQ(sinkCount, (count + 3) / 4, what);
    }
    sinkReset();
    XhciDbgWords("hcd: psw verdict val set guard / image sys", w, 6);
    CHECK_EQ(sinkCount, 2, "six words: two lines");
    CHECK(strcmp(sinkLine[0], "xhci98: hcd: psw verdict val set guard / image sys+00: "
                 "11111111 22222222 33333333 44444444\n") == 0, "words: the first line");
    CHECK(strcmp(sinkLine[1], "xhci98: hcd: psw verdict val set guard / image sys+10: "
                 "55555555 66666666\n") == 0, "words: the second line");

    sinkReset();
    XhciDbgWords("hcd: x", NULL, 4);
    CHECK_EQ(sinkCount, 1, "a NULL dump: one line");
    CHECK(strcmp(sinkLine[0], "xhci98: (null word dump)\n") == 0, "a NULL dump's line");

    /* A label long enough to split inside the words. */
    makeText(100);
    sinkReset();
    XhciDbgWords(text, w, 4);
    checkPhysical("words, long label");
    {
        char want[300];

        sprintf(want, "xhci98: %s+00: 11111111 22222222 33333333 44444444", text);
        CHECK_EQ(checkPieces(0, want, "words, long label"), 2, "words, long label: two pieces");
    }
}

/* Every label length across the split, every count from none to nine
 * words: each row against a formatter of its own, a row that fits as one
 * line byte for byte, a longer one as pieces that rebuild it. */
static void testWordsSweep(void)
{
    ULONG w[9];
    ULONG n;
    ULONG count;
    ULONG i;

    for (i = 0; i < 9; i++) {
        w[i] = 0x10203040UL + 0x0F0E0D0CUL * i;
    }
    for (n = 0; n <= 130; n++) {
        makeText(n);
        for (count = 0; count <= 9; count++) {
            char what[64];
            char want[300];
            ULONG at;
            ULONG row;
            ULONG k;

            sinkReset();
            XhciDbgWords(text, w, count);
            sprintf(what, "words sweep %lu/%lu", n, count);
            checkPhysical(what);
            at = 0;
            for (row = 0; row * 4 < count; row++) {
                sprintf(want, "xhci98: %s+%02lX:", text, (row * 16) & 0xFF);
                for (k = row * 4; k < count && k < row * 4 + 4; k++) {
                    sprintf(want + strlen(want), " %08lX", w[k]);
                }
                if (at >= sinkCount) {
                    CHECK(0, what);
                    break;
                }
                if (strlen(want) <= XHCI_DBG_TEXT_MAX) {
                    CHECK(strncmp(sinkLine[at], want, strlen(want)) == 0 &&
                          strcmp(sinkLine[at] + strlen(want), "\n") == 0, what);
                    at++;
                } else {
                    at += checkPieces(at, want, what);
                }
            }
            CHECK_EQ(at, sinkCount, what);
        }
    }
}

int main(void)
{
    testTextAndValue();
    testCallback();
    testWords();
    testWordsSweep();
    printf("test_dbg: %d checks, %d failures\n", checks, failures);
    return failures;
}
