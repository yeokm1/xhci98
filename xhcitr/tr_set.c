/*
 * tr_set.c - the pure half of XHCITR's Settings window and XHCITR /set
 * (tr_set.h; design record 25 section 22; roadmap-usb4.md task 47.35).
 * Pure: no Windows call.
 */

#include <stdlib.h>
#include <string.h>
#include "tr_set.h"

/* ---- the Tree's own two rows -------------------------------------------- */

/* Setup offers neither (record 23 section 8.1 keeps the hardware quirks out
 * of it); the owner's choice of 2026-11-07 puts both on the Tree's USB tab.
 * The meanings follow docs\using\tuning.md. XhciIntelNoRouteIrq's texts
 * are held for w47-hp12x's tuning.md wording (task 47.33), which extends
 * the value to 32-bit 2000 and XP: see run47-trset.md. */
const INST_VALUE trsExtra[] = {
    { "XhciIntelIrqRoute", INST_INF_XHCI, TRS_APPLY_PIRQ, 0x0UL, 0x7UL,
      "repair the Intel 7/8/9-series interrupt routing, pointing the "
      "controller's interrupt at the line Windows assigned",
      "2 writes two chipset registers on any of the six reviewed Intel "
      "controllers. Use it only if USB is slow or stops on such a computer.",
      0x4UL,
      { "report only", "on the Dell Latitude E7250 only",
        "on any reviewed Intel chipset", NULL, NULL } },
    { "XhciIntelNoRouteIrq", INST_INF_XHCI, TRS_APPLY_NOROUTE, 0x0UL, 0x7UL,
      "where the firmware gives the USB 3 controller no interrupt, offer "
      "Windows the line the chipset routes it to before polling it",
      "2 reads two chipset registers on any of the reviewed Intel chipsets "
      "and writes nothing. A line the firmware gives is always taken first. "
      "Windows 2000 on the listed computers: 1 and 2 ask Windows for no "
      "interrupt and the controller is polled; 0 asks for one, and Windows "
      "2000 then stops at startup (STOP 0x000000A5) unless the computer "
      "type is Standard PC.",
      0x4UL,
      { "never: the controller is polled (Windows 2000: see the note)",
        "on the listed computers only",
        "on any reviewed Intel chipset", NULL, NULL } }
};
const int trsExtraCount = (int)(sizeof(trsExtra) / sizeof(trsExtra[0]));

/* ---- the rows ------------------------------------------------------------- */

#define C TRS_VIEW_COMMON
#define A TRS_VIEW_ADV
#define O TRS_VIEW_OUT

/* Name, tab, view, class, cycle, effect, accepted values, the driver's
 * value when none is set, the INFs' value, Stor98's row, note. The order
 * is the window's: within a tab the common rows, then the advanced ones,
 * each in Setup's order. Every instValues[] row and every Stor98 row is
 * here (test_trset holds it), listed or out with why. */
