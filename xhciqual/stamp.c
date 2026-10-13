/*
 * stamp.c - the date-and-time 8.3 name of a field-report output (roadmap-hcd
 * task 38.1). See stamp.h for the scheme; test\test_stamp.c for its cases.
 *
 * C89, no DOS dependency, no library call beyond what a freestanding build
 * would have to supply itself.
 */

#include "stamp.h"

static char hexdigit(unsigned v)
{
    return (char)((v < 10) ? ('0' + v) : ('A' + (v - 10)));
}

static void two(char *p, unsigned v)
{
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' + v % 10);
}

int stamp_prefix(unsigned month, unsigned day, unsigned hour,
                 unsigned minute, char *out)
{
    if (out == 0)
        return 0;
    out[0] = '\0';
    /* Day 31 is accepted in any month: the DOS clock is what it is, and a
     * 31 April in a name harms nothing, where refusing it would lose the
     * report. A zero day or month, or a field past its range, is a clock
     * that cannot be read as a date and time at all. */
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59)
        return 0;
    out[0] = hexdigit(month);
    two(out + 1, day);
    two(out + 3, hour);
    two(out + 5, minute);
    out[7] = '\0';
    return 1;
}

int stamp_name(unsigned month, unsigned day, unsigned hour, unsigned minute,
               const char *ext, STAMP_EXISTS_FN exists, void *ctx,
               char *out)
{
    unsigned n, i;
    char letter;

    if (out == 0)
        return STAMP_BAD_ARG;
    out[0] = '\0';
    if (ext == 0 || exists == 0)
        return STAMP_BAD_ARG;
    for (n = 0; ext[n] != '\0'; n++) {
        if (n >= 3)
            return STAMP_BAD_ARG;
    }
    if (n == 0)
        return STAMP_BAD_ARG;
    if (!stamp_prefix(month, day, hour, minute, out))
        return STAMP_BAD_CLOCK;
    out[8] = '.';
    for (i = 0; i <= n; i++)
        out[9 + i] = ext[i];            /* copies the NUL too */
    for (letter = 'A'; letter <= 'Z'; letter++) {
        out[7] = letter;
        if (!exists(out, ctx))
            return STAMP_OK;
    }
    out[0] = '\0';
    return STAMP_ALL_TAKEN;
}
