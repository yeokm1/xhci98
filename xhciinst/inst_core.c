/*
 * inst_core.c - the pure half of XHCIINST.EXE and XHCIIN64.EXE (design
 * record 23 section 10; inst_core.h). No Windows header: the host suite
 * test\test_inst.c links this file under /Za. C89.
 */

#include <string.h>
#include "inst_core.h"

/* ======================================================================== */
/* Strings                                                                   */
/* ======================================================================== */

int inst_upper(int c)
{
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

int inst_eqn(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (inst_upper((unsigned char)a[i]) != inst_upper((unsigned char)b[i])) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

int inst_eq(const char *a, const char *b)
{
    if (strlen(a) != strlen(b)) {
        return 0;
    }
    return inst_eqn(a, b, strlen(a));
}

int inst_starts(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);

    if (strlen(s) < n) {
        return 0;
    }
    return inst_eqn(s, prefix, n);
}

void inst_copy(char *dst, const char *src, size_t cap)
{
    size_t n;

    if (cap == 0) {
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int inst_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim in place. */
static void inst_trim(char *s)
{
    size_t n;
    size_t i;

    n = strlen(s);
    while (n > 0 && inst_space((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    i = 0;
    while (s[i] != '\0' && inst_space((unsigned char)s[i])) {
        i++;
    }
    if (i > 0) {
        memmove(s, s + i, n - i + 1);
    }
}

/* Remove one pair of surrounding double quotes, in place. */
static void inst_unquote(char *s)
{
    size_t n = strlen(s);

    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

/* Decimal or 0x hexadecimal, the whole string. 1 on success. */
static int inst_number(const char *s, INST_U32 *out)
{
    INST_U32 v = 0;
    int digits = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s != '\0') {
            int c = inst_upper((unsigned char)*s);
            INST_U32 d;
            if (c >= '0' && c <= '9') {
                d = (INST_U32)(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                d = (INST_U32)(c - 'A' + 10);
            } else {
                return 0;
            }
            if (++digits > 8) {
                return 0;
            }
            v = (v << 4) | d;
            s++;
        }
    } else {
        while (*s != '\0') {
            if (*s < '0' || *s > '9') {
                return 0;
            }
            if (++digits > 9) {
                return 0;
            }
            v = v * 10UL + (INST_U32)(*s - '0');
            s++;
        }
    }
    if (digits == 0) {
        return 0;
    }
    *out = v;
    return 1;
}

/* ======================================================================== */
/* The INF reader                                                            */
/* ======================================================================== */

/* The next raw logical line from p - comment removed, trimmed, possibly
 * empty - copied to line; the return points past it, NULL at the end. */
static const char *inst_raw_line(const char *p, char *line, size_t cap)
{
    size_t n = 0;
    int quoted = 0;
    int comment = 0;

    if (p == NULL || *p == '\0') {
        return NULL;
    }
    while (*p != '\0' && *p != '\n') {
        char c = *p++;
        if (c == '"') {
            quoted = !quoted;
        }
        if (c == ';' && !quoted) {
            comment = 1;
        }
        if (!comment && n + 1 < cap) {
            line[n++] = c;
        }
    }
    if (*p == '\n') {
        p++;
    }
    line[n] = '\0';
    inst_trim(line);
    return p;
}

/* 1 when line is a section header, its name copied to name. */
static int inst_header(const char *line, char *name, size_t cap)
{
    const char *end;
    size_t n;

    if (line[0] != '[') {
        return 0;
    }
    end = strchr(line, ']');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - line - 1);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(name, line + 1, n);
    name[n] = '\0';
    inst_trim(name);
    return 1;
}

const char *inst_inf_section(const char *text, const char *section)
{
    char line[INST_LINE_CHARS];
    char name[INST_LINE_CHARS];
    const char *p = text;

    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        if (inst_header(line, name, sizeof(name)) && inst_eq(name, section)) {
            return p;
        }
    }
    return NULL;
}

const char *inst_inf_line(const char *p, char *line, size_t cap)
{
    while ((p = inst_raw_line(p, line, cap)) != NULL) {
        if (line[0] == '[') {
            return NULL;
        }
        if (line[0] != '\0') {
            return p;
        }
    }
    return NULL;
}

int inst_inf_kv(const char *line, char *key, size_t keyCap,
                char *value, size_t valueCap)
{
    const char *eq = NULL;
    const char *s;
    int quoted = 0;
    size_t n;

    for (s = line; *s != '\0'; s++) {
        if (*s == '"') {
            quoted = !quoted;
        } else if (*s == '=' && !quoted) {
            eq = s;
            break;
        }
    }
    if (eq == NULL) {
        return 0;
    }
    n = (size_t)(eq - line);
    if (n >= keyCap) {
        n = keyCap - 1;
    }
    memcpy(key, line, n);
    key[n] = '\0';
    inst_trim(key);
    inst_copy(value, eq + 1, valueCap);
    inst_trim(value);
    return 1;
}

int inst_inf_field(const char *value, int index, char *out, size_t cap)
{
    const char *s = value;
    int field = 0;
    int quoted = 0;
    size_t n = 0;

    for (;;) {
        char c = *s;
        if (c == '\0' || (c == ',' && !quoted)) {
            if (field == index) {
                out[n] = '\0';
                inst_trim(out);
                inst_unquote(out);
                return 1;
            }
            if (c == '\0') {
                return 0;
            }
            field++;
            n = 0;
            s++;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
        }
        if (field == index && n + 1 < cap) {
            out[n++] = c;
        }
        s++;
    }
}

int inst_inf_expand(const char *text, const char *in, char *out, size_t cap)
{
    char token[INST_LINE_CHARS];
    char line[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    const char *p;
    size_t n = strlen(in);

    if (n < 2 || in[0] != '%' || in[n - 1] != '%') {
        inst_copy(out, in, cap);
        return 1;
    }
    inst_copy(token, in + 1, sizeof(token));
    token[n - 2 < sizeof(token) ? n - 2 : sizeof(token) - 1] = '\0';
    p = inst_inf_section(text, "Strings");
    while ((p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        if (inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) &&
            inst_eq(key, token)) {
            inst_unquote(value);
            inst_copy(out, value, cap);
            return 1;
        }
    }
    return 0;
}

int inst_inf_driverver(const char *text, INST_U32 ver[4])
{
    char line[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    char field[INST_LINE_CHARS];
    const char *p = inst_inf_section(text, "Version");

    while ((p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        if (inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) &&
            inst_eq(key, "DriverVer")) {
            if (!inst_inf_field(value, 1, field, sizeof(field))) {
                return 0;
            }
            return inst_ver_parse(field, ver);
        }
    }
    return 0;
}

/* ======================================================================== */
/* Versions                                                                  */
/* ======================================================================== */

int inst_ver_parse(const char *s, INST_U32 ver[4])
{
    int field = 0;
    int digits = 0;
    INST_U32 v = 0;

    ver[0] = ver[1] = ver[2] = ver[3] = 0;
    for (;;) {
        char c = *s++;
        if (c >= '0' && c <= '9') {
            if (++digits > 5) {
                return 0;
            }
            v = v * 10UL + (INST_U32)(c - '0');
            if (v > 65535UL) {
                return 0;
            }
        } else if (c == '.' || c == '\0') {
            if (digits == 0 || field >= 4) {
                return 0;
            }
            ver[field++] = v;
            v = 0;
            digits = 0;
            if (c == '\0') {
                return 1;
            }
        } else {
            return 0;
        }
    }
}

int inst_ver_cmp(const INST_U32 a[4], const INST_U32 b[4])
{
    int i;

    for (i = 0; i < 4; i++) {
        if (a[i] < b[i]) {
            return -1;
        }
        if (a[i] > b[i]) {
            return 1;
        }
    }
    return 0;
}

/* ======================================================================== */
/* Models sections                                                           */
/* ======================================================================== */

/* A decoration's own number field: 1 to 3 digits, at most 255. */
static const char *inst_deco_number(const char *s, INST_U32 *value)
{
    INST_U32 v = 0;
    int n = 0;

    while (*s >= '0' && *s <= '9') {
        if (++n > 3) {
            return NULL;
        }
        v = v * 10UL + (INST_U32)(*s - '0');
        s++;
    }
    if (n == 0 || v > 255UL) {
        return NULL;
    }
    *value = v;
    return s;
}

int inst_decoration_parse(const char *d, int *arch, INST_U32 *major,
                          INST_U32 *minor)
{
    const char *s;

    *arch = 0;
    *major = 0;
    *minor = 0;
    if (!inst_starts(d, "NT")) {
        return 0;
    }
    s = d + 2;
    if (inst_starts(s, "x86")) {
        *arch = INST_DECO_X86;
        s += 3;
    } else if (inst_starts(s, "amd64")) {
        *arch = INST_DECO_AMD64;
        s += 5;
    }
    if (*s == '.') {
        s = inst_deco_number(s + 1, major);
        if (s == NULL) {
            return 0;
        }
        if (*s == '.') {
            s = inst_deco_number(s + 1, minor);
            if (s == NULL) {
                return 0;
            }
        }
    }
    /* Nothing follows: no other architecture, no product-type or suite
     * field (record 23 sections 4.2 item 5 and 5.2). */
    return *s == '\0';
}

/* A decoration "NT[arch][.major[.minor]]". Returns 1 when it applies to
 * the system, with its rank (version, then arch-specific) in *rank. */
static int inst_decoration(const char *d, const INST_ENV *env, INST_U32 *rank)
{
    int arch;
    INST_U32 major;
    INST_U32 minor;

    if (!inst_decoration_parse(d, &arch, &major, &minor)) {
        return 0;
    }
    if ((arch == INST_DECO_X86 && env->Arch != INST_ARCH_X86) ||
        (arch == INST_DECO_AMD64 && env->Arch != INST_ARCH_AMD64)) {
        return 0;
    }
    if (major > env->Major || (major == env->Major && minor > env->Minor)) {
        return 0;
    }
    *rank = (major << 17) | (minor << 1) | (INST_U32)(arch != 0 ? 1 : 0);
    return 1;
}

int inst_models_section(const char *mfgValue, const INST_ENV *env,
                        char *out, size_t cap)
{
    char models[INST_LINE_CHARS];
    char deco[INST_LINE_CHARS];
    char best[INST_LINE_CHARS];
    INST_U32 rank;
    INST_U32 bestRank = 0;
    int found = 0;
    int i;

    if (!inst_inf_field(mfgValue, 0, models, sizeof(models))) {
        models[0] = '\0';
    }
    inst_copy(out, models, cap);
    if (!env->Nt || (env->Major == 5 && env->Minor == 0)) {
        return 1;
    }
    for (i = 1; inst_inf_field(mfgValue, i, deco, sizeof(deco)); i++) {
        if (deco[0] != '\0' && inst_decoration(deco, env, &rank) &&
            (!found || rank > bestRank)) {
            found = 1;
            bestRank = rank;
            inst_copy(best, deco, sizeof(best));
        }
    }
    if (found && strlen(models) + 1 + strlen(best) < cap) {
        strcpy(out, models);
        strcat(out, ".");
        strcat(out, best);
    }
    return 1;
}

static void inst_ids_add(INST_IDLIST *ids, const char *id)
{
    int i;

    if (id[0] == '\0') {
        return;
    }
    for (i = 0; i < ids->Count; i++) {
        if (inst_eq(ids->Id[i], id)) {
            return;
        }
    }
    if (ids->Count >= INST_MAX_IDS || strlen(id) >= INST_ID_CHARS) {
        ids->Overflow = 1;
        return;
    }
    inst_copy(ids->Id[ids->Count++], id, INST_ID_CHARS);
}

int inst_inf_models(const char *text, const INST_ENV *env, INST_IDLIST *ids)
{
    char line[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    char section[INST_LINE_CHARS];
    char mline[INST_LINE_CHARS];
    char field[INST_LINE_CHARS];
    const char *p;
    const char *m;
    int i;

    ids->Count = 0;
    ids->Overflow = 0;
    p = inst_inf_section(text, "Manufacturer");
    if (p == NULL) {
        return 0;
    }
    while ((p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        inst_models_section(value, env, section, sizeof(section));
        m = inst_inf_section(text, section);
        while ((m = inst_inf_line(m, mline, sizeof(mline))) != NULL) {
            if (!inst_inf_kv(mline, key, sizeof(key), value, sizeof(value))) {
                continue;
            }
            for (i = 1; inst_inf_field(value, i, field, sizeof(field)); i++) {
                inst_ids_add(ids, field);
            }
        }
    }
    return ids->Overflow ? 0 : 1;
}

int inst_inf_arch_ok(const char *text, int arch)
{
    char line[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    char field[INST_LINE_CHARS];
    const char *p = inst_inf_section(text, "Manufacturer");
    int lines = 0;
    int i;

    if (p == NULL) {
        return 0;
    }
    while ((p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        int amd64 = 0;
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value))) {
            return 0;
        }
        lines++;
        for (i = 1; inst_inf_field(value, i, field, sizeof(field)); i++) {
            int da;
            INST_U32 dmaj;
            INST_U32 dmin;
            /* The whole token, by the parser the selection uses: a
             * decoration it does not know is refused, not guessed. */
            if (!inst_decoration_parse(field, &da, &dmaj, &dmin)) {
                return 0;
            }
            if (da == INST_DECO_AMD64) {
                amd64 = 1;
                if (arch == INST_ARCH_X86) {
                    return 0;
                }
            } else if (arch == INST_ARCH_AMD64 || da != INST_DECO_X86) {
                return 0;
            }
        }
        if (!inst_inf_field(value, 0, field, sizeof(field))) {
            return 0;
        }
        if (arch == INST_ARCH_X86) {
            if (inst_inf_section(text, field) == NULL) {
                return 0;
            }
        } else {
            if (!amd64 || inst_inf_section(text, field) != NULL) {
                return 0;
            }
        }
    }
    return lines > 0;
}

int inst_inf_source_file(const char *text, int index, char *out, size_t cap)
{
    char line[INST_LINE_CHARS];
    char name[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    const char *p = text;
    int in = 0;
    int n = 0;

    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        if (inst_header(line, name, sizeof(name))) {
            in = inst_eq(name, "SourceDisksFiles") ||
                 inst_starts(name, "SourceDisksFiles.");
            continue;
        }
        if (!in || line[0] == '\0') {
            continue;
        }
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value))) {
            inst_copy(key, line, sizeof(key));
        }
        if (n++ == index) {
            inst_unquote(key);
            inst_copy(out, key, cap);
            return 1;
        }
    }
    return 0;
}

/* The [SourceDisksNames] path of disk id (field 4), or "". */
static void inst_disk_path(const char *text, const char *disk, char *out,
                           size_t cap)
{
    char line[INST_LINE_CHARS];
    char name[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    const char *p = text;
    int in = 0;

    out[0] = '\0';
    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        if (inst_header(line, name, sizeof(name))) {
            in = inst_eq(name, "SourceDisksNames") ||
                 inst_starts(name, "SourceDisksNames.");
            continue;
        }
        if (in && inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) &&
            inst_eq(key, disk)) {
            if (!inst_inf_field(value, 3, out, cap)) {
                out[0] = '\0';
            }
            return;
        }
    }
}

static int inst_path_join(char *out, size_t cap, const char *part)
{
    size_t n = strlen(out);

    while (*part == '\\' || *part == '/') {
        part++;
    }
    if (part[0] == '\0') {
        return 1;
    }
    if (n > 0 && out[n - 1] != '\\') {
        if (n + 1 >= cap) {
            return 0;
        }
        out[n++] = '\\';
        out[n] = '\0';
    }
    if (n + strlen(part) >= cap) {
        return 0;
    }
    strcat(out, part);
    return 1;
}

int inst_inf_source_path(const char *text, int index, char *out, size_t cap)
{
    char line[INST_LINE_CHARS];
    char name[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    char disk[INST_LINE_CHARS];
    char sub[INST_LINE_CHARS];
    char dpath[INST_LINE_CHARS];
    const char *p = text;
    int in = 0;
    int n = 0;
    size_t i;

    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        if (inst_header(line, name, sizeof(name))) {
            in = inst_eq(name, "SourceDisksFiles") ||
                 inst_starts(name, "SourceDisksFiles.");
            continue;
        }
        if (!in || line[0] == '\0') {
            continue;
        }
        if (n++ != index) {
            continue;
        }
        value[0] = '\0';
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value))) {
            inst_copy(key, line, sizeof(key));
        }
        inst_unquote(key);
        if (!inst_inf_field(value, 0, disk, sizeof(disk))) {
            disk[0] = '\0';
        }
        if (!inst_inf_field(value, 1, sub, sizeof(sub))) {
            sub[0] = '\0';
        }
        inst_disk_path(text, disk, dpath, sizeof(dpath));
        out[0] = '\0';
        if (key[0] == '\0' || !inst_path_join(out, cap, dpath) ||
            !inst_path_join(out, cap, sub) || !inst_path_join(out, cap, key)) {
            out[0] = '\0';
            return -1;
        }
        for (i = 0; out[i] != '\0'; i++) {
            if (out[i] == '/') {
                out[i] = '\\';
            }
        }
        if (strchr(out, ':') != NULL) {
            return -1;
        }
        return 1;
    }
    return 0;
}
int inst_inf_addreg_default(const char *text, const char *name,
                            INST_U32 *value)
{
    char line[INST_LINE_CHARS];
    char field[INST_LINE_CHARS];
    const char *p = text;

    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        if (!inst_inf_field(line, 0, field, sizeof(field)) ||
            !inst_eq(field, "HKR")) {
            continue;
        }
        if (!inst_inf_field(line, 2, field, sizeof(field)) ||
            !inst_eq(field, name)) {
            continue;
        }
        if (!inst_inf_field(line, 4, field, sizeof(field))) {
            return 0;
        }
        return inst_number(field, value);
    }
    return 0;
}

static int inst_own_file(const char *file)
{
    return inst_eq(file, "xhci98.sys") || inst_eq(file, "xhciuas.sys") ||
           inst_eq(file, "xhcicm.sys");
}

int inst_bundled_file(const char *name)
{
    return inst_starts(name, "stor98");
}

int inst_bundled_service(const char *service)
{
    return inst_eq(service, "stor98");
}

int inst_own_service(const char *service)
{
    return inst_eq(service, "xhci98") || inst_eq(service, "xhciuas") ||
           inst_eq(service, "xhcicm");
}

/* The fields of one INF line inst_inf_is_ours reads for the bundled
 * driver's name. */
#define INST_INF_SCAN_FIELDS 64

/* A field of the INF with every %token% in it expanded from [Strings] (a
 * token anywhere in the field, "%%" a percent sign, an unknown token left
 * as written) and its quotes removed: 1 when it names the bundled driver
 * (inst_bundled_file: its service and every file of it start with
 * "stor98"). */
static int inst_inf_bundled(const char *text, const char *field)
{
    char raw[INST_LINE_CHARS];
    char expanded[INST_LINE_CHARS];
    char token[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    const char *s;
    const char *end;
    size_t n;
    size_t len;

    inst_copy(raw, field, sizeof(raw));
    inst_unquote(raw);
    n = 0;
    s = raw;
    while (*s != '\0' && n + 1 < sizeof(expanded)) {
        end = (*s == '%') ? strchr(s + 1, '%') : NULL;
        if (end == NULL) {
            expanded[n++] = *s++;
            continue;
        }
        len = (size_t)(end - s) + 1;
        if (len == 2) {
            expanded[n++] = '%';
            s = end + 1;
            continue;
        }
        if (len >= sizeof(token)) {
            len = sizeof(token) - 1;
        }
        memcpy(token, s, len);
        token[len] = '\0';
        if (!inst_inf_expand(text, token, value, sizeof(value))) {
            inst_copy(value, token, sizeof(value));
        }
        inst_copy(expanded + n, value, sizeof(expanded) - n);
        n = strlen(expanded);
        s = end + 1;
    }
    expanded[n] = '\0';
    inst_unquote(expanded);
    return inst_bundled_file(expanded);
}

int inst_inf_is_ours(const char *text)
{
    char line[INST_LINE_CHARS];
    char key[INST_LINE_CHARS];
    char value[INST_LINE_CHARS];
    char field[INST_LINE_CHARS];
    char provider[INST_LINE_CHARS];
    const char *p = inst_inf_section(text, "Version");
    int providerOk = 0;
    int k;

    while ((p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        if (inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) &&
            inst_eq(key, "Provider")) {
            inst_unquote(value);
            if (inst_inf_expand(text, value, provider, sizeof(provider)) &&
                inst_eq(provider, "Yeo Kheng Meng")) {
                providerOk = 1;
            }
            break;
        }
    }
    if (!providerOk) {
        return 0;
    }
    /* The bundled Stor98 shares the Provider: an INF any field of which,
     * its %tokens% expanded, starts with "stor98" (its service, its files,
     * its strings) is never this package's, whatever directive or value
     * name carries it; no INF of this package has such a field. */
    p = text;
    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        for (k = 0; k < INST_INF_SCAN_FIELDS; k++) {
            if (!inst_inf_field(line, k, field, sizeof(field))) {
                break;
            }
            if (inst_inf_bundled(text, field)) {
                return 0;
            }
        }
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        for (k = 0; k < INST_INF_SCAN_FIELDS; k++) {
            if (!inst_inf_field(value, k, field, sizeof(field))) {
                break;
            }
            if (inst_inf_bundled(text, field)) {
                return 0;
            }
        }
    }
    p = text;
    while ((p = inst_raw_line(p, line, sizeof(line))) != NULL) {
        if (inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) &&
            inst_eq(key, "AddService") &&
            inst_inf_field(value, 0, field, sizeof(field)) &&
            inst_own_service(field)) {
            return 1;
        }
        if (inst_inf_field(line, 0, field, sizeof(field)) &&
            inst_eq(field, "HKR") &&
            inst_inf_field(line, 2, field, sizeof(field)) &&
            inst_eq(field, "NTMPDriver") &&
            inst_inf_field(line, 4, field, sizeof(field)) &&
            inst_own_file(field)) {
            return 1;
        }
    }
    return 0;
}

/* ======================================================================== */
/* Ids                                                                       */
/* ======================================================================== */

int inst_msz_has(const char *msz, size_t bytes, const char *id)
{
    size_t off = 0;

    if (msz == NULL) {
        return 0;
    }
    while (off < bytes && msz[off] != '\0') {
        size_t n = 0;
        while (off + n < bytes && msz[off + n] != '\0') {
            n++;
        }
        if (n == strlen(id) && inst_eqn(msz + off, id, n)) {
            return 1;
        }
        off += n + 1;
    }
    return 0;
}

