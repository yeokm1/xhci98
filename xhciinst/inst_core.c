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
    return inst_starts(name, "nvme98");
}

int inst_own_service(const char *service)
{
    return inst_eq(service, "xhci98") || inst_eq(service, "xhciuas") ||
           inst_eq(service, "xhcicm");
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
    { "XhciIntelPortSwitch", INST_INF_XHCI, INST_APPLY_PSW, 0x3UL, 0x7UL,
      "route the shared connectors from the EHCI controller to this driver "
      "(0 off, 1 on)",
      "2 forces the switch writes on an Intel controller that does not "
      "have the switch\n  registers, at every start, resume, stop and "
      "shutdown; use it only if the\n  release notes' \"Value 2\" section "
      "applies to your machine.", 0x4UL },
    { "XhciInterruptMode", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0xFUL, 0xFUL,
      "how the controller's events arrive (0 automatic, 1 interrupt line "
      "only, 2 polling always, 3 message-signalled only)",
      "1 and 3 refuse to start a controller Windows gives no line or no "
      "message (Code 10).", 0xAUL },
    /* Owner, 2026-10-15: never asked (Asked 0); /set Name=Value only. */
    { "XhciPollFineTimer", INST_INF_XHCI, INST_APPLY_NT_XHCI, 0x0UL, 0x3UL,
      "a finer system clock while the controller is polled (0 off, 1 on)",
      NULL, 0 },
    { "XhciSelectiveSuspend", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x0UL,
      0x3UL, "put idle USB devices to sleep (0 off, 1 on)",
      "leave it off unless a device needs it; if a device stops responding "
      "after idling,\n  set it back to 0.", 0x2UL },
    /* Owner, 2026-10-15: asked on 98 SE and ME only, default off; on is
     * tools\silent\silent-on.bat's effect (the SILENT.REG list, then the
     * switch), off silent-off.bat's (the switch, the list left). */
    { "XhciSilentInstall", INST_INF_XHCI, INST_APPLY_9X_XHCI, 0x3UL, 0x3UL,
      "install hubs, boot keyboards and mice and Bulk-Only storage with no "
      "Add New Hardware\n  wizard (0 off, 1 on)",
      "turn it on only once each listed class's drivers have been installed "
      "at least\n  once: otherwise 98 SE asks for its CD, with no wizard. "
      "Restart Windows after\n  changing it.", 0 },
    { "XhciForceBulkOnly", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3UL, 0x3UL,
      "Bulk-Only instead of UAS for a disk that offers both (0 UAS where "
      "offered, 1 Bulk-Only)",
      "read as each device is connected; on Windows 2000 and later a disk "
      "already\n  installed keeps its driver until it is uninstalled in "
      "Device Manager and\n  plugged in again. A UAS-only disk stays on "
      "UAS.", 0 },
    { "XhciCmMode", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0xFUL,
      "the Thunderbolt and USB4 connection manager (0 off, 1 automatic)",
      "2 and 3 force one connection manager whatever the controller "
      "reports; use them\n  only on the release notes' advice.", 0xCUL },
    { "XhciCmApprove", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0x3UL,
      "approve Thunderbolt devices automatically (0 no, 1 yes)", NULL, 0 },
    { "XhciCmSafeEject", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0x3UL,
      "offer Safely Remove for the devices behind a dock (0 no, 1 yes)",
      NULL, 0 },
    /* Owner, 2026-10-15: never asked, /set Name=Value only (record 23
     * section 8.1, record 24 section 11). */
    { "XhciCmReenum", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "make Windows look for devices after each Thunderbolt or USB4 "
      "change (0 no, 1 yes)",
      "0 is for diagnosing a machine where that misbehaves: devices then "
      "appear only\n  after a Device Manager refresh.", 0x1UL },
    { "XhciCmUsb3Tunnel", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "USB4: carry a device's USB over a USB3 connection (0 no, 1 yes)",
      NULL, 0 },
    { "XhciCmLaneBonding", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x3UL,
      "USB4: bond the two lanes of a link (0 no, 1 yes)", NULL, 0 },
    { "XhciCmPreOsCleanup", INST_INF_CM, INST_APPLY_CM, 0x0UL, 0x7UL,
      "USB4: connections found at start (0 kept, 1 cleaned where they do "
      "not verify, 2\n  all cleaned)",
      "1 and 2 may drop a device the firmware connected before Windows "
      "started.", 0x6UL }
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
    default:
        return 0;
    }
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
    if (instValues[index].Apply == INST_APPLY_9X_XHCI && env->Nt) {
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
    if (!inst_number(sep + 1, &v) || v > 31UL ||
        (instValues[i].Settable & (1UL << v)) == 0) {
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
            /* INSTALL.BAT chose the directory; nothing more to do here. */
        } else if (inst_switch(a, "selftest")) {
            opts->SelfTest = 1;
        } else if (inst_switch(a, "thunderbolt")) {
            opts->Thunderbolt = 1;
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
        } else if (inst_switch(a, "dir") || inst_switch(a, "log")) {
            int isDir = inst_switch(a, "dir");
            if (i + 1 >= argc) {
                inst_err(err, errCap, a, " needs a value");
                return 1;
            }
            i++;
            if (isDir) {
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
    if (opts->NoTray && (opts->Uninstall || opts->Devices || opts->Eject)) {
        inst_err(err, errCap, "/notray belongs to an install", NULL);
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
        return mayRemove ? INST_NT_FALLBACK : INST_NT_REPLUG;
    }
    if (inst_rights_answer(cr, vetoType)) {
        return INST_NT_REPLUG;
    }
    return INST_NT_ANSWER;
}

int inst_eject_nt_removal(INST_U32 cr, INST_U32 vetoType, const char **text,
                          int *withName)
{
    if (inst_rights_answer(cr, vetoType)) {
        *withName = 0;
        *text = inst_replug_text();
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

const char *inst_replug_text(void)
{
    return "Windows does not list this device for safe removal until it has "
           "been plugged\nin once more. Unplug it and plug it back in, or run "
           "this as an administrator.";
}

const char *inst_not_removable_text(void)
{
    return "Windows will not stop this device: it does not see it as "
           "removable now, and\nthis Windows has no other way to stop it. "
           "Restart Windows with the device\nconnected: it is then listed "
           "for removal again. Or close every program using\nit, shut down "
           "Windows and unplug it while the computer is off.";
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
               "Close the programs\nusing it and run INSTALL /eject again.";
    default:
        return "Before you unplug it, make sure none of its other devices "
               "(USB ports, card\nreaders, network) is in use; stop them in "
               "Windows first.";
    }
}

/* ======================================================================== */
/* The Thunderbolt 3 / USB4 question (record 23 section 8.5)                 */
/* ======================================================================== */

int inst_tb_decide(int haveCm, int nhiPresent, int optYes, int optThunderbolt,
                   int optList, int *ask, int *defaultYes)
{
    *ask = 0;
    *defaultYes = nhiPresent ? 1 : 0;
    if (!haveCm) {
        *defaultYes = 0;
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
