/*
 * test_tbt_hw.c - host vectors for the hardware profiles, the platform
 * readiness sequence and the connection manager's selection
 * (src\cm\tbt_hw.c; roadmap-usb4.md task 39-A.3; the orchestrator's
 * coverage plan of 2026-10-15, owner to confirm).
 *
 * The ids, the profile traits and the Ice Lake family's configuration
 * sequence are Linux drivers/thunderbolt's (nhi.h, pci.c nhi_ids and
 * icl_nhi_*, icm.c icm_probe and its predicates) and FreeBSD nhi_reg.h's;
 * the firmware-status values are ICM-PROTOCOL.md 2.1's. Every outcome of
 * TbtHwSelect is driven: ICM, safe mode, software admission by identity,
 * by a valid disabled ICM and by XhciCmMode 3, and each inert reason -
 * with the check that an inert selection wrote nothing and read no Intel
 * status register where none may be read.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_hw.h"
#include "../src/cm/cm_vals.h"
#include "tbt_fake.h"
#include "test_harness.h"

static const TBT_CFG g_cfg = { NULL, fakeCfgRead, fakeCfgWrite };

static void test_match(void)
{
    const TBT_HW_PROFILE *hw;

    hw = TbtHwMatch(0x8086, 0x15BF, 0x088000, FALSE);
    CHECK(hw != NULL, "15BF matched");
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileAlpineRidge, "Alpine Ridge row");
    CHECK(hw != NULL && (hw->Flags & TBT_HW_CAPTURED), "the captured id");
    CHECK(hw != NULL && hw->Ownership == TBT_HW_OWN_ICM_RUNNING, "icm_ar_is_supported");
    hw = TbtHwMatch(0x8086, 0x15D9, 0x088000, FALSE);
    CHECK(hw != NULL && !(hw->Flags & TBT_HW_CAPTURED), "15D9: Alpine Ridge, uncaptured");
    hw = TbtHwMatch(0x8086, 0x156A, 0x088000, FALSE);
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileFalconRidge, "Falcon Ridge row");
    CHECK(hw != NULL && hw->Ownership == TBT_HW_OWN_PRESENT, "icm_fr_is_supported");
    CHECK(hw != NULL && (hw->Flags & TBT_HW_FR_E2E), "QUIRK_E2E noted");
    hw = TbtHwMatch(0x8086, 0x15EB, 0x088000, FALSE);
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileTitanRidge, "Titan Ridge row");
    hw = TbtHwMatch(0x8086, 0x1137, 0x0C0340, FALSE);
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileTitanRidge, "Maple Ridge: Titan Ridge messages");
    CHECK(hw != NULL && hw->Ownership == TBT_HW_OWN_AUTH_DONE, "with the Tiger Lake predicate");
    hw = TbtHwMatch(0x8086, 0x8A17, 0x0C0340, FALSE);
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileIceLake, "Ice Lake row");
    CHECK(hw != NULL && hw->Ownership == TBT_HW_OWN_PRESENT, "Ice Lake: icm_fr_is_supported");
    CHECK(hw != NULL && (hw->Flags & TBT_HW_ICL_POWER), "icl_nhi_ops");
    hw = TbtHwMatch(0x8086, 0x7EC3, 0x0C0340, FALSE);
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileIceLake, "Meteor Lake: Ice Lake messages");
    CHECK(hw != NULL && hw->Ownership == TBT_HW_OWN_AUTH_DONE, "icm_tgl_is_supported");
    hw = TbtHwMatch(0x8086, 0xA833, 0x0C0340, FALSE);
    CHECK(hw != NULL && hw->Icm == NULL, "Lunar Lake: no ICM row");
    CHECK(hw != NULL && (hw->Flags & TBT_HW_ICL_POWER), "but icl_nhi_ops");
    hw = TbtHwMatch(0x8086, 0x5784, 0x0C0340, FALSE);
    CHECK(hw != NULL && hw->Icm == NULL && !(hw->Flags & TBT_HW_ICL_POWER), "Barlow Ridge");
    hw = TbtHwMatch(0x1022, 0x1668, 0x0C0340, FALSE);
    CHECK(hw != NULL && hw->Icm == NULL && hw->DeviceId == 0x1668, "AMD Pink Sardine, named");
    hw = TbtHwMatch(0x1B21, 0x2425, 0x0C0340, FALSE);
    CHECK(hw != NULL && (hw->Flags & TBT_HW_CLASS_MATCH), "any class 0C0340 host");
    CHECK(TbtHwMatch(0x8086, 0x15DC, 0x088000, FALSE) == NULL, "USB-only NHI: not a candidate");
    CHECK(TbtHwMatch(0x8086, 0x1513, 0x088000, FALSE) == NULL, "Light Ridge: not claimed");
    hw = TbtHwMatch(0x8086, 0x1234, 0x088000, TRUE);
    CHECK(hw != NULL && (hw->Flags & TBT_HW_FORCED), "XhciCmMode 2: unlisted Intel id");
    CHECK(hw != NULL && hw->Icm == &TbtIcmProfileAlpineRidge, "on the Alpine Ridge row");
    CHECK(TbtHwMatch(0x1022, 0x1234, 0x088000, TRUE) == NULL, "never a non-Intel id");

    CHECK_EQ(TbtHwRingFlags(TbtHwMatch(0x8086, 0x15BF, 0, FALSE)), 0,
             "Intel: mask written directly");
    CHECK_EQ(TbtHwRingFlags(TbtHwMatch(0x1022, 0x1669, 0, FALSE)),
             TBT_RING_START_MASK_IMC, "elsewhere: Mask Clear");
}

static void test_platform(void)
{
    const TBT_HW_PROFILE *icl;
    const TBT_HW_PROFILE *ar;
    TBT_HW_POWER pw;
    ULONG uuid[4];

    icl = TbtHwMatch(0x8086, 0x9A1B, 0x0C0340, FALSE);
    ar = TbtHwMatch(0x8086, 0x15BF, 0x088000, FALSE);

    CHECK_EQ(TbtHwForcePowerValue(0x00000000UL, TRUE), 0x22000002UL,
             "force power with DMA delay 0x22");
    CHECK_EQ(TbtHwForcePowerValue(0x55000001UL, TRUE), 0x22000003UL,
             "other bits kept, delay replaced");
    CHECK_EQ(TbtHwForcePowerValue(0x22000003UL, FALSE), 0x22000001UL, "clear");

    fakeReset();
    memset(&pw, 0, sizeof(pw));
    CHECK_EQ(TbtHwPlatformInit(ar, &g_cfg, &g_fakeHost, &pw), TBT_OK,
             "nothing to do on Alpine Ridge");
    CHECK_EQ(g_fake.CfgWrites, 0, "no configuration write");

    fakeReset();
    memset(&pw, 0, sizeof(pw));
    g_fake.Cfg[0xE4 / 4] = 0x00001003UL;
    g_fake.Vs9ReadyAfterReads = 5;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_OK, "Ice Lake family powered");
    CHECK_EQ(g_fake.Cfg[0xFC / 4], 0x22000002UL, "VS_CAP_22 written");
    CHECK(pw.ForcePowerSetHere, "recorded as set here");
    CHECK_EQ(g_fake.Vs9Reads, 5, "FW_READY polled until set");
    CHECK_EQ(g_fake.Cfg[0xE0 / 4], 0x10031003UL, "LTR: the maximum both ways");
    CHECK(TbtHwReadHostUuid(icl, &g_cfg, uuid), "fused UUID");
    CHECK_EQ(uuid[2], 0xFFFFFFFFUL, "upper half ones");
    CHECK(!TbtHwReadHostUuid(ar, &g_cfg, uuid), "no fused UUID on Alpine Ridge");

    /* Resume: the same sequence; force power already on stays recorded. */
    g_fake.Vs9Reads = 0;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_OK, "again at D0");
    CHECK(pw.ForcePowerSetHere, "still ours");
    (void)TbtHwPlatformShutdown(icl, &g_cfg, &pw, TRUE);
    CHECK_EQ(g_fake.Cfg[0xFC / 4], 0x22000000UL, "shutdown clears what this driver set");
    CHECK(!pw.ForcePowerSetHere, "record cleared");

    /* Force power the BIOS left on is left on. */
    fakeReset();
    memset(&pw, 0, sizeof(pw));
    g_fake.Cfg[0xFC / 4] = 0x22000002UL;
    g_fake.Vs9ReadyAfterReads = 1;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_OK, "already on");
    CHECK(!pw.ForcePowerSetHere, "not ours");
    (void)TbtHwPlatformShutdown(icl, &g_cfg, &pw, TRUE);
    CHECK(g_fake.Cfg[0xFC / 4] & 0x2UL, "left on");

    /* Never ready: 350 polls 3 ms apart, then a timeout; an inert
     * instance leaves the force power it set, even then (record 22
     * section 4.2, settled over record 21 section 1.4.2 on 2026-10-15). */
    fakeReset();
    memset(&pw, 0, sizeof(pw));
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_E_TIMEOUT, "never ready");
    CHECK_EQ(g_fake.Vs9Reads, 350, "350 reads");
    CHECK(g_fake.NowUs >= 1050000UL, "about a second");
    CHECK(pw.FwReadyFailed, "recorded");
    (void)TbtHwPlatformShutdown(icl, &g_cfg, &pw, FALSE);
    CHECK(g_fake.Cfg[0xFC / 4] & 2UL, "inert, never ready: left set");
    CHECK(pw.ForcePowerSetHere, "and still recorded as this driver's");

    /* Ready, but inert for another reason: left set. */
    fakeReset();
    memset(&pw, 0, sizeof(pw));
    g_fake.Vs9ReadyAfterReads = 1;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_OK, "ready");
    (void)TbtHwPlatformShutdown(icl, &g_cfg, &pw, FALSE);
    CHECK(g_fake.Cfg[0xFC / 4] & 2UL, "inert after a ready poll: left set");
    /* An unreadable shutdown keeps the record for another try. */
    fakeReset();
    memset(&pw, 0, sizeof(pw));
    g_fake.Vs9ReadyAfterReads = 1;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_OK, "ready");
    g_fake.CfgAllOnes = 1;
    CHECK_EQ(TbtHwPlatformShutdown(icl, &g_cfg, &pw, TRUE), TBT_E_HW, "unreadable");
    CHECK(pw.ForcePowerSetHere, "still ours");
    g_fake.CfgAllOnes = 0;
    CHECK_EQ(TbtHwPlatformShutdown(icl, &g_cfg, &pw, TRUE), TBT_OK, "retried");
    CHECK_EQ(g_fake.Cfg[0xFC / 4] & 2UL, 0, "cleared on the retry");
    CHECK(TbtHwIsApple(0x106B), "Apple's subsystem vendor");
    CHECK(!TbtHwIsApple(0x17AA), "Lenovo's is not");

    fakeReset();
    memset(&pw, 0, sizeof(pw));
    g_fake.Vs9ReadyAfterReads = 1;
    g_fake.Cfg[0xE4 / 4] = 0xFFFFFFFFUL;
    g_fake.Cfg[0xE0 / 4] = 0x12345678UL;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_E_HW,
             "VS_CAP_16 unreadable: not ready");
    CHECK_EQ(g_fake.Cfg[0xE0 / 4], 0x12345678UL, "no LTR written from it");

    fakeReset();
    memset(&pw, 0, sizeof(pw));
    g_fake.CfgAllOnes = 1;
    CHECK_EQ(TbtHwPlatformInit(icl, &g_cfg, &g_fakeHost, &pw), TBT_E_HW, "unreadable");
    CHECK_EQ(g_fake.CfgWrites, 0, "nothing written");

    /* The LC mailbox at system sleep. */
    fakeReset();
    g_fake.Vs18DoneAfterReads = 3;
    CHECK_EQ(TbtHwPlatformSuspend(icl, &g_cfg, &g_fakeHost, TRUE, TRUE), TBT_OK, "GO2SX");
    CHECK_EQ(g_fake.Cfg[0xF0 / 4], 0, "VALID cleared after DONE");
    CHECK_EQ(g_fake.CfgWrites, 2, "command, then clear");
    fakeReset();
    CHECK_EQ(TbtHwPlatformSuspend(icl, &g_cfg, &g_fakeHost, TRUE, FALSE), TBT_E_TIMEOUT,
             "never done: timeout");
    CHECK_EQ(g_fake.Cfg[0xF0 / 4], (0x03UL << 1) | 1UL, "GO2SX_NO_WAKE sent, left set");
    fakeReset();
    CHECK_EQ(TbtHwPlatformSuspend(icl, &g_cfg, &g_fakeHost, FALSE, TRUE), TBT_OK,
             "no ICM: nothing");
    CHECK_EQ(g_fake.CfgWrites, 0, "nothing written");
}