static const TRS_ROW trsRows[] = {
    { "XhciIntelPortSwitch", TRS_TAB_USB, C, TRS_AP_BOOT, 0,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciIntelIrqRoute", TRS_TAB_USB, C, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciIntelNoRouteIrq", TRS_TAB_USB, C, TRS_AP_ADD, 1,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciInterruptMode", TRS_TAB_USB, C, TRS_AP_CTRL, 1,
      TRS_EFF_LIST, 0xFUL, 0UL, 0UL, -1, NULL },
    { "XhciForceBulkOnly", TRS_TAB_USB, C, TRS_AP_REPLUG, 0,
      TRS_EFF_NONZERO, 0, 1UL, 0UL, -1, NULL },
    { "XhciSilentInstall", TRS_TAB_USB, C, TRS_AP_BOOT, 0,
      TRS_EFF_ONE, 0, 0UL, 0UL, -1,
      "1 also puts the list of device classes that may install without "
      "the wizard in place first (tools\\silent\\SILENT.REG's list, as "
      "Setup and silent-on.bat do); 0 turns the switch off and leaves the "
      "list. Restart Windows after changing it." },
    { "XhciSelectiveSuspend", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_ONE, 0, 0UL, 0UL, -1, NULL },
    { "XhciLinkKeep", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciResumeKeep", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciUsb2Bos", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciImodInterval250ns", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_RANGE, 0, 4000UL, 160UL, -1, NULL },
    { "XhciLogDebugView", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciPollIdleMs", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ZERODEF, 0, 50UL, 50UL, -1, NULL },
    { "XhciPollActiveMs", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ZERODEF, 0, 1UL, 1UL, -1, NULL },
    { "XhciPollFineTimer", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciBulkOnlyOwnId", TRS_TAB_USB, A, TRS_AP_REPLUG, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1,
      "Read when a drive is plugged in. 0 gives a drive that offers UAS and "
      "Bulk-Only one Device Manager entry everywhere, as before 3.4.2.0; one "
      "installed with the UAS driver then stops at Code 10 on a controller "
      "that cannot stream." },
    { "XhciFastPollFsLs", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x1FUL, 0UL, 0UL, -1, NULL },
    { "XhciMaxTransferKB", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_RANGE, 0, 1024UL, 1024UL, -1, NULL },
    { "XhciWideMap", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciFirstEnumWaitMs", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_HIGH, 0, 5000UL, 5000UL, -1, NULL },
    { "XhciFirstEnumPortMs", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_HIGH, 0, 2000UL, 2000UL, -1, NULL },
    { "XhciLogVerbosity", TRS_TAB_USB, O, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x1FUL, 2UL, 2UL, -1,
      "set in the Logging level dialog (owner, 2026-11-07)" },

    { "XhciCmMode", TRS_TAB_CM, C, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0xFUL, 1UL, 1UL, -1, NULL },
    { "XhciCmApprove", TRS_TAB_CM, C, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmSafeEject", TRS_TAB_CM, C, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1,
      "0: devices behind a dock are not offered for safe removal. The "
      "safe-removal mark service is installed and removed by Setup, not "
      "here." },
    { "XhciCmRingInterrupt", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciCmReenum", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmUsb3Tunnel", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmLaneBonding", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmPreOsCleanup", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x7UL, 0UL, 0UL, -1, NULL },
    { "XhciCmUsb4V2", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmPollIdleMs", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_CLAMP, 0, 200UL, 200UL, -1, NULL },
    { "XhciCmCmdTimeoutMs", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_CLAMP, 0, 5000UL, 5000UL, -1, NULL },
    { "XhciCmApproveTimeoutMs", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_CLAMP, 0, 10000UL, 10000UL, -1, NULL },
    { "XhciCmLogDebugView", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciCmBridgeNumbering", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmNoIrqStart", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmBridgeReq", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciCmAsm4242Structural", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciCmAsm4242Reads", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciPfwNhiLine", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "Restart", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmLogVerbosity", TRS_TAB_CM, O, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x1FUL, 2UL, 2UL, -1,
      "set in the Logging level dialog (owner, 2026-11-07)" },

    { "StorWriteThrough", TRS_TAB_S98, C, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_WRITETHROUGH, NULL },
    { "StorPollMode", TRS_TAB_S98, C, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_POLLMODE, NULL },
    { "StorMaxTransferKB", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_MAXTRANSFERKB, NULL },
    { "StorPollBusyUs", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_POLLBUSYUS, NULL },
    { "StorPollIdleMs", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_POLLIDLEMS, NULL },
    { "StorPollSpinUs", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_POLLSPINUS, NULL },
    { "StorIntWatchdog", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_INTWATCH, NULL },
    { "StorGoneMs", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_GONE, NULL },
    { "StorWriteIdleMs", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_IDLE, NULL },
    { "StorHold9xMs", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_HOLD9X, NULL }
};

#undef C
#undef A
#undef O

#define TRS_ROWS ((int)(sizeof(trsRows) / sizeof(trsRows[0])))

int trs_row_count(void)
{
    return TRS_ROWS;
}

const TRS_ROW *trs_row_at(int i)
{
    return (i >= 0 && i < TRS_ROWS) ? &trsRows[i] : NULL;
}

const TRS_ROW *trs_row_find(const char *name)
{
    int i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < TRS_ROWS; i++) {
        if (inst_eq(trsRows[i].Name, name)) {
            return &trsRows[i];
        }
        if (trsRows[i].S98 >= 0 &&
            instS98Values[trsRows[i].S98].KeyNt != NULL &&
            inst_eq(instS98Values[trsRows[i].S98].KeyNt, name)) {
            return &trsRows[i];
        }
    }
    return NULL;
}

int trs_s98(const TRS_ROW *r)
{
    return r != NULL ? r->S98 : -1;
}

const INST_VALUE *trs_value(const TRS_ROW *r)
{
    int i;

    if (r == NULL || r->S98 >= 0) {
        return NULL;
    }
    for (i = 0; i < trsExtraCount; i++) {
        if (inst_eq(trsExtra[i].Name, r->Name)) {
            return &trsExtra[i];
        }
    }
    i = inst_value_find(r->Name);
    return i >= 0 ? &instValues[i] : NULL;
}

int trs_loc(const TRS_ROW *r)
{
    const INST_VALUE *v = trs_value(r);

    if (r == NULL || r->S98 >= 0) {
        return TRS_LOC_S98;
    }
    if (v != NULL && v->Loc == INST_LOC_PFW) {
        return TRS_LOC_PFW;
    }
    if (v != NULL && v->Loc == INST_LOC_MARK) {
        return TRS_LOC_MARK;
    }
    return TRS_LOC_DRIVER;
}

/* ---- the system rules ---------------------------------------------------- */

static int trs_nt5(const TRS_ENV *e)
{
    return e->Inst.Nt && e->Inst.Major == 5UL;
}

int trs_state(const TRS_ROW *r, const TRS_ENV *e)
{
    const INST_VALUE *v;
    int i;

    if (r == NULL || r->View == TRS_VIEW_OUT) {
        return TRS_HIDE;
    }
    if (r->S98 >= 0) {
        if (inst_s98_applies(r->S98, e->Inst.Nt)) {
            return TRS_SHOW;
        }
        return inst_s98_greyed(r->S98, e->Inst.Nt) ? TRS_GREY : TRS_HIDE;
    }
    v = trs_value(r);
    if (v == NULL) {
        return TRS_HIDE;
    }
    if (v->Apply == TRS_APPLY_PIRQ) {
        return e->PirqController ? TRS_SHOW : TRS_HIDE;
    }
    if (v->Apply == TRS_APPLY_NOROUTE) {
        if (!e->PirqController) {
            return TRS_HIDE;
        }
        return trs_nt5(e) ? TRS_SHOW : TRS_GREY;
    }
    if (v->Loc == INST_LOC_PFW) {
        if (e->Inst.Nt) {
            return TRS_HIDE;
        }
        return e->Pfw ? TRS_SHOW : TRS_HIDE;
    }
    if (v->Loc == INST_LOC_MARK) {
        if (!inst_mk_system(&e->Inst)) {
            return TRS_HIDE;
        }
        return e->Mark ? TRS_SHOW : TRS_HIDE;
    }
    i = inst_value_find(r->Name);
    if (r->Tab == TRS_TAB_CM && !e->Inst.CmNhi) {
        return TRS_HIDE;
    }
    if (r->Tab == TRS_TAB_USB && !e->Inst.AnyXhci) {
        return TRS_HIDE;
    }
    if (inst_value_greyed(i, &e->Inst)) {
        return TRS_GREY;
    }
    if (inst_value_set_mask(i, &e->Inst) == 0 && v->Hi == 0) {
        return TRS_GREY;
    }
    return TRS_SHOW;
}

const char *trs_only(const TRS_ROW *r)
{
    const INST_VALUE *v;

    if (r == NULL) {
        return NULL;
    }
    if (r->S98 >= 0) {
        return inst_s98_only(r->S98);
    }
    v = trs_value(r);
    if (v != NULL && v->Apply == TRS_APPLY_NOROUTE) {
        return "Windows 2000 and XP only";
    }
    if (v != NULL && v->Apply == TRS_APPLY_PIRQ) {
        return NULL;
    }
    return inst_value_only(inst_value_find(r->Name));
}

/* ---- values ------------------------------------------------------------- */

/* The values a list row admits on this system, Setup's /set mask (the
 * Tree's own rows: their table mask). */
static INST_U32 trs_mask(const TRS_ROW *r, const TRS_ENV *e)
{
    const INST_VALUE *v = trs_value(r);
    int i;

    if (v == NULL) {
        return 0;
    }
    if (v->Apply == TRS_APPLY_PIRQ || v->Apply == TRS_APPLY_NOROUTE) {
        return v->Settable;
    }
    i = inst_value_find(r->Name);
    return i >= 0 ? inst_value_set_mask(i, &e->Inst) : 0;
}

int trs_is_range(const TRS_ROW *r)
{
    const INST_VALUE *v;
    const INST_S98_VALUE *s;

    if (r == NULL) {
        return 0;
    }
    if (r->S98 >= 0) {
        s = &instS98Values[r->S98];
        return s->Kind == INST_S98_NUMBER && s->Means[0] == NULL;
    }
    v = trs_value(r);
    return v != NULL && v->Hi != 0;
}

int trs_choices(const TRS_ROW *r, const TRS_ENV *e, int adv, INST_U32 *vals,
                int cap)
{
    INST_U32 mask;
    INST_U32 k;
    int n = 0;
    const INST_S98_VALUE *s;

    if (r == NULL || trs_is_range(r)) {
        return 0;
    }
    if (r->S98 >= 0) {
        s = &instS98Values[r->S98];
        for (k = 0; k < INST_S98_WORDS_MAX && n < cap; k++) {
            if (s->Kind == INST_S98_WORDS ? s->Words[k] != NULL
                                          : s->Means[k] != NULL) {
                vals[n++] = k;
            }
        }
        return n;
    }
    mask = trs_mask(r, e);
    if (inst_eq(r->Name, "XhciIntelPortSwitch")) {
        mask &= 0x3UL;
    }
    if (inst_eq(r->Name, "XhciCmMode") && !adv) {
        mask &= 0x3UL;
    }
    for (k = 0; k < 32UL && n < cap; k++) {
        if (mask & (1UL << k)) {
            vals[n++] = k;
        }
    }
    return n;
}

void trs_value_text(const TRS_ROW *r, INST_U32 v, char *out, size_t cap)
{
    const INST_VALUE *x;
    const INST_S98_VALUE *s;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (r == NULL) {
        return;
    }
    if (r->S98 >= 0) {
        s = &instS98Values[r->S98];
        if (s->Kind == INST_S98_WORDS && v < INST_S98_WORDS_MAX &&
            s->Words[v] != NULL) {
            tr_sprintf(out, cap, "%s - %s", s->Words[v],
                       s->Means[v] != NULL ? s->Means[v] : "");
        } else if (s->Kind == INST_S98_NUMBER && v < INST_S98_WORDS_MAX &&
                   s->Means[0] != NULL && s->Means[v] != NULL) {
            tr_sprintf(out, cap, "%lu - %s", (unsigned long)v, s->Means[v]);
        } else {
            tr_sprintf(out, cap, "%lu", (unsigned long)v);
        }
        return;
    }
    x = trs_value(r);
    if (x != NULL && x->Hi == 0 && v < INST_CHOICES && x->Choice[v] != NULL) {
        tr_sprintf(out, cap, "%lu - %s", (unsigned long)v, x->Choice[v]);
        return;
    }
    tr_sprintf(out, cap, "%lu", (unsigned long)v);
}

void trs_range_text(const TRS_ROW *r, const TRS_ENV *e, char *out, size_t cap)
{
    const INST_VALUE *x;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (r == NULL) {
        return;
    }
    if (r->S98 >= 0) {
        inst_s98_range(r->S98, inst_s98_hi(r->S98, e->Inst.Nt), out, cap);
        return;
    }
    x = trs_value(r);
    if (x == NULL || x->Hi == 0) {
        return;
    }
    if (x->Step > 1UL) {
        tr_sprintf(out, cap, "%lu to %lu, a multiple of %lu",
                   (unsigned long)x->Lo, (unsigned long)x->Hi,
                   (unsigned long)x->Step);
    } else {
        tr_sprintf(out, cap, "%lu to %lu", (unsigned long)x->Lo,
                   (unsigned long)x->Hi);
    }
}

/* A decimal number of at most nine digits, nothing else. */
static int trs_number(const char *s, INST_U32 *v)
{
    INST_U32 n = 0;
    int digits = 0;

    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || ++digits > 9) {
            return 0;
        }
        n = n * 10UL + (INST_U32)(*s - '0');
    }
    *v = n;
    return 1;
}

int trs_parse(const TRS_ROW *r, const TRS_ENV *e, const char *text,
              char *out, size_t cap, const char **why)
{
    const INST_VALUE *x;
    INST_U32 v;
    INST_U32 vals[32];
    int n;
    int k;

    *why = "";
    if (cap > 0) {
        out[0] = '\0';
    }
    if (r == NULL || r->View == TRS_VIEW_OUT) {
        *why = "not a setting the xHCI98 USB Tree changes";
        return 0;
    }
    if (trs_state(r, e) != TRS_SHOW) {
        *why = "not a setting of this computer";
        return 0;
    }
    if (r->S98 >= 0) {
        if (tr_eq(text, "default")) {
            text = inst_s98_default(r->S98, e->Inst.Nt);
        }
        return inst_s98_check(r->S98, e->Inst.Nt, text, out, cap, why);
    }
    x = trs_value(r);
    if (x == NULL) {
        *why = "not a setting the xHCI98 USB Tree changes";
        return 0;
    }
    if (tr_eq(text, "default")) {
        v = r->InstallDef;
    } else if (!trs_number(text, &v)) {
        *why = "not a decimal number";
        return 0;
    }
    if (x->Hi != 0) {
        if (v < x->Lo || v > x->Hi || (x->Step > 1UL && v % x->Step != 0)) {
            *why = "out of its range";
            return 0;
        }
    } else {
        n = trs_choices(r, e, 1, vals, 32);
        for (k = 0; k < n; k++) {
            if (vals[k] == v) {
                break;
            }
        }
        if (k == n) {
            *why = "not a value the xHCI98 USB Tree offers for it";
            return 0;
        }
    }
    tr_sprintf(out, cap, "%lu", (unsigned long)v);
    return 1;
}

INST_U32 trs_effective(const TRS_ROW *r, int found, INST_U32 v,
                       INST_U32 held, int *how)
{
    const INST_VALUE *x = trs_value(r);
    INST_U32 e;

    *how = TRS_HOW_AS_SET;
    if (r == NULL) {
        return v;
    }
    if (!found) {
        *how = TRS_HOW_DEFAULT;
        e = r->AbsentDef;
        if (held != TRS_NO_HOLD && e > held) {
            e = held;
        }
        return e;
    }
    e = v;
    switch (r->Effect) {
    case TRS_EFF_LIST:
        if (v > 31UL || (r->Accept & (1UL << v)) == 0) {
            *how = TRS_HOW_DEFAULT;
            e = r->AbsentDef;
        }
        break;
    case TRS_EFF_NONZERO:
        if (v > 1UL) {
            *how = TRS_HOW_AS_ON;
            e = 1;
        }
        break;
    case TRS_EFF_ONE:
        if (v > 1UL) {
            *how = TRS_HOW_DEFAULT;
            e = 0;
        }
        break;
    case TRS_EFF_RANGE:
        if (x != NULL && (v < x->Lo || v > x->Hi ||
                          (x->Step > 1UL && v % x->Step != 0))) {
            *how = TRS_HOW_DEFAULT;
            e = r->AbsentDef;
        }
        break;
    case TRS_EFF_CLAMP:
        if (x != NULL && v < x->Lo) {
            *how = TRS_HOW_HELD;
            e = x->Lo;
        } else if (x != NULL && v > x->Hi) {
            *how = TRS_HOW_HELD;
            e = x->Hi;
        }
        break;
    case TRS_EFF_ZERODEF:
        if (v == 0) {
            *how = TRS_HOW_DEFAULT;
            e = r->AbsentDef;
        } else if (x != NULL && v > x->Hi) {
            *how = TRS_HOW_HELD;
            e = x->Hi;
        }
        break;
    case TRS_EFF_HIGH:
        if (x != NULL && v > x->Hi) {
            *how = TRS_HOW_HELD;
            e = x->Hi;
        }
        break;
    }
    if (held != TRS_NO_HOLD && e > held) {
        *how = TRS_HOW_HELD;
        e = held;
    }
    return e;
}

const TRS_ROW *trs_held_by(const TRS_ROW *r)
{
    if (r == NULL) {
        return NULL;
    }
    if (inst_eq(r->Name, "XhciPollActiveMs")) {
        return trs_row_find("XhciPollIdleMs");
    }
    if (inst_eq(r->Name, "XhciFirstEnumPortMs")) {
        return trs_row_find("XhciFirstEnumWaitMs");
    }
    return NULL;
}

void trs_raw_text(const TRS_ROW *r, const TRS_RAW *v, char *out, size_t cap)
{
    trs_raw_text_held(r, v, TRS_NO_HOLD, out, cap);
}

void trs_raw_text_held(const TRS_ROW *r, const TRS_RAW *v, INST_U32 held,
                       char *out, size_t cap)
{
    char t[160];
    int how;
    INST_U32 e;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (r == NULL || v == NULL) {
        return;
    }
    switch (v->State) {
    case TRS_RAW_DWORD:
        e = trs_effective(r, 1, v->Value, held, &how);
        trs_value_text(r, e, t, sizeof(t));
        if (how == TRS_HOW_AS_SET) {
            tr_copy(out, t, cap);
        } else if (how == TRS_HOW_DEFAULT) {
            tr_sprintf(out, cap, "%lu (the driver uses its default, %s)",
                       (unsigned long)v->Value, t);
        } else {
            tr_sprintf(out, cap, "%lu (the driver reads it as %s)",
                       (unsigned long)v->Value, t);
        }
        break;
    case TRS_RAW_OTHER:
        trs_value_text(r, r->AbsentDef, t, sizeof(t));
        tr_sprintf(out, cap, "not a number (the driver uses %s)", t);
        break;
    case TRS_RAW_ABSENT:
        e = trs_effective(r, 0, 0, held, &how);
        trs_value_text(r, e, t, sizeof(t));
        tr_sprintf(out, cap, "not set (the driver uses %s)", t);
        break;
    default:
        tr_copy(out, "could not be read", cap);
        break;
    }
}

static int trs_raw_same(const TRS_RAW *a, const TRS_RAW *b)
{
    if (a->State != b->State) {
        return 0;
    }
    return a->State != TRS_RAW_DWORD || a->Value == b->Value;
}

void trs_now_text(const TRS_ROW *r, const TRS_RAW *machine,
                  const TRS_RAW *own, int n, char *out, size_t cap)
{
    trs_now_text_held(r, machine, own, n, TRS_NO_HOLD, out, cap);
}

void trs_now_text_held(const TRS_ROW *r, const TRS_RAW *machine,
                       const TRS_RAW *own, int n, INST_U32 held, char *out,
                       size_t cap)
{
    char t[200];
    int i;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (machine != NULL && (machine->State == TRS_RAW_DWORD ||
                            machine->State == TRS_RAW_OTHER)) {
        trs_raw_text_held(r, machine, held, t, sizeof(t));
        tr_sprintf(out, cap, "%s (machine-wide)", t);
        return;
    }
    if (n <= 0) {
        trs_raw_text_held(r, machine, held, out, cap);
        return;
    }
    for (i = 1; i < n; i++) {
        if (!trs_raw_same(&own[0], &own[i])) {
            tr_copy(out, "not the same on every controller", cap);
            return;
        }
    }
    trs_raw_text_held(r, &own[0], held, out, cap);
}

void trs_now_text_each(const TRS_ROW *r, const TRS_RAW *machine,
                       const TRS_RAW *own, int n, const INST_U32 *held,
                       char *out, size_t cap)
{
    char first[200];
    char t[200];
    int i;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (n <= 0) {
        trs_now_text_held(r, machine, own, n,
                          held != NULL ? held[0] : TRS_NO_HOLD, out, cap);
        return;
    }
    trs_now_text_held(r, machine, &own[0], 1,
                      held != NULL ? held[0] : TRS_NO_HOLD, first,
                      sizeof(first));
    for (i = 1; i < n; i++) {
        trs_now_text_held(r, machine, &own[i], 1,
                          held != NULL ? held[i] : TRS_NO_HOLD, t, sizeof(t));
        if (strcmp(t, first) != 0) {
            tr_copy(out, "not the same on every controller", cap);
            return;
        }
    }
    tr_copy(out, first, cap);
}

/* ---- applying ------------------------------------------------------------- */

int trs_apply(const TRS_ROW *r, int nt)
{
    if (r == NULL) {
        return TRS_AP_BOOT;
    }
    if (r->Apply == TRS_AP_REPLUG) {
        return TRS_AP_REPLUG;
    }
    return nt ? r->Apply : TRS_AP_BOOT;
}

void trs_sum_init(TRS_SUM *s)
{
    memset(s, 0, sizeof(*s));
    s->CycleAll = 1;
}

void trs_sum_add(TRS_SUM *s, const TRS_ROW *r, int nt)
{
    size_t len;
    int ap = trs_apply(r, nt);

    if (ap == TRS_AP_REPLUG) {
        s->Replug |= strcmp(r->Name, "XhciBulkOnlyOwnId") == 0
                         ? TRS_REPLUG_OWN_ID
                         : TRS_REPLUG_FORCE;
        return;
    }
    if (strstr(s->Names, r->Name) == NULL) {
        len = strlen(s->Names);
        if (len + strlen(r->Name) + 3 < sizeof(s->Names)) {
            if (len > 0) {
                strcat(s->Names, ", ");
            }
            strcat(s->Names, r->Name);
        }
    }
    s->Restart = 1;
    if (!(nt && r->Cycle && ap != TRS_AP_BOOT)) {
        s->CycleAll = 0;
    }
}

void trs_sum_text(const TRS_SUM *s, int nt, char *out, size_t cap)
{
    TR_TEXT t;

    tr_text_init(&t, out, cap);
    if (s->Restart) {
        tr_printf(&t, "Restart Windows for this to take effect: %s.",
                  s->Names);
        if (nt && s->CycleAll) {
            tr_put(&t, " (Disabling and enabling the controller in Device "
                       "Manager applies it too.)");
        }
    }
    if (s->Replug) {
        if (s->Restart) {
            tr_put(&t, "\n");
        }
        if (s->Replug == (TRS_REPLUG_FORCE | TRS_REPLUG_OWN_ID)) {
            tr_put(&t, "XhciForceBulkOnly and XhciBulkOnlyOwnId take effect "
                       "when a drive is plugged in again.");
        } else if (s->Replug == TRS_REPLUG_OWN_ID) {
            tr_put(&t, "XhciBulkOnlyOwnId takes effect when a drive is "
                       "plugged in again.");
        } else {
            tr_put(&t, "XhciForceBulkOnly takes effect when a drive is "
                       "plugged in again.");
        }
        if (nt && (s->Replug & TRS_REPLUG_FORCE) != 0) {
            tr_put(&t, " With XhciBulkOnlyOwnId at 0, a drive Windows has "
                       "installed keeps its driver until it is uninstalled in "
                       "Device Manager and plugged in again.");
        }
    }
}

int trs_sum_exit(const TRS_SUM *s, int refused, int failed)
{
    return inst_exit_code(refused, failed, s->Restart, s->Replug);
}

/* ---- the elevated run's request ------------------------------------------ */

#define TRS_REQ_HEAD    "XHCITR-SET 1\n"
#define TRS_REQ_END     "END\n"

static int trs_field_ok(const char *s, size_t cap)
{
    size_t n = 0;

    for (; *s != '\0'; s++, n++) {
        if (*s == '\t' || *s == '\n' || *s == '\r' || n + 1 >= cap) {
            return 0;
        }
    }
    return 1;
}

int trs_req_make(const TRS_CHANGE *c, int n, char *out, size_t cap)
{
    size_t len;
    int i;

    if (cap == 0 || n < 0 || n > TRS_MAX_CHANGES) {
        return 0;
    }
    out[0] = '\0';
    len = strlen(TRS_REQ_HEAD);
    if (len + 1 > cap) {
        return 0;
    }
    strcpy(out, TRS_REQ_HEAD);
    for (i = 0; i < n; i++) {
        const char *on = c[i].On[0] != '\0' ? c[i].On : "*";
        size_t add;
        if (c[i].Name[0] == '\0' || c[i].Value[0] == '\0' ||
            !trs_field_ok(c[i].Name, TRS_NAME_CHARS) ||
            !trs_field_ok(c[i].Value, TRS_VAL_CHARS) ||
            !trs_field_ok(c[i].On, TR_ID_CHARS) ||
            strchr(c[i].Name, '=') != NULL) {
            out[0] = '\0';
            return 0;
        }
        add = strlen(c[i].Name) + 1 + strlen(c[i].Value) + 1 + strlen(on) + 1;
        if (len + add + 1 > cap) {
            out[0] = '\0';
            return 0;
        }
        strcat(out, c[i].Name);
        strcat(out, "=");
        strcat(out, c[i].Value);
        strcat(out, "\t");
        strcat(out, on);
        strcat(out, "\n");
        len += add;
    }
    if (len + strlen(TRS_REQ_END) + 1 > cap) {
        out[0] = '\0';
        return 0;
    }
    strcat(out, TRS_REQ_END);
    return 1;
}

/* Copies [from, to) into dst of cap; 0 when it does not fit or is empty. */
static int trs_span(const char *from, const char *to, char *dst, size_t cap)
{
    size_t n = (size_t)(to - from);

    if (n == 0 || n >= cap) {
        return 0;
    }
    memcpy(dst, from, n);
    dst[n] = '\0';
    return 1;
}

int trs_req_parse(const char *in, size_t cap, TRS_CHANGE *c, int max,
                  int *n)
{
    const char *p;
    const char *end;
    size_t hl = strlen(TRS_REQ_HEAD);

    *n = 0;
    end = (const char *)memchr(in, '\0', cap);
    if (end == NULL || (size_t)(end - in) < hl ||
        memcmp(in, TRS_REQ_HEAD, hl) != 0) {
        return 0;
    }
    for (p = in + hl; p < end;) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *eq;
        const char *tab;
        if (nl == NULL) {
            return 0;
        }
        if ((size_t)(nl - p) == 3 && memcmp(p, "END", 3) == 0) {
            return nl + 1 == end;
        }
        if (*n >= max) {
            return 0;
        }
        eq = (const char *)memchr(p, '=', (size_t)(nl - p));
        tab = eq != NULL ? (const char *)memchr(eq, '\t', (size_t)(nl - eq))
                         : NULL;
        if (eq == NULL || tab == NULL ||
            !trs_span(p, eq, c[*n].Name, sizeof(c[*n].Name)) ||
            !trs_span(eq + 1, tab, c[*n].Value, sizeof(c[*n].Value)) ||
            !trs_span(tab + 1, nl, c[*n].On, sizeof(c[*n].On))) {
            return 0;
        }
        if (strcmp(c[*n].On, "*") == 0) {
            c[*n].On[0] = '\0';
        }
        (*n)++;
        p = nl + 1;
    }
    return 0;
}

