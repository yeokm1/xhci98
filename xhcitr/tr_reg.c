/*
 * tr_reg.c - XHCITR's listing of a USB device's driver keys, the pure half
 * (design record 25 section 25; roadmap-usb4.md task 50.29). No Windows
 * call: the values come through the caller's RegEnumValueA (tr_regw.c in
 * the tool, a model in test\test_trreg.c), the devnodes through the
 * caller's walk. C89.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include "tr_reg.h"

static int up(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

static int is_letter(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_capital(int c)
{
    return c >= 'A' && c <= 'Z';
}

static int is_small(int c)
{
    return c >= 'a' && c <= 'z';
}

/* name holds word at i, case ignored. Stops at the name's NUL, so it never
 * reads past it. */
static int at_ci(const char *name, size_t i, const char *word)
{
    size_t k;

    for (k = 0; word[k] != '\0'; k++) {
        if (name[i + k] == '\0' || up((unsigned char)name[i + k]) != word[k]) {
            return 0;
        }
    }
    return 1;
}

/* A word of the name begins at i: a letter at the name's start, after a
 * character that is not a letter, or a capital after a small letter. */
static int word_start(const char *name, size_t i)
{
    int c = (unsigned char)name[i];
    int before;

    if (!is_letter(c)) {
        return 0;
    }
    if (i == 0) {
        return 1;
    }
    before = (unsigned char)name[i - 1];
    return !is_letter(before) || (is_small(before) && is_capital(c));
}

/* At j, an S or none; then nothing but characters that are not letters.
 * Stops at the name's NUL. */
static int tail_clear(const char *name, size_t j)
{
    if (up((unsigned char)name[j]) == 'S') {
        j++;
    }
    for (; name[j] != '\0'; j++) {
        if (is_letter((unsigned char)name[j])) {
            return 0;
        }
    }
    return 1;
}

/* At i, the term's letters (capitals, case ignored, side by side; a space
 * in the term stands for one or more characters that are not letters),
 * then an S or none, then the name ends - after characters that are not
 * letters, and after one of the words that name a secret's form (HASH,
 * HASHES, HEX, VALUE, MATERIAL, LIST, themselves with an S or none) or
 * none. A mismatch is found at the name's NUL at the latest, so no read
 * passes it. */
static int term_ends(const char *name, size_t i, const char *term)
{
    static const char *const forms[] = {
        "HASH", "HASHES", "HEX", "VALUE", "MATERIAL", "LIST", NULL
    };
    size_t j = i;
    size_t k;

    for (k = 0; term[k] != '\0'; k++) {
        if (term[k] == ' ') {
            if (name[j] == '\0' || is_letter((unsigned char)name[j])) {
                return 0;
            }
            while (name[j] != '\0' && !is_letter((unsigned char)name[j])) {
                j++;
            }
            continue;
        }
        if (up((unsigned char)name[j]) != term[k]) {
            return 0;
        }
        j++;
    }
    if (tail_clear(name, j)) {
        return 1;
    }
    if (up((unsigned char)name[j]) == 'S') {
        j++;
    }
    while (name[j] != '\0' && !is_letter((unsigned char)name[j])) {
        j++;
    }
    for (k = 0; forms[k] != NULL; k++) {
        if (at_ci(name, j, forms[k]) &&
            tail_clear(name, j + strlen(forms[k]))) {
            return 1;
        }
    }
    return 0;
}

/* The whole name is one of the harmless names ending in KEY, case
 * ignored, with an S or none. */
static int harmless_key(const char *name)
{
    static const char *const names[] = { "HOTKEY", "SHORTCUTKEY", NULL };
    int k;

    for (k = 0; names[k] != NULL; k++) {
        size_t n = strlen(names[k]);

        if (at_ci(name, 0, names[k]) &&
            (name[n] == '\0' ||
             (up((unsigned char)name[n]) == 'S' && name[n + 1] == '\0'))) {
            return 1;
        }
    }
    return 0;
}

