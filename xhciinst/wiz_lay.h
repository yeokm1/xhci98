/*
 * wiz_lay.h - the settings pages' layout arithmetic (design record 26
 * section 5.4, "The layout"; roadmap-usb4.md task 43.10).
 *
 * Pure: no Windows header, every size an int in the caller's units (the
 * window passes pixels it measured from the page's own font and size), so
 * the host suite test\test_wizlay.c checks it as the window runs it. A
 * file of its own rather than part of inst_core.c, which XHCICLN, the mark
 * service and XHCITR also compile.
 */

#ifndef WIZ_LAY_H
#define WIZ_LAY_H

/* The most rows one part of a page shows (owner, 2026-10-20). */
#define WIZ_LAY_PER_PART    3

/* The "now" column: as wide as the widest "now" text, at most two fifths
 * of the column; 0 when no row has one. */
int wiz_lay_now_col(int widest, int column);

/* The height of rows from..to-1 of rowH, with sep between each two. */
int wiz_lay_part_h(const int *rowH, int from, int to, int sep);

/* The rows split into parts: the fewest parts of at most perPart rows
 * each, the rows in their order and spread evenly (the first parts one
 * row more where they cannot be equal), such that every part fits room;
 * a part of one row stands whatever its height. start[k] is part k's
 * first row and start[parts] is n, so start needs n + 1 entries at most
 * (cap). Returns the number of parts, 1 for n == 0 (an empty part), or 0
 * when cap is too small. */
int wiz_lay_split(const int *rowH, int n, int sep, int room, int perPart,
                  int *start, int cap);

/* An open list's width: the longest entry plus the list's frame, never
 * narrower than the closed box, never wider than the work area (screen;
 * 0 unknown). */
int wiz_lay_drop_w(int longest, int frame, int closed, int screen);

/* The note's top: under the tallest part's rows (rowsTop + tallest + gap);
 * a lone row taller than its room takes from the note, which keeps floor
 * above bottom whatever the rows. */
int wiz_lay_note_top(int rowsTop, int tallest, int gap, int bottom,
                     int floor);

/* The part to show: 0 for the first, -1 for the last, else part clamped
 * to 0..parts-1. */
int wiz_lay_part(int part, int parts);

#endif
