/*
 * tr_acpi.c - the pure half of File > Save Firmware Tables... and XHCITR
 * /acpi (tr_acpi.h; design record 25 section 21, revisions 26 to 28;
 * tasks 46.21 and 47.34). Written for this project.
 */

#include <stdio.h>
#include <string.h>
#include "tr_acpi.h"
#include "../src/cm/cm_query.h"
#include "../src/xhci_acpi.h"

static const char *acpi_from(int from);
static void acpi_root_entries(const TR_ACPI *a, TR_TEXT *t);

static TR_U32 qa_get(const unsigned char *b, TR_U32 o)
{
    return (TR_U32)b[o] | ((TR_U32)b[o + 1] << 8) | ((TR_U32)b[o + 2] << 16) |
           ((TR_U32)b[o + 3] << 24);
}

/* ---- xhcicm.sys's answer -------------------------------------------------- */

int tr_qa_header(const unsigned char *b, TR_U32 got, TR_QAHDR *h)
{
    TR_U32 shape;

    memset(h, 0, sizeof(*h));
    if (b == NULL || got < CM_QA_HDR_BYTES ||
        qa_get(b, CM_QA_O_MAGIC) != CM_QA_MAGIC ||
        qa_get(b, CM_QA_O_VERSION) != CM_QA_VERSION) {
        return 0;
    }
    h->Bytes = qa_get(b, CM_QA_O_BYTES);
    h->Flags = qa_get(b, CM_QA_O_FLAGS);
    h->EpochLo = qa_get(b, CM_QA_O_EPOCH_LO);
    h->EpochHi = qa_get(b, CM_QA_O_EPOCH_HI);
    h->State = qa_get(b, CM_QA_O_STATE);
    h->Outcome = qa_get(b, CM_QA_O_OUTCOME);
    h->Tier = qa_get(b, CM_QA_O_TIER);
    h->BodyBytes = qa_get(b, CM_QA_O_BODY_BYTES);
    shape = qa_get(b, CM_QA_O_ENT_SHAPE);
    h->Count = shape & 0xFFFFUL;
    h->EntBytes = shape >> 16;
    h->PartAt = qa_get(b, CM_QA_O_PART_AT);
    h->PartBytes = qa_get(b, CM_QA_O_PART_BYTES);
    if (h->Bytes != got || h->PartBytes != got - CM_QA_HDR_BYTES ||
        h->PartBytes > CM_QA_PART_MAX || h->BodyBytes > CM_QA_MAX_BODY ||
        h->PartAt > h->BodyBytes || h->PartBytes > h->BodyBytes - h->PartAt ||
        h->EntBytes != CM_QA_ENT_BYTES || h->Count > CM_QA_MAX_ENTRIES ||
        h->Count * CM_QA_ENT_BYTES > h->BodyBytes) {
        return 0;
    }
    return 1;
}

int tr_qa_same(const TR_QAHDR *first, const TR_QAHDR *h, TR_U32 want)
{
    return h->SetGen == first->SetGen &&
           h->EpochLo == first->EpochLo && h->EpochHi == first->EpochHi &&
           h->BodyBytes == first->BodyBytes && h->Count == first->Count &&
           h->EntBytes == first->EntBytes && h->PartAt == want &&
           (h->Flags & CM_QA_F_PAST) == 0;
}

int tr_qa_body(const unsigned char *body, TR_U32 bytes, TR_U32 count,
               TR_ACPI *a)
{
    TR_U32 i;
    TR_U32 kind;
    TR_U32 held;
    TR_U32 off;
    TR_U32 how;
    TR_ACPI_T *t;

    a->Count = 0;
    if (count > CM_QA_MAX_ENTRIES || count * CM_QA_ENT_BYTES > bytes) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *e = body + i * CM_QA_ENT_BYTES;
        kind = qa_get(e, CM_QA_E_KIND);
        held = qa_get(e, CM_QA_E_HELD);
        off = qa_get(e, CM_QA_E_OFFSET);
        how = (kind >> 8) & 0xFFUL;
        if ((kind & 0xFFUL) == 0) {
            if (held != 0) {
                goto bad;
            }
            continue;
        }
        if ((kind & 0xFFUL) > CM_QA_K_ENTRY || how > CM_QA_H_HEADER ||
            (how == CM_QA_H_NONE) != (held == 0)) {
            goto bad;
        }
        if (held != 0 && (off < count * CM_QA_ENT_BYTES || off > bytes ||
                          held > bytes - off)) {
            goto bad;
        }
        if ((kind & 0xFFUL) == CM_QA_K_RSDP) {
            /* 20 bytes before revision 2, else its own length field's
             * 36 to 255; the entry's length the same. */
            if (how == CM_QA_H_HEADER ||
                (held != 0 &&
                 (held < 20 ||
                  (body[off + 15] < 2 && held != 20) ||
                  (body[off + 15] >= 2 &&
                   (held < 36 || held > 255 ||
                    qa_get(body + off, 20) != held)) ||
                  qa_get(e, CM_QA_E_LENGTH) != held))) {
                goto bad;
            }
        } else if (how == CM_QA_H_HEADER) {
            if (held != CM_QA_TBL_HDR) {
                goto bad;
            }
        } else if (how == CM_QA_H_WHOLE) {
            if (held < CM_QA_TBL_HDR || qa_get(body + off, 4) != held) {
                goto bad;
            }
        }
        t = &a->T[a->Count++];
        memset(t, 0, sizeof(*t));
        t->Sig[0] = (unsigned char)(qa_get(e, CM_QA_E_SIG) & 0xFFUL);
        t->Sig[1] = (unsigned char)((qa_get(e, CM_QA_E_SIG) >> 8) & 0xFFUL);
        t->Sig[2] = (unsigned char)((qa_get(e, CM_QA_E_SIG) >> 16) & 0xFFUL);
        t->Sig[3] = (unsigned char)((qa_get(e, CM_QA_E_SIG) >> 24) & 0xFFUL);
        t->Rsdp = (kind & 0xFFUL) == CM_QA_K_RSDP;
        t->Held = how == CM_QA_H_WHOLE    ? TR_ACPI_HELD_WHOLE
                : how == CM_QA_H_HEADER   ? TR_ACPI_HELD_HEADER
                                          : TR_ACPI_HELD_NONE;
        t->Data = held != 0 ? body + off : NULL;
        t->Bytes = held;
        t->Length = qa_get(e, CM_QA_E_LENGTH);
        t->Listed = 1;
        t->Kind = (int)(kind & 0xFFUL);
        t->Index = kind >> 16;
        t->From = TR_ACPI_SRC_CM;
    }
    return 1;
