/*
 * sysinfo.c - the system description in the report header (roadmap-hcd.md
 * task 36.48): which machine a pasted report came from.
 *
 * This half is pure. It finds and checks an SMBIOS entry point in an image
 * of F0000h-FFFFFh, walks a structure table it is handed, turns CPUID
 * register values into text, and prints. It reads no memory it was not
 * given and touches no port, so xhciqual/test/test_sysinfo.c drives every
 * branch on the build host: a valid 2.x table, a 3.0 entry, a truncated and
 * a garbled table, missing strings. sysprobe.c is the DOS half that reads
 * the BIOS area, maps the table and calls CPUID, E820 and DOS.
 *
 * **NO IDENTIFIERS** (owner, 2026-10-10). The header names the make and
 * model, never the unit. The string and byte fields that identify one
 * machine - Type 1 serial number, UUID and SKU; Type 2 serial number and
 * asset tag; Type 3 serial number and asset tag; Type 4 serial number,
 * asset tag and part number - are never copied out of the table, so there
 * is nothing for a printer to leak. They are fetched into a local only to
 * withhold any printed string that repeats one (see scrub()), since an
 * allowed field can name the same string as a serial. The test suite
 * plants all of them, aliased too, and checks no line carries one.
 *
 * Bounds: every structure's formatted length is checked against the table,
 * every field offset against the formatted length, and every string walk
 * against the end of its string set; the table itself is capped at
 * SMB_TABLE_MAX bytes. A malformed entry ends the walk and keeps what was
 * read before it, so a broken table cannot crash or hang the tool.
 */

#include <stdio.h>
#include <string.h>
#include "qual.h"

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static int sum_zero(const u8 *p, u32 n)
{
    u8 s;
    u32 i;

    s = 0;
    for (i = 0; i < n; i++)
        s = (u8)(s + p[i]);
    return s == 0;
}

/*
 * Copy at most n bytes of src (stopping at a NUL) into out[size]: anything
 * outside printable ASCII becomes '?', runs of spaces become one, and
 * leading and trailing spaces go. Firmware pads strings with spaces and
 * the odd control character; neither belongs in a log line.
 */
