/*
 * test_cm_snap.c - the connection manager's state snapshot (design record 21
 * section 8.4, design record 25 section 23; roadmap-usb4 task 49.37): the
 * generated field directories of the pure core's structures, the image's
 * builder (src\cm\cm_snap.c) and XHCITR's reader of it (xhcitr\tr_cms.c),
 * over images the builder made and over a canned fixture.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/cm/cm_snapb.h"
#include "../src/cm/tbt_u4cm.h"
#include "../src/cm/tbt_icm.h"
#include "../src/cm/tbt_ring.h"
#include "../src/cm/tbt_chain.h"
#include "../xhcitr/tr_cms.h"
#include "test_harness.h"

#define IMG_MAX     (CM_QS_MAX_BYTES + 4096UL)
#define TEXT_MAX    (2UL * 1024UL * 1024UL)

typedef struct {
    const char *Name;
    const CM_QS_FIELD *Fields;
    ULONG Count;
    ULONG Size;
} DIR_UNDER_TEST;

static unsigned char *img;
static unsigned char *img2;
static char *text;

/* Every entry inside its structure, whole elements, no two overlapping. */
static void check_directory(const DIR_UNDER_TEST *d)
{
    char what[160];
    unsigned char *owner;
    ULONG i;
    ULONG b;
    int overlap = 0;
    int outside = 0;
    int shape = 0;

    owner = (unsigned char *)calloc(d->Size, 1);
    for (i = 0; i < d->Count; i++) {
        const CM_QS_FIELD *f = &d->Fields[i];
        if (f->Offset + f->Size > d->Size || f->Size == 0) {
            outside++;
            continue;
        }
        if (!(f->Width == 0 || f->Width == 1 || f->Width == 2 ||
              f->Width == 4) || (f->Width != 0 && f->Size % f->Width != 0) ||
            strlen(f->Name) > CM_QS_NAME_MAX) {
            shape++;
        }
        for (b = 0; b < f->Size; b++) {
            if (owner[f->Offset + b]) {
                overlap++;
            }
            owner[f->Offset + b] = 1;
        }
    }
    free(owner);
    sprintf(what, "%s: every field inside the structure", d->Name);
    CHECK(outside == 0, what);
    sprintf(what, "%s: widths 1, 2, 4 or a hash, whole elements", d->Name);
    CHECK(shape == 0, what);
    sprintf(what, "%s: no two fields overlap", d->Name);
    CHECK(overlap == 0, what);
    /* No held packet's words (a TBT_FRAME's Dw: UUIDs and property
       blocks travel in them). */
    for (i = 0; i < d->Count; i++) {
        size_t len = strlen(d->Fields[i].Name);
        if (len >= 3 && strcmp(d->Fields[i].Name + len - 3, ".Dw") == 0) {
            sprintf(what, "%s: a packet's words listed: %s", d->Name,
                    d->Fields[i].Name);
            CHECK(0, what);
        }
    }
}

/* Each field's elements set to their own value, one that names the field. */
static ULONG pattern(ULONG table, ULONG field, ULONG element, ULONG width)
{
    ULONG v = ((table + 1) << 20) | ((field + 1) << 6) | (element + 1);
    if (width == 1) {
        return (v % 254) + 1;
    }
    if (width == 2) {
        return (v % 65534) + 1;
    }
    return v;
}

static void fill(unsigned char *base, const CM_QS_FIELD *f, ULONG n, ULONG t)
{
    ULONG i;
    ULONG e;

    for (i = 0; i < n; i++) {
        if (f[i].Width == 0) {
            for (e = 0; e < f[i].Size; e++) {
                base[f[i].Offset + e] = (unsigned char)(i + e + t);
            }
            continue;
        }
        for (e = 0; e < f[i].Size / f[i].Width; e++) {
            ULONG v = pattern(t, i, e, f[i].Width);
            unsigned char *p = base + f[i].Offset + e * f[i].Width;
            p[0] = (unsigned char)(v & 0xFF);
            if (f[i].Width >= 2) {
                p[1] = (unsigned char)((v >> 8) & 0xFF);
            }
            if (f[i].Width == 4) {
                p[2] = (unsigned char)((v >> 16) & 0xFF);
                p[3] = (unsigned char)((v >> 24) & 0xFF);
            }
        }
    }
}

