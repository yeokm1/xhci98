/*
 * tr_cms.h - XHCITR's reader of xhcicm.sys's state snapshot, the pure half
 * (design record 25 section 23, design record 21 section 8.4; roadmap-usb4
 * task 49.37). The Windows half is tr_cmsw.c.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TR_CMS_H
#define TR_CMS_H

#include "tr_core.h"

/* What tr_cms_check says of an image. */
#define TR_CMS_IMG_OK        0
#define TR_CMS_IMG_VERSION   1   /* a version this tool does not read */
#define TR_CMS_IMG_BAD       2   /* not an image, or cut short         */

int tr_cms_check(const unsigned char *img, TR_U32 bytes);
TR_U32 tr_cms_get32(const unsigned char *p);

/* The windows of one read agree: the same size, epoch and detector. */
int tr_cms_window_agrees(const unsigned char *first, const unsigned char *hdr);

/* Every hashed word zeroed and CM_QS_F_WITHHELD set (/nosysinfo). A
   checked image only. */
void tr_cms_withhold(unsigned char *img, TR_U32 bytes);

/* The head's facts the image does not carry. */
typedef struct _TR_CMS_HEAD {
    const char *Tool;       /* "XHCITR 3.5.0.0, built ..."               */
    const char *When;       /* the local time, "2026-11-12 14:05"        */
    TR_U32 Device;          /* XHCICM<n>                                  */
    TR_U32 Bdf;             /* CM_Q_O_NHI_BDF, or 0xFFFFFFFF unread       */
    int Tries;              /* reads made, 1 to 3                         */
    int Agreed;             /* one detector held across the read          */
    int NoSysinfo;
} TR_CMS_HEAD;

/* The .CT<n> text: the head, then every table. Lines end CR LF, 79
   characters at most. */
void tr_cms_decode(TR_TEXT *t, const unsigned char *img, TR_U32 bytes,
                   const TR_CMS_HEAD *h);

/* The file suffix character of device n: 0-9, then A-F. */
char tr_cms_suffix(TR_U32 n);

/* Save Snapshot's line for the images saved (record 25 section 23 item 7,
   the owner's answer of 2026-11-13): `sfx` the suffixes whose .CT<n> was
   written, `raw` 1 where its .CB<n> was too, `count` of them; "" when
   none. */
void tr_cms_saved_line(char *out, size_t cap, const char *name,
                       const char *sfx, const int *raw, int count);
/* The images a /snapshot run says it wrote: its "files written" lines
   "  NAME.CT<n>" and "  NAME.CB<n>" in `text` (the run's output), into
   sfx/raw (16 at most); returns how many .CT<n>. */
int tr_cms_saved_files(const char *text, const char *name, char *sfx,
                       int *raw, int max);
#define TR_CMS_SAVED_LINE_MAX 400   /* 16 images and an 8.3 name */

#endif /* TR_CMS_H */
