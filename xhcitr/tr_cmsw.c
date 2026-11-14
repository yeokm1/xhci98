/*
 * tr_cmsw.c - XHCITR's reader of xhcicm.sys's state snapshot, the Windows
 * half (design record 25 section 23, design record 21 section 8.4;
 * roadmap-usb4 task 49.37).
 *
 * Every query device \\.\XHCICM0 to \\.\XHCICM15 is asked for its image
 * (CM_QS_IOCTL), window after window, up to three reads until one tear
 * detector holds across the whole read; the image goes to BASE.CB<n> and
 * its decode to BASE.CT<n>, each made under a .TMP name with CREATE_NEW and
 * renamed with a bare MoveFileA, so no file this run did not make is ever
 * replaced (record 25 section 15).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "tr_cmsw.h"
#include "../src/cm/cm_snap.h"
#include "../src/cm/cm_query.h"

#define TR_CMS_DEVICES  16UL
#define TR_CMS_TRIES    3
#define TR_CMS_TEXT     (1024UL * 1024UL)

static const char *const trCmsKinds[] = { ".CT", ".CB" };

/* TR_TEXT writes CR LF; the screen and the .DEV go through text-mode
   output, which adds its own CR. */
static void tr_cms_lf(TR_TEXT *t)
{
    size_t i;
    size_t o = 0;

    for (i = 0; i < t->Len; i++) {
        if (t->Buf[i] != '\r') {
            t->Buf[o++] = t->Buf[i];
        }
    }
    t->Len = o;
    if (t->Cap > 0) {
        t->Buf[o] = '\0';
    }
}

int tr_cms_set_taken(const char *base, int (*exists)(const char *, void *),
                     void *context, char *which, size_t whichSize)
{
    char name[32];
    TR_U32 n;
    int k;
    int tmp;

    for (n = 0; n < TR_CMS_DEVICES; n++) {
        for (k = 0; k < 2; k++) {
            for (tmp = 0; tmp < 2; tmp++) {
                tr_sprintf(name, sizeof(name), "%s%s%c%s", base, trCmsKinds[k],
                           tr_cms_suffix(n), tmp ? ".TMP" : "");
                if (exists(name, context)) {
                    if (which != NULL && whichSize != 0) {
                        tr_copy(which, name, whichSize);
                    }
                    return 1;
                }
            }
        }
    }
    if (which != NULL && whichSize != 0) {
        which[0] = '\0';
    }
    return 0;
}

/* The NHI's location and epoch from its XCMQ header; 0 when unread. */
static int tr_cms_query(HANDLE h, TR_U32 *bdf, TR_U32 *epochLo,
                        TR_U32 *epochHi)
{
    unsigned char b[CM_Q_HDR_BYTES];
    DWORD got = 0;

    if (!DeviceIoControl(h, CM_Q_IOCTL, NULL, 0, b, sizeof(b), &got, NULL) ||
        got < CM_Q_HDR_BYTES || tr_cms_get32(b + CM_Q_O_MAGIC) != CM_Q_MAGIC) {
        return 0;
    }
    *bdf = tr_cms_get32(b + CM_Q_O_NHI_BDF);
    *epochLo = tr_cms_get32(b + CM_Q_O_EPOCH_LO);
    *epochHi = tr_cms_get32(b + CM_Q_O_EPOCH_HI);
    return 1;
}

