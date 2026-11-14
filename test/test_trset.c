/*
 * test_trset.c - XHCITR's Settings window and /set, the pure half
 * (xhcitr\tr_set.c; design record 25 section 22; roadmap-usb4.md task
 * 47.35): every row of Setup's table and of Stor98's classified by the
 * Tree (a row added to Setup and not to the Tree fails here), each
 * installed default held to the INFs' AddReg lines, the system rules, the
 * values offered and parsed, what the driver makes of a value, the Now
 * texts, the apply summary and its exit code (record 23 section 7.4), the
 * elevated run's request and its bounds, and the port switchover's texts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xhcitr/tr_set.h"
#include "test_harness.h"

static char *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void env_98(TRS_ENV *e)
{
    memset(e, 0, sizeof(*e));
    e->Inst.Nt = 0;
    e->Inst.Major = 4;
    e->Inst.Minor = 10;
    e->Inst.AnyXhci = 1;
    e->Inst.CmNhi = 1;
}

static void env_nt(TRS_ENV *e, INST_U32 major, INST_U32 minor)
{
    memset(e, 0, sizeof(*e));
    e->Inst.Nt = 1;
    e->Inst.Major = major;
    e->Inst.Minor = minor;
    e->Inst.AnyXhci = 1;
    e->Inst.CmNhi = 1;
}

/* ---- every row classified -------------------------------------------- */

static void test_coverage(void)
{
    int i;
    int j;
    int n = trs_row_count();

    for (i = 0; i < instValueCount; i++) {
        const TRS_ROW *r = trs_row_find(instValues[i].Name);
        if (r == NULL) {
            printf("unclassified Setup row: %s\n", instValues[i].Name);
        }
        CHECK(r != NULL, "every instValues[] row is the Tree's row");
        CHECK(r == NULL || trs_value(r) == &instValues[i],
              "and its definition is Setup's own");
    }
    for (i = 0; i < INST_S98_COUNT; i++) {
        const TRS_ROW *r = trs_row_find(instS98Values[i].Name9x);
        if (r == NULL) {
            printf("unclassified Stor98 row: %s\n", instS98Values[i].Name9x);
        }
        CHECK(r != NULL && trs_s98(r) == i, "every Stor98 row");
    }
    CHECK_EQ(n, instValueCount + INST_S98_COUNT,
             "no row besides them");
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            CHECK(!inst_eq(trs_row_at(i)->Name, trs_row_at(j)->Name),
                  "each row once");
        }
        CHECK(trs_row_at(i)->View != TRS_VIEW_OUT ||
                  trs_row_at(i)->Note != NULL,
              "an out row says why");
    }
    CHECK(trs_row_find("storwritethrough") == trs_row_find("StorWriteThrough"),
          "Stor98 by its NT key");
    CHECK(trs_row_find("XhciCommandCheck") != NULL &&
              trs_row_find("XhciCommandCheck")->View == TRS_VIEW_ADV,
          "task 49.38: the checker is an advanced row");
    CHECK(trs_row_find("XhciQemuNoInterrupt") == NULL, "nor a qemu value");
    CHECK_EQ(trs_row_find("XhciLogVerbosity")->View, TRS_VIEW_OUT,
             "the log level is the Logging level dialog's");
    CHECK_EQ(trs_row_find("XhciCmLogVerbosity")->View, TRS_VIEW_OUT,
             "both of them");
}

/* ---- the installed defaults against the INFs ---------------------------- */

static void test_defaults(void)
{
    char *x86 = load("..\\src\\xhci98.inf");
    char *amd = load("..\\src\\xhci98-amd64.inf");
    char *cm = load("..\\src\\cm\\xhcicm.inf");
    char *cm64 = load("..\\src\\cm\\xhcicm-amd64.inf");
    char *uas = load("..\\src\\uas\\xhciuas.inf");
    int i;

    CHECK(x86 != NULL && amd != NULL && cm != NULL && cm64 != NULL,
          "the INFs");
    if (x86 == NULL || amd == NULL || cm == NULL || cm64 == NULL) {
        return;
    }
    for (i = 0; i < trs_row_count(); i++) {
        const TRS_ROW *r = trs_row_at(i);
        const INST_VALUE *v = trs_value(r);
        INST_U32 d = 0xFFFFFFFFUL;
        int found;
        if (v == NULL) {
            continue;
        }
        if (r->AbsentDef == TRS_DEF_SYSTEM) {
            /* XhciResumeKeepBot (record 20 section 4.3.4, M18): written by
             * neither INF, its default this Windows's. */
            INST_ENV s;
            CHECK(r->InstallDef == TRS_DEF_SYSTEM && v->NoInf &&
                      !inst_inf_addreg_default(x86, r->Name, &d) &&
                      !inst_inf_addreg_default(amd, r->Name, &d),
                  "a row of this Windows's default: no INF writes it");
            memset(&s, 0, sizeof(s));
            s.Nt = 1;
            s.Major = 5;
            s.Minor = 0;
            trs_set_system(&s);
            CHECK(trs_absent_def(r) == 1 && trs_install_def(r) == 1,
                  "2000: 1");
            s.Minor = 1;
            trs_set_system(&s);
            CHECK(trs_absent_def(r) == 0 && trs_install_def(r) == 0,
                  "XP: 0");
            s.Nt = 0;
            s.Major = 4;
            s.Minor = 90;
            trs_set_system(&s);
            CHECK(trs_absent_def(r) == 0, "ME: 0");
            {
                int how;
                CHECK(trs_effective(r, 1, 2, TRS_NO_HOLD, &how) == 2 &&
                          how == TRS_HOW_AS_SET,
                      "XhciResumeKeepBot 2 runs as set");
                CHECK(trs_effective(r, 1, 5, TRS_NO_HOLD, &how) == 1 &&
                          how == TRS_HOW_HELD,
                      "XhciResumeKeepBot 5: its two low bits, 1");
                CHECK(trs_effective(r, 0, 0, TRS_NO_HOLD, &how) == 0 &&
                          how == TRS_HOW_DEFAULT,
                      "XhciResumeKeepBot absent on ME: 0");
            }
            memset(&s, 0, sizeof(s));
            trs_set_system(&s);
            continue;
        }
        if (v->Kind == INST_VK_TEXT) {
            char t[INST_TEXT_CHARS];
            CHECK(inst_inf_addreg_text(cm, r->Name, t, sizeof(t)) &&
                      strcmp(t, "auto") == 0,
                  "the text row's default is the INF's auto");
            continue;
        }
        if (v->Loc == INST_LOC_DRIVE) {
            CHECK(uas != NULL && inst_inf_addreg_default(uas, r->Name, &d) &&
                      d == r->InstallDef,
                  "a drive row's default is the UAS INF's");
            continue;
        }
        if (r->Tab == TRS_TAB_USB) {
            found = inst_inf_addreg_default(x86, r->Name, &d);
            CHECK(found, r->Name);
            CHECK_EQ(d, r->InstallDef, r->Name);
            d = 0xFFFFFFFFUL;
            found = inst_inf_addreg_default(amd, r->Name, &d);
            if (v->Apply == INST_APPLY_NT5X86_XHCI) {
                /* Mode 4's input pool: the x86 image's alone. */
                CHECK(!found, "not in the amd64 INF");
            } else {
                CHECK(found && d == r->InstallDef, "the amd64 INF too");
            }
        } else if (v->NoInf || v->Loc != INST_LOC_MW) {
            CHECK_EQ(v->Def, r->InstallDef, r->Name);
        } else {
            found = inst_inf_addreg_default(cm, r->Name, &d);
            CHECK(found, r->Name);
            CHECK_EQ(d, r->InstallDef, r->Name);
        }
        if (v->Hi != 0) {
            CHECK(r->InstallDef >= v->Lo && r->InstallDef <= v->Hi,
                  "a range's default in its range");
        }
    }
    free(x86);
    free(amd);
    free(cm);
    free(cm64);
    free(uas);
}

/* ---- every registry value has a row (task 49.38) --------------------------- */

/* The owner, 2026-11-13: the Tree's settings page covers every registry
 * value a driver of the package reads. This gate holds three lists to one
 * another: docs\using\tuning.md's value tables (each row "| `Name` | what
 * | min | max | default when absent | written by the install |"), the
 * values the code names (the INFs' HKR AddReg lines and the value
 * definitions of the files below), and the Tree's rows (trs_row_find; an
 * OUT row counts, it says why). A name in either list with no row, or a
 * row in neither, fails here unless the list below exempts it with why.
 * Each row's range and default are held to tuning.md's columns; the INFs'
 * written values are test_defaults'. */

/* Not a row of the page, with why. Remove an entry when its row comes. */
static const char *const trsNoRow[] = {
    /* The network adapter's own key, a string Windows' network Advanced
     * page ("MTU") sets; and NDIS's own three. */
    "XhciNetMtu",
    "*IfType",
    "*MediaType",
    "*PhysicalMediaType",
    /* Set by Setup's Thunderbolt networking box with the network driver it
     * stages (/tbnet, /notbnet), not by /set (tuning.md). */
    "XhciCmNet",
    NULL
};

/* Named in the code, not a setting: written by the drivers for the tools,
 * or the tools' requests to them. (qemu-flavour names, every one with
 * "Qemu" in it, and XhciHlprInject and the two XhciMsi86 switches, are
 * read inside XHCI_FLAVOUR_QEMU alone and skipped by the scan.) */
static const char *const trsNotSetting[] = {
    "XhciCmStatus", "XhciCmStatusAsk", "XhciCmEjectAsk", "XhciCmEjectConfirm",
    "XhciCmEjectReply", "XhciCmEjectResult", "XhciCmOscPending",
    "XhciCmQueryName", "XhciCmBridgeDiag", "XhciCmBridgeReqDiag",
    "XhciCmNoIrqDiag", "XhciUasCommandBytes", "XhciUasCommandSource",
    "XhciUasCommandStatus", "XhciUasDeviceLimit", "XhciUasDeviceLimitWhy",
    "XhciHlprInject", "XhciMsi86NoStrip", "XhciMsi86InjNoLine",
    NULL
};

/* A page row tuning.md does not carry, with why. */
static const char *const trsNoTuning[] = {
    /* Stor98's own settings, its README's "Settings" (StorTakeFirmware,
     * which tuning.md carries, is checked). */
    "StorWriteThrough", "StorPollMode", "StorMaxTransferKB", "StorPollBusyUs",
    "StorPollIdleMs", "StorPollSpinUs", "StorIntWatchdog", "StorGoneMs",
    "StorWriteIdleMs", "StorHold9xMs",
    NULL
};

static int trs_in(const char *const *list, const char *name)
{
    int i;

    for (i = 0; list[i] != NULL; i++) {
        if (inst_eq(list[i], name)) {
            return 1;
        }
    }
    return 0;
}

#define TUN_MAX     160
#define TUN_CELL    48

typedef struct _TUN_ROW {
    char Name[TUN_CELL];
    char Min[TUN_CELL];
    char Max[TUN_CELL];
    char Absent[TUN_CELL];
    char Written[TUN_CELL];
} TUN_ROW;

static TUN_ROW tunRows[TUN_MAX];
static int tunCount;

/* One cell, trimmed, its first `...` taken when it starts with one. */
static void tun_cell(const char *from, const char *to, char *out)
{
    size_t n;

    while (from < to && *from == ' ') {
        from++;
    }
    while (to > from && to[-1] == ' ') {
        to--;
    }
    if (from < to && *from == '`') {
        const char *e = from + 1;
        while (e < to && *e != '`') {
            e++;
        }
        from++;
        to = e;
    }
    n = (size_t)(to - from);
    if (n >= TUN_CELL) {
        n = TUN_CELL - 1;
    }
    memcpy(out, from, n);
    out[n] = '\0';
}