int trs_arg(const char *arg, TRS_CHANGE *c, const char **why)
{
    const char *eq = strchr(arg, '=');

    memset(c, 0, sizeof(*c));
    *why = "";
    if (eq == NULL || eq == arg || eq[1] == '\0') {
        *why = "takes Name=Value";
        return 0;
    }
    if (!trs_span(arg, eq, c->Name, sizeof(c->Name)) ||
        strlen(eq + 1) >= sizeof(c->Value)) {
        *why = "is too long";
        return 0;
    }
    tr_copy(c->Value, eq + 1, sizeof(c->Value));
    if (!trs_field_ok(c->Name, sizeof(c->Name)) ||
        !trs_field_ok(c->Value, sizeof(c->Value))) {
        *why = "takes Name=Value";
        return 0;
    }
    return 1;
}

/* ---- texts ---------------------------------------------------------------- */

void trs_psw_text(int on, int noDriver, char *out, size_t cap)
{
    TR_TEXT t;

    tr_text_init(&t, out, cap);
    if (on) {
        tr_put(&t, "At Windows' next start the shared connectors (usually "
                   "the blue ones) move from the USB 2.0 (EHCI) controller "
                   "to xHCI98, and a device on them is found again on "
                   "xHCI98. Do not have a drive on them busy when you "
                   "restart.");
        return;
    }
    tr_put(&t, "At Windows' next start the shared connectors stay with the "
               "USB 2.0 controller: a device on them runs at USB 2.0 speed "
               "at most, under that controller's driver.");
    if (noDriver) {
        tr_put(&t, " Windows has no working driver for the USB 2.0 "
                   "controller here, so a device on those connectors will "
                   "not work.");
    }
}