int tr_reg_secret_name(const char *name)
{
    static const char *const terms[] = {
        "KEY", "PASSWORD", "PASSWD", "PASSPHRASE", "PWD", "SECRET",
        "CREDENTIAL", "PSK", "TOKEN", "LICENSE", "SSID", "PIN", "PINCODE",
        "PIN CODE", "WPAPSK", NULL
    };
    size_t i;
    size_t w = 0;           /* the start of the word i is in */
    int caps = 0;           /* that word has no small letter */
    int k;

    if (name == NULL || harmless_key(name)) {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++) {
        if (!is_letter((unsigned char)name[i])) {
            continue;
        }
        if (word_start(name, i)) {
            size_t e;

            w = i;
            caps = 1;
            for (e = i; is_letter((unsigned char)name[e]) &&
                        (e == i || !word_start(name, e)); e++) {
                if (is_small((unsigned char)name[e])) {
                    caps = 0;
                }
            }
        }
        /* A term counts at a word's start; inside a word of capitals alone
         * (USERPASSWORD, WEPKEY) anywhere. */
        if (i != w && !caps) {
            continue;
        }
        for (k = 0; terms[k] != NULL; k++) {
            if (term_ends(name, i, terms[k])) {
                return 1;
            }
        }
    }
    return 0;
}

const char *tr_reg_type_name(TR_U32 type)
{
    switch (type) {
    case TR_REG_NONE:               return "REG_NONE";
    case TR_REG_SZ:                 return "REG_SZ";
    case TR_REG_EXPAND_SZ:          return "REG_EXPAND_SZ";
    case TR_REG_BINARY:             return "REG_BINARY";
    case TR_REG_DWORD:              return "REG_DWORD";
    case TR_REG_DWORD_BIG_ENDIAN:   return "REG_DWORD_BIG_ENDIAN";
    case TR_REG_LINK:               return "REG_LINK";
    case TR_REG_MULTI_SZ:           return "REG_MULTI_SZ";
    case TR_REG_RESOURCE_LIST:      return "REG_RESOURCE_LIST";
    case TR_REG_FULL_RESOURCE:      return "REG_FULL_RESOURCE_DESCRIPTOR";
    case TR_REG_RESOURCE_REQ:       return "REG_RESOURCE_REQUIREMENTS_LIST";
    case TR_REG_QWORD:              return "REG_QWORD";
    default:                        return NULL;
    }
}

/* The type as the value's line shows it. */
static void put_type(TR_TEXT *t, TR_U32 type)
{
    const char *n = tr_reg_type_name(type);

    if (n != NULL) {
        tr_put(t, n);
    } else {
        tr_printf(t, "type 0x%08lX", (unsigned long)type);
    }
}

/* n characters of s, each outside printable ASCII as '?' and a quote
 * doubled, so the closing quote is the only single one; with idOnly (an
 * unscreened name) only the characters of an identifier. */
static void put_chars(TR_TEXT *t, const char *s, size_t n, int idOnly)
{
    char chunk[66];
    size_t k = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        int keep;

        if (idOnly) {
            keep = is_letter(c) || (c >= '0' && c <= '9') || c == '_' ||
                   c == '-' || c == '.' || c == '&' || c == '#' ||
                   c == '{' || c == '}' || c == '(' || c == ')' ||
                   c == '\\' || c == ' ';
        } else {
            keep = c >= 0x20 && c < 0x7F;
        }
        chunk[k++] = keep ? (char)c : '?';
        if (keep && c == '"') {
            chunk[k++] = '"';
        }
        if (k >= sizeof(chunk) - 2) {
            chunk[k] = '\0';
            tr_put(t, chunk);
            k = 0;
        }
    }
    chunk[k] = '\0';
    tr_put(t, chunk);
}

/* The screen's answer about data[0..bytes): first the bytes as stored (the
 * NULs left out, so a name kept as UTF-16 reads as one, control bytes as
 * blanks, every byte of the system's code page kept), then where a
 * converter is given the data read as UTF-16 at either alignment, in the
 * system's code page. Nothing is replaced before the screen has seen it:
 * the shown text's '?' is put_chars's, after the answer. */