/* What the reader prints for one element: its own formatting rules. */
static void expect(char *out, const char *name, ULONG count, ULONG k, ULONG v,
                   ULONG flags)
{
    char value[24];

    if (flags & CM_QS_FD_HASH) {
        sprintf(value, "#%04lx", v & 0xFFFFUL);
    } else if (v < 10) {
        sprintf(value, "%lu", v);
    } else {
        sprintf(value, "0x%lX", v);
    }
    if (count > 1) {
        sprintf(out, "%s[%lu]=%s", name, k, value);
    } else {
        sprintf(out, "%s=%s", name, value);
    }
}

static TBT_U4CM u4;
static TBT_ICM icm;
static TBT_RING ring;
static XHCI_CHAIN_BIND binds[3];

static void test_roundtrip(void)
{
    CM_QS_TABLE t[6];
    CM_QS_IN in;
    TR_CMS_HEAD h;
    TR_TEXT tx;
    ULONG total;
    ULONG bytes;
    ULONG agree = 0;
    ULONG missing = 0;
    ULONG n;
    ULONG i;
    ULONG k;
    ULONG r;
    char item[128];
    char what[160];

    memset(&u4, 0, sizeof(u4));
    memset(&icm, 0, sizeof(icm));
    memset(&ring, 0, sizeof(ring));
    memset(binds, 0, sizeof(binds));
    fill((unsigned char *)&u4, CmQsF_TBT_U4CM, CmQsN_TBT_U4CM, 0);
    fill((unsigned char *)&u4.Ports[1], CmQsF_TBT_U4_PORT, CmQsN_TBT_U4_PORT, 1);
    fill((unsigned char *)&icm.Devices[2], CmQsF_TBT_ICM_DEVICE,
         CmQsN_TBT_ICM_DEVICE, 2);
    fill((unsigned char *)&ring, CmQsF_TBT_RING, CmQsN_TBT_RING, 3);
    fill((unsigned char *)&binds[0], CmQsF_XHCI_CHAIN_BIND,
         CmQsN_XHCI_CHAIN_BIND, 4);
    fill((unsigned char *)&binds[2], CmQsF_XHCI_CHAIN_BIND,
         CmQsN_XHCI_CHAIN_BIND, 4);
    n = 0;
    t[n].Name = "u4"; t[n].Fields = CmQsF_TBT_U4CM; t[n].FieldCount = CmQsN_TBT_U4CM;
    t[n].Base = (const UCHAR *)&u4; t[n].Records = 1; t[n].Stride = sizeof(u4); n++;
    t[n].Name = "u4.port"; t[n].Fields = CmQsF_TBT_U4_PORT; t[n].FieldCount = CmQsN_TBT_U4_PORT;
    t[n].Base = (const UCHAR *)u4.Ports; t[n].Records = TBT_U4_MAX_PORTS;
    t[n].Stride = sizeof(TBT_U4_PORT); n++;
    t[n].Name = "icm.dev"; t[n].Fields = CmQsF_TBT_ICM_DEVICE; t[n].FieldCount = CmQsN_TBT_ICM_DEVICE;
    t[n].Base = (const UCHAR *)icm.Devices; t[n].Records = TBT_ICM_MAX_DEVICES;
    t[n].Stride = sizeof(TBT_ICM_DEVICE); n++;
    t[n].Name = "ring"; t[n].Fields = CmQsF_TBT_RING; t[n].FieldCount = CmQsN_TBT_RING;
    t[n].Base = (const UCHAR *)&ring; t[n].Records = 1; t[n].Stride = sizeof(ring); n++;
    t[n].Name = "chain.bind"; t[n].Fields = CmQsF_XHCI_CHAIN_BIND; t[n].FieldCount = CmQsN_XHCI_CHAIN_BIND;
    t[n].Base = (const UCHAR *)binds; t[n].Records = 3; t[n].Stride = sizeof(binds[0]); n++;
    t[n].Name = "icm"; t[n].Fields = CmQsF_TBT_ICM; t[n].FieldCount = CmQsN_TBT_ICM;
    t[n].Base = NULL; t[n].Records = 1; t[n].Stride = sizeof(icm); n++;

    memset(&in, 0, sizeof(in));
    in.DetBefore = 1234;
    in.EpochLo = 0x11111111UL;
    in.EpochHi = 0x22222222UL;
    in.Driver = CM_QS_DRIVER_OF(3, 5, 0, 0);
    in.Level = 2;
    in.ClockMs = 0x377ECUL;
    in.Layout = 99999;
    in.Salt = 0xC0FFEE11UL;
    in.Flags = CM_QS_F_U4;
    bytes = CmQsBuild(img, IMG_MAX, 0, &in, t, n, &total);
    CmQsSetAfter(img, 1234);
    CHECK(total <= CM_QS_WINDOW_MAX + CM_QS_HDR_BYTES, "the test's image fits one window");
    CHECK_EQ(bytes, total, "one window carries the whole image");
    CHECK_EQ(tr_cms_check(img, total), TR_CMS_IMG_OK, "the reader takes the builder's image");
    CHECK_EQ(tr_cms_get32(img + CM_QS_O_FLAGS) & CM_QS_F_TORN, 0, "an even detector: not torn");

    h.Tool = "XHCITR test";
    h.When = "2026-11-12 14:00";
    h.Device = 0;
    h.Bdf = 0x0100;
    h.Tries = 1;
    h.Agreed = 1;
    h.NoSysinfo = 0;
    tr_text_init(&tx, text, TEXT_MAX);
    tr_cms_decode(&tx, img, total, &h);
    CHECK(!tx.Overflow, "the decode fits");
    /* Every element of every filled record, by name and value. */
    for (k = 0; k < 5; k++) {
        const CM_QS_FIELD *f = t[k].Fields;
        const UCHAR *base;
        for (r = 0; r < t[k].Records; r++) {
            ULONG tag = (k == 0) ? 0 : (k == 1) ? 1 : (k == 2) ? 2 : (k == 3) ? 3 : 4;
            if ((k == 1 && r != 1) || (k == 2 && r != 2) || (k == 4 && r == 1)) {
                continue;
            }
            base = t[k].Base + r * t[k].Stride;
            for (i = 0; i < t[k].FieldCount; i++) {
                ULONG count = CmQsFieldWords(&f[i]);
                ULONG e;
                for (e = 0; e < count; e++) {
                    ULONG v = (f[i].Width == 0)
                        ? CmQsHash(in.Salt, base + f[i].Offset, f[i].Size)
                        : pattern(tag, i, e, f[i].Width);
                    if (v == 0) {
                        continue;
                    }
                    expect(item, f[i].Name, count, e, v, f[i].Flags);
                    if (strstr(text, item) != NULL) {
                        agree++;
                    } else {
                        if (missing < 5) {
                            printf("  missing: [%s] #%lu %s\n", t[k].Name, r, item);
                        }
                        missing++;
                    }
                }
            }
        }
    }
    sprintf(what, "cm snapshot: all %lu fields agree", agree);
    CHECK(missing == 0, what);
    printf("cm snapshot: all %lu fields agree (%lu missing)\n", agree, missing);
    CHECK(strstr(text, "[u4.port] 4 records") != NULL, "a table's head");
    CHECK(strstr(text, "  record 0: all zero") != NULL, "a zero record counted");
    CHECK(strstr(text, "  records 2 to 3: all zero") != NULL, "zero records counted");
    CHECK(strstr(text, "[icm] 0 records") != NULL, "an absent table: no records");
    CHECK(strstr(text, "no counted event during the read (detector 1234)") != NULL,
          "the detector in the head");
    CHECK(strstr(text, "driver 3.5.0.0, level 2") != NULL, "the driver's version");
    CHECK(strstr(text, "the NHI at bus 1, device 0, function 0") != NULL, "the location");
    /* 79 columns, CR LF. */
    {
        const char *p = text;
        int longest = 0;
        int bare = 0;
        while (*p != '\0') {
            const char *e = strchr(p, '\n');
            int len;
            if (e == NULL) {
                e = p + strlen(p);
            }
            len = (int)(e - p);
            if (len > 0 && p[len - 1] == '\r') {
                len--;
            } else if (*e == '\n') {
                bare++;
            }
            if (len > longest) {
                longest = len;
            }
            p = (*e == '\0') ? e : e + 1;
        }
        CHECK(longest <= 79, "every line 79 columns or fewer");
        CHECK(bare == 0, "every line ends CR LF");
    }

    /* Windows: the same bytes at every split. */
    {
        static const ULONG rooms[] = { 64 + 1, 64 + 777, 64 + 4096, 64 + 61440 };
        ULONG w;
        for (w = 0; w < 4; w++) {
            ULONG at = 0;
            int same = 1;
            memcpy(img2, img, CM_QS_HDR_BYTES);
            while (at < total - CM_QS_HDR_BYTES) {
                static unsigned char win[64 + 61440];
                ULONG tot2;
                ULONG got = CmQsBuild(win, rooms[w], at, &in, t, n, &tot2);
                if (got <= CM_QS_HDR_BYTES || tot2 != total ||
                    tr_cms_get32(win + CM_QS_O_WIN_AT) != at) {
                    same = 0;
                    break;
                }
                memcpy(img2 + CM_QS_HDR_BYTES + at, win + CM_QS_HDR_BYTES,
                       got - CM_QS_HDR_BYTES);
                at += got - CM_QS_HDR_BYTES;
            }
            same = same && memcmp(img + CM_QS_HDR_BYTES, img2 + CM_QS_HDR_BYTES,
                                  total - CM_QS_HDR_BYTES) == 0;
            sprintf(what, "windows of %lu bytes give the whole image's bytes",
                    rooms[w]);
            CHECK(same, what);
        }
    }

    /* /nosysinfo: every hash zeroed, the flag set, the decode says so. */
    memcpy(img2, img, total);
    tr_cms_withhold(img2, total);
    CHECK(tr_cms_get32(img2 + CM_QS_O_FLAGS) & CM_QS_F_WITHHELD, "withheld: flagged");
    h.NoSysinfo = 1;
    tr_text_init(&tx, text, TEXT_MAX);
    tr_cms_decode(&tx, img2, total, &h);
    CHECK(strstr(text, "#") == NULL || strstr(text, "=#") == NULL,
          "withheld: no hash printed");
    CHECK(strstr(text, "identities  withheld") != NULL, "withheld: said");
    h.NoSysinfo = 0;

    /* A torn read, a header alone, a buffer too small, a cut. */
    memcpy(img2, img, total);
    CmQsSetAfter(img2, 1235);
    CHECK(tr_cms_get32(img2 + CM_QS_O_FLAGS) & CM_QS_F_TORN, "a moved detector: torn");
    CHECK_EQ(CmQsBuild(img2, 63, 0, &in, t, n, &bytes), 0, "63 bytes: nothing");
    CHECK_EQ(bytes, total, "63 bytes: the size still said");
    CHECK_EQ(CmQsBuild(img2, 64, 0, &in, t, n, &bytes), 64, "64 bytes: the header alone");
    CHECK_EQ(tr_cms_get32(img2 + CM_QS_O_BYTES), total, "the header names the whole size");
    CHECK_EQ(tr_cms_check(img, total - 4), TR_CMS_IMG_BAD, "an image cut short is refused");
    memcpy(img2, img, total);
    img2[CM_QS_O_VERSION] = 2;
    CHECK_EQ(tr_cms_check(img2, total), TR_CMS_IMG_VERSION, "a later version is not read");
    {
        CM_QS_TABLE big[2];
        static unsigned char zeros[300000];
        big[0] = t[3];
        big[0].Base = zeros; big[0].Records = 1;
        big[1] = t[3];
        big[1].Name = "big"; big[1].Base = zeros; big[1].Records = 5000;
        big[1].Stride = 4;
        (VOID)CmQsBuild(img2, 64, 0, &in, big, 2, &bytes);
        CHECK(bytes <= CM_QS_MAX_BYTES, "a cut image stays inside the bound");
        CHECK(tr_cms_get32(img2 + CM_QS_O_FLAGS) & CM_QS_F_CUT, "a cut is flagged");
    }
    /* The hash: the same under one salt, another under another. */
    {
        unsigned char id[16];
        memset(id, 0x5A, sizeof(id));
        CHECK_EQ(CmQsHash(1, id, 16), CmQsHash(1, id, 16), "a hash is stable");
        CHECK(CmQsHash(1, id, 16) != CmQsHash(2, id, 16) ||
              CmQsHash(3, id, 16) != CmQsHash(4, id, 16), "another salt, another hash");
        CHECK(CmQsHash(7, id, 16) <= 0xFFFFUL, "16 bits");
    }
}