size_t inst_ids_split(char *buf, size_t cap)
{
    size_t n;
    size_t i;

    if (cap < 2) {
        return 0;
    }
    buf[cap - 1] = '\0';
    buf[cap - 2] = '\0';
    n = strlen(buf);
    for (i = 0; i < n; i++) {
        if (buf[i] == ',') {
            buf[i] = '\0';
        }
    }
    if (n == 0) {
        return 0;
    }
    /* The string's own NUL, then a second one ends the multi-string. */
    if (n + 1 < cap) {
        buf[n + 1] = '\0';
    }
    return n + 2 <= cap ? n + 2 : cap;
}
int inst_ids_match(const char *hw, size_t hwBytes, const char *compat,
                   size_t compatBytes, const INST_IDLIST *ids)
{
    int i;

    for (i = 0; i < ids->Count; i++) {
        if (inst_msz_has(hw, hwBytes, ids->Id[i]) ||
            inst_msz_has(compat, compatBytes, ids->Id[i])) {
            return i;
        }
    }
    return -1;
}

static int inst_hex4(const char *s, INST_U32 *out)
{
    INST_U32 v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int c = inst_upper((unsigned char)s[i]);
        if (c >= '0' && c <= '9') {
            v = (v << 4) | (INST_U32)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v = (v << 4) | (INST_U32)(c - 'A' + 10);
        } else {
            return 0;
        }
    }
    *out = v;
    return 1;
}

int inst_pci_vendev(const char *id, INST_U32 *vendorDevice)
{
    INST_U32 ven;
    INST_U32 dev;

    if (strlen(id) < 21 || !inst_eqn(id, "PCI\\VEN_", 8) ||
        !inst_hex4(id + 8, &ven) || !inst_eqn(id + 12, "&DEV_", 5) ||
        !inst_hex4(id + 17, &dev)) {
        return 0;
    }
    if (id[21] != '\0' && id[21] != '&' && id[21] != '\\') {
        return 0;
    }
    *vendorDevice = (dev << 16) | ven;
    return 1;
}

/* The bus id's base, 0 none: 1 root hub, 2 hub, 3 hub30, 4 composite. */
static int inst_bus_base(const char *instanceId, int *old)
{
    const char *s;
    size_t n;

    *old = 0;
    if (inst_starts(instanceId, "USB\\XHCI98_")) {
        s = instanceId + 11;
    } else if (inst_starts(instanceId, "XHCI98\\")) {
        s = instanceId + 7;
        *old = 1;
    } else {
        return 0;
    }
    n = 0;
    while (s[n] != '\0' && s[n] != '\\' && s[n] != '&') {
        n++;
    }
    if (n == 8 && inst_eqn(s, "ROOT_HUB", 8)) {
        return 1;
    }
    if (n == 3 && inst_eqn(s, "HUB", 3)) {
        return 2;
    }
    if (n == 5 && inst_eqn(s, "HUB30", 5)) {
        return 3;
    }
    if (n == 9 && inst_eqn(s, "COMPOSITE", 9)) {
        return 4;
    }
    return 0;
}

int inst_bus_id(const char *instanceId)
{
    int old;

    if (inst_bus_base(instanceId, &old) == 0) {
        return 0;
    }
    return old ? 2 : 1;
}

/* ======================================================================== */
/* The action per device                                                     */
/* ======================================================================== */

static int inst_versioned(const INST_DEV *dev, const INST_U32 pkg[4])
{
    int c;

    if (dev->VersionState == INST_VS_UNREADABLE) {
        return INST_ACT_UNKNOWN;
    }
    if (dev->VersionState != INST_VS_HAVE) {
        return INST_ACT_UPDATE;
    }
    c = inst_ver_cmp(dev->Version, pkg);
    if (c < 0) {
        return INST_ACT_UPDATE;
    }
    return c == 0 ? INST_ACT_CURRENT : INST_ACT_NEWER;
}

int inst_action(const INST_DEV *dev, const INST_U32 pkg[4])
{
    int c;

    if (dev->FilterInf) {
        if (dev->FilterPresent) {
            return inst_versioned(dev, pkg);
        }
        if (!dev->HasService) {
            return INST_ACT_INSTALL;
        }
        return dev->ServiceIsPci ? INST_ACT_FILTER : INST_ACT_FOREIGN;
    }
    /* Ownership first: a problem code never turns another vendor's device
     * into an install (decision D3). */
    if (!dev->HasService) {
        return INST_ACT_INSTALL;
    }
    if (!dev->OwnService) {
        return INST_ACT_FOREIGN;
    }
    c = inst_versioned(dev, pkg);
    if (c == INST_ACT_CURRENT && (dev->Problem == 1UL ||
                                  dev->Problem == 18UL ||
                                  dev->Problem == 28UL)) {
        return INST_ACT_UPDATE;
    }
    return c;
}

int inst_group_verdict(int anyNewer, int anyUnknown, int binary, int force)
{
    if (anyUnknown || binary == 2) {
        return INST_GROUP_REFUSED_UNKNOWN;
    }
    if ((anyNewer || binary == 1) && !force) {
        return INST_GROUP_REFUSED_NEWER;
    }
    return INST_GROUP_OK;
}

int inst_uas_fix_9x(int nt, INST_U32 minor, int isUas, INST_U32 problem,
                    int ntmapPresent)
{
    if (nt || !isUas || problem != 2UL || ntmapPresent) {
        return INST_UASFIX_NONE;
    }
    return minor == 90UL ? INST_UASFIX_ME_STICK : INST_UASFIX_NUSB;
}

int inst_flavour_marker(const unsigned char *buf, size_t len, char *out,
                        size_t cap)
{
    static const char tag[] = "XHCI98_FLAVOUR_";
    size_t tl = sizeof(tag) - 1;
    size_t i;
    int count = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    for (i = 0; i + tl < len; i++) {
        size_t n = 0;
        if (memcmp(buf + i, tag, tl) != 0) {
            continue;
        }
        while (i + tl + n < len && buf[i + tl + n] >= 'A' &&
               buf[i + tl + n] <= 'Z') {
            n++;
        }
        if (n == 0) {
            continue;
        }
        if (count == 0 && n < cap) {
            memcpy(out, buf + i + tl, n);
            out[n] = '\0';
        }
        count++;
        i += tl + n - 1;
    }
    return count;
}

INST_U32 inst_fnv1a(INST_U32 h, const unsigned char *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= (INST_U32)buf[i];
        h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    return h;
}

int inst_action_runs(int action, int force)
{
    switch (action) {
    case INST_ACT_INSTALL:
    case INST_ACT_UPDATE:
    case INST_ACT_FILTER:
        return 1;
    case INST_ACT_CURRENT:
    case INST_ACT_NEWER:
        return force ? 1 : 0;
    default:
        return 0;
    }
}

const char *inst_action_text(int action)
{
    switch (action) {
    case INST_ACT_INSTALL: return "install (no driver)";
    case INST_ACT_UPDATE:  return "update (an older release)";
    case INST_ACT_CURRENT: return "current (this release)";
    case INST_ACT_NEWER:   return "newer release installed: left (/force goes back)";
    case INST_ACT_FOREIGN: return "another driver: left";
    case INST_ACT_FILTER:  return "add the tunnel-port filter";
    case INST_ACT_UNKNOWN: return "version unreadable: left";
    default:               return "?";
    }
}

/* ======================================================================== */
/* Small helpers                                                             */
/* ======================================================================== */

int inst_pe_machine(const unsigned char *buf, size_t len, INST_U32 *machine)
{
    INST_U32 off;

    if (len < 0x40 || buf[0] != 'M' || buf[1] != 'Z') {
        return 0;
    }
    off = (INST_U32)buf[0x3C] | ((INST_U32)buf[0x3D] << 8) |
          ((INST_U32)buf[0x3E] << 16) | ((INST_U32)buf[0x3F] << 24);
    if (off > len || len - off < 6) {
        return 0;
    }
    if (buf[off] != 'P' || buf[off + 1] != 'E' || buf[off + 2] != 0 ||
        buf[off + 3] != 0) {
        return 0;
    }
    *machine = (INST_U32)buf[off + 4] | ((INST_U32)buf[off + 5] << 8);
    return 1;
}

static INST_U32 inst_le32(const unsigned char *p)
{
    return (INST_U32)p[0] | ((INST_U32)p[1] << 8) | ((INST_U32)p[2] << 16) |
           ((INST_U32)p[3] << 24);
}

int inst_pe_signed(const unsigned char *buf, size_t len)
{
    INST_U32 off;
    INST_U32 opt;
    INST_U32 dirs;
    INST_U32 count;
    INST_U32 optSize;
    INST_U32 certOff;
    INST_U32 certSize;
    unsigned magic;

    if (len < 0x40 || buf[0] != 'M' || buf[1] != 'Z') {
        return -1;
    }
    off = inst_le32(buf + 0x3C);
    if (off > len || len - off < 24 || buf[off] != 'P' || buf[off + 1] != 'E' ||
        buf[off + 2] != 0 || buf[off + 3] != 0) {
        return -1;
    }
    optSize = (INST_U32)buf[off + 20] | ((INST_U32)buf[off + 21] << 8);
    opt = off + 24;
    if (len - opt < 2) {
        return -1;
    }
    magic = (unsigned)buf[opt] | ((unsigned)buf[opt + 1] << 8);
    if (magic == 0x10BU) {
        dirs = 96;
    } else if (magic == 0x20BU) {
        dirs = 112;
    } else {
        return -1;
    }
    /* NumberOfRvaAndSizes sits just before the directories; directory 4
     * must lie inside both the optional header and the buffer. */
    if (optSize < dirs + 5 * 8 || len - opt < optSize) {
        return -1;
    }
    dirs += opt;
    count = inst_le32(buf + dirs - 4);
    if (count < 5) {
        return 0;
    }
    /* Directory 4, IMAGE_DIRECTORY_ENTRY_SECURITY: a FILE offset and a
     * size. An embedded signature has a size, and its table lies inside
     * the file; one that points past the end is no signature to rely on. */
    certOff = inst_le32(buf + dirs + 4 * 8);
    certSize = inst_le32(buf + dirs + 4 * 8 + 4);
    if (certSize == 0) {
        return 0;
    }
    if (certOff == 0 || certOff > len || certSize > len - certOff) {
        return -1;
    }
    return 1;
}

/* The next ';' entry of list from *pos, trimmed, into out. 0 at the end. */
static int inst_path_entry(const char *list, size_t *pos, char *out,
                           size_t cap)
{
    size_t n = 0;

    if (list[*pos] == '\0') {
        return 0;
    }
    while (list[*pos] != '\0' && list[*pos] != ';') {
        if (n + 1 < cap) {
            out[n++] = list[*pos];
        }
        (*pos)++;
    }
    if (list[*pos] == ';') {
        (*pos)++;
    }
    out[n] = '\0';
    inst_trim(out);
    return 1;
}

int inst_path_append(const char *list, const char *dir, char *out, size_t cap)
{
    char entry[INST_LINE_CHARS];
    size_t pos = 0;
    size_t n = strlen(list);
    int sep;

    while (inst_path_entry(list, &pos, entry, sizeof(entry))) {
        if (inst_eq(entry, dir)) {
            if (n + 1 > cap) {
                return -1;
            }
            strcpy(out, list);
            return 0;
        }
    }
    sep = (n > 0 && list[n - 1] != ';') ? 1 : 0;
    if (n + (size_t)sep + strlen(dir) + 1 > cap) {
        return -1;
    }
    strcpy(out, list);
    if (sep) {
        strcat(out, ";");
    }
    strcat(out, dir);
    return 1;
}

int inst_path_remove(const char *list, const char *dir, char *out, size_t cap)
{
    char entry[INST_LINE_CHARS];
    size_t pos = 0;
    size_t used = 0;
    int removed = 0;

    if (cap == 0) {
        return -1;
    }
    out[0] = '\0';
    while (inst_path_entry(list, &pos, entry, sizeof(entry))) {
        if (inst_eq(entry, dir)) {
            removed = 1;
            continue;
        }
        if (entry[0] == '\0') {
            continue;
        }
        if (used + (used ? 1 : 0) + strlen(entry) + 1 > cap) {
            return -1;
        }
        if (used) {
            strcat(out, ";");
            used++;
        }
        strcat(out, entry);
        used += strlen(entry);
    }
    return removed;
}