bad:
    a->Count = 0;
    return 0;
}

/* ---- the tables ------------------------------------------------------------- */

int tr_acpi_add(TR_ACPI *a, const unsigned char *data, TR_U32 bytes,
                int listed)
{
    TR_ACPI_T *t;

    if (a->Count >= TR_ACPI_MAX || data == NULL || bytes < 4) {
        return 0;
    }
    t = &a->T[a->Count++];
    memset(t, 0, sizeof(*t));
    memcpy(t->Sig, data, 4);
    t->Held = TR_ACPI_HELD_WHOLE;
    t->Data = data;
    t->Bytes = bytes;
    t->Length = bytes >= 8 ? qa_get(data, 4) : 0;
    t->Listed = listed;
    t->From = TR_ACPI_SRC_NT;
    return 1;
}

static int acpi_is_facs(const TR_ACPI_T *t)
{
    return memcmp(t->Sig, "FACS", 4) == 0;
}

static TR_U32 acpi_sum(const unsigned char *p, TR_U32 n)
{
    TR_U32 s = 0;
    TR_U32 i;

    for (i = 0; i < n; i++) {
        s += p[i];
    }
    return s & 0xFFUL;
}

int tr_acpi_sum(const TR_ACPI_T *t)
{
    if (t->Held != TR_ACPI_HELD_WHOLE || t->Data == NULL) {
        return TR_ACPI_SUM_NONE;
    }
    if (t->Rsdp) {
        if (t->Bytes < 20 || acpi_sum(t->Data, 20) != 0) {
            return TR_ACPI_SUM_BAD;
        }
        if (t->Data[15] < 2) {
            return t->Bytes == 20 ? TR_ACPI_SUM_OK : TR_ACPI_SUM_BAD;
        }
        if (t->Bytes < 36 || qa_get(t->Data, 20) != t->Bytes ||
            acpi_sum(t->Data, t->Bytes) != 0) {
            return TR_ACPI_SUM_BAD;
        }
        return TR_ACPI_SUM_OK;
    }
    if (acpi_is_facs(t)) {
        return TR_ACPI_SUM_NONE;
    }
    if (t->Bytes < TR_ACPI_HDR || t->Bytes != t->Length ||
        acpi_sum(t->Data, t->Bytes) != 0) {
        return TR_ACPI_SUM_BAD;
    }
    return TR_ACPI_SUM_OK;
}

static char acpi_file_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return (char)c;
    }
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return '_';
}

/* MSDM and SLIC, the licence tables: the signature's four bytes. */
static int acpi_is_licence(const TR_ACPI_T *t)
{
    return !t->Rsdp && (memcmp(t->Sig, "MSDM", 4) == 0 ||
                        memcmp(t->Sig, "SLIC", 4) == 0);
}

/* A good table (record 25 section 21 item 10): whole, at least its header
 * (the FACS: 64 bytes), its length field the bytes held, its checksum zero
 * (the FACS has none). */
static int acpi_good(const TR_ACPI_T *t)
{
    return t->Held == TR_ACPI_HELD_WHOLE && !t->Rsdp && t->Data != NULL &&
           t->Bytes >= (memcmp(t->Sig, "FACS", 4) == 0 ? 64UL
                                                        : (TR_U32)TR_ACPI_HDR) &&
           t->Length == t->Bytes && tr_acpi_sum(t) != TR_ACPI_SUM_BAD;
}

int tr_acpi_licence(TR_ACPI *a)
{
    int i;
    int n = 0;

    for (i = 0; i < a->Count; i++) {
        TR_ACPI_T *t = &a->T[i];
        if (!acpi_is_licence(t)) {
            continue;
        }
        if (t->Data != NULL && t->Bytes >= TR_ACPI_HDR) {
            t->Held = TR_ACPI_HELD_HEADER;
            t->Bytes = TR_ACPI_HDR;
        } else {
            t->Held = TR_ACPI_HELD_NONE;
            t->Data = NULL;
            t->Bytes = 0;
        }
        t->Reason = TR_ACPI_R_LICENCE;
        t->SameAsCm = 0;
        t->File[0] = '\0';
        n++;
    }
    return n;
}

int tr_acpi_names(TR_ACPI *a)
{
    char stem[5];
    int cm;
    int i;
    int j;
    int k;
    int n = 0;

    (void)tr_acpi_licence(a);
    for (i = 0; i < a->Count; i++) {
        TR_ACPI_T *t = &a->T[i];
        t->File[0] = '\0';
        if (t->Held != TR_ACPI_HELD_WHOLE) {
            continue;
        }
        if (t->Rsdp) {
            tr_copy(stem, "RSDP", sizeof(stem));
        } else {
            for (k = 0; k < 4; k++) {
                stem[k] = acpi_file_char(t->Sig[k]);
            }
            stem[4] = '\0';
        }
        /* xhcicm.sys's copies beside xhci98.sys's tables: <SIG><n>C. */
        cm = a->Source == TR_ACPI_SRC_XHA && t->From == TR_ACPI_SRC_CM;
        k = 1;
        for (j = 0; j < i; j++) {
            if (a->T[j].File[0] != '\0' &&
                strncmp(a->T[j].File, stem, 4) == 0 &&
                (a->T[j].From == TR_ACPI_SRC_CM &&
                 a->Source == TR_ACPI_SRC_XHA) == cm) {
                k++;
            }
        }
        tr_sprintf(t->File, sizeof(t->File), cm ? "%s%dC.DAT" : "%s%d.DAT",
                   stem, k);
        n++;
    }
    return n;
}