/* A canned image, not the builder's: one table "t", fields A (a word), B
 * (two bytes, two elements) and H (a hash), two records. */
static void test_fixture(void)
{
    static const ULONG words[] = {
        /* header */
        CM_QS_MAGIC, 1, 64, 0 /* bytes, set below */, CM_QS_F_ICM, 64, 1,
        5, 5, 0, 0, 0x03050000UL, 2, 100, 0, 0,
        /* table: name at 112, fields at 88, 3 fields, records at 124 */
        112, 88, 3, 124, 2, 4,
        /* fields */
        114, 4UL | (1UL << 16), 116, 1UL | (2UL << 16),
        118, 0UL | (2UL << 8) | (1UL << 16),
        /* names "t\0A\0B\0H\0" at 112, padding to 124 */
        0x00410074UL, 0x00480042UL, 0,
        /* records */
        0x1234, 3, 0, 0xBEEF,
        0, 0, 0, 0
    };
    static const char *want =
        "[t] 2 records, 3 fields\r\n"
        "  #0  A=0x1234  B[0]=3  H=#beef\r\n"
        "  record 1: all zero\r\n"
        "end of state snapshot\r\n";
    unsigned char b[sizeof(words)];
    TR_CMS_HEAD h;
    TR_TEXT tx;
    ULONG i;

    for (i = 0; i < sizeof(words) / 4; i++) {
        ULONG v = words[i];
        if (i == 3) {
            v = (ULONG)sizeof(words);
        }
        b[i * 4] = (unsigned char)(v & 0xFF);
        b[i * 4 + 1] = (unsigned char)((v >> 8) & 0xFF);
        b[i * 4 + 2] = (unsigned char)((v >> 16) & 0xFF);
        b[i * 4 + 3] = (unsigned char)((v >> 24) & 0xFF);
    }
    CHECK_EQ(tr_cms_check(b, sizeof(b)), TR_CMS_IMG_OK, "the fixture is an image");
    memset(&h, 0, sizeof(h));
    h.Tool = "t";
    h.When = "w";
    h.Bdf = 0xFFFFFFFFUL;
    h.Tries = 2;
    h.Agreed = 1;
    tr_text_init(&tx, text, TEXT_MAX);
    tr_cms_decode(&tx, b, sizeof(b), &h);
    CHECK(strstr(text, want) != NULL, "the fixture decodes as written");
    CHECK(strstr(text, "the firmware connection manager's client") != NULL,
          "the fixture's flags");
    CHECK(strstr(text, "2 tries") != NULL, "the tries said");
    /* A hostile image: records over the metadata are refused, and
     * /nosysinfo's withholding then writes nothing. */
    {
        unsigned char bad[sizeof(words)];
        unsigned char keep[sizeof(words)];
        ULONG k;

        memcpy(bad, b, sizeof(b));
        /* the records at the field directory (88) */
        bad[64 + CM_QS_T_RECORDS_AT] = 88;
        bad[64 + CM_QS_T_RECORDS_AT + 1] = 0;
        CHECK_EQ(tr_cms_check(bad, sizeof(bad)), TR_CMS_IMG_BAD,
                 "records over the field directory are refused");
        memcpy(keep, bad, sizeof(bad));
        tr_cms_withhold(bad, sizeof(bad));
        CHECK(memcmp(keep, bad, sizeof(bad)) == 0,
              "a refused image is not written by the withholding");
        memcpy(bad, b, sizeof(b));
        /* the table directory over the header */
        bad[CM_QS_O_TABLES_AT] = 32;
        CHECK_EQ(tr_cms_check(bad, sizeof(bad)), TR_CMS_IMG_BAD,
                 "a directory in the header is refused");
        memcpy(bad, b, sizeof(b));
        /* a name with no terminator before the end */
        for (k = 112; k < 124; k++) {
            bad[k] = 'x';
        }
        CHECK_EQ(tr_cms_check(bad, sizeof(bad)), TR_CMS_IMG_BAD,
                 "a name not ended inside its bound is refused");
        memcpy(bad, b, sizeof(b));
        /* B (two one-byte elements; its info word at 100) marked as a hash */
        bad[100 + 1] = (unsigned char)CM_QS_FD_HASH;
        CHECK_EQ(tr_cms_check(bad, sizeof(bad)), TR_CMS_IMG_BAD,
                 "a hash flag on a field of two elements is refused");
        memcpy(bad, b, sizeof(b));
        /* H (a hash; its info word at 108) with its flag cleared: width 0 alone */
        bad[108 + 1] = 0;
        CHECK_EQ(tr_cms_check(bad, sizeof(bad)), TR_CMS_IMG_BAD,
                 "a width of 0 with no hash flag is refused");
        CHECK_EQ(tr_cms_check(b, 63), TR_CMS_IMG_BAD,
                 "fewer bytes than a header are refused");
        tr_cms_withhold(b, 63);
    }
    if (strstr(text, want) == NULL) {
        printf("%s", text);
    }
}