int inst_copy_name(const char *name)
{
    size_t i;
    size_t n = strlen(name);

    if (n == 12 && inst_eq(name + 8, ".NEW")) {
        n = 8;
    }
    if (n != 8) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        int c = inst_upper((unsigned char)name[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

int inst_path_remove_copies(const char *list, const char *root, char *out,
                            size_t cap)
{
    char entry[INST_LINE_CHARS];
    size_t pos = 0;
    size_t used = 0;
    size_t r = strlen(root);
    int removed = 0;

    while (r > 0 && root[r - 1] == '\\') {
        r--;
    }
    if (cap == 0) {
        return -1;
    }
    out[0] = '\0';
    while (inst_path_entry(list, &pos, entry, sizeof(entry))) {
        if (r > 0 && strlen(entry) > r + 1 && inst_eqn(entry, root, r) &&
            entry[r] == '\\' && inst_copy_name(entry + r + 1)) {
            removed = 1;
            continue;
        }
        if (entry[0] == '\0') {
            continue;
        }
        if (used + (used ? 1 : 0) + strlen(entry) + 1 > cap) {
            return -1;
        }
        if (used) {
            strcat(out, ";");
            used++;
        }
        strcat(out, entry);
        used += strlen(entry);
    }
    return removed;
}

int inst_path_remove_prefix(const char *list, const char *prefix, char *out,
                            size_t cap)
{
    char entry[INST_LINE_CHARS];
    size_t pos = 0;
    size_t used = 0;
    int removed = 0;

    if (cap == 0) {
        return -1;
    }
    out[0] = '\0';
    while (inst_path_entry(list, &pos, entry, sizeof(entry))) {
        if (inst_starts(entry, prefix)) {
            removed = 1;
            continue;
        }
        if (entry[0] == '\0') {
            continue;
        }
        if (used + (used ? 1 : 0) + strlen(entry) + 1 > cap) {
            return -1;
        }
        if (used) {
            strcat(out, ";");
            used++;
        }
        strcat(out, entry);
        used += strlen(entry);
    }
    return removed;
}
size_t inst_msz_remove(const char *in, size_t inBytes, const char *name,
                       char *out, size_t cap, int *removed)
{
    size_t off = 0;
    size_t used = 0;

    *removed = 0;
    while (off < inBytes && in[off] != '\0') {
        size_t n = 0;
        while (off + n < inBytes && in[off + n] != '\0') {
            n++;
        }
        if (n == strlen(name) && inst_eqn(in + off, name, n)) {
            *removed = 1;
        } else {
            if (used + n + 2 > cap) {
                return (size_t)-1;
            }
            memcpy(out + used, in + off, n);
            used += n;
            out[used++] = '\0';
        }
        off += n + 1;
    }
    if (used == 0) {
        return 0;
    }
    out[used++] = '\0';
    return used;
}

int inst_filter_drop(const char *in, size_t inBytes, const char *name,
                     char *out, size_t cap, size_t *outBytes)
{
    size_t n;
    int removed;

    *outBytes = 0;
    if (in == NULL || !inst_msz_has(in, inBytes, name)) {
        return INST_FD_NONE;
    }
    n = inst_msz_remove(in, inBytes, name, out, cap, &removed);
    if (n == (size_t)-1) {
        return INST_FD_ERROR;
    }
    if (n == 0) {
        return INST_FD_DELETE;
    }
    *outBytes = n;
    return INST_FD_SET;
}

int inst_testsigning(const char *options)
{
    const char *s = options;

    if (s == NULL) {
        return 0;
    }
    while (*s != '\0') {
        size_t n = 0;
        while (*s == ' ' || *s == '/' || *s == '\t') {
            s++;
        }
        while (s[n] != '\0' && s[n] != ' ' && s[n] != '/' && s[n] != '\t') {
            n++;
        }
        if (n == 11 && inst_eqn(s, "TESTSIGNING", 11)) {
            return 1;
        }
        s += n;
    }
    return 0;
}

/* ======================================================================== */
/* Registry values                                                           */
/* ======================================================================== */

const INST_VALUE instValues[] = {
    /* The meanings and choices are the release notes' and
     * docs\using\tuning.md's; the question prints each choice on its own
     * line (owner, 2026-10-16), and the console wraps the cautions. */
    { "XhciIntelPortSwitch", INST_INF_XHCI, INST_APPLY_PSW, 0x3UL, 0x7UL,
      "route the shared connectors from the EHCI controller to this driver",
      "2 forces the switch writes on an Intel controller that does not "
      "have the switch registers, at every start, resume, stop and "
      "shutdown; use it only if the release notes' \"Value 2\" section "
      "applies to your machine.", 0x4UL,
      { "off", "on", "on for any Intel controller, at your own risk",
        NULL } },
    { "XhciInterruptMode", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0xFUL, 0xFUL,
      "how the controller's events arrive",
      "with %s, a controller Windows gives no such interrupt does not "
      "start (Code 10).", 0xAUL,
      { "automatic", "the interrupt line only", "polling always",
        "a message-signalled interrupt only" } },
    /* Owner, 2026-10-15: never asked (Asked 0); /set Name=Value only. */
    { "XhciPollFineTimer", INST_INF_XHCI, INST_APPLY_NT_XHCI, 0x0UL, 0x3UL,
      "a finer system clock while the controller is polled",
      NULL, 0, { "off", "on", NULL, NULL } },
    { "XhciSelectiveSuspend", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x0UL,
      0x3UL, "put idle USB devices to sleep",
      "leave it off unless a device needs it; if a device stops responding "
      "after idling, set it back to 0.", 0x2UL,
      { "off", "on", NULL, NULL } },
    /* Owner, 2026-10-15: asked on 98 SE and ME only, default off; on is
     * tools\silent\silent-on.bat's effect (the SILENT.REG list, then the
     * switch), off silent-off.bat's (the switch, the list left). */
    { "XhciSilentInstall", INST_INF_XHCI, INST_APPLY_9X_XHCI, 0x3UL, 0x3UL,
      "install hubs, boot keyboards and mice and Bulk-Only storage with no "
      "Add New Hardware wizard",
      "turn it on only once each listed class's drivers have been installed "
      "at least once: otherwise 98 SE asks for its CD, with no wizard. "
      "Restart Windows after changing it.", 0,
      { "off", "on", NULL, NULL } },
    { "XhciForceBulkOnly", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3UL, 0x3UL,
      "Bulk-Only instead of UAS for a disk that offers both",
      "read as each device is connected; on Windows 2000 and later a disk "
      "already installed keeps its driver until it is uninstalled in "
      "Device Manager and plugged in again. A UAS-only disk stays on "
      "UAS.", 0,
      { "UAS where the disk offers it", "Bulk-Only", NULL, NULL } },
    /* Owner, 2026-10-19: never asked, the USB page's "Advanced..." row
     * and /set; 1 is not a mode (docs\using\tuning.md). */
    { "XhciFastPollFsLs", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x0UL,
      0x1DUL, "poll a mouse faster than 1000 Hz, with SweetLow's hidusbf",
      "this is outside the xHCI specification for Low- and Full-Speed "
      "devices. It is read when the controller starts: restart Windows "
      "after changing it. With 4, set the value and restart before "
      "setting the filter's code.", 0,
      { "off", NULL,
        "a root-port mouse set to 31/62 Hz in hidusbf runs at 2000/4000 Hz",
        "a root-port mouse set to 31/62 Hz in hidusbf runs at 4000/8000 Hz",
        "the rate the client chooses, codes 128 to 143" } },
    { "XhciCmMode", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0xFUL,
      "the Thunderbolt and USB4 connection manager",
      "2 and 3 force one connection manager whatever the controller "
      "reports; use them only on the release notes' advice.", 0xCUL,
      { "off", "automatic",
        "an unlisted Intel controller driven as an Alpine Ridge",
        "the driver's own USB4 connection manager, forced" } },
    { "XhciCmApprove", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0x3UL,
      "approve Thunderbolt devices automatically", NULL, 0,
      { "no", "yes", NULL, NULL } },
    { "XhciCmSafeEject", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0x3UL,
      "offer Safely Remove for the devices behind a dock",
      "no also leaves out the safe-removal mark service, and removes it "
      "with its marks where it is installed.", 0x1UL,
      { "no", "yes", NULL, NULL } },
    /* Owner, 2026-10-15: never asked, /set Name=Value only (record 23
     * section 8.1, record 24 section 11). */
    { "XhciCmReenum", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "make Windows look for devices after each Thunderbolt or USB4 "
      "change",
      "0 is for diagnosing a machine where that misbehaves: devices then "
      "appear only after a Device Manager refresh.", 0x1UL,
      { "no", "yes", NULL, NULL } },
    { "XhciCmUsb3Tunnel", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "USB4: carry a device's USB over a USB3 connection",
      NULL, 0, { "no", "yes", NULL, NULL } },
    { "XhciCmLaneBonding", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "USB4: bond the two lanes of a link", NULL, 0,
      { "no", "yes", NULL, NULL } },
    { "XhciCmPreOsCleanup", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x7UL,
      "USB4: connections found at start",
      "1 and 2 may drop a device the firmware connected before Windows "
      "started.", 0x6UL,
      { "kept", "cleaned where they do not verify", "all cleaned", NULL } },
    /* Record 26 section 5.5 (task 42-A.2): the Thunderbolt page's
     * "Advanced..." rows from the release notes' table, never asked by
     * the console; the ranges are cm_vals.c's. XhciCmBridgeNumbering is
     * not written by the INF: its default is the driver's. */
    { "XhciCmPollIdleMs", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x0UL,
      "how often, in ms, the driver looks at the controller with nothing "
      "happening", NULL, 0, { NULL, NULL, NULL, NULL }, 50UL, 5000UL, 0, 0 },
    { "XhciCmCmdTimeoutMs", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x0UL,
      "firmware connection manager: the wait for a command's answer, in ms",
      NULL, 0, { NULL, NULL, NULL, NULL }, 1000UL, 60000UL, 0, 0 },
    { "XhciCmApproveTimeoutMs", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x0UL,
      "firmware connection manager: the wait for an approval's answer, in ms",
      NULL, 0, { NULL, NULL, NULL, NULL }, 1000UL, 60000UL, 0, 0 },
    { "XhciCmLogVerbosity", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x1FUL,
      "the connection manager's log",
      "4 adds memory addresses to the log: for a developer's report.", 0x10UL,
      { "off", "counters only", "the log", "adds port status" } },
    { "XhciCmLogDebugView", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "the connection manager's log also to a debugger or DebugView", NULL, 0,
      { "no", "yes", NULL, NULL } },
    { "XhciCmBridgeNumbering", INST_INF_CM, INST_APPLY_9X_CM, 0x0UL, 0x3UL,
      "set up the bridges a device brings up behind a tunnel",
      "0 leaves them as they come up: a device behind a switch may then "
      "show Code 29 until a restart.", 0x1UL,
      { "leave them as they come up", "set them up", NULL, NULL },
      0, 0, 1, 1UL }
};
const int instValueCount = (int)(sizeof(instValues) / sizeof(instValues[0]));

static int inst_hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int inst_reg_hex_line(const char *line, char *name, size_t nameCap,
                      unsigned char *data, size_t dataCap, size_t *n)
{
    const char *q;
    size_t len;

    *n = 0;
    if (line[0] != '"') {
        return 0;
    }
    q = strchr(line + 1, '"');
    if (q == NULL) {
        return 0;
    }
    len = (size_t)(q - line - 1);
    if (len == 0 || len >= nameCap || strncmp(q, "\"=hex:", 6) != 0) {
        return 0;
    }
    memcpy(name, line + 1, len);
    name[len] = '\0';
    /* Exactly two hex digits per byte, a comma between bytes, none after
     * the last. */
    for (q += 6;;) {
        int hi = inst_hexval(q[0]);
        int lo = hi < 0 ? -1 : inst_hexval(q[1]);
        if (hi < 0 || lo < 0 || *n >= dataCap) {
            return 0;
        }
        data[(*n)++] = (unsigned char)((hi << 4) | lo);
        q += 2;
        if (*q == '\0') {
            return 1;
        }
        if (*q != ',' || q[1] == '\0') {
            return 0;
        }
        q++;
    }
}

int inst_value_find(const char *name)
{
    int i;

    for (i = 0; i < instValueCount; i++) {
        if (inst_eq(instValues[i].Name, name)) {
            return i;
        }
    }
    return -1;
}

int inst_value_applies(int index, const INST_ENV *env)
{
    if (index < 0 || index >= instValueCount) {
        return 0;
    }
    if (instValues[index].Asked == 0) {
        return 0;   /* settable with /set only, never asked */
    }
    switch (instValues[index].Apply) {
    case INST_APPLY_ANY_XHCI:
        return env->AnyXhci ? 1 : 0;
    case INST_APPLY_PSW:
        return env->PswController ? 1 : 0;
    case INST_APPLY_NT_XHCI:
        return (env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_CM:
        return env->CmNhi ? 1 : 0;
    case INST_APPLY_9X_XHCI:
        return (!env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_9X_CM:
        return (!env->Nt && env->CmNhi) ? 1 : 0;
    default:
        return 0;
    }
}

int inst_value_relevant(int index, const INST_ENV *env)
{
    switch (index >= 0 && index < instValueCount ? instValues[index].Apply
                                                 : -1) {
    case INST_APPLY_ANY_XHCI:
        return env->AnyXhci ? 1 : 0;
    case INST_APPLY_PSW:
        return env->PswController ? 1 : 0;
    case INST_APPLY_NT_XHCI:
        return (env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_CM:
        return env->CmNhi ? 1 : 0;
    case INST_APPLY_9X_XHCI:
        return (!env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_9X_CM:
        return (!env->Nt && env->CmNhi) ? 1 : 0;
    default:
        return 0;
    }
}

int inst_value_greyed(int index, const INST_ENV *env)
{
    switch (index >= 0 && index < instValueCount ? instValues[index].Apply
                                                 : -1) {
    case INST_APPLY_PSW:
        return (env->AnyXhci && !env->PswController) ? 1 : 0;
    case INST_APPLY_NT_XHCI:
        return (!env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_9X_XHCI:
        return (env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_9X_CM:
        /* The Thunderbolt rows are shown with no host interface present
         * too (record 26 section 5.5), so this one is as well. */
        return env->Nt ? 1 : 0;
    default:
        return 0;
    }
}

const char *inst_value_only(int index)
{
    switch (index >= 0 && index < instValueCount ? instValues[index].Apply
                                                 : -1) {
    case INST_APPLY_PSW:
        return "Intel 7-9 series, C610 only";
    case INST_APPLY_NT_XHCI:
        return "Windows 2000 to 7 only";
    case INST_APPLY_9X_XHCI:
    case INST_APPLY_9X_CM:
        return "Windows 98 SE and ME only";
    default:
        return NULL;
    }
}

int inst_value_default(int index, const char *infText, INST_U32 *def)
{
    if (index < 0 || index >= instValueCount) {
        return 0;
    }
    if (instValues[index].NoInf) {
        *def = instValues[index].Def;
        return 1;
    }
    return infText != NULL &&
           inst_inf_addreg_default(infText, instValues[index].Name, def);
}

int inst_value_allowed(int index, const INST_ENV *env, INST_U32 v)
{
    if (index < 0 || index >= instValueCount) {
        return 0;
    }
    if (instValues[index].Hi != 0) {
        return v >= instValues[index].Lo && v <= instValues[index].Hi;
    }
    return v <= 31UL && (inst_value_set_mask(index, env) & (1UL << v)) != 0;
}

INST_U32 inst_value_asked_mask(int index, const INST_ENV *env)
{
    INST_U32 mask;

    if (index < 0 || index >= instValueCount) {
        return 0;
    }
    mask = instValues[index].Asked;
    if (inst_eq(instValues[index].Name, "XhciInterruptMode") &&
        !(env->Nt && env->Major >= 6)) {
        mask &= ~0x8UL;
    }
    return mask;
}

INST_U32 inst_value_set_mask(int index, const INST_ENV *env)
{
    INST_U32 mask;

    if (index < 0 || index >= instValueCount) {
        return 0;
    }
    mask = instValues[index].Settable;
    if ((instValues[index].Apply == INST_APPLY_9X_XHCI ||
         instValues[index].Apply == INST_APPLY_9X_CM) && env->Nt) {
        return 0;   /* a 98 SE and ME value: /set refuses it on NT */
    }
    if (inst_eq(instValues[index].Name, "XhciInterruptMode") &&
        !(env->Nt && env->Major >= 6)) {
        mask &= ~0x8UL;
    }
    return mask;
}

int inst_value_answer(const char *line, INST_U32 mask, INST_U32 *value)
{
    char buf[32];
    INST_U32 v;

    inst_copy(buf, line, sizeof(buf));
    inst_trim(buf);
    if (buf[0] == '\0') {
        return 0;
    }
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        return -1;
    }
    if (!inst_number(buf, &v) || v > 31UL || (mask & (1UL << v)) == 0) {
        return -1;
    }
    *value = v;
    return 1;
}

int inst_value_set(const char *arg, int *index, INST_U32 *value)
{
    char name[INST_NAME_CHARS];
    const char *sep;
    size_t n;
    int i;
    INST_U32 v;

    sep = strchr(arg, '=');
    if (sep == NULL) {
        sep = strchr(arg, ':');
    }
    if (sep == NULL || sep == arg) {
        return -2;
    }
    n = (size_t)(sep - arg);
    if (n >= sizeof(name)) {
        return 0;
    }
    memcpy(name, arg, n);
    name[n] = '\0';
    i = inst_value_find(name);
    if (i < 0) {
        return 0;
    }
    if (sep[1] == '0' && (sep[2] == 'x' || sep[2] == 'X')) {
        return -1;
    }
    if (!inst_number(sep + 1, &v) ||
        (instValues[i].Hi != 0 ? (v < instValues[i].Lo || v > instValues[i].Hi)
                               : (v > 31UL ||
                                  (instValues[i].Settable & (1UL << v)) == 0))) {
        return -1;
    }
    *index = i;
    *value = v;
    return 1;
}

void inst_mask_text(INST_U32 mask, char *out, size_t cap)
{
    INST_U32 v;
    size_t used = 0;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    for (v = 0; v < 32UL; v++) {
        char num[8];
        size_t k = 0;
        INST_U32 t = v;
        char rev[8];
        size_t r = 0;
        if ((mask & (1UL << v)) == 0) {
            continue;
        }
        do {
            rev[r++] = (char)('0' + (int)(t % 10UL));
            t /= 10UL;
        } while (t != 0 && r < sizeof(rev));
        if (used != 0) {
            num[k++] = ',';
            num[k++] = ' ';
        }
        while (r > 0) {
            num[k++] = rev[--r];
        }
        num[k] = '\0';
        if (used + k + 1 > cap) {
            return;
        }
        strcat(out, num);
        used += k;
    }
}

/* ======================================================================== */
/* Switches and exit codes                                                   */
/* ======================================================================== */

static int inst_switch(const char *arg, const char *name)
{
    if (arg[0] != '/' && arg[0] != '-') {
        return 0;
    }
    return inst_eq(arg + 1, name);
}

static void inst_err(char *err, size_t cap, const char *a, const char *b)
{
    inst_copy(err, a, cap);
    if (b != NULL && strlen(err) + strlen(b) + 1 < cap) {
        strcat(err, b);
    }
}

int inst_parse_args(int argc, char **argv, INST_OPTS *opts, char *err,
                    size_t errCap)
{
    int i;
    int r;

    memset(opts, 0, sizeof(*opts));
    err[0] = '\0';
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (inst_switch(a, "list")) {
            opts->List = 1;
        } else if (inst_switch(a, "yes")) {
            opts->Yes = 1;
        } else if (inst_switch(a, "defaults")) {
            opts->Defaults = 1;
        } else if (inst_switch(a, "force")) {
            opts->Force = 1;
        } else if (inst_switch(a, "restage")) {
            opts->Restage = 1;
        } else if (inst_switch(a, "uninstall")) {
            opts->Uninstall = 1;
        } else if (inst_switch(a, "nopause")) {
            opts->NoPause = 1;
        } else if (inst_switch(a, "debug")) {
            /* INSTALL.BAT chose the directory for the console tool; the
             * window, which takes no /dir, picks it itself. */
            opts->Debug = 1;
        } else if (inst_switch(a, "console")) {
            /* INSTALL.BAT's word for the console tool (record 26 section
             * 2): the console takes it and goes on; the window refuses it. */
            opts->Console = 1;
        } else if (inst_switch(a, "selftest")) {
            opts->SelfTest = 1;
        } else if (inst_switch(a, "thunderbolt")) {
            opts->Thunderbolt = 1;
        } else if (inst_switch(a, "cmonly")) {
            opts->CmOnly = 1;
        } else if (inst_switch(a, "stor98")) {
            opts->Stor98 = 1;
        } else if (inst_switch(a, "nostor98")) {
            opts->NoStor98 = 1;
        } else if (inst_switch(a, "nouas")) {
            opts->NoUas = 1;
        } else if (inst_switch(a, "notray")) {
            opts->NoTray = 1;
        } else if (inst_switch(a, "devices")) {
            opts->Devices = 1;
        } else if (inst_switch(a, "eject")) {
            const char *v;
            if (opts->Eject) {
                inst_err(err, errCap, "/eject given twice", NULL);
                return 1;
            }
            opts->Eject = 1;
            if (i + 1 < argc && argv[i + 1][0] != '/' && argv[i + 1][0] != '-') {
                i++;
                v = argv[i];
                if (v[0] >= '0' && v[0] <= '9') {
                    INST_U32 n = 0;
                    const char *q = v;
                    while (*q >= '0' && *q <= '9' && n < 100000UL) {
                        n = n * 10UL + (INST_U32)(*q - '0');
                        q++;
                    }
                    if (*q != '\0' || n == 0 || n >= 100000UL) {
                        inst_err(err, errCap, "/eject: not a device number: ", v);
                        return 1;
                    }
                    opts->EjectNum = n;
                } else if (inst_eq(v, "id") && i + 1 < argc) {
                    /* COMMAND.COM splits id=... at '=': take the next. */
                    i++;
                    if (argv[i][0] == '\0' ||
                        strlen(argv[i]) >= sizeof(opts->EjectId)) {
                        inst_err(err, errCap, "/eject: id= needs an instance id", NULL);
                        return 1;
                    }
                    inst_copy(opts->EjectId, argv[i], sizeof(opts->EjectId));
                } else if (inst_eqn(v, "id=", 3)) {
                    if (v[3] == '\0' || strlen(v + 3) >= sizeof(opts->EjectId)) {
                        inst_err(err, errCap, "/eject: id= needs an instance id", NULL);
                        return 1;
                    }
                    inst_copy(opts->EjectId, v + 3, sizeof(opts->EjectId));
                } else {
                    inst_err(err, errCap, "/eject takes a number or id=<instance id>: ", v);
                    return 1;
                }
            }
        } else if (inst_switch(a, "from")) {
            if (i + 1 >= argc || opts->From[0] != '\0' ||
                argv[i + 1][0] == '\0' ||
                strlen(argv[i + 1]) >= sizeof(opts->From)) {
                inst_err(err, errCap, "/from needs one directory", NULL);
                return 1;
            }
            i++;
            inst_copy(opts->From, argv[i], sizeof(opts->From));
        } else if (inst_switch(a, "dir") || inst_switch(a, "log") ||
                   inst_switch(a, "to")) {
            int isDir = inst_switch(a, "dir");
            if (i + 1 >= argc) {
                inst_err(err, errCap, a, " needs a value");
                return 1;
            }
            i++;
            if (inst_switch(a, "to")) {
                if (opts->To[0] != '\0' || argv[i][0] == '\0' ||
                    strlen(argv[i]) >= sizeof(opts->To)) {
                    inst_err(err, errCap, "/to given twice, empty or too long",
                             NULL);
                    return 1;
                }
                inst_copy(opts->To, argv[i], sizeof(opts->To));
            } else if (isDir) {
                if (opts->Dir[0] != '\0' || strlen(argv[i]) >= sizeof(opts->Dir)) {
                    inst_err(err, errCap, "/dir given twice or too long", NULL);
                    return 1;
                }
                inst_copy(opts->Dir, argv[i], sizeof(opts->Dir));
            } else {
                if (opts->Log[0] != '\0' || strlen(argv[i]) >= sizeof(opts->Log)) {
                    inst_err(err, errCap, "/log given twice or too long", NULL);
                    return 1;
                }
                inst_copy(opts->Log, argv[i], sizeof(opts->Log));
            }
        } else if (inst_switch(a, "set")) {
            char joined[INST_LINE_CHARS];
            int index;
            INST_U32 value;
            int k;
            if (i + 1 >= argc) {
                inst_err(err, errCap, "/set needs Name=Value", NULL);
                return 1;
            }
            i++;
            /* A batch file splits Name=Value at '=' into two arguments, and
             * COMMAND.COM has no %* to keep them whole: take the next two. */
            if (strchr(argv[i], '=') == NULL && strchr(argv[i], ':') == NULL) {
                if (i + 1 >= argc || strlen(argv[i]) + strlen(argv[i + 1]) + 2 >
                    sizeof(joined)) {
                    inst_err(err, errCap, "/set needs Name=Value: ", argv[i]);
                    return 1;
                }
                strcpy(joined, argv[i]);
                strcat(joined, "=");
                strcat(joined, argv[i + 1]);
                i++;
            } else {
                inst_copy(joined, argv[i], sizeof(joined));
            }
            r = inst_value_set(joined, &index, &value);
            if (r == 0) {
                inst_err(err, errCap, "/set: not a value this tool sets: ", joined);
                return 1;
            }
            if (r < 0) {
                inst_err(err, errCap, "/set: not an allowed value: ", joined);
                return 1;
            }
            for (k = 0; k < opts->SetCount; k++) {
                if (opts->SetIndex[k] == index) {
                    inst_err(err, errCap, "/set names a value twice: ", joined);
                    return 1;
                }
            }
            if (opts->SetCount >= INST_MAX_SET) {
                inst_err(err, errCap, "/set given too many times", NULL);
                return 1;
            }
            opts->SetIndex[opts->SetCount] = index;
            opts->SetValue[opts->SetCount] = value;
            opts->SetCount++;
        } else {
            inst_err(err, errCap, "unknown argument: ", a);
            return 1;
        }
    }
    if (opts->SelfTest && argc != 2) {
        inst_err(err, errCap, "/selftest takes no other argument", NULL);
        return 1;
    }
    if (opts->List && (opts->Yes || opts->Restage)) {
        inst_err(err, errCap, "/list changes nothing: it takes no /yes "
                 "or /restage", NULL);
        return 1;
    }
    if (opts->Defaults && opts->SetCount != 0) {
        inst_err(err, errCap, "/defaults and /set contradict each other", NULL);
        return 1;
    }
    if ((opts->Devices || opts->Eject) &&
        (opts->List || opts->Defaults || opts->SetCount != 0 ||
         opts->Force || opts->Restage || opts->Uninstall)) {
        inst_err(err, errCap, "/devices and /eject take only /yes, /nopause, "
                 "/log and /dir", NULL);
        return 1;
    }
    if (opts->Thunderbolt && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/thunderbolt belongs to an install", NULL);
        return 1;
    }
    if (opts->CmOnly && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/cmonly belongs to an install", NULL);
        return 1;
    }
    if (opts->Stor98 && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/stor98 belongs to an install", NULL);
        return 1;
    }
    if (opts->Stor98 && !opts->Thunderbolt && !opts->CmOnly) {
        inst_err(err, errCap, "/stor98 needs /thunderbolt (or /cmonly): "
                 "Stor98 is part of the Thunderbolt 3 / USB4 support", NULL);
        return 1;
    }
    if (opts->NoStor98 && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/nostor98 belongs to an install", NULL);
        return 1;
    }
    if (opts->NoStor98 && opts->Stor98) {
        inst_err(err, errCap, "/stor98 and /nostor98 contradict each other",
                 NULL);
        return 1;
    }
    if (opts->NoUas && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/nouas belongs to an install", NULL);
        return 1;
    }
    if (opts->NoTray && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/notray belongs to an install", NULL);
        return 1;
    }
    if (opts->To[0] != '\0' && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/to belongs to an install", NULL);
        return 1;
    }
    if (opts->From[0] != '\0' && !opts->Uninstall) {
        inst_err(err, errCap, "/from belongs to an uninstall", NULL);
        return 1;
    }
    if (opts->Devices && opts->Eject) {
        inst_err(err, errCap, "/devices and /eject: one at a time", NULL);
        return 1;
    }
    if (opts->Eject && opts->Yes && opts->EjectId[0] == '\0') {
        inst_err(err, errCap, "/eject takes /yes only with id=<instance id>: "
                 "a number is always confirmed", NULL);
        return 1;
    }
    if (opts->Uninstall && (opts->Defaults || opts->SetCount != 0 ||
                            opts->Restage || opts->Force)) {
        inst_err(err, errCap, "/uninstall takes only /list, /yes, /nopause, "
                 "/log and /dir", NULL);
        return 1;
    }
    return 0;
}

int inst_exit_code(int refused, int failed, int restart, int steps)
{
    if (refused) {
        return INST_EXIT_REFUSED;
    }
    if (failed) {
        return INST_EXIT_FAILED;
    }
    if (steps) {
        return INST_EXIT_STEPS;
    }
    if (restart) {
        return INST_EXIT_RESTART;
    }
    return INST_EXIT_DONE;
}

int inst_final(int changed, int list, int code, int *line)
{
    if (list || !changed) {
        *line = INST_FINAL_NO_CHANGES;
        return code;
    }
    if (code == INST_EXIT_DONE || code == INST_EXIT_RESTART) {
        *line = INST_FINAL_RESTART;
        return INST_EXIT_RESTART;
    }
    *line = INST_FINAL_UNFINISHED;
    return code == INST_EXIT_REFUSED ? INST_EXIT_FAILED : code;
}

const char *inst_final_text(int line)
{
    switch (line) {
    case INST_FINAL_RESTART:
        return "Changes were made. Please restart Windows now.";
    case INST_FINAL_UNFINISHED:
        return "Changes were made, but not everything is done: do what is "
               "said above first,\nthen restart Windows.";
    default:
        return "No changes were made.";
    }
}

const char *inst_final_text_cold(int line, int shutdown)
{
    if (!shutdown) {
        return inst_final_text(line);
    }
    switch (line) {
    case INST_FINAL_RESTART:
        return "Changes were made. Please shut down and start Windows again.";
    case INST_FINAL_UNFINISHED:
        return "Changes were made, but not everything is done: do what is "
               "said above first,\nthen shut down and start Windows again.";
    default:
        return inst_final_text(line);
    }
}

/* ======================================================================== */
/* Installing beside Windows' own installer                                  */
/* ======================================================================== */

void inst_settle_init(INST_SETTLE *s, INST_U32 limitMs, INST_U32 tellMs,
                      INST_U32 expectMs)
{
    memset(s, 0, sizeof(*s));
    s->LimitMs = limitMs;
    s->TellMs = tellMs;
    s->ExpectMs = expectMs;
}

int inst_settle_step(INST_SETTLE *s, INST_U32 nowMs, int pending,
                     INST_U32 sig, int complete, int expect)
{
    int first = s->Polls++ == 0;

    if (pending < 0) {
        return INST_SETTLE_FAILED;
    }
    if (pending) {
        s->Moved = 1;
        s->Quiet = 0;
        if (!s->Pending) {
            s->Pending = 1;
            s->PendingFrom = nowMs;
        }
        if (nowMs >= s->LimitMs) {
            return INST_SETTLE_TIMEOUT;
        }
        if (!s->Told && nowMs - s->PendingFrom >= s->TellMs) {
            s->Told = 1;
            return INST_SETTLE_TELL;
        }
        return INST_SETTLE_WAIT;
    }
    s->Pending = 0;
    if (!complete) {
        /* A reading that failed proves nothing: no quiet stretch. */
        s->Moved = 1;
        s->Quiet = 0;
    } else if (!s->Quiet || sig != s->Sig) {
        if (!first && (!s->Quiet || sig != s->Sig)) {
            s->Moved = 1;
        }
        s->Quiet = 1;
        s->QuietFrom = nowMs;
        s->Sig = sig;
    }
    if (s->Quiet && nowMs - s->QuietFrom >= INST_SETTLE_QUIET_MS &&
        (!expect || nowMs >= s->ExpectMs)) {
        return INST_SETTLE_DONE;
    }
    if (nowMs >= s->LimitMs) {
        return INST_SETTLE_UNSTEADY;
    }
    return INST_SETTLE_WAIT;
}

INST_U32 inst_dn_problem(INST_U32 status, INST_U32 problem)
{
    return (status & INST_DN_HAS_PROBLEM) ? problem : 0;
}

static INST_U32 inst_sig_bytes(INST_U32 h, INST_U32 v)
{
    unsigned char b[4];

    b[0] = (unsigned char)(v & 0xFFUL);
    b[1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[3] = (unsigned char)((v >> 24) & 0xFFUL);
    return inst_fnv1a(h, b, sizeof(b));
}

int inst_tree_sig(const INST_TREE_OPS *ops, INST_U32 dn, int depth,
                  INST_U32 *h, int *expect, int *nodes)
{
    INST_U32 child;
    INST_U32 status;
    INST_U32 problem;
    char id[INST_ID_CHARS];
    int r;

    r = ops->Child(ops->Ctx, dn, &child);
    if (r == INST_TREE_NONE) {
        return 1;
    }
    if (r != INST_TREE_OK || depth >= INST_TREE_DEPTH) {
        /* Below the depth bound a subtree is there but unread: not whole. */
        return 0;
    }
    for (;;) {
        if (++*nodes > INST_TREE_NODES) {
            return 0;
        }
        if (ops->Id(ops->Ctx, child, id, sizeof(id)) != INST_TREE_OK ||
            ops->Status(ops->Ctx, child, &status, &problem) != INST_TREE_OK) {
            return 0;
        }
        id[sizeof(id) - 1] = '\0';
        problem = inst_dn_problem(status, problem);
        *h = inst_fnv1a(*h, (const unsigned char *)id, strlen(id) + 1);
        *h = inst_sig_bytes(*h, status & (INST_DN_STARTED | INST_DN_HAS_PROBLEM));
        *h = inst_sig_bytes(*h, problem);
        if (inst_bus_id(id) == 1 && (problem == INST_PROB_NOT_CONFIGURED ||
                                     problem == INST_PROB_REINSTALL)) {
            *expect = 1;
        }
        if (!inst_tree_sig(ops, child, depth + 1, h, expect, nodes)) {
            return 0;
        }
        r = ops->Sibling(ops->Ctx, child, &child);
        if (r == INST_TREE_NONE) {
            return 1;
        }
        if (r != INST_TREE_OK) {
            return 0;
        }
    }
}

int inst_busy_retry(INST_U32 error, int tries)
{
    return error == 0x20UL && tries >= 1 && tries < INST_BUSY_TRIES;
}

int inst_busy_spent(const int *tries, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (tries[i] >= INST_BUSY_TRIES) {
            return 1;
        }
    }
    return 0;
}

int inst_parent_kind(const char *instanceId, int controller)
{
    int old;

    return controller || inst_bus_base(instanceId, &old) != 0;
}

int inst_settle_after_job(int parent, int outcome, int more)
{
    return parent && more && outcome != INST_DI_OUT_ABSENT &&
           outcome != INST_DI_OUT_REFUSED;
}

int inst_absent_retry(int outcome, int parentJobs, int retries)
{
    return outcome == INST_DI_OUT_ABSENT && parentJobs > 0 && retries == 0;
}

int inst_tunnel_restart(int win2000, int filterInstalled, int inf, int gone,
                        INST_U32 problem)
{
    if (!win2000 || !filterInstalled ||
        (inf != INST_INF_CM && inf != INST_INF_CMF)) {
        return 0;
    }
    return gone || problem == 12UL;
}

static void inst_cat(char *out, size_t cap, const char *s)
{
    size_t n = strlen(out);

    while (*s != '\0' && n + 1 < cap) {
        out[n++] = *s++;
    }
    out[n] = '\0';
}

static void inst_cat_number(char *out, size_t cap, INST_U32 v)
{
    char digits[12];
    int n = 0;
    char one[2];

    do {
        digits[n++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0 && n < (int)sizeof(digits));
    one[1] = '\0';
    while (n > 0) {
        one[0] = digits[--n];
        inst_cat(out, cap, one);
    }
}

void inst_state_text(char *out, size_t cap, const char *service,
                     const char *version, int started, INST_U32 problem)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    inst_cat(out, cap, (service != NULL && service[0] != '\0') ?
                       service : "no driver");
    if (version != NULL && version[0] != '\0') {
        inst_cat(out, cap, " ");
        inst_cat(out, cap, version);
    }
    if (problem != 0) {
        inst_cat(out, cap, ", problem code ");
        inst_cat_number(out, cap, problem);
    } else {
        inst_cat(out, cap, started ? ", started" : ", not started");
    }
}

int inst_staged_again(int hadIdentical, int copiedNow, const char *before,
                      const char *answer)
{
    const char *name;
    const char *p;

    if (!hadIdentical || copiedNow || before == NULL || answer == NULL ||
        before[0] == '\0') {
        return 0;
    }
    name = answer;
    for (p = answer; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/' || *p == ':') {
            name = p + 1;
        }
    }
    return inst_eq(before, name);
}

const char *inst_confirm_text(int runs, int writes, int nt, int unsure)
{
    if (runs) {
        return nt ? "Install and stage the package now" :
                    "Copy the package and update DevicePath now";
    }
    if (unsure) {
        return writes ? "Write the changed settings and check the local copy "
                        "and DevicePath now" :
                        "Check the local copy and DevicePath now (see the entry "
                        "listed apart above)";
    }
    if (writes) {
        return "Write the changed settings now (no device needs a new driver)";
    }
    return nt ? "Every device is current and no setting changes. Check the "
                "staged INFs (and\nstage any missing) now" :
                "Every device is current and no setting changes. Check the "
                "local copy and\nDevicePath now";
}

const char *inst_summary_head(int runs, int writes, int other, int nt)
{
    if (runs || (!writes && !other)) {
        return inst_confirm_text(runs, 0, nt, 0);
    }
    if (!other) {
        return inst_confirm_text(0, 1, nt, 0);
    }
    return writes ? "Write the changed settings and make the other changes "
                    "listed below now (no\ndevice needs a new driver)" :
                    "Make the changes listed below now (no device needs a new "
                    "driver)";
}

/* ======================================================================== */
/* The uninstall's selection and order                                       */
/* ======================================================================== */

int inst_remove_rank(const char *instanceId, const char *service,
                     int filterPresent)
{
    int old;
    int base = inst_bus_base(instanceId, &old);
    int own = (service != NULL && inst_own_service(service));

    if (base == 4) {
        return 1;
    }
    if (base == 2 || base == 3) {
        return 2;
    }
    if (base == 1) {
        return 3;
    }
    if (!own) {
        (void)filterPresent;
        return -1;
    }
    if (inst_eq(service, "xhciuas")) {
        return 0;
    }
    if (inst_eq(service, "xhcicm")) {
        return 4;
    }
    if (inst_starts(instanceId, "PCI\\")) {
        return 5;
    }
    return 3;
}

/* ======================================================================== */
/* The Thunderbolt and USB4 device viewer and eject (record 24)              */
/* ======================================================================== */

/* src\cm\xhcicmf.inf's model ids, in its order; test_inst compares them. */
const char *const instTbPortIds[] = {
    "PCI\\VEN_8086&DEV_15C0&CC_0604", "PCI\\VEN_8086&DEV_156B&CC_0604",
    "PCI\\VEN_8086&DEV_156D&CC_0604", "PCI\\VEN_8086&DEV_1576&CC_0604",
    "PCI\\VEN_8086&DEV_1578&CC_0604", "PCI\\VEN_8086&DEV_15D3&CC_0604",
    "PCI\\VEN_8086&DEV_15DA&CC_0604", "PCI\\VEN_8086&DEV_15E7&CC_0604",
    "PCI\\VEN_8086&DEV_15EA&CC_0604", "PCI\\VEN_8086&DEV_1133&CC_0604",
    "PCI\\VEN_8086&DEV_1136&CC_0604", "PCI\\VEN_8086&DEV_8A1D&CC_0604",
    "PCI\\VEN_8086&DEV_8A1F&CC_0604", "PCI\\VEN_8086&DEV_8A21&CC_0604",
    "PCI\\VEN_8086&DEV_8A23&CC_0604", "PCI\\VEN_8086&DEV_9A23&CC_0604",
    "PCI\\VEN_8086&DEV_9A25&CC_0604", "PCI\\VEN_8086&DEV_9A27&CC_0604",
    "PCI\\VEN_8086&DEV_9A29&CC_0604", "PCI\\VEN_8086&DEV_9A2B&CC_0604",
    "PCI\\VEN_8086&DEV_9A2D&CC_0604", "PCI\\VEN_8086&DEV_9A2F&CC_0604",
    "PCI\\VEN_8086&DEV_9A31&CC_0604", "PCI\\VEN_8086&DEV_466E&CC_0604",
    "PCI\\VEN_8086&DEV_463F&CC_0604", "PCI\\VEN_8086&DEV_462F&CC_0604",
    "PCI\\VEN_8086&DEV_461F&CC_0604", "PCI\\VEN_8086&DEV_A76E&CC_0604",
    "PCI\\VEN_8086&DEV_A71F&CC_0604", "PCI\\VEN_8086&DEV_A72F&CC_0604",
    "PCI\\VEN_8086&DEV_5780&CC_0604", "PCI\\VEN_8086&DEV_5783&CC_0604",
    "PCI\\VEN_1022&DEV_14CD&CC_0604", "PCI\\VEN_1022&DEV_14EF&CC_0604",
    "PCI\\VEN_1022&DEV_150A&CC_0604", "PCI\\VEN_1B21&DEV_2423&CC_0604",
    "PCI\\VEN_1B36&DEV_0001&CC_0604", "PCI\\VEN_1B36&DEV_000C&CC_0604"
};
const int instTbPortIdCount =
    (int)(sizeof(instTbPortIds) / sizeof(instTbPortIds[0]));

int inst_tb_candidate(const char *hw, size_t hwBytes)
{
    int i;

    for (i = 0; i < instTbPortIdCount; i++) {
        if (inst_msz_has(hw, hwBytes, instTbPortIds[i])) {
            return 1;
        }
    }
    return 0;
}

/* 1 when an id of the list has "CC_" followed by `cls` at a field start. */
static int inst_msz_class(const char *msz, size_t bytes, const char *cls)
{
    size_t off = 0;
    size_t n;
    size_t k;
    size_t c = strlen(cls);

    if (msz == NULL) {
        return 0;
    }
    while (off < bytes && msz[off] != '\0') {
        n = 0;
        while (off + n < bytes && msz[off + n] != '\0') {
            n++;
        }
        for (k = 0; k + 3 + c <= n; k++) {
            if ((k == 0 || msz[off + k - 1] == '&' || msz[off + k - 1] == '\\') &&
                inst_eqn(msz + off + k, "CC_", 3) &&
                inst_eqn(msz + off + k + 3, cls, c)) {
                return 1;
            }
        }
        off += n + 1;
    }
    return 0;
}

int inst_msz_has_class(const char *msz, size_t bytes, const char *cls)
{
    return inst_msz_class(msz, bytes, cls);
}

int inst_tb_is_nhi(const char *hw, size_t hwBytes, const char *compat,
                   size_t compatBytes, const char *service)
{
    if (service != NULL && inst_eq(service, "xhcicm")) {
        return 1;
    }
    return inst_msz_class(hw, hwBytes, "0C0340") ||
           inst_msz_class(compat, compatBytes, "0C0340") ||
           inst_msz_class(hw, hwBytes, "0880") ||
           inst_msz_class(compat, compatBytes, "0880");
}

int inst_tb_is_usbhc(const char *hw, size_t hwBytes, const char *compat,
                     size_t compatBytes, const char *service)
{
    if (inst_tb_is_nhi(hw, hwBytes, compat, compatBytes, service)) {
        return 0;
    }
    return inst_msz_class(hw, hwBytes, "0C03") ||
           inst_msz_class(compat, compatBytes, "0C03");
}

static const char *inst_last_part(const char *id)
{
    const char *p = strrchr(id, '\\');

    return p != NULL ? p + 1 : id;
}

static int inst_hex2(const char *s, INST_U32 *v)
{
    int a = inst_hexval(s[0]);
    int b = inst_hexval(s[1]);

    if (a < 0 || b < 0) {
        return 0;
    }
    *v = (INST_U32)(a * 16 + b);
    return 1;
}

int inst_id_location(const char *id, INST_U32 *bdf)
{
    const char *s = inst_last_part(id);
    INST_U32 bus;
    INST_U32 dev;
    INST_U32 fn;

    if (strlen(s) != 21 || !inst_eqn(s, "BUS_", 4) ||
        !inst_eqn(s + 6, "&DEV_", 5) || !inst_eqn(s + 13, "&FUNC_", 6) ||
        !inst_hex2(s + 4, &bus) || !inst_hex2(s + 11, &dev) ||
        !inst_hex2(s + 19, &fn) || dev > 31 || fn > 7) {
        return 0;
    }
    *bdf = (bus << 8) | (dev << 3) | fn;
    return 1;
}

int inst_id_path(const char *id, unsigned char *path, int *count, int room)
{
    const char *s = inst_last_part(id);
    size_t n = strlen(s);
    size_t i;
    INST_U32 v;

    if (n < 4 || (n % 2) != 0 || n / 2 > (size_t)room) {
        return 0;
    }
    for (i = 0; i < n / 2; i++) {
        if (!inst_hex2(s + 2 * i, &v)) {
            return 0;
        }
        path[i] = (unsigned char)v;
    }
    *count = (int)(n / 2);
    return 1;
}

int inst_tb_loc_match(const char *id, INST_U32 bdf, const unsigned char *path,
                      int pathCount)
{
    INST_U32 at;
    unsigned char got[16];
    int n;
    int i;

    if (inst_id_location(id, &at)) {
        return at == bdf;
    }
    if (pathCount <= 0 || !inst_id_path(id, got, &n, 16) || n != pathCount) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (got[i] != path[i]) {
            return 0;
        }
    }
    return 1;
}

INST_U32 inst_get32(const unsigned char *b, size_t offset)
{
    return (INST_U32)b[offset] | ((INST_U32)b[offset + 1] << 8) |
           ((INST_U32)b[offset + 2] << 16) | ((INST_U32)b[offset + 3] << 24);
}

static void inst_put32(unsigned char *b, size_t offset, INST_U32 v)
{
    b[offset] = (unsigned char)v;
    b[offset + 1] = (unsigned char)(v >> 8);
    b[offset + 2] = (unsigned char)(v >> 16);
    b[offset + 3] = (unsigned char)(v >> 24);
}

int inst_mw_reading(int found, INST_U32 type, INST_U32 bytes, INST_U32 dword,
                    INST_U32 def, INST_U32 *value)
{
    if (!found || (type == INST_REG_NONE && bytes == 0)) {
        return 0;
    }
    if (type == INST_REG_DWORD && bytes == 4) {
        *value = dword;
        return 1;
    }
    *value = def;
    return 2;
}

int inst_mk_system(const INST_ENV *env)
{
    /* 2000 to 7, and 8 and 8.1 for the connection manager alone (record 23
     * section 4.5, revision 13). */
    return env->Nt && (env->Major == 5UL ||
                       (env->Major == 6UL && env->Minor <= 3UL));
}

int inst_mk_end_action(int cmService, int tbInRun, int markSystem,
                       int safeEject)
{
    if (cmService == 0) {
        return INST_MK_REMOVE;
    }
    if (cmService != 1) {
        return INST_MK_LEAVE;
    }
    if (!safeEject) {
        /* Safe eject off: no mark has a use, and the service goes with
         * its marks as the uninstall's step 0 takes them. */
        return INST_MK_REMOVE;
    }
    if (tbInRun && markSystem) {
        return INST_MK_INSTALL;
    }
    return INST_MK_LEAVE;
}

int inst_cddb_service_gone(const char *service, const char *const *gone,
                           int n)
{
    int i;

    if (service == NULL || service[0] == '\0' || gone == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (gone[i] != NULL && inst_eq(service, gone[i])) {
            return 1;
        }
    }
    return 0;
}

int inst_mk_safe_eject(int mwState, INST_U32 mwValue, const INST_VNOW *now,
                       int nNow)
{
    int i;

    if (mwState > 0) {
        return mwValue != 0 ? 1 : 0;
    }
    if (mwState < 0 || now == NULL || nNow <= 0) {
        return 1;
    }
    for (i = 0; i < nNow; i++) {
        if (now[i].State != 1 || now[i].Value != 0) {
            return 1;
        }
    }
    return 0;
}

/* The marks answer's and the confirmation's layouts (src\cm\cm_query.h),
 * repeated as numbers as the status record's are; test_inst checks them
 * against it. */
#define INST_QM_MAGIC       0x4D4D4358UL
#define INST_QM_HDR         64UL
#define INST_QM_ENT         64UL
#define INST_QM_MAX_BYTES   (64UL + 256UL * 64UL)
#define INST_QM_EF_LOCATED  0x01UL

int inst_qc_build(const unsigned char *b, size_t n, INST_U32 member,
                  INST_U32 bus, INST_U32 address, unsigned char *out,
                  INST_U32 *need)
{
    INST_U32 bytes;
    INST_U32 count;
    INST_U32 entBytes;
    INST_U32 off;
    INST_U32 i;
    const unsigned char *hit = NULL;

    *need = 0;
    if (b == NULL || n < INST_QM_HDR || inst_get32(b, 0) != INST_QM_MAGIC ||
        inst_get32(b, 4) != 1UL) {
        return 0;
    }
    bytes = inst_get32(b, 8);
    if (bytes < INST_QM_HDR || bytes > INST_QM_MAX_BYTES) {
        return 0;
    }
    if ((size_t)bytes > n) {
        *need = bytes;
        return -2;
    }
    count = inst_get32(b, 28);
    entBytes = inst_get32(b, 32);
    off = inst_get32(b, 36);
    if (count > 256UL || entBytes < INST_QM_ENT || off < INST_QM_HDR ||
        off > bytes || count * entBytes > bytes - off) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *e = b + off + i * entBytes;
        if (inst_get32(e, 0) != member || inst_get32(e, 4) != bus ||
            inst_get32(e, 8) != address ||
            (inst_get32(e, 16) & INST_QM_EF_LOCATED) == 0 ||
            inst_get32(e, 32) == 0) {
            continue;
        }
        if (hit != NULL) {
            return -1;
        }
        hit = e;
    }
    if (hit == NULL) {
        return 0;
    }
    memset(out, 0, INST_QC_BYTES);
    inst_put32(out, 0, inst_get32(b, 16));      /* the epoch, low word  */
    inst_put32(out, 4, inst_get32(b, 20));      /* and high             */
    inst_put32(out, 8, inst_get32(hit, 0));     /* the member           */
    inst_put32(out, 12, inst_get32(hit, 4));    /* the bus              */
    inst_put32(out, 16, inst_get32(hit, 8));    /* the address          */
    inst_put32(out, 20, inst_get32(hit, 12));   /* the serial           */
    inst_put32(out, 24, inst_get32(hit, 36));   /* the pending's gen    */
    return 1;
}

/* The record's layout (src\cm\cm_status.h), repeated here as numbers so
 * the pure half needs no driver header; test_inst checks them against it. */
#define INST_ST_MAGIC       0x534D4358UL
#define INST_ST_HDR         128UL
#define INST_ST_ENTRY       128UL
#define INST_ST_MAX_ROUTERS 24UL

int inst_st_check(const unsigned char *b, size_t n)
{
    INST_U32 count;
    INST_U32 i;

    if (n < INST_ST_HDR || inst_get32(b, 0) != INST_ST_MAGIC ||
        inst_get32(b, 4) != 1UL || inst_get32(b, 76) != INST_ST_ENTRY) {
        return 0;
    }
    count = inst_get32(b, 72);
    if (count > INST_ST_MAX_ROUTERS || inst_get32(b, 48) > 4UL ||
        inst_get32(b, 8) != INST_ST_HDR + count * INST_ST_ENTRY ||
        (size_t)inst_get32(b, 8) != n) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (b[84 + 8 * i] > 7) {
            return 0;
        }
    }
    return 1;
}

/* 64-bit helpers over two 32-bit words, for the pure half's C89. */
static void inst_mul32(INST_U32 a, INST_U32 b, INST_U32 *hi, INST_U32 *lo)
{
    INST_U32 al = a & 0xFFFFUL;
    INST_U32 ah = a >> 16;
    INST_U32 bl = b & 0xFFFFUL;
    INST_U32 bh = b >> 16;
    INST_U32 ll = al * bl;
    INST_U32 lh = al * bh;
    INST_U32 hl = ah * bl;
    INST_U32 hh = ah * bh;
    INST_U32 mid = (ll >> 16) + (lh & 0xFFFFUL) + (hl & 0xFFFFUL);

    *lo = (ll & 0xFFFFUL) | ((mid & 0xFFFFUL) << 16);
    *hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

/* (aHi:aLo - bHi:bLo) >> 24, as a signed 32-bit count of 1.68 s units
 * (2^24 x 100 ns); the difference of two boot times stays small. */
static long inst_boot_units(INST_U32 tHi, INST_U32 tLo, INST_U32 uHi,
                            INST_U32 uLo)
{
    INST_U32 lo = tLo - uLo;
    INST_U32 hi = tHi - uHi - (tLo < uLo ? 1UL : 0UL);

    return (long)((hi << 8) | (lo >> 24));
}

int inst_st_fresh(INST_U32 recUptimeS, INST_U32 recTimeLo, INST_U32 recTimeHi,
                  INST_U32 nowLo, INST_U32 nowHi, INST_U32 tickMs)
{
    INST_U32 hi;
    INST_U32 lo;
    long rec;
    long now;
    long d;

    inst_mul32(recUptimeS, 10000000UL, &hi, &lo);
    rec = inst_boot_units(recTimeHi, recTimeLo, hi, lo);
    inst_mul32(tickMs, 10000UL, &hi, &lo);
    now = inst_boot_units(nowHi, nowLo, hi, lo);
    d = rec - now;
    if (d < 0) {
        d = -d;
    }
    /* Five minutes: 300 s / 1.6777216 s, rounded down. */
    return d <= 178L;
}

int inst_st_confirmed(const unsigned char *b, INST_U32 ask)
{
    return ask != 0 && inst_get32(b, 124) == ask;
}

static int inst_grand(const int *parent, int n, int i)
{
    int p;

    if (i < 0 || i >= n) {
        return -1;
    }
    p = parent[i];
    if (p < 0 || p >= n) {
        return -1;
    }
    return parent[p];
}

void inst_tb_protect(const int *parent, const int *isNhi, const int *isUsbHc,
                     int n, int *prot)
{
    int i;
    int j;
    int a;
    int g;

    for (i = 0; i < n; i++) {
        prot[i] = 0;
    }
    for (i = 0; i < n; i++) {
        if (!isNhi[i]) {
            continue;
        }
        for (a = i; a >= 0 && a < n && !prot[a]; a = parent[a]) {
            prot[a] = 1;
        }
        g = inst_grand(parent, n, i);
        for (j = 0; j < n; j++) {
            if (isUsbHc[j] && inst_grand(parent, n, j) == g) {
                prot[j] = 1;
            }
        }
    }
}

void inst_tb_units(const int *parent, const int *verified, int n,
                   int *unitOf)
{
    int i;
    int p;

    for (i = 0; i < n; i++) {
        unitOf[i] = -1;
        p = parent[i];
        if (p < 0 || p >= i || verified[i]) {
            continue;
        }
        unitOf[i] = verified[p] ? i : unitOf[p];
    }
}

int inst_tb_unit_blocked(const int *unitOf, const int *prot, int n, int u)
{
    int i;

    for (i = 0; i < n; i++) {
        if (unitOf[i] == u && prot[i]) {
            return 1;
        }
    }
    return 0;
}

int inst_eject_code(int outcome)
{
    switch (outcome) {
    case INST_EJ_DONE:
        return INST_EXIT_DONE;
    case INST_EJ_VETO:
        return INST_EXIT_STEPS;
    case INST_EJ_FAILED:
        return INST_EXIT_FAILED;
    default:
        return INST_EXIT_REFUSED;
    }
}

int inst_eject9x_query(INST_U32 cr, const char **text)
{
    if (cr == 0) {
        *text = "";
        return INST_EJ_DONE;
    }
    if (cr == INST_CR_REMOVE_VETOED) {
        *text = "Windows refused: the device is in use (a program may have a "
                "file open on it,\nor its driver refused). Close any program "
                "using it and try again.";
        return INST_EJ_VETO;
    }
    if (cr == INST_CR_INVALID_DEVNODE || cr == INST_CR_NO_SUCH_DEVNODE) {
        *text = "The device list changed; nothing was ejected.";
        return INST_EJ_CHANGED;
    }
    if (cr == INST_CR_CALL_NOT_IMPLEMENTED) {
        *text = "This Windows does not offer the removal this needs.";
        return INST_EJ_UNSUPPORTED;
    }
    *text = "Windows refused the removal.";
    return INST_EJ_VETO;
}

int inst_eject9x_remove(INST_U32 cr, const char **text)
{
    if (cr == 0) {
        *text = "";
        return INST_EJ_DONE;
    }
    *text = "Windows began to stop the device but could not finish. It is not "
            "safe to unplug\nit now. Shut down Windows, then unplug it.";
    return INST_EJ_FAILED;
}

/* PNP_VETO_TYPE (cfg.h). */
#define INST_VETO_LEGACY_DEVICE     1UL
#define INST_VETO_PENDING_CLOSE     2UL
#define INST_VETO_WINDOWS_APP       3UL
#define INST_VETO_WINDOWS_SERVICE   4UL
#define INST_VETO_OUTSTANDING_OPEN  5UL
#define INST_VETO_DEVICE            6UL
#define INST_VETO_DRIVER            7UL
#define INST_VETO_ILLEGAL_REQUEST   8UL
#define INST_VETO_NON_DISABLEABLE   10UL
#define INST_VETO_LEGACY_DRIVER     11UL
#define INST_VETO_RIGHTS            12UL

int inst_unit_not_marked(int capsKnown, INST_U32 caps)
{
    /* CM_DEVCAP_REMOVABLE 0x4, CM_DEVCAP_SURPRISEREMOVALOK 0x80: the
     * fallback routine is for devices with neither (Microsoft's
     * reference). */
    return capsKnown && (caps & 0x84UL) == 0;
}

INST_U32 inst_eject_cr(INST_U32 cr, INST_U32 vetoType)
{
    return cr == 0 && vetoType != 0 ? INST_CR_REMOVE_VETOED : cr;
}

static int inst_rights_answer(INST_U32 cr, INST_U32 vetoType)
{
    return cr == INST_CR_ACCESS_DENIED ||
           (cr == INST_CR_REMOVE_VETOED && vetoType == INST_VETO_RIGHTS);
}

int inst_eject_nt_step(INST_U32 cr, INST_U32 vetoType, int notMarked,
                       int mayRemove)
{
    if (!notMarked) {
        return INST_NT_ANSWER;
    }
    if (cr == INST_CR_REMOVE_VETOED && vetoType == INST_VETO_ILLEGAL_REQUEST) {
        return mayRemove ? INST_NT_FALLBACK : INST_NT_ADMIN;
    }
    if (inst_rights_answer(cr, vetoType)) {
        return INST_NT_ADMIN;
    }
    return INST_NT_ANSWER;
}

int inst_eject_nt_removal(INST_U32 cr, INST_U32 vetoType, const char **text,
                          int *withName)
{
    if (inst_rights_answer(cr, vetoType)) {
        *withName = 0;
        *text = inst_admin_text();
        return INST_EJ_RIGHTS;
    }
    if (cr == INST_CR_CALL_NOT_IMPLEMENTED) {
        *withName = 0;
        *text = inst_not_removable_text();
        return INST_EJ_VETO;
    }
    return inst_eject_nt(cr, vetoType, text, withName);
}

int inst_removal_retry(INST_U32 cr, INST_U32 flags)
{
    return cr == INST_CR_INVALID_FLAG && (flags & INST_REMOVE_NO_RESTART) != 0;
}

const char *inst_admin_text(void)
{
    return "Windows does not list this device for safe removal. Run this as "
           "an\nadministrator to stop it.";
}

const char *inst_not_removable_text(void)
{
    return "Windows will not stop this device: it does not see it as "
           "removable now, and\nthis Windows has no other way to stop it. "
           "Restart Windows with the device\nconnected: it is then listed "
           "for removal again. Or close every program using\nit, shut "
           "down Windows and unplug it while the computer is off.";
}

int inst_eject_nt(INST_U32 cr, INST_U32 vetoType, const char **text,
                  int *withName)
{
    *withName = 0;
    if (cr == 0) {
        *text = "";
        return INST_EJ_DONE;
    }
    if (cr == INST_CR_ACCESS_DENIED) {
        *text = "Windows did not allow this account to remove the device. "
                "Run it as an administrator.";
        return INST_EJ_RIGHTS;
    }
    if (cr == INST_CR_INVALID_DEVNODE || cr == INST_CR_NO_SUCH_DEVNODE) {
        *text = "The device list changed; nothing was ejected.";
        return INST_EJ_CHANGED;
    }
    if (cr == INST_CR_CALL_NOT_IMPLEMENTED) {
        *text = "This Windows does not offer the removal this needs.";
        return INST_EJ_UNSUPPORTED;
    }
    if (cr != INST_CR_REMOVE_VETOED) {
        *text = "Windows could not eject the device, and its state is not "
                "known. Do not unplug\nit until Windows has been shut down.";
        return INST_EJ_FAILED;
    }
    switch (vetoType) {
    case INST_VETO_OUTSTANDING_OPEN:
        *text = "Windows refused: a program is using the device. Close it "
                "and try again.";
        break;
    case INST_VETO_DEVICE:
    case INST_VETO_DRIVER:
        *text = "Windows refused: %s did not allow it.";
        *withName = 1;
        break;
    case INST_VETO_PENDING_CLOSE:
        *text = "Windows refused: the device is still being closed. Try "
                "again in a moment.";
        break;
    case INST_VETO_WINDOWS_APP:
    case INST_VETO_WINDOWS_SERVICE:
        *text = "Windows refused: %s is using the device.";
        *withName = 1;
        break;
    case INST_VETO_RIGHTS:
        *text = "Windows did not allow this account to remove the device. "
                "Run it as an administrator.";
        return INST_EJ_RIGHTS;
    case INST_VETO_ILLEGAL_REQUEST:
    case INST_VETO_NON_DISABLEABLE:
    case INST_VETO_LEGACY_DEVICE:
    case INST_VETO_LEGACY_DRIVER:
        *text = "Windows does not allow this device to be removed.";
        break;
    default:
        *text = "Windows refused the removal (%s).";
        *withName = 1;
        break;
    }
    return INST_EJ_VETO;
}

int inst_unplug_kind(int walkWhole, int othersLeft, int singleFunction,
                     INST_U32 usb3)
{
    if (othersLeft) {
        return INST_UNPLUG_OTHERS;
    }
    if (walkWhole && singleFunction && usb3 == 0) {
        return INST_UNPLUG_NOW;
    }
    return INST_UNPLUG_CHECK;
}

const char *inst_unplug_text(int kind)
{
    switch (kind) {
    case INST_UNPLUG_NOW:
        return "You can now unplug the device.";
    case INST_UNPLUG_OTHERS:
        return "Other devices behind the same Thunderbolt port are still "
               "running: eject them\ntoo before you unplug it.";
    case INST_UNPLUG_RESTARTED:
        return "The device has started again, so it is not safe to unplug. "
               "Close the programs\nusing it and run INSTALL /console /eject again.";
    default:
        return "Before you unplug it, make sure none of its other devices "
               "(USB ports, card\nreaders, network) is in use; stop them in "
               "Windows first.";
    }
}

/* ======================================================================== */
/* The system's kind and the USB drivers (record 23 sections 4.3, 4.5)       */
/* ======================================================================== */

int inst_os_kind(int nt, INST_U32 major, INST_U32 minor, INST_U32 build)
{
    if (!nt) {
        /* 98 SE is 4.10.2222, ME 4.90.3000; 98 (4.10.1998) and 95 are
         * not targets. */
        return (major == 4UL && ((minor == 10UL && build >= 2222UL) ||
                                 minor == 90UL)) ? INST_OS_FULL
                                                 : INST_OS_REFUSED;
    }
    if (major == 5UL && minor <= 2UL) {
        return INST_OS_FULL;
    }
    if (major == 6UL && minor <= 1UL) {
        return INST_OS_FULL;
    }
    if (major == 6UL && (minor == 2UL || minor == 3UL)) {
        return INST_OS_CMONLY;
    }
    return INST_OS_REFUSED;
}

const char *inst_os_name(int nt, INST_U32 major, INST_U32 minor)
{
    if (!nt) {
        return minor >= 90UL ? "Windows ME" : "Windows 98 SE";
    }
    if (major == 5UL) {
        return minor == 0UL ? "Windows 2000" : minor == 1UL ? "Windows XP"
                                                            : "Windows XP x64";
    }
    if (major == 6UL) {
        return minor == 0UL ? "Windows Vista" : minor == 1UL ? "Windows 7" :
               minor == 2UL ? "Windows 8" : minor == 3UL ? "Windows 8.1" :
               "Windows";
    }
    return "Windows";
}

int inst_uas_decide(int usbInclude, int noUas)
{
    return usbInclude && !noUas;
}

int inst_usb_decide(int osKind, int nt, int haveCm, int cmOnly, int *offered,
                    const char **refusal)
{
    *offered = 0;
    *refusal = NULL;
    if (osKind == INST_OS_CMONLY) {
        if (!haveCm) {
            *refusal = "this package carries no Thunderbolt or USB4 driver: "
                       "there is nothing to install on Windows 8 or 8.1.";
            return -1;
        }
        return 0;
    }
    if (osKind != INST_OS_FULL) {
        *refusal = "this Windows is not one this package supports.";
        return -1;
    }
    if (!nt) {
        if (cmOnly) {
            *refusal = "/cmonly: the connection manager alone is not offered "
                       "on Windows 98 SE and ME.";
            return -1;
        }
        return 1;
    }
    if (!haveCm) {
        if (cmOnly) {
            *refusal = "/cmonly: this package carries no Thunderbolt or USB4 "
                       "driver.";
            return -1;
        }
        return 1;
    }
    *offered = 1;
    return cmOnly ? 0 : 1;
}

/* ======================================================================== */
/* The Thunderbolt 3 / USB4 question (record 23 section 8.5)                 */
/* ======================================================================== */

int inst_tb_decide(int haveCm, int nhiPresent, int optYes, int optThunderbolt,
                   int optList, int *ask, int *defaultYes)
{
    (void)nhiPresent;
    *ask = 0;
    *defaultYes = 0;
    if (!haveCm) {
        return 0;
    }
    if (optThunderbolt) {
        return 1;
    }
    if (optYes || optList) {
        return *defaultYes;
    }
    *ask = 1;
    return 0;
}

int inst_tb_answer(const char *line, int defaultYes)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return defaultYes ? 1 : 0;
    }
    if ((line[0] == 'y' || line[0] == 'Y') &&
        (line[1] == '\0' || inst_eq(line, "yes"))) {
        return 1;
    }
    if ((line[0] == 'n' || line[0] == 'N') &&
        (line[1] == '\0' || inst_eq(line, "no"))) {
        return 0;
    }
    return -1;
}

int inst_out_mirror(int outIsConsole, int errIsConsole, int inIsConsole)
{
    return !outIsConsole && errIsConsole && inIsConsole;
}

/* ======================================================================== */
/* The filter INF's place in a flavour directory (record 23 section 4.1)    */
/* ======================================================================== */

const char *inst_cmf_rel(int arch)
{
    return arch == INST_ARCH_AMD64 ? "xhcicmf.inf" : "nt\\xhcicmf.inf";
}

const char *inst_cmf_pnf(int arch)
{
    return arch == INST_ARCH_AMD64 ? "xhcicmf.PNF" : "nt\\xhcicmf.PNF";
}

const char *inst_cmf_where(int arch)
{
    return arch == INST_ARCH_AMD64 ? "the package's x64 directory" :
                                     "the package's nt\\ directory";
}

/* ======================================================================== */
/* The USB tree tool (record 23 section 9.5, record 25 section 8)            */
/* ======================================================================== */

const char *inst_tray_file(int arch)
{
    return arch == INST_ARCH_AMD64 ? "XHCITR64.EXE" : "XHCITR.EXE";
}

int inst_tray_run_data(const char *exe, char *out, size_t cap)
{
    size_t n = strlen(exe);

    if (strchr(exe, '"') != NULL || n == 0 || n + 9 > cap) {
        return 0;
    }
    strcpy(out, "\"");
    strcat(out, exe);
    strcat(out, "\" /tray");
    return 1;
}

int inst_tray_copy_needed(int installed, const INST_U32 instVer[4], int pkg,
                          const INST_U32 pkgVer[4])
{
    if (installed == 0) {
        return 1;
    }
    if (pkg != 1) {
        return 0;
    }
    return installed != 1 || inst_ver_cmp(instVer, pkgVer) < 0;
}

int inst_tray_leftover(int bakExists, int exeExists)
{
    if (!bakExists) {
        return INST_TRAY_BAK_NONE;
    }
    return exeExists ? INST_TRAY_BAK_DELETE : INST_TRAY_BAK_RESTORE;
}

int inst_tray_run_action(int noTray, int runState, int inPlace)
{
    if (noTray) {
        return runState != 0 ? INST_TRAY_RUN_DELETE : INST_TRAY_RUN_KEEP;
    }
    if (inPlace && runState != 1) {
        return INST_TRAY_RUN_SET;
    }
    return INST_TRAY_RUN_KEEP;
}

const char *inst_tray_list_text(int runState, int action)
{
    if (runState == 1) {
        return action == INST_TRAY_RUN_DELETE ?
               "; it starts at login now, and would no longer (/notray)." :
               "; it starts at login.";
    }
    if (runState == 0) {
        return action == INST_TRAY_RUN_SET ?
               "; it does not start at login now (a run without /notray "
               "makes it start at login)." :
               "; it does not start at login.";
    }
    if (action == INST_TRAY_RUN_SET) {
        return "; its start at login holds other data, or could not be read "
               "(a run without /notray sets it to this file).";
    }
    return action == INST_TRAY_RUN_DELETE ?
           "; its start at login holds other data, or could not be read; it "
           "would be removed (/notray)." :
           "; its start at login holds other data, or could not be read.";
}

int inst_oem_name_ours(const char *valueName, const char *path,
                       const char *shortPath)
{
    if (path != NULL && path[0] != '\0' && inst_eq(valueName, path)) {
        return 1;
    }
    return shortPath != NULL && shortPath[0] != '\0' &&
           inst_eq(valueName, shortPath);
}

/* ======================================================================== */
/* The console (owner, 2026-10-16)                                           */
/* ======================================================================== */

#define INST_MAX_INDENT 40

void inst_wrap_init(INST_WRAP *w)
{
    memset(w, 0, sizeof(*w));
    w->InLead = 1;
    w->Cont = 2;
}

/* A new logical line: what the next character starts. */
static void inst_wrap_line(INST_WRAP *w)
{
    w->Col = 0;
    w->Lead = 0;
    w->Cont = 2;
    w->InLead = 1;
    w->Words = 0;
    w->Wrapped = 0;
    w->Hung = 0;
    w->Spaces = 0;
    w->WordLen = 0;
}

static void inst_put(char *out, size_t cap, size_t *n, char c)
{
    if (*n + 1 < cap) {
        out[(*n)++] = c;
        out[*n] = '\0';
    }
}

static void inst_wrap_max(INST_WRAP *w)
{
    if (w->Col > w->MaxCol) {
        w->MaxCol = w->Col;
    }
}

static void inst_wrap_break(INST_WRAP *w, char *out, size_t cap, size_t *n)
{
    size_t i;

    inst_put(out, cap, n, '\n');
    for (i = 0; i < w->Cont; i++) {
        inst_put(out, cap, n, ' ');
    }
    w->Col = w->Cont;
    w->Wraps++;
    w->Wrapped = 1;
}

/* "-", "1.", "[1]", "NOTE:": a line's marker, which its text hangs under. */
static int inst_marker(const char *word, size_t len)
{
    size_t i;

    if (len == 1 && (word[0] == '-' || word[0] == '*')) {
        return 1;
    }
    if (len == 5 && strncmp(word, "NOTE:", 5) == 0) {
        return 1;
    }
    if (len >= 2 && word[len - 1] == '.') {
        for (i = 0; i + 1 < len; i++) {
            if (word[i] < '0' || word[i] > '9') {
                return 0;
            }
        }
        return 1;
    }
    if (len >= 3 && word[0] == '[' && word[len - 1] == ']') {
        for (i = 1; i + 1 < len; i++) {
            if (word[i] < '0' || word[i] > '9') {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

/* The held word placed: on this line, or on a continuation line when it
 * does not fit; a word wider than the room on a line is broken after its
 * last backslash in the second half of that room, or at the width. */
static void inst_wrap_word(INST_WRAP *w, char *out, size_t cap, size_t *n)
{
    size_t i;
    size_t start = 0;

    if (w->WordLen == 0) {
        return;
    }
    if (w->Words > 0 && w->Col + w->Spaces + w->WordLen > INST_COLUMNS) {
        inst_wrap_break(w, out, cap, n);
        w->Spaces = 0;
    }
    if (w->Words >= 1 && !w->Wrapped && !w->Hung && w->Spaces >= 2 &&
        w->Col + w->Spaces <= INST_MAX_INDENT) {
        w->Cont = w->Col + w->Spaces;
        w->Hung = 1;
    }
    for (i = 0; i < w->Spaces; i++) {
        inst_put(out, cap, n, ' ');
    }
    w->Col += w->Spaces;
    w->Spaces = 0;
    for (;;) {
        size_t room = w->Col < INST_COLUMNS ? INST_COLUMNS - w->Col : 0;
        size_t cut = room;
        size_t k;
        if (w->WordLen - start <= room) {
            break;
        }
        if (room == 0) {
            inst_wrap_break(w, out, cap, n);
            continue;
        }
        for (k = room; k > room / 2; k--) {
            if (w->Word[start + k - 1] == '\\') {
                cut = k;
                break;
            }
        }
        for (k = 0; k < cut; k++) {
            inst_put(out, cap, n, w->Word[start + k]);
        }
        w->Col += cut;
        inst_wrap_max(w);
        start += cut;
        inst_wrap_break(w, out, cap, n);
    }
    for (i = start; i < w->WordLen; i++) {
        inst_put(out, cap, n, w->Word[i]);
    }
    w->Col += w->WordLen - start;
    inst_wrap_max(w);
    if (w->Words == 0 && !w->Wrapped && inst_marker(w->Word, w->WordLen) &&
        w->Lead + w->WordLen + 1 <= INST_MAX_INDENT) {
        w->Cont = w->Lead + w->WordLen + 1;
    }
    w->Words++;
    w->WordLen = 0;
}

static void inst_wrap_feed_at(INST_WRAP *w, const char *text, char *out,
                              size_t cap, size_t *n)
{
    for (; *text != '\0'; text++) {
        char c = *text;
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            inst_wrap_word(w, out, cap, n);
            inst_put(out, cap, n, '\n');
            inst_wrap_line(w);
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (w->InLead) {
                inst_put(out, cap, n, ' ');
                w->Col++;
                w->Lead++;
                inst_wrap_max(w);
            } else {
                inst_wrap_word(w, out, cap, n);
                w->Spaces++;
            }
            continue;
        }
        if (w->InLead) {
            w->InLead = 0;
            w->Cont = w->Lead + 2 <= INST_MAX_INDENT ? w->Lead + 2
                                                      : INST_MAX_INDENT;
        }
        if (w->WordLen + 1 >= sizeof(w->Word)) {
            inst_wrap_word(w, out, cap, n);
        }
        w->Word[w->WordLen++] = c;
    }
}

size_t inst_wrap_feed(INST_WRAP *w, const char *text, char *out, size_t cap)
{
    size_t n = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    inst_wrap_feed_at(w, text, out, cap, &n);
    return n;
}

static void inst_wrap_flush_at(INST_WRAP *w, char *out, size_t cap, size_t *n)
{
    size_t i;

    inst_wrap_word(w, out, cap, n);
    if (w->Col + w->Spaces <= INST_COLUMNS) {
        for (i = 0; i < w->Spaces; i++) {
            inst_put(out, cap, n, ' ');
        }
        w->Col += w->Spaces;
        inst_wrap_max(w);
    }
    w->Spaces = 0;
}

size_t inst_wrap_flush(INST_WRAP *w, char *out, size_t cap)
{
    size_t n = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    inst_wrap_flush_at(w, out, cap, &n);
    return n;
}

void inst_wrap_input(INST_WRAP *w)
{
    inst_wrap_line(w);
}

size_t inst_wrap_text(const char *text, char *out, size_t cap)
{
    INST_WRAP w;
    size_t n = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    inst_wrap_init(&w);
    inst_wrap_feed_at(&w, text, out, cap, &n);
    inst_wrap_flush_at(&w, out, cap, &n);
    return n;
}

size_t inst_text_width(const char *text)
{
    size_t col = 0;
    size_t max = 0;

    for (; *text != '\0'; text++) {
        if (*text == '\n') {
            col = 0;
        } else if (*text != '\r') {
            col++;
            if (col > max) {
                max = col;
            }
        }
    }
    return max;
}

/* ======================================================================== */
/* The questions (owner, 2026-10-16)                                         */
/* ======================================================================== */

#define INST_RAW_CHARS 8192

void inst_caution_text(int index, INST_U32 values, char *out, size_t cap)
{
    const char *c;
    const char *p;
    INST_U32 these;
    INST_U32 b;
    int n = 0;
    int total = 0;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (index < 0 || index >= instValueCount ||
        instValues[index].Caution == NULL) {
        return;
    }
    c = instValues[index].Caution;
    p = strstr(c, "%s");
    if (p == NULL) {
        inst_copy(out, c, cap);
        return;
    }
    these = instValues[index].CautionMask != 0 ?
            (instValues[index].CautionMask & values) : values;
    for (b = 0; b < 32UL; b++) {
        total += (these & (1UL << b)) != 0;
    }
    inst_copy(out, c, (size_t)(p - c) + 1 < cap ? (size_t)(p - c) + 1 : cap);
    for (b = 0; b < 32UL; b++) {
        if ((these & (1UL << b)) == 0) {
            continue;
        }
        if (n > 0) {
            inst_cat(out, cap, n + 1 == total ? " or " : ", ");
        }
        inst_cat_number(out, cap, b);
        n++;
    }
    inst_cat(out, cap, p + 2);
}

static int inst_value_question_at(char *out, size_t cap, int index,
                                  INST_U32 mask, INST_U32 def,
                                  const INST_VNOW *now, int n, int mwState,
                                  INST_U32 mwValue, int cmNewNote, int list,
                                  INST_U32 *enter);

int inst_value_question(char *out, size_t cap, int index, INST_U32 mask,
                        INST_U32 def, const INST_VNOW *now, int n,
                        int cmNewNote, int list, INST_U32 *enter)
{
    return inst_value_question_at(out, cap, index, mask, def, now, n, -2, 0,
                                  cmNewNote, list, enter);
}

int inst_value_question_mw(char *out, size_t cap, int index, INST_U32 mask,
                           INST_U32 def, const INST_VNOW *now, int n,
                           int mwState, INST_U32 mwValue, int cmNewNote,
                           int list, INST_U32 *enter)
{
    return inst_value_question_at(out, cap, index, mask, def, now, n, mwState,
                                  mwValue, cmNewNote, list, enter);
}

int inst_mw_write_needed(int mwState, INST_U32 mwValue, const INST_VNOW *now,
                         int n, INST_U32 def, INST_U32 answer)
{
    int i;

    if (mwState == 1) {
        return mwValue != answer;
    }
    if (n == 0) {
        return 1;   /* no device to keep it: the machine-wide value holds it */
    }
    for (i = 0; i < n; i++) {
        INST_U32 eff = now[i].State > 0 ? now[i].Value : def;
        if (eff != answer) {
            return 1;
        }
    }
    return 0;
}

/* mwState: -2 no machine-wide reading at all (the console of 3.0.0.0's
 * form), -1 unreadable, 0 not set, 1 set to mwValue. */
static int inst_value_question_at(char *out, size_t cap, int index,
                                  INST_U32 mask, INST_U32 def,
                                  const INST_VNOW *now, int n, int mwState,
                                  INST_U32 mwValue, int cmNewNote, int list,
                                  INST_U32 *enter)
{
    char raw[INST_RAW_CHARS];
    char line[INST_LINE_CHARS];
    const INST_VALUE *v;
    INST_U32 first = def;
    INST_U32 b;
    INST_U32 lo = 32;
    INST_U32 hi = 0;
    int count = 0;
    int named = 0;
    int same = 1;
    int i;

    if (index < 0 || index >= instValueCount) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    v = &instValues[index];
    raw[0] = '\0';
    inst_cat(raw, sizeof(raw), v->Name);
    inst_cat(raw, sizeof(raw), " - ");
    inst_cat(raw, sizeof(raw), v->Meaning);
    inst_cat(raw, sizeof(raw), "\n");
    if (mwState == 1) {
        /* Task 42-A.17: the machine-wide value wins wherever it is set. */
        inst_cat(raw, sizeof(raw), "  now: ");
        inst_cat_number(raw, sizeof(raw), mwValue);
        inst_cat(raw, sizeof(raw), ", set machine-wide (every controller "
                 "uses it)\n");
        for (i = 0; i < n; i++) {
            if (now[i].State <= 0 || now[i].Value == mwValue) {
                continue;
            }
            line[0] = '\0';
            inst_cat(line, sizeof(line), "  the device's own value: ");
            inst_cat_number(line, sizeof(line), now[i].Value);
            inst_cat(line, sizeof(line), ", not used");
            if (now[i].Desc != NULL && now[i].Desc[0] != '\0') {
                inst_cat(line, sizeof(line),
                         strlen(line) + strlen(now[i].Desc) + 3 <=
                         INST_COLUMNS ? " (" : "\n    (");
                inst_cat(line, sizeof(line), now[i].Desc);
                inst_cat(line, sizeof(line), ")");
            }
            inst_cat(raw, sizeof(raw), line);
            inst_cat(raw, sizeof(raw), "\n");
        }
        first = mwValue;
        n = 0;
    } else if (mwState == -1) {
        inst_cat(raw, sizeof(raw), "  the machine-wide value could not be "
                 "read; each device's own is shown\n");
    }
    for (i = 0; i < n; i++) {
        INST_U32 eff = now[i].State > 0 ? now[i].Value : def;
        if (i == 0) {
            first = eff;
        } else if (eff != first) {
            same = 0;
        }
        line[0] = '\0';
        inst_cat(line, sizeof(line), "  now: ");
        if (now[i].State > 0) {
            inst_cat_number(line, sizeof(line), now[i].Value);
        } else {
            inst_cat(line, sizeof(line), now[i].State == 0 ?
                     "not set, the install writes " :
                     "new install, the default ");
            inst_cat_number(line, sizeof(line), def);
        }
        if (now[i].Desc != NULL && now[i].Desc[0] != '\0') {
            /* A long name goes on its own line, under the value. */
            inst_cat(line, sizeof(line),
                     strlen(line) + strlen(now[i].Desc) + 3 <= INST_COLUMNS ?
                     " (" : "\n    (");
            inst_cat(line, sizeof(line), now[i].Desc);
            inst_cat(line, sizeof(line), ")");
        }
        inst_cat(raw, sizeof(raw), line);
        inst_cat(raw, sizeof(raw), "\n");
    }
    for (b = 0; b < 32UL; b++) {
        if ((mask & (1UL << b)) == 0) {
            continue;
        }
        count++;
        if (b < lo) {
            lo = b;
        }
        hi = b;
        if (b < INST_CHOICES && v->Choice[b] != NULL) {
            named = 1;
        }
    }
    if (named) {
        inst_cat(raw, sizeof(raw), "  allowed:\n");
        for (b = 0; b < 32UL; b++) {
            if ((mask & (1UL << b)) == 0) {
                continue;
            }
            inst_cat(raw, sizeof(raw), "    ");
            inst_cat_number(raw, sizeof(raw), b);
            if (b < INST_CHOICES && v->Choice[b] != NULL) {
                inst_cat(raw, sizeof(raw), "  ");
                inst_cat(raw, sizeof(raw), v->Choice[b]);
            }
            inst_cat(raw, sizeof(raw), "\n");
        }
    } else if (count >= 3 && hi - lo + 1 == (INST_U32)count) {
        inst_cat(raw, sizeof(raw), "  allowed: ");
        inst_cat_number(raw, sizeof(raw), lo);
        inst_cat(raw, sizeof(raw), " to ");
        inst_cat_number(raw, sizeof(raw), hi);
        inst_cat(raw, sizeof(raw), "\n");
    } else {
        inst_mask_text(mask, line, sizeof(line));
        inst_cat(raw, sizeof(raw), "  allowed: ");
        inst_cat(raw, sizeof(raw), line);
        inst_cat(raw, sizeof(raw), "\n");
    }
    inst_cat(raw, sizeof(raw), "  default: ");
    inst_cat_number(raw, sizeof(raw), def);
    if (!same) {
        inst_cat(raw, sizeof(raw), " (set now: not the same on every device)");
    } else if (first != def) {
        inst_cat(raw, sizeof(raw), " (set now: ");
        inst_cat_number(raw, sizeof(raw), first);
        inst_cat(raw, sizeof(raw), ")");
    }
    inst_cat(raw, sizeof(raw), "\n");
    if (v->Caution != NULL &&
        (v->CautionMask == 0 || (v->CautionMask & mask) != 0)) {
        inst_caution_text(index, mask, line, sizeof(line));
        inst_cat(raw, sizeof(raw), "  NOTE: ");
        inst_cat(raw, sizeof(raw), line);
        inst_cat(raw, sizeof(raw), "\n");
    }
    /* Record 23 section 8.4 (decision D12): the two values whose default
     * approves devices until the restart; not XhciCmSafeEject. */
    if (cmNewNote && (inst_eq(v->Name, "XhciCmMode") ||
                      inst_eq(v->Name, "XhciCmApprove"))) {
        inst_cat(raw, sizeof(raw), "  NOTE: a new install starts with the "
                 "default; your answer applies from the next restart. Until "
                 "then Thunderbolt devices are approved by the default: unplug "
                 "the dock until the restart if you answer 0.\n");
    }
    line[0] = '\0';
    if (same) {
        inst_cat_number(line, sizeof(line), first);
    } else {
        inst_cat(line, sizeof(line), "each keeps its own");
    }
    if (list) {
        inst_cat(raw, sizeof(raw), "  /list: not asked; Enter would give [");
        inst_cat(raw, sizeof(raw), line);
        inst_cat(raw, sizeof(raw), "].\n");
    } else {
        inst_cat(raw, sizeof(raw), "  Enter for [");
        inst_cat(raw, sizeof(raw), line);
        inst_cat(raw, sizeof(raw), "]: ");
    }
    inst_wrap_text(raw, out, cap);
    *enter = first;
    return same;
}

size_t inst_yesno_question(char *out, size_t cap, const char *question,
                           const char *yes, const char *no, int defaultYes)
{
    char raw[INST_RAW_CHARS];

    raw[0] = '\0';
    inst_cat(raw, sizeof(raw), question);
    inst_cat(raw, sizeof(raw), "\n  Y  ");
    inst_cat(raw, sizeof(raw), yes);
    inst_cat(raw, sizeof(raw), "\n  N  ");
    inst_cat(raw, sizeof(raw), no);
    inst_cat(raw, sizeof(raw), defaultYes ? "\n  Enter for [Y]: "
                                          : "\n  Enter for [N]: ");
    return inst_wrap_text(raw, out, cap);
}

size_t inst_dir_question(char *out, size_t cap, const char *toolName,
                         const char *setupName, const char *copyName,
                         const char *now, const char *def, int nt, int list)
{
    char raw[INST_RAW_CHARS];
    char key[32];
    size_t width = 0;

    raw[0] = '\0';
    if (toolName != NULL && strlen(toolName) > width) {
        width = strlen(toolName);
    }
    if (setupName != NULL && strlen(setupName) > width) {
        width = strlen(setupName);
    }
    if (copyName != NULL && strlen(copyName) > width) {
        width = strlen(copyName);
    }
    if (width > 16) {
        width = 16;
    }
    inst_cat(raw, sizeof(raw), "Install directory for the xHCI98 tools\n");
    if (toolName != NULL) {
        inst_copy(key, toolName, sizeof(key));
        while (strlen(key) < width && strlen(key) + 1 < sizeof(key)) {
            inst_cat(key, sizeof(key), " ");
        }
        inst_cat(raw, sizeof(raw), "  ");
        inst_cat(raw, sizeof(raw), key);
        inst_cat(raw, sizeof(raw), "  the xHCI98 USB Tree, which starts at "
                 "each login\n");
    }
    if (setupName != NULL) {
        inst_copy(key, setupName, sizeof(key));
        while (strlen(key) < width && strlen(key) + 1 < sizeof(key)) {
            inst_cat(key, sizeof(key), " ");
        }
        inst_cat(raw, sizeof(raw), "  ");
        inst_cat(raw, sizeof(raw), key);
        inst_cat(raw, sizeof(raw), "  this installer, which Add/Remove "
                 "Programs runs to remove it\n");
    }
    if (copyName != NULL) {
        inst_copy(key, copyName, sizeof(key));
        while (strlen(key) < width && strlen(key) + 1 < sizeof(key)) {
            inst_cat(key, sizeof(key), " ");
        }
        inst_cat(raw, sizeof(raw), "  ");
        inst_cat(raw, sizeof(raw), key);
        inst_cat(raw, sizeof(raw), "  this package's drivers, for the "
                 "hardware wizards\n");
    }
    if (now != NULL && now[0] != '\0') {
        inst_cat(raw, sizeof(raw), "  now: ");
        inst_cat(raw, sizeof(raw), now);
        inst_cat(raw, sizeof(raw), "\n");
    }
    if (!nt) {
        inst_cat(raw, sizeof(raw), "  Windows 98 SE and ME: a full path with "
                 "short names (8 characters, and a\n  3-character extension) "
                 "and no spaces, such as C:\\XHCI98.\n");
    }
    if (list) {
        inst_cat(raw, sizeof(raw), "  /list: not asked; Enter would give [");
        inst_cat(raw, sizeof(raw), def);
        inst_cat(raw, sizeof(raw), "].\n");
    } else {
        inst_cat(raw, sizeof(raw), "  Enter for [");
        inst_cat(raw, sizeof(raw), def);
        inst_cat(raw, sizeof(raw), "]: ");
    }
    return inst_wrap_text(raw, out, cap);
}

int inst_path_within(const char *path, const char *dir)
{
    size_t d = strlen(dir);

    while (d > 0 && (dir[d - 1] == '\\' || dir[d - 1] == '/')) {
        d--;
    }
    if (d == 0 || strlen(path) < d || !inst_eqn(path, dir, d)) {
        return 0;
    }
    return path[d] == '\0' || path[d] == '\\';
}

/* An 8.3 name of the characters a FAT short name takes, no space. */
static int inst_name83(const char *s, size_t len)
{
    size_t i;
    size_t base = 0;
    size_t ext = 0;
    int dot = 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '.') {
            if (dot) {
                return 0;
            }
            dot = 1;
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || strchr("_-$~!#&@^'(){}", c) != NULL)) {
            return 0;
        }
        if (dot) {
            ext++;
        } else {
            base++;
        }
    }
    return base >= 1 && base <= 8 && ext <= 3 && (!dot || ext >= 1);
}

int inst_dir_check(const char *path, int nt, const char *windir,
                   const char *pkgRoot, const char *allowed, char *out,
                   size_t cap)
{
    char p[INST_LINE_CHARS];
    size_t n;
    size_t i;
    size_t partStart;

    inst_copy(p, path, sizeof(p));
    inst_trim(p);
    inst_unquote(p);
    inst_trim(p);
    for (i = 0; p[i] != '\0'; i++) {
        if (p[i] == '/') {
            p[i] = '\\';
        }
    }
    n = strlen(p);
    while (n > 3 && p[n - 1] == '\\') {
        p[--n] = '\0';
    }
    if (n < 2 || inst_upper((unsigned char)p[0]) < 'A' ||
        inst_upper((unsigned char)p[0]) > 'Z' || p[1] != ':') {
        return INST_DIR_FORM;
    }
    if (n == 2 || (n == 3 && p[2] == '\\')) {
        return INST_DIR_ROOT;
    }
    if (p[2] != '\\') {
        return INST_DIR_FORM;
    }
    if (n > INST_DIR_CHARS) {
        return INST_DIR_LONG;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c >= 0x7F || strchr("\"*?<>|;", c) != NULL ||
            (c == ':' && i != 1)) {
            return INST_DIR_CHAR;
        }
    }
    p[0] = (char)inst_upper((unsigned char)p[0]);
    partStart = 3;
    for (i = 3; i <= n; i++) {
        if (i == n || p[i] == '\\') {
            const char *s = p + partStart;
            size_t len = i - partStart;
            if (len == 0) {
                return INST_DIR_FORM;
            }
            if ((len == 1 && s[0] == '.') ||
                (len == 2 && s[0] == '.' && s[1] == '.') ||
                s[0] == ' ' || s[len - 1] == ' ' || s[len - 1] == '.') {
                return INST_DIR_PART;
            }
            if (!nt && !inst_name83(s, len)) {
                return INST_DIR_NAME83;
            }
            partStart = i + 1;
        }
    }
    if (windir != NULL && windir[0] != '\0' && inst_path_within(p, windir) &&
        !(allowed != NULL && allowed[0] != '\0' && inst_eq(p, allowed))) {
        return INST_DIR_WINDOWS;
    }
    if (pkgRoot != NULL && pkgRoot[0] != '\0' && inst_path_within(p, pkgRoot)) {
        return INST_DIR_PACKAGE;
    }
    inst_copy(out, p, cap);
    return INST_DIR_OK;
}

const char *inst_dir_reason(int code)
{
    switch (code) {
    case INST_DIR_OK:
        return "accepted";
    case INST_DIR_ROOT:
        return "not a drive's top level: name a folder, such as C:\\XHCI98";
    case INST_DIR_CHAR:
        return "a path cannot hold \" * ? < > | ; or a second colon";
    case INST_DIR_PART:
        return "a folder name cannot be . or .., or begin or end with a "
               "space, or end with a dot";
    case INST_DIR_LONG:
        return "too long: 100 characters at most";
    case INST_DIR_WINDOWS:
        return "not inside the Windows directory";
    case INST_DIR_PACKAGE:
        return "not inside the package being installed";
    case INST_DIR_NAME83:
        return "Windows 98 SE and ME: each folder name 8 characters at most "
               "(and a 3-character extension), letters and digits, no spaces";
    default:
        return "give a full path on a drive, such as C:\\XHCI98";
    }
}

void inst_dir_default(const char *sysDrive, const char *windir, char *out,
                      size_t cap)
{
    char drive[3];
    const char *s = NULL;

    if (sysDrive != NULL && strlen(sysDrive) >= 2 && sysDrive[1] == ':') {
        s = sysDrive;
    } else if (windir != NULL && strlen(windir) >= 2 && windir[1] == ':') {
        s = windir;
    }
    drive[0] = s != NULL ? (char)inst_upper((unsigned char)s[0]) : 'C';
    drive[1] = ':';
    drive[2] = '\0';
    if (drive[0] < 'A' || drive[0] > 'Z') {
        drive[0] = 'C';
    }
    inst_copy(out, drive, cap);
    inst_cat(out, cap, "\\XHCI98");
}

/* ======================================================================== */
/* The 98 SE and ME helper (record 27 sections 4 and 5.4; task 42-A.2)      */
/* ======================================================================== */

/* A full path whose every part is an 8.3 name with no space: X:\A\B.C. */
int inst_di_path(const char *p)
{
    const char *part;
    const char *end;

    if (p == NULL || strlen(p) < 4 || p[1] != ':' || p[2] != '\\' ||
        inst_upper((unsigned char)p[0]) < 'A' ||
        inst_upper((unsigned char)p[0]) > 'Z') {
        return 0;
    }
    for (part = p + 3; *part != '\0'; part = *end != '\0' ? end + 1 : end) {
        end = strchr(part, '\\');
        if (end == NULL) {
            end = part + strlen(part);
        }
        if (!inst_name83(part, (size_t)(end - part))) {
            return 0;
        }
    }
    return 1;
}

static const char *inst_di_job_word(int job)
{
    return job == INST_DI_INSTALL ? "install" :
           job == INST_DI_REMOVE ? "remove" : "probe";
}

int inst_di_jobpath(char *out, size_t cap, const char *dir)
{
    char t[INST_LINE_CHARS];
    size_t n;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (dir == NULL || strlen(dir) < 3 || strlen(dir) + 16 >= sizeof(t)) {
        return 0;
    }
    inst_copy(t, dir, sizeof(t));
    n = strlen(t);
    if (t[n - 1] != '\\') {
        inst_cat(t, sizeof(t), "\\");
    }
    inst_cat(t, sizeof(t), INST_DI_JOB_NAME);
    if (strlen(t) > INST_DI_JOB_PATH_CHARS || !inst_di_path(t)) {
        /* Too long, or not 8.3: the root of the install directory's
         * drive (record 27 section 4.1). */
        t[0] = dir[0];
        t[1] = ':';
        t[2] = '\\';
        t[3] = '\0';
        inst_cat(t, sizeof(t), INST_DI_JOB_ROOT_NAME);
        if (!inst_di_path(t)) {
            return 0;
        }
    }
    if (strlen(t) + 1 > cap) {
        return 0;
    }
    inst_copy(out, t, cap);
    return 1;
}

int inst_di_cmdline(char *out, size_t cap, const char *version,
                    const char *jobFile)
{
    char t[INST_LINE_CHARS];

    if (cap > 0) {
        out[0] = '\0';
    }
    if (version == NULL || version[0] == '\0' || strlen(version) > 31 ||
        strchr(version, ' ') != NULL || strchr(version, '"') != NULL ||
        jobFile == NULL || strlen(jobFile) > INST_DI_JOB_PATH_CHARS ||
        !inst_di_path(jobFile)) {
        return 0;
    }
    t[0] = '\0';
    inst_cat(t, sizeof(t), version);
    inst_cat(t, sizeof(t), " ");
    inst_cat(t, sizeof(t), jobFile);
    if (strlen(t) > INST_DI_TAIL_CHARS || strlen(t) + 1 > cap) {
        return 0;
    }
    inst_copy(out, t, cap);
    return 1;
}

/* An operand of the job file: no space, no tab, no control character. */
static int inst_di_operand(const char *s, size_t max)
{
    const unsigned char *p;

    if (s == NULL || s[0] == '\0' || strlen(s) > max) {
        return 0;
    }
    for (p = (const unsigned char *)s; *p != '\0'; p++) {
        if (*p <= ' ' || *p >= 0x7F) {
            return 0;
        }
    }
    return 1;
}

int inst_di_jobfile(char *out, size_t cap, int job, const char *result,
                    unsigned long hwnd, const char *devId, const char *inf,
                    int force, int quiet)
{
    char t[INST_DI_JOB_BYTES + 64];
    size_t n;

    if (cap > 0) {
        out[0] = '\0';
    }
    if ((job != INST_DI_PROBE && job != INST_DI_INSTALL &&
         job != INST_DI_REMOVE) ||
        result == NULL || strlen(result) > 255 || !inst_di_path(result) ||
        hwnd > 65535UL) {
        return 0;
    }
    if (job != INST_DI_PROBE && !inst_di_operand(devId, 200)) {
        return 0;
    }
    if (job == INST_DI_INSTALL &&
        (inf == NULL || strlen(inf) > 255 || !inst_di_path(inf) ||
         (n = strlen(inf)) < 4 || !inst_eq(inf + n - 4, ".INF"))) {
        return 0;
    }
    t[0] = '\0';
    inst_cat(t, sizeof(t), "XHCIDI-JOB 1\r\nJOB ");
    inst_cat(t, sizeof(t), inst_di_job_word(job));
    inst_cat(t, sizeof(t), "\r\nRESULT ");
    inst_cat(t, sizeof(t), result);
    inst_cat(t, sizeof(t), "\r\nHWND ");
    inst_cat_number(t, sizeof(t), (INST_U32)hwnd);
    inst_cat(t, sizeof(t), "\r\n");
    if (job != INST_DI_PROBE) {
        inst_cat(t, sizeof(t), "DEVICE ");
        inst_cat(t, sizeof(t), devId);
        inst_cat(t, sizeof(t), "\r\n");
    }
    if (job == INST_DI_INSTALL) {
        inst_cat(t, sizeof(t), "INF ");
        inst_cat(t, sizeof(t), inf);
        inst_cat(t, sizeof(t), "\r\n");
        if (force || quiet) {
            inst_cat(t, sizeof(t), "FLAGS");
            if (force) {
                inst_cat(t, sizeof(t), " force");
            }
            if (quiet) {
                inst_cat(t, sizeof(t), " quiet");
            }
            inst_cat(t, sizeof(t), "\r\n");
        }
    }
    inst_cat(t, sizeof(t), "END\r\n");
    if (strlen(t) > INST_DI_JOB_BYTES || strlen(t) + 1 > cap) {
        return 0;
    }
    inst_copy(out, t, cap);
    return 1;
}

/* The next line of text into line (CR and LF removed); NULL at the end. */
static const char *inst_di_line(const char *p, char *line, size_t cap)
{
    size_t n = 0;

    if (p == NULL || *p == '\0') {
        return NULL;
    }
    while (*p != '\0' && *p != '\n') {
        if (*p != '\r' && n + 1 < cap) {
            line[n++] = *p;
        }
        p++;
    }
    line[n] = '\0';
    return *p == '\n' ? p + 1 : p;
}

/* The index-th space-separated word of a line. */
static int inst_di_word(const char *line, int index, char *out, size_t cap)
{
    const char *p = line;
    size_t n;
    int i = 0;

    for (;;) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            if (cap > 0) {
                out[0] = '\0';
            }
            return 0;
        }
        n = strcspn(p, " ");
        if (i == index) {
            if (n + 1 > cap) {
                n = cap - 1;
            }
            memcpy(out, p, n);
            out[n] = '\0';
            return 1;
        }
        p += n;
        i++;
    }
}

