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

/*
 * The name, created: what --log-stamp uses (task 38.1). A
 * name that is free when asked about can be taken before it is created, and
 * a create that truncated would then overwrite it; so the caller's create
 * must be exclusive (INT 21h AH=5Bh, "create new file"), and a create that
 * finds the name taken moves on to the next letter rather than refusing.
 *
 * create answers STAMP_CREATE_OK when it made the file (and holds it, in
 * ctx), STAMP_CREATE_EXISTS when the name was taken, or STAMP_CREATE_FAIL.
 * A failure is asked about once more with exists, since what appeared in
 * the gap may be something an exclusive create reports as a plain failure
 * (a directory); a name still free after a failure is a medium that cannot
 * be written, and that stops the search: STAMP_CREATE_FAILED, with out
 * holding the name that could not be created.
 */
#define STAMP_CREATE_OK      0
#define STAMP_CREATE_EXISTS  1
#define STAMP_CREATE_FAIL    2

#define STAMP_CREATE_FAILED  4  /* out holds the name that failed */

typedef int (*STAMP_CREATE_FN)(const char *name, void *ctx);

int stamp_create(unsigned month, unsigned day, unsigned hour,
                 unsigned minute, const char *ext, STAMP_EXISTS_FN exists,
                 STAMP_CREATE_FN create, void *ctx, char *out);

/* The base without its letter, for a refusal message: "mDDhhmm" (7 chars
 * and NUL; out must hold 8). Returns 0 when a field is out of range. */
int stamp_prefix(unsigned month, unsigned day, unsigned hour,
                 unsigned minute, char *out);

#endif