/* ---- quick save and its hotkey ------------------------------------------ */

int trs_qs_where(int installKnown, int installMade, int docsKnown)
{
    if (installKnown && installMade) {
        return TRS_QS_INSTALL;
    }
    return docsKnown ? TRS_QS_DOCS : TRS_QS_NONE;
}

void trs_qs_name(unsigned y, unsigned mo, unsigned d, unsigned h,
                 unsigned mi, unsigned s, int n, char *out, size_t cap)
{
    if (n >= 2) {
        tr_sprintf(out, cap, "%04u-%02u-%02u_%02u%02u%02u-%d", y, mo, d, h, mi,
                   s, n);
    } else {
        tr_sprintf(out, cap, "%04u-%02u-%02u_%02u%02u%02u", y, mo, d, h, mi, s);
    }
}

TR_U32 trs_hk_choice(int i)
{
    static const TR_U32 keys[TRS_HK_CHOICES] = {
        TRS_HK_DEFAULT,
        (0x6UL << 16) | 0x7AUL,     /* Ctrl+Shift+F11 */
        (0x3UL << 16) | 0x79UL,     /* Ctrl+Alt+F10 */
        (0x3UL << 16) | 0x78UL,     /* Ctrl+Alt+F9 */
        (0x2UL << 16) | 0x7AUL,     /* Ctrl+F11 (two keys: clashes more) */
        (0x7UL << 16) | 0x7AUL      /* Ctrl+Alt+Shift+F11 */
    };

    return (i >= 0 && i < TRS_HK_CHOICES) ? keys[i] : 0;
}

