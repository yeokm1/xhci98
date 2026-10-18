/*
 * di_core.c - the pure part of XHCIDI.EXE (roadmap-usb4.md task 42-A.1,
 * design record 27 section 11): the command line, the result file's lines,
 * the setup engine's error names, the verdicts. See di_core.h.
 *
 * C89, no Windows header, no C runtime beyond what both compilers carry in
 * their freestanding form: the string work is done here by hand, so the
 * 16-bit build and the host suite run the same code.
 */

#include "di_core.h"

static unsigned di_strlen(const char *s)
{
    unsigned n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int di_streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static char di_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

int di_ieq(const char *a, const char *b)
{
    while (*a != '\0' && di_upper(*a) == di_upper(*b)) {
        a++;
        b++;
    }
    return di_upper(*a) == di_upper(*b);
}

static int di_copy(char *out, unsigned size, const char *s, unsigned n)
{
    unsigned i;
    if (size == 0 || n >= size) {
        if (size != 0) {
            out[0] = '\0';
        }
        return 0;
    }
    for (i = 0; i < n; i++) {
        out[i] = s[i];
    }
    out[n] = '\0';
    return 1;
}

/* ---- the command line (record 27 section 4.1) ------------------------- */

#define DI_MAX_TOKENS 9

typedef struct _DI_TOK {
    const char *p;
    unsigned n;
} DI_TOK;

static int di_tokens(const char *s, DI_TOK *t, int max)
{
    int count = 0;
    for (;;) {
        unsigned n = 0;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0' || *s == '\r' || *s == '\n') {
            return count;
        }
        if (count == max) {
            return max + 1;
        }
        t[count].p = s;
        while (s[n] != '\0' && s[n] != ' ' && s[n] != '\t' &&
               s[n] != '\r' && s[n] != '\n') {
            n++;
        }
        t[count].n = n;
        s += n;
        count++;
    }
}

static int di_tok_is(const DI_TOK *t, const char *word)
{
    unsigned i;
    for (i = 0; i < t->n; i++) {
        if (word[i] == '\0' || word[i] != t->p[i]) {
            return 0;
        }
    }
    return word[t->n] == '\0';
}

static int di_name_char(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }
    /*
     * The installer's set for an 8.3 part of its install directory
     * (xhciinst\inst_core.c, inst_name83), so every directory it accepts
     * reaches the helper whole.
     */
    switch (c) {
    case '_': case '-': case '~': case '!': case '#': case '$':
    case '&': case '@': case '^': case '\'': case '(': case ')':
    case '{': case '}':
        return 1;
    default:
        return 0;
    }
}

/*
 * A drive-rooted path whose every part is 8.3 (record 23 section 9.6): a
 * name of one to eight characters, an optional extension of one to three, no
 * space, no empty part and no trailing backslash. With want_inf the last
 * part's extension is INF.
 */
int di_path_ok(const char *path, int want_inf)
{
    unsigned len = di_strlen(path);
    unsigned i = 3;
    const char *last = 0;

    if (len < 4 || len > DI_PATH_MAX) {
        return 0;
    }
    if (!((path[0] >= 'A' && path[0] <= 'Z') ||
          (path[0] >= 'a' && path[0] <= 'z')) ||
        path[1] != ':' || path[2] != '\\') {
        return 0;
    }
    while (i < len) {
        unsigned name = 0;
        unsigned ext = 0;
        int dot = 0;
        last = path + i;
        while (i < len && path[i] != '\\') {
            char c = path[i];
            if (c == '.') {
                if (dot) {
                    return 0;
                }
                dot = 1;
            } else if (!di_name_char(c)) {
                return 0;
            } else if (dot) {
                ext++;
            } else {
                name++;
            }
            i++;
        }
        if (name == 0 || name > 8 || ext > 3 || (dot && ext == 0)) {
            return 0;
        }
        if (i < len) {
            i++;
            if (i == len) {
                return 0;
            }
        }
    }
    if (want_inf) {
        unsigned n = di_strlen(last);
        if (n < 5 || !di_ieq(last + n - 4, ".INF")) {
            return 0;
        }
    }
    return 1;
}

/*
 * A device id as cfgmgr32.dll gives one: printable ASCII with no space or
 * quote, at most MAX_DEVICE_ID_LEN, an enumerator of letters, digits and
 * underscores, then at least one more part, no empty part.
 */