static void tr_cms_set32(unsigned char *p, TR_U32 v)
{
    p[0] = (unsigned char)(v & 0xFFUL);
    p[1] = (unsigned char)((v >> 8) & 0xFFUL);
    p[2] = (unsigned char)((v >> 16) & 0xFFUL);
    p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

/* One whole read of the image: 1 read, with *agreed; 0 failed, with the
   Win32 error (0 when the answers themselves were wrong). */
static int tr_cms_read(HANDLE h, unsigned char *win, unsigned char **img,
                       TR_U32 *bytes, int *agreed, DWORD *err)
{
    unsigned char *out = NULL;
    TR_U32 total = 0;
    TR_U32 at = 0;
    TR_U32 lo = 0;
    TR_U32 hi = 0;
    DWORD got;
    int ok = 1;

    *agreed = 1;
    *err = 0;
    for (;;) {
        TR_U32 n;
        TR_U32 before;
        TR_U32 after;
        DWORD in = at;

        got = 0;
        if (!DeviceIoControl(h, CM_QS_IOCTL, &in, sizeof(in), win,
                             CM_QS_ASK_BYTES, &got, NULL)) {
            *err = GetLastError();
            ok = 0;
            break;
        }
        if (got < CM_QS_HDR_BYTES ||
            tr_cms_get32(win + CM_QS_O_MAGIC) != CM_QS_MAGIC ||
            tr_cms_get32(win + CM_QS_O_WIN_AT) != at) {
            ok = 0;
            break;
        }
        before = tr_cms_get32(win + CM_QS_O_DET_BEFORE);
        after = tr_cms_get32(win + CM_QS_O_DET_AFTER);
        if (out == NULL) {
            total = tr_cms_get32(win + CM_QS_O_BYTES);
            if (total < CM_QS_HDR_BYTES || total > CM_QS_MAX_BYTES) {
                ok = 0;
                break;
            }
            out = (unsigned char *)malloc(total);
            if (out == NULL) {
                ok = 0;
                break;
            }
            memcpy(out, win, CM_QS_HDR_BYTES);
            lo = before;
            hi = before;
        } else if (!tr_cms_window_agrees(out, win)) {
            *agreed = 0;
            if (tr_cms_get32(win + CM_QS_O_BYTES) != total) {
                ok = 0;     /* another image: this read is not one */
                break;
            }
        }
        if (before != after ||
            (tr_cms_get32(win + CM_QS_O_FLAGS) & CM_QS_F_TORN) != 0) {
            *agreed = 0;
        }
        if (before < lo) {
            lo = before;
        }
        if (after < lo) {
            lo = after;
        }
        if (before > hi) {
            hi = before;
        }
        if (after > hi) {
            hi = after;
        }
        n = (TR_U32)got - CM_QS_HDR_BYTES;
        if (n > total - CM_QS_HDR_BYTES - at) {
            ok = 0;
            break;
        }
        memcpy(out + CM_QS_HDR_BYTES + at, win + CM_QS_HDR_BYTES, n);
        at += n;
        if (at == total - CM_QS_HDR_BYTES) {
            break;
        }
        if (n == 0) {
            ok = 0;
            break;
        }
    }
    if (!ok) {
        free(out);
        return 0;
    }
    tr_cms_set32(out + CM_QS_O_WIN_AT, 0);
    if (!*agreed) {
        tr_cms_set32(out + CM_QS_O_FLAGS,
                     tr_cms_get32(out + CM_QS_O_FLAGS) | CM_QS_F_TORN);
        tr_cms_set32(out + CM_QS_O_DET_BEFORE, lo);
        tr_cms_set32(out + CM_QS_O_DET_AFTER, hi);
    }
    *img = out;
    *bytes = total;
    return 1;
}

/* NAME made exclusively through NAME.TMP: 0 written, 1 a name of the two
   existed (another run's; left alone), 2 not written in full. */
static int tr_cms_write(const char *name, const void *data, TR_U32 len)
{
    char tmp[40];
    HANDLE f;
    DWORD w = 0;
    BOOL ok;

    tr_sprintf(tmp, sizeof(tmp), "%s.TMP", name);
    f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_EXISTS ? 1 : 2;
    }
    ok = WriteFile(f, data, (DWORD)len, &w, NULL) && w == (DWORD)len;
    if (!CloseHandle(f)) {
        ok = FALSE;
    }
    if (!ok) {
        DeleteFileA(tmp);
        return 2;
    }
    if (!MoveFileA(tmp, name)) {
        DWORD e = GetLastError();
        DeleteFileA(tmp);
        return (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) ? 1 : 2;
    }
    return 0;
}