/* Every six-column value row of tuning.md ("| `Name` | ..." with seven
 * bars). A name twice (the bridges' table repeats five) must agree. */
static void tun_load(const char *text)
{
    const char *p = text;

    tunCount = 0;
    while (p != NULL && *p != '\0') {
        const char *eol = strchr(p, '\n');
        const char *bars[8];
        const char *q;
        int nb = 0;
        if (eol == NULL) {
            eol = p + strlen(p);
        }
        if (p[0] == '|' && p[1] == ' ' && p[2] == '`') {
            for (q = p; q < eol && nb < 8; q++) {
                if (*q == '|') {
                    bars[nb++] = q;
                }
            }
        }
        if (nb == 7) {
            TUN_ROW t;
            int k;
            int dup = 0;
            tun_cell(bars[0] + 1, bars[1], t.Name);
            tun_cell(bars[2] + 1, bars[3], t.Min);
            tun_cell(bars[3] + 1, bars[4], t.Max);
            tun_cell(bars[4] + 1, bars[5], t.Absent);
            tun_cell(bars[5] + 1, bars[6], t.Written);
            for (k = 0; k < tunCount; k++) {
                if (strcmp(tunRows[k].Name, t.Name) == 0) {
                    dup = 1;
                    if (strcmp(tunRows[k].Min, t.Min) != 0 ||
                        strcmp(tunRows[k].Max, t.Max) != 0 ||
                        strcmp(tunRows[k].Absent, t.Absent) != 0) {
                        printf("tuning.md: %s twice, unlike\n", t.Name);
                    }
                    CHECK(strcmp(tunRows[k].Min, t.Min) == 0 &&
                              strcmp(tunRows[k].Max, t.Max) == 0 &&
                              strcmp(tunRows[k].Absent, t.Absent) == 0,
                          "a value tuning.md lists twice reads alike");
                }
            }
            if (!dup && tunCount < TUN_MAX) {
                tunRows[tunCount++] = t;
            }
        }
        p = *eol != '\0' ? eol + 1 : eol;
    }
}

static const TUN_ROW *tun_find(const char *name)
{
    int k;

    for (k = 0; k < tunCount; k++) {
        if (inst_eq(tunRows[k].Name, name)) {
            return &tunRows[k];
        }
    }
    return NULL;
}

/* A cell that is a decimal number: 1 with it. */
static int tun_number(const char *s, INST_U32 *v)
{
    INST_U32 n = 0;

    if (*s < '0' || *s > '9') {
        return 0;
    }
    for (; *s >= '0' && *s <= '9'; s++) {
        n = n * 10UL + (INST_U32)(*s - '0');
    }
    *v = n;
    return *s == '\0';
}

#define CODE_MAX    256

static char codeNames[CODE_MAX][TUN_CELL];
static int codeCount;

static void code_add(const char *from, size_t n)
{
    char name[TUN_CELL];
    int k;

    if (n == 0 || n >= TUN_CELL) {
        return;
    }
    memcpy(name, from, n);
    name[n] = '\0';
    if (strstr(name, "Qemu") != NULL) {
        return;
    }
    /* XHCIPFW's results, written at each start for the tools; its one
     * setting is XhciPfwNhiLine. */
    if (strncmp(name, "XhciPfw", 7) == 0 &&
        strcmp(name, "XhciPfwNhiLine") != 0) {
        return;
    }
    for (k = 0; k < codeCount; k++) {
        if (strcmp(codeNames[k], name) == 0) {
            return;
        }
    }
    if (codeCount < CODE_MAX) {
        strcpy(codeNames[codeCount++], name);
    }
}

/* Every "Xhci..." in quotes: the value definitions (L"..." and NDIS's
 * NDIS_STRING_CONST("...") alike). */
static void code_scan_quoted(const char *text)
{
    const char *p = text;

    while ((p = strstr(p, "\"Xhci")) != NULL) {
        const char *e = p + 1;
        while ((*e >= 'A' && *e <= 'Z') || (*e >= 'a' && *e <= 'z') ||
               (*e >= '0' && *e <= '9')) {
            e++;
        }
        if (*e == '"') {
            code_add(p + 1, (size_t)(e - p - 1));
        }
        p = e;
    }
}

/* The INFs' HKR,,Name, lines: every Xhci value they write. */
static void code_scan_inf(const char *text)
{
    const char *p = text;

    while ((p = strstr(p, "HKR,,Xhci")) != NULL) {
        const char *e = p + 5;
        while (*e != ',' && *e != '\0' && *e != '\r' && *e != '\n') {
            e++;
        }
        if (*e == ',') {
            code_add(p + 5, (size_t)(e - p - 5));
        }
        p = e;
    }
}

/* The mark service's own Parameters values (mk_reg_dword with
 * MK_PARAMS_KEY): Restart and LogBytes. */
static void code_scan_mark(const char *text)
{
    const char *p = text;

    while ((p = strstr(p, "MK_PARAMS_KEY,")) != NULL) {
        const char *q = p + 14;
        const char *e;
        while (*q == ' ' || *q == '\r' || *q == '\n') {
            q++;
        }
        p = q;
        if (*q != '"') {
            continue;
        }
        e = q + 1;
        while (*e != '"' && *e != '\0') {
            e++;
        }
        if (*e == '"') {
            code_add(q + 1, (size_t)(e - q - 1));
        }
    }
}

static const char *const codeFiles[] = {
    "..\\src\\hcd_ctl.c", "..\\src\\xhci_quirk.h", "..\\src\\hcd_msi86.c",
    "..\\src\\hcd_msg4.c", "..\\src\\cm\\cm_reg.c", "..\\src\\cm\\cm_statos.c",
    "..\\src\\uas\\uas_fdo.c", "..\\src\\net\\ndis5\\n5_main.c",
    "..\\src\\net\\ndis6\\n6_main.c", "..\\src\\pfw\\pfw.c",
    "..\\src\\pfw\\nt\\pfw_nt.c", "..\\src\\hlpr\\hlpr_entry.c", NULL
};

static const char *const codeInfs[] = {
    "..\\src\\xhci98.inf", "..\\src\\xhci98-amd64.inf",
    "..\\src\\cm\\xhcicm.inf", "..\\src\\cm\\xhcicm-amd64.inf",
    "..\\src\\uas\\xhciuas.inf", "..\\src\\uas\\xhciuas-amd64.inf",
    "..\\src\\net\\xhcinet.inf", "..\\src\\net\\xhcinet-amd64.inf", NULL
};

/* The extremes a row admits: a range's Lo and Hi, a list's lowest and
 * highest value the driver takes. 0 for a Stor98 row. */
static int trs_extremes(const TRS_ROW *r, INST_U32 *lo, INST_U32 *hi)
{
    const INST_VALUE *v = trs_value(r);
    INST_U32 mask;
    INST_U32 b;

    if (v == NULL) {
        return 0;
    }
    if (v->Hi != 0) {
        *lo = v->Lo;
        *hi = v->Hi;
        return 1;
    }
    mask = r->Effect == TRS_EFF_LIST ? r->Accept : v->Settable;
    *lo = 32;
    *hi = 0;
    for (b = 0; b < 32UL; b++) {
        if (mask & (1UL << b)) {
            if (*lo == 32UL) {
                *lo = b;
            }
            *hi = b;
        }
    }
    return *lo != 32UL;
}

static void test_every_value(void)
{
    char *tun = load("..\\docs\\using\\tuning.md");
    int i;
    int k;

    CHECK(tun != NULL, "tuning.md");
    if (tun == NULL) {
        return;
    }
    tun_load(tun);
    CHECK(tunCount > 90, "tuning.md's value rows read");
    codeCount = 0;
    for (i = 0; codeFiles[i] != NULL; i++) {
        char *t = load(codeFiles[i]);
        CHECK(t != NULL, codeFiles[i]);
        if (t != NULL) {
            code_scan_quoted(t);
            free(t);
        }
    }
    for (i = 0; codeInfs[i] != NULL; i++) {
        char *t = load(codeInfs[i]);
        CHECK(t != NULL, codeInfs[i]);
        if (t != NULL) {
            code_scan_inf(t);
            free(t);
        }
    }
    {
        char *t = load("..\\xhcimark\\xhcimark.c");
        CHECK(t != NULL, "xhcimark.c");
        if (t != NULL) {
            code_scan_mark(t);
            free(t);
        }
    }
    CHECK(codeCount > 90, "the code's value names read");

    /* The code's names: a row, or exempt; and a tuning.md row. */
    for (k = 0; k < codeCount; k++) {
        const char *n = codeNames[k];
        if (trs_in(trsNotSetting, n)) {
            continue;
        }
        if (trs_row_find(n) == NULL && !trs_in(trsNoRow, n)) {
            printf("a value the code reads has no row: %s\n", n);
        }
        CHECK(trs_row_find(n) != NULL || trs_in(trsNoRow, n),
              "every value the code reads is a row of the page");
        if (tun_find(n) == NULL) {
            printf("a value the code reads is not in tuning.md: %s\n", n);
        }
        CHECK(tun_find(n) != NULL, "and a tuning.md row");
    }
    /* tuning.md's names: a row, or exempt. */
    for (k = 0; k < tunCount; k++) {
        if (trs_row_find(tunRows[k].Name) == NULL &&
            !trs_in(trsNoRow, tunRows[k].Name)) {
            printf("a tuning.md value has no row: %s\n", tunRows[k].Name);
        }
        CHECK(trs_row_find(tunRows[k].Name) != NULL ||
                  trs_in(trsNoRow, tunRows[k].Name),
              "every tuning.md value is a row of the page");
    }
    /* The rows: in tuning.md, unless exempt; their range and defaults as
     * its columns say, where a column is a number. */
    for (i = 0; i < trs_row_count(); i++) {
        const TRS_ROW *r = trs_row_at(i);
        const TUN_ROW *t = tun_find(r->Name);
        INST_U32 lo;
        INST_U32 hi;
        INST_U32 n;
        if (t == NULL) {
            if (!trs_in(trsNoTuning, r->Name)) {
                printf("a row tuning.md does not carry: %s\n", r->Name);
            }
            CHECK(trs_in(trsNoTuning, r->Name), "every row is in tuning.md");
            continue;
        }
        CHECK(!trs_in(trsNoTuning, r->Name), "an exemption still needed");
        CHECK(!trs_in(trsNoRow, r->Name), "a no-row entry with a row");
        if (trs_s98(r) >= 0) {
            continue;
        }
        if (trs_extremes(r, &lo, &hi)) {
            if (tun_number(t->Min, &n) && n != lo) {
                printf("%s: tuning.md's minimum %s, the row's %lu\n", r->Name,
                       t->Min, (unsigned long)lo);
            }
            CHECK(!tun_number(t->Min, &n) || n == lo, "the minimum agrees");
            if (tun_number(t->Max, &n) && n != hi) {
                printf("%s: tuning.md's maximum %s, the row's %lu\n", r->Name,
                       t->Max, (unsigned long)hi);
            }
            CHECK(!tun_number(t->Max, &n) || n == hi, "the maximum agrees");
        }
        if (r->AbsentDef == TRS_DEF_SYSTEM) {
            /* XhciResumeKeepBot: this Windows's default, written by Setup
             * and by no INF (test_defaults holds the INFs to that). */
            /* The cell's first `...`: Windows 2000's, as the row's. */
            INST_ENV w2k;
            memset(&w2k, 0, sizeof(w2k));
            w2k.Nt = 1;
            w2k.Major = 5;
            CHECK(tun_number(t->Absent, &n) && n == inst_keepbot_default(&w2k),
                  "a default by Windows: tuning.md's Windows 2000 one");
            CHECK(strncmp(t->Written, "by Setup", 8) == 0,
                  "written by Setup, not by the INF");
            continue;
        }
        if (tun_number(t->Absent, &n) && n != r->AbsentDef) {
            printf("%s: tuning.md's default %s, the row's %lu\n", r->Name,
                   t->Absent, (unsigned long)r->AbsentDef);
        }
        CHECK(!tun_number(t->Absent, &n) || n == r->AbsentDef,
              "the default when absent agrees");
        if (tun_number(t->Written, &n) && n != r->InstallDef) {
            printf("%s: tuning.md's written %s, the row's %lu\n", r->Name,
                   t->Written, (unsigned long)r->InstallDef);
        }
        CHECK(!tun_number(t->Written, &n) || n == r->InstallDef,
              "the value the install writes agrees");
    }
    /* Each exemption still names something. */
    for (i = 0; trsNoRow[i] != NULL; i++) {
        int seen = tun_find(trsNoRow[i]) != NULL;
        for (k = 0; k < codeCount && !seen; k++) {
            seen = inst_eq(codeNames[k], trsNoRow[i]);
        }
        CHECK(seen && trs_row_find(trsNoRow[i]) == NULL,
              "a no-row entry names a value without a row");
    }
    free(tun);
}