/* What follows the index-th word, trimmed. */
static const char *inst_di_rest(const char *line, int index)
{
    const char *p = line;
    int i;

    for (i = 0; i <= index; i++) {
        while (*p == ' ') {
            p++;
        }
        p += strcspn(p, " ");
    }
    while (*p == ' ') {
        p++;
    }
    return p;
}

int inst_di_ended(const char *text)
{
    char line[INST_LINE_CHARS];
    char w[16];
    const char *p = text;

    while ((p = inst_di_line(p, line, sizeof(line))) != NULL) {
        if (inst_di_word(line, 0, w, sizeof(w)) && strcmp(w, "END") == 0) {
            return 1;
        }
    }
    return 0;
}

int inst_di_parse(const char *text, const char *version, int job,
                  const char *devId, const char *inf, INST_DI_RESULT *r)
{
    char line[INST_LINE_CHARS];
    char w[INST_LINE_CHARS];
    char word[64];
    const char *p = text;
    int lines = 0;
    int versionOk = 0;
    int jobOk = 0;
    int results = 0;
    int after = 0;
    INST_U32 count = 0;
    int verdict = INST_DI_V_NONE;

    memset(r, 0, sizeof(*r));
    word[0] = '\0';
    if (text == NULL) {
        return INST_DI_V_NONE;
    }
    while ((p = inst_di_line(p, line, sizeof(line))) != NULL) {
        if (r->Ended) {
            if (line[0] != '\0') {
                after = 1;
            }
            continue;
        }
        if (!inst_di_word(line, 0, w, sizeof(w))) {
            lines++;
            continue;
        }
        if (strcmp(w, "END") == 0) {
            r->Ended = 1;
            if (!inst_di_word(line, 1, w, sizeof(w)) || !inst_number(w, &count)) {
                count = 0xFFFFFFFFUL;
            }
            continue;
        }
        lines++;
        if (lines == 1 && strcmp(w, "XHCIDI") == 0) {
            versionOk = inst_di_word(line, 1, w, sizeof(w)) &&
                        strcmp(w, version) == 0;
        } else if (strcmp(w, "JOB") == 0) {
            jobOk = inst_di_word(line, 1, w, sizeof(w)) &&
                    inst_eq(w, inst_di_job_word(job));
            if (jobOk && job != INST_DI_PROBE) {
                jobOk = inst_di_word(line, 2, w, sizeof(w)) &&
                        inst_eq(w, devId);
            }
            if (jobOk && job == INST_DI_INSTALL) {
                jobOk = inst_di_word(line, 3, w, sizeof(w)) &&
                        inst_eq(w, inf);
            }
        } else if (strcmp(w, "RESULT") == 0) {
            results++;
            inst_di_word(line, 1, word, sizeof(word));
            if (inst_eq(word, "failed")) {
                inst_di_word(line, 2, r->Error, sizeof(r->Error));
                if (inst_di_word(line, 3, w, sizeof(w))) {
                    inst_number(w, &r->Number);
                }
                inst_copy(r->Detail, inst_di_rest(line, 1), sizeof(r->Detail));
            } else if (inst_eq(word, "refused")) {
                inst_copy(r->Detail, inst_di_rest(line, 1), sizeof(r->Detail));
            }
        } else if (strcmp(w, "FLAGS") == 0) {
            char hex[16];
            inst_copy(hex, "0x", sizeof(hex));
            if (inst_di_word(line, 1, w, sizeof(w)) && strlen(w) <= 8) {
                inst_cat(hex, sizeof(hex), w);
                r->HaveFlags = inst_number(hex, &r->Flags);
            }
        }
    }
    r->Lines = lines;
    if (results == 1 && inst_eq(word, "refused")) {
        /* The helper refused before any call, whatever its version line. */
        r->Verdict = INST_DI_V_REFUSED;
        return r->Verdict;
    }
    if (!r->Ended || after || count != (INST_U32)lines || !versionOk ||
        !jobOk || results > 1) {
        return INST_DI_V_NONE;
    }
    if (job == INST_DI_PROBE) {
        /* The probe's one verdict word (the helper writes "probed"): any
         * other, or none, is no clean probe (record 27 section 4.2). */
        verdict = (results == 1 && inst_eq(word, "probed")) ?
                  INST_DI_V_PROBED : INST_DI_V_NONE;
    } else if (results == 0) {
        verdict = INST_DI_V_NONE;
    } else if (job == INST_DI_INSTALL) {
        verdict = inst_eq(word, "installed") ? INST_DI_V_INSTALLED :
                  inst_eq(word, "installed-restart") ?
                      INST_DI_V_INSTALLED_RESTART :
                  inst_eq(word, "cancelled") ? INST_DI_V_CANCELLED :
                  inst_eq(word, "no-match") ? INST_DI_V_NO_MATCH :
                  inst_eq(word, "not-present") ? INST_DI_V_NOT_PRESENT :
                  inst_eq(word, "failed") ? INST_DI_V_FAILED : INST_DI_V_NONE;
    } else {
        verdict = inst_eq(word, "removed") ? INST_DI_V_REMOVED :
                  inst_eq(word, "removed-restart") ? INST_DI_V_REMOVED_RESTART :
                  inst_eq(word, "cancelled") ? INST_DI_V_CANCELLED :
                  inst_eq(word, "not-found") ? INST_DI_V_NOT_FOUND :
                  inst_eq(word, "failed") ? INST_DI_V_FAILED : INST_DI_V_NONE;
    }
    r->Verdict = verdict;
    return verdict;
}