/* The all-controllers run's name check counts the connection managers'
   files (record 25 section 23 item 11). */
static const char *takenName;

static int taken_one(const char *name, void *context)
{
    (void)context;
    return strcmp(name, takenName) == 0;
}

static void test_pick_base(void)
{
    char out[16];

    takenName = "SNAPX.CB3";
    CHECK(tr_snap_pick_base("SNAPX", 11, 13, 12, 0, taken_one, NULL, out) ==
          TR_PICK_TAKEN, "a given name whose .CB3 is there is taken");
    takenName = "SNAPX.CTF.TMP";
    CHECK(tr_snap_pick_base("SNAPX", 11, 13, 12, 0, taken_one, NULL, out) ==
          TR_PICK_TAKEN, "a given name whose .CTF.TMP is there is taken");
    takenName = "B1312A0A.CT0";
    CHECK(tr_snap_pick_base(NULL, 11, 13, 12, 0, taken_one, NULL, out) ==
          TR_PICK_OK, "the clock's name");
    takenName = out;
    {
        char first[16];
        char probe[24];
        strcpy(first, out);
        sprintf(probe, "%s.CT0", first);
        takenName = probe;
        CHECK(tr_snap_pick_base(NULL, 11, 13, 12, 0, taken_one, NULL, out) ==
              TR_PICK_OK && strcmp(out, first) != 0,
              "the clock's name moves past one whose .CT0 is there");
    }
}