/* ---- task 49.38's rows ------------------------------------------------------ */

static void test_new_rows(void)
{
    TRS_ENV e;
    char out[96];
    const char *why;
    INST_U32 v[32];
    int n;
    int i;
    int how;
    const TRS_ROW *st = trs_row_find("XhciMsiStrategy");
    const TRS_ROW *irq = trs_row_find("XhciMsiIsaIrq");
    const TRS_ROW *pools = trs_row_find("XhciMsiPools");
    const TRS_ROW *first = trs_row_find("XhciMsiInputFirst");
    const TRS_ROW *acpi = trs_row_find("XhciAcpiTables");
    const TRS_ROW *quirk = trs_row_find("XhciStreamsOff");
    const TRS_ROW *amd = trs_row_find("XhciAmdSuspendBitClear");
    const TRS_ROW *net = trs_row_find("XhciCmNetRxFrames");
    const TRS_ROW *lb = trs_row_find("LogBytes");
    const TRS_ROW *im = trs_row_find("XhciInterruptMode");

    CHECK(st && irq && pools && first && acpi && quirk && amd && net && lb,
          "the new rows");
    if (!(st && irq && pools && first && acpi && quirk && amd && net && lb)) {
        return;
    }
    for (i = 0; i < instValueCount; i++) {
        const INST_VALUE *x = &instValues[i];
        int b;
        const TRS_ROW *r = trs_row_find(x->Name);
        if (x->Group == NULL) {
            continue;   /* task 49.38's rows are the grouped ones */
        }
        CHECK(r != NULL && r->View == TRS_VIEW_ADV, "each an advanced row");
        CHECK(x->Asked == 0, "and never asked by Setup");
        CHECK(strchr(x->Meaning, '(') == NULL && strchr(x->Meaning, '\n') == NULL,
              "the meaning carries no choice list and no line break");
        CHECK(x->Caution == NULL || strstr(x->Caution, "%s") == NULL,
              "no %s caution (inst_caution_text is Setup's rows')");
        for (b = 0; b < INST_CHOICES; b++) {
            if (x->Hi == 0 && (x->Settable & (1UL << b)) == 0) {
                CHECK(x->Choice[b] == NULL, "a choice only where allowed");
            }
        }
    }

    env_98(&e);
    CHECK_EQ(trs_state(st, &e), TRS_GREY, "mode-4 values greyed on 98 SE");
    CHECK(strcmp(trs_only(st, &e), "Windows 2000 and XP only") == 0, "its text");
    CHECK(!trs_parse(st, &e, "1", out, sizeof(out), &why), "and refused");
    CHECK_EQ(trs_state(acpi, &e), TRS_SHOW, "XhciAcpiTables on 98 SE");
    CHECK_EQ(trs_state(quirk, &e), TRS_SHOW, "a quirk on 98 SE");
    n = trs_choices(quirk, &e, 1, v, 32);
    CHECK(n == 3 && v[2] == 2, "a quirk: 0, 1 and 2");
    n = trs_choices(amd, &e, 1, v, 32);
    CHECK_EQ(n, 2, "a vendor quirk: 0 and 1");
    CHECK_EQ(trs_effective(amd, 1, 2, TRS_NO_HOLD, &how), 1, "2 read as 1");
    CHECK_EQ(trs_state(lb, &e), TRS_HIDE, "no mark service on 98 SE");
    trs_value_text(im, 4, out, sizeof(out));
    CHECK(strncmp(out, "4 - MSI", 7) == 0, "mode 4's own text");
    CHECK_EQ(trs_effective(im, 1, 4, TRS_NO_HOLD, &how), 4, "4 read as 4");
    CHECK_EQ(how, TRS_HOW_AS_SET, "as set");
    CHECK(!trs_parse(im, &e, "4", out, sizeof(out), &why),
          "4 refused on 98 SE (it runs as 0 there)");

    env_nt(&e, 5, 1);
    e.Inst.Arch = INST_ARCH_X86;
    CHECK_EQ(trs_state(st, &e), TRS_SHOW, "XP");
    CHECK_EQ(trs_state(pools, &e), TRS_SHOW, "32-bit XP: the pool");
    CHECK_EQ(trs_state(acpi, &e), TRS_GREY, "XhciAcpiTables greyed on NT");
    CHECK(trs_parse(irq, &e, "10", out, sizeof(out), &why), "IRQ 10");
    CHECK(!trs_parse(irq, &e, "9", out, sizeof(out), &why), "IRQ 9 refused");
    CHECK(!trs_parse(irq, &e, "1", out, sizeof(out), &why), "IRQ 1 refused");
    CHECK(trs_parse(irq, &e, "24", out, sizeof(out), &why) &&
              trs_parse(irq, &e, "255", out, sizeof(out), &why), "24 to 255");
    CHECK(!trs_parse(irq, &e, "256", out, sizeof(out), &why), "256 refused");
    trs_range_text(irq, &e, out, sizeof(out));
    CHECK(strcmp(out, "0, 3 to 7, 10, 11, 24 to 255") == 0, "its range text");
    CHECK(!trs_parse(first, &e, "23", out, sizeof(out), &why), "first 23 refused");
    CHECK(trs_parse(first, &e, "0", out, sizeof(out), &why) &&
              trs_parse(first, &e, "24", out, sizeof(out), &why), "0 and 24");
    trs_range_text(first, &e, out, sizeof(out));
    CHECK(strcmp(out, "0, 24 to 255") == 0, "first's range text");
    CHECK_EQ(trs_effective(st, 1, 0xFFFFFFFFUL, TRS_NO_HOLD, &how), 0,
             "the reads' not-found number is the default");
    CHECK_EQ(how, TRS_HOW_DEFAULT, "said so");
    CHECK_EQ(trs_effective(st, 1, 5, TRS_NO_HOLD, &how), 5, "5 as set");
    n = trs_choices(im, &e, 1, v, 32);
    CHECK(n == 4 && v[3] == 4, "XP: mode 4 with advanced");
    {
        const TRS_ROW *sh = trs_row_find("XhciMsiShare");
        CHECK_EQ(trs_effective(sh, 1, 1, TRS_NO_HOLD, &how), 1, "share 1 is 1");
        CHECK_EQ(how, TRS_HOW_AS_SET, "as set");
        CHECK_EQ(trs_effective(sh, 1, 0, TRS_NO_HOLD, &how), 1, "0 is 1");
        CHECK_EQ(how, TRS_HOW_HELD, "read as 1");
        CHECK_EQ(trs_effective(sh, 1, 2, TRS_NO_HOLD, &how), 2, "2 is 2");
        CHECK_EQ(trs_effective(sh, 1, 16, TRS_NO_HOLD, &how), 16, "16 is 16");
        CHECK_EQ(trs_effective(sh, 1, 17, TRS_NO_HOLD, &how), 16, "17 is 16");
        CHECK(!trs_parse(sh, &e, "0", out, sizeof(out), &why) &&
                  trs_parse(sh, &e, "1", out, sizeof(out), &why) &&
                  trs_parse(sh, &e, "16", out, sizeof(out), &why) &&
                  !trs_parse(sh, &e, "17", out, sizeof(out), &why),
              "1 to 16 set, 0 and 17 refused");
        trs_range_text(sh, &e, out, sizeof(out));
        CHECK(strcmp(out, "1 to 16") == 0, "share's range text");
        {
            int si = inst_value_find("XhciMsiShare");
            CHECK(!inst_value_allowed(si, &e.Inst, 0) &&
                      inst_value_allowed(si, &e.Inst, 1) &&
                      inst_value_allowed(si, &e.Inst, 8) &&
                      inst_value_allowed(si, &e.Inst, 16) &&
                      !inst_value_allowed(si, &e.Inst, 17),
                  "Setup: 1 to 16, 0 refused");
        }
        CHECK_EQ(trs_effective(sh, 1, 0xFFFFFFFFUL, TRS_NO_HOLD, &how), 8,
                 "not found is 8 (the owner, 2026-11-13)");
        CHECK_EQ(trs_absent_def(sh), 8, "absent: 8");
        CHECK_EQ(trs_install_def(sh), 8, "the INFs' 8");
    }
    e.Inst.Arch = INST_ARCH_AMD64;
    e.Inst.Minor = 2;
    CHECK(!trs_parse(irq, &e, "24", out, sizeof(out), &why) &&
              trs_parse(irq, &e, "11", out, sizeof(out), &why),
          "XP x64: no input above 23");
    trs_range_text(irq, &e, out, sizeof(out));
    CHECK(strcmp(out, "0, 3 to 7, 10, 11") == 0, "XP x64's range text");
    CHECK_EQ(trs_state(st, &e), TRS_SHOW, "XP x64: the strategy");
    CHECK_EQ(trs_state(pools, &e), TRS_GREY, "XP x64: no pool");
    CHECK(strcmp(trs_only(pools, &e), "32-bit Windows 2000 and XP only") == 0,
          "its text");
    CHECK(trs_state(lb, &e) == TRS_HIDE, "no mark key: hidden");
    e.Mark = 1;
    CHECK_EQ(trs_state(lb, &e), TRS_SHOW, "the mark service's LogBytes");
    CHECK_EQ(trs_loc(lb), TRS_LOC_MARK, "its own key");
    CHECK_EQ(trs_effective(lb, 1, 100, TRS_NO_HOLD, &how), 4096,
             "below 4096 is 4096");
    CHECK(trs_parse(net, &e, "default", out, sizeof(out), &why) &&
              strcmp(out, "128") == 0, "a networking default");
    CHECK(!trs_parse(net, &e, "31", out, sizeof(out), &why), "below its range");
    env_nt(&e, 6, 1);
    CHECK_EQ(trs_state(st, &e), TRS_GREY, "7: mode 4 is not read");
    e.Inst.CmNhi = 0;
    CHECK_EQ(trs_state(net, &e), TRS_HIDE, "no NHI: no networking rows");
    CHECK(trs_value(trs_row_find("XhciCmNetPollMs"))->Group != NULL &&
              trs_value(trs_row_find("XhciLinkKeep"))->Group == NULL,
          "the new rows in groups, the old under Advanced settings");
    {
        /* Task 49.38: the text row and the drive rows. */
        const TRS_ROW *tb = trs_row_find("XhciCmTunnelBridge");
        const TRS_ROW *na = trs_row_find("XhciUasNoAtaPassThrough");
        const TRS_ROW *fr = trs_row_find("XhciUasFastReset");
        TRS_RAW raw;
        TRS_SUM sum;
        char t[256];
        env_nt(&e, 5, 1);
        CHECK(tb != NULL && na != NULL && fr != NULL, "the rows");
        if (tb != NULL && na != NULL && fr != NULL) {
            CHECK(trs_is_range(tb), "the text row has a box");
            CHECK(trs_parse(tb, &e, "04:00.0,05:1f.7", out, sizeof(out),
                            &why) && strcmp(out, "04:00.0,05:1f.7") == 0,
                  "a list");
            CHECK(trs_parse(tb, &e, "default", out, sizeof(out), &why) &&
                      strcmp(out, "auto") == 0, "Default is auto");
            CHECK(trs_parse(tb, &e, "AUTO", out, sizeof(out), &why) &&
                      strcmp(out, "auto") == 0, "auto in any case");
            CHECK(!trs_parse(tb, &e, "04:00", out, sizeof(out), &why),
                  "a list the driver cannot read");
            memset(&raw, 0, sizeof(raw));
            raw.State = TRS_RAW_OTHER;
            raw.IsText = 1;
            strcpy(raw.Text, "04:00.0");
            trs_raw_text(tb, &raw, t, sizeof(t));
            CHECK(strcmp(t, "04:00.0") == 0, "the string as it reads");
            raw.State = TRS_RAW_ABSENT;
            trs_raw_text(tb, &raw, t, sizeof(t));
            CHECK(strstr(t, "auto") != NULL, "absent: auto");
            raw.State = TRS_RAW_DWORD;
            trs_raw_text(tb, &raw, t, sizeof(t));
            CHECK(strstr(t, "not text") != NULL, "a number there");
            CHECK_EQ(trs_loc(na), TRS_LOC_DEVICE, "a drive's own key");
            CHECK_EQ(trs_state(na, &e), TRS_HIDE, "no UAS drive: hidden");
            e.UasDrives = 1;
            CHECK_EQ(trs_state(na, &e), TRS_SHOW, "a UAS drive");
            env_98(&e);
            e.UasDrives = 1;
            CHECK_EQ(trs_state(na, &e), TRS_SHOW, "98 SE too");
            CHECK_EQ(trs_apply(na, 1), TRS_AP_REPLUG, "the drive plugged again");
            trs_sum_init(&sum);
            trs_sum_add(&sum, na, 1);
            trs_sum_add(&sum, fr, 1);
            trs_sum_text(&sum, 1, t, sizeof(t));
            CHECK(strstr(t, "XhciUasNoAtaPassThrough and XhciUasFastReset "
                            "take effect when the drive is plugged in again") !=
                      NULL &&
                      strstr(t, "Restart") == NULL,
                  "said for the drive values");
            CHECK_EQ(trs_sum_exit(&sum, 0, 0), INST_EXIT_STEPS, "a step left");
            trs_sum_init(&sum);
            trs_sum_add(&sum, na, 1);
            trs_sum_add(&sum, trs_row_find("XhciForceBulkOnly"), 1);
            trs_sum_text(&sum, 1, t, sizeof(t));
            CHECK(strstr(t, "XhciUasNoAtaPassThrough takes effect") != NULL &&
                      strstr(t, "XhciForceBulkOnly takes effect") != NULL,
                  "both replug lines");
        }
    }
    {
        const TRS_ROW *sp = trs_row_find("XhciStagePoolKiB");
        CHECK_EQ(trs_effective(sp, 1, 319, TRS_NO_HOLD, &how), 256,
                 "the pool in whole 64 KiB blocks");
        CHECK(!trs_parse(sp, &e, "300", out, sizeof(out), &why) &&
                  trs_parse(sp, &e, "320", out, sizeof(out), &why),
              "and offered so");
    }
}