static void si_copy(char *out, u32 size, const u8 *src, u32 n)
{
    u32 i, o;
    int space;

    o = 0;
    space = 0;
    for (i = 0; i < n && src[i] != 0; i++) {
        u8 ch;

        ch = src[i];
        if (ch == ' ') {
            space = 1;
            continue;
        }
        if (ch < 0x20 || ch > 0x7E)
            ch = '?';
        if (space && o > 0 && o + 1 < size)
            out[o++] = ' ';
        space = 0;
        if (o + 1 < size)
            out[o++] = (char)ch;
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int smb_scan(const u8 *area, u32 len, u32 area_phys, SYSINFO *si)
{
    u32 off;
    int got3, got2, gotd;
    u32 off3, off2, offd;

    got3 = got2 = gotd = 0;
    off3 = off2 = offd = 0;
    si->smb_state = SMB_NONE;
    si->smb_kind = 0;
    si->other_ep_bad = 0;
    for (off = 0; off + 16 <= len; off += 16) {
        const u8 *p;

        p = area + off;
        if (memcmp(p, "_SM3_", 5) == 0) {
            /* 3.0: 18h bytes, one checksum over the length byte's count */
            if (!got3 && off + 0x18 <= len && p[6] >= 0x18 &&
                (u32)p[6] <= len - off && sum_zero(p, p[6])) {
                got3 = 1;
                off3 = off;
            } else if (!got3) {
                si->other_ep_bad = 1;
            }
        } else if (memcmp(p, "_SM_", 4) == 0) {
            /* 2.1+: 1Fh bytes (1Eh on some 2.1 firmware), its own checksum,
             * then the intermediate "_DMI_" anchor at 10h with a checksum
             * over 0Fh bytes. */
            if (!got2 && off + 0x20 <= len && p[5] >= 0x1E && p[5] <= 0x20 &&
                sum_zero(p, p[5]) && memcmp(p + 0x10, "_DMI_", 5) == 0 &&
                sum_zero(p + 0x10, 0x0F)) {
                got2 = 1;
                off2 = off;
            } else if (!got2) {
                si->other_ep_bad = 1;
            }
        } else if (memcmp(p, "_DMI_", 5) == 0) {
            /* A legacy DMI 2.0 entry alone, or the intermediate anchor of
             * a "_SM_" just above (used only if no "_SM_" validates). */
            if (!gotd && sum_zero(p, 0x0F)) {
                gotd = 1;
                offd = off;
            }
        }
    }

    if (got3 && rd32(area + off3 + 0x14) == 0) {
        const u8 *p = area + off3;

        si->smb_kind = SMB_EP_64;
        si->smb_ep_phys = area_phys + off3;
        si->smb_major = p[7];
        si->smb_minor = p[8];
        si->table_len = rd32(p + 0x0C);     /* the maximum size */
        si->table_lo = rd32(p + 0x10);
        si->table_hi = 0;
        si->table_count = 0;
    } else if (got2) {
        const u8 *p = area + off2;

        si->smb_kind = SMB_EP_32;
        si->smb_ep_phys = area_phys + off2;
        si->smb_major = p[6];
        si->smb_minor = p[7];
        si->table_len = rd16(p + 0x16);
        si->table_lo = rd32(p + 0x18);
        si->table_hi = 0;
        si->table_count = rd16(p + 0x1C);
    } else if (gotd) {
        /* Before an unusable 3.0 entry: a legacy table below 4 GB can
         * still be read (review round 1, P3). */
        const u8 *p = area + offd;

        si->smb_kind = SMB_EP_DMI;
        si->smb_ep_phys = area_phys + offd;
        si->smb_major = (u8)(p[0x0E] >> 4);  /* BCD revision */
        si->smb_minor = (u8)(p[0x0E] & 0x0F);
        si->table_len = rd16(p + 6);
        si->table_lo = rd32(p + 8);
        si->table_hi = 0;
        si->table_count = rd16(p + 0x0C);
    } else if (got3) {
        /* Kept only to say why nothing was read (above 4 GB). */
        const u8 *p = area + off3;

        si->smb_kind = SMB_EP_64;
        si->smb_ep_phys = area_phys + off3;
        si->smb_major = p[7];
        si->smb_minor = p[8];
        si->table_len = rd32(p + 0x0C);
        si->table_lo = rd32(p + 0x10);
        si->table_hi = rd32(p + 0x14);
        si->table_count = 0;
    } else {
        return 0;
    }

    if (si->table_len == 0)
        si->smb_state = SMB_EMPTY;
    else if (si->table_hi != 0 ||
             si->table_lo + si->table_len < si->table_lo)
        si->smb_state = SMB_ABOVE_4G;       /* or wraps past 4 GB */
    else
        si->smb_state = SMB_OK;
    return si->smb_kind;
}

/* ------------------------------------------------------------------ */
/* Structure table                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    const u8 *f;        /* the structure (formatted area)       */
    u32 flen;           /* its formatted length                 */
    const u8 *s;        /* the string set                       */
    u32 slen;           /* its bytes, through the double NUL     */
} SMB_STRUCT;

/* The byte at a formatted-area offset, or 0 when the structure is too
 * short to carry it (an older SMBIOS version's shorter structure). */
static u8 fbyte(const SMB_STRUCT *st, u32 at)
{
    return (at < st->flen) ? st->f[at] : 0;
}

/* String `field` names, bounded by the string set; empty when absent. */
static void fstr(const SMB_STRUCT *st, u32 field, char *out)
{
    u8 k;
    u32 i;

    out[0] = '\0';
    k = fbyte(st, field);
    if (k == 0)
        return;
    i = 0;
    while (k > 1) {
        while (i < st->slen && st->s[i] != 0)
            i++;
        i++;                            /* past this string's NUL */
        if (i >= st->slen || st->s[i] == 0)
            return;                     /* fewer strings than k */
        k--;
    }
    if (i < st->slen)
        si_copy(out, SI_STR, st->s + i, st->slen - i);
}

static u32 add_sat(u32 a, u32 b)
{
    return (a + b < a) ? 0xFFFFFFFFUL : a + b;
}

/* A 64-bit byte address (lo, hi) in KB, saturating. */
static u32 kb64(const u8 *p)
{
    u32 lo, hi;

    lo = rd32(p);
    hi = rd32(p + 4);
    if (hi >= 0x400UL)
        return 0xFFFFFFFFUL;
    return (hi << 22) | (lo >> 10);
}

static void take(const SMB_STRUCT *st, u8 type, SYSINFO *si)
{
    switch (type) {
    case 0:     /* BIOS Information */
        if (si->seen_t0)
            break;
        si->seen_t0 = 1;
        fstr(st, 0x04, si->bios_vendor);
        fstr(st, 0x05, si->bios_version);
        fstr(st, 0x08, si->bios_date);
        break;
    case 1:     /* System Information: NOT 07h serial, 08h-17h UUID,
                 * 19h SKU */
        if (si->seen_t1)
            break;
        si->seen_t1 = 1;
        fstr(st, 0x04, si->sys_mfr);
        fstr(st, 0x05, si->sys_product);
        fstr(st, 0x06, si->sys_version);
        fstr(st, 0x1A, si->sys_family);
        break;
    case 2:     /* Baseboard: NOT 07h serial, 08h asset tag */
        if (si->seen_t2)
            break;
        si->seen_t2 = 1;
        fstr(st, 0x04, si->board_mfr);
        fstr(st, 0x05, si->board_product);
        break;
    case 4:     /* Processor: NOT 20h serial, 21h asset tag, 22h part.
                 * The first populated socket (status 18h bit 6). */
        if (si->seen_t4)
            break;
        if (st->flen > 0x18 && (st->f[0x18] & 0x40) == 0)
            break;
        si->seen_t4 = 1;
        fstr(st, 0x04, si->cpu_socket);
        fstr(st, 0x10, si->cpu_version);
        break;
    case 17:    /* Memory Device: size 0Ch, extended size 1Ch */
        if (st->flen >= 0x0E) {
            u16 size;
            u32 kb;

            size = rd16(st->f + 0x0C);
            if (size == 0 || size == 0xFFFF)
                break;              /* empty slot, or unknown */
            if (size == 0x7FFF) {
                u32 mb;

                if (st->flen < 0x20)
                    break;
                mb = rd32(st->f + 0x1C) & 0x7FFFFFFFUL;
                kb = (mb >= 0x400000UL) ? 0xFFFFFFFFUL : mb << 10;
            } else if (size & 0x8000) {
                kb = size & 0x7FFF;
            } else {
                kb = (u32)size << 10;
            }
            si->mem17_kb = add_sat(si->mem17_kb, kb);
            si->mem17_n++;
        }
        break;
    case 19:    /* Memory Array Mapped Address: KB, or bytes extended */
        if (st->flen >= 0x0F) {
            u32 st_kb, en_kb;

            st_kb = rd32(st->f + 0x04);
            en_kb = rd32(st->f + 0x08);
            if (st_kb == 0xFFFFFFFFUL) {
                if (st->flen < 0x1F)
                    break;
                st_kb = kb64(st->f + 0x0F);
                en_kb = kb64(st->f + 0x17);
            }
            if (en_kb < st_kb)
                break;
            si->mem19_kb = add_sat(si->mem19_kb,
                                   add_sat(en_kb - st_kb, 1));
            si->mem19_n++;
        }
        break;
    default:
        break;
    }
}

/*
 * **Identifiers by value, not only by field** (review round 1, P1). Not
 * copying the serial's field is not enough: a Type 1 whose manufacturer and
 * serial number name the same string index, or a product string whose text
 * is the board's serial, prints the identifier through an allowed field.
 * So after the walk every identifier string in the table - Type 1 serial
 * and SKU, Type 2 and 3 serial and asset tag, Type 4 serial, asset tag and
 * part number, Type 17 serial, asset tag and part number - is fetched into
 * a local and every string the header would print is compared with it: a
 * printed string containing it anywhere, ignoring case, prints
 * "(withheld)". Type 1's UUID is compared as 32 hex digits in both byte
 * orders, ignoring case and separators. The identifier itself is never
 * stored in SYSINFO.
 *
 * **Except a firmware placeholder** (review round 3, P1; orchestrator
 * decision, owner to confirm): a value the same on every unit identifies
 * none, and matching it would withhold most of a header - a serial of "0"
 * appears in every BIOS date. A placeholder is one character repeated
 * ("0", "00000000", "FFFFFFFF", " ") or one of the stock phrases below;
 * anything else of any length is matched as a substring.
 *
 * And when the walk cannot finish - a malformed structure, or a table
 * longer than SMB_TABLE_MAX or 4096 structures - an identifier in the
 * unread part could match an earlier printed string, so every SMBIOS
 * string is withheld (review round 2, P1); the version, memory sums and
 * the rest still print.
 */
#define WITHHELD "(withheld)"

typedef struct {
    u8 type;
    u8 nfield;
    u8 field[3];
} SMB_IDS;

static const SMB_IDS id_fields[] = {
    {  1, 2, { 0x07, 0x19, 0 } },
    {  2, 2, { 0x07, 0x08, 0 } },
    {  3, 2, { 0x07, 0x08, 0 } },
    {  4, 3, { 0x20, 0x21, 0x22 } },
    { 17, 3, { 0x18, 0x19, 0x1A } }
};

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* strstr, ignoring ASCII case. */
static int contains_nocase(const char *hay, const char *needle)
{
    size_t i, n;

    n = strlen(needle);
    if (n == 0)
        return 0;
    for (; *hay != '\0'; hay++) {
        for (i = 0; i < n && hay[i] != '\0' &&
             lower(hay[i]) == lower(needle[i]); i++)
            ;
        if (i == n)
            return 1;
    }
    return 0;
}

/* Stock firmware fillers, compared ignoring case; a value CONTAINING one
 * of the first group is a filler too ("Chassis Serial Number"). */
static const char *const filler_part[] = {
    "serial number", "o.e.m.", "filled by", "not specified",
    "default string", "not available", "not applicable", "asset tag",
    "no asset", "unknown", 0
};
static const char *const filler_whole[] = {
    "none", "n/a", "na", "default", "oem", "sku", "123456789",
    "1234567890", "0123456789", 0
};

static int is_placeholder(const char *id)
{
    const char *p;
    int i;

    for (p = id + 1; *p != '\0' && *p == id[0]; p++)
        ;
    if (*p == '\0')
        return 1;                       /* one character repeated */
    for (i = 0; filler_part[i] != 0; i++) {
        if (contains_nocase(id, filler_part[i]))
            return 1;
    }
    for (i = 0; filler_whole[i] != 0; i++) {
        if (strlen(id) == strlen(filler_whole[i]) &&
            contains_nocase(id, filler_whole[i]))
            return 1;
    }
    return 0;
}

/* Does `shown` carry `id`: anywhere, ignoring case, unless id is a
 * placeholder. */
static int carries(const char *shown, const char *id)
{
    if (id[0] == '\0' || is_placeholder(id))
        return 0;
    return contains_nocase(shown, id);
}

/* Every printed SMBIOS string, for the scrubber and for withhold_all. */
static char *shown_field(SYSINFO *si, int i)
{
    switch (i) {
    case 0:  return si->bios_vendor;
    case 1:  return si->bios_version;
    case 2:  return si->bios_date;
    case 3:  return si->sys_mfr;
    case 4:  return si->sys_product;
    case 5:  return si->sys_version;
    case 6:  return si->sys_family;
    case 7:  return si->board_mfr;
    case 8:  return si->board_product;
    case 9:  return si->cpu_socket;
    case 10: return si->cpu_version;
    default: return 0;
    }
}

static void withhold_if(char *shown, const char *id)
{
    if (shown[0] == '\0' || strcmp(shown, WITHHELD) == 0)
        return;
    if (carries(shown, id))
        strcpy(shown, WITHHELD);
}

/* The UUID's 16 bytes as 32 uppercase hex digits: raw order, and the
 * SMBIOS 2.6+ text order (first three fields little-endian). */
static void uuid_hex(const u8 *u, char *raw, char *wire)
{
    static const char hx[] = "0123456789ABCDEF";
    static const u8 order[16] = { 3, 2, 1, 0, 5, 4, 7, 6,
                                  8, 9, 10, 11, 12, 13, 14, 15 };
    int i;

    for (i = 0; i < 16; i++) {
        raw[i * 2] = hx[u[i] >> 4];
        raw[i * 2 + 1] = hx[u[i] & 0x0F];
        wire[i * 2] = hx[u[order[i]] >> 4];
        wire[i * 2 + 1] = hx[u[order[i]] & 0x0F];
    }
    raw[32] = '\0';
    wire[32] = '\0';
}

/* A printed string's hex digits alone, upper case: what a UUID written
 * with dashes, braces or in lower case reduces to. */
static void hex_only(const char *s, char *out, size_t size)
{
    size_t o;

    o = 0;
    for (; *s != '\0' && o + 1 < size; s++) {
        char c = *s;

        if (c >= 'a' && c <= 'f')
            c = (char)(c - 'a' + 'A');
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
            out[o++] = c;
    }
    out[o] = '\0';
}

static void scrub_uuid(const SMB_STRUCT *st, SYSINFO *si)
{
    char raw[33], wire[33], hex[SI_STR];
    int i, all00, allff;
    char *f;

    if (st->flen < 0x18)
        return;
    all00 = allff = 1;
    for (i = 0; i < 16; i++) {
        if (st->f[0x08 + i] != 0x00)
            all00 = 0;
        if (st->f[0x08 + i] != 0xFF)
            allff = 0;
    }
    if (all00 || allff)
        return;                 /* "not present" / "not set" */
    uuid_hex(st->f + 0x08, raw, wire);
    for (i = 0; (f = shown_field(si, i)) != 0; i++) {
        hex_only(f, hex, sizeof(hex));
        if (strstr(hex, raw) != 0 || strstr(hex, wire) != 0)
            strcpy(f, WITHHELD);
    }
    memset(raw, 0, sizeof(raw));
    memset(wire, 0, sizeof(wire));
}

static void scrub(const SMB_STRUCT *st, u8 type, SYSINFO *si)
{
    unsigned i, k;
    int j;
    char id[SI_STR];
    char *f;

    if (type == 1)
        scrub_uuid(st, si);
    for (i = 0; i < sizeof(id_fields) / sizeof(id_fields[0]); i++) {
        if (id_fields[i].type != type)
            continue;
        for (k = 0; k < id_fields[i].nfield; k++) {
            fstr(st, id_fields[i].field[k], id);
            if (id[0] == '\0')
                continue;
            for (j = 0; (f = shown_field(si, j)) != 0; j++)
                withhold_if(f, id);
        }
    }
    memset(id, 0, sizeof(id));
}

static void withhold_all(SYSINFO *si)
{
    int j;
    char *f;

    for (j = 0; (f = shown_field(si, j)) != 0; j++) {
        if (f[0] != '\0')
            strcpy(f, WITHHELD);
    }
}

typedef void (*SMB_VISIT)(const SMB_STRUCT *st, u8 type, SYSINFO *si);

/* One bounded walk; returns the structures visited. *complete is 1 when
 * the walk saw the whole table: End-of-Table, the entry point's count,
 * zero padding, or the table's own end (not the SMB_TABLE_MAX cap). */
static int walk(const u8 *t, u32 len, u16 count, SMB_VISIT visit,
                SYSINFO *si, int *complete)
{
    u32 off;
    int n;

    si->broken = 0;
    *complete = 0;
    off = 0;
    n = 0;
    while (off < len && n < 4096) {
        SMB_STRUCT st;
        u8 type;
        u32 j;

        if (count != 0 && n >= (int)count) {
            *complete = 1;
            return n;
        }
        if (len - off >= 2 && t[off] == 0 && t[off + 1] == 0) {
            /* Zero padding past the last structure is not damage. A real
             * Type 0 has a formatted length of at least 12h. */
            for (j = off; j < len && t[j] == 0; j++)
                ;
            if (j == len) {
                /* ...unless the cap cut the table inside it. */
                *complete = (si->table_len <= len);
                return n;
            }
        }
        if (len - off < 4 || t[off + 1] < 4 || t[off + 1] > len - off) {
            si->broken = 1;
            si->broken_at = off;
            break;
        }
        type = t[off];
        st.f = t + off;
        st.flen = t[off + 1];
        /* The string set ends at the first double NUL. */
        j = off + st.flen;
        while (j + 1 < len && !(t[j] == 0 && t[j + 1] == 0))
            j++;
        if (j + 1 >= len) {
            si->broken = 1;
            si->broken_at = off;
            break;
        }
        st.s = t + off + st.flen;
        st.slen = j + 2 - (off + st.flen);
        visit(&st, type, si);
        n++;
        if (type == 127) {      /* End-of-Table */
            *complete = 1;
            return n;
        }
        off = j + 2;
    }
    /* Out of bytes: whole only if these were all the table's bytes. A
     * count reached exactly at the end is whole as well. */
    if (off >= len && (si->table_len <= len ||
                       (count != 0 && n >= (int)count)))
        *complete = 1;
    return n;
}

void smb_parse_table(const u8 *t, u32 len, u16 count, SYSINFO *si)
{
    int complete;

    if (len > SMB_TABLE_MAX)
        len = SMB_TABLE_MAX;
    si->used_len = len;
    si->nstructs = walk(t, len, count, take, si, &complete);
    /* The same bounds, so the same structures (and the same damage). */
    (void)walk(t, len, count, scrub, si, &complete);
    si->ids_unchecked = !complete;
    if (!complete)
        withhold_all(si);
}

/* ------------------------------------------------------------------ */
/* CPUID and the ROM                                                   */
/* ------------------------------------------------------------------ */

static void regs_bytes(u8 *out, const u32 *r, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        out[i * 4 + 0] = (u8)(r[i]);
        out[i * 4 + 1] = (u8)(r[i] >> 8);
        out[i * 4 + 2] = (u8)(r[i] >> 16);
        out[i * 4 + 3] = (u8)(r[i] >> 24);
    }
}

