/*
 * tr_cms.c - XHCITR's reader of xhcicm.sys's state snapshot, the pure half
 * (design record 25 section 23, design record 21 section 8.4; roadmap-usb4
 * task 49.37): the image's checks, /nosysinfo's withholding and the decode
 * into the .CT<n> text. Host-tested in test_cm_snap, against the driver's
 * own builder and a canned fixture.
 *
 * The image names its own fields, so nothing here knows a structure of the
 * driver's: a field it never heard of is printed like any other.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>

#include "tr_cms.h"
#include "../src/cm/cm_snap.h"

#define TR_CMS_LINE 79

TR_U32 tr_cms_get32(const unsigned char *p)
{
    return (TR_U32)p[0] | ((TR_U32)p[1] << 8) | ((TR_U32)p[2] << 16) |
           ((TR_U32)p[3] << 24);
}

static void tr_cms_put32(unsigned char *p, TR_U32 v)
{
    p[0] = (unsigned char)(v & 0xFFUL);
    p[1] = (unsigned char)((v >> 8) & 0xFFUL);
    p[2] = (unsigned char)((v >> 16) & 0xFFUL);
    p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

char tr_cms_suffix(TR_U32 n)
{
    return (char)(n < 10 ? '0' + (int)n : 'A' + (int)(n - 10));
}

void tr_cms_saved_line(char *out, size_t cap, const char *name,
                       const char *sfx, const int *raw, int count)
{
    TR_TEXT t;
    int i;

    tr_text_init(&t, out, cap);
    if (count <= 0 || name == NULL) {
        return;
    }
    tr_printf(&t, "The Thunderbolt / USB4 state is in %s", name);
    for (i = 0; i < count; i++) {
        if (i > 0) {
            tr_put(&t, ", ");
        }
        if (raw[i]) {
            tr_printf(&t, ".CT%c and .CB%c", sfx[i], sfx[i]);
        } else {
            tr_printf(&t, ".CT%c", sfx[i]);
        }
    }
    tr_put(&t, ".");
}

/* Whether `line` (up to its end of line) is exactly "  NAME.<kind><c>". */
static int tr_cms_file_line(const char *line, const char *name,
                            const char *kind, char *c)
{
    size_t n = strlen(name);
    size_t k = strlen(kind);

    if (line[0] != ' ' || line[1] != ' ' || strncmp(line + 2, name, n) != 0 ||
        line[2 + n] != '.' || strncmp(line + 3 + n, kind, k) != 0) {
        return 0;
    }
    *c = line[3 + n + k];
    if (!((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'F'))) {
        return 0;
    }
    return line[4 + n + k] == '\r' || line[4 + n + k] == '\n' ||
           line[4 + n + k] == '\0';
}