/* Every selection, and so every inert reason, is held to writing
 * nothing: no BAR0 register and no configuration dword. */
static void doSelect(const TBT_HW_PROFILE *hw, BOOLEAN ready, BOOLEAN apple,
                   BOOLEAN force, TBT_HW_SELECTION *s)
{
    unsigned long writes = g_fake.Writes;
    unsigned long cfgWrites = g_fake.CfgWrites;

    TbtHwSelect(hw, &g_fakeHost, ready, apple, force, s);
    if (s->Outcome == TBT_SEL_INERT) {
        CHECK_EQ(g_fake.Writes, writes, "an inert selection writes no register");
        CHECK_EQ(g_fake.CfgWrites, cfgWrites,
                 "an inert selection writes no configuration dword");
    }
}

static void test_select(void)
{
    TBT_HW_SELECTION s;
    const TBT_HW_PROFILE *ar = TbtHwMatch(0x8086, 0x15BF, 0, FALSE);
    const TBT_HW_PROFILE *fr = TbtHwMatch(0x8086, 0x156C, 0, FALSE);
    const TBT_HW_PROFILE *icl = TbtHwMatch(0x8086, 0x8A0D, 0, FALSE);
    const TBT_HW_PROFILE *tgl = TbtHwMatch(0x8086, 0x9A1D, 0, FALSE);
    const TBT_HW_PROFILE *mr = TbtHwMatch(0x8086, 0x1134, 0, FALSE);
    const TBT_HW_PROFILE *amd = TbtHwMatch(0x1022, 0x1669, 0x0C0340, FALSE);

    /* The captured Alpine Ridge: ICM. */
    fakeReset();
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_ICM, "Alpine Ridge in CM mode: ICM");
    CHECK_EQ(s.Mode, 3, "mode 3");
    CHECK_EQ(s.FwStatus, 0x800001A1UL, "status as captured");
    CHECK_EQ(g_fake.Writes, 0, "selection writes nothing");

    fakeReset();
    g_fake.Outmail = 0x800000F5UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SAFE_MODE, "mode 0: safe mode");
    fakeReset();
    g_fake.Outmail = 0x800001F5UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_INERT, "authentication mode: inert");
    CHECK_EQ(s.Reason, TBT_INERT_MODE, "mode");
    fakeReset();
    g_fake.Outmail = 0x800002F5UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_MODE, "endpoint mode: inert");
    fakeReset();
    g_fake.Outmail = 0x800007F5UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_INERT, "mode 7: inert");
    CHECK_EQ(s.Reason, TBT_INERT_MODE, "mode 7: the mode reason");

    fakeReset();
    g_fake.FwStatus = 0x800001A0UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_INERT, "ICM enable clear on Alpine Ridge: inert, no fallback");
    CHECK_EQ(s.Reason, TBT_INERT_ICM_NOT_RUNNING, "reason");

    fakeReset();
    g_fake.FwStatus = 0x000001A1UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_AUTHENTICATED, "never authenticated");
    CHECK(g_fake.NowUs >= 60UL * 50000UL, "after 60 reads 50 ms apart");

    fakeReset();
    g_fake.Caps = 0x0040000CUL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NHI_VERSION, "interface version 0x40: inert");
    fakeReset();
    g_fake.Caps = 0x0020000CUL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NHI_VERSION, "Alpine Ridge takes 0 alone");
    fakeReset();
    g_fake.Caps = 0x0020000CUL;
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_ICM, "the other rows take below 0x40");
    fakeReset();
    g_fake.Caps = 0x0040000CUL;
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NHI_VERSION, "and not 0x40");

    fakeReset();
    g_fake.Caps = 0xFFFFFFFFUL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_DECODING, "BAR0 not decoding");
    CHECK_EQ(g_fake.FwStatusReads, 0, "status never read");

    fakeReset();
    g_fake.FwStatus = 0xFFFFFFFFUL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_DECODING, "status all ones: inert, not a disabled ICM");

    fakeReset();
    doSelect(ar, FALSE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_PLATFORM, "platform not ready");
    fakeReset();
    doSelect(ar, TRUE, TRUE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_APPLE, "Apple: platform policy");
    fakeReset();
    doSelect(NULL, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_PLATFORM, "no profile");

    /* Falcon Ridge and Ice Lake read no mode. */
    fakeReset();
    g_fake.Outmail = 0x800001F5UL;
    doSelect(fr, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_ICM, "Falcon Ridge: ICM without a mode query");
    CHECK_EQ(s.Mode, 0xFFFFFFFFUL, "mode not read");
    fakeReset();
    doSelect(icl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_ICM, "Ice Lake: ICM");
    fakeReset();
    g_fake.FwStatus = 0x80000000UL;
    doSelect(icl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_ICM_NOT_RUNNING, "Ice Lake without ICM: inert");

    /* Tiger Lake class: ICM when authenticated and enabled. */
    fakeReset();
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_ICM, "Tiger Lake with ICM");
    fakeReset();
    g_fake.FwStatus = 0x00000001UL;
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_AUTHENTICATED, "ICM enabled, not authenticated: inert");
    CHECK(g_fake.NowUs >= 10000UL && g_fake.NowUs < 15000UL, "after the 10 ms wait");
    /* A valid disabled ICM admits the software CM, authenticated or not. */
    fakeReset();
    g_fake.FwStatus = 0x80000000UL;
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "Tiger Lake, ICM disabled: software CM");
    fakeReset();
    g_fake.FwStatus = 0x00000000UL;
    doSelect(mr, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "Maple Ridge, ICM disabled, not authenticated: software CM");
    fakeReset();
    g_fake.FwStatus = 0x00000001UL;
    g_fake.FwStatusNextAfter = 2;
    g_fake.FwStatusNext = 0x80000000UL;
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_CONFLICT, "enabled, then authenticated but disabled: conflict");
    fakeReset();
    g_fake.FwStatus = 0x000001A1UL;
    g_fake.FwStatusNextAfter = 3;
    g_fake.FwStatusNext = 0x800001A0UL;
    doSelect(ar, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_CONFLICT, "Alpine Ridge: enable drops during the auth poll");
    fakeReset();
    g_fake.FwStatus = 0xFFFFFFFFUL;
    doSelect(tgl, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_INERT, "unreadable status is not a disabled ICM");
    fakeReset();
    doSelect(mr, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_ICM, "Maple Ridge with ICM in CM mode");
    CHECK_EQ(s.Mode, 3, "the mode query ran (icm_ar_get_mode)");

    /* No ICM row: software admission; no Intel register read. */
    fakeReset();
    doSelect(amd, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "AMD: software CM");
    CHECK_EQ(g_fake.FwStatusReads, 0, "the Intel status register is never read");
    fakeReset();
    doSelect(amd, FALSE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_INERT, "but not before the platform is ready");
    fakeReset();
    g_fake.Caps = 0xFFFFFFFFUL;
    doSelect(amd, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_DECODING, "nor with BAR0 dead");

    /* XhciCmMode 3. */
    fakeReset();
    g_fake.FwStatus = 0x80000000UL;
    doSelect(tgl, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "forced, ICM disabled: software CM");
    fakeReset();
    doSelect(tgl, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_ICM_EN, "forced while the ICM runs: refused");
    fakeReset();
    g_fake.FwStatus = 0x80000000UL;
    doSelect(ar, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_UNSUPPORTED, "forced on Alpine Ridge: refused");
    CHECK_EQ(g_fake.FwStatusReads, 0, "without reading its status");
    fakeReset();
    doSelect(icl, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_UNSUPPORTED, "forced on Ice Lake: refused");
    fakeReset();
    g_fake.Caps = 0xFFFFFFFFUL;
    doSelect(tgl, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_DECODING, "forced cannot pass a dead BAR0");
    fakeReset();
    doSelect(tgl, FALSE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_PLATFORM, "nor a failed platform init");
}