int trs_subsys(const char *hwid, TR_U32 *subsys)
{
    const char *p = hwid != NULL ? strstr(hwid, "&SUBSYS_") : NULL;
    TR_U32 v = 0;
    int i;

    if (p == NULL) {
        return 0;
    }
    p += 8;
    for (i = 0; i < 8; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (TR_U32)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v |= (TR_U32)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            v |= (TR_U32)(c - 'a' + 10);
        } else {
            return 0;
        }
    }
    if (p[8] != '\0' && p[8] != '&' && p[8] != '\\') {
        return 0;
    }
    *subsys = v;
    return 1;
}

int trs_a5_confirm(const TRS_ROW *r, const TRS_ENV *e, const char *value)
{
    if (r == NULL || e == NULL || value == NULL ||
        !inst_eq(r->Name, "XhciIntelNoRouteIrq") ||
        (!e->NoRouteW2kListed && !e->WalkPartial) ||
        !e->Inst.Nt || e->Inst.Major != 5UL || e->Inst.Minor != 0UL) {
        return 0;
    }
    {
        char *end;
        unsigned long v = strtoul(value, &end, 10);
        return value[0] >= '0' && value[0] <= '9' && *end == '\0' &&
               v == 0UL;
    }
}

void trs_a5_text(char *out, size_t cap)
{
    tr_copy(out, "XhciIntelNoRouteIrq 0 on this computer: Windows 2000 may "
            "stop at startup with STOP 0x000000A5 when the computer type in "
            "Device Manager is an ACPI one. Use 0 here only with the "
            "\"Standard PC\" computer type. If Windows stops at startup, "
            "press F8 at the start and choose \"Last Known Good "
            "Configuration\" (this undoes the change only if Windows has "
            "not started with it yet), then set XhciIntelNoRouteIrq back to "
            "1.", cap);
}