/* ---- the system rules and the values offered --------------------------- */

static void test_rules(void)
{
    TRS_ENV e;
    INST_U32 v[32];
    int n;
    const TRS_ROW *psw = trs_row_find("XhciIntelPortSwitch");
    const TRS_ROW *nr = trs_row_find("XhciIntelNoRouteIrq");
    const TRS_ROW *ir = trs_row_find("XhciIntelIrqRoute");
    const TRS_ROW *si = trs_row_find("XhciSilentInstall");
    const TRS_ROW *im = trs_row_find("XhciInterruptMode");
    const TRS_ROW *cmm = trs_row_find("XhciCmMode");
    const TRS_ROW *pfw = trs_row_find("XhciPfwNhiLine");
    const TRS_ROW *mk = trs_row_find("Restart");
    const TRS_ROW *h9 = trs_row_find("StorHold9xMs");
    const TRS_ROW *tf = trs_row_find("StorTakeFirmware");

    env_98(&e);
    CHECK_EQ(trs_state(psw, &e), TRS_GREY, "no gated controller: greyed");
    e.Inst.PswController = 1;
    CHECK_EQ(trs_state(psw, &e), TRS_SHOW, "a gated controller");
    n = trs_choices(psw, &e, 1, v, 32);
    CHECK(n == 2 && v[0] == 0 && v[1] == 1, "0 and 1 only, 2 a hand edit");
    CHECK_EQ(trs_state(ir, &e), TRS_HIDE, "no reviewed Intel controller");
    CHECK_EQ(trs_state(nr, &e), TRS_HIDE, "no reviewed Intel controller 2");
    e.Inst.PirqController = 1;
    CHECK_EQ(trs_state(ir, &e), TRS_SHOW, "every system");
    CHECK_EQ(trs_state(nr, &e), TRS_GREY, "98 SE: 2000 and XP only");
    CHECK(strcmp(trs_only(nr, &e), "Windows 2000 and XP only") == 0, "its text");
    CHECK_EQ(trs_state(si, &e), TRS_SHOW, "silent install on 98 SE");
    CHECK_EQ(trs_state(pfw, &e), TRS_HIDE, "no XHCIPFW.VXD");
    e.Pfw = 1;
    CHECK_EQ(trs_state(pfw, &e), TRS_SHOW, "XHCIPFW.VXD");
    CHECK_EQ(trs_loc(pfw), TRS_LOC_PFW, "its own key");
    CHECK_EQ(trs_state(mk, &e), TRS_HIDE, "no mark service on 98 SE");
    CHECK_EQ(trs_state(h9, &e), TRS_SHOW, "StorHold9xMs on 98 SE");
    CHECK_EQ(trs_state(tf, &e), TRS_SHOW, "StorTakeFirmware on 98 SE");
    CHECK(trs_only(tf, &e) != NULL &&
          strcmp(trs_only(tf, &e), "Windows 98 SE and ME only") == 0,
          "StorTakeFirmware's family on 98 SE");
    e.Inst.Minor = 90;
    CHECK_EQ(trs_state(tf, &e), TRS_GREY, "StorTakeFirmware greyed on ME (48.28)");
    CHECK(strcmp(trs_only(tf, &e), INST_S98_ME_WHY) == 0, "and why");
    CHECK_EQ(trs_state(h9, &e), TRS_SHOW, "StorHold9xMs still on ME");
    {
        char o[32];
        const char *w;
        CHECK(!trs_parse(tf, &e, "1", o, sizeof(o), &w),
              "StorTakeFirmware refused on ME");
    }
    e.Inst.Minor = 10;
    n = trs_choices(im, &e, 1, v, 32);
    CHECK(n == 3 && v[2] == 2, "98 SE: no 3 and no 4, even with advanced");
    n = trs_choices(im, &e, 0, v, 32);
    CHECK_EQ(n, 3, "and without it");
    {
        const TRS_ROW *ri = trs_row_find("XhciCmRingInterrupt");
        e.Inst.CmNhi = 1;
        n = trs_choices(ri, &e, 1, v, 32);
        CHECK(n == 2 && v[0] == 0 && v[1] == 1,
              "98 SE: the ring interrupt 0 and 1, no 2");
        e.Inst.CmNhi = 0;
    }

    env_nt(&e, 5, 2);
    e.Inst.PirqController = 1;
    CHECK_EQ(trs_state(nr, &e), TRS_SHOW, "XP x64");
    env_nt(&e, 5, 0);
    e.Inst.PirqController = 1;
    CHECK_EQ(trs_state(nr, &e), TRS_SHOW, "2000");
    CHECK_EQ(trs_state(si, &e), TRS_GREY, "silent install greyed on NT");
    CHECK_EQ(trs_state(h9, &e), TRS_GREY, "StorHold9xMs greyed on NT");
    CHECK_EQ(trs_state(pfw, &e), TRS_HIDE, "no XHCIPFW on NT");
    e.Mark = 1;
    CHECK_EQ(trs_state(mk, &e), TRS_SHOW, "the mark service");
    CHECK_EQ(trs_loc(mk), TRS_LOC_MARK, "its own key");
    env_nt(&e, 6, 1);
    e.Inst.PirqController = 1;
    CHECK_EQ(trs_state(nr, &e), TRS_GREY, "7: not read");
    n = trs_choices(im, &e, 1, v, 32);
    CHECK(n == 4 && v[3] == 3, "MSI only on 7, no mode 4");
    n = trs_choices(cmm, &e, 0, v, 32);
    CHECK_EQ(n, 2, "XhciCmMode 0 and 1 without advanced");
    n = trs_choices(cmm, &e, 1, v, 32);
    CHECK_EQ(n, 4, "and 2 and 3 with it");
    e.Inst.CmNhi = 0;
    CHECK_EQ(trs_state(cmm, &e), TRS_HIDE, "no NHI: no Thunderbolt rows");
    e.Inst.AnyXhci = 0;
    CHECK_EQ(trs_state(im, &e), TRS_HIDE, "no controller: no USB rows");
}

static void test_parse(void)
{
    TRS_ENV e;
    char out[32];
    const char *why;
    const TRS_ROW *psw = trs_row_find("XhciIntelPortSwitch");
    const TRS_ROW *imod = trs_row_find("XhciImodInterval250ns");
    const TRS_ROW *mt = trs_row_find("XhciMaxTransferKB");
    const TRS_ROW *wt = trs_row_find("StorWriteThrough");
    const TRS_ROW *si = trs_row_find("XhciSilentInstall");

    env_98(&e);
    e.Inst.PswController = 1;
    CHECK(trs_parse(psw, &e, "1", out, sizeof(out), &why) &&
              strcmp(out, "1") == 0, "1");
    CHECK(!trs_parse(psw, &e, "2", out, sizeof(out), &why), "2 refused");
    CHECK(!trs_parse(psw, &e, "x", out, sizeof(out), &why), "not a number");
    CHECK(!trs_parse(psw, &e, "0x1", out, sizeof(out), &why), "no hex");
    CHECK(trs_parse(psw, &e, "default", out, sizeof(out), &why) &&
              strcmp(out, "0") == 0, "default");
    CHECK(trs_parse(imod, &e, "10", out, sizeof(out), &why), "imod low");
    CHECK(!trs_parse(imod, &e, "9", out, sizeof(out), &why), "imod below");
    CHECK(trs_parse(imod, &e, "default", out, sizeof(out), &why) &&
              strcmp(out, "160") == 0, "imod's installed default");
    CHECK(!trs_parse(mt, &e, "66", out, sizeof(out), &why), "off its step");
    CHECK(trs_parse(mt, &e, "68", out, sizeof(out), &why), "on its step");
    CHECK(trs_parse(wt, &e, "FUA", out, sizeof(out), &why) &&
              strcmp(out, "fua") == 0, "a Stor98 word");
    CHECK(!trs_parse(wt, &e, "fast", out, sizeof(out), &why), "a bad word");
    CHECK(trs_parse(si, &e, "1", out, sizeof(out), &why), "98 SE: settable");
    env_nt(&e, 5, 1);
    CHECK(!trs_parse(si, &e, "1", out, sizeof(out), &why), "NT: greyed");
    CHECK(!trs_parse(trs_row_find("XhciLogVerbosity"), &e, "3", out,
                     sizeof(out), &why), "the log level is not /set's");
}