/*
 * The two id tables kept in step (task 39-A.4): cm_vals.c's, which the
 * INF gate's CM-IDLIST rule reads and AddDevice logs, and tbt_hw.c's,
 * which decides at start (cm_nhi.c, cmProfileOfHw's mapping). Every
 * Intel and AMD device id, under the Thunderbolt class and the USB4 one,
 * gives the same profile from both.
 */
static ULONG hwProfile(const TBT_HW_PROFILE *hw)
{
    if (hw == NULL) {
        return CM_PROFILE_UNKNOWN;
    }
    if (hw->Icm == &TbtIcmProfileFalconRidge) {
        return CM_PROFILE_ICM_FR;
    }
    if (hw->Icm == &TbtIcmProfileAlpineRidge) {
        return CM_PROFILE_ICM_AR;
    }
    /* Maple Ridge and Tiger Lake to Meteor Lake: the Titan Ridge and Ice
     * Lake messages under the NVM-authentication predicate. */
    if (hw->Icm == &TbtIcmProfileTitanRidge) {
        return hw->Ownership == TBT_HW_OWN_AUTH_DONE ? CM_PROFILE_ICM_MR
                                                     : CM_PROFILE_ICM_TR;
    }
    if (hw->Icm == &TbtIcmProfileIceLake) {
        return hw->Ownership == TBT_HW_OWN_AUTH_DONE ? CM_PROFILE_ICM_TGL
                                                     : CM_PROFILE_ICM_ICL;
    }
    return CM_PROFILE_USB4;
}

static void test_tables(void)
{
    static const ULONG vendors[2] = { 0x8086, 0x1022 };
    static const ULONG classes[2] = { 0x088000, 0x0C0340 };
    CM_IDS ids;
    ULONG v;
    ULONG c;
    ULONG d;
    ULONG mismatches;

    mismatches = 0;
    for (v = 0; v < 2; v++) {
        for (c = 0; c < 2; c++) {
            for (d = 0; d < 0x10000UL; d++) {
                ids.Role = CM_ROLE_NHI;
                ids.Vendor = vendors[v];
                ids.Device = d;
                ids.ClassCode = classes[c];
                if (CmProfileFor(&ids) !=
                    hwProfile(TbtHwMatch(vendors[v], d, classes[c], FALSE))) {
                    if (mismatches < 4) {
                        printf("  table mismatch %04lX:%04lX class %06lX\n",
                               vendors[v], d, classes[c]);
                    }
                    mismatches++;
                }
            }
        }
    }
    CHECK_EQ(mismatches, 0, "cm_vals.c and tbt_hw.c name the same profiles");
}

int main(void)
{
    test_match();
    test_tables();
    test_platform();
    test_select();
    printf("test_tbt_hw: %d checks, %d failures\n", checks, failures);
    return failures;
}