/* Save Snapshot's line (record 25 section 23 item 7). */
static void test_saved_line(void)
{
    char out[200];
    char sfx[3];
    int raw[3];

    tr_cms_saved_line(out, sizeof(out), "B1121405", sfx, raw, 0);
    CHECK(out[0] == '\0', "no image: no line");
    sfx[0] = '0';
    raw[0] = 1;
    tr_cms_saved_line(out, sizeof(out), "B1121405", sfx, raw, 1);
    CHECK(strcmp(out, "The Thunderbolt / USB4 state is in B1121405.CT0 and "
                      ".CB0.") == 0, "one image: the owner's line");
    sfx[1] = 'A';
    raw[1] = 1;
    tr_cms_saved_line(out, sizeof(out), "B1121405", sfx, raw, 2);
    CHECK(strcmp(out, "The Thunderbolt / USB4 state is in B1121405.CT0 and "
                      ".CB0, .CTA and .CBA.") == 0, "two images");
    raw[1] = 0;
    tr_cms_saved_line(out, sizeof(out), "B1121405", sfx, raw, 2);
    CHECK(strcmp(out, "The Thunderbolt / USB4 state is in B1121405.CT0 and "
                      ".CB0, .CTA.") == 0, "an image whose .CB was not kept");
    {
        static const char outText[] =
            "--- files written in the current directory\r\n"
            "  B1121405.DEV\r\n  B1121405.TXT\r\n  B1121405.CB0\r\n"
            "  B1121405.CT0\r\n  B1121405.CT2\r\n  X1121405.CT3\r\n"
            "  B1121405.CT45\r\n    XHCICM5: state saved in B1121405.CT5\r\n"
            "Attach: B1121405.DEV B1121405.TXT B1121405.CT0\r\n";
        char s16[16];
        int r16[16];
        char all[16];
        int allRaw[16];
        char line[TR_CMS_SAVED_LINE_MAX];
        int n;
        int i;

        n = tr_cms_saved_files(outText, "B1121405", s16, r16, 16);
        CHECK(n == 2 && s16[0] == '0' && r16[0] == 1 && s16[1] == '2' &&
              r16[1] == 0, "the run's own list: CT0 with CB0, CT2 alone");
        CHECK(tr_cms_saved_files("no list\r\n", "B1121405", s16, r16, 16) == 0,
              "no list: no image");
        for (i = 0; i < 16; i++) {
            all[i] = tr_cms_suffix((TR_U32)i);
            allRaw[i] = 1;
        }
        tr_cms_saved_line(line, sizeof(line), "B1121405", all, allRaw, 16);
        CHECK(strstr(line, ", .CTF and .CBF.") != NULL &&
              line[strlen(line) - 1] == '.',
              "sixteen images: the whole list fits");
    }
}

