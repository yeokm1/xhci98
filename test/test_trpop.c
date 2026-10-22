/*
 * test_trpop.c - host vectors for XHCITR's 98 SE pop-up, its pure half
 * (xhcitr\tr_pop.c; design record 25 section 16 item 9, revision 17;
 * roadmap-usb4.md task 43.55): the taskbar's edge, shown and auto-hidden,
 * and with no taskbar; the anchor with and without the notification area;
 * the room, the place at each edge and in a corner of a 640 x 480 screen;
 * the size at small and large fonts, a 255-character text and the cut to
 * the room; the timeout at the tick's wrap; the queue's order, bound and
 * drop rule.
 */

#include <stdio.h>
#include <string.h>
#include "../xhcitr/tr_pop.h"
#include "test_harness.h"

static TR_POP_RECT R(int l, int t, int r, int b)
{
    TR_POP_RECT x;

    x.Left = l;
    x.Top = t;
    x.Right = r;
    x.Bottom = b;
    return x;
}

static void check_rect(const TR_POP_RECT *r, int l, int t, int rr, int b,
                       const char *what)
{
    char msg[160];

    sprintf(msg, "%.120s: left", what);
    CHECK_EQ(r->Left, l, msg);
    sprintf(msg, "%.120s: top", what);
    CHECK_EQ(r->Top, t, msg);
    sprintf(msg, "%.120s: right", what);
    CHECK_EQ(r->Right, rr, msg);
    sprintf(msg, "%.120s: bottom", what);
    CHECK_EQ(r->Bottom, b, msg);
}

static const TR_POP_RECT *nul = 0;

static void test_bar_ok(void)
{
    TR_POP_RECT s = R(0, 0, 640, 480);
    TR_POP_RECT b;

    CHECK_EQ(tr_pop_bar_ok(&s, nul), 0, "no taskbar");
    b = R(-2, 452, 642, 482);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 1, "98 SE's taskbar at the bottom");
    b = R(-2, 478, 642, 508);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 1, "auto-hidden: a sliver on the screen");
    b = R(0, 0, 0, 0);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 0, "an empty rectangle");
    b = R(10, 20, 10, 40);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 0, "no width");
    b = R(700, 0, 800, 30);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 0, "wholly off the screen");
    b = R(-2, 480, 642, 510);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 0, "just below the screen");
    b = R(100, 100, 300, 130);
    CHECK_EQ(tr_pop_bar_ok(&s, &b), 0, "touching no edge");
}

static void test_edge(void)
{
    TR_POP_RECT s = R(0, 0, 640, 480);
    TR_POP_RECT b;
    TR_POP_RECT w;

    b = R(-2, 452, 642, 482);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_BOTTOM, "bottom");
    b = R(-2, -2, 642, 28);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_TOP, "top");
    b = R(-2, -2, 100, 482);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_LEFT, "left");
    b = R(540, -2, 642, 482);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_RIGHT, "right");
    b = R(-2, 478, 642, 508);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_BOTTOM, "auto-hidden bottom");
    b = R(-2, -28, 642, 2);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_TOP, "auto-hidden top");
    b = R(-100, -2, 2, 482);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_LEFT, "auto-hidden left");
    b = R(638, -2, 740, 482);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_RIGHT, "auto-hidden right");
    b = R(-2, 300, 642, 482);
    CHECK_EQ(tr_pop_edge(&s, &s, &b), TR_POP_EDGE_BOTTOM,
             "a tall bottom taskbar, still wider than tall");

    /* No taskbar: the work area says. */
    w = R(0, 0, 640, 452);
    CHECK_EQ(tr_pop_edge(&s, &w, nul), TR_POP_EDGE_BOTTOM, "work area: bottom");
    w = R(0, 28, 640, 480);
    CHECK_EQ(tr_pop_edge(&s, &w, nul), TR_POP_EDGE_TOP, "work area: top");
    w = R(100, 0, 640, 480);
    CHECK_EQ(tr_pop_edge(&s, &w, nul), TR_POP_EDGE_LEFT, "work area: left");
    w = R(0, 0, 540, 480);
    CHECK_EQ(tr_pop_edge(&s, &w, nul), TR_POP_EDGE_RIGHT, "work area: right");
    CHECK_EQ(tr_pop_edge(&s, &s, nul), TR_POP_EDGE_BOTTOM, "work area whole: bottom");
    CHECK_EQ(tr_pop_edge(&s, nul, nul), TR_POP_EDGE_BOTTOM, "nothing known: bottom");
    w = R(0, 10, 640, 470);
    CHECK_EQ(tr_pop_edge(&s, &w, nul), TR_POP_EDGE_BOTTOM, "a tie: bottom first");
    w = R(0, 30, 640, 470);
    CHECK_EQ(tr_pop_edge(&s, &w, nul), TR_POP_EDGE_TOP, "the larger shortfall");
    b = R(100, 100, 300, 130);
    w = R(0, 0, 540, 480);
    CHECK_EQ(tr_pop_edge(&s, &w, &b), TR_POP_EDGE_RIGHT,
             "a taskbar touching no edge: the work area");
}