int inst_di_outcome(int job, int verdict)
{
    (void)job;
    switch (verdict) {
    case INST_DI_V_INSTALLED:
    case INST_DI_V_REMOVED:
    case INST_DI_V_PROBED:
        return INST_DI_OUT_OK;
    case INST_DI_V_INSTALLED_RESTART:
    case INST_DI_V_REMOVED_RESTART:
        return INST_DI_OUT_RESTART;
    case INST_DI_V_CANCELLED:
    case INST_DI_V_NO_MATCH:
        return INST_DI_OUT_STEPS;
    case INST_DI_V_NOT_PRESENT:
    case INST_DI_V_NOT_FOUND:
        return INST_DI_OUT_ABSENT;
    case INST_DI_V_REFUSED:
        return INST_DI_OUT_REFUSED;
    default:
        return INST_DI_OUT_FAILED;   /* failed, or the helper did not finish */
    }
}

int inst_di_unfinished(int verdict)
{
    switch (verdict) {
    case INST_DI_V_INSTALLED:
    case INST_DI_V_INSTALLED_RESTART:
    case INST_DI_V_CANCELLED:
    case INST_DI_V_NO_MATCH:
    case INST_DI_V_NOT_PRESENT:
    case INST_DI_V_FAILED:
    case INST_DI_V_REFUSED:
    case INST_DI_V_REMOVED:
    case INST_DI_V_REMOVED_RESTART:
    case INST_DI_V_NOT_FOUND:
    case INST_DI_V_PROBED:
        return 0;
    default:
        return 1;
    }
}