void cpu_fill_vendor(SYSINFO *si, const u32 leaf0[4])
{
    u32 r[3];
    u8 b[12];

    r[0] = leaf0[1];    /* EBX, EDX, ECX: "Genu" "ineI" "ntel" */
    r[1] = leaf0[3];
    r[2] = leaf0[2];
    regs_bytes(b, r, 3);
    si_copy(si->cpu_vendor, sizeof(si->cpu_vendor), b, 12);
}

void cpu_fill_signature(SYSINFO *si, u32 eax1)
{
    u32 base;

    si->cpu_sig = eax1;
    si->cpu_stepping = eax1 & 0x0F;
    base = (eax1 >> 8) & 0x0F;
    si->cpu_family = base;
    si->cpu_model = (eax1 >> 4) & 0x0F;
    if (base == 0x0F)
        si->cpu_family += (eax1 >> 20) & 0xFF;
    if (base == 0x06 || base == 0x0F)
        si->cpu_model |= ((eax1 >> 16) & 0x0F) << 4;
}

void cpu_fill_brand(SYSINFO *si, const u32 brand[12])
{
    u8 b[48];

    regs_bytes(b, brand, 12);
    si_copy(si->cpu_brand, sizeof(si->cpu_brand), b, 48);
}

/* --no-sysinfo (owner, 2026-10-10): for a machine where the probing itself
 * misbehaves. Nothing is read; one line says the option skipped it, on
 * the screen and in the log alike. Each under 80 characters. */