void tr_acpi_sig(const TR_ACPI_T *t, char *out)
{
    int k;

    if (t->Rsdp) {
        tr_copy(out, "RSDP", 5);
        return;
    }
    for (k = 0; k < 4; k++) {
        out[k] = (t->Sig[k] >= 0x20 && t->Sig[k] <= 0x7E) ? (char)t->Sig[k]
                                                          : '.';
    }
    out[4] = '\0';
}

void tr_acpi_list(const TR_ACPI *a, char *out, size_t cap)
{
    TR_TEXT t;
    char sig[5];
    char other[5];
    int i;
    int j;
    int n;
    int first = 1;

    tr_text_init(&t, out, cap);
    for (i = 0; i < a->Count; i++) {
        if (a->T[i].Held != TR_ACPI_HELD_WHOLE) {
            continue;
        }
        tr_acpi_sig(&a->T[i], sig);
        n = 0;
        for (j = 0; j < a->Count; j++) {
            if (a->T[j].Held != TR_ACPI_HELD_WHOLE) {
                continue;
            }
            tr_acpi_sig(&a->T[j], other);
            if (strcmp(sig, other) == 0) {
                if (j < i) {
                    break;
                }
                n++;
            }
        }
        if (j < a->Count && n == 0) {
            continue;       /* listed at its first */
        }
        tr_printf(&t, first ? "%s" : ", %s", sig);
        if (n > 1) {
            tr_printf(&t, " (%d)", n);
        }
        first = 0;
    }
}

/* An id's n characters: printable ASCII, else '.', the trailing NULs and
 * spaces (the field's padding) left out; "-" for none. */
static void acpi_id(const unsigned char *p, int n, char *out)
{
    int k;

    while (n > 0 && (p[n - 1] == 0 || p[n - 1] == ' ')) {
        n--;
    }
    for (k = 0; k < n; k++) {
        out[k] = (p[k] >= 0x20 && p[k] <= 0x7E) ? (char)p[k] : '.';
    }
    out[k] = '\0';
    if (k == 0) {
        tr_copy(out, "-", 2);
    }
}