/* ---- the driver's reading ---------------------------------------------- */

static void test_effective(void)
{
    int how;
    const TRS_ROW *fbo = trs_row_find("XhciForceBulkOnly");
    const TRS_ROW *ss = trs_row_find("XhciSelectiveSuspend");
    const TRS_ROW *imod = trs_row_find("XhciImodInterval250ns");
    const TRS_ROW *idle = trs_row_find("XhciPollIdleMs");
    const TRS_ROW *act = trs_row_find("XhciPollActiveMs");
    const TRS_ROW *im = trs_row_find("XhciInterruptMode");
    const TRS_ROW *cmp = trs_row_find("XhciCmPollIdleMs");
    TRS_RAW m;
    TRS_RAW own[2];
    char t[200];

    CHECK_EQ(trs_effective(fbo, 0, 0, TRS_NO_HOLD, &how), 1, "absent bulk-only is 1");
    CHECK_EQ(how, TRS_HOW_DEFAULT, "as the default");
    CHECK_EQ(trs_effective(fbo, 1, 7, TRS_NO_HOLD, &how), 1, "7 is on");
    CHECK_EQ(how, TRS_HOW_AS_ON, "read as on");
    CHECK_EQ(trs_effective(ss, 1, 2, TRS_NO_HOLD, &how), 0, "selective suspend: only 1");
    CHECK_EQ(trs_effective(imod, 1, 5, TRS_NO_HOLD, &how), 4000, "below range: 4000");
    CHECK_EQ(trs_effective(idle, 1, 0, TRS_NO_HOLD, &how), 50, "0: the default 50");
    CHECK_EQ(trs_effective(idle, 1, 9000, TRS_NO_HOLD, &how), 5000, "held to 5000");
    CHECK_EQ(trs_effective(act, 1, 80, 50, &how), 50, "held to the idle period");
    CHECK_EQ(trs_effective(trs_row_find("XhciFirstEnumPortMs"), 1, 900, 0, &how),
             0, "held to a total of 0");
    CHECK_EQ(trs_effective(trs_row_find("XhciFirstEnumPortMs"), 0, 0, 1000,
                           &how), 1000, "absent: its default held too");
    CHECK_EQ(trs_effective(im, 1, 9, TRS_NO_HOLD, &how), 0, "an unlisted mode is 0");
    CHECK_EQ(trs_effective(cmp, 1, 10, TRS_NO_HOLD, &how), 50, "CM period clamped");

    m.State = TRS_RAW_ABSENT;
    own[0].State = TRS_RAW_DWORD;
    own[0].Value = 1;
    own[1].State = TRS_RAW_DWORD;
    own[1].Value = 1;
    trs_now_text(fbo, &m, own, 2, t, sizeof(t));
    CHECK(strcmp(t, "1 - Bulk-Only") == 0, "the common value");
    own[1].Value = 0;
    trs_now_text(fbo, &m, own, 2, t, sizeof(t));
    CHECK(strstr(t, "not the same") != NULL, "mixed");
    m.State = TRS_RAW_DWORD;
    m.Value = 0;
    trs_now_text(fbo, &m, own, 2, t, sizeof(t));
    CHECK(strstr(t, "machine-wide") != NULL && t[0] == '0',
          "the machine-wide value wins");
    m.State = TRS_RAW_OTHER;
    trs_now_text(fbo, &m, own, 2, t, sizeof(t));
    CHECK(strstr(t, "not a number") != NULL, "machine-wide not a DWORD");
    m.State = TRS_RAW_ABSENT;
    own[0].State = TRS_RAW_ABSENT;
    trs_now_text(fbo, &m, own, 1, t, sizeof(t));
    CHECK(strstr(t, "not set") != NULL && strstr(t, "Bulk-Only") != NULL,
          "absent: the driver's default named");
    own[0].State = TRS_RAW_DWORD;
    own[0].Value = 80;
    trs_now_text_held(act, &m, own, 1, 50, t, sizeof(t));
    CHECK(strstr(t, "reads it as 50") != NULL, "held to the idle period");
    {
        INST_U32 held[2];

        /* Each controller held to its own idle period (10 and 50 ms). */
        m.State = TRS_RAW_DWORD;
        m.Value = 100;
        held[0] = 10;
        held[1] = 50;
        trs_now_text_each(act, &m, own, 2, held, t, sizeof(t));
        CHECK(strstr(t, "not the same") != NULL,
              "machine-wide 100, held to 10 and to 50: not one value");
        held[0] = 50;
        trs_now_text_each(act, &m, own, 2, held, t, sizeof(t));
        CHECK(strstr(t, "reads it as 50") != NULL &&
                  strstr(t, "machine-wide") != NULL,
              "both held to 50: the one value");
        m.Value = 5;
        held[0] = 10;
        trs_now_text_each(act, &m, own, 2, held, t, sizeof(t));
        CHECK(t[0] == '5' && strstr(t, "reads it as") == NULL,
              "5 under both holds: 5 on each");
        m.State = TRS_RAW_ABSENT;
        own[0].State = TRS_RAW_DWORD;
        own[0].Value = 80;
        own[1].State = TRS_RAW_DWORD;
        own[1].Value = 80;
        trs_now_text_each(act, &m, own, 2, held, t, sizeof(t));
        CHECK(strstr(t, "not the same") != NULL,
              "the same own 80, held to 10 and to 50: not one value");
        trs_now_text_each(act, &m, own, 2, NULL, t, sizeof(t));
        CHECK(t[0] == '8' && strstr(t, "not the same") == NULL,
              "no hold: the common value");
        own[1].Value = 70;
        trs_now_text_each(fbo, &m, own, 2, NULL, t, sizeof(t));
        CHECK(strstr(t, "not the same") != NULL, "no hold, mixed: as before");
    }
    CHECK(trs_held_by(act) == idle && trs_held_by(idle) == NULL, "which row");
}

/* ---- applying and the exit code ------------------------------------------ */

static void test_apply(void)
{
    TRS_SUM s;
    char t[1024];

    trs_sum_init(&s);
    CHECK_EQ(trs_sum_exit(&s, 0, 0), INST_EXIT_DONE, "nothing: 0");
    trs_sum_add(&s, trs_row_find("XhciForceBulkOnly"), 1);
    CHECK_EQ(trs_sum_exit(&s, 0, 0), INST_EXIT_STEPS, "a replug: 4");
    trs_sum_text(&s, 1, t, sizeof(t));
    CHECK(strstr(t, "plugged in again") != NULL &&
              strstr(t, "Device Manager") != NULL, "said, with NT's uninstall");
    CHECK(strstr(t, "XhciForceBulkOnly takes") == t &&
              strstr(t, "With XhciBulkOnlyOwnId at 0") != NULL,
          "the force value alone, its caveat under the own-id value");
    CHECK(strstr(t, "Restart") == NULL, "no restart for it");
    {
        TRS_SUM o;
        char u[1024];

        /* UASBOT.1: the own-id value is a replug value too, named. */
        trs_sum_init(&o);
        trs_sum_add(&o, trs_row_find("XhciBulkOnlyOwnId"), 1);
        CHECK_EQ(trs_sum_exit(&o, 0, 0), INST_EXIT_STEPS, "own id: a replug");
        trs_sum_text(&o, 1, u, sizeof(u));
        CHECK(strcmp(u, "XhciBulkOnlyOwnId takes effect when a drive is "
                        "plugged in again.") == 0, "own id alone");
        trs_sum_add(&o, trs_row_find("XhciForceBulkOnly"), 1);
        trs_sum_text(&o, 0, u, sizeof(u));
        CHECK(strcmp(u, "XhciForceBulkOnly and XhciBulkOnlyOwnId take effect "
                        "when a drive is plugged in again.") == 0,
              "both, on 98 SE and ME no caveat");
        trs_sum_text(&o, 1, u, sizeof(u));
        CHECK(strstr(u, "With XhciBulkOnlyOwnId at 0") != NULL,
              "both, on NT the caveat");
        CHECK_EQ(trs_apply(trs_row_find("XhciBulkOnlyOwnId"), 1),
                 TRS_AP_REPLUG, "own id: replug on NT too");
    }
    trs_sum_add(&s, trs_row_find("XhciIntelPortSwitch"), 1);
    CHECK_EQ(trs_sum_exit(&s, 0, 0), INST_EXIT_STEPS,
             "record 23's precedence: the step outranks the restart");
    trs_sum_init(&s);
    trs_sum_add(&s, trs_row_find("XhciIntelPortSwitch"), 1);
    CHECK_EQ(trs_sum_exit(&s, 0, 0), INST_EXIT_RESTART, "a restart: 3");
    CHECK_EQ(trs_sum_exit(&s, 0, 1), INST_EXIT_FAILED, "failed: 2");
    CHECK_EQ(trs_sum_exit(&s, 1, 0), INST_EXIT_REFUSED, "refused: 1");
    trs_sum_text(&s, 1, t, sizeof(t));
    CHECK(strstr(t, "XhciIntelPortSwitch") != NULL, "named");
    CHECK(strstr(t, "Device Manager") == NULL, "the port switch: restart only");
    trs_sum_init(&s);
    trs_sum_add(&s, trs_row_find("XhciIntelNoRouteIrq"), 1);
    trs_sum_add(&s, trs_row_find("XhciInterruptMode"), 1);
    trs_sum_text(&s, 1, t, sizeof(t));
    CHECK(strstr(t, "Device Manager") != NULL, "a cycle applies both");
    trs_sum_text(&s, 0, t, sizeof(t));
    CHECK(strstr(t, "Device Manager") == NULL, "never said on 98 SE");
    trs_sum_init(&s);
    trs_sum_add(&s, trs_row_find("XhciInterruptMode"), 0);
    trs_sum_text(&s, 1, t, sizeof(t));
    CHECK(strstr(t, "Device Manager") == NULL, "98 SE's change: restart");
    CHECK_EQ(trs_apply(trs_row_find("XhciCmAsm4242Reads"), 1), TRS_AP_BOOT,
             "the handover: once per boot");
    CHECK_EQ(trs_apply(trs_row_find("StorPollMode"), 1), TRS_AP_BOOT,
             "Stor98: restart");
    CHECK_EQ(trs_apply(trs_row_find("XhciForceBulkOnly"), 0), TRS_AP_REPLUG,
             "bulk-only replug on 98 SE too");
}

/* ---- the request ------------------------------------------------------- */