int tr_cms_saved_files(const char *text, const char *name, char *sfx,
                       int *raw, int max)
{
    const char *p = text;
    int n = 0;
    int i;
    char c;

    if (text == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (tr_cms_file_line(p, name, "CT", &c) && n < max) {
            for (i = 0; i < n && sfx[i] != c; i++) {
            }
            if (i == n) {
                sfx[n] = c;
                raw[n] = 0;
                n++;
            }
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    p = text;
    while (*p != '\0') {
        if (tr_cms_file_line(p, name, "CB", &c)) {
            for (i = 0; i < n; i++) {
                if (sfx[i] == c) {
                    raw[i] = 1;
                }
            }
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return n;
}

/* [at, at + len) inside an image of `bytes`, with no wrap. */
static int tr_cms_inside(TR_U32 at, TR_U32 len, TR_U32 bytes)
{
    return at <= bytes && len <= bytes - at;
}

/* The end of a name already found good (tr_cms_name_ok). */
static TR_U32 tr_cms_name_end(const unsigned char *img, TR_U32 at)
{
    while (img[at] != 0) {
        at++;
    }
    return at + 1;
}

/* A name at `at`: NUL-terminated inside the image, CM_QS_NAME_MAX at most. */
static int tr_cms_name_ok(const unsigned char *img, TR_U32 bytes, TR_U32 at)
{
    TR_U32 i;

    for (i = 0; i <= CM_QS_NAME_MAX; i++) {
        if (at + i >= bytes || at + i < at) {
            return 0;
        }
        if (img[at + i] == 0) {
            return i > 0;
        }
    }
    return 0;
}

static void tr_cms_name(const unsigned char *img, TR_U32 at, char *out)
{
    TR_U32 i;
    char c;

    for (i = 0; i < CM_QS_NAME_MAX && img[at + i] != 0; i++) {
        c = (char)img[at + i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '[' ||
              c == ']')) {
            c = '?';
        }
        out[i] = c;
    }
    out[i] = '\0';
}

/*
 * The image's own checks, before anything reads past its header: every
 * directory, name and record inside the bytes, the widths and counts sane,
 * and - since tr_cms_withhold writes into the records - every table's
 * records after all of the metadata (the header, the directories and the
 * names) and no two tables' records overlapping, so a write into one can
 * never change what a later read of the directories finds.
 */
int tr_cms_check(const unsigned char *img, TR_U32 bytes)
{
    TR_U32 tablesAt;
    TR_U32 tables;
    TR_U32 metaEnd;
    TR_U32 t;
    TR_U32 u;

    if (img == NULL || bytes < CM_QS_HDR_BYTES ||
        tr_cms_get32(img + CM_QS_O_MAGIC) != CM_QS_MAGIC) {
        return TR_CMS_IMG_BAD;
    }
    if (tr_cms_get32(img + CM_QS_O_VERSION) != CM_QS_VERSION) {
        return TR_CMS_IMG_VERSION;
    }
    if (tr_cms_get32(img + CM_QS_O_HDR_BYTES) != CM_QS_HDR_BYTES ||
        tr_cms_get32(img + CM_QS_O_BYTES) != bytes) {
        return TR_CMS_IMG_BAD;
    }
    tablesAt = tr_cms_get32(img + CM_QS_O_TABLES_AT);
    tables = tr_cms_get32(img + CM_QS_O_TABLES);
    if (tables > 64 || tablesAt < CM_QS_HDR_BYTES ||
        !tr_cms_inside(tablesAt, tables * CM_QS_TAB_BYTES, bytes)) {
        return TR_CMS_IMG_BAD;
    }
    metaEnd = tablesAt + tables * CM_QS_TAB_BYTES;
    for (t = 0; t < tables; t++) {
        const unsigned char *e = img + tablesAt + t * CM_QS_TAB_BYTES;
        TR_U32 fieldsAt = tr_cms_get32(e + CM_QS_T_FIELDS_AT);
        TR_U32 fields = tr_cms_get32(e + CM_QS_T_FIELDS);
        TR_U32 recordsAt = tr_cms_get32(e + CM_QS_T_RECORDS_AT);
        TR_U32 records = tr_cms_get32(e + CM_QS_T_RECORDS);
        TR_U32 words = tr_cms_get32(e + CM_QS_T_RECORD_WORDS);
        TR_U32 sum = 0;
        TR_U32 i;

        if (tr_cms_get32(e + CM_QS_T_NAME) < CM_QS_HDR_BYTES ||
            !tr_cms_name_ok(img, bytes, tr_cms_get32(e + CM_QS_T_NAME)) ||
            fields > 0x10000UL || words > 0x10000UL || records > 0x10000UL ||
            fieldsAt < CM_QS_HDR_BYTES ||
            !tr_cms_inside(fieldsAt, fields * CM_QS_FLD_BYTES, bytes) ||
            (words != 0 && records > (bytes / 4) / words) ||
            !tr_cms_inside(recordsAt, records * words * 4, bytes) ||
            (recordsAt & 3UL) != 0) {
            return TR_CMS_IMG_BAD;
        }
        if (tr_cms_name_end(img, tr_cms_get32(e + CM_QS_T_NAME)) > metaEnd) {
            metaEnd = tr_cms_name_end(img, tr_cms_get32(e + CM_QS_T_NAME));
        }
        if (fieldsAt + fields * CM_QS_FLD_BYTES > metaEnd) {
            metaEnd = fieldsAt + fields * CM_QS_FLD_BYTES;
        }
        for (i = 0; i < fields; i++) {
            const unsigned char *f = img + fieldsAt + i * CM_QS_FLD_BYTES;
            TR_U32 info = tr_cms_get32(f + CM_QS_FL_INFO);
            TR_U32 w = CM_QS_FI_WIDTH(info);
            if (tr_cms_get32(f + CM_QS_FL_NAME) < CM_QS_HDR_BYTES ||
                !tr_cms_name_ok(img, bytes, tr_cms_get32(f + CM_QS_FL_NAME)) ||
                CM_QS_FI_COUNT(info) == 0 ||
                (w != 0 && w != 1 && w != 2 && w != 4) ||
                (w == 0 && CM_QS_FI_COUNT(info) != 1) ||
                /* A hash is one word of width 0, as the builder makes it,
                   and only that: anything else could keep an identity
                   that the withholding never clears. */
                (((CM_QS_FI_FLAGS(info) & CM_QS_FD_HASH) != 0) != (w == 0))) {
                return TR_CMS_IMG_BAD;
            }
            sum += CM_QS_FI_COUNT(info);
            if (tr_cms_name_end(img, tr_cms_get32(f + CM_QS_FL_NAME)) > metaEnd) {
                metaEnd = tr_cms_name_end(img, tr_cms_get32(f + CM_QS_FL_NAME));
            }
        }
        if (sum != words) {
            return TR_CMS_IMG_BAD;
        }
    }
    /* The records: after every piece of metadata, and apart. */
    for (t = 0; t < tables; t++) {
        const unsigned char *e = img + tablesAt + t * CM_QS_TAB_BYTES;
        TR_U32 at = tr_cms_get32(e + CM_QS_T_RECORDS_AT);
        TR_U32 len = tr_cms_get32(e + CM_QS_T_RECORDS) *
                     tr_cms_get32(e + CM_QS_T_RECORD_WORDS) * 4;

        if (len == 0) {
            continue;
        }
        if (at < metaEnd) {
            return TR_CMS_IMG_BAD;
        }
        for (u = 0; u < t; u++) {
            const unsigned char *o = img + tablesAt + u * CM_QS_TAB_BYTES;
            TR_U32 oat = tr_cms_get32(o + CM_QS_T_RECORDS_AT);
            TR_U32 olen = tr_cms_get32(o + CM_QS_T_RECORDS) *
                          tr_cms_get32(o + CM_QS_T_RECORD_WORDS) * 4;
            if (olen != 0 && at < oat + olen && oat < at + len) {
                return TR_CMS_IMG_BAD;
            }
        }
    }
    return TR_CMS_IMG_OK;
}

int tr_cms_window_agrees(const unsigned char *first, const unsigned char *hdr)
{
    return tr_cms_get32(hdr + CM_QS_O_MAGIC) == CM_QS_MAGIC &&
           tr_cms_get32(hdr + CM_QS_O_VERSION) ==
               tr_cms_get32(first + CM_QS_O_VERSION) &&
           tr_cms_get32(hdr + CM_QS_O_BYTES) ==
               tr_cms_get32(first + CM_QS_O_BYTES) &&
           tr_cms_get32(hdr + CM_QS_O_EPOCH_LO) ==
               tr_cms_get32(first + CM_QS_O_EPOCH_LO) &&
           tr_cms_get32(hdr + CM_QS_O_EPOCH_HI) ==
               tr_cms_get32(first + CM_QS_O_EPOCH_HI) &&
           tr_cms_get32(hdr + CM_QS_O_DET_BEFORE) ==
               tr_cms_get32(first + CM_QS_O_DET_BEFORE) &&
           tr_cms_get32(hdr + CM_QS_O_DET_AFTER) ==
               tr_cms_get32(first + CM_QS_O_DET_BEFORE) &&
           (tr_cms_get32(hdr + CM_QS_O_FLAGS) & CM_QS_F_TORN) == 0;
}

void tr_cms_withhold(unsigned char *img, TR_U32 bytes)
{
    TR_U32 tablesAt;
    TR_U32 tables;
    TR_U32 t;

    /* Checked first: the records it zeroes then lie apart from every
     * directory it reads (tr_cms_check). */
    if (tr_cms_check(img, bytes) != TR_CMS_IMG_OK) {
        return;
    }
    tablesAt = tr_cms_get32(img + CM_QS_O_TABLES_AT);
    tables = tr_cms_get32(img + CM_QS_O_TABLES);
    for (t = 0; t < tables; t++) {
        const unsigned char *e = img + tablesAt + t * CM_QS_TAB_BYTES;
        TR_U32 fieldsAt = tr_cms_get32(e + CM_QS_T_FIELDS_AT);
        TR_U32 fields = tr_cms_get32(e + CM_QS_T_FIELDS);
        TR_U32 recordsAt = tr_cms_get32(e + CM_QS_T_RECORDS_AT);
        TR_U32 records = tr_cms_get32(e + CM_QS_T_RECORDS);
        TR_U32 words = tr_cms_get32(e + CM_QS_T_RECORD_WORDS);
        TR_U32 r;
        TR_U32 i;
        TR_U32 w;

        for (r = 0; r < records; r++) {
            w = 0;
            for (i = 0; i < fields; i++) {
                TR_U32 info = tr_cms_get32(img + fieldsAt + i * CM_QS_FLD_BYTES +
                                           CM_QS_FL_INFO);
                if ((CM_QS_FI_FLAGS(info) & CM_QS_FD_HASH) && w < words) {
                    tr_cms_put32(img + recordsAt + (r * words + w) * 4, 0);
                }
                w += CM_QS_FI_COUNT(info);
            }
        }
    }
    tr_cms_put32(img + CM_QS_O_FLAGS,
                 tr_cms_get32(img + CM_QS_O_FLAGS) | CM_QS_F_WITHHELD);
}

/* One `name=value` item onto the line, wrapping at TR_CMS_LINE. */
typedef struct _TR_CMS_LINE_STATE {
    TR_TEXT *T;
    size_t Col;
    int Fresh;              /* at a line's indent: no separator owed */
} TR_CMS_LINE_STATE;

static void tr_cms_item(TR_CMS_LINE_STATE *s, const char *item)
{
    size_t len = strlen(item);

    if (!s->Fresh && s->Col + 2 + len > TR_CMS_LINE) {
        tr_put(s->T, "\n    ");
        s->Col = 4;
    } else if (!s->Fresh) {
        tr_put(s->T, "  ");
        s->Col += 2;
    }
    tr_put(s->T, item);
    s->Col += len;
    s->Fresh = 0;
}

static void tr_cms_value(char *out, size_t cap, TR_U32 v, TR_U32 flags,
                         int withheld)
{
    if (flags & CM_QS_FD_HASH) {
        if (withheld) {
            tr_sprintf(out, cap, "withheld");
        } else {
            tr_sprintf(out, cap, "#%04lx", v & 0xFFFFUL);
        }
    } else if ((flags & CM_QS_FD_SIGNED) && (v & 0x80000000UL)) {
        tr_sprintf(out, cap, "-%lu", (~v) + 1UL);
    } else if (v < 10) {
        tr_sprintf(out, cap, "%lu", v);
    } else {
        tr_sprintf(out, cap, "0x%lX", v);
    }
}

static void tr_cms_table(TR_TEXT *t, const unsigned char *img,
                         const unsigned char *e, int withheld)
{
    char name[CM_QS_NAME_MAX + 1];
    char item[CM_QS_NAME_MAX + 32];
    char value[24];
    TR_CMS_LINE_STATE s;
    TR_U32 fieldsAt = tr_cms_get32(e + CM_QS_T_FIELDS_AT);
    TR_U32 fields = tr_cms_get32(e + CM_QS_T_FIELDS);
    TR_U32 recordsAt = tr_cms_get32(e + CM_QS_T_RECORDS_AT);
    TR_U32 records = tr_cms_get32(e + CM_QS_T_RECORDS);
    TR_U32 words = tr_cms_get32(e + CM_QS_T_RECORD_WORDS);
    TR_U32 zeroFrom = 0xFFFFFFFFUL;
    TR_U32 r;

    tr_cms_name(img, tr_cms_get32(e + CM_QS_T_NAME), name);
    tr_printf(t, "[%s] %lu record%s, %lu field%s\n", name, records,
              records == 1 ? "" : "s", fields, fields == 1 ? "" : "s");
    s.T = t;
    for (r = 0; r <= records; r++) {
        const unsigned char *rec = img + recordsAt + r * words * 4;
        TR_U32 i;
        TR_U32 w;
        TR_U32 k;
        int any = 0;

        if (r < records) {
            for (k = 0; k < words && !any; k++) {
                any = tr_cms_get32(rec + k * 4) != 0;
            }
        }
        if (!any && r < records) {
            if (zeroFrom == 0xFFFFFFFFUL) {
                zeroFrom = r;
            }
            continue;
        }
        if (zeroFrom != 0xFFFFFFFFUL) {
            if (zeroFrom + 1 == r) {
                tr_printf(t, "  record %lu: all zero\n", zeroFrom);
            } else {
                tr_printf(t, "  records %lu to %lu: all zero\n", zeroFrom,
                          r - 1);
            }
            zeroFrom = 0xFFFFFFFFUL;
        }
        if (r == records) {
            break;
        }
        if (records > 1) {
            tr_sprintf(item, sizeof(item), "  #%lu", r);
            tr_put(t, item);
            s.Col = strlen(item);
            s.Fresh = 0;
        } else {
            tr_put(t, "    ");
            s.Col = 4;
            s.Fresh = 1;
        }
        w = 0;
        for (i = 0; i < fields; i++) {
            const unsigned char *f = img + fieldsAt + i * CM_QS_FLD_BYTES;
            TR_U32 info = tr_cms_get32(f + CM_QS_FL_INFO);
            TR_U32 count = CM_QS_FI_COUNT(info);
            TR_U32 flags = CM_QS_FI_FLAGS(info);

            tr_cms_name(img, tr_cms_get32(f + CM_QS_FL_NAME), name);
            for (k = 0; k < count; k++) {
                TR_U32 v = tr_cms_get32(rec + (w + k) * 4);
                if (v == 0) {
                    continue;
                }
                tr_cms_value(value, sizeof(value), v, flags, withheld);
                if (count > 1) {
                    tr_sprintf(item, sizeof(item), "%s[%lu]=%s", name, k, value);
                } else {
                    tr_sprintf(item, sizeof(item), "%s=%s", name, value);
                }
                tr_cms_item(&s, item);
            }
            w += count;
        }
        tr_put(t, "\n");
    }
}

void tr_cms_decode(TR_TEXT *t, const unsigned char *img, TR_U32 bytes,
                   const TR_CMS_HEAD *h)
{
    TR_U32 flags;
    TR_U32 drv;
    TR_U32 tablesAt;
    TR_U32 tables;
    TR_U32 i;
    int withheld;
    int rc;

    tr_put(t, "xhcicm.sys state snapshot - XHCITR /snapshot\n");
    tr_printf(t, "  tool        %s\n", h->Tool);
    tr_printf(t, "  local time  %s\n", h->When);
    if (h->Bdf == 0xFFFFFFFFUL) {
        tr_printf(t, "  device      XHCICM%lu, the NHI's location not read\n",
                  h->Device);
    } else {
        tr_printf(t, "  device      XHCICM%lu, the NHI at bus %lu, device %lu, "
                  "function %lu\n", h->Device, (h->Bdf >> 8) & 0xFFUL,
                  (h->Bdf >> 3) & 0x1FUL, h->Bdf & 7UL);
    }
    rc = tr_cms_check(img, bytes);
    if (rc == TR_CMS_IMG_VERSION) {
        tr_printf(t, "  image       version %lu, which this XHCITR does not "
                  "read; send the .CB%c\n", tr_cms_get32(img + CM_QS_O_VERSION),
                  tr_cms_suffix(h->Device));
        return;
    }
    if (rc != TR_CMS_IMG_OK) {
        tr_printf(t, "  image       not readable: %lu bytes that do not hold "
                  "a whole image\n", bytes);
        return;
    }
    flags = tr_cms_get32(img + CM_QS_O_FLAGS);
    drv = tr_cms_get32(img + CM_QS_O_DRIVER);
    tablesAt = tr_cms_get32(img + CM_QS_O_TABLES_AT);
    tables = tr_cms_get32(img + CM_QS_O_TABLES);
    withheld = h->NoSysinfo || (flags & CM_QS_F_WITHHELD) != 0;
    tr_printf(t, "  image       version %lu, %lu bytes, %lu tables; driver "
              "%lu.%lu.%lu.%lu, level %lu\n", tr_cms_get32(img + CM_QS_O_VERSION),
              bytes, tables, drv >> 24, (drv >> 16) & 0xFFUL,
              (drv >> 8) & 0xFFUL, drv & 0xFFUL,
              tr_cms_get32(img + CM_QS_O_LEVEL));
    tr_printf(t, "  running     %s%s%s%s\n",
              (flags & CM_QS_F_U4) ? "the USB4 connection manager"
              : (flags & CM_QS_F_ICM) ? "the firmware connection manager's client"
                                      : "no connection manager",
              (flags & CM_QS_F_INERT) ? ", inert" : "",
              (flags & CM_QS_F_NO_THREAD) ? ", no thread" : "",
              (flags & CM_QS_F_STOPPING) ? ", stopping" : "");
    tr_printf(t, "  clock       %lu ms (the connection manager's)\n",
              tr_cms_get32(img + CM_QS_O_CLOCK_MS));
    if (h->Agreed) {
        tr_printf(t, "  read        no counted event during the read "
                  "(detector %lu), %d tr%s\n",
                  tr_cms_get32(img + CM_QS_O_DET_BEFORE), h->Tries,
                  h->Tries == 1 ? "y" : "ies");
    } else {
        tr_printf(t, "  read        taken while it was changing: values may "
                  "be a mixture\n              (detector %lu to %lu, %d "
                  "tr%s)\n", tr_cms_get32(img + CM_QS_O_DET_BEFORE),
                  tr_cms_get32(img + CM_QS_O_DET_AFTER), h->Tries,
                  h->Tries == 1 ? "y" : "ies");
    }
    if (flags & CM_QS_F_CUT) {
        tr_put(t, "  cut         the image reached its bound: the last "
                  "tables are short\n");
    }
    if (withheld) {
        tr_put(t, "  identities  withheld (/nosysinfo)\n");
    } else {
        tr_put(t, "  #xxxx       an identity (a UUID, UID, MAC address or "
                  "serial) as a hash\n              salted anew at every "
                  "start; equal hashes may be one device\n");
    }
    tr_put(t, "  Only nonzero values are printed; 0x is hexadecimal.\n\n");
    for (i = 0; i < tables; i++) {
        tr_cms_table(t, img, img + tablesAt + i * CM_QS_TAB_BYTES, withheld);
    }
    tr_put(t, "end of state snapshot\n");
}
