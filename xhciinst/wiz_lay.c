/*
 * wiz_lay.c - the settings pages' layout arithmetic (design record 26
 * section 5.4, "The layout"; roadmap-usb4.md task 43.10). Pure: see
 * wiz_lay.h. The window (xhciwiz.c) measures and places; this decides.
 */

#include "wiz_lay.h"

int wiz_lay_now_col(int widest, int column)
{
    int most = column * 2 / 5;

    if (widest <= 0 || column <= 0) {
        return 0;
    }
    return widest < most ? widest : most;
}

int wiz_lay_part_h(const int *rowH, int from, int to, int sep)
{
    int h = 0;
    int i;

    for (i = from; i < to; i++) {
        h += rowH[i];
        if (i > from) {
            h += sep;
        }
    }
    return h;
}

/* The even split into parts: the first n % parts take one row more. */
static void split_even(int n, int parts, int *start)
{
    int base = n / parts;
    int extra = n % parts;
    int k;

    start[0] = 0;
    for (k = 0; k < parts; k++) {
        start[k + 1] = start[k] + base + (k < extra ? 1 : 0);
    }
}

int wiz_lay_split(const int *rowH, int n, int sep, int room, int perPart,
                  int *start, int cap)
{
    int parts;
    int k;

    if (perPart < 1) {
        perPart = 1;
    }
    if (n <= 0) {
        if (cap < 2) {
            return 0;
        }
        start[0] = 0;
        start[1] = 0;
        return 1;
    }
    if (cap < n + 1) {
        return 0;
    }
    for (parts = (n + perPart - 1) / perPart; parts < n; parts++) {
        int fits = 1;
        split_even(n, parts, start);
        for (k = 0; k < parts && fits; k++) {
            if (start[k + 1] - start[k] > 1 &&
                wiz_lay_part_h(rowH, start[k], start[k + 1], sep) > room) {
                fits = 0;
            }
        }
        if (fits) {
            return parts;
        }
    }
    split_even(n, n, start);
    return n;
}

int wiz_lay_drop_w(int longest, int frame, int closed, int screen)
{
    int w = longest + frame;

    if (w < closed) {
        w = closed;
    }
    if (screen > 0 && w > screen) {
        w = screen;
    }
    return w;
}

int wiz_lay_note_top(int rowsTop, int tallest, int gap, int bottom,
                     int floor)
{
    int top = rowsTop + tallest + gap;

    if (top > bottom - floor) {
        top = bottom - floor;
    }
    return top;
}

int wiz_lay_part(int part, int parts)
{
    if (parts <= 1) {
        return 0;
    }
    return part < 0 || part >= parts ? parts - 1 : part;
}