static void test_request(void)
{
    TRS_CHANGE c[TRS_MAX_CHANGES + 1];
    TRS_CHANGE back[TRS_MAX_CHANGES];
    char buf[4096];
    int n;
    int i;
    const char *why;

    memset(c, 0, sizeof(c));
    tr_copy(c[0].Name, "XhciIntelPortSwitch", sizeof(c[0].Name));
    tr_copy(c[0].Value, "1", sizeof(c[0].Value));
    tr_copy(c[1].Name, "StorWriteThrough", sizeof(c[1].Name));
    tr_copy(c[1].Value, "fua", sizeof(c[1].Value));
    tr_copy(c[1].On, "PCI\\VEN_8086&DEV_1E31&SUBSYS_179B103C&REV_04\\3&11583659&0&A0",
            sizeof(c[1].On));
    CHECK(trs_req_make(c, 2, buf, sizeof(buf)), "made");
    CHECK(trs_req_parse(buf, sizeof(buf), back, TRS_MAX_CHANGES, &n) && n == 2,
          "read back");
    CHECK(strcmp(back[0].Name, c[0].Name) == 0 && back[0].On[0] == '\0' &&
              strcmp(back[1].On, c[1].On) == 0 &&
              strcmp(back[1].Value, "fua") == 0, "the same changes");
    CHECK(!trs_req_make(c, 2, buf, 40), "too small: nothing, no prefix");
    CHECK(buf[0] == '\0', "and empty");
    for (i = 0; i < TRS_MAX_CHANGES; i++) {
        tr_copy(c[i].Name, "XhciCmApproveTimeoutMs", sizeof(c[i].Name));
        tr_copy(c[i].Value, "10000", sizeof(c[i].Value));
        tr_copy(c[i].On, "PCI\\VEN_8086&DEV_15D2&SUBSYS_00000000&REV_02\\4&2A3B0C&0&0008",
                sizeof(c[i].On));
    }
    CHECK(!trs_req_make(c, TRS_MAX_CHANGES, buf, sizeof(buf)),
          "48 long changes do not fit 4096");
    CHECK(!trs_req_make(c, TRS_MAX_CHANGES + 1, buf, sizeof(buf)),
          "past the most");
    tr_copy(c[0].Value, "1\n", sizeof(c[0].Value));
    CHECK(!trs_req_make(c, 1, buf, sizeof(buf)), "a line break refused");
    strcpy(buf, "XHCITR-SET 1\nXhciWideMap=1\t*\n");
    CHECK(!trs_req_parse(buf, sizeof(buf), back, 4, &n), "no END");
    strcpy(buf, "XHCITR-SET 2\nEND\n");
    CHECK(!trs_req_parse(buf, sizeof(buf), back, 4, &n), "another version");
    strcpy(buf, "XHCITR-SET 1\nXhciWideMap 1\t*\nEND\n");
    CHECK(!trs_req_parse(buf, sizeof(buf), back, 4, &n), "no =");
    strcpy(buf, "XHCITR-SET 1\nEND\nmore");
    CHECK(!trs_req_parse(buf, sizeof(buf), back, 4, &n), "after END");
    strcpy(buf, "XHCITR-SET 1\nA=1\t*\nB=1\t*\nEND\n");
    CHECK(!trs_req_parse(buf, sizeof(buf), back, 1, &n), "too many");
    memset(buf, 'x', sizeof(buf));
    CHECK(!trs_req_parse(buf, sizeof(buf), back, 4, &n), "no NUL");
    CHECK(trs_arg("XhciWideMap=1", &c[0], &why) &&
              strcmp(c[0].Name, "XhciWideMap") == 0 &&
              strcmp(c[0].Value, "1") == 0, "an argument");
    CHECK(!trs_arg("=1", &c[0], &why), "no name");
    CHECK(!trs_arg("XhciWideMap=", &c[0], &why), "no value");
}

static void test_texts(void)
{
    char t[1024];

    trs_psw_text(1, 0, t, sizeof(t));
    CHECK(strstr(t, "move from the USB 2.0") != NULL, "on");
    trs_psw_text(0, 0, t, sizeof(t));
    CHECK(strstr(t, "stay with the USB 2.0") != NULL &&
              strstr(t, "no working driver") == NULL, "off");
    trs_psw_text(0, 1, t, sizeof(t));
    CHECK(strstr(t, "no working driver") != NULL, "off, no EHCI driver");
    CHECK(strstr(trs_caution(trs_row_find("XhciCmSafeEject")),
                 "installed and removed by Setup") != NULL,
          "the Tree's own caution");
    CHECK(trs_caution(trs_row_find("XhciFastPollFsLs")) ==
              instValues[inst_value_find("XhciFastPollFsLs")].Caution,
          "else Setup's");
    trs_caution_text(trs_row_find("XhciInterruptMode"), 0xFUL, t, sizeof(t));
    CHECK(strstr(t, "%s") == NULL && strstr(t, "with 1 or 3,") != NULL,
          "Setup's %s named as Setup names it");
    trs_caution_text(trs_row_find("XhciInterruptMode"), 0x1UL, t, sizeof(t));
    CHECK(strstr(t, "with 1 or 3,") != NULL, "none offered: every one named");
    trs_caution_text(trs_row_find("XhciCmSafeEject"), 0x3UL, t, sizeof(t));
    CHECK(strstr(t, "installed and removed by Setup") != NULL,
          "the Tree's own caution as it is");
    trs_caution_text(trs_row_find("XhciLinkKeep"), 0x3UL, t, sizeof(t));
    CHECK(trs_caution(trs_row_find("XhciLinkKeep")) != NULL || t[0] == '\0',
          "no caution: empty");
}

/* ---- the command line (tr_core.c's tr_parse_args) ------------------------ */

static void test_args(void)
{
    TR_OPTS o;
    char err[512];
    char *a[8];

    a[0] = "x";
    a[1] = "/set";
    a[2] = "XhciWideMap=1";
    a[3] = "StorPollMode=1";
    CHECK(tr_parse_args(4, a, &o, err, sizeof(err)) == 0 && o.Set &&
              o.SetCount == 2 && strcmp(o.SetArg[1], "StorPollMode=1") == 0,
          "/set Name=Value Name=Value");
    a[4] = "/on";
    a[5] = "PCI\\VEN_8086&DEV_1E31\\3&0";
    CHECK(tr_parse_args(6, a, &o, err, sizeof(err)) == 0 && o.SetOn &&
              strcmp(o.SetOnId, a[5]) == 0, "/on");
    a[2] = "/show";
    CHECK(tr_parse_args(3, a, &o, err, sizeof(err)) == 0 && o.SetShow,
          "/set /show");
    CHECK(tr_parse_args(2, a, &o, err, sizeof(err)) == 1, "/set alone");
    a[2] = "XhciWideMap";
    CHECK(tr_parse_args(3, a, &o, err, sizeof(err)) == 1, "no =");
    a[2] = "/request";
    CHECK(tr_parse_args(3, a, &o, err, sizeof(err)) == 1,
          "/request without /result");
    a[3] = "/result";
    a[4] = "Local\\Xhci98TrAnswer-12-34";
    CHECK(tr_parse_args(5, a, &o, err, sizeof(err)) == 0 && o.SetRequest &&
              o.Result, "/set /request /result");
    a[2] = "XhciWideMap=1";
    CHECK(tr_parse_args(5, a, &o, err, sizeof(err)) == 1,
          "/result is the window's run alone");
    a[1] = "/tray";
    a[2] = "/set";
    a[3] = "XhciWideMap=1";
    CHECK(tr_parse_args(4, a, &o, err, sizeof(err)) == 1, "/set stands alone");
    a[1] = "/on";
    a[2] = "x";
    CHECK(tr_parse_args(3, a, &o, err, sizeof(err)) == 1, "/on without /set");
}

static void test_quick(void)
{
    char t[64];
    int i;

    CHECK_EQ(trs_qs_where(1, 1, 1), TRS_QS_INSTALL, "the install folder");
    CHECK_EQ(trs_qs_where(1, 0, 1), TRS_QS_DOCS, "not writable: documents");
    CHECK_EQ(trs_qs_where(0, 0, 1), TRS_QS_DOCS, "no record: documents");
    CHECK_EQ(trs_qs_where(1, 0, 0), TRS_QS_NONE, "nowhere");
    trs_qs_name(2026, 11, 7, 9, 5, 3, 1, t, sizeof(t));
    CHECK(strcmp(t, "2026-11-07_090503") == 0, "the dated name");
    trs_qs_name(2026, 11, 7, 9, 5, 3, 2, t, sizeof(t));
    CHECK(strcmp(t, "2026-11-07_090503-2") == 0, "a second in the same second");
    trs_hk_text(TRS_HK_DEFAULT, t, sizeof(t));
    CHECK(strcmp(t, "Ctrl+Alt+F11") == 0, "the default's text");
    CHECK_EQ(trs_hk_choice(0), TRS_HK_DEFAULT, "the default first");
    CHECK_EQ(trs_hk_keys(TRS_HK_DEFAULT), 3, "the default is three keys");
    {
        static const char *const want[TRS_HK_CHOICES] = {
            "Ctrl+Alt+F11", "Ctrl+Shift+F11", "Ctrl+Alt+F10", "Ctrl+Alt+F9",
            "Ctrl+F11", "Ctrl+Alt+Shift+F11"
        };
        static const int nkeys[TRS_HK_CHOICES] = { 3, 3, 3, 3, 2, 4 };
        for (i = 0; i < TRS_HK_CHOICES; i++) {
            trs_hk_text(trs_hk_choice(i), t, sizeof(t));
            CHECK(strcmp(t, want[i]) == 0, "the choices in the owner's order");
            CHECK_EQ(trs_hk_keys(trs_hk_choice(i)), nkeys[i], "keys pressed");
            CHECK_EQ(trs_hk_index(trs_hk_choice(i)), i, "a choice's index");
        }
    }
    CHECK_EQ(trs_hk_index((0x7UL << 16) | 0x79UL), -1, "not a choice");
    CHECK_EQ(trs_hk_index(0), -1, "off is no choice");
    for (i = 0; i < TRS_HK_CHOICES; i++) {
        CHECK(trs_hk_ok(trs_hk_choice(i)), "every choice accepted");
    }
    CHECK(!trs_hk_ok((0x7UL << 16) | 0x7BUL), "F12 refused");
    CHECK(!trs_hk_ok((0x4UL << 16) | 0x7AUL), "Shift alone refused");
    CHECK(!trs_hk_ok((0xAUL << 16) | 0x7AUL), "the Windows key refused");
    CHECK(!trs_hk_ok((0x3UL << 16) | 0x41UL), "a letter refused");
    CHECK(!trs_hk_ok(0), "off is not a key");
}

/* TRSETWARN.1: /set prints a value's note only for a change it accepts; a
 * refused one prints its refusal alone. */
static void test_set_note(void)
{
    TRS_ENV e;
    char t[800];
    char out[32];
    const char *why;
    const TRS_ROW *psw = trs_row_find("XhciIntelPortSwitch");
    const TRS_ROW *nr = trs_row_find("XhciIntelNoRouteIrq");

    env_98(&e);
    e.Inst.PswController = 0;
    CHECK(!trs_parse(psw, &e, "1", out, sizeof(out), &why) &&
              strcmp(why, "not a setting of this computer") == 0,
          "no listed controller: XhciIntelPortSwitch=1 refused");
    CHECK_EQ(trs_set_note(psw, &e, "1"), TRS_NOTE_NONE,
             "no listed controller: no switch warning before the refusal");
    CHECK_EQ(trs_set_note(psw, &e, "0"), TRS_NOTE_NONE,
             "no listed controller: none for 0 either");
    e.Inst.PswController = 1;
    CHECK_EQ(trs_set_note(psw, &e, "1"), TRS_NOTE_PSW,
             "a listed controller: the switch warning for 1, as before");
    CHECK_EQ(trs_set_note(psw, &e, "0"), TRS_NOTE_PSW,
             "a listed controller: and for 0");
    CHECK_EQ(trs_set_note(psw, &e, "2"), TRS_NOTE_NONE,
             "2 is refused: no warning");
    trs_psw_text(1, 0, t, sizeof(t));
    CHECK(strstr(t, "At Windows' next start the shared connectors (usually "
                    "the blue ones) move from the USB 2.0 (EHCI) controller "
                    "to xHCI98") == t,
          "the accepted change's warning text is unchanged");
    CHECK_EQ(trs_set_note(trs_row_find("XhciImodInterval250ns"), &e, "160"),
             TRS_NOTE_NONE, "another value has no note");
    CHECK_EQ(trs_set_note(NULL, &e, "1"), TRS_NOTE_NONE, "no row: none");

    env_nt(&e, 5, 0);
    e.NoRouteW2kListed = 1;
    e.Inst.PirqController = 0;
    CHECK(trs_a5_confirm(nr, &e, "0") &&
              !trs_parse(nr, &e, "0", out, sizeof(out), &why),
          "2000, a listed computer, the row not shown: 0 refused");
    CHECK_EQ(trs_set_note(nr, &e, "0"), TRS_NOTE_NONE,
             "refused: no STOP 0xA5 question before the refusal");
    e.Inst.PirqController = 1;
    CHECK_EQ(trs_set_note(nr, &e, "0"), TRS_NOTE_A5,
             "accepted: the STOP 0xA5 question, as before");
    CHECK_EQ(trs_set_note(nr, &e, "1"), TRS_NOTE_NONE, "1 asks nothing");
}