void tr_acpi_index(const TR_ACPI *a, TR_TEXT *t)
{
    char sig[5];
    char oem[8];
    char tbl[10];
    char rev[8];
    const char *sum;
    int i;
    int headers = 0;
    int licence = 0;

    tr_put(t, "This computer's firmware (ACPI) tables, saved by the xHCI98 "
              "USB Tree.\n\n");
    tr_put(t, "File        Sig   OEM ID  OEM table  Rev    Length     Saved"
              "  From     Sum\n");
    for (i = 0; i < a->Count; i++) {
        const TR_ACPI_T *x = &a->T[i];
        tr_acpi_sig(x, sig);
        tr_copy(oem, "-", sizeof(oem));
        tr_copy(tbl, "-", sizeof(tbl));
        tr_copy(rev, "-", sizeof(rev));
        if (x->Data != NULL && x->Rsdp && x->Bytes >= 20) {
            acpi_id(x->Data + 9, 6, oem);
            tr_sprintf(rev, sizeof(rev), "%u", (unsigned int)x->Data[15]);
        } else if (x->Data != NULL && !acpi_is_facs(x) &&
                   x->Bytes >= TR_ACPI_HDR) {
            acpi_id(x->Data + 10, 6, oem);
            acpi_id(x->Data + 16, 8, tbl);
            tr_sprintf(rev, sizeof(rev), "%u", (unsigned int)x->Data[8]);
        }
        switch (tr_acpi_sum(x)) {
        case TR_ACPI_SUM_OK:
            sum = "ok";
            break;
        case TR_ACPI_SUM_BAD:
            sum = "BAD";
            break;
        default:
            sum = "-";
            break;
        }
        if (x->Reason == TR_ACPI_R_LICENCE) {
            licence++;
        } else if (x->Held != TR_ACPI_HELD_WHOLE) {
            headers++;      /* the driver's own headers, not the tool's */
        }
        if (x->Addr != 0 && x->Held == TR_ACPI_HELD_NONE) {
            tr_copy(sig, "?", sizeof(sig));
        }
        tr_printf(t, "%-11s %-5s %-7s %-10s %3s %9lu %9lu  %-8s %s\n",
                  x->File[0] != '\0' ? x->File : "not saved", sig, oem, tbl,
                  rev, (unsigned long)x->Length,
                  (unsigned long)(x->Held == TR_ACPI_HELD_WHOLE ? x->Bytes
                                                                : 0),
                  acpi_from(x->From), sum);
        if (x->Addr != 0 && x->Held == TR_ACPI_HELD_NONE) {
            tr_printf(t, "            the RSDT's entry %lu, at %08lXh: %s\n",
                      (unsigned long)x->Index, (unsigned long)x->Addr,
                      tr_acpi_reason(x->Reason));
        } else if (x->Reason != 0) {
            tr_printf(t, "            %s\n", tr_acpi_reason(x->Reason));
        }
        if (x->SameAsCm) {
            tr_put(t, "            the same as read by xhcicm.sys at its "
                      "start\n");
        }
    }
    tr_put(t, "\n");
    if (a->Source == TR_ACPI_SRC_NT) {
        tr_put(t, "Read through Windows (GetSystemFirmwareTable). Windows "
                  "names a table by its\nsignature alone, so of several "
                  "tables with one signature it gives the first.\n");
        for (i = 0; i < a->Count; i++) {
            if (a->T[i].Listed > 1 && a->T[i].Held == TR_ACPI_HELD_WHOLE) {
                tr_acpi_sig(&a->T[i], sig);
                tr_printf(t, "Windows listed %s %d times; the first was "
                             "saved.\n", sig, a->T[i].Listed);
            }
        }
        if (a->DsdtAsked) {
            tr_put(t, "DSDT was not in Windows' list and was asked for by "
                      "name.\n");
        }
        if (a->RootAsked) {
            tr_put(t, "The root table was not in Windows' list and was asked "
                      "for by name.\n");
        }
        for (i = 0; i < a->Count; i++) {
            int j;
            if (a->T[i].From != TR_ACPI_SRC_REG) {
                continue;
            }
            for (j = 0; j < i && !(a->T[j].From == TR_ACPI_SRC_REG &&
                                   memcmp(a->T[j].Sig, a->T[i].Sig, 4) == 0);
                 j++) {
            }
            if (j < i) {
                continue;   /* said at its first */
            }
            for (j = 0; j < a->Count && !(a->T[j].From == TR_ACPI_SRC_NT &&
                                          memcmp(a->T[j].Sig, a->T[i].Sig,
                                                 4) == 0);
                 j++) {
            }
            tr_acpi_sig(&a->T[i], sig);
            if (j < a->Count) {
                tr_printf(t, "Windows' %s did not check out; the copy it "
                             "keeps in the registry\n(HKLM\\HARDWARE\\ACPI) "
                             "was read too.\n", sig);
            } else {
                tr_printf(t, "Windows gave no %s; the copy it keeps in the "
                             "registry (HKLM\\HARDWARE\\ACPI)\nwas read.\n",
                          sig);
            }
        }
        acpi_root_entries(a, t);
    } else if (a->Source == TR_ACPI_SRC_REG) {
        tr_put(t, "Read from the copies Windows keeps in the registry "
                  "(HKLM\\HARDWARE\\ACPI).\nThis Windows keeps no copy of "
                  "the SSDTs or the other tables the RSDT lists.\n");
        acpi_root_entries(a, t);
    } else if (a->Source == TR_ACPI_SRC_XHA) {
        tr_put(t, "Read by xhci98.sys from memory for this save (Windows 98 "
                  "SE and ME give\nprograms no way to read them); \"xhcicm\" "
                  "is a copy xhcicm.sys read at its start.\n");
        tr_printf(t, "Its flags %lXh.\n", (unsigned long)a->Flags);
    } else {
        tr_put(t, "Read through xhcicm.sys: the tables its search for the "
                  "USB4 handover read at\nits start (Windows 98 SE and ME "
                  "give programs no way to read them).\n");
        tr_printf(t, "Its state %lu, outcome %lXh, tier %lu, flags %lXh.\n",
                  (unsigned long)a->State, (unsigned long)a->Outcome,
                  (unsigned long)a->Tier, (unsigned long)a->Flags);
        if (headers != 0 && licence != 0) {
            tr_put(t, "A table \"not saved\" other than MSDM and SLIC is one "
                      "it read the header\nof alone; it reads no DSDT.\n");
        } else if (headers != 0) {
            tr_put(t, "A table \"not saved\" is one it read the header of "
                      "alone; it reads no DSDT.\n");
        }
        tr_printf(t, "xhci98.sys did not read them: %s.\n",
                  tr_xha_why(a->XhaState));
    }
}

/* NT: the root table's entries by address, and how many of them Windows
 * gave (it names a table by its signature alone). */
static void acpi_root_entries(const TR_ACPI *a, TR_TEXT *t)
{
    const TR_ACPI_T *r = NULL;
    char sig[5];
    TR_U32 miss;
    TR_U32 n;
    TR_U32 i;
    TR_U32 w;
    int xs;
    int saved = 0;
    int k;

    for (k = 0; k < a->Count; k++) {
        const TR_ACPI_T *x = &a->T[k];
        if (memcmp(x->Sig, "XSDT", 4) == 0 || memcmp(x->Sig, "RSDT", 4) == 0) {
            if (r == NULL && x->Held == TR_ACPI_HELD_WHOLE &&
                x->Bytes >= TR_ACPI_HDR) {
                r = x;
            }
        } else if (x->Held == TR_ACPI_HELD_WHOLE &&
                   memcmp(x->Sig, "DSDT", 4) != 0 &&
                   memcmp(x->Sig, "FACS", 4) != 0) {
            /* Each signature once: the root names a table of a signature
             * once (the SSDTs aside), and the registry may keep two
             * revisions of one table. */
            int j;
            for (j = 0; j < k; j++) {
                const TR_ACPI_T *y = &a->T[j];
                if (y->Held == TR_ACPI_HELD_WHOLE &&
                    memcmp(y->Sig, x->Sig, 4) == 0) {
                    break;
                }
            }
            if (j == k) {
                saved++;
            }
        }
    }
    if (r == NULL) {
        return;
    }
    xs = memcmp(r->Sig, "XSDT", 4) == 0;
    w = xs ? 8UL : 4UL;
    n = (r->Bytes - TR_ACPI_HDR) / w;
    tr_acpi_sig(r, sig);
    if ((TR_U32)saved > n) {
        saved = (int)n;
    }
    miss = n - (TR_U32)saved;
    if (a->Source == TR_ACPI_SRC_REG) {
        tr_printf(t, "The %s lists %lu tables, at these addresses; the "
                     "registry keeps %d of them,\nso %lu %s not saved:\n",
                  sig, (unsigned long)n, saved, (unsigned long)miss,
                  miss == 1 ? "was" : "were");
    } else {
        tr_printf(t, "The %s lists %lu tables, at these addresses; %d of "
                     "them %s saved\n(Windows gives each name once), so "
                     "%lu %s not saved:\n",
                  sig, (unsigned long)n, saved, saved == 1 ? "was" : "were",
                  (unsigned long)miss, miss == 1 ? "was" : "were");
    }
    for (i = 0; i < n; i++) {
        TR_U32 lo = qa_get(r->Data, TR_ACPI_HDR + w * i);
        TR_U32 hi = xs ? qa_get(r->Data, TR_ACPI_HDR + w * i + 4UL) : 0;
        if (hi != 0) {
            tr_printf(t, "  %2lu  %08lX%08lXh\n", (unsigned long)i,
                      (unsigned long)hi, (unsigned long)lo);
        } else {
            tr_printf(t, "  %2lu  %08lXh\n", (unsigned long)i,
                      (unsigned long)lo);
        }
    }
}

