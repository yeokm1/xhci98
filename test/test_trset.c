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
    for (i = 0; i < trsExtraCount; i++) {
        const TRS_ROW *r = trs_row_find(trsExtra[i].Name);
        CHECK(r != NULL && trs_value(r) == &trsExtra[i], "the Tree's own rows");
        CHECK(inst_value_find(trsExtra[i].Name) < 0,
              "the Tree's own rows are not Setup's");
    }
    CHECK_EQ(n, instValueCount + INST_S98_COUNT + trsExtraCount,
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
    CHECK(trs_row_find("XhciCommandCheck") == NULL, "a quirk is not a row");
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
        if (r->Tab == TRS_TAB_USB) {
            found = inst_inf_addreg_default(x86, r->Name, &d);
            CHECK(found, r->Name);
            CHECK_EQ(d, r->InstallDef, r->Name);
            d = 0xFFFFFFFFUL;
            found = inst_inf_addreg_default(amd, r->Name, &d);
            CHECK(found && d == r->InstallDef, "the amd64 INF too");
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

    env_98(&e);
    CHECK_EQ(trs_state(psw, &e), TRS_GREY, "no gated controller: greyed");
    e.Inst.PswController = 1;
    CHECK_EQ(trs_state(psw, &e), TRS_SHOW, "a gated controller");
    n = trs_choices(psw, &e, 1, v, 32);
    CHECK(n == 2 && v[0] == 0 && v[1] == 1, "0 and 1 only, 2 a hand edit");
    CHECK_EQ(trs_state(ir, &e), TRS_HIDE, "no reviewed Intel controller");
    CHECK_EQ(trs_state(nr, &e), TRS_HIDE, "no reviewed Intel controller 2");
    e.PirqController = 1;
    CHECK_EQ(trs_state(ir, &e), TRS_SHOW, "every system");
    CHECK_EQ(trs_state(nr, &e), TRS_GREY, "98 SE: 2000 and XP only");
    CHECK(strcmp(trs_only(nr), "Windows 2000 and XP only") == 0, "its text");
    CHECK_EQ(trs_state(si, &e), TRS_SHOW, "silent install on 98 SE");
    CHECK_EQ(trs_state(pfw, &e), TRS_HIDE, "no XHCIPFW.VXD");
    e.Pfw = 1;
    CHECK_EQ(trs_state(pfw, &e), TRS_SHOW, "XHCIPFW.VXD");
    CHECK_EQ(trs_loc(pfw), TRS_LOC_PFW, "its own key");
    CHECK_EQ(trs_state(mk, &e), TRS_HIDE, "no mark service on 98 SE");
    CHECK_EQ(trs_state(h9, &e), TRS_SHOW, "StorHold9xMs on 98 SE");
    n = trs_choices(im, &e, 1, v, 32);
    CHECK_EQ(n, 3, "no message-signalled interrupt on 98 SE");

    env_nt(&e, 5, 2);
    e.PirqController = 1;
    CHECK_EQ(trs_state(nr, &e), TRS_SHOW, "XP x64");
    env_nt(&e, 5, 0);
    e.PirqController = 1;
    CHECK_EQ(trs_state(nr, &e), TRS_SHOW, "2000");
    CHECK_EQ(trs_state(si, &e), TRS_GREY, "silent install greyed on NT");
    CHECK_EQ(trs_state(h9, &e), TRS_GREY, "StorHold9xMs greyed on NT");
    CHECK_EQ(trs_state(pfw, &e), TRS_HIDE, "no XHCIPFW on NT");
    e.Mark = 1;
    CHECK_EQ(trs_state(mk, &e), TRS_SHOW, "the mark service");
    CHECK_EQ(trs_loc(mk), TRS_LOC_MARK, "its own key");
    env_nt(&e, 6, 1);
    e.PirqController = 1;
    CHECK_EQ(trs_state(nr, &e), TRS_GREY, "7: not read");
    n = trs_choices(im, &e, 1, v, 32);
    CHECK_EQ(n, 4, "MSI only on 7");
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

int main(void)
{
    test_coverage();
    test_defaults();
    test_rules();
    test_parse();
    test_effective();
    test_apply();
    test_request();
    test_texts();
    test_args();
    test_quick();
    test_a5();
    test_tray();
    test_qskind();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