/* XhciIntelNoRouteIrq 0 on Windows 2000 with a listed controller asks
 * first (task 47.40's STOP 0xA5; tuning.md). */
static void test_a5(void)
{
    TRS_ENV e;
    TR_U32 ss = 0;
    char t[800];
    char *a[5];
    TR_OPTS o;
    char err[256];
    const TRS_ROW *nr = trs_row_find("XhciIntelNoRouteIrq");

    CHECK(trs_subsys("PCI\\VEN_8086&DEV_1E31&SUBSYS_179B103C&REV_04", &ss) &&
              ss == 0x179B103CUL, "SUBSYS read");
    CHECK(trs_subsys("PCI\\VEN_8086&DEV_1E31&SUBSYS_179b103c", &ss) &&
              ss == 0x179B103CUL, "SUBSYS at the end, lower case");
    CHECK(!trs_subsys("PCI\\VEN_8086&DEV_1E31&REV_04", &ss), "no SUBSYS");
    CHECK(!trs_subsys("PCI\\VEN_8086&DEV_1E31&SUBSYS_179B10", &ss),
          "a short SUBSYS");
    memset(&e, 0, sizeof(e));
    e.Inst.Nt = 1;
    e.Inst.Major = 5;
    e.Inst.Minor = 0;
    e.NoRouteW2kListed = 1;
    CHECK(trs_a5_confirm(nr, &e, "0"), "2000, listed, 0: asks");
    CHECK(trs_a5_confirm(nr, &e, "00"), "2000, listed, 00: asks");
    CHECK(!trs_a5_confirm(nr, &e, "1"), "1 does not ask");
    CHECK(!trs_a5_confirm(nr, &e, "2"), "2 does not ask");
    CHECK(!trs_a5_confirm(nr, &e, "default"), "default (1) does not ask");
    CHECK(!trs_a5_confirm(nr, &e, ""), "empty does not ask");
    CHECK(!trs_a5_confirm(trs_row_find("XhciIntelIrqRoute"), &e, "0"),
          "another row does not ask");
    e.Inst.Minor = 1;
    CHECK(!trs_a5_confirm(nr, &e, "0"), "XP does not ask");
    e.Inst.Minor = 0;
    e.NoRouteW2kListed = 0;
    CHECK(!trs_a5_confirm(nr, &e, "0"), "an unlisted computer does not ask");
    e.WalkPartial = 1;
    CHECK(trs_a5_confirm(nr, &e, "0"),
          "a walk not read whole asks: a listed controller may be missing");
    e.Inst.Minor = 1;
    CHECK(!trs_a5_confirm(nr, &e, "0"), "XP never asks");
    e.Inst.Minor = 0;
    e.WalkPartial = 0;
    trs_a5_text(t, sizeof(t));
    CHECK(strstr(t, "0x000000A5") != NULL && strstr(t, "Standard PC") != NULL &&
              strstr(t, "Last Known Good Configuration") != NULL,
          "the question names the stop, the computer type and the way back");
    CHECK(strstr(t, "\"Last Known Good Configuration\" (this undoes the "
                    "change only if Windows has not started with it yet)") !=
              NULL,
          "the way back says when Last Known Good undoes the change");
    CHECK(strstr(trs_value(nr)->Choice[0], "see the note") != NULL &&
              strstr(trs_caution(nr), "0x000000A5") != NULL,
          "the row's 0 and note say Windows 2000's meaning");
    a[0] = "XHCITR";
    a[1] = "/set";
    a[2] = "XhciIntelNoRouteIrq=0";
    a[3] = "/yes";
    a[4] = NULL;
    memset(&o, 0, sizeof(o));
    CHECK(tr_parse_args(4, a, &o, err, sizeof(err)) == 0 && o.SetYes,
          "/set ... /yes");
    a[1] = "/set";
    a[2] = "/show";
    memset(&o, 0, sizeof(o));
    CHECK(tr_parse_args(4, a, &o, err, sizeof(err)) != 0,
          "/yes goes with Name=Value only");
}

/* The icon's menu (the owner, 2026-11-07): Open, Settings..., About,
 * Quick save above Exit, the options gone to the window and Settings. */
static void test_tray(void)
{
    int i;
    int n = 0;

    CHECK(strcmp(trs_tray_item(TRS_TRAY_OPEN), "&Open") == 0, "Open first");
    CHECK(strcmp(trs_tray_item(TRS_TRAY_SETTINGS), "Setti&ngs...") == 0,
          "Settings...");
    CHECK(strcmp(trs_tray_item(TRS_TRAY_ABOUT), "&About") == 0, "About");
    CHECK(strcmp(trs_tray_item(TRS_TRAY_QUICKSAVE),
                 "&Quick save (report + snapshot)") == 0 &&
              strcmp(trs_tray_item(TRS_TRAY_QUICKSAVE + 1), "-") == 0 &&
              strcmp(trs_tray_item(TRS_TRAY_EXIT), "E&xit") == 0 &&
              trs_tray_item(TRS_TRAY_EXIT + 1) == NULL,
          "quick save just above the separator and Exit, Exit last");
    for (i = 0; trs_tray_item(i) != NULL; i++) {
        const char *t = trs_tray_item(i);
        n++;
        CHECK(strstr(t, "Refresh") == NULL && strstr(t, "login") == NULL &&
                  strstr(t, "faster") == NULL && strstr(t, "ports") == NULL &&
                  strstr(t, "removed") == NULL,
              "the options are not in the icon's menu");
    }
    CHECK_EQ(n, 7, "seven entries with the two separators");
}

/* The quick save's balloon, tracked as the notice's: one waiting behind
 * another notice still counts (it is taken away before the next). */
static void test_qskind(void)
{
    TR_BKIND b;

    memset(&b, 0, sizeof(b));
    CHECK(!tr_bkind_outstanding(&b), "nothing sent");
    tr_bkind_step(&b, TR_BE_OTHER);
    tr_bkind_step(&b, TR_BE_SHOW);
    tr_bkind_step(&b, TR_BE_SENT);
    CHECK(tr_bkind_outstanding(&b), "A waiting behind another notice");
    CHECK(!tr_bkind_fast(&b, 1), "the shown one is the other notice");
    tr_bkind_step(&b, TR_BE_HIDE);
    tr_bkind_step(&b, TR_BE_SHOW);
    CHECK(tr_bkind_outstanding(&b) && tr_bkind_fast(&b, 1), "A shown");
    tr_bkind_step(&b, TR_BE_TIMEOUT);
    CHECK(!tr_bkind_outstanding(&b), "A gone");
    tr_bkind_step(&b, TR_BE_SENT);
    tr_bkind_step(&b, TR_BE_DROP);
    CHECK(tr_bkind_outstanding(&b),
          "taken away while waiting: still in doubt, so the next clears again");
}

/* XhciEventRingTrbs (the owner's final decision of 2026-11-12): two
 * picks, a list in the Tree, read as the driver reads it. */
static void test_event_ring(void)
{
    TRS_ENV e;
    char out[160];
    const char *why;
    INST_U32 v[32];
    int how;
    int n;
    TRS_RAW raw;
    const TRS_ROW *er = trs_row_find("XhciEventRingTrbs");

    CHECK(er != NULL, "the event ring's row");
    if (er == NULL) {
        return;
    }
    env_nt(&e, 5, 1);
    CHECK_EQ(trs_state(er, &e), TRS_SHOW, "shown on XP");
    CHECK(!trs_is_range(er), "a list, not a number box");
    n = trs_choices(er, &e, 1, v, 32);
    CHECK(n == 2 && v[0] == 256 && v[1] == 1024, "256 and 1024");
    trs_value_text(er, 256, out, sizeof(out));
    CHECK(strcmp(out, "256 - the smaller ring, the size other systems use") ==
              0, "256's text");
    trs_value_text(er, 1024, out, sizeof(out));
    CHECK(strcmp(out, "1024 - the larger ring") == 0, "1024's text");
    CHECK(trs_parse(er, &e, "1024", out, sizeof(out), &why) &&
              strcmp(out, "1024") == 0, "1024 taken");
    CHECK(trs_parse(er, &e, "default", out, sizeof(out), &why) &&
              strcmp(out, "256") == 0, "Default is 256, the INFs'");
    CHECK(!trs_parse(er, &e, "512", out, sizeof(out), &why) &&
              !trs_parse(er, &e, "0", out, sizeof(out), &why),
          "512 and 0 refused");
    CHECK_EQ(trs_effective(er, 1, 512, TRS_NO_HOLD, &how), 1024,
             "512 read as 1024, as the driver reads it");
    CHECK_EQ(how, TRS_HOW_HELD, "said so");
    CHECK_EQ(trs_effective(er, 1, 0, TRS_NO_HOLD, &how), 1024, "0 is 1024");
    CHECK_EQ(trs_effective(er, 1, 1024, TRS_NO_HOLD, &how), 1024, "1024 as set");
    CHECK_EQ(how, TRS_HOW_AS_SET, "as set");
    CHECK_EQ(trs_effective(er, 1, 256, TRS_NO_HOLD, &how), 256, "256 as set");
    CHECK_EQ(trs_effective(er, 0, 0, TRS_NO_HOLD, &how), 256, "none set: 256");
    memset(&raw, 0, sizeof(raw));
    raw.State = TRS_RAW_OTHER;
    trs_raw_text(er, &raw, out, sizeof(out));
    CHECK(strstr(out, "not a number (the driver uses 1024") == out,
          "another type: the larger ring");
    CHECK(trs_matches(er, "event ring") && trs_matches(er, "smaller ring"),
          "Find: its meaning and a pick's text");
    env_98(&e);
    CHECK_EQ(trs_state(er, &e), TRS_SHOW, "shown on 98 SE");
}

/* Mode 4 a hidden beta (the owner, 2026-11-12): its caution only with the
 * choices "Show advanced settings" offers; and every choice's text fits
 * the window's and the report's 160-byte buffers. */
static void test_hidden_mode4(void)
{
    const TRS_ROW *im = trs_row_find("XhciInterruptMode");
    char c[1024];
    char t[160];
    int i;
    INST_U32 k;

    trs_caution_text(im, 0xFUL, c, sizeof(c));
    CHECK(strstr(c, "Code 10") != NULL && strstr(c, "Last Known") == NULL &&
              strstr(c, "4 (") == NULL, "no mode 4 in the common caution");
    trs_caution_text(im, 0x1FUL, c, sizeof(c));
    CHECK(strstr(c, "4 (2000 and XP)") != NULL &&
              strstr(c, "Last Known Good") != NULL && strstr(c, "98") == NULL &&
              strstr(c, "msi4") == NULL,
          "mode 4's caution with the advanced choices: 2000 and XP, no "
          "component named");
    for (i = 0; i < trs_row_count(); i++) {
        const TRS_ROW *r = trs_row_at(i);
        const INST_VALUE *x = trs_value(r);
        if (x == NULL) {
            continue;
        }
        for (k = 0; k < INST_CHOICES; k++) {
            INST_U32 v = x->PickCount > 0 && (int)k < x->PickCount
                             ? x->Picks[k] : k;
            if (x->Choice[k] == NULL) {
                continue;
            }
            trs_value_text(r, v, t, sizeof(t));
            CHECK(strlen(t) + 1 < sizeof(t) - 20, r->Name);
            CHECK(strstr(t, "msi4") == NULL, "no component named in a choice");
        }
        trs_caution_text(r, 0xFFFFFFFFUL, c, sizeof(c));
        CHECK(strstr(c, "msi4") == NULL, "no component named in a caution");
    }
}

