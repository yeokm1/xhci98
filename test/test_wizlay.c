/*
 * test_wizlay.c - host vectors for the settings pages' layout arithmetic
 * (xhciinst\wiz_lay.c; design record 26 section 5.4, "The layout", and
 * section 11; roadmap-usb4.md task 43.10): the "now" column, a part's
 * height, the split into parts of at most three rows, the open list's
 * width, the note's top and the part shown.
 */

#include <stdio.h>
#include <string.h>
#include "../xhciinst/wiz_lay.h"
#include "test_harness.h"

static void test_now_col(void)
{
    CHECK_EQ(wiz_lay_now_col(0, 400), 0, "no now text: no column");
    CHECK_EQ(wiz_lay_now_col(100, 400), 100, "a short now text: its width");
    CHECK_EQ(wiz_lay_now_col(160, 400), 160, "two fifths exactly");
    CHECK_EQ(wiz_lay_now_col(161, 400), 160, "past two fifths: two fifths");
    CHECK_EQ(wiz_lay_now_col(900, 426), 170, "a long one at 98 SE's column");
    CHECK_EQ(wiz_lay_now_col(50, 0), 0, "no column");
}

static void test_part_h(void)
{
    static const int h[] = { 50, 60, 70, 80 };

    CHECK_EQ(wiz_lay_part_h(h, 0, 0, 15), 0, "no rows");
    CHECK_EQ(wiz_lay_part_h(h, 1, 2, 15), 60, "one row: no gap");
    CHECK_EQ(wiz_lay_part_h(h, 0, 3, 15), 50 + 60 + 70 + 30, "three rows, two gaps");
    CHECK_EQ(wiz_lay_part_h(h, 2, 4, 15), 70 + 80 + 15, "the last two");
}

/* Every row in exactly one part, in order; no part over perPart; a part of
 * two or more rows within room; parts as even as they can be. */
static void check_split(const int *h, int n, int sep, int room, int per,
                        int want, const char *what)
{
    int start[16];
    int parts;
    int k;
    int lo = 1000;
    int hi = 0;
    char msg[128];

    memset(start, 0xFF, sizeof(start));
    parts = wiz_lay_split(h, n, sep, room, per, start, 16);
    sprintf(msg, "%.100s: parts", what);
    CHECK_EQ(parts, want, msg);
    if (parts < 1) {
        return;
    }
    CHECK_EQ(start[0], 0, "the first part starts at row 0");
    CHECK_EQ(start[parts], n, "the last part ends at n");
    for (k = 0; k < parts; k++) {
        int rows = start[k + 1] - start[k];
        CHECK(rows >= (n > 0 ? 1 : 0), "no empty part");
        CHECK(rows <= per, "no part over perPart");
        CHECK(rows < 2 || wiz_lay_part_h(h, start[k], start[k + 1], sep) <=
              room, "a part of two or more rows fits");
        if (rows < lo) {
            lo = rows;
        }
        if (rows > hi) {
            hi = rows;
        }
        CHECK(k == 0 || start[k + 1] - start[k] <= start[k] - start[k - 1],
              "the first parts take the extra row");
    }
    CHECK(hi - lo <= 1, "the parts even");
}

static void test_split(void)
{
    /* 98 SE at 96 DPI, roughly: rows of two label lines and a drop-down. */
    static const int four[] = { 50, 50, 50, 50 };
    static const int six[] = { 50, 50, 50, 50, 50, 50 };
    static const int seven[] = { 50, 50, 50, 50, 50, 50, 50 };
    static const int tall[] = { 50, 300, 50 };
    static const int big[] = { 120, 120, 120, 120 };
    int start[8];

    check_split(four, 0, 15, 226, 3, 1, "no rows");
    check_split(four, 1, 15, 226, 3, 1, "one row");
    check_split(four, 3, 15, 226, 3, 1, "three rows fit one part");
    check_split(four, 4, 15, 226, 3, 2, "four rows: two parts");
    memset(start, 0, sizeof(start));
    wiz_lay_split(four, 4, 15, 226, 3, start, 8);
    CHECK_EQ(start[1], 2, "four rows: two and two");
    check_split(six, 6, 15, 226, 3, 2, "six rows: three and three");
    check_split(seven, 7, 15, 226, 3, 3, "seven rows: three, two, two");
    memset(start, 0, sizeof(start));
    wiz_lay_split(seven, 7, 15, 226, 3, start, 8);
    CHECK(start[1] == 3 && start[2] == 5 && start[3] == 7,
          "seven rows: the first part takes the extra row");
    /* Three rows that do not fit together (120 DPI, long labels). */
    check_split(four, 3, 15, 150, 3, 2, "three rows over the room: two parts");
    check_split(big, 4, 15, 260, 3, 2, "two of these fit a part");
    check_split(big, 4, 15, 200, 3, 4, "none pair: a row a part");
    /* One row taller than the room stands alone. */
    check_split(tall, 3, 15, 226, 3, 3, "a tall row alone");
    check_split(tall, 1, 15, 10, 3, 1, "a single row over the room");
    /* Never more than perPart, whatever the room. */
    check_split(six, 6, 0, 100000, 3, 2, "room for all: still three a part");
    check_split(six, 6, 0, 100000, 1, 6, "one a part");
    CHECK_EQ(wiz_lay_split(six, 6, 15, 226, 3, start, 6), 0,
             "a start array too small is refused");
    CHECK_EQ(wiz_lay_split(six, 0, 15, 226, 3, start, 1), 0,
             "no room for start[1] is refused");
}

static void test_drop_w(void)
{
    CHECK_EQ(wiz_lay_drop_w(200, 26, 426, 640), 426,
             "shorter than the closed box: the box's width");
    CHECK_EQ(wiz_lay_drop_w(450, 26, 426, 640), 476,
             "longer: the entry and its frame");
    CHECK_EQ(wiz_lay_drop_w(700, 26, 426, 640), 640,
             "never wider than the screen");
    CHECK_EQ(wiz_lay_drop_w(700, 26, 426, 0), 726, "no screen width known");
}

static void test_note_top(void)
{
    /* 98 SE at 96 DPI, roughly: six lines 84, two lines 32. */
    CHECK_EQ(wiz_lay_note_top(36, 180, 10, 356, 32), 226,
             "under the tallest part");
    CHECK_EQ(wiz_lay_note_top(36, 236, 10, 356, 32), 282,
             "a lone tall row takes from the note");
    CHECK_EQ(wiz_lay_note_top(36, 300, 10, 356, 32), 324,
             "never less than its floor");
}

static void test_part(void)
{
    CHECK_EQ(wiz_lay_part(0, 1), 0, "one part");
    CHECK_EQ(wiz_lay_part(-1, 1), 0, "the last of one");
    CHECK_EQ(wiz_lay_part(-1, 3), 2, "the last of three");
    CHECK_EQ(wiz_lay_part(1, 3), 1, "the second");
    CHECK_EQ(wiz_lay_part(5, 3), 2, "past the end: the last");
    CHECK_EQ(wiz_lay_part(1, 0), 0, "no parts: the first");
}

int main(void)
{
    test_now_col();
    test_part_h();
    test_split();
    test_drop_w();
    test_note_top();
    test_part();
    printf("test_wizlay: %d checks, %d failures\n", checks, failures);
    return failures;
}