static int screen_bytes(const unsigned char *data, TR_U32 bytes, char *proj,
                        const TR_REG_SCR *scr)
{
    TR_U32 i;
    TR_U32 n = 0;
    int align;
    int answer;

    if (scr == NULL || scr->Screen == NULL) {
        return TR_REG_SHOW;
    }
    if (proj == NULL) {
        return TR_REG_UNSCREENED;
    }
    for (i = 0; i < bytes; i++) {
        unsigned char c = data[i];
        if (c == 0) {
            continue;
        }
        proj[n++] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    proj[n] = '\0';
    answer = scr->Screen(proj, scr->Context);
    if (answer != TR_REG_SHOW || scr->Narrow == NULL) {
        return answer;
    }
    for (align = 0; align < 2; align++) {
        TR_U32 units = bytes > (TR_U32)align ? (bytes - (TR_U32)align) / 2 : 0;
        unsigned char *u = (unsigned char *)proj;
        char *out = proj + TR_REG_READ_BYTES;
        size_t len;

        if (units == 0) {
            continue;
        }
        for (i = 0; i < units; i++) {
            unsigned char lo = data[align + 2 * i];
            unsigned char hi = data[align + 2 * i + 1];
            if (hi == 0 && (lo < 0x20 || lo == 0x7F)) {
                lo = ' ';
            }
            u[2 * i] = lo;
            u[2 * i + 1] = hi;
        }
        len = scr->Narrow(u, units, out,
                          TR_REG_PROJ_BYTES - TR_REG_READ_BYTES,
                          scr->Context);
        if (len == 0) {
            continue;
        }
        out[len < TR_REG_PROJ_BYTES - TR_REG_READ_BYTES
                ? len
                : TR_REG_PROJ_BYTES - TR_REG_READ_BYTES - 1] = '\0';
        answer = scr->Screen(out, scr->Context);
        if (answer != TR_REG_SHOW) {
            return answer;
        }
    }
    return TR_REG_SHOW;
}

int tr_reg_screen_windows(const char *text, char *chunk, TR_REG_HAS has,
                          void *context)
{
    size_t n = strlen(text);
    size_t at = 0;

    for (;;) {
        size_t len = n - at > TR_REG_WINDOW ? TR_REG_WINDOW : n - at;

        memcpy(chunk, text + at, len);
        chunk[len] = '\0';
        if (has(chunk, context)) {
            return 1;
        }
        if (at + len >= n) {
            return 0;
        }
        at += TR_REG_WINDOW_STEP;
    }
}

/* The decimal of the 64-bit number hi:lo, from 16-bit limbs (no 64-bit
 * arithmetic, AGENTS.md's rule for the tree). */
static void put_dec64(TR_TEXT *t, TR_U32 hi, TR_U32 lo)
{
    TR_U32 limb[4];
    char digits[24];
    int n = 0;
    int k;
    int zero;

    limb[0] = (hi >> 16) & 0xFFFFUL;
    limb[1] = hi & 0xFFFFUL;
    limb[2] = (lo >> 16) & 0xFFFFUL;
    limb[3] = lo & 0xFFFFUL;
    do {
        TR_U32 rem = 0;
        zero = 1;
        for (k = 0; k < 4; k++) {
            TR_U32 cur = (rem << 16) | limb[k];
            limb[k] = cur / 10UL;
            rem = cur % 10UL;
            if (limb[k] != 0) {
                zero = 0;
            }
        }
        digits[n++] = (char)('0' + (int)rem);
    } while (!zero && n < 21);
    for (k = n - 1; k >= 0; k--) {
        char one[2];
        one[0] = digits[k];
        one[1] = '\0';
        tr_put(t, one);
    }
}

static TR_U32 le32(const unsigned char *b)
{
    return (TR_U32)b[0] | ((TR_U32)b[1] << 8) | ((TR_U32)b[2] << 16) |
           ((TR_U32)b[3] << 24);
}

/* ", N bytes: aa bb" for 16 or fewer, else ", N bytes:" (", N bytes, the
 * first 256 shown:") and offset lines under the value. note goes after
 * the count ("(not 4)"), or NULL. */
static void put_hex(TR_TEXT *t, const char *indent, const unsigned char *b,
                    TR_U32 bytes, const char *note)
{
    TR_U32 shown = bytes > TR_REG_HEX_BYTES ? TR_REG_HEX_BYTES : bytes;
    TR_U32 i;

    tr_printf(t, ", %lu byte%s%s%s", (unsigned long)bytes,
              bytes == 1 ? "" : "s", note != NULL ? " " : "",
              note != NULL ? note : "");
    if (bytes == 0) {
        tr_put(t, "\n");
        return;
    }
    if (bytes <= 16) {
        tr_put(t, ":");
        for (i = 0; i < bytes; i++) {
            tr_printf(t, " %02X", (unsigned int)b[i]);
        }
        tr_put(t, "\n");
        return;
    }
    if (shown < bytes) {
        tr_printf(t, ", the first %lu shown", (unsigned long)shown);
    }
    tr_put(t, ":\n");
    for (i = 0; i < shown; i += 16) {
        TR_U32 j;
        tr_printf(t, "%s    %04lX ", indent, (unsigned long)i);
        for (j = i; j < i + 16 && j < shown; j++) {
            tr_printf(t, " %02X", (unsigned int)b[j]);
        }
        tr_put(t, "\n");
    }
}

/* A string of n characters, quoted, cut at `max` with its length said. */
static void put_quoted(TR_TEXT *t, const char *s, size_t n, size_t max)
{
    tr_put(t, "\"");
    put_chars(t, s, n > max ? max : n, 0);
    if (n > max) {
        tr_printf(t, "...\" (the first %lu of %lu characters)",
                  (unsigned long)max, (unsigned long)n);
    } else {
        tr_put(t, "\"");
    }
}

/* The length of the string at s, within its n bytes. */
static size_t str_len(const char *s, size_t n)
{
    size_t k = 0;

    while (k < n && s[k] != '\0') {
        k++;
    }
    return k;
}

static void put_multi(TR_TEXT *t, const char *indent, const char *s,
                      TR_U32 bytes)
{
    size_t at = 0;
    size_t total = 0;
    int count = 0;
    int shown = 0;

    /* The strings up to the first empty one, or the data's end. */
    while (at < bytes) {
        size_t n = str_len(s + at, bytes - at);
        if (n == 0) {
            break;
        }
        count++;
        at += n + 1;
    }
    if (count == 0) {
        tr_put(t, ", no strings\n");
        return;
    }
    tr_printf(t, ", %d string%s:\n", count, count == 1 ? "" : "s");
    at = 0;
    while (shown < count && shown < TR_REG_MSZ_STRINGS &&
           total < TR_REG_MSZ_CHARS) {
        size_t n = str_len(s + at, bytes - at);
        size_t max = TR_REG_STR_CHARS;
        if (max > TR_REG_MSZ_CHARS - total) {
            max = TR_REG_MSZ_CHARS - total;
        }
        tr_printf(t, "%s    ", indent);
        put_quoted(t, s + at, n, max);
        tr_put(t, "\n");
        total += n > max ? max : n;
        shown++;
        at += n + 1;
    }
    if (shown < count) {
        tr_printf(t, "%s    (%d more string%s not shown)\n", indent,
                  count - shown, count - shown == 1 ? "" : "s");
    }
}

/* The value's first words: indent, its name, " = " and its type. */
static void put_head(TR_TEXT *t, const char *indent, const char *name,
                     TR_U32 type, char *proj, const TR_REG_SCR *scr)
{
    size_t n = name != NULL ? strlen(name) : 0;
    int nameAnswer = TR_REG_SHOW;

    tr_put(t, indent);
    if (n == 0) {
        tr_put(t, "(default)");
    } else {
        nameAnswer = screen_bytes((const unsigned char *)name, (TR_U32)n,
                                  proj, scr);
        if (nameAnswer == TR_REG_WITHHOLD) {
            tr_put(t, "(a name holding this machine's name)");
        } else {
            tr_put(t, "\"");
            put_chars(t, name, n > TR_REG_NAME_SHOWN ? TR_REG_NAME_SHOWN : n,
                      nameAnswer == TR_REG_UNSCREENED);
            tr_put(t, n > TR_REG_NAME_SHOWN ? "...\"" : "\"");
            if (n > TR_REG_NAME_SHOWN) {
                tr_printf(t, " (a name of %lu characters)", (unsigned long)n);
            }
        }
    }
    tr_put(t, " = ");
    put_type(t, type);
}

void tr_reg_value_text(TR_TEXT *t, const char *indent, const char *name,
                       TR_U32 type, const unsigned char *data, TR_U32 bytes,
                       char *proj, const TR_REG_SCR *scr)
{
    int answer;

    put_head(t, indent, name, type, proj, scr);

    if (tr_reg_secret_name(name)) {
        tr_printf(t, ", %lu byte%s: withheld (its name suggests a key or "
                     "password)\n", (unsigned long)bytes,
                  bytes == 1 ? "" : "s");
        return;
    }
    /* Numbers of their own size are never screened: they hold no name. */
    if ((type == TR_REG_DWORD || type == TR_REG_DWORD_BIG_ENDIAN) &&
        bytes == 4) {
        TR_U32 v = type == TR_REG_DWORD
                       ? le32(data)
                       : ((TR_U32)data[0] << 24) | ((TR_U32)data[1] << 16) |
                             ((TR_U32)data[2] << 8) | (TR_U32)data[3];
        tr_printf(t, " %lu (0x%08lX)\n", (unsigned long)v, (unsigned long)v);
        return;
    }
    if (type == TR_REG_QWORD && bytes == 8) {
        TR_U32 lo = le32(data);
        TR_U32 hi = le32(data + 4);
        tr_put(t, " ");
        put_dec64(t, hi, lo);
        tr_printf(t, " (0x%08lX%08lX)\n", (unsigned long)hi,
                  (unsigned long)lo);
        return;
    }
    answer = screen_bytes(data, bytes, proj, scr);
    if (answer != TR_REG_SHOW) {
        tr_printf(t, ", %lu byte%s: withheld (%s)\n", (unsigned long)bytes,
                  bytes == 1 ? "" : "s",
                  answer == TR_REG_WITHHOLD ? "holds this machine's name"
                                            : "not screened");
        return;
    }
    switch (type) {
    case TR_REG_SZ:
    case TR_REG_EXPAND_SZ:
        tr_put(t, " ");
        put_quoted(t, (const char *)data, str_len((const char *)data, bytes),
                   TR_REG_STR_CHARS);
        tr_put(t, "\n");
        break;
    case TR_REG_MULTI_SZ:
        put_multi(t, indent, (const char *)data, bytes);
        break;
    case TR_REG_DWORD:
    case TR_REG_DWORD_BIG_ENDIAN:
        put_hex(t, indent, data, bytes, "(not 4)");
        break;
    case TR_REG_QWORD:
        put_hex(t, indent, data, bytes, "(not 8)");
        break;
    default:
        put_hex(t, indent, data, bytes, NULL);
        break;
    }
}

#define TR_REG_SKIP     0xFFFFFFFFUL    /* listed without its data */

/* A note of a key's text (task 50.39): fitted to its width. */
static void key_note(TR_TEXT *t, const TR_REG_KEY *k, const char *line)
{
    tr_fit_text(t, line, k->Width > 0 ? k->Width : TR_TEXT_COLS);
}

/* The limits reached at value `index`: the rest counted by name and size
 * alone, and said. A name too long for the buffer still counts as a value.
 * Any answer but "no more items" leaves the count a lower bound, and the
 * line says so with the error, as it does at TR_REG_COUNT_MAX. */
static void count_rest(TR_TEXT *t, const char *indent, TR_REG_KEY *k,
                       TR_U32 index)
{
    TR_U32 more = 0;
    TR_U32 rc;
    char line[400];

    line[0] = '\0';
    for (;;) {
        TR_U32 nameChars = TR_REG_NAME_CHARS;
        TR_U32 type = 0;
        TR_U32 bytes = 0;

        if (more >= TR_REG_COUNT_MAX) {
            rc = TR_REG_OK;
            break;
        }
        rc = k->Enum(k->Context, index + more, k->Name, &nameChars, &type,
                     NULL, &bytes);
        if (rc != TR_REG_OK && rc != TR_REG_MORE_DATA) {
            break;
        }
        more++;
    }
    if (rc == TR_REG_NO_MORE) {
        if (more != 0) {
            tr_sprintf(line, sizeof(line), "%s(%lu more value%s not listed: "
                       "the report lists %d values / 64 KB of a key)\n",
                       indent, (unsigned long)more, more == 1 ? "" : "s",
                       TR_REG_KEY_VALUES);
        }
    } else if (rc == TR_REG_OK) {
        tr_sprintf(line, sizeof(line), "%s(%lu or more values not listed: "
                   "the report lists %d values / 64 KB of a key, and counts "
                   "no further than %lu)\n", indent, (unsigned long)more,
                   TR_REG_KEY_VALUES, (unsigned long)TR_REG_COUNT_MAX);
    } else if (more == 0) {
        tr_sprintf(line, sizeof(line), "%s(the values past the report's %d "
                   "values / 64 KB of a key were not counted: the count "
                   "stopped at value %lu: error %lu)\n", indent,
                   TR_REG_KEY_VALUES, (unsigned long)index,
                   (unsigned long)rc);
    } else {
        tr_sprintf(line, sizeof(line), "%s(%lu or more values not listed: "
                   "the report lists %d values / 64 KB of a key; the count "
                   "stopped at value %lu: error %lu)\n", indent,
                   (unsigned long)more, TR_REG_KEY_VALUES,
                   (unsigned long)(index + more), (unsigned long)rc);
    }
    key_note(t, k, line);
}

int tr_reg_key_text(TR_TEXT *t, const char *indent, TR_REG_KEY *k)
{
    size_t start = t->Len;
    TR_U32 index;
    TR_U32 rc = TR_REG_OK;
    int listed = 0;
    char line[400];

    if (k->OpenError != 0) {
        tr_sprintf(line, sizeof(line), "%snot read: the key did not open "
                   "(error %lu)\n", indent, (unsigned long)k->OpenError);
        key_note(t, k, line);
        return 0;
    }
    for (index = 0; ; index++) {
        TR_U32 nameChars;
        TR_U32 type;
        TR_U32 bytes;
        int tries = 0;

        if (listed >= TR_REG_KEY_VALUES ||
            (t->Len - start) + TR_REG_VALUE_MAX > TR_REG_KEY_TEXT) {
            count_rest(t, indent, k, index);
            rc = TR_REG_NO_MORE;
            break;
        }
        for (;;) {
            nameChars = TR_REG_NAME_CHARS;
            bytes = TR_REG_READ_BYTES;
            type = 0;
            rc = k->Enum(k->Context, index, k->Name, &nameChars, &type,
                         k->Data, &bytes);
            if (rc != TR_REG_MORE_DATA) {
                break;
            }
            /* Too little room: the name and the size alone. */
            nameChars = TR_REG_NAME_CHARS;
            bytes = 0;
            type = 0;
            rc = k->Enum(k->Context, index, k->Name, &nameChars, &type, NULL,
                         &bytes);
            if (rc == TR_REG_MORE_DATA) {
                tr_sprintf(line, sizeof(line), "%s(a value whose name is "
                           "longer than %lu characters: not read)\n",
                           indent, (unsigned long)(TR_REG_NAME_CHARS - 1));
                key_note(t, k, line);
                rc = TR_REG_SKIP;
                break;
            }
            if (rc != TR_REG_OK) {
                break;
            }
            k->Name[nameChars < TR_REG_NAME_CHARS ? nameChars
                                                  : TR_REG_NAME_CHARS - 1] =
                '\0';
            if (bytes > TR_REG_READ_BYTES) {
                put_head(t, indent, k->Name, type, k->Proj, k->Scr);
                tr_printf(t, ", %lu bytes: more than %lu, not read\n",
                          (unsigned long)bytes,
                          (unsigned long)TR_REG_READ_BYTES);
                rc = TR_REG_SKIP;
                break;
            }
            if (++tries >= 3) {
                put_head(t, indent, k->Name, type, k->Proj, k->Scr);
                tr_put(t, ": its size changed at each of 3 reads; not "
                          "shown\n");
                rc = TR_REG_SKIP;
                break;
            }
        }
        if (rc == TR_REG_SKIP) {
            listed++;
            continue;
        }
        if (rc == TR_REG_NO_MORE) {
            break;
        }
        if (rc != TR_REG_OK) {
            tr_sprintf(line, sizeof(line), "%s(the reading stopped at value "
                       "%lu: error %lu)\n", indent, (unsigned long)index,
                       (unsigned long)rc);
            key_note(t, k, line);
            break;
        }
        if (bytes > TR_REG_READ_BYTES) {
            bytes = TR_REG_READ_BYTES;
        }
        k->Name[nameChars < TR_REG_NAME_CHARS ? nameChars
                                              : TR_REG_NAME_CHARS - 1] = '\0';
        tr_reg_value_text(t, indent, k->Name, type, k->Data, bytes, k->Proj,
                          k->Scr);
        listed++;
    }
    if (listed == 0 && rc == TR_REG_NO_MORE) {
        tr_printf(t, "%sno values\n", indent);
    } else if (listed > 0) {
        tr_printf(t, "%s%d value%s\n", indent, listed,
                  listed == 1 ? "" : "s");
    }
    return listed;
}

/* ---- the devnodes of one device ----------------------------------------- */

typedef struct _TREE_STATE {
    int Listed;
    TR_U32 Over;            /* past TR_REG_TREE_NODES, counted only       */
    TR_U32 Visits;
    int Stopped;
} TREE_STATE;

static int tree_note(const TR_REG_TREE *w, TREE_STATE *s, int level,
                     const char *line)
{
    if (!w->Note(w->Context, level, line)) {
        s->Stopped = 1;
        return 0;
    }
    return 1;
}

/* The devnodes below `node` not all read: rc is the answer that said so. */
static int tree_partial(const TR_REG_TREE *w, TREE_STATE *s, int level,
                        TR_U32 rc)
{
    char line[120];

    if (rc == TR_REG_REL_PARTIAL) {
        strcpy(line, "(the devnodes below it were not all read)");
    } else {
        tr_sprintf(line, sizeof(line), "(the devnodes below it were not all "
                   "read: error %lu)", (unsigned long)rc);
    }
    return tree_note(w, s, level, line);
}

static int tree_node(const TR_REG_TREE *w, TREE_STATE *s, TR_U32 node,
                     int level)
{
    TR_U32 child;
    TR_U32 rc;
    int listing;

    if (++s->Visits > TR_REG_TREE_VISITS) {
        return 1;
    }
    listing = s->Listed < TR_REG_TREE_NODES;
    if (listing) {
        if (!w->Emit(w->Context, node, level)) {
            s->Stopped = 1;
            return 0;
        }
        s->Listed++;
    } else {
        s->Over++;
    }
    /* A hub's devnodes are the devices on its ports, each a device of its
     * own. */
    if (w->IsHub(w->Context, node)) {
        return 1;
    }
    rc = w->Child(w->Context, node, &child);
    if (rc == TR_REG_REL_NONE) {
        return 1;
    }
    if (rc != TR_REG_REL_OK) {
        return !listing || tree_partial(w, s, level + 1, rc);
    }
    if (level >= TR_REG_TREE_DEPTH) {
        char line[160];

        if (!listing) {
            return 1;
        }
        tr_sprintf(line, sizeof(line), "(the devnodes below it are not "
                   "listed: the report follows devnodes %d levels below a "
                   "USB device)", TR_REG_TREE_DEPTH);
        return tree_note(w, s, level + 1, line);
    }
    for (;;) {
        TR_U32 next;

        if (!tree_node(w, s, child, level + 1)) {
            return 0;
        }
        if (s->Visits > TR_REG_TREE_VISITS) {
            return 1;
        }
        rc = w->Sibling(w->Context, child, &next);
        if (rc == TR_REG_REL_NONE) {
            return 1;
        }
        if (rc != TR_REG_REL_OK) {
            return !listing || tree_partial(w, s, level + 1, rc);
        }
        child = next;
    }
}

int tr_reg_tree_walk(const TR_REG_TREE *w, TR_U32 device)
{
    TREE_STATE s;

    memset(&s, 0, sizeof(s));
    if (!tree_node(w, &s, device, 0)) {
        return -1;
    }
    if (s.Over != 0 || s.Visits > TR_REG_TREE_VISITS) {
        char line[200];

        tr_sprintf(line, sizeof(line), "(%s%lu more devnode%s below this "
                   "device not listed: the report lists %d devnodes of one "
                   "device)", s.Visits > TR_REG_TREE_VISITS ? "at least " : "",
                   (unsigned long)s.Over, s.Over == 1 ? "" : "s",
                   TR_REG_TREE_NODES);
        if (!tree_note(w, &s, 1, line)) {
            return -1;
        }
    }
    return s.Listed;
}

int tr_reg_hub_id(const char *id)
{
    size_t i;

    if (id == NULL) {
        return 0;
    }
    if (at_ci(id, 0, "USB\\XHCI98_HUB")) {
        return 1;
    }
    for (i = 0; id[i] != '\0'; i++) {
        if (at_ci(id, i, "ROOT_HUB")) {
            return 1;
        }
    }
    return 0;
}