const char *inst_di_verdict_text(int verdict)
{
    switch (verdict) {
    case INST_DI_V_INSTALLED:           return "installed";
    case INST_DI_V_INSTALLED_RESTART:   return "installed, restart pending";
    case INST_DI_V_CANCELLED:           return "cancelled in a Windows dialog";
    case INST_DI_V_NO_MATCH:            return "the INF has no driver for it";
    case INST_DI_V_NOT_PRESENT:         return "not present";
    case INST_DI_V_FAILED:              return "failed";
    case INST_DI_V_REFUSED:             return "refused by the helper";
    case INST_DI_V_REMOVED:             return "removed";
    case INST_DI_V_REMOVED_RESTART:     return "removed, restart pending";
    case INST_DI_V_NOT_FOUND:           return "not found";
    case INST_DI_V_PROBED:              return "the helper runs here";
    default:                            return "the helper did not finish";
    }
}

/* ======================================================================== */
/* The installer's own files and the uninstall (record 26 sections 6.2, 9)  */
/* ======================================================================== */

const char *inst_self_file(int arch, int which)
{
    if (arch == INST_ARCH_AMD64) {
        return which == INST_OWN_WINDOW ? "XHCIWZ64.EXE" : "XHCIIN64.EXE";
    }
    return which == INST_OWN_WINDOW ? "XHCIWIZ.EXE" : "XHCIINST.EXE";
}

