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

/* A decoration "NT[arch][.major[.minor[...]]]". Returns 1 when it applies to
 * the system, with its rank (version, then arch-specific) in *rank. */
static int inst_decoration(const char *d, const INST_ENV *env, INST_U32 *rank)
{
    char arch[INST_NAME_CHARS];
    const char *s;
    size_t n = 0;
    INST_U32 major = 0;
    INST_U32 minor = 0;
    int specific;

    if (!inst_starts(d, "NT")) {
        return 0;
    }
    s = d + 2;
    while (*s != '\0' && *s != '.' && n + 1 < sizeof(arch)) {
        arch[n++] = *s++;
    }
    arch[n] = '\0';
    specific = (n != 0);
    if (specific) {
        if (inst_eq(arch, "x86")) {
            if (env->Arch != INST_ARCH_X86) {
                return 0;
            }
        } else if (inst_eq(arch, "amd64")) {
            if (env->Arch != INST_ARCH_AMD64) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') {
            return 0;
        }
        while (*s >= '0' && *s <= '9') {
            major = major * 10UL + (INST_U32)(*s - '0');
            if (major > 255UL) {
                return 0;
            }
            s++;
        }
        if (*s == '.') {
            s++;
            if (*s >= '0' && *s <= '9') {
                while (*s >= '0' && *s <= '9') {
                    minor = minor * 10UL + (INST_U32)(*s - '0');
                    if (minor > 255UL) {
                        return 0;
                    }
                    s++;
                }
            }
        }
    }
    if (major > env->Major || (major == env->Major && minor > env->Minor)) {
        return 0;
    }
    *rank = (major << 17) | (minor << 1) | (INST_U32)(specific ? 1 : 0);
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
            if (inst_starts(field, "NTamd64")) {
                amd64 = 1;
                if (arch == INST_ARCH_X86) {
                    return 0;
                }
            } else if (arch == INST_ARCH_AMD64) {
                return 0;
            } else if (!inst_starts(field, "NTx86")) {
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
      "2 is at your own risk: on an Intel controller without the switch "
      "registers it\n  writes registers of unknown meaning at every start, "
      "resume, stop and shutdown,\n  and the result is unknown (release "
      "notes, \"Value 2\").", 0x4UL },
    { "XhciInterruptMode", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0xFUL, 0xFUL,
      "how the controller's events arrive (0 automatic, 1 interrupt line "
      "only, 2 polling always, 3 message-signalled only)",
      "1 and 3 refuse to start a controller Windows gives no line or no "
      "message (Code 10).", 0xAUL },
    { "XhciPollFineTimer", INST_INF_XHCI, INST_APPLY_NT_XHCI, 0x3UL, 0x3UL,
      "a finer system clock while the controller is polled (0 off, 1 on)",
      NULL, 0 },
    { "XhciSelectiveSuspend", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3UL,
      0x3UL, "put idle USB devices to sleep (0 off, 1 on)",
      "no real machine has been read with it on: leave it off unless you are "
      "testing it.", 0x2UL },
    { "XhciForceBulkOnly", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3UL, 0x3UL,
      "Bulk-Only instead of UAS for a disk that offers both (0 UAS where "
      "offered, 1 Bulk-Only)",
      "read as each device is connected; on Windows 2000 and later a disk "
      "already\n  installed keeps its driver until it is uninstalled in "
      "Device Manager and\n  plugged in again. A UAS-only disk stays on "
      "UAS.", 0 },
    { "XhciCmMode", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0xFUL,
      "the Thunderbolt and USB4 connection manager (0 off, 1 automatic)",
      "2 and 3 are the overrides of design record 21 section 8, for testing "
      "only.", 0xCUL },
    { "XhciCmApprove", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0x3UL,
      "approve Thunderbolt devices automatically (0 no, 1 yes)", NULL, 0 },
    { "XhciCmSafeEject", INST_INF_CM, INST_APPLY_CM, 0x3UL, 0x3UL,
      "offer Safely Remove for the devices behind a dock (0 no, 1 yes)",
      NULL, 0 }
};
const int instValueCount = (int)(sizeof(instValues) / sizeof(instValues[0]));

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
    switch (instValues[index].Apply) {
    case INST_APPLY_ANY_XHCI:
        return env->AnyXhci ? 1 : 0;
    case INST_APPLY_PSW:
        return env->PswController ? 1 : 0;
    case INST_APPLY_NT_XHCI:
        return (env->Nt && env->AnyXhci) ? 1 : 0;
    case INST_APPLY_CM:
        return env->CmNhi ? 1 : 0;
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
        } else if (inst_switch(a, "clean")) {
            opts->Clean = 1;
        } else if (inst_switch(a, "uninstall")) {
            opts->Uninstall = 1;
        } else if (inst_switch(a, "nopause")) {
            opts->NoPause = 1;
        } else if (inst_switch(a, "debug")) {
            /* INSTALL.BAT chose the directory; nothing more to do here. */
        } else if (inst_switch(a, "selftest")) {
            opts->SelfTest = 1;
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
    if (opts->List && (opts->Yes || opts->Clean || opts->Restage)) {
        inst_err(err, errCap, "/list changes nothing: it takes no /yes, "
                 "/clean or /restage", NULL);
        return 1;
    }
    if (opts->Defaults && opts->SetCount != 0) {
        inst_err(err, errCap, "/defaults and /set contradict each other", NULL);
        return 1;
    }
    if (opts->Uninstall && (opts->Defaults || opts->SetCount != 0 ||
                            opts->Restage || opts->Clean || opts->Force)) {
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