#define SKIPPED_LINE \
    "System:  not read - skipped by --no-sysinfo (no SMBIOS, CPUID, E820)\n"
#define SKIPPED_SHORT "System: not read (--no-sysinfo)\n"

void sysinfo_skip(SYSINFO *si)
{
    memset(si, 0, sizeof(*si));
    si->skipped = 1;
}

void rom_fill(SYSINFO *si, const u8 *date8, u8 model)
{
    si_copy(si->rom_date, sizeof(si->rom_date), date8, 8);
    si->rom_model = model;
    si->rom_read = 1;
}

/* ------------------------------------------------------------------ */
/* Printing                                                            */
/* ------------------------------------------------------------------ */

/* Append " s" (or s, at the start) when s is not empty. */
static void cat_word(char *line, const char *s)
{
    if (s[0] == '\0')
        return;
    if (line[0] != '\0')
        strcat(line, " ");
    strcat(line, s);
}

static const char *ep_name(int kind)
{
    if (kind == SMB_EP_64)
        return "64-bit entry";
    if (kind == SMB_EP_32)
        return "32-bit entry";
    return "legacy DMI entry";
}

static const char *oem_name(u8 oem)
{
    switch (oem) {
    case 0x00: return "IBM";
    case 0xEE: return "DR-DOS";
    case 0xEF: return "Novell";
    case 0xFD: return "FreeDOS";
    case 0xFF: return "Microsoft";
    default:   return 0;
    }
}