int inst_arp_cm_only(int xhciService, int cmService, int runCmOnly)
{
    return xhciService == 0 && (cmService == 1 || runCmOnly);
}

void inst_arp_name(char *out, size_t cap, const char *version, int incomplete,
                   int cmOnly)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    inst_cat(out, cap, cmOnly ? "xHCI98 Thunderbolt 3 / USB4 connection "
                                "manager "
                              : "xHCI98 USB 3.x drivers ");
    inst_cat(out, cap, version);
    if (incomplete) {
        inst_cat(out, cap, " (incomplete: run again)");
    }
}

int inst_arp_command(char *out, size_t cap, const char *dir, const char *exe,
                     int nt)
{
    char t[INST_LINE_CHARS];
    size_t n;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (dir == NULL || exe == NULL || dir[0] == '\0' ||
        strchr(dir, '"') != NULL || strchr(exe, '"') != NULL ||
        strlen(dir) + strlen(exe) + 16 >= sizeof(t)) {
        return 0;
    }
    if (!nt && strchr(dir, ' ') != NULL) {
        return 0;
    }
    t[0] = '\0';
    if (nt) {
        inst_cat(t, sizeof(t), "\"");
    }
    inst_cat(t, sizeof(t), dir);
    n = strlen(t);
    if (n > 0 && t[n - 1] != '\\') {
        inst_cat(t, sizeof(t), "\\");
    }
    inst_cat(t, sizeof(t), exe);
    if (nt) {
        inst_cat(t, sizeof(t), "\"");
    }
    inst_cat(t, sizeof(t), " /uninstall");
    if (strlen(t) + 1 > cap) {
        return 0;
    }
    inst_copy(out, t, cap);
    return 1;
}

int inst_uninstall_route(int selfInInstallDir, int fromGiven,
                         int fromIsInstallDir)
{
    if (fromGiven) {
        return (fromIsInstallDir && !selfInInstallDir) ? INST_UN_RUN
                                                       : INST_UN_REFUSE;
    }
    return selfInInstallDir ? INST_UN_RELAUNCH : INST_UN_RUN;
}

int inst_relaunch_cmd(char *out, size_t cap, const char *exe,
                      const char *fromDir, const char *log, int nopause,
                      int yes)
{
    char t[INST_LINE_CHARS * 2];

    if (cap > 0) {
        out[0] = '\0';
    }
    if (exe == NULL || fromDir == NULL || exe[0] == '\0' ||
        fromDir[0] == '\0' || strchr(exe, '"') != NULL ||
        strchr(fromDir, '"') != NULL ||
        (log != NULL && strchr(log, '"') != NULL) ||
        strlen(exe) + strlen(fromDir) +
            (log != NULL ? strlen(log) : 0) + 64 >= sizeof(t)) {
        return 0;
    }
    t[0] = '\0';
    inst_cat(t, sizeof(t), "\"");
    inst_cat(t, sizeof(t), exe);
    inst_cat(t, sizeof(t), "\" /uninstall /from \"");
    inst_cat(t, sizeof(t), fromDir);
    inst_cat(t, sizeof(t), "\"");
    if (log != NULL && log[0] != '\0') {
        inst_cat(t, sizeof(t), " /log \"");
        inst_cat(t, sizeof(t), log);
        inst_cat(t, sizeof(t), "\"");
    }
    if (nopause) {
        inst_cat(t, sizeof(t), " /nopause");
    }
    if (yes) {
        inst_cat(t, sizeof(t), " /yes");
    }
    if (strlen(t) + 1 > cap) {
        return 0;
    }
    inst_copy(out, t, cap);
    return 1;
}

const char *inst_window_refusal(const INST_OPTS *opts, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (inst_switch(argv[i], "console")) {
            return "/console is INSTALL.BAT's: run INSTALL /console ... for "
                   "the console tool.";
        }
    }
    if (opts->Yes || opts->List || opts->NoPause || opts->Devices ||
        opts->Eject || opts->Dir[0] != '\0') {
        return "that switch is the console tool's: use INSTALL /console "
               "with it.";
    }
    return NULL;
}

/* ======================================================================== */
/* The window's texts (record 26 section 5)                                  */
/* ======================================================================== */

const INST_PAGE_TEXT instPageTexts[] = {
    { INST_PT_WELCOME,
      "This installs or updates the xHCI98 USB 3.x drivers on this "
      "computer, puts the xHCI98 tools in a folder you choose, and asks "
      "you to restart Windows at the end. Nothing is changed until you "
      "click Install on the Summary page. Help opens the readme.",
      284, 58 },
    { INST_PT_WAIT, "Please wait: reading this computer's devices ...",
      284, 54 },
    { INST_PT_FOUND,
      "The devices this package's drivers are for, and what will be done:",
      284, 10 },
    { INST_PT_DRIVERS,
      "xHCI98 USB 3.x host controller driver (xhci98.sys)", 270, 10 },
    { INST_PT_DRIVERS_T,
      "The controller, its hubs and devices, and Bulk-Only disks.",
      272, 16 },
    { INST_PT_UAS, "UAS disk driver (xhciuas.sys)", 258, 10 },
    { INST_PT_UAS_T,
      "Untick only if you use another UAS driver. Bulk-Only disks are not "
      "affected; XhciForceBulkOnly also avoids UAS.",
      260, 18 },
    { INST_PT_TB,
      "Thunderbolt 3 / USB4 support (experimental) (xhcicm.sys)", 270, 10 },
    { INST_PT_TB_T,
      "A Thunderbolt 3 / USB4 controller was found. With this, a device "
      "plugged into its port can reach the computer's memory: see "
      "XhciCmApprove.",
      272, 26 },
    { INST_PT_TB_ABSENT,
      "No Thunderbolt 3 / USB4 controller was found (some laptops show it "
      "only with a device plugged in or a BIOS setting). Tick it only if "
      "this computer has such a port.",
      272, 26 },
    { INST_PT_TRAY, "xHCI98 USB Tree, started at login", 270, 10 },
    { INST_PT_TRAY_T,
      "The tray tool that lists the USB devices and removes them safely.",
      272, 10 },
    { INST_PT_DIR, "The folder for the xHCI98 tools and this installer:",
      284, 10 },
    { INST_PT_DIR_9X,
      "Windows 98 SE and ME: the package's drivers go there too. Each "
      "folder name 8 characters at most, no spaces.",
      284, 18 },
    { INST_PT_USB,
      "The USB settings. A value left as it is is not written; one you "
      "change is written machine-wide, for every controller.",
      284, 18 },
    { INST_PT_TBSET,
      "The Thunderbolt and USB4 settings. A value left as it is is not "
      "written; one you change is written machine-wide.",
      284, 18 },
    { INST_PT_TB_LATER,
      "No controller is present now: these take effect when it appears.",
      218, 20 },
    { INST_PT_ADVANCED,
      "For the release notes' advice and a tester's use. A value left as "
      "it is is not written; one you change is written machine-wide.",
      304, 18 },
    { INST_PT_SUMMARY,
      "Nothing has been changed yet. Click Install to do the following.",
      284, 18 },
    { INST_PT_PROGRESS,
      "Windows may open windows of its own: answer them.", 284, 10 },
    { INST_PT_STEPS,
      "These devices could not be installed by the installer. Do their "
      "steps after the restart; save them to a file first.",
      284, 20 },
    { INST_PT_NOW, "Restart Windows now", 284, 10 },
    { INST_PT_LATER, "Restart Windows later", 284, 10 },
    { INST_PT_S98, "Stor98, the bundled NVMe driver (a separate project)",
      258, 10 },
    { INST_PT_S98_T,
      "For NVMe drives behind a Thunderbolt or USB4 tunnel; the installer "
      "selects tunnel drives only.",
      260, 18 },
    { INST_PT_S98SET,
      "Stor98's settings (its readme, SETTINGS). A row left as it is writes "
      "nothing; Stor98 keeps its built-in default.",
      284, 18 },
    { INST_PT_S98_UNUSED, instS98DisconnectText, 284, 10 },
    { INST_PT_SHUT_NOW, "Shut down now, then start Windows again", 284, 10 },
    { INST_PT_SHUT_LATER, "Shut down later", 284, 10 },
    { INST_PT_WELCOME_CM,
      "This installs or updates the xHCI98 Thunderbolt 3 / USB4 connection "
      "manager on this computer, puts the xHCI98 tools in a folder you "
      "choose, and asks you to restart Windows at the end. Windows' own USB "
      "3 driver keeps running the USB controllers. Nothing is changed until "
      "you click Install on the Summary page. Help opens the readme.",
      284, 58 },
    { INST_PT_DRIVERS_T8,
      "Not offered on Windows 8 and 8.1: Windows' own USB 3 driver runs the "
      "USB controllers there.",
      272, 16 },
    { INST_PT_DRIVERS_OFF,
      "Left out: the connection manager alone. USB drivers already "
      "installed stay as they are; the uninstall removes all of them.",
      272, 16 },
    { INST_PT_UN_TITLE_CM, "Remove the xHCI98 connection manager", 284, 10 },
    { INST_PT_UN_WARN_CM,
      "This removes the xHCI98 Thunderbolt 3 / USB4 connection manager from "
      "Windows. Devices connected through a Thunderbolt or USB4 port stop "
      "working when it is removed. Close every program that uses one, and "
      "save your work, before you go on.",
      284, 50 },
    { INST_PT_UN_AGREE_CM,
      "I have closed every program that uses a device connected through a "
      "Thunderbolt or USB4 port.",
      270, 20 },
    { INST_PT_UN_TITLE, "Remove the xHCI98 USB 3.x drivers", 284, 10 },
    { INST_PT_UN_WARN,
      "This removes the xhci98 drivers from Windows. On a machine whose "
      "only USB controller is xHCI, the USB keyboard and mouse stop working "
      "when the controller's driver is removed, and stay dead until another "
      "driver is installed. Have a PS/2 keyboard, a laptop's built-in one, "
      "or a USB controller another driver runs, before you go on.",
      284, 50 },
    { INST_PT_UN_AGREE,
      "I have a keyboard and mouse that do not go through this driver, or "
      "I accept that the USB ones stop working.",
      270, 20 },
    { INST_PT_UN_SUMMARY,
      "Nothing has been changed yet. Click Remove to take away every "
      "driver of this package, its tools and its records.",
      284, 18 },
    { INST_PT_GATE,
      "If Windows has a hardware wizard open (Found New Hardware, Add New "
      "Hardware), finish it first: Next through Finish, even when it finds "
      "no driver. Do not cancel the wizard: that hides its device until "
      "Windows restarts. OK goes on; Cancel stops here.",
      0, 0 }
};
const int instPageTextCount =
    (int)(sizeof(instPageTexts) / sizeof(instPageTexts[0]));

