/*
 * stamp.h - the date-and-time 8.3 name of a field-report output (roadmap-hcd
 * task 38.1, owner 2026-10-13).
 *
 * A batch file cannot build a name from the clock (COMMAND.COM on 98 SE, ME
 * and MS-DOS 7.x has no %DATE% or %TIME%), so the tool does. The scheme is
 * the one XHCISNAP uses for its own outputs:
 *
 *   mDDhhmmS.EXT
 *
 *   m   the month as one hex digit: 1-9, then A, B, C for 10, 11, 12
 *   DD  the day, 01-31
 *   hh  the hour, 00-23
 *   mm  the minute, 00-59
 *   S   A to Z: the first letter for which mDDhhmmS.EXT does not exist,
 *       so a second run in the same minute never overwrites the first
 *
 * The year is not in the name (there is no room in eight characters); the
 * caller prints the full date and time in the output itself.
 *
 * Pure: no clock, no file system. The caller passes the clock's fields and
 * the predicate that says whether a name is taken, which is what lets the
 * host test (test\test_stamp.c) run every case on the build machine.
 *
 * C89, no DOS dependency.
 */

#ifndef STAMP_H
#define STAMP_H

#define STAMP_OK         0  /* out holds the name */
#define STAMP_BAD_CLOCK  1  /* a field is out of range; out is "" */
#define STAMP_ALL_TAKEN  2  /* A to Z all exist; out is "" */
#define STAMP_BAD_ARG    3  /* null pointer, or an extension over 3 chars */

/* 8 + '.' + 3 + NUL */
#define STAMP_NAME_MAX   13

/* Answers nonzero when a file (or anything else) of that name exists. */
typedef int (*STAMP_EXISTS_FN)(const char *name, void *ctx);

/*
 * Build the name for the given clock. ext is the extension without its dot,
 * one to three characters (for example "LOG"). out must hold STAMP_NAME_MAX
 * bytes. Returns STAMP_OK or one of the refusals above.
 */
int stamp_name(unsigned month, unsigned day, unsigned hour, unsigned minute,
               const char *ext, STAMP_EXISTS_FN exists, void *ctx,
               char *out);

/* The base without its letter, for a refusal message: "mDDhhmm" (7 chars
 * and NUL; out must hold 8). Returns 0 when a field is out of range. */
int stamp_prefix(unsigned month, unsigned day, unsigned hour,
                 unsigned minute, char *out);

#endif