/* Where a table came from, for the index. */
static const char *acpi_from(int from)
{
    switch (from) {
    case TR_ACPI_SRC_NT:
        return "Windows";
    case TR_ACPI_SRC_REG:
        return "registry";
    case TR_ACPI_SRC_XHA:
        return "xhci98";
    case TR_ACPI_SRC_CM:
        return "xhcicm";
    default:
        return "-";
    }
}

/* ---- xhci98.sys's answer (option B) ------------------------------------ */

int tr_xha_header(const unsigned char *b, TR_U32 got, TR_QAHDR *h)
{
    TR_U32 shape;

    memset(h, 0, sizeof(*h));
    if (b == NULL || got < XHA_HDR_BYTES ||
        qa_get(b, XHA_O_MAGIC) != XHA_MAGIC ||
        qa_get(b, XHA_O_VERSION) != XHA_VERSION) {
        return 0;
    }
    h->Bytes = qa_get(b, XHA_O_BYTES);
    h->Flags = qa_get(b, XHA_O_FLAGS);
    h->State = qa_get(b, XHA_O_STATE);
    h->Outcome = qa_get(b, XHA_O_OUTCOME);
    h->BodyBytes = qa_get(b, XHA_O_BODY_BYTES);
    shape = qa_get(b, XHA_O_ENT_SHAPE);
    h->Count = shape & 0xFFFFUL;
    h->EntBytes = shape >> 16;
    h->PartAt = qa_get(b, XHA_O_PART_AT);
    h->PartBytes = qa_get(b, XHA_O_PART_BYTES);
    h->SetGen = qa_get(b, XHA_O_SET_GEN);
    if (h->Bytes != got || h->PartBytes != got - XHA_HDR_BYTES ||
        h->PartBytes > XHA_PART_MAX || h->BodyBytes > XHA_MAX_BODY ||
        h->PartAt > h->BodyBytes || h->PartBytes > h->BodyBytes - h->PartAt ||
        h->EntBytes != XHA_ENT_BYTES || h->Count > XHA_MAX_ENTRIES ||
        h->Count * XHA_ENT_BYTES > h->BodyBytes) {
        return 0;
    }
    return 1;
}

int tr_xha_body(const unsigned char *body, TR_U32 bytes, TR_U32 count,
                TR_ACPI *a)
{
    TR_U32 i;
    TR_U32 kind;
    TR_U32 held;
    TR_U32 off;
    TR_U32 how;
    TR_ACPI_T *t;

    a->Count = 0;
    if (count > XHA_MAX_ENTRIES || count * XHA_ENT_BYTES > bytes) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *e = body + i * XHA_ENT_BYTES;
        kind = qa_get(e, XHA_E_KIND);
        held = qa_get(e, XHA_E_HELD);
        off = qa_get(e, XHA_E_OFFSET);
        how = (kind >> 8) & 0xFFUL;
        if ((kind & 0xFFUL) == 0 || (kind & 0xFFUL) > XHA_K_DSDT ||
            how > XHA_H_HEADER || (how == XHA_H_NONE) != (held == 0)) {
            goto bad;
        }
        if (held != 0 && (off < count * XHA_ENT_BYTES || off > bytes ||
                          held > bytes - off)) {
            goto bad;
        }
        if ((kind & 0xFFUL) == XHA_K_RSDP) {
            if (how == XHA_H_HEADER ||
                (held != 0 &&
                 (held < 20 ||
                  (body[off + 15] < 2 && held != 20) ||
                  (body[off + 15] >= 2 &&
                   (held < 36 || held > 255 ||
                    qa_get(body + off, 20) != held)) ||
                  qa_get(e, XHA_E_LENGTH) != held))) {
                goto bad;
            }
        } else if (how == XHA_H_HEADER) {
            if (held != XHA_TBL_HDR) {
                goto bad;
            }
        } else if (how == XHA_H_WHOLE) {
            if (held < XHA_TBL_HDR || qa_get(body + off, 4) != held ||
                qa_get(e, XHA_E_LENGTH) != held) {
                goto bad;
            }
        }
        t = &a->T[a->Count++];
        memset(t, 0, sizeof(*t));
        t->Sig[0] = (unsigned char)(qa_get(e, XHA_E_SIG) & 0xFFUL);
        t->Sig[1] = (unsigned char)((qa_get(e, XHA_E_SIG) >> 8) & 0xFFUL);
        t->Sig[2] = (unsigned char)((qa_get(e, XHA_E_SIG) >> 16) & 0xFFUL);
        t->Sig[3] = (unsigned char)((qa_get(e, XHA_E_SIG) >> 24) & 0xFFUL);
        t->Rsdp = (kind & 0xFFUL) == XHA_K_RSDP;
        t->Held = how == XHA_H_WHOLE    ? TR_ACPI_HELD_WHOLE
                : how == XHA_H_HEADER   ? TR_ACPI_HELD_HEADER
                                        : TR_ACPI_HELD_NONE;
        t->Data = held != 0 ? body + off : NULL;
        t->Bytes = held;
        t->Length = qa_get(e, XHA_E_LENGTH);
        t->Listed = 1;
        t->Kind = (int)(kind & 0xFFUL);
        t->Index = kind >> 16;
        t->Reason = qa_get(e, XHA_E_REASON);
        t->From = TR_ACPI_SRC_XHA;
    }
    return 1;