static const char *win_name(u8 major, u8 minor)
{
    if (major == 4 && minor == 0)
        return "Windows 95";
    if (major == 4 && minor == 10)
        return "Windows 98";
    if (major == 4 && minor == 90)
        return "Windows ME";
    if (major == 3)
        return "Windows 3.x enhanced mode";
    return 0;
}

static void cpu_text(const SYSINFO *si, char *out)
{
    out[0] = '\0';
    if (si->cpu_brand[0] != '\0')
        strcpy(out, si->cpu_brand);
    else if (si->seen_t4 && si->cpu_version[0] != '\0')
        strcpy(out, si->cpu_version);
}

/* The ROM fallback's words: "ROM BIOS date 08/22/98, model byte FCh". */
static void rom_text(const SYSINFO *si, char *out)
{
    if (!si->rom_read)
        strcpy(out, "ROM BIOS date not read");
    else
        sprintf(out, "ROM BIOS date %s, model byte %02Xh",
                si->rom_date[0] ? si->rom_date : "(blank)", si->rom_model);
}

static void smb_problem(const SYSINFO *si, char *out)
{
    switch (si->smb_state) {
    case SMB_ABOVE_4G:
        sprintf(out, "SMBIOS %u.%u table above 4 GB, not read",
                si->smb_major, si->smb_minor);
        break;
    case SMB_MAP_FAILED:
        sprintf(out, "SMBIOS %u.%u table at %08lXh not mapped",
                si->smb_major, si->smb_minor, si->table_lo);
        break;
    case SMB_EMPTY:
        sprintf(out, "SMBIOS %u.%u entry names an empty table",
                si->smb_major, si->smb_minor);
        break;
    default:
        strcpy(out, si->other_ep_bad ?
               "no SMBIOS (an entry point failed its checksum)" :
               "no SMBIOS");
        break;
    }
}

