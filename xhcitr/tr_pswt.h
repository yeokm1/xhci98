/*
 * tr_pswt.h - the xHCI98 USB Tree's words for the Intel port switchover's
 * decision (task 50.34; design record 16 section 21.7; tr_pswt.c). Plain
 * C types, so a file with windows.h includes it as one without.
 */

#ifndef TR_PSWT_H
#define TR_PSWT_H

/* The record's (or the snapshot's) words, `len` bytes, into lines
 * separated by '\n' in out (cap bytes, always terminated): the decision,
 * where the values in force were set, and, at a start that reached the
 * Setup check, how each reading read. writeStatus is the snapshot's
 * status of the start's write of the record (0 or none: nothing said);
 * fromSnapshot 0 for the registry's record. Returns 1 when the words are
 * a record of an Intel controller's decision, else 0 with out empty. */
int tr_psw_lines(const unsigned char *rec, unsigned long len,
                 unsigned long writeStatus, int fromSnapshot, char *out,
                 unsigned long cap);

/* The record's time, as KeQuerySystemTime gave it; 0 when not a record. */
int tr_psw_time(const unsigned char *rec, unsigned long len,
                unsigned long *lo, unsigned long *hi);

#endif /* TR_PSWT_H */