bad:
    a->Count = 0;
    return 0;
}

void tr_acpi_merge(TR_ACPI *a, const TR_ACPI *cm)
{
    int i;
    int j;
    int n;
    const TR_ACPI_T *c;

    n = a->Count;
    for (i = 0; i < cm->Count; i++) {
        c = &cm->T[i];
        if (c->Held != TR_ACPI_HELD_WHOLE) {
            /* A licence table's header xhci98.sys's set has no entry of
             * is kept, so it is listed (item 10); other headers are not. */
            if (!acpi_is_licence(c)) {
                continue;
            }
            for (j = 0; j < n && memcmp(a->T[j].Sig, c->Sig, 4) != 0; j++) {
            }
            if (j == n && a->Count < TR_ACPI_MAX) {
                a->T[a->Count] = *c;
                a->T[a->Count].From = TR_ACPI_SRC_CM;
                a->T[a->Count].File[0] = '\0';
                a->Count++;
            }
            continue;
        }
        for (j = 0; j < n; j++) {
            TR_ACPI_T *x = &a->T[j];
            if (x->Held == TR_ACPI_HELD_WHOLE && x->Kind == c->Kind &&
                x->Index == c->Index && memcmp(x->Sig, c->Sig, 4) == 0) {
                break;
            }
        }
        if (j < n && a->T[j].Bytes == c->Bytes &&
            memcmp(a->T[j].Data, c->Data, c->Bytes) == 0) {
            a->T[j].SameAsCm = 1;
            continue;
        }
        if (a->Count < TR_ACPI_MAX) {
            a->T[a->Count] = *c;
            a->T[a->Count].From = TR_ACPI_SRC_CM;
            a->T[a->Count].File[0] = '\0';
            a->Count++;
        }
    }
}

/* ---- 2000 and 32-bit XP: the registry's copies ------------------------- */

TR_U32 tr_acpi_reg_check(const char *key, TR_U32 type,
                         const unsigned char *data, TR_U32 bytes)
{
    const char *sig;
    TR_U32 need;

    sig = strcmp(key, "FADT") == 0 ? "FACP" : key;
    need = strcmp(key, "FACS") == 0 ? 64UL : (TR_U32)TR_ACPI_HDR;
    if (type != TR_REG_BINARY) {
        return TR_ACPI_R_TYPE;
    }
    if (data == NULL || bytes < 8 || memcmp(data, sig, 4) != 0) {
        return XHA_R_SIGNATURE;
    }
    if (bytes < need || bytes > TR_REG_TABLE_MAX || qa_get(data, 4) != bytes) {
        return XHA_R_LENGTH;
    }
    if (strcmp(key, "FACS") != 0 && acpi_sum(data, bytes) != 0) {
        return XHA_R_CHECKSUM;
    }
    return 0;
}

/* One value as a table of `key` into a: copied into the store when
 * whole. */
static void reg_take(TR_ACPI *a, const char *key, const unsigned char *buf,
                     TR_U32 bytes, TR_U32 type, unsigned char *store,
                     TR_U32 cap, TR_U32 *used)
{
    TR_ACPI_T *t;
    TR_U32 r;

    if (a->Count >= TR_ACPI_MAX) {
        return;
    }
    t = &a->T[a->Count++];
    memset(t, 0, sizeof(*t));
    memcpy(t->Sig, strcmp(key, "FADT") == 0 ? "FACP" : key, 4);
    t->From = TR_ACPI_SRC_REG;
    t->Listed = 1;
    t->Length = bytes >= 8 ? qa_get(buf, 4) : 0;
    r = tr_acpi_reg_check(key, type, buf, bytes);
    if (r == XHA_R_SIGNATURE) {
        r = TR_ACPI_R_REG_SIG;      /* no address was read */
    }
    if (r == 0 && bytes > cap - *used) {
        r = XHA_R_LIMIT;
    }
    if (r != 0) {
        t->Held = TR_ACPI_HELD_NONE;
        t->Reason = r;
        return;
    }
    memcpy(store + *used, buf, bytes);
    t->Data = store + *used;
    t->Bytes = bytes;
    t->Held = TR_ACPI_HELD_WHOLE;
    *used += bytes;
}