int tr_cms_take(const char *base, int noSysinfo, const char *tool,
                const char *when, TR_TEXT *dev, TR_TEXT *files,
                TR_TEXT *attach, char *clash, size_t clashSize)
{
    unsigned char *win;
    char *text;
    int rc = TR_CMS_TAKE_OK;
    int found = 0;
    TR_U32 n;

    clash[0] = '\0';
    tr_put(dev, "\n  Thunderbolt connection manager state (xhcicm.sys):\n");
    win = (unsigned char *)malloc(CM_QS_ASK_BYTES);
    text = (char *)malloc(TR_CMS_TEXT);
    if (win == NULL || text == NULL) {
        free(win);
        free(text);
        tr_put(dev, "    not read: no memory\n");
        return TR_CMS_TAKE_OK;
    }
    for (n = 0; n < TR_CMS_DEVICES && rc != TR_CMS_TAKE_CLASH; n++) {
        char path[24];
        char ct[32];
        char cb[32];
        TR_CMS_HEAD head;
        TR_TEXT t;
        unsigned char *img = NULL;
        TR_U32 bytes = 0;
        TR_U32 bdf = 0xFFFFFFFFUL;
        TR_U32 epochLo = 0;
        TR_U32 epochHi = 0;
        HANDLE h;
        DWORD err = 0;
        int agreed = 0;
        int tries;
        int check;
        int wrb;
        int wrt;
        int raw;

        tr_sprintf(path, sizeof(path), "\\\\.\\XHCICM%lu", n);
        h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        found++;
        if (!tr_cms_query(h, &bdf, &epochLo, &epochHi)) {
            bdf = 0xFFFFFFFFUL;
        }
        for (tries = 1; tries <= TR_CMS_TRIES; tries++) {
            free(img);
            img = NULL;
            if (!tr_cms_read(h, win, &img, &bytes, &agreed, &err)) {
                break;
            }
            if (agreed) {
                break;
            }
        }
        if (tries > TR_CMS_TRIES) {
            tries = TR_CMS_TRIES;
        }
        CloseHandle(h);
        if (img == NULL) {
            if (err == ERROR_INVALID_FUNCTION) {
                tr_printf(dev, "    XHCICM%lu: not served by this xhcicm.sys\n", n);
            } else if (err != 0) {
                tr_printf(dev, "    XHCICM%lu: not read: error %lu\n", n,
                          (TR_U32)err);
            } else {
                tr_printf(dev, "    XHCICM%lu: not read: its answers did not "
                          "make one image\n", n);
            }
            continue;
        }
        if (bdf != 0xFFFFFFFFUL &&
            (tr_cms_get32(img + CM_QS_O_EPOCH_LO) != epochLo ||
             tr_cms_get32(img + CM_QS_O_EPOCH_HI) != epochHi)) {
            tr_printf(dev, "    XHCICM%lu: not kept: the connection manager "
                      "restarted during the read\n", n);
            free(img);
            continue;
        }
        check = tr_cms_check(img, bytes);
        if (noSysinfo && check == TR_CMS_IMG_OK) {
            tr_cms_withhold(img, bytes);
        }
        tr_sprintf(ct, sizeof(ct), "%s.CT%c", base, tr_cms_suffix(n));
        tr_sprintf(cb, sizeof(cb), "%s.CB%c", base, tr_cms_suffix(n));
        head.Tool = tool;
        head.When = when;
        head.Device = n;
        head.Bdf = bdf;
        head.Tries = tries;
        head.Agreed = agreed;
        head.NoSysinfo = noSysinfo;
        tr_text_init(&t, text, TR_CMS_TEXT);
        tr_cms_decode(&t, img, bytes, &head);
        if (noSysinfo && check != TR_CMS_IMG_OK) {
            tr_put(&t, "  The raw image is not saved: under /nosysinfo an "
                       "image this XHCITR cannot\n  screen is not written.\n");
        }
        if (t.Overflow) {
            tr_put(&t, "\r\n*** cut short: the text did not fit\r\n");
        }
        /* raw: this run made the .CB<n>, the one file a clash on the
         * .CT<n> takes back. */
        raw = !(noSysinfo && check != TR_CMS_IMG_OK);
        wrb = raw ? tr_cms_write(cb, img, bytes) : 0;
        wrt = (wrb == 1) ? 1 : tr_cms_write(ct, text, (TR_U32)t.Len);
        free(img);
        if (wrb == 1 || wrt == 1) {
            if (raw && wrb == 0) {
                DeleteFileA(cb);
            }
            tr_copy(clash, wrb == 1 ? cb : ct, clashSize);
            tr_printf(dev, "    XHCICM%lu: not kept: %s appeared after this "
                      "run chose its name, and was left alone\n", n, clash);
            rc = TR_CMS_TAKE_CLASH;
            continue;
        }
        if (raw && wrb == 0) {
            tr_printf(files, "  %s\n", cb);
        }
        if (wrt == 0) {
            tr_printf(files, "  %s\n", ct);
            tr_printf(attach, " %s", ct);
        }
        if (wrb == 2 || wrt == 2 || t.Overflow) {
            rc = TR_CMS_TAKE_INCOMPLETE;
        }
        tr_printf(dev, "    XHCICM%lu: %s %s%s%s\n      (%lu bytes, %s)\n",
                  n, (wrt == 0 && wrb != 2) ? "state saved in"
                                            : "state NOT saved in full in",
                  ct, raw ? " and " : "", raw ? cb : "", bytes,
                  agreed ? "no counted event during the read"
                         : "taken while it was changing");
    }
    if (found == 0) {
        tr_put(dev, "    No connection manager is running.\n");
    }
    free(win);
    free(text);
    tr_cms_lf(dev);
    tr_cms_lf(files);
    tr_cms_lf(attach);
    return rc;
}