int di_devid_ok(const char *devid)
{
    unsigned len = di_strlen(devid);
    unsigned i;
    unsigned parts = 1;
    unsigned enumlen = 0;
    int in_enum = 1;

    if (len == 0 || len > DI_DEVID_MAX) {
        return 0;
    }
    if (devid[0] == '\\' || devid[len - 1] == '\\') {
        return 0;
    }
    for (i = 0; i < len; i++) {
        char c = devid[i];
        if (c <= ' ' || c > '~' || c == '"') {
            return 0;
        }
        if (c == '\\') {
            if (devid[i + 1] == '\\') {
                return 0;
            }
            in_enum = 0;
            parts++;
            continue;
        }
        if (in_enum) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) {
                return 0;
            }
            enumlen++;
        }
    }
    return parts >= 2 && enumlen > 0 && enumlen <= 31;
}

static int di_parse_hwnd(const DI_TOK *t, unsigned long *out)
{
    unsigned long v = 0;
    unsigned i;
    if (t->n == 0 || t->n > 5) {
        return 0;
    }
    if (t->n > 1 && t->p[0] == '0') {
        return 0;
    }
    for (i = 0; i < t->n; i++) {
        char c = t->p[i];
        if (c < '0' || c > '9') {
            return 0;
        }
        v = v * 10UL + (unsigned long)(c - '0');
    }
    if (v > 65535UL) {
        return 0;
    }
    *out = v;
    return 1;
}

static void di_cmd_clear(DI_CMD *cmd)
{
    cmd->job = DI_JOB_NONE;
    cmd->version[0] = '\0';
    cmd->result[0] = '\0';
    cmd->hwnd = 0;
    cmd->devid[0] = '\0';
    cmd->inf[0] = '\0';
    cmd->flags = 0;
    cmd->why = "";
}

/*
 * XHCIDI.EXE <version> install <result> <hwnd> <device id> <inf> [<flags>]
 * XHCIDI.EXE <version> remove  <result> <hwnd> <device id>
 * XHCIDI.EXE <version> probe   <result> <hwnd>
 *
 * The result file is the third word, so it is read first: with no usable
 * path the helper can report nothing. The version is compared next and
 * before any other word is read.
 */
int di_parse(const char *cmdline, const char *own_version, DI_CMD *cmd)
{
    DI_TOK t[DI_MAX_TOKENS];
    int n;
    int i;

    di_cmd_clear(cmd);
    n = di_tokens(cmdline, t, DI_MAX_TOKENS);
    if (n < 3) {
        cmd->why = "arguments";
        return DI_PARSE_NOFILE;
    }
    if (!di_copy(cmd->result, sizeof cmd->result, t[2].p, t[2].n) ||
        !di_path_ok(cmd->result, 0)) {
        cmd->result[0] = '\0';
        cmd->why = "result-file";
        return DI_PARSE_NOFILE;
    }
    if (!di_copy(cmd->version, sizeof cmd->version, t[0].p, t[0].n) ||
        !di_streq(cmd->version, own_version)) {
        cmd->why = "version";
        return DI_PARSE_VERSION;
    }
    if (n > DI_MAX_TOKENS) {
        cmd->why = "arguments";
        return DI_PARSE_COMMAND;
    }
    if (di_tok_is(&t[1], "install")) {
        if (n < 6 || n > 8) {
            cmd->why = "arguments";
            return DI_PARSE_COMMAND;
        }
        cmd->job = DI_JOB_INSTALL;
    } else if (di_tok_is(&t[1], "remove")) {
        if (n != 5) {
            cmd->why = "arguments";
            return DI_PARSE_COMMAND;
        }
        cmd->job = DI_JOB_REMOVE;
    } else if (di_tok_is(&t[1], "probe")) {
        if (n != 4) {
            cmd->why = "arguments";
            return DI_PARSE_COMMAND;
        }
        cmd->job = DI_JOB_PROBE;
    } else {
        cmd->why = "job";
        return DI_PARSE_COMMAND;
    }
    if (!di_parse_hwnd(&t[3], &cmd->hwnd)) {
        cmd->job = DI_JOB_NONE;
        cmd->why = "hwnd";
        return DI_PARSE_COMMAND;
    }
    if (cmd->job == DI_JOB_PROBE) {
        return DI_PARSE_OK;
    }
    if (!di_copy(cmd->devid, sizeof cmd->devid, t[4].p, t[4].n) ||
        !di_devid_ok(cmd->devid)) {
        cmd->devid[0] = '\0';
        cmd->job = DI_JOB_NONE;
        cmd->why = "device-id";
        return DI_PARSE_COMMAND;
    }
    if (cmd->job == DI_JOB_REMOVE) {
        return DI_PARSE_OK;
    }
    if (!di_copy(cmd->inf, sizeof cmd->inf, t[5].p, t[5].n) ||
        !di_path_ok(cmd->inf, 1)) {
        cmd->inf[0] = '\0';
        cmd->job = DI_JOB_NONE;
        cmd->why = "inf-path";
        return DI_PARSE_COMMAND;
    }
    for (i = 6; i < n; i++) {
        unsigned bit;
        if (di_tok_is(&t[i], "force")) {
            bit = DI_FLAG_FORCE;
        } else if (di_tok_is(&t[i], "quiet")) {
            bit = DI_FLAG_QUIET;
        } else {
            bit = 0;
        }
        if (bit == 0 || (cmd->flags & bit) != 0) {
            cmd->flags = 0;
            cmd->job = DI_JOB_NONE;
            cmd->why = "flags";
            return DI_PARSE_COMMAND;
        }
        cmd->flags |= bit;
    }
    return DI_PARSE_OK;
}