static void test_anchor(void)
{
    TR_POP_RECT s = R(0, 0, 640, 480);
    TR_POP_RECT b;
    TR_POP_RECT n;
    TR_POP_RECT w;
    int line = -1;
    int along = -1;

    b = R(-2, 452, 642, 482);
    n = R(574, 454, 638, 480);
    tr_pop_anchor(&s, &s, &b, &n, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(line, 452, "bottom: the taskbar's top");
    CHECK_EQ(along, 606, "bottom: the notification area's middle");

    tr_pop_anchor(&s, &s, &b, nul, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(along, 634, "bottom, no notification area: the far end less the margin");
    n = R(574, 100, 638, 120);
    tr_pop_anchor(&s, &s, &b, &n, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(along, 634, "a notification area outside the taskbar is not read");
    n = R(574, 454, 574, 480);
    tr_pop_anchor(&s, &s, &b, &n, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(along, 634, "an empty notification area is not read");

    b = R(-2, 478, 642, 508);
    n = R(574, 480, 638, 506);
    tr_pop_anchor(&s, &s, &b, &n, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(line, 478, "auto-hidden bottom: the sliver's edge");
    CHECK_EQ(along, 606, "auto-hidden bottom: the notification area, off the screen");

    b = R(-2, -2, 642, 28);
    tr_pop_anchor(&s, &s, &b, nul, TR_POP_EDGE_TOP, 6, &line, &along);
    CHECK_EQ(line, 28, "top: the taskbar's bottom");
    CHECK_EQ(along, 634, "top: the far end");
    b = R(-2, -28, 642, 2);
    tr_pop_anchor(&s, &s, &b, nul, TR_POP_EDGE_TOP, 6, &line, &along);
    CHECK_EQ(line, 2, "auto-hidden top");

    b = R(-2, -2, 100, 482);
    n = R(4, 440, 96, 476);
    tr_pop_anchor(&s, &s, &b, &n, TR_POP_EDGE_LEFT, 6, &line, &along);
    CHECK_EQ(line, 100, "left: the taskbar's right");
    CHECK_EQ(along, 458, "left: the notification area's middle");
    tr_pop_anchor(&s, &s, &b, nul, TR_POP_EDGE_LEFT, 6, &line, &along);
    CHECK_EQ(along, 474, "left: the bottom end less the margin");

    b = R(540, -2, 642, 482);
    tr_pop_anchor(&s, &s, &b, nul, TR_POP_EDGE_RIGHT, 6, &line, &along);
    CHECK_EQ(line, 540, "right: the taskbar's left");
    CHECK_EQ(along, 474, "right: the bottom end");
    b = R(638, -2, 740, 482);
    tr_pop_anchor(&s, &s, &b, nul, TR_POP_EDGE_RIGHT, 6, &line, &along);
    CHECK_EQ(line, 638, "auto-hidden right");

    /* No taskbar. */
    w = R(0, 28, 640, 480);
    tr_pop_anchor(&s, &w, nul, nul, TR_POP_EDGE_TOP, 6, &line, &along);
    CHECK_EQ(line, 28, "no taskbar, top: the work area's top");
    CHECK_EQ(along, 634, "no taskbar, top: its right end");
    w = R(0, 0, 640, 452);
    n = R(574, 454, 638, 480);
    tr_pop_anchor(&s, &w, nul, &n, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(line, 452, "no taskbar, bottom: the work area's bottom");
    CHECK_EQ(along, 634, "no taskbar: no notification area read");
    w = R(100, 0, 640, 480);
    tr_pop_anchor(&s, &w, nul, nul, TR_POP_EDGE_LEFT, 6, &line, &along);
    CHECK_EQ(line, 100, "no taskbar, left");
    CHECK_EQ(along, 474, "no taskbar, left: the bottom end");
    w = R(0, 0, 540, 480);
    tr_pop_anchor(&s, &w, nul, nul, TR_POP_EDGE_RIGHT, 6, &line, &along);
    CHECK_EQ(line, 540, "no taskbar, right");
    tr_pop_anchor(&s, nul, nul, nul, TR_POP_EDGE_BOTTOM, 6, &line, &along);
    CHECK_EQ(line, 480, "nothing known: the screen's bottom");
    CHECK_EQ(along, 634, "nothing known: the screen's right less the margin");
}

static void test_room(void)
{
    TR_POP_RECT s = R(0, 0, 640, 480);
    int w = 0;
    int h = 0;

    tr_pop_room(&s, TR_POP_EDGE_BOTTOM, 452, 12, 6, &w, &h);
    CHECK_EQ(w, 628, "bottom: the width");
    CHECK_EQ(h, 434, "bottom: above the taskbar, less the stem and margin");
    tr_pop_room(&s, TR_POP_EDGE_TOP, 28, 12, 6, &w, &h);
    CHECK_EQ(h, 434, "top: below it");
    tr_pop_room(&s, TR_POP_EDGE_LEFT, 100, 12, 6, &w, &h);
    CHECK_EQ(w, 522, "left: right of it");
    CHECK_EQ(h, 468, "left: the height");
    tr_pop_room(&s, TR_POP_EDGE_RIGHT, 540, 12, 6, &w, &h);
    CHECK_EQ(w, 522, "right: left of it");
    tr_pop_room(&s, TR_POP_EDGE_BOTTOM, 5, 12, 6, &w, &h);
    CHECK_EQ(h, 1, "no room: at least 1");
}

static void check_stem(const TR_POP_PLACE *p, int x0, int y0, int x1, int y1,
                       int x2, int y2, const char *what)
{
    char msg[160];

    sprintf(msg, "%.120s: stem base 0", what);
    CHECK(p->StemX[0] == x0 && p->StemY[0] == y0, msg);
    sprintf(msg, "%.120s: stem base 1", what);
    CHECK(p->StemX[1] == x1 && p->StemY[1] == y1, msg);
    sprintf(msg, "%.120s: stem tip", what);
    CHECK(p->StemX[2] == x2 && p->StemY[2] == y2, msg);
}

static void test_place(void)
{
    TR_POP_RECT s = R(0, 0, 640, 480);
    TR_POP_PLACE p;

    memset(&p, 0, sizeof(p));
    tr_pop_place(&s, TR_POP_EDGE_BOTTOM, 452, 606, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 382, 370, 624, 452, "bottom: the window");
    check_rect(&p.Body, 0, 0, 242, 70, "bottom: the body");
    check_stem(&p, 212, 70, 224, 70, 224, 82, "bottom");

    tr_pop_place(&s, TR_POP_EDGE_BOTTOM, 452, 636, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 392, 370, 634, 452, "bottom corner: held inside");
    check_stem(&p, 224, 70, 236, 70, 236, 82, "bottom corner: the stem held");

    tr_pop_place(&s, TR_POP_EDGE_BOTTOM, 452, 3, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 6, 370, 248, 452, "bottom, left end: held inside");
    check_stem(&p, 6, 70, 18, 70, 18, 82, "bottom, left end: the stem past the corner");

    tr_pop_place(&s, TR_POP_EDGE_TOP, 28, 634, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 392, 28, 634, 110, "top: the window");
    check_rect(&p.Body, 0, 12, 242, 82, "top: the body under the stem");
    check_stem(&p, 224, 12, 236, 12, 236, 0, "top");

    tr_pop_place(&s, TR_POP_EDGE_LEFT, 100, 458, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 100, 404, 354, 474, "left: the window");
    check_rect(&p.Body, 12, 0, 254, 70, "left: the body right of the stem");
    check_stem(&p, 12, 42, 12, 54, 0, 54, "left");

    tr_pop_place(&s, TR_POP_EDGE_RIGHT, 540, 474, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 286, 404, 540, 474, "right: the window");
    check_rect(&p.Body, 0, 0, 242, 70, "right: the body");
    check_stem(&p, 242, 52, 242, 64, 254, 64, "right");

    tr_pop_place(&s, TR_POP_EDGE_BOTTOM, 478, 606, 242, 70, 12, 6, 6, &p);
    check_rect(&p.Win, 382, 396, 624, 478, "auto-hidden bottom: on the sliver");

    tr_pop_place(&s, TR_POP_EDGE_BOTTOM, 452, 606, 700, 70, 12, 6, 6, &p);
    CHECK_EQ(p.Win.Left, 6, "wider than the screen: from the left margin");
}

static void test_wrap(void)
{
    CHECK_EQ(tr_pop_wrap_w(6, 6, 628), 240, "small fonts: 40 characters");
    CHECK_EQ(tr_pop_wrap_w(8, 8, 628), 320, "large fonts: 40 characters");
    CHECK_EQ(tr_pop_wrap_w(8, 8, 200), 184, "little room: the room less the padding");
    CHECK_EQ(tr_pop_wrap_w(8, 8, 10), 1, "no room: 1");
}

static void test_layout(void)
{
    TR_POP_METRICS m;
    TR_POP_LAYOUT l;

    /* Small fonts at 96 DPI. */
    memset(&m, 0, sizeof(m));
    m.Pad = 6;
    m.Icon = 16;
    m.Close = 13;
    m.TitleW = 100;
    m.TitleH = 13;
    m.TextW = 230;
    m.TextH = 39;
    tr_pop_layout(&m, 240, 434, &l);
    CHECK_EQ(l.W, 242, "small: the width");
    CHECK_EQ(l.H, 70, "small: the height");
    check_rect(&l.Icon, 6, 6, 22, 22, "small: the icon");
    check_rect(&l.Title, 25, 7, 125, 20, "small: the title");
    check_rect(&l.Close, 223, 7, 236, 20, "small: the close box");
    check_rect(&l.Text, 6, 25, 236, 64, "small: the text");

    /* A long title is cut to the wrap width. */
    m.TitleW = 500;
    tr_pop_layout(&m, 240, 434, &l);
    CHECK_EQ(l.Title.Right - l.Title.Left, 205, "a long title: cut");
    CHECK_EQ(l.W, 252, "a long title: the wrap width and the padding");
    CHECK(l.Close.Right <= l.W - m.Pad, "a long title: the close box inside");

    /* No icon, no text. */
    m.TitleW = 100;
    m.Icon = 0;
    m.TextW = 0;
    m.TextH = 0;
    tr_pop_layout(&m, 240, 434, &l);
    CHECK_EQ(l.Title.Left, 6, "no icon: the title at the padding");
    CHECK_EQ(l.Icon.Right - l.Icon.Left, 0, "no icon: empty");
    CHECK_EQ(l.H, 6 + 13 + 6, "no text: the title row alone");
    CHECK_EQ(l.Text.Bottom - l.Text.Top, 0, "no text: empty");
    CHECK_EQ(l.W, 6 + 100 + 3 + 13 + 6, "no text: as wide as the title row");

    /* Large fonts at 640 x 480: the longest text, seven lines at 320. */
    memset(&m, 0, sizeof(m));
    m.Pad = 8;
    m.Icon = 16;
    m.Close = 16;
    m.TitleW = 150;
    m.TitleH = 16;
    m.TextW = 320;
    m.TextH = 7 * 16;
    tr_pop_layout(&m, 320, 422, &l);
    CHECK_EQ(l.W, 336, "large: the width");
    CHECK_EQ(l.H, 148, "large: the height");
    CHECK(l.W <= 640 - 16 && l.H <= 422, "large: inside 640 x 480");
    check_rect(&l.Text, 8, 28, 328, 140, "large: the text");

    /* Cut to the room. */
    tr_pop_layout(&m, 320, 60, &l);
    CHECK_EQ(l.H, 60, "cut: the room's height");
    CHECK_EQ(l.Text.Bottom - l.Text.Top, 24, "cut: the text's share");
    tr_pop_layout(&m, 320, 36, &l);
    CHECK_EQ(l.H, 32, "no room for text: the title row alone");
    CHECK_EQ(l.Text.Bottom - l.Text.Top, 0, "no room for text: none");
    tr_pop_layout(&m, 320, 30, &l);
    CHECK_EQ(l.H, 30, "lower than the title row: cut to the room");
    CHECK_EQ(l.Text.Bottom - l.Text.Top, 0, "lower than the title row: no text");

    /* Narrower than the icon and the close box: the title goes, the body
     * stays at the wrap width. */
    m.TextW = 20;
    m.TextH = 16;
    tr_pop_layout(&m, 20, 422, &l);
    CHECK_EQ(l.W, 20 + 2 * 8, "narrow: the wrap width");
    CHECK_EQ(l.Title.Right - l.Title.Left, 0, "narrow: no title");
    m.TextW = 320;
    m.TextH = 7 * 16;

    /* A word wider than the wrap: the body stays at the wrap width. */
    m.TextW = 900;
    m.TextH = 16;
    tr_pop_layout(&m, 320, 422, &l);
    CHECK_EQ(l.W, 336, "an unbreakable word: the wrap width");
}

static void test_expired(void)
{
    CHECK_EQ(tr_pop_expired(1000UL, 10999UL, 10000UL), 0, "a millisecond short");
    CHECK_EQ(tr_pop_expired(1000UL, 11000UL, 10000UL), 1, "on time");
    CHECK_EQ(tr_pop_expired(0xFFFFF000UL, 0x0000170FUL, 10000UL), 0,
             "across the wrap, short");
    CHECK_EQ(tr_pop_expired(0xFFFFF000UL, 0x00001710UL, 10000UL), 1,
             "across the wrap, on time");
    CHECK_EQ(tr_pop_expired(5000UL, 5000UL, 10000UL), 0, "just shown");
}

static void push_n(TR_POP_QUEUE *q, const char *name, int icon)
{
    tr_popq_push(q, name, "text", icon);
}

static void test_queue(void)
{
    static TR_POP_QUEUE q;
    TR_POP_NOTE n;
    char longer[400];
    char name[8];
    int i;

    tr_popq_init(&q);
    CHECK_EQ(tr_popq_pop(&q, &n), 0, "empty: nothing");
    CHECK_EQ(tr_popq_push(&q, "a", "first", TR_POP_INFO), 1, "push");
    tr_popq_push(&q, "b", "second", TR_POP_WARNING);
    tr_popq_push(&q, "c", 0, TR_POP_ERROR);
    CHECK_EQ(q.Count, 3, "three waiting");
    CHECK(tr_popq_pop(&q, &n) && strcmp(n.Title, "a") == 0 &&
          strcmp(n.Text, "first") == 0 && n.Icon == TR_POP_INFO, "the first in");
    CHECK(tr_popq_pop(&q, &n) && strcmp(n.Title, "b") == 0 &&
          n.Icon == TR_POP_WARNING, "then the second");
    CHECK(tr_popq_pop(&q, &n) && strcmp(n.Title, "c") == 0 &&
          n.Text[0] == '\0' && n.Icon == TR_POP_ERROR, "then the third, no text");
    CHECK_EQ(tr_popq_pop(&q, &n), 0, "then none");

    /* The bound: the oldest information notice goes. */
    tr_popq_init(&q);
    for (i = 1; i <= TR_POP_QMAX; i++) {
        sprintf(name, "%d", i);
        CHECK_EQ(tr_popq_push(&q, name, "text", TR_POP_INFO), 1, "within the bound");
    }
    CHECK_EQ(tr_popq_push(&q, "9", "text", TR_POP_INFO), 2, "a ninth: one dropped");
    CHECK_EQ(q.Count, TR_POP_QMAX, "still eight");
    CHECK(tr_popq_pop(&q, &n) && strcmp(n.Title, "2") == 0, "the oldest went");
    CHECK(strcmp(q.Note[q.Count - 1].Title, "9") == 0, "the newest at the end");

    /* Warnings kept before information. */
    tr_popq_init(&q);
    push_n(&q, "w1", TR_POP_WARNING);
    push_n(&q, "w2", TR_POP_ERROR);
    for (i = 3; i <= TR_POP_QMAX; i++) {
        sprintf(name, "i%d", i);
        push_n(&q, name, i == 5 ? TR_POP_NONE : TR_POP_INFO);
    }
    CHECK_EQ(tr_popq_push(&q, "i9", "text", TR_POP_INFO), 2, "full: one dropped");
    CHECK(strcmp(q.Note[0].Title, "w1") == 0 && strcmp(q.Note[1].Title, "w2") == 0,
          "the warnings kept");
    CHECK(strcmp(q.Note[2].Title, "i4") == 0, "the oldest information notice went");
    CHECK_EQ(tr_popq_push(&q, "i10", "text", TR_POP_INFO), 2, "again");
    CHECK(strcmp(q.Note[2].Title, "i5") == 0, "again the oldest information notice");
    CHECK_EQ(tr_popq_push(&q, "i11", "text", TR_POP_INFO), 2, "and again");
    CHECK(strcmp(q.Note[2].Title, "i6") == 0, "a notice of no icon counts as information");

    /* All warnings: the oldest goes. */
    tr_popq_init(&q);
    for (i = 1; i <= TR_POP_QMAX; i++) {
        sprintf(name, "w%d", i);
        push_n(&q, name, TR_POP_WARNING);
    }
    tr_popq_push(&q, "e9", "text", TR_POP_ERROR);
    CHECK(strcmp(q.Note[0].Title, "w2") == 0, "all warnings: the oldest went");
    CHECK(strcmp(q.Note[TR_POP_QMAX - 1].Title, "e9") == 0, "all warnings: the new one in");

    /* The icon kinds and the fields' bounds. */
    tr_popq_init(&q);
    tr_popq_push(&q, "x", "y", 7);
    tr_popq_push(&q, "x", "y", -1);
    CHECK(q.Note[0].Icon == TR_POP_INFO && q.Note[1].Icon == TR_POP_INFO,
          "an unknown kind: information");
    memset(longer, 'a', sizeof(longer) - 1);
    longer[sizeof(longer) - 1] = '\0';
    tr_popq_push(&q, longer, longer, TR_POP_INFO);
    CHECK_EQ(strlen(q.Note[2].Title), TR_POP_TITLE - 1, "the title cut at 63");
    CHECK_EQ(strlen(q.Note[2].Text), TR_POP_TEXT - 1, "the text cut at 255");
    tr_popq_push(&q, 0, 0, TR_POP_NONE);
    CHECK(q.Note[3].Title[0] == '\0' && q.Note[3].Text[0] == '\0' &&
          q.Note[3].Icon == TR_POP_NONE, "no strings: empty");
}

int main(void)
{
    test_bar_ok();
    test_edge();
    test_anchor();
    test_room();
    test_place();
    test_wrap();
    test_layout();
    test_expired();
    test_queue();
    printf("test_trpop: %d checks, %d failures\n", checks, failures);
    return failures;
}