int main(void)
{
    static const DIR_UNDER_TEST dirs[] = {
        { "TBT_U4CM", CmQsF_TBT_U4CM, 0, sizeof(TBT_U4CM) },
        { "TBT_U4_PORT", CmQsF_TBT_U4_PORT, 0, sizeof(TBT_U4_PORT) },
        { "TBT_U4_LINK", CmQsF_TBT_U4_LINK, 0, sizeof(TBT_U4_LINK) },
        { "TBT_U4_KID", CmQsF_TBT_U4_KID, 0, sizeof(TBT_U4_KID) },
        { "TBT_U4_OBLIGATION", CmQsF_TBT_U4_OBLIGATION, 0, sizeof(TBT_U4_OBLIGATION) },
        { "TBT_U4_COUNTERS", CmQsF_TBT_U4_COUNTERS, 0, sizeof(TBT_U4_COUNTERS) },
        { "TBT_U4_XD", CmQsF_TBT_U4_XD, 0, sizeof(TBT_U4_XD) },
        { "TBT_ICM", CmQsF_TBT_ICM, 0, sizeof(TBT_ICM) },
        { "TBT_ICM_DEVICE", CmQsF_TBT_ICM_DEVICE, 0, sizeof(TBT_ICM_DEVICE) },
        { "TBT_ICM_COUNTERS", CmQsF_TBT_ICM_COUNTERS, 0, sizeof(TBT_ICM_COUNTERS) },
        { "TBT_RING", CmQsF_TBT_RING, 0, sizeof(TBT_RING) },
        { "XHCI_CHAIN_BIND", CmQsF_XHCI_CHAIN_BIND, 0, sizeof(XHCI_CHAIN_BIND) },
    };
    ULONG counts[12];
    ULONG i;

    counts[0] = CmQsN_TBT_U4CM;
    counts[1] = CmQsN_TBT_U4_PORT;
    counts[2] = CmQsN_TBT_U4_LINK;
    counts[3] = CmQsN_TBT_U4_KID;
    counts[4] = CmQsN_TBT_U4_OBLIGATION;
    counts[5] = CmQsN_TBT_U4_COUNTERS;
    counts[6] = CmQsN_TBT_U4_XD;
    counts[7] = CmQsN_TBT_ICM;
    counts[8] = CmQsN_TBT_ICM_DEVICE;
    counts[9] = CmQsN_TBT_ICM_COUNTERS;
    counts[10] = CmQsN_TBT_RING;
    counts[11] = CmQsN_XHCI_CHAIN_BIND;

    img = (unsigned char *)malloc(IMG_MAX);
    img2 = (unsigned char *)malloc(IMG_MAX);
    text = (char *)malloc(TEXT_MAX);
    if (img == NULL || img2 == NULL || text == NULL) {
        printf("FAIL: no memory\n");
        return 1;
    }
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        DIR_UNDER_TEST d = dirs[i];
        d.Count = counts[i];
        check_directory(&d);
    }
    test_roundtrip();
    test_fixture();
    test_saved_line();
    test_pick_base();
    printf("test_cm_snap: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
