/*
 * tr_set.c - the pure half of XHCITR's Settings window and XHCITR /set
 * (tr_set.h; design record 25 section 22; roadmap-usb4.md task 47.35).
 * Pure: no Windows call.
 */

#include <stdlib.h>
#include <string.h>
#include "tr_set.h"


/* ---- this Windows's defaults (tr_set.h) ---------------------------------- */

static INST_ENV trsSystem;

void trs_set_system(const INST_ENV *e)
{
    if (e != NULL) {
        trsSystem = *e;
    }
}

INST_U32 trs_absent_def(const TRS_ROW *r)
{
    if (r == NULL) {
        return 0;
    }
    return r->AbsentDef == TRS_DEF_SYSTEM ? inst_keepbot_default(&trsSystem)
                                          : r->AbsentDef;
}

INST_U32 trs_install_def(const TRS_ROW *r)
{
    if (r == NULL) {
        return 0;
    }
    return r->InstallDef == TRS_DEF_SYSTEM ? inst_keepbot_default(&trsSystem)
                                           : r->InstallDef;
}

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
      TRS_EFF_LIST, 0x1FUL, 0UL, 0UL, -1, NULL },
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
    { "XhciSsFlapFallback", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciResumeKeep", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciResumeKeepBot", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LOW2, 0, TRS_DEF_SYSTEM, TRS_DEF_SYSTEM, -1, NULL },
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
    /* Task 49.38: every other value, by group (instValues[]). */
    { "XhciUasNoAtaPassThrough", TRS_TAB_USB, A, TRS_AP_REPLUG, 0,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciUasFastReset", TRS_TAB_USB, A, TRS_AP_REPLUG, 0,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciMsiStrategy", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
    { "XhciMsiIsaIrq", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
    { "XhciMsiShare", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_SHARE, 0, 8UL, 8UL, -1, NULL },
    { "XhciMsiPools", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
    { "XhciMsiInputFirst", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
    { "XhciMsiInputLast", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
    { "XhciMsiInputBudget", TRS_TAB_USB, A, TRS_AP_CTRL, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
    { "XhciDmaStaging", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 0UL, 0UL, -1, NULL },
    { "XhciStagePoolKiB", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_CLAMP, 0, 2048UL, 2048UL, -1, NULL },
    { "XhciEventRingTrbs", TRS_TAB_USB, A, TRS_AP_BOOT, 0,
      TRS_EFF_RING, 0, 256UL, 256UL, -1, NULL },
    { "XhciTolerance", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCommandCheck", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciStreamDequeueRepair", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciAccept096", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciAcpiTables", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciIntervalCap", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciAvgTrbEsit", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciUsb2HubSpeedFallback", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_NONZERO, 0, 1UL, 1UL, -1, NULL },
    { "XhciMsiAvoid", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciStreamsOff", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciResumeReinit", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciIntelShutdownToEhci", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciShutdownReset", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 0UL, 0UL, -1, NULL },
    { "XhciSuspendHaltDelay", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciSuspendHaltTimeout", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciAsmediaFlowControl", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciTiIntervalCap", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciStuckLinkReset", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciStreamRingPad", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciControlLinkNoOp", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciIsochSpuriousSuccess", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x7UL, 1UL, 1UL, -1, NULL },
    { "XhciAmdSuspendBitClear", TRS_TAB_USB, A, TRS_AP_CTRL, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
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
      TRS_EFF_LIST, 0x7UL, 0UL, 0UL, -1, NULL },
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
    { "LogBytes", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LOW, 0, 262144UL, 262144UL, -1, NULL },
    { "XhciCmNetPollMs", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_RANGE, 0, 1UL, 1UL, -1, NULL },
    { "XhciCmNetRxFrames", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_RANGE, 0, 128UL, 128UL, -1, NULL },
    { "XhciCmNetTxFrames", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_RANGE, 0, 128UL, 128UL, -1, NULL },
    { "XhciCmNetE2E", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmNetInterrupt", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmInferAtStart", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciCmAsm4242Handover", TRS_TAB_CM, A, TRS_AP_BOOT, 0,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmGen4LinkRecovery", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmSleepEntry", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmWakePowerUp", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 1UL, 1UL, -1, NULL },
    { "XhciCmAsmediaPcie7Credits", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_LIST, 0x3UL, 0UL, 0UL, -1, NULL },
    { "XhciCmTunnelBridge", TRS_TAB_CM, A, TRS_AP_NHI, 1,
      TRS_EFF_ASIS, 0, 0UL, 0UL, -1, NULL },
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
      0, 0UL, 0UL, INST_S98_HOLD9X, NULL },
    { "StorTakeFirmware", TRS_TAB_S98, A, TRS_AP_BOOT, 0, TRS_EFF_LIST,
      0, 0UL, 0UL, INST_S98_TAKEFW, NULL }
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

int trs_matches(const TRS_ROW *r, const char *text)
{
    const INST_S98_VALUE *s;
    int k;

    if (inst_has_text("", text)) {
        return 1;
    }
    if (r == NULL) {
        return 0;
    }
    if (r->S98 < 0) {
        return inst_has_text(r->Name, text) ||
               inst_value_matches(trs_value(r), text);
    }
    s = &instS98Values[r->S98];
    if (inst_has_text(s->Name9x, text) ||
        (s->KeyNt != NULL && inst_has_text(s->KeyNt, text)) ||
        inst_has_text(s->What, text)) {
        return 1;
    }
    for (k = 0; k < INST_S98_WORDS_MAX; k++) {
        if ((s->Kind == INST_S98_WORDS && s->Words[k] != NULL &&
             inst_has_text(s->Words[k], text)) ||
            (s->Means[k] != NULL && inst_has_text(s->Means[k], text))) {
            return 1;
        }
    }
    return 0;
}

int trs_same_devices(const char *const *ida, const char *const *drva, int n,
                     const char *const *idb, const char *const *drvb, int m)
{
    int i;
    int k;

    if (n != m) {
        return 0;
    }
    for (i = 0; i < m; i++) {
        for (k = 0; k < n; k++) {
            if (tr_eq(ida[k], idb[i]) && tr_eq(drva[k], drvb[i])) {
                break;
            }
        }
        if (k == n) {
            return 0;
        }
    }
    return 1;
}

int trs_listed(const TRS_ROW *r, const TRS_ENV *e, int adv, const char *text)
{
    int searching = !inst_has_text("", text);

    if (r == NULL || r->View == TRS_VIEW_OUT ||
        trs_state(r, e) == TRS_HIDE) {
        return 0;
    }
    if (r->View == TRS_VIEW_ADV && !adv && !searching) {
        return 0;
    }
    return trs_matches(r, text);
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
    if (v != NULL && v->Loc == INST_LOC_DRIVE) {
        return TRS_LOC_DEVICE;
    }
    return TRS_LOC_DRIVER;
}

/* ---- the system rules ---------------------------------------------------- */

static int trs_nt5(const TRS_ENV *e)
{
    return e->Inst.Nt && e->Inst.Major == 5UL;
}

static int trs_me(const TRS_ENV *e)
{
    return !e->Inst.Nt && e->Inst.Minor == 90UL;
}

int trs_state(const TRS_ROW *r, const TRS_ENV *e)
{
    const INST_VALUE *v;

    if (r == NULL || r->View == TRS_VIEW_OUT) {
        return TRS_HIDE;
    }
    if (r->S98 >= 0) {
        if (inst_s98_applies_os(r->S98, e->Inst.Nt, trs_me(e))) {
            return TRS_SHOW;
        }
        return inst_s98_greyed_os(r->S98, e->Inst.Nt, trs_me(e)) ? TRS_GREY
                                                                  : TRS_HIDE;
    }
    v = trs_value(r);
    if (v == NULL) {
        return TRS_HIDE;
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
    if (v->Loc == INST_LOC_DRIVE) {
        return e->UasDrives > 0 ? TRS_SHOW : TRS_HIDE;
    }
    if (r->Tab == TRS_TAB_CM && !e->Inst.CmNhi) {
        return TRS_HIDE;
    }
    if (r->Tab == TRS_TAB_USB && !e->Inst.AnyXhci) {
        return TRS_HIDE;
    }
    if (inst_def_greyed(v, &e->Inst)) {
        return TRS_GREY;
    }
    if (!inst_def_relevant(v, &e->Inst)) {
        return TRS_HIDE;    /* XhciIntelIrqRoute with no reviewed controller */
    }
    if (inst_def_set_mask(v, &e->Inst) == 0 && v->Hi == 0 &&
        v->Kind != INST_VK_TEXT) {
        return TRS_GREY;
    }
    return TRS_SHOW;
}

const char *trs_only(const TRS_ROW *r, const TRS_ENV *e)
{
    const INST_VALUE *v;

    if (r == NULL) {
        return NULL;
    }
    if (r->S98 >= 0) {
        return inst_s98_only_os(r->S98, e != NULL && trs_me(e));
    }
    v = trs_value(r);
    return inst_def_only(v);
}

/* ---- values ------------------------------------------------------------- */

/* The values a list row admits on this system, Setup's /set mask (the
 * Tree's own rows: their table mask). */
static INST_U32 trs_hi(const TRS_ROW *r, const INST_VALUE *x,
                       const TRS_ENV *e);

static INST_U32 trs_mask(const TRS_ROW *r, const TRS_ENV *e)
{
    const INST_VALUE *v = trs_value(r);

    if (v == NULL) {
        return 0;
    }
    /* XhciIntelNoRouteIrq 0 on Windows 2000: Setup does not offer it, the
     * Tree does, after its question (trs_a5_confirm). */
    if (v->Apply == INST_APPLY_NOROUTE) {
        return trs_nt5(e) ? v->Settable : 0;
    }
    return inst_def_set_mask(v, &e->Inst);
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
    /* A row of picks (XhciEventRingTrbs) is a list: its picks, their
     * texts. */
    return v != NULL && ((v->Hi != 0 && v->PickCount == 0) ||
                         v->Kind == INST_VK_TEXT);
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
    if (trs_value(r) != NULL && trs_value(r)->PickCount > 0) {
        const INST_VALUE *x = trs_value(r);
        for (k = 0; k < (INST_U32)x->PickCount && n < cap; k++) {
            vals[n++] = x->Picks[k];
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
    if (inst_eq(r->Name, "XhciInterruptMode") && !adv) {
        mask &= ~0x10UL;    /* mode 4: with "Show advanced settings" */
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
    if (x != NULL && x->Kind == INST_VK_TEXT) {
        tr_copy(out, "auto", cap);  /* its only default (the INFs') */
        return;
    }
    if (x != NULL && x->Hi == 0 && v < INST_CHOICES && x->Choice[v] != NULL) {
        tr_sprintf(out, cap, "%lu - %s", (unsigned long)v, x->Choice[v]);
        return;
    }
    if (x != NULL && x->PickCount > 0) {
        int k;
        for (k = 0; k < x->PickCount && k < INST_CHOICES; k++) {
            if (x->Picks[k] == v && x->Choice[k] != NULL) {
                tr_sprintf(out, cap, "%lu - %s", (unsigned long)v,
                           x->Choice[k]);
                return;
            }
        }
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
    if (x != NULL && x->Kind == INST_VK_TEXT) {
        tr_copy(out, "auto, or up to 4 bb:dd.f, comma-separated", cap);
        return;
    }
    if (x == NULL || x->Hi == 0) {
        return;
    }
    if (x->Accept != 0) {
        /* The numbers below 32 the row takes, in runs, the last one
         * running on to Hi when 31 is taken ("0, 3 to 7, 10, 11, 24 to
         * 255"). */
        TR_TEXT t;
        INST_U32 k = x->Lo;
        INST_U32 hi = trs_hi(r, x, e);
        int first = 1;
        tr_text_init(&t, out, cap);
        while (k < 32UL && k <= hi) {
            INST_U32 end;
            if ((x->Accept & (1UL << k)) == 0) {
                k++;
                continue;
            }
            end = k;
            while (end + 1UL < 32UL && end + 1UL <= hi &&
                   (x->Accept & (1UL << (end + 1UL)))) {
                end++;
            }
            if (end == 31UL && hi > 31UL) {
                end = hi;
            }
            tr_printf(&t, "%s%lu", first ? "" : ", ", (unsigned long)k);
            if (end > k) {
                tr_printf(&t, end == k + 1UL ? ", %lu" : " to %lu",
                          (unsigned long)end);
            }
            first = 0;
            k = end + 1UL;
        }
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

/* A range row's top here: XhciMsiIsaIrq names no input above 23 on XP x64,
 * whose image offers the ISA IRQs alone (hcd_ctl.c, PoolsOffered). */
static INST_U32 trs_hi(const TRS_ROW *r, const INST_VALUE *x,
                       const TRS_ENV *e)
{
    if (e != NULL && e->Inst.Arch == INST_ARCH_AMD64 &&
        inst_eq(r->Name, "XhciMsiIsaIrq")) {
        return 23UL;
    }
    return x->Hi;
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
    if (x->Kind == INST_VK_TEXT) {
        /* Task 49.38: XhciCmTunnelBridge, as the driver reads it. */
        if (tr_eq(text, "default") || text[0] == '\0') {
            text = "auto";
        }
        if (!inst_bdf_list_ok(text) || strlen(text) >= cap) {
            *why = "it takes auto, or up to 4 bb:dd.f separated by commas "
                   "(bb and dd hex, dd up to 1F, f 0 to 7)";
            return 0;
        }
        tr_copy(out, inst_bdf_auto(text) ? "auto" : text, cap);
        return 1;
    }
    if (tr_eq(text, "default")) {
        v = trs_install_def(r);
    } else if (!trs_number(text, &v)) {
        *why = "not a decimal number";
        return 0;
    }
    if (x->Hi != 0 && x->PickCount == 0) {
        if (v < x->Lo || v > trs_hi(r, x, e) ||
            (x->Step > 1UL && v % x->Step != 0) ||
            (x->Accept != 0 && v < 32UL && (x->Accept & (1UL << v)) == 0)) {
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

/* The system the rows are read on, for the two values whose meaning is
 * NT's alone (trs_set_nine). */
static int trsNine;

void trs_set_nine(int nine)
{
    trsNine = nine ? 1 : 0;
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
        e = trs_absent_def(r);
        if (held != TRS_NO_HOLD && e > held) {
            e = held;
        }
        return e;
    }
    e = v;
    switch (r->Effect) {
    case TRS_EFF_LIST:
        if (v > 31UL || (r->Accept & (1UL << v)) == 0 ||
            (trsNine && ((v == 4UL && inst_eq(r->Name, "XhciInterruptMode")) ||
                         (v == 2UL &&
                          inst_eq(r->Name, "XhciCmRingInterrupt"))))) {
            /* 98 SE and ME: mode 4 runs as 0 and the ring interrupt's 2
             * polls, as the drivers read them there. */
            *how = TRS_HOW_DEFAULT;
            e = trs_absent_def(r);
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
                          (x->Step > 1UL && v % x->Step != 0) ||
                          (x->PickCount > 0 && !inst_value_takes(x, v)))) {
            *how = TRS_HOW_DEFAULT;
            e = trs_absent_def(r);
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
        if (x != NULL && x->Step > 1UL && e % x->Step != 0) {
            /* Whole blocks (xhci_stage.c, XhciStagePoolSlabs). */
            *how = TRS_HOW_HELD;
            e -= e % x->Step;
        }
        break;
    case TRS_EFF_ZERODEF:
        if (v == 0) {
            *how = TRS_HOW_DEFAULT;
            e = trs_absent_def(r);
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
    case TRS_EFF_ASIS:
        /* The mode-4 reads take 0xFFFFFFFF for "not found" (xhci_msg4.h,
         * XHCI_POLL_NOT_FOUND): the default. */
        if (v == 0xFFFFFFFFUL) {
            *how = TRS_HOW_DEFAULT;
            e = r->AbsentDef;
        }
        break;
    case TRS_EFF_LOW:
        if (x != NULL && v < x->Lo) {
            *how = TRS_HOW_HELD;
            e = x->Lo;
        }
        break;
    case TRS_EFF_SHARE:
        if (v == 0xFFFFFFFFUL) {
            *how = TRS_HOW_DEFAULT;
            e = r->AbsentDef;
        } else if (v == 0) {
            /* Not offered (the owner, 2026-11-13): the driver runs it as
             * 1, no sharing. */
            *how = TRS_HOW_HELD;
            e = 1;
        } else if (x != NULL && v > x->Hi) {
            *how = TRS_HOW_HELD;
            e = x->Hi;
        }
        break;
    case TRS_EFF_RING:
        if (v != 256UL && v != 1024UL) {
            *how = TRS_HOW_HELD;
            e = 1024UL;
        }
        break;
    case TRS_EFF_LOW2:
        if (v > 3UL) {
            *how = TRS_HOW_HELD;
            e = v & 3UL;
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
        if (trs_value(r) != NULL && trs_value(r)->Kind == INST_VK_TEXT) {
            tr_copy(out, "not text (the driver uses auto)", cap);
            break;
        }
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
        if (trs_value(r) != NULL && trs_value(r)->Kind == INST_VK_TEXT) {
            if (v->IsText) {
                tr_copy(out, v->Text[0] != '\0' ? v->Text : "auto", cap);
            } else {
                tr_copy(out, "not text (the driver uses auto)", cap);
            }
            break;
        }
        /* XhciEventRingTrbs: another type is the larger ring. */
        trs_value_text(r, r->Effect == TRS_EFF_RING ? 1024UL
                                                    : trs_absent_def(r),
                       t, sizeof(t));
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

void trs_raw_from_query(int q, unsigned long type, unsigned long n,
                        const unsigned char *data, TRS_RAW *v)
{
    v->State = TRS_RAW_UNREAD;
    v->Value = 0;
    v->IsText = 0;
    v->Text[0] = '\0';
    if (q == TRS_Q_NOT_FOUND ||
        (q == TRS_Q_OK && type == TRS_REG_NONE && n == 0)) {
        v->State = TRS_RAW_ABSENT;
    } else if (q == TRS_Q_MORE_DATA) {
        v->State = TRS_RAW_OTHER;
    } else if (q == TRS_Q_OK) {
        if (type == TRS_REG_DWORD && n == 4) {
            v->State = TRS_RAW_DWORD;
            v->Value = (INST_U32)data[0] | ((INST_U32)data[1] << 8) |
                       ((INST_U32)data[2] << 16) | ((INST_U32)data[3] << 24);
        } else {
            v->State = TRS_RAW_OTHER;
            if (type == TRS_REG_SZ) {
                /* XhciCmTunnelBridge's (task 49.38). */
                size_t k;
                v->IsText = 1;
                for (k = 0; k < n && k + 1 < sizeof(v->Text) &&
                            data[k] != '\0'; k++) {
                    v->Text[k] = (char)data[k];
                }
                v->Text[k] = '\0';
            }
        }
    }
}

static int trs_raw_same(const TRS_RAW *a, const TRS_RAW *b)
{
    if (a->State != b->State) {
        return 0;
    }
    if (a->State == TRS_RAW_OTHER) {
        return a->IsText == b->IsText &&
               (!a->IsText || tr_eq(a->Text, b->Text));
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
            tr_copy(out, trs_loc(r) == TRS_LOC_DEVICE
                             ? "not the same on every drive"
                             : "not the same on every controller", cap);
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
            tr_copy(out, trs_loc(r) == TRS_LOC_DEVICE
                             ? "not the same on every drive"
                             : "not the same on every controller", cap);
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
        if (trs_loc(r) == TRS_LOC_DEVICE) {
            s->Replug |= TRS_REPLUG_DRIVE;
            if (strstr(s->DriveNames, r->Name) == NULL &&
                strlen(s->DriveNames) + strlen(r->Name) + 6 <
                    sizeof(s->DriveNames)) {
                if (s->DriveNames[0] != '\0') {
                    strcat(s->DriveNames, " and ");
                }
                strcat(s->DriveNames, r->Name);
            }
            return;
        }
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
        if ((s->Replug & TRS_REPLUG_DRIVE) != 0) {
            tr_printf(&t, "%s take%s effect when the drive is plugged in "
                          "again.%s", s->DriveNames,
                      strstr(s->DriveNames, " and ") != NULL ? "" : "s",
                      (s->Replug & ~TRS_REPLUG_DRIVE) != 0 ? "\n" : "");
        }
        if ((s->Replug & ~TRS_REPLUG_DRIVE) == 0) {
            /* the drive values alone */
        } else if ((s->Replug & ~TRS_REPLUG_DRIVE) ==
                   (TRS_REPLUG_FORCE | TRS_REPLUG_OWN_ID)) {
            tr_put(&t, "XhciForceBulkOnly and XhciBulkOnlyOwnId take effect "
                       "when a drive is plugged in again.");
        } else if ((s->Replug & ~TRS_REPLUG_DRIVE) == TRS_REPLUG_OWN_ID) {
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
            !trs_field_ok(c[i].Drv, TRS_DRV_CHARS) ||
            (c[i].Drv[0] != '\0' && c[i].On[0] == '\0') ||
            strchr(c[i].Name, '=') != NULL) {
            out[0] = '\0';
            return 0;
        }
        add = strlen(c[i].Name) + 1 + strlen(c[i].Value) + 1 + strlen(on) + 1;
        if (c[i].Drv[0] != '\0') {
            add += 1 + strlen(c[i].Drv);
        }
        if (len + add + 1 > cap) {
            out[0] = '\0';
            return 0;
        }
        strcat(out, c[i].Name);
        strcat(out, "=");
        strcat(out, c[i].Value);
        strcat(out, "\t");
        strcat(out, on);
        if (c[i].Drv[0] != '\0') {
            /* Task 49.38: the drive's driver key as the window saw it. */
            strcat(out, "\t");
            strcat(out, c[i].Drv);
        }
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
        const char *tab2;
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
        tab2 = tab != NULL ? (const char *)memchr(tab + 1, '\t',
                                                  (size_t)(nl - tab - 1))
                           : NULL;
        c[*n].Drv[0] = '\0';
        if (eq == NULL || tab == NULL ||
            !trs_span(p, eq, c[*n].Name, sizeof(c[*n].Name)) ||
            !trs_span(eq + 1, tab, c[*n].Value, sizeof(c[*n].Value)) ||
            !trs_span(tab + 1, tab2 != NULL ? tab2 : nl, c[*n].On,
                      sizeof(c[*n].On)) ||
            (tab2 != NULL &&
             (!trs_span(tab2 + 1, nl, c[*n].Drv, sizeof(c[*n].Drv)) ||
              memchr(tab2 + 1, '\t', (size_t)(nl - tab2 - 1)) != NULL))) {
            return 0;
        }
        if (strcmp(c[*n].On, "*") == 0) {
            if (c[*n].Drv[0] != '\0') {
                return 0;   /* a driver key names one device */
            }
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

int trs_set_note(const TRS_ROW *r, const TRS_ENV *e, const char *value)
{
    char canon[TRS_VAL_CHARS];
    const char *why;

    if (r == NULL || e == NULL || value == NULL ||
        !trs_parse(r, e, value, canon, sizeof(canon), &why)) {
        return TRS_NOTE_NONE;
    }
    if (trs_a5_confirm(r, e, value)) {
        return TRS_NOTE_A5;
    }
    return inst_eq(r->Name, "XhciIntelPortSwitch") ? TRS_NOTE_PSW
                                                   : TRS_NOTE_NONE;
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
    if (i >= 0 && instValues[i].Caution == c) {
        /* its advanced part with the values that are offered alone */
        inst_caution_text(i, values, out, cap);
        return;
    }
    inst_copy(out, c, cap);
}