int tr_acpi_reg_walk(const TR_REGOPS *ops, TR_ACPI *a, unsigned char *store,
                     TR_U32 cap)
{
    static const char *const keys[3] = { "DSDT", "FADT", "RSDT" };
    static unsigned char buf[TR_REG_TABLE_MAX];
    char p1[128];
    char p2[192];
    char p3[256];
    char name[64];
    TR_U32 used = 0;
    TR_U32 bytes;
    TR_U32 type;
    TR_U32 i;
    int k;
    int x;
    int y;
    int z;
    int taken;
    int whole = 0;
    int rsdt = -1;

    a->Count = 0;
    a->Source = TR_ACPI_SRC_REG;
    for (k = 0; k < 3; k++) {
        taken = 0;
        for (x = 0; x < TR_REG_SUBKEYS && taken < 4; x++) {
            tr_sprintf(p1, sizeof(p1), "HARDWARE\\ACPI\\%s", keys[k]);
            if (!ops->Subkey(ops->Ctx, p1, x, name, sizeof(name))) {
                break;
            }
            tr_sprintf(p1, sizeof(p1), "HARDWARE\\ACPI\\%s\\%s", keys[k],
                       name);
            for (y = 0; y < TR_REG_SUBKEYS && taken < 4; y++) {
                if (!ops->Subkey(ops->Ctx, p1, y, name, sizeof(name))) {
                    break;
                }
                tr_sprintf(p2, sizeof(p2), "%s\\%s", p1, name);
                for (z = 0; z < TR_REG_SUBKEYS && taken < 4; z++) {
                    if (!ops->Subkey(ops->Ctx, p2, z, name, sizeof(name))) {
                        break;
                    }
                    tr_sprintf(p3, sizeof(p3), "%s\\%s", p2, name);
                    bytes = 0;
                    type = 0;
                    if (!ops->Value(ops->Ctx, p3, "00000000", buf, sizeof(buf),
                                    &bytes, &type)) {
                        continue;
                    }
                    reg_take(a, keys[k], buf, bytes, type, store, cap, &used);
                    if (a->T[a->Count - 1].Held == TR_ACPI_HELD_WHOLE) {
                        whole++;
                        if (k == 2 && rsdt < 0) {
                            rsdt = a->Count - 1;
                        }
                    }
                    taken++;
                }
            }
        }
    }
    bytes = 0;
    type = 0;
    if (ops->Value(ops->Ctx, "HARDWARE\\ACPI\\FACS", "00000000", buf,
                   sizeof(buf), &bytes, &type)) {
        reg_take(a, "FACS", buf, bytes, type, store, cap, &used);
        if (a->T[a->Count - 1].Held == TR_ACPI_HELD_WHOLE) {
            whole++;
        }
    }
    /* The RSDT's entries are listed by the index, by address
     * (acpi_root_entries): the registry gives no address with a table, so
     * none is matched to a file. */
    (void)rsdt;
    (void)i;
    return whole;
}

int tr_acpi_reg_needed(const TR_ACPI *a)
{
    int i;

    for (i = 0; i < a->Count; i++) {
        if (memcmp(a->T[i].Sig, "DSDT", 4) == 0 && acpi_good(&a->T[i])) {
            return 0;
        }
    }
    return 1;
}

int tr_acpi_reg_fill(TR_ACPI *a, const TR_ACPI *reg)
{
    static const char *const sigs[4] = { "DSDT", "FACP", "RSDT", "FACS" };
    int n0 = a->Count;
    int whole = 0;
    int i;
    int j;
    int k;

    for (i = 0; i < reg->Count && a->Count < TR_ACPI_MAX; i++) {
        const TR_ACPI_T *r = &reg->T[i];
        for (k = 0; k < 4 && memcmp(r->Sig, sigs[k], 4) != 0; k++) {
        }
        if (k == 4 || r->Rsdp) {
            continue;
        }
        for (j = 0; j < n0; j++) {
            if (memcmp(a->T[j].Sig, r->Sig, 4) == 0 && acpi_good(&a->T[j])) {
                break;
            }
        }
        if (j < n0) {
            continue;       /* the interface gave it good */
        }
        a->T[a->Count] = *r;
        a->T[a->Count].From = TR_ACPI_SRC_REG;
        a->T[a->Count].File[0] = '\0';
        if (r->Held == TR_ACPI_HELD_WHOLE) {
            whole++;
        }
        a->Count++;
    }
    /* The interface gave nothing: the set is the registry's alone, and
     * says so as on 2000 and 32-bit XP. */
    if (n0 == 0 && a->Count != 0) {
        a->Source = TR_ACPI_SRC_REG;
    }
    return whole;
}

const char *tr_acpi_reason(TR_U32 r)
{
    switch (r) {
    case XHA_R_SIGNATURE:
        return "no longer a table at its address";
    case XHA_R_LENGTH:
        return "its length is wrong";
    case XHA_R_CHECKSUM:
        return "its checksum is wrong";
    case XHA_R_BOUNDS:
        return "outside the memory it may be read from";
    case XHA_R_LIMIT:
        return "past the size limit";
    case XHA_R_NOMEM:
        return "no memory";
    case XHA_R_TIMEOUT:
        return "timeout";
    case XHA_R_READ:
        return "it could not be read";
    case TR_ACPI_R_NOT_KEPT:
        return "not kept by this Windows";
    case TR_ACPI_R_TYPE:
        return "not a binary value";
    case TR_ACPI_R_LICENCE:
        return "not saved: licence data";
    case TR_ACPI_R_REG_SIG:
        return "not the table its registry key names";
    default:
        return "";
    }
}

const char *tr_xha_why(long state)
{
    switch (state) {
    case 0:
        return "this xhci98.sys does not read them";
    case -2:
        return "no controller here is driven by xhci98.sys";
    case (long)XHA_S_NOT_OS:
        return "it does not read them on this system";
    case (long)XHA_S_OFF:
        return "XhciAcpiTables is 0";
    case (long)XHA_S_NO_RSDP:
        return "no ACPI tables were found in memory";
    case (long)XHA_S_NOMEM:
        return "it had no memory to read them";
    case (long)XHA_S_BUSY:
        return "it was reading them for another program";
    case (long)XHA_S_REFUSED:
        return "the tables in memory could not be read safely";
    default:
        return "its answer could not be read";
    }
}

/* ---- the message -------------------------------------------------------- */

const char *tr_qa_why(long state)
{
    switch (state) {
    case 0:
        return "this xhcicm.sys does not serve them";
    case (long)CM_QA_S_NOT_OS:
        return "it does not read them on this system";
    case (long)CM_QA_S_NOT_LOOKED:
        return "it reads them only for an ASM4242 USB4 host";
    case (long)CM_QA_S_OFF:
        return "XhciCmAsm4242Handover is 0";
    case (long)CM_QA_S_NO_HOST:
        return "the ASM4242 host's ports were not found";
    case (long)CM_QA_S_NO_RSDP:
        return "no ACPI tables were found in memory";
    case (long)CM_QA_S_NOMEM:
        return "it had no memory to keep them";
    default:
        return "the driver's answer could not be read";
    }
}

