/*
 * test_stamp.c - host-side unit tests for xhciqual/stamp.c, the 8.3
 * date-and-time name of a field-report log (roadmap-hcd task 38.1).
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework, no DOS dependencies.
 */

#include <stdio.h>
#include <string.h>
#include "../stamp.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL line %d: %s\n", line, what);
    }
}

/* The directory: a list of names that exist, and a count of the asks. */
#define MAX_TAKEN 32
static char taken[MAX_TAKEN][STAMP_NAME_MAX];
static int ntaken;
static int asks;
static char last_ask[64];

static void dir_clear(void)
{
    ntaken = 0;
    asks = 0;
    last_ask[0] = '\0';
}

static void dir_add(const char *name)
{
    if (ntaken < MAX_TAKEN) {
        strcpy(taken[ntaken], name);
        ntaken++;
    }
}

static int fake_exists(const char *name, void *ctx)
{
    int i;

    (void)ctx;
    asks++;
    if (strlen(name) < sizeof(last_ask))
        strcpy(last_ask, name);
    for (i = 0; i < ntaken; i++) {
        if (strcmp(taken[i], name) == 0)
            return 1;
    }
    return 0;
}

static int name_is(unsigned mo, unsigned d, unsigned h, unsigned mi,
                   const char *want)
{
    char out[STAMP_NAME_MAX];

    if (stamp_name(mo, d, h, mi, "LOG", fake_exists, 0, out) != STAMP_OK)
        return 0;
    return strcmp(out, want) == 0;
}

static void test_months(void)
{
    dir_clear();
    CHECK(name_is(1, 5, 9, 7, "1050907A.LOG"), "January is 1");
    CHECK(name_is(9, 30, 23, 59, "9302359A.LOG"), "September is 9");
    CHECK(name_is(10, 13, 14, 25, "A131425A.LOG"), "October is A");
    CHECK(name_is(11, 1, 12, 0, "B011200A.LOG"), "November is B");
    CHECK(name_is(12, 31, 23, 59, "C312359A.LOG"), "December is C");
}

static void test_midnight(void)
{
    dir_clear();
    CHECK(name_is(1, 1, 0, 0, "1010000A.LOG"), "midnight is 0000");
    CHECK(name_is(6, 15, 0, 1, "6150001A.LOG"), "a minute past midnight");
}

static void test_next_letter(void)
{
    char out[STAMP_NAME_MAX];
    int rc;

    dir_clear();
    dir_add("A131425A.LOG");
    rc = stamp_name(10, 13, 14, 25, "LOG", fake_exists, 0, out);
    CHECK(rc == STAMP_OK && strcmp(out, "A131425B.LOG") == 0,
          "A taken gives B");
    CHECK(asks == 2, "two names asked about");

    dir_add("A131425B.LOG");
    dir_add("A131425D.LOG");
    rc = stamp_name(10, 13, 14, 25, "LOG", fake_exists, 0, out);
    CHECK(rc == STAMP_OK && strcmp(out, "A131425C.LOG") == 0,
          "the first free letter, not the one past the highest");

    /* Another minute's names are not this minute's. */
    dir_clear();
    dir_add("A131424A.LOG");
    dir_add("A131425A.TXT");
    CHECK(name_is(10, 13, 14, 25, "A131425A.LOG"),
          "another minute, or another extension, takes nothing");

    /* The extension is the caller's. */
    dir_clear();
    rc = stamp_name(10, 13, 14, 25, "TXT", fake_exists, 0, out);
    CHECK(rc == STAMP_OK && strcmp(out, "A131425A.TXT") == 0,
          "extension passed through");
    rc = stamp_name(10, 13, 14, 25, "D", fake_exists, 0, out);
    CHECK(rc == STAMP_OK && strcmp(out, "A131425A.D") == 0,
          "one-character extension");
}

static void test_all_taken(void)
{
    char out[STAMP_NAME_MAX];
    char name[STAMP_NAME_MAX];
    char c;
    int rc;

    dir_clear();
    for (c = 'A'; c <= 'Z'; c++) {
        sprintf(name, "C312359%c.LOG", c);
        dir_add(name);
    }
    strcpy(out, "garbage");
    rc = stamp_name(12, 31, 23, 59, "LOG", fake_exists, 0, out);
    CHECK(rc == STAMP_ALL_TAKEN, "all 26 taken is refused");
    CHECK(out[0] == '\0', "no name handed back on refusal");
    CHECK(asks == 26, "every letter asked about, none past Z");
    CHECK(strcmp(last_ask, "C312359Z.LOG") == 0, "Z is the last asked");

    /* Z alone free. */
    dir_clear();
    for (c = 'A'; c < 'Z'; c++) {
        sprintf(name, "C312359%c.LOG", c);
        dir_add(name);
    }
    rc = stamp_name(12, 31, 23, 59, "LOG", fake_exists, 0, out);
    CHECK(rc == STAMP_OK && strcmp(out, "C312359Z.LOG") == 0,
          "Z is used when A to Y are taken");
}

static void test_invalid(void)
{
    char out[STAMP_NAME_MAX];
    char pre[8];

    dir_clear();
    strcpy(out, "garbage");
    CHECK(stamp_name(0, 1, 0, 0, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK && out[0] == '\0', "month 0");
    CHECK(stamp_name(13, 1, 0, 0, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK, "month 13");
    CHECK(stamp_name(1, 0, 0, 0, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK, "day 0");
    CHECK(stamp_name(1, 32, 0, 0, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK, "day 32");
    CHECK(stamp_name(1, 1, 24, 0, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK, "hour 24");
    CHECK(stamp_name(1, 1, 0, 60, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK, "minute 60");
    CHECK(stamp_name(1000, 1, 0, 0, "LOG", fake_exists, 0, out) ==
          STAMP_BAD_CLOCK, "month far out of range");
    CHECK(asks == 0, "a bad clock asks the directory nothing");

    CHECK(stamp_name(1, 1, 0, 0, "LOGS", fake_exists, 0, out) ==
          STAMP_BAD_ARG, "four-character extension");
    CHECK(stamp_name(1, 1, 0, 0, "", fake_exists, 0, out) ==
          STAMP_BAD_ARG, "empty extension");
    CHECK(stamp_name(1, 1, 0, 0, 0, fake_exists, 0, out) ==
          STAMP_BAD_ARG, "no extension");
    CHECK(stamp_name(1, 1, 0, 0, "LOG", 0, 0, out) ==
          STAMP_BAD_ARG, "no predicate");
    CHECK(stamp_name(1, 1, 0, 0, "LOG", fake_exists, 0, 0) ==
          STAMP_BAD_ARG, "no output buffer");

    CHECK(stamp_prefix(10, 13, 14, 25, pre) && strcmp(pre, "A131425") == 0,
          "prefix for a refusal message");
    CHECK(!stamp_prefix(10, 13, 24, 25, pre) && pre[0] == '\0',
          "no prefix for a bad clock");
}

int main(void)
{
    test_months();
    test_midnight();
    test_next_letter();
    test_all_taken();
    test_invalid();
    if (failures == 0)
        printf("test_stamp: %d checks passed\n", checks);
    else
        printf("test_stamp: %d of %d checks FAILED\n", failures, checks);
    return failures;
}