/* ---- the device and the INF ------------------------------------------- */

int di_enumerator(const char *devid, char *out, unsigned size)
{
    unsigned n = 0;
    while (devid[n] != '\0' && devid[n] != '\\') {
        n++;
    }
    if (n == 0 || devid[n] != '\\') {
        if (size != 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return di_copy(out, size, devid, n);
}

/*
 * DEVICE_INFO.szRegSubkey is "the device's hardware registry subkey ...
 * usually some place in the \ENUM branch" (setupx.h's DEVICE_INFO block):
 * Enum\<device id>, compared without case.
 */
int di_subkey_matches(const char *subkey, const char *devid)
{
    static const char prefix[] = "ENUM\\";
    unsigned i;
    for (i = 0; prefix[i] != '\0'; i++) {
        if (di_upper(subkey[i]) != prefix[i]) {
            return 0;
        }
    }
    return devid[0] != '\0' && di_ieq(subkey + i, devid);
}

const char *di_basename(const char *path)
{
    const char *b = path;
    while (*path != '\0') {
        if (*path == '\\' || *path == ':') {
            b = path + 1;
        }
        path++;
    }
    return b;
}

/*
 * The INF's directory, for LDID_SRCPATH (section 5.3): the path up to its
 * last backslash, which is kept only for a drive's root ("C:\").
 */
int di_inf_dir(const char *inf, char *out, unsigned size)
{
    unsigned len = di_strlen(inf);
    unsigned cut = len;
    while (cut > 0 && inf[cut - 1] != '\\') {
        cut--;
    }
    if (cut == 0) {
        if (size != 0) {
            out[0] = '\0';
        }
        return 0;
    }
    if (cut > 3) {
        cut--;
    }
    return di_copy(out, size, inf, cut);
}

/*
 * The chosen driver node's INF (DRIVER_NODE.atInfFileName) against the INF
 * the helper was given: a whole path compares as a path, a bare name with
 * the given INF's name.
 */
int di_same_inf(const char *atom_text, const char *inf)
{
    const char *p = atom_text;
    if (atom_text[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (*p == '\\' || *p == ':') {
            return di_ieq(atom_text, inf);
        }
        p++;
    }
    return di_ieq(atom_text, di_basename(inf));
}

/*
 * HKLM\Software\Microsoft\Windows\CurrentVersion's VersionNumber: 98 SE is
 * 4.10.2222, ME 4.90.3000 (record 27 section 1.3). Nothing else is run on,
 * Windows 98's first edition (4.10.1998) included.
 */
int di_system_supported(const char *version_number)
{
    static const char *const ok[2] = { "4.10.2222", "4.90.3000" };
    int k;
    const char *v = version_number;
    while (*v == ' ') {
        v++;
    }
    for (k = 0; k < 2; k++) {
        unsigned i = 0;
        while (ok[k][i] != '\0' && v[i] == ok[k][i]) {
            i++;
        }
        if (ok[k][i] == '\0' && !(v[i] >= '0' && v[i] <= '9') &&
            v[i] != '.') {
            return 1;
        }
    }
    return 0;
}

/* ---- the engine's errors ------------------------------------------------ */

/*
 * The RETERR values setupx.h enumerates: _ERR_IP from IP_ERROR (100) + 1,
 * the text processor's from TP_ERROR (200) + 1, _ERR_VCP from VCP_ERROR
 * (300) + 1, _ERR_GENERIC from GEN_ERROR (400) + 1, and _ERR_DEVICE_INSTALL
 * from DI_ERROR (500), each counting up in the header's order. Names and
 * values only: test_di.c reads every enumeration again from the header.
 */
const DI_ERRNAME di_errnames[] = {
    { 0, "OK" },
    { 101, "ERR_IP_INVALID_FILENAME" },
    { 102, "ERR_IP_ALLOC_ERR" },
    { 103, "ERR_IP_INVALID_SECT_NAME" },
    { 104, "ERR_IP_OUT_OF_HANDLES" },
    { 105, "ERR_IP_INF_NOT_FOUND" },
    { 106, "ERR_IP_INVALID_INFFILE" },
    { 107, "ERR_IP_INVALID_HINF" },
    { 108, "ERR_IP_INVALID_FIELD" },
    { 109, "ERR_IP_SECT_NOT_FOUND" },
    { 110, "ERR_IP_END_OF_SECTION" },
    { 111, "ERR_IP_PROFILE_NOT_FOUND" },
    { 112, "ERR_IP_LINE_NOT_FOUND" },
    { 113, "ERR_IP_FILEREAD" },
    { 114, "ERR_IP_TOOMANYINFFILES" },
    { 115, "ERR_IP_INVALID_SAVERESTORE" },
    { 116, "ERR_IP_INVALID_INFTYPE" },
    { 201, "ERR_TP_NOT_FOUND" },
    { 202, "ERR_TP_NO_MEM" },
    { 203, "ERR_TP_READ" },
    { 204, "ERR_TP_WRITE" },
    { 205, "ERR_TP_INVALID_REQUEST" },
    { 206, "ERR_TP_INVALID_LINE" },
    { 301, "ERR_VCP_IOFAIL" },
    { 302, "ERR_VCP_STRINGTOOLONG" },
    { 303, "ERR_VCP_NOMEM" },
    { 304, "ERR_VCP_QUEUEFULL" },
    { 305, "ERR_VCP_NOVHSTR" },
    { 306, "ERR_VCP_OVERFLOW" },
    { 307, "ERR_VCP_BADARG" },
    { 308, "ERR_VCP_UNINIT" },
    { 309, "ERR_VCP_NOTFOUND" },
    { 310, "ERR_VCP_BUSY" },
    { 311, "ERR_VCP_INTERRUPTED" },
    { 312, "ERR_VCP_BADDEST" },
    { 313, "ERR_VCP_SKIPPED" },
    { 314, "ERR_VCP_IO" },
    { 315, "ERR_VCP_LOCKED" },
    { 316, "ERR_VCP_WRONGDISK" },
    { 317, "ERR_VCP_CHANGEMODE" },
    { 318, "ERR_VCP_LDDINVALID" },
    { 319, "ERR_VCP_LDDFIND" },
    { 320, "ERR_VCP_LDDUNINIT" },
    { 321, "ERR_VCP_LDDPATH_INVALID" },
    { 322, "ERR_VCP_NOEXPANSION" },
    { 323, "ERR_VCP_NOTOPEN" },
    { 324, "ERR_VCP_NO_DIGITAL_SIGNATURE_CATALOG" },
    { 325, "ERR_VCP_NO_DIGITAL_SIGNATURE_FILE" },
    { 401, "ERR_GEN_LOW_MEM" },
    { 402, "ERR_GEN_INVALID_FILE" },
    { 403, "ERR_GEN_LOGCONFIG" },
    { 404, "ERR_GEN_CFGAUTO" },
    { 405, "ERR_GEN_UPDINI" },
    { 406, "ERR_GEN_UPDINIFIELDS" },
    { 407, "ERR_GEN_ADDREG" },
    { 408, "ERR_GEN_DELREG" },
    { 409, "ERR_GEN_INI2REG" },
    { 410, "ERR_GEN_FILE_COPY" },
    { 411, "ERR_GEN_FILE_DEL" },
    { 412, "ERR_GEN_FILE_REN" },
    { 413, "ERR_GEN_REG_API" },
    { 414, "ERR_GEN_DO_FILES" },
    { 415, "ERR_GEN_ADDIME" },
    { 416, "ERR_GEN_DELIME" },
    { 417, "ERR_GEN_PERUSER" },
    { 418, "ERR_GEN_BITREG" },
    { 500, "ERR_DI_INVALID_DEVICE_ID" },
    { 501, "ERR_DI_INVALID_COMPATIBLE_DEVICE_LIST" },
    { 502, "ERR_DI_REG_API" },
    { 503, "ERR_DI_LOW_MEM" },
    { 504, "ERR_DI_BAD_DEV_INFO" },
    { 505, "ERR_DI_INVALID_CLASS_INSTALLER" },
    { 506, "ERR_DI_DO_DEFAULT" },
    { 507, "ERR_DI_USER_CANCEL" },
    { 508, "ERR_DI_NOFILECOPY" },
    { 509, "ERR_DI_BAD_CLASS_INFO" },
    { 510, "ERR_DI_BAD_INF" },
    { 511, "ERR_DI_BAD_MOVEDEV_PARAMS" },
    { 512, "ERR_DI_NO_INF" },
    { 513, "ERR_DI_BAD_PROPCHANGE_PARAMS" },
    { 514, "ERR_DI_BAD_SELECTDEVICE_PARAMS" },
    { 515, "ERR_DI_BAD_REMOVEDEVICE_PARAMS" },
    { 516, "ERR_DI_BAD_ENABLECLASS_PARAMS" },
    { 517, "ERR_DI_FAIL_QUERY" },
    { 518, "ERR_DI_API_ERROR" },
    { 519, "ERR_DI_BAD_PATH" },
    { 520, "ERR_DI_BAD_UNREMOVEDEVICE_PARAMS" },
    { 521, "ERR_DI_NOUPDATE" },
    { 522, "ERR_DI_NODATE" },
    { 523, "ERR_DI_NOVERSION" },
    { 524, "ERR_DI_DONT_INSTALL" },
    { 525, "ERR_DI_NO_DIGITAL_SIGNATURE_CATALOG" },
    { 526, "ERR_DI_NO_DIGITAL_SIGNATURE_INF" },
    { 527, "ERR_DI_NO_DIGITAL_SIGNATURE_FILE" }
};

const unsigned di_errname_count = sizeof di_errnames / sizeof di_errnames[0];

const char *di_err_name(unsigned err)
{
    unsigned i;
    for (i = 0; i < di_errname_count; i++) {
        if (di_errnames[i].value == err) {
            return di_errnames[i].name;
        }
    }
    return "ERR_UNKNOWN";
}

/*
 * The verdict of the last engine call of a job (section 5.3's install,
 * section 9's removal): 0 is done, with the restart flags the engine
 * returned in DEVICE_INFO.Flags ("flags returned by DiInstallDevice to
 * indicate need to reboot/restart", setupx.h); ERR_DI_USER_CANCEL is a
 * Windows dialog the user closed; anything else failed.
 */
const char *di_verdict(int job, unsigned err, unsigned long flags)
{
    int restart = (flags & (DI_FL_NEEDRESTART | DI_FL_NEEDREBOOT)) != 0;
    if (job != DI_JOB_INSTALL && job != DI_JOB_REMOVE) {
        return DI_V_FAILED;
    }
    if (err == DI_OK) {
        if (job == DI_JOB_INSTALL) {
            return restart ? DI_V_INSTALLED_RESTART : DI_V_INSTALLED;
        }
        return restart ? DI_V_REMOVED_RESTART : DI_V_REMOVED;
    }
    if (err == DI_ERR_USER_CANCEL) {
        return DI_V_CANCELLED;
    }
    return DI_V_FAILED;
}

/* ---- the result file (section 4.2) ------------------------------------ */

void di_out_init(DI_OUT *out)
{
    out->line[0] = '\0';
    out->len = 0;
    out->count = 0;
    out->overflow = 0;
}

static void di_begin(DI_OUT *out, const char *key)
{
    out->len = 0;
    out->line[0] = '\0';
    while (*key != '\0' && out->len < DI_LINE_MAX - 3) {
        out->line[out->len++] = *key++;
    }
    out->line[out->len] = '\0';
}

/*
 * One field: a space and the text, every character below a space or above
 * '~' (and, in a bare field, a space or a quote) written as '_', so a field
 * never splits the line or the record. A field that does not fit is left out
 * whole, and the line says so with overflow set.
 */
static void di_field(DI_OUT *out, const char *s, int quoted)
{
    unsigned need = di_strlen(s) + 1 + (quoted ? 2 : 0);
    unsigned i;
    if (out->len + need > DI_LINE_MAX - 3) {
        out->overflow = 1;
        return;
    }
    out->line[out->len++] = ' ';
    if (quoted) {
        out->line[out->len++] = '"';
    }
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c < ' ' || c > '~' || c == '"' || (!quoted && c == ' ')) {
            c = '_';
        }
        out->line[out->len++] = c;
    }
    if (quoted) {
        out->line[out->len++] = '"';
    }
    out->line[out->len] = '\0';
}

static void di_field_dec(DI_OUT *out, unsigned long v)
{
    char buf[12];
    char rev[12];
    unsigned n = 0;
    unsigned i;
    do {
        rev[n++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    } while (v != 0 && n < sizeof rev);
    for (i = 0; i < n; i++) {
        buf[i] = rev[n - 1 - i];
    }
    buf[n] = '\0';
    di_field(out, buf, 0);
}

static void di_field_hex8(DI_OUT *out, unsigned long v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[9];
    int i;
    for (i = 7; i >= 0; i--) {
        buf[i] = hex[(int)(v & 0xFUL)];
        v >>= 4;
    }
    buf[8] = '\0';
    di_field(out, buf, 0);
}

static const char *di_finish(DI_OUT *out)
{
    out->line[out->len++] = '\r';
    out->line[out->len++] = '\n';
    out->line[out->len] = '\0';
    out->count++;
    return out->line;
}

const char *di_out_header(DI_OUT *out, const char *version)
{
    di_begin(out, "XHCIDI");
    di_field(out, version, 0);
    return di_finish(out);
}

const char *di_out_job(DI_OUT *out, const DI_CMD *cmd)
{
    di_begin(out, "JOB");
    switch (cmd->job) {
    case DI_JOB_INSTALL:
        di_field(out, "install", 0);
        di_field(out, cmd->devid, 0);
        di_field(out, cmd->inf, 0);
        break;
    case DI_JOB_REMOVE:
        di_field(out, "remove", 0);
        di_field(out, cmd->devid, 0);
        break;
    case DI_JOB_PROBE:
        di_field(out, "probe", 0);
        break;
    default:
        di_field(out, "none", 0);
        break;
    }
    return di_finish(out);
}

const char *di_out_found(DI_OUT *out, int found)
{
    di_begin(out, "FOUND");
    di_field(out, found ? "1" : "0", 0);
    return di_finish(out);
}

/*
 * DRIVER <inf> <section> "<description>" rank <n> date <yyyy-mm-dd>: the
 * chosen DRIVER_NODE's atInfFileName (its name part), lpszSectionName,
 * lpszDescription, Rank and DriverDate, the last an MS-DOS date (bits 15-9
 * the year from 1980, 8-5 the month, 4-0 the day), "-" when it is 0.
 */
const char *di_out_driver(DI_OUT *out, const char *inf_name,
                          const char *section, const char *description,
                          unsigned rank, unsigned dos_date)
{
    char date[11];
    di_begin(out, "DRIVER");
    di_field(out, inf_name[0] != '\0' ? di_basename(inf_name) : "-", 0);
    di_field(out, section[0] != '\0' ? section : "-", 0);
    di_field(out, description, 1);
    di_field(out, "rank", 0);
    di_field_dec(out, (unsigned long)rank);
    di_field(out, "date", 0);
    if (dos_date == 0) {
        di_field(out, "-", 0);
    } else {
        unsigned year = 1980U + ((dos_date >> 9) & 0x7FU);
        unsigned month = (dos_date >> 5) & 0x0FU;
        unsigned day = dos_date & 0x1FU;
        date[0] = (char)('0' + (int)(year / 1000U));
        date[1] = (char)('0' + (int)((year / 100U) % 10U));
        date[2] = (char)('0' + (int)((year / 10U) % 10U));
        date[3] = (char)('0' + (int)(year % 10U));
        date[4] = '-';
        date[5] = (char)('0' + (int)(month / 10U));
        date[6] = (char)('0' + (int)(month % 10U));
        date[7] = '-';
        date[8] = (char)('0' + (int)(day / 10U));
        date[9] = (char)('0' + (int)(day % 10U));
        date[10] = '\0';
        di_field(out, date, 0);
    }
    return di_finish(out);
}

const char *di_out_call(DI_OUT *out, const char *routine, const char *arg,
                        unsigned err)
{
    di_begin(out, "CALL");
    di_field(out, routine, 0);
    if (arg != 0 && arg[0] != '\0') {
        di_field(out, arg, 0);
    }
    di_field_dec(out, (unsigned long)err);
    return di_finish(out);
}

const char *di_out_flags(DI_OUT *out, unsigned long flags)
{
    di_begin(out, "FLAGS");
    di_field_hex8(out, flags);
    return di_finish(out);
}

/*
 * NODE <flags> [force]: the chosen DRIVER_NODE's Flags, and whether the
 * installer asked for a reinstall, so the log shows what the engine marked
 * (DNF_OLDDRIVER, DNF_CURRENT_DRIVER) beside what was asked.
 */
const char *di_out_node(DI_OUT *out, unsigned long node_flags, int force)
{
    di_begin(out, "NODE");
    di_field_hex8(out, node_flags);
    if (force) {
        di_field(out, "force", 0);
    }
    return di_finish(out);
}

const char *di_out_ids(DI_OUT *out, const char *value_name,
                       const char *value)
{
    di_begin(out, "IDS");
    di_field(out, value_name, 0);
    di_field(out, value[0] != '\0' ? value : "-", 0);
    return di_finish(out);
}

const char *di_out_error(DI_OUT *out, const char *name, unsigned err)
{
    di_begin(out, "ERROR");
    di_field(out, name, 0);
    di_field_dec(out, (unsigned long)err);
    return di_finish(out);
}

/*
 * RESULT <verdict>, and for failed the error's name and number: "failed
 * ERR_DI_BAD_INF 510".
 */
const char *di_out_result(DI_OUT *out, const char *verdict, unsigned err)
{
    di_begin(out, "RESULT");
    di_field(out, verdict, 0);
    if (di_streq(verdict, DI_V_FAILED)) {
        di_field(out, di_err_name(err), 0);
        di_field_dec(out, (unsigned long)err);
    }
    return di_finish(out);
}

/*
 * RESULT <verdict> <detail>: refused and its reason, or a failed of the
 * helper's own (DI_SELF_*) with the number 0.
 */
const char *di_out_result_text(DI_OUT *out, const char *verdict,
                               const char *detail)
{
    di_begin(out, "RESULT");
    di_field(out, verdict, 0);
    if (detail != 0 && detail[0] != '\0') {
        di_field(out, detail, 0);
    }
    if (di_streq(verdict, DI_V_FAILED)) {
        di_field(out, "0", 0);
    }
    return di_finish(out);
}

/*
 * RESULT refused <reason> [<detail>]: record 27 section 4.4's refusals,
 * the reason and its detail separate fields ("refused command hwnd",
 * "refused system 4.10.1998").
 */
const char *di_out_refused(DI_OUT *out, const char *reason,
                           const char *detail)
{
    di_begin(out, "RESULT");
    di_field(out, DI_V_REFUSED, 0);
    di_field(out, reason, 0);
    if (detail != 0 && detail[0] != '\0') {
        di_field(out, detail, 0);
    }
    return di_finish(out);
}

const char *di_out_end(DI_OUT *out)
{
    unsigned lines = out->count;
    di_begin(out, "END");
    di_field_dec(out, (unsigned long)lines);
    return di_finish(out);
}
