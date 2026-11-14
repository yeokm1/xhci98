/*
 * tr_cmsw.h - XHCITR's reader of xhcicm.sys's state snapshot, the Windows
 * half (design record 25 section 23; roadmap-usb4 task 49.37): the query
 * devices, the windows of each image and the .CT<n> and .CB<n> files.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TR_CMSW_H
#define TR_CMSW_H

#include "tr_cms.h"

/* tr_cms_take's outcome, which joins /snapshot's exit (section 23 item 2). */
#define TR_CMS_TAKE_OK          0   /* every image written, or none taken */
#define TR_CMS_TAKE_CLASH       1   /* a name of the set appeared: exit 8 */
#define TR_CMS_TAKE_INCOMPLETE  2   /* a file not written in full: exit 3 */

/* Whether a file of BASE's connection manager set (.CT0 to .CTF, .CB0 to
   .CBF and their .TMP) exists, by `exists`; the first such name into
   `which`. */
int tr_cms_set_taken(const char *base, int (*exists)(const char *, void *),
                     void *context, char *which, size_t whichSize);

/* Every query device's image into BASE.CT<n> and BASE.CB<n> in the current
   directory, made exclusively. The .DEV's section into `dev`; the files
   written into `files` (one "  NAME\n" each) and the attach words into
   `attach` (" NAME" each, the .CT<n> alone); a name another run made into
   `clash`. */
int tr_cms_take(const char *base, int noSysinfo, const char *tool,
                const char *when, TR_TEXT *dev, TR_TEXT *files,
                TR_TEXT *attach, char *clash, size_t clashSize);

/* Save Report: each DIR\BASE.CT<n> appended to `f`, whole; 1 when every
   append succeeded. And the set's files removed from DIR. */
int tr_cms_append(void *f, const char *dir, const char *base);
void tr_cms_remove(const char *dir, const char *base);

#endif /* TR_CMSW_H */