void report_sysinfo(const SYSINFO *si, QPRINTF_FN pf)
{
    char line[400];
    char part[160];

    if (si->skipped) {
        pf(SKIPPED_LINE);
        return;
    }
    if (si->smb_state == SMB_OK) {
        line[0] = '\0';
        if (si->seen_t1) {
            cat_word(line, si->sys_mfr);
            cat_word(line, si->sys_product);
            if (line[0] == '\0')
                strcpy(line, "(maker and model not given)");
            if (si->sys_version[0] != '\0') {
                strcat(line, ", version ");
                strcat(line, si->sys_version);
            }
            if (si->sys_family[0] != '\0') {
                strcat(line, ", family ");
                strcat(line, si->sys_family);
            }
        } else {
            strcpy(line, "(no SMBIOS system structure)");
        }
        pf("System:  %s\n", line);
        if (si->seen_t2) {
            line[0] = '\0';
            cat_word(line, si->board_mfr);
            cat_word(line, si->board_product);
            pf("Board:   %s\n", line[0] ? line : "(not given)");
        }
        line[0] = '\0';
        if (si->seen_t0) {
            cat_word(line, si->bios_vendor);
            cat_word(line, si->bios_version);
            if (si->bios_date[0] != '\0') {
                strcat(line, line[0] ? ", " : "");
                strcat(line, si->bios_date);
            }
            if (line[0] == '\0')
                strcpy(line, "(not given)");
        } else {
            strcpy(line, "(no SMBIOS BIOS structure)");
        }
        sprintf(part, "; SMBIOS %u.%u (%s at %05lXh, %d structures)",
                si->smb_major, si->smb_minor, ep_name(si->smb_kind),
                si->smb_ep_phys, si->nstructs);
        pf("BIOS:    %s%s\n", line, part);
        if (si->broken)
            pf("         SMBIOS table malformed at offset %lu; the "
               "rest was not read\n", si->broken_at);
        else if (si->ids_unchecked)
            pf("         SMBIOS table past this tool's limit (%lu KB or "
               "4096 structures); the rest was not read\n",
               SMB_TABLE_MAX >> 10);
        if (si->ids_unchecked)
            pf("         strings withheld: identifiers in the unread "
               "part could not be checked\n");
    } else {
        smb_problem(si, line);
        rom_text(si, part);
        pf("BIOS:    %s; %s\n", line, part);
    }

    cpu_text(si, line);
    if (si->has_cpuid) {
        sprintf(part, "%s family %02lXh model %02lXh stepping %lu",
                si->cpu_vendor[0] ? si->cpu_vendor : "(no vendor)",
                si->cpu_family, si->cpu_model, si->cpu_stepping);
        if (line[0] != '\0')
            pf("CPU:     %s (%s)", line, part);
        else
            pf("CPU:     %s", part);
    } else {
        pf("CPU:     %s (no CPUID)", line[0] ? line : "unknown");
    }
    if (si->seen_t4 && si->cpu_socket[0] != '\0')
        pf(", socket %s", si->cpu_socket);
    pf("\n");

    if (si->e820_n > 0)
        pf("RAM:     %lu MB usable (INT 15h E820, %d ranges)\n",
           si->e820_kb >> 10, si->e820_n);
    else if (si->mem17_n > 0)
        pf("RAM:     %lu MB in %d module(s) (SMBIOS Type 17; no E820)\n",
           si->mem17_kb >> 10, si->mem17_n);
    else if (si->mem19_n > 0)
        pf("RAM:     %lu MB mapped (SMBIOS Type 19; no E820)\n",
           si->mem19_kb >> 10);
    else
        pf("RAM:     unknown (no E820, no SMBIOS memory structures)\n");

    if (si->dos_read) {
        const char *oem;
        const char *win;

        oem = oem_name(si->dos_oem);
        if (oem != 0)
            sprintf(line, "%u.%02u (%s)", si->dos_major, si->dos_minor, oem);
        else
            sprintf(line, "%u.%02u (OEM %02Xh)", si->dos_major,
                    si->dos_minor, si->dos_oem);
        if (si->dos_true && (si->dos_true_major != si->dos_major ||
                             si->dos_true_minor != si->dos_minor)) {
            sprintf(part, ", true version %u.%02u", si->dos_true_major,
                    si->dos_true_minor);
            strcat(line, part);
        }
        if (si->win_state == 2) {
            win = win_name(si->win_major, si->win_minor);
            sprintf(part, "; in a Windows %u.%02u DOS box%s%s%s",
                    si->win_major, si->win_minor, win ? " (" : "",
                    win ? win : "", win ? ")" : "");
            strcat(line, part);
        } else if (si->win_state == 1) {
            strcat(line, "; in a Windows/386 2.x DOS box");
        } else {
            strcat(line, "; not under Windows");
        }
        pf("DOS:     %s\n", line);
    } else {
        pf("DOS:     version not read\n");
    }
}