const char *trs_tray_item(int i)
{
    static const char *const items[] = {
        "&Open", "Setti&ngs...", "&About", "-",
        "&Quick save (report + snapshot)", "-", "E&xit"
    };

    return (i >= 0 && i < (int)(sizeof(items) / sizeof(items[0]))) ? items[i]
                                                                   : NULL;
}

int trs_hk_index(TR_U32 key)
{
    int i;

    for (i = 0; key != 0 && i < TRS_HK_CHOICES; i++) {
        if (trs_hk_choice(i) == key) {
            return i;
        }
    }
    return -1;
}

int trs_hk_keys(TR_U32 key)
{
    TR_U32 m = (key >> 16) & 0xFFFFUL;
    int n = 1;

    n += (m & TRS_HK_ALT) ? 1 : 0;
    n += (m & TRS_HK_CTRL) ? 1 : 0;
    n += (m & TRS_HK_SHIFT) ? 1 : 0;
    n += (m & TRS_HK_WIN) ? 1 : 0;
    return n;
}

void trs_hk_text(TR_U32 key, char *out, size_t cap)
{
    TR_U32 m = (key >> 16) & 0xFFFFUL;
    TR_U32 vk = key & 0xFFFFUL;

    tr_sprintf(out, cap, "%s%s%s%sF%lu", (m & TRS_HK_CTRL) ? "Ctrl+" : "",
               (m & TRS_HK_ALT) ? "Alt+" : "", (m & TRS_HK_SHIFT) ? "Shift+" : "",
               (m & TRS_HK_WIN) ? "Win+" : "",
               (unsigned long)(vk >= 0x70UL ? vk - 0x6FUL : 0UL));
}