const char *inst_page_text(int id)
{
    int i;

    for (i = 0; i < instPageTextCount; i++) {
        if (instPageTexts[i].Id == id) {
            return instPageTexts[i].Text;
        }
    }
    return "";
}

INST_U32 inst_text_lines(const char *text, INST_U32 cols)
{
    INST_U32 lines = 0;
    INST_U32 col = 0;
    const char *p = text;

    if (cols == 0) {
        return 0;
    }
    while (*p != '\0') {
        size_t w;
        if (*p == '\n') {
            lines++;
            col = 0;
            p++;
            continue;
        }
        if (*p == ' ') {
            p++;
            if (col > 0 && col < cols) {
                col++;
            }
            continue;
        }
        w = strcspn(p, " \n");
        if (col > 0 && col + w > cols) {
            lines++;
            col = 0;
        }
        while (w > cols) {
            lines++;
            w -= cols;
            p += cols;
        }
        col += (INST_U32)w;
        p += w;
    }
    return lines + (col > 0 ? 1 : 0);
}

INST_U32 inst_page_cols(INST_U32 widthDlu)
{
    /* A dialog unit across is a quarter of the font's average character;
     * the margin keeps a proportional font's wider letters inside. The
     * window's self-test measures the real rendering. */
    return widthDlu >= 32 ? widthDlu / 4 - 3 : 1;
}

INST_U32 inst_page_rows(INST_U32 heightDlu)
{
    return heightDlu / 8;
}

/* ======================================================================== */
/* Stor98, the bundled NVMe driver (record 26 sections 5.6 and 6.3)          */
/* ======================================================================== */

/* Copied from Stor98's readme.txt, "SETTINGS", at INST_S98_TABLE_VERSION;
 * test_inst holds each row to test\stor98-settings.txt, that table. */
const INST_S98_VALUE instS98Values[INST_S98_COUNT] = {
    { "StorWriteThrough", "writethrough", INST_S98_WORDS, 1, "",
      "auto", "auto", 0UL, 0UL, 0UL, 0, NULL,
      { "auto", "off", "cacheoff", "fua" },
      { "the default: cacheoff on ME, off elsewhere",
        "leaves the drive as it is",
        "turns the drive's write cache off (fua if it refuses)",
        "marks every write Force Unit Access" },
      "how writes reach the drive's media" },
    { "StorPollMode", "pollmode", INST_S98_NUMBER, 1, "",
      "0", "0", 0UL, 1UL, 1UL, 0, NULL,
      { NULL, NULL, NULL, NULL },
      { "poll only without an interrupt", "always poll", NULL, NULL },
      "when the driver polls the controller for completed requests" },
    { "StorMaxTransferKB", "maxtransferkb", INST_S98_NUMBER, 0, "KiB",
      "512", "256", 64UL, 512UL, 4UL, 0, NULL,
      { NULL, NULL, NULL, NULL }, { NULL, NULL, NULL, NULL },
      "the largest request Windows sends the driver" },
    { "StorPollBusyUs", "pollbusyus", INST_S98_NUMBER, 0, "us",
      "1000", "1000", 1000UL, 100000UL, 1UL, 1, "the default",
      { NULL, NULL, NULL, NULL }, { NULL, NULL, NULL, NULL },
      "the polling interval while requests are outstanding" },
    { "StorPollIdleMs", "pollidlems", INST_S98_NUMBER, 0, "ms",
      "1000", "1000", 100UL, 60000UL, 1UL, 1, "no polling when idle",
      { NULL, NULL, NULL, NULL }, { NULL, NULL, NULL, NULL },
      "the polling interval while the drive is idle" },
    { "StorPollSpinUs", "pollspinus", INST_S98_NUMBER, 0, "us",
      "0", "500", 0UL, 500UL, 1UL, 0, NULL,
      { NULL, NULL, NULL, NULL }, { NULL, NULL, NULL, NULL },
      "how long to check for a completion right after a request" },
    { NULL, "gone", INST_S98_NUMBER, 0, "ms",
      NULL, "5000", 1UL, 600000UL, 1UL, 0, NULL,
      { NULL, NULL, NULL, NULL }, { NULL, NULL, NULL, NULL },
      "how long held requests wait for proof the drive is gone" },
    { NULL, "idle", INST_S98_NUMBER, 0, "ms",
      NULL, "1000", 10UL, 4294967295UL, 1UL, 0, NULL,
      { NULL, NULL, NULL, NULL }, { NULL, NULL, NULL, NULL },
      "the write-idle time before a flush" }
};

int inst_s98_applies(int i, int nt)
{
    if (i < 0 || i >= INST_S98_COUNT) {
        return 0;
    }
    return nt ? instS98Values[i].KeyNt != NULL
              : instS98Values[i].Name9x != NULL;
}

int inst_s98_greyed(int i, int nt)
{
    return !inst_s98_applies(i, nt) && inst_s98_applies(i, !nt);
}

const char *inst_s98_only(int i)
{
    if (i < 0 || i >= INST_S98_COUNT) {
        return NULL;
    }
    if (instS98Values[i].Name9x == NULL) {
        return "Windows 2000 to 7 only";
    }
    return instS98Values[i].KeyNt == NULL ? "Windows 98 SE and ME only" : NULL;
}

const char *inst_s98_default(int i, int nt)
{
    if (!inst_s98_applies(i, nt)) {
        return "";
    }
    return nt ? instS98Values[i].DefNt : instS98Values[i].Def9x;
}

void inst_s98_default_text(int i, int nt, int me, char *out, size_t cap)
{
    const INST_S98_VALUE *v;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (!inst_s98_applies(i, nt)) {
        return;
    }
    v = &instS98Values[i];
    inst_cat(out, cap, inst_s98_default(i, nt));
    if (i == INST_S98_WRITETHROUGH) {
        /* auto is a word for two behaviours: say which runs here. */
        inst_cat(out, cap, me ? " (here: cacheoff)" : " (here: off)");
    } else if (v->Unit[0] != '\0') {
        inst_cat(out, cap, " ");
        inst_cat(out, cap, v->Unit);
    }
}

/* The range's top on this system: the transfer size is bounded by its own
 * default, which differs. */
static INST_U32 inst_s98_hi(int i, int nt)
{
    if (i == INST_S98_MAXTRANSFERKB) {
        return nt ? 256UL : 512UL;
    }
    return instS98Values[i].Hi;
}

int inst_s98_check(int i, int nt, const char *text, char *out, size_t cap,
                   const char **why)
{
    const INST_S98_VALUE *v;
    char t[32];
    INST_U32 n;
    INST_U32 hi;
    int k;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (why != NULL) {
        *why = NULL;
    }
    if (!inst_s98_applies(i, nt)) {
        if (why != NULL) {
            *why = "not a setting of Stor98 on this system";
        }
        return 0;
    }
    v = &instS98Values[i];
    if (text == NULL || strlen(text) >= sizeof(t) || text[0] == '\0') {
        if (why != NULL) {
            *why = "no value";
        }
        return 0;
    }
    inst_copy(t, text, sizeof(t));
    inst_trim(t);
    if (t[0] == '\0') {
        if (why != NULL) {
            *why = "no value";
        }
        return 0;
    }
    if (v->Kind == INST_S98_WORDS) {
        for (k = 0; k < INST_S98_WORDS_MAX && v->Words[k] != NULL; k++) {
            if (inst_eq(t, v->Words[k])) {
                inst_copy(out, v->Words[k], cap);
                return 1;
            }
        }
        if (why != NULL) {
            *why = "not one of auto, off, cacheoff, fua";
        }
        return 0;
    }
    /* Decimal digits only, to 4294967295 ("10 and up" has no other top
     * than the DWORD's). */
    n = 0;
    for (k = 0; t[k] != '\0'; k++) {
        INST_U32 dgt = (INST_U32)(t[k] - '0');
        if (t[k] < '0' || t[k] > '9' ||
            n > (0xFFFFFFFFUL - dgt) / 10UL) {
            if (why != NULL) {
                *why = (t[k] < '0' || t[k] > '9') ? "not a decimal number"
                                                  : "outside the range "
                                                    "Stor98's readme gives";
            }
            return 0;
        }
        n = n * 10UL + dgt;
    }
    hi = inst_s98_hi(i, nt);
    if (!(n == 0UL && v->ZeroOk) &&
        (n < v->Lo || n > hi || (v->Step > 1UL && n % v->Step != 0UL))) {
        if (why != NULL) {
            *why = "outside the range Stor98's readme gives";
        }
        return 0;
    }
    out[0] = '\0';
    inst_cat_number(out, cap, n);
    return 1;
}

/* The next pair of a DriverParameter string from *p: its bounds, or 0 at
 * the end. Empty segments are skipped. */
static int inst_s98_pair(const char **p, const char **start, size_t *len)
{
    const char *s = *p;
    const char *e;

    while (*s == ';') {
        s++;
    }
    if (*s == '\0') {
        *p = s;
        return 0;
    }
    e = strchr(s, ';');
    if (e == NULL) {
        e = s + strlen(s);
    }
    *start = s;
    *len = (size_t)(e - s);
    *p = e;
    return 1;
}

/* 1 when the pair [s, s+len) is key's: "key=" at its start, whatever the
 * case and spaces around the key. */
static int inst_s98_pair_is(const char *s, size_t len, const char *key,
                            const char **value)
{
    size_t kl = strlen(key);
    size_t i = 0;

    while (i < len && s[i] == ' ') {
        i++;
    }
    if (len - i < kl || !inst_eqn(s + i, key, kl)) {
        return 0;
    }
    i += kl;
    while (i < len && s[i] == ' ') {
        i++;
    }
    if (i >= len || s[i] != '=') {
        return 0;
    }
    if (value != NULL) {
        *value = s + i + 1;
    }
    return 1;
}

int inst_s98_param_get(const char *param, const char *key, char *out,
                       size_t cap)
{
    const char *p = param != NULL ? param : "";
    const char *s;
    const char *v;
    size_t len;
    size_t n;

    if (cap > 0) {
        out[0] = '\0';
    }
    while (inst_s98_pair(&p, &s, &len)) {
        if (inst_s98_pair_is(s, len, key, &v)) {
            n = len - (size_t)(v - s);
            if (n + 1 > cap) {
                return 0;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            inst_trim(out);
            return 1;
        }
    }
    return 0;
}

int inst_s98_param_set(const char *param, const char *key, const char *value,
                       char *out, size_t cap)
{
    const char *p = param != NULL ? param : "";
    const char *s;
    size_t len;
    size_t n = 0;
    int done = 0;
    char pair[64];
    size_t pl;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    pair[0] = '\0';
    if (value != NULL) {
        inst_cat(pair, sizeof(pair), key);
        inst_cat(pair, sizeof(pair), "=");
        inst_cat(pair, sizeof(pair), value);
        for (pl = 0; pair[pl] != '='; pl++) {
            if (pair[pl] >= 'A' && pair[pl] <= 'Z') {
                pair[pl] = (char)(pair[pl] - 'A' + 'a');
            }
        }
    }
    pl = strlen(pair);
    while (inst_s98_pair(&p, &s, &len)) {
        const char *put = s;
        size_t putLen = len;
        if (inst_s98_pair_is(s, len, key, NULL)) {
            if (done || value == NULL) {
                /* A second pair of the key, or the key removed. */
                continue;
            }
            put = pair;
            putLen = pl;
            done = 1;
        }
        if (n + (n ? 1 : 0) + putLen + 1 > cap) {
            out[0] = '\0';
            return 0;
        }
        if (n) {
            out[n++] = ';';
        }
        memcpy(out + n, put, putLen);
        n += putLen;
        out[n] = '\0';
    }
    if (!done && value != NULL) {
        if (n + (n ? 1 : 0) + pl + 1 > cap) {
            out[0] = '\0';
            return 0;
        }
        if (n) {
            out[n++] = ';';
        }
        memcpy(out + n, pair, pl);
        n += pl;
        out[n] = '\0';
    }
    return 1;
}

void inst_s98_now_text(int i, int nt, int me, int state, const char *value,
                       char *out, size_t cap)
{
    char d[48];

    if (cap > 0) {
        out[0] = '\0';
    }
    if (state < 0) {
        inst_cat(out, cap, "cannot be read");
        return;
    }
    if (state == 0 || value == NULL) {
        inst_s98_default_text(i, nt, me, d, sizeof(d));
        inst_cat(out, cap, "not set (the built-in default is ");
        inst_cat(out, cap, d);
        inst_cat(out, cap, ")");
        return;
    }
    inst_cat(out, cap, "set: ");
    inst_cat(out, cap, value);
}

int inst_s98_owned_by_call(int before, int afterS98)
{
    return before != 2 && afterS98;
}

int inst_s98_owned_by_settle(int before, int afterS98, int nt,
                             int infStaged, int preExisting)
{
    if (before == 2 || !afterS98) {
        return 0;
    }
    return nt ? infStaged : !preExisting;
}

int inst_s98_service_removable(int devnodesLeft, int preExisting)
{
    return !devnodesLeft && !preExisting;
}

int inst_s98_cold_start(int nt)
{
    return !nt;
}

int inst_s98_decide(int haveS98, int tb, int yes, int stor98Switch,
                    int noStor98, int list, int *asked, int *def)
{
    *def = 1;
    *asked = haveS98 && tb && !yes && !stor98Switch && !noStor98 && !list;
    return haveS98 && tb && !noStor98;
}

int inst_s98_known_tunnel(int nt, int present, int place, int recorded)
{
    if (present) {
        return place == INST_S98_AT_TUNNEL;
    }
    return nt ? recorded != 0 : 1;
}

int inst_s98_file_update(int nt, int include, int share, int installed,
                         int cmp)
{
    return !nt && include && share == INST_S98_SHARE_FREE &&
           installed > 0 && cmp < 0;
}

const char instS98DisconnectText[] =
    "Please disconnect all external NVMe drives before proceeding";

int inst_cm_mode_off(const char *name, INST_U32 value)
{
    return name != NULL && inst_eq(name, "XhciCmMode") && value == 0;
}

const char instCmOffText[] =
    "Thunderbolt 3 and USB4 will be turned off (XhciCmMode 0)";

void inst_tb_found_text(char *out, size_t cap, const char *name)
{
    const char *all = inst_page_text(INST_PT_TB_T);
    const char *rest = strstr(all, ". ");
    char cut[INST_TB_NAME_SHOWN + 1];

    if (cap == 0) {
        return;
    }
    if (name == NULL || name[0] == '\0' || rest == NULL) {
        inst_copy(out, all, cap);
        return;
    }
    inst_copy(cut, name, sizeof(cut));
    inst_copy(out, "A Thunderbolt 3 / USB4 controller was found: ", cap);
    inst_cat(out, cap, cut);
    inst_cat(out, cap, rest);
}

int inst_s98_box_open(int haveS98, int tbHave, int tbTicked)
{
    return haveS98 && tbHave && tbTicked;
}

int inst_s98_await_reconnect(int nt, int updatedOk, int ownCm,
                             int behindTunnel)
{
    return nt && updatedOk && ownCm && behindTunnel;
}

int inst_s98_place(int found, int changing, int whole, int cmAll,
                   int verifiedAbove, int portAbove)
{
    if (!found || changing) {
        return INST_S98_AT_UNKNOWN;
    }
    if (verifiedAbove) {
        return INST_S98_AT_TUNNEL;
    }
    if (!whole || !cmAll || portAbove) {
        return INST_S98_AT_UNKNOWN;
    }
    return INST_S98_AT_INTERNAL;
}

int inst_s98_eligible(int tbInRun, int place)
{
    if (!tbInRun) {
        return INST_S98_NEEDS_TB;
    }
    if (place == INST_S98_AT_TUNNEL) {
        return INST_S98_ELIGIBLE;
    }
    return place == INST_S98_AT_INTERNAL ? INST_S98_SKIP_INTERNAL :
           INST_S98_SKIP_UNKNOWN;
}

const char *inst_s98_skip_text(int why, int brief)
{
    switch (why) {
    case INST_S98_SKIP_INTERNAL:
        return brief ? "left: an internal drive (Stor98 goes on tunnel drives)" :
               "an internal drive, not behind a Thunderbolt or USB4 tunnel: "
               "the installer leaves it as it is (Stor98 goes on tunnel "
               "drives only)";
    case INST_S98_SKIP_UNKNOWN:
        return brief ? "left: where it sits could not be read" :
               "where it sits could not be read (no Thunderbolt connection "
               "manager answered, or the reading was not whole): left as it "
               "is; run the installer again once the Thunderbolt driver runs";
    case INST_S98_NEEDS_TB:
        return brief ? "left: Stor98 needs Thunderbolt 3 / USB4 support" :
               "Stor98 needs Thunderbolt 3 / USB4 support, which this run "
               "leaves out";
    case INST_S98_SKIP_SHARED:
        return brief ? "left: another Stor98 shares its file" :
               "a Stor98 the installer did not put on a tunnel drive shares "
               "its file at another release: no Stor98 change in this run";
    default:
        return NULL;
    }
}

int inst_inf_names_file(const char *text, size_t n, const char *file)
{
    size_t w = strlen(file);
    size_t i;
    int comment = 0;

    for (i = 0; i < n; i++) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            comment = 0;
            continue;
        }
        if (c == ';') {
            comment = 1;
        }
        if (comment || i + w > n || !inst_eqn(text + i, file, w)) {
            continue;
        }
        if ((i == 0 || strchr(" \t,=\"\\\r\n", text[i - 1]) != NULL) &&
            (i + w == n || strchr(" \t,=\"\r\n;", text[i + w]) != NULL)) {
            return 1;
        }
    }
    return 0;
}

int inst_s98_adoptable(int preExisting, int present, int place)
{
    if (preExisting) {
        return 0;
    }
    return !present || place == INST_S98_AT_TUNNEL;
}

int inst_s98_svc_value_stor98(const char *name)
{
    return !inst_eq(name, "LinkState");
}

int inst_s98_stray_inf_ours(int preExisting, int namesStor98)
{
    return !preExisting && namesStor98;
}

int inst_s98_share(int otherBound, int fileSame)
{
    if (!otherBound) {
        return INST_S98_SHARE_FREE;
    }
    return fileSame ? INST_S98_SHARE_SAME : INST_S98_SHARE_HOLD;
}

/* ---- SHA-256 (FIPS 180-4): the 98 SE and ME INF listings' file hashes ---- */

static const INST_U32 instShaK[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
    0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
    0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
    0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
    0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
    0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
    0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
    0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
    0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
    0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

#define INST_ROR(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & 0xFFFFFFFFUL)

static void inst_sha_block(INST_U32 *h, const unsigned char *p)
{
    INST_U32 w[64];
    INST_U32 a, b, c, d, e, f, g, k, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((INST_U32)p[4 * i] << 24) | ((INST_U32)p[4 * i + 1] << 16) |
               ((INST_U32)p[4 * i + 2] << 8) | (INST_U32)p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        INST_U32 s0 = INST_ROR(w[i - 15], 7) ^ INST_ROR(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        INST_U32 s1 = INST_ROR(w[i - 2], 17) ^ INST_ROR(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & 0xFFFFFFFFUL;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (i = 0; i < 64; i++) {
        t1 = (k + (INST_ROR(e, 6) ^ INST_ROR(e, 11) ^ INST_ROR(e, 25)) +
              ((e & f) ^ (~e & g)) + instShaK[i] + w[i]) & 0xFFFFFFFFUL;
        t2 = ((INST_ROR(a, 2) ^ INST_ROR(a, 13) ^ INST_ROR(a, 22)) +
              ((a & b) ^ (a & c) ^ (b & c))) & 0xFFFFFFFFUL;
        k = g; g = f; f = e;
        e = (d + t1) & 0xFFFFFFFFUL;
        d = c; c = b; b = a;
        a = (t1 + t2) & 0xFFFFFFFFUL;
    }
    h[0] = (h[0] + a) & 0xFFFFFFFFUL; h[1] = (h[1] + b) & 0xFFFFFFFFUL;
    h[2] = (h[2] + c) & 0xFFFFFFFFUL; h[3] = (h[3] + d) & 0xFFFFFFFFUL;
    h[4] = (h[4] + e) & 0xFFFFFFFFUL; h[5] = (h[5] + f) & 0xFFFFFFFFUL;
    h[6] = (h[6] + g) & 0xFFFFFFFFUL; h[7] = (h[7] + k) & 0xFFFFFFFFUL;
}

void inst_sha256(const unsigned char *data, size_t len, char *hex65)
{
    static const char digits[] = "0123456789ABCDEF";
    INST_U32 h[8];
    unsigned char tail[128];
    size_t full = len - len % 64;
    size_t rest = len - full;
    size_t i;
    size_t n;
    INST_U32 bitsLo = (INST_U32)((len << 3) & 0xFFFFFFFFUL);
    INST_U32 bitsHi = (INST_U32)((len >> 29) & 0xFFFFFFFFUL);

    h[0] = 0x6a09e667UL; h[1] = 0xbb67ae85UL; h[2] = 0x3c6ef372UL;
    h[3] = 0xa54ff53aUL; h[4] = 0x510e527fUL; h[5] = 0x9b05688cUL;
    h[6] = 0x1f83d9abUL; h[7] = 0x5be0cd19UL;
    for (i = 0; i < full; i += 64) {
        inst_sha_block(h, data + i);
    }
    memset(tail, 0, sizeof(tail));
    if (rest > 0) {
        memcpy(tail, data + full, rest);
    }
    tail[rest] = 0x80;
    n = rest + 1 + 8 <= 64 ? 64 : 128;
    tail[n - 8] = (unsigned char)(bitsHi >> 24);
    tail[n - 7] = (unsigned char)(bitsHi >> 16);
    tail[n - 6] = (unsigned char)(bitsHi >> 8);
    tail[n - 5] = (unsigned char)bitsHi;
    tail[n - 4] = (unsigned char)(bitsLo >> 24);
    tail[n - 3] = (unsigned char)(bitsLo >> 16);
    tail[n - 2] = (unsigned char)(bitsLo >> 8);
    tail[n - 1] = (unsigned char)bitsLo;
    inst_sha_block(h, tail);
    if (n == 128) {
        inst_sha_block(h, tail + 64);
    }
    for (i = 0; i < 8; i++) {
        int b;
        for (b = 0; b < 8; b++) {
            hex65[i * 8 + (size_t)b] =
                digits[(h[i] >> (28 - 4 * b)) & 0xFUL];
        }
    }
    hex65[64] = '\0';
}