void tr_acpi_message(int kind, const TR_ACPI *a, const char *folder,
                     const char *name, const char *why, int n, int m,
                     char *out, size_t cap)
{
    TR_TEXT t;
    static char list[4096];
    char sig[5];
    int i;
    int dups;
    int headers = 0;

    tr_text_init(&t, out, cap);
    switch (kind) {
    case TR_ACPI_M_SAVED:
        tr_acpi_list(a, list, sizeof(list));
        if (list[0] == '\0') {
            /* Every table held is a header (the licence tables alone). */
            tr_printf(&t, "Only TABLES.TXT was saved in %s, listing this "
                          "computer's firmware (ACPI) tables.", folder);
        } else {
            tr_printf(&t, "This computer's firmware (ACPI) tables were saved "
                          "in %s: %s, and TABLES.TXT listing them.", folder,
                      list);
        }
        dups = 0;
        for (i = 0; i < a->Count; i++) {
            if (a->T[i].Held != TR_ACPI_HELD_WHOLE) {
                headers++;
            }
            if (a->Source == TR_ACPI_SRC_NT && a->T[i].Listed > 1 &&
                a->T[i].Held == TR_ACPI_HELD_WHOLE) {
                /* One sentence for them all, as long as the list. */
                tr_acpi_sig(&a->T[i], sig);
                tr_printf(&t, dups == 0 ? " Windows gives one table of each "
                                          "name: of %s (%d)"
                                        : ", %s (%d)",
                          sig, a->T[i].Listed);
                dups++;
            }
        }
        if (dups != 0) {
            tr_put(&t, ", the first was saved.");
        }
        /* The SSDTs the root lists that Windows did not give. */
        for (i = 0; i < a->Count; i++) {
            if (a->Source == TR_ACPI_SRC_NT && a->T[i].Listed > 1 &&
                memcmp(a->T[i].Sig, "SSDT", 4) == 0) {
                tr_printf(&t, a->T[i].Listed == 2
                                  ? " %d SSDT was not saved."
                                  : " %d SSDTs were not saved.",
                          a->T[i].Listed - 1);
            }
        }
        /* The interface's set: the signatures taken whole from the
         * registry (item 10), each once. */
        if (a->Source == TR_ACPI_SRC_NT) {
            int regs = 0;
            for (i = 0; i < a->Count; i++) {
                int j;
                if (a->T[i].From != TR_ACPI_SRC_REG ||
                    a->T[i].Held != TR_ACPI_HELD_WHOLE) {
                    continue;
                }
                for (j = 0; j < i; j++) {
                    if (a->T[j].From == TR_ACPI_SRC_REG &&
                        a->T[j].Held == TR_ACPI_HELD_WHOLE &&
                        memcmp(a->T[j].Sig, a->T[i].Sig, 4) == 0) {
                        break;
                    }
                }
                if (j < i) {
                    continue;
                }
                tr_acpi_sig(&a->T[i], sig);
                tr_printf(&t, regs == 0 ? " %s" : ", %s", sig);
                regs++;
            }
            if (regs != 0) {
                tr_put(&t, regs == 1 ? " came from the copy Windows keeps in "
                                       "the registry."
                                     : " came from the copies Windows keeps "
                                       "in the registry.");
            }
        }
        if (a->Source == TR_ACPI_SRC_XHA) {
            tr_put(&t, " Windows 98 SE / ME give no access to these tables; "
                       "xhci98.sys read them from memory for this save.");
        } else if (a->Source == TR_ACPI_SRC_REG) {
            tr_put(&t, " They come from the copies Windows keeps in the "
                       "registry; the SSDTs cannot be read on this Windows.");
        } else if (a->Source == TR_ACPI_SRC_CM) {
            tr_put(&t, " Windows 98 SE / ME give no access to these tables; "
                       "these are the ones xhcicm.sys read for the USB4 "
                       "handover.");
            tr_printf(&t, " xhci98.sys did not read the DSDT and the other "
                          "SSDTs: %s.", tr_xha_why(a->XhaState));
        }
        if (headers != 0) {
            tr_printf(&t, headers == 1
                              ? " %d more is listed in TABLES.TXT, not saved."
                              : " %d more are listed in TABLES.TXT, not saved.",
                      headers);
        }
        break;
    case TR_ACPI_M_NO_API:
        tr_put(&t, "No tables were saved: this Windows gives programs no "
                   "way to read the firmware (ACPI) tables. XP x64, Vista "
                   "and later do.");
        break;
    case TR_ACPI_M_NO_CM:
        tr_put(&t, "No tables were saved: Windows 98 SE / ME give no access "
                   "to them, and xhcicm.sys, which reads them for an "
                   "ASM4242 USB4 host, is not running here.");
        break;
    case TR_ACPI_M_CM_NONE:
        tr_printf(&t, "No tables were saved: xhcicm.sys did not read them at "
                      "this start: %s.",
                  why != NULL ? why : tr_qa_why((long)a->State));
        break;
    case TR_ACPI_M_NT_NONE:
        tr_put(&t, "No tables were saved: Windows gave none.");
        break;
    case TR_ACPI_M_NO_98:
        tr_put(&t, "No tables were saved: Windows 98 SE / ME give no access "
                   "to them, and neither xhci98.sys nor xhcicm.sys, which "
                   "can read them, is running here.");
        break;
    case TR_ACPI_M_XHA_NONE:
        tr_printf(&t, "No tables were saved: xhci98.sys did not read them: "
                      "%s.", tr_xha_why(a->XhaState));
        break;
    case TR_ACPI_M_EXISTS:
        tr_printf(&t, "Nothing was saved: %s already has %s. Pick an empty "
                      "folder.", folder, name);
        break;
    default:
        tr_printf(&t, "Saving stopped at %s in %s: %d of %d files were "
                      "written.", name, folder, n, m);
        break;
    }
}