/* What a stored value reads as on 98 SE and ME (the drivers run
 * XhciInterruptMode 4 as 0 and poll for XhciCmRingInterrupt 2 there) and
 * on NT, machine-wide and per controller. */
static void test_nine_reads(void)
{
    const TRS_ROW *im = trs_row_find("XhciInterruptMode");
    const TRS_ROW *ri = trs_row_find("XhciCmRingInterrupt");
    TRS_RAW m;
    TRS_RAW own[1];
    char t[200];
    int how;

    memset(&m, 0, sizeof(m));
    memset(own, 0, sizeof(own));
    m.State = TRS_RAW_DWORD;
    m.Value = 4;
    own[0].State = TRS_RAW_ABSENT;

    trs_set_nine(1);
    CHECK_EQ(trs_effective(im, 1, 4, TRS_NO_HOLD, &how), 0, "98: 4 is 0");
    CHECK_EQ(how, TRS_HOW_DEFAULT, "98: as the default");
    CHECK_EQ(trs_effective(im, 1, 2, TRS_NO_HOLD, &how), 2, "98: 2 as set");
    CHECK_EQ(trs_effective(ri, 1, 2, TRS_NO_HOLD, &how), 0, "98: ring 2 is 0");
    CHECK_EQ(how, TRS_HOW_DEFAULT, "98: ring 2 as the default");
    CHECK_EQ(trs_effective(ri, 1, 1, TRS_NO_HOLD, &how), 1, "98: ring 1 as set");
    trs_raw_text(im, &m, t, sizeof(t));
    CHECK(strcmp(t, "4 (the driver uses its default, 0 - automatic)") == 0,
          "98: a machine-wide 4");
    trs_now_text(im, &m, own, 1, t, sizeof(t));
    CHECK(strstr(t, "4 (the driver uses its default, 0 - automatic)") != NULL,
          "98: a machine-wide 4, no controller's own");
    own[0] = m;
    m.State = TRS_RAW_ABSENT;
    trs_now_text(im, &m, own, 1, t, sizeof(t));
    CHECK(strstr(t, "4 (the driver uses its default, 0 - automatic)") != NULL,
          "98: a controller's own 4");
    m.State = TRS_RAW_DWORD;
    m.Value = 2;
    trs_raw_text(ri, &m, t, sizeof(t));
    CHECK(strncmp(t, "2 (the driver uses its default, 0 - ", 36) == 0,
          "98: a stored ring 2");

    trs_set_nine(0);
    CHECK_EQ(trs_effective(im, 1, 4, TRS_NO_HOLD, &how), 4, "NT: 4 as set");
    CHECK_EQ(how, TRS_HOW_AS_SET, "NT: as set");
    CHECK_EQ(trs_effective(ri, 1, 2, TRS_NO_HOLD, &how), 2, "NT: ring 2");
    m.Value = 4;
    trs_raw_text(im, &m, t, sizeof(t));
    CHECK(strcmp(t, "4 - MSI on 2000 / XP, no fallback") == 0,
          "NT: a machine-wide 4 as set");
    CHECK(strstr(t, "default") == NULL, "NT: not the default");
}

/* A zero-length REG_NONE is absent, as the drivers read it, in a
 * controller's key and machine-wide, where it does not hide the
 * controller's value (XhciEventRingTrbs: absent 256, another type 1024). */
static void test_reg_none(void)
{
    static const unsigned char d256[4] = { 0x00, 0x01, 0x00, 0x00 };
    static const unsigned char none[4] = { 0, 0, 0, 0 };
    TRS_RAW mw;
    TRS_RAW own[1];
    char out[200];
    const TRS_ROW *er = trs_row_find("XhciEventRingTrbs");

    trs_raw_from_query(TRS_Q_OK, TRS_REG_NONE, 0, none, &mw);
    CHECK_EQ(mw.State, TRS_RAW_ABSENT, "an empty REG_NONE is absent");
    trs_raw_from_query(TRS_Q_OK, TRS_REG_NONE, 4, none, &mw);
    CHECK_EQ(mw.State, TRS_RAW_OTHER, "a REG_NONE with data is another type");
    trs_raw_from_query(TRS_Q_NOT_FOUND, 0, 0, none, &mw);
    CHECK_EQ(mw.State, TRS_RAW_ABSENT, "not found");
    trs_raw_from_query(TRS_Q_OK, TRS_REG_DWORD, 4, d256, &mw);
    CHECK(mw.State == TRS_RAW_DWORD && mw.Value == 256, "a DWORD 256");
    trs_raw_from_query(TRS_Q_ERROR, 0, 0, none, &mw);
    CHECK_EQ(mw.State, TRS_RAW_UNREAD, "an error is unread");
    /* A controller's own empty REG_NONE: not set, the driver's 256. */
    trs_raw_from_query(TRS_Q_OK, TRS_REG_NONE, 0, none, &own[0]);
    trs_raw_text(er, &own[0], out, sizeof(out));
    CHECK(strncmp(out, "not set (the driver uses 256", 28) == 0,
          "a controller's empty REG_NONE: 256");
    /* Machine-wide empty REG_NONE over a controller's DWORD 256: the
     * controller's value is the one in force. */
    trs_raw_from_query(TRS_Q_OK, TRS_REG_NONE, 0, none, &mw);
    trs_raw_from_query(TRS_Q_OK, TRS_REG_DWORD, 4, d256, &own[0]);
    trs_now_text(er, &mw, own, 1, out, sizeof(out));
    CHECK(strncmp(out, "256", 3) == 0 && strstr(out, "machine-wide") == NULL,
          "the machine-wide empty REG_NONE falls through to the controller");
    /* And a machine-wide REG_NONE with data is the larger ring. */
    trs_raw_from_query(TRS_Q_OK, TRS_REG_NONE, 4, none, &mw);
    trs_now_text(er, &mw, own, 1, out, sizeof(out));
    CHECK(strstr(out, "1024") != NULL, "another type machine-wide: 1024");
}

/* The elevated request carries a drive's driver key (task 49.38). */
static void test_req_drv(void)
{
    TRS_CHANGE c[2];
    TRS_CHANGE b[2];
    char req[512];
    int n;

    memset(c, 0, sizeof(c));
    strcpy(c[0].Name, "XhciUasFastReset");
    strcpy(c[0].Value, "1");
    strcpy(c[0].On, "USB\\VID_1\\A");
    strcpy(c[0].Drv, "{36FC9E60-C465-11CF-8056-444553540000}\\0007");
    strcpy(c[1].Name, "XhciLinkKeep");
    strcpy(c[1].Value, "0");
    CHECK(trs_req_make(c, 2, req, sizeof(req)), "made");
    CHECK(trs_req_parse(req, sizeof(req), b, 2, &n) && n == 2, "parsed");
    CHECK(strcmp(b[0].Drv, c[0].Drv) == 0 && strcmp(b[0].On, c[0].On) == 0,
          "the drive's id and driver key round trip");
    CHECK(b[1].Drv[0] == '\0' && b[1].On[0] == '\0', "every device, no key");
    c[1].Drv[0] = 'x';
    c[1].Drv[1] = '\0';
    CHECK(!trs_req_make(c, 2, req, sizeof(req)), "a key with no device refused");
}

/* The window's drives against a fresh reading: drive A unplugged and B
 * plugged in since it opened is a change (the reviewer's case). */
static void test_same_devices(void)
{
    const char *a[2] = { "USB\\VID_1\\A", "USB\\VID_1\\C" };
    const char *ad[2] = { "{x}\\0001", "{x}\\0002" };
    const char *b[2] = { "USB\\VID_1\\C", "usb\\vid_1\\a" };
    const char *bd[2] = { "{x}\\0002", "{X}\\0001" };
    const char *c[2] = { "USB\\VID_1\\B", "USB\\VID_1\\C" };
    const char *cd[2] = { "{x}\\0003", "{x}\\0002" };

    CHECK(trs_same_devices(a, ad, 2, b, bd, 2), "the same two, any order and case");
    CHECK(!trs_same_devices(a, ad, 2, c, cd, 2), "A gone, B new: changed");
    CHECK(!trs_same_devices(a, ad, 2, b, bd, 1), "one gone: changed");
    CHECK(!trs_same_devices(a, ad, 1, b, bd, 2), "one new: changed");
    bd[1] = "{x}\\0009";
    CHECK(!trs_same_devices(a, ad, 2, b, bd, 2), "the same id, another driver key");
    CHECK(trs_same_devices(a, ad, 0, b, bd, 0), "none then, none now");
}

/* Task 49.38's Find: the match and which rows the window lists. */
static void test_find(void)
{
    TRS_ENV e;
    const TRS_ROW *mw = trs_row_find("XhciMsiAvoid");
    const TRS_ROW *im = trs_row_find("XhciInterruptMode");
    const TRS_ROW *wt = trs_row_find("StorWriteThrough");
    const TRS_ROW *lv = trs_row_find("XhciLogVerbosity");
    int i;
    int n = 0;

    CHECK(trs_matches(mw, "") && trs_matches(mw, "   ") &&
              trs_matches(mw, NULL), "an empty search matches every row");
    CHECK(trs_matches(mw, "msiavoid") && trs_matches(mw, "XHCIMSIAVOID") &&
              trs_matches(mw, " MsiAvoid "), "its name, any case, spaces trimmed");
    CHECK(trs_matches(mw, "polling instead"), "its meaning");
    CHECK(trs_matches(mw, "Linux lists"), "a choice text");
    CHECK(!trs_matches(mw, "zzz-no-such"), "no match");
    CHECK(trs_matches(wt, "storwritethrough") && trs_matches(wt, "fua"),
          "a Stor98 row by its NT key and a word");
    env_nt(&e, 5, 1);
    CHECK(!trs_listed(mw, &e, 0, ""), "advanced, not ticked, no search: hidden");
    CHECK(trs_listed(mw, &e, 0, "msiavoid"), "a search lists an advanced row");
    CHECK(trs_listed(mw, &e, 1, ""), "ticked: listed");
    CHECK(!trs_listed(mw, &e, 1, "nomatch-here"), "ticked, no match: hidden");
    CHECK(trs_listed(im, &e, 0, ""), "a common row, no search: listed");
    CHECK(!trs_listed(lv, &e, 1, "XhciLogVerbosity"),
          "an out row is never listed (the Logging level dialog's)");
    for (i = 0; i < trs_row_count(); i++) {
        n += trs_listed(trs_row_at(i), &e, 0, "quirk") ||
             trs_listed(trs_row_at(i), &e, 0, "linux");
    }
    CHECK(n >= 14, "the Linux quirk rows found by a word of their texts");
    /* A staged change of a row the search hides is the window's to keep:
     * the pure half says it is not listed (set_hidden_staged counts it). */
    CHECK(!trs_listed(im, &e, 0, "msiavoid"), "a staged row hidden by Find");
}

int main(void)
{
    test_find();
    test_same_devices();
    test_reg_none();
    test_req_drv();
    test_event_ring();
    test_hidden_mode4();
    test_nine_reads();
    test_coverage();
    test_defaults();
    test_every_value();
    test_new_rows();
    test_rules();
    test_parse();
    test_effective();
    test_apply();
    test_request();
    test_texts();
    test_args();
    test_quick();
    test_a5();
    test_set_note();
    test_tray();
    test_qskind();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