int trs_hk_ok(TR_U32 key)
{
    TR_U32 m = (key >> 16) & 0xFFFFUL;
    TR_U32 vk = key & 0xFFFFUL;

    if ((m & ~(TRS_HK_ALT | TRS_HK_CTRL | TRS_HK_SHIFT)) != 0 ||
        (m & (TRS_HK_ALT | TRS_HK_CTRL)) == 0) {
        return 0;
    }
    return vk >= 0x70UL && vk <= 0x87UL && vk != 0x7BUL;
}

const char *trs_caution(const TRS_ROW *r)
{
    const INST_VALUE *v;

    if (r == NULL) {
        return NULL;
    }
    if (r->Note != NULL && r->View != TRS_VIEW_OUT) {
        return r->Note;
    }
    v = trs_value(r);
    return v != NULL ? v->Caution : NULL;
}

void trs_caution_text(const TRS_ROW *r, INST_U32 values, char *out,
                      size_t cap)
{
    const char *c = trs_caution(r);
    int i;

    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (c == NULL) {
        return;
    }
    i = r != NULL ? inst_value_find(r->Name) : -1;
    if (i >= 0 && instValues[i].Caution == c && strstr(c, "%s") != NULL) {
        if (instValues[i].CautionMask != 0 &&
            (instValues[i].CautionMask & values) == 0) {
            values = instValues[i].CautionMask;
        }
        inst_caution_text(i, values, out, cap);
        return;
    }
    inst_copy(out, c, cap);
}