static void pf79(QPRINTF_FN pf, const char *line)
{
    pf("%.79s\n", line);
}

void report_sysinfo_short(const SYSINFO *si, QPRINTF_FN pf)
{
    char line[400];
    char part[160];

    if (si->skipped) {
        pf(SKIPPED_SHORT);
        return;
    }
    strcpy(line, "System:");
    if (si->smb_state == SMB_OK) {
        part[0] = '\0';
        cat_word(part, si->sys_mfr);
        cat_word(part, si->sys_product);
        cat_word(line, part[0] ? part : "(not given)");
        part[0] = '\0';
        cat_word(part, si->bios_version);
        cat_word(part, si->bios_date);
        if (part[0] != '\0') {
            strcat(line, "; BIOS ");
            strcat(line, part);
        }
    } else {
        smb_problem(si, part);
        cat_word(line, part);
        rom_text(si, part);
        strcat(line, "; ");
        strcat(line, part);
    }
    pf79(pf, line);

    strcpy(line, "CPU:");
    cpu_text(si, part);
    if (part[0] == '\0' && si->has_cpuid)
        sprintf(part, "%s %02lXh/%02lXh/%lu",
                si->cpu_vendor[0] ? si->cpu_vendor : "CPUID",
                si->cpu_family, si->cpu_model, si->cpu_stepping);
    cat_word(line, part[0] ? part : "unknown");
    if (si->e820_n > 0)
        sprintf(part, "; RAM %lu MB", si->e820_kb >> 10);
    else if (si->mem17_n > 0)
        sprintf(part, "; RAM %lu MB", si->mem17_kb >> 10);
    else if (si->mem19_n > 0)
        sprintf(part, "; RAM %lu MB", si->mem19_kb >> 10);
    else
        part[0] = '\0';
    strcat(line, part);
    if (si->dos_read) {
        sprintf(part, "; DOS %u.%02u", si->dos_major, si->dos_minor);
        strcat(line, part);
        if (si->win_state == 2) {
            sprintf(part, " in Windows %u.%02u", si->win_major,
                    si->win_minor);
            strcat(line, part);
        }
    }
    pf79(pf, line);
}