int tr_cms_append(void *f, const char *dir, const char *base)
{
    char name[MAX_PATH + 16];
    char buf[4096];
    HANDLE in;
    DWORD got;
    DWORD w;
    TR_U32 n;
    int ok = 1;

    for (n = 0; n < TR_CMS_DEVICES; n++) {
        tr_sprintf(name, sizeof(name), "%s\\%s.CT%c", dir, base,
                   tr_cms_suffix(n));
        in = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, 0, NULL);
        if (in == INVALID_HANDLE_VALUE) {
            continue;
        }
        for (;;) {
            if (!ReadFile(in, buf, sizeof(buf), &got, NULL)) {
                ok = 0;
                break;
            }
            if (got == 0) {
                break;
            }
            if (!WriteFile((HANDLE)f, buf, got, &w, NULL) || w != got) {
                ok = 0;
                break;
            }
        }
        CloseHandle(in);
        if (!WriteFile((HANDLE)f, "\r\n", 2, &w, NULL) || w != 2) {
            ok = 0;
        }
    }
    return ok;
}

void tr_cms_remove(const char *dir, const char *base)
{
    char name[MAX_PATH + 16];
    TR_U32 n;
    int k;

    for (n = 0; n < TR_CMS_DEVICES; n++) {
        for (k = 0; k < 2; k++) {
            tr_sprintf(name, sizeof(name), "%s\\%s%s%c", dir, base,
                       trCmsKinds[k], tr_cms_suffix(n));
            DeleteFileA(name);
            tr_sprintf(name, sizeof(name), "%s\\%s%s%c.TMP", dir, base,
                       trCmsKinds[k], tr_cms_suffix(n));
            DeleteFileA(name);
        }
    }
}
