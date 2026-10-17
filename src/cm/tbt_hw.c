/*
 * tbt_hw.c - hardware profiles, platform readiness and the connection
 * manager's selection (see tbt_hw.h).
 *
 * Written for this project from the facts tbt_hw.h cites; no function
 * body of Linux or FreeBSD is copied.
 *
 * IRQL: TbtHwMatch, TbtHwRingFlags and TbtHwForcePowerValue at any IRQL;
 * the rest PASSIVE_LEVEL on the connection-manager thread, no lock held.
 */

#include "tbt_hw.h"

#define TBT_HW_ICL   (TBT_HW_ICL_POWER | TBT_HW_INTEL_INTERRUPTS)
#define TBT_HW_INTEL TBT_HW_INTEL_INTERRUPTS

/*
 * The table. Ids: Linux nhi.h and pci_ids.h PCI_DEVICE_ID_INTEL_*_NHI*,
 * pci.c nhi_ids (which gives icl_nhi_ops to Ice Lake through Wildcat Lake),
 * icm.c icm_probe (which gives each id its message row and predicate);
 * names as pci.ids (pci-ids.ucw.cz, 2026.10.14) gives them where it lists
 * them. FreeBSD nhi_reg.h DEVICE_PINK_SARDINE_0/1 for AMD.
 */
static const TBT_HW_PROFILE tbtHwTable[] = {
    /* Falcon Ridge: icm_fr_is_supported. */
    { 0x8086, 0x156A, "DSL5320 Falcon Ridge 2C", TBT_HW_INTEL | TBT_HW_FR_E2E,
      TBT_HW_OWN_PRESENT, &TbtIcmProfileFalconRidge },
    { 0x8086, 0x156C, "DSL5520 Falcon Ridge 4C", TBT_HW_INTEL | TBT_HW_FR_E2E,
      TBT_HW_OWN_PRESENT, &TbtIcmProfileFalconRidge },
    /* Alpine Ridge: icm_ar_is_supported. */
    { 0x8086, 0x1575, "DSL6340 Alpine Ridge 2C", TBT_HW_INTEL,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileAlpineRidge },
    { 0x8086, 0x1577, "DSL6540 Alpine Ridge 4C", TBT_HW_INTEL,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileAlpineRidge },
    { 0x8086, 0x15BF, "JHL6240 Alpine Ridge LP", TBT_HW_INTEL | TBT_HW_CAPTURED,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileAlpineRidge },
    { 0x8086, 0x15D2, "JHL6540 Alpine Ridge C 4C", TBT_HW_INTEL,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileAlpineRidge },
    { 0x8086, 0x15D9, "JHL6340 Alpine Ridge C 2C", TBT_HW_INTEL,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileAlpineRidge },
    /* Titan Ridge: icm_ar_is_supported. */
    { 0x8086, 0x15E8, "JHL7540 Titan Ridge 2C", TBT_HW_INTEL,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileTitanRidge },
    { 0x8086, 0x15EB, "JHL7540 Titan Ridge 4C", TBT_HW_INTEL,
      TBT_HW_OWN_ICM_RUNNING, &TbtIcmProfileTitanRidge },
    /* Maple Ridge: Titan Ridge's messages, icm_tgl_is_supported. */
    { 0x8086, 0x1134, "Maple Ridge 2C", TBT_HW_INTEL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileTitanRidge },
    { 0x8086, 0x1137, "Maple Ridge 4C", TBT_HW_INTEL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileTitanRidge },
    /* Ice Lake: icm_fr_is_supported. */
    { 0x8086, 0x8A0D, "Ice Lake NHI1", TBT_HW_ICL,
      TBT_HW_OWN_PRESENT, &TbtIcmProfileIceLake },
    { 0x8086, 0x8A17, "Ice Lake NHI0", TBT_HW_ICL,
      TBT_HW_OWN_PRESENT, &TbtIcmProfileIceLake },
    /* Tiger Lake to Meteor Lake: icm_tgl_is_supported. */
    { 0x8086, 0x9A1B, "Tiger Lake-LP NHI0", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x9A1D, "Tiger Lake-LP NHI1", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x9A1F, "Tiger Lake-H NHI0", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x9A21, "Tiger Lake-H NHI1", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x463E, "Alder Lake-P NHI0", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x466D, "Alder Lake-P NHI1", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0xA73E, "Raptor Lake-P NHI0", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0xA76D, "Raptor Lake-P NHI1", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x7EB2, "Meteor Lake-M NHI0", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x7EC2, "Meteor Lake-P NHI0", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    { 0x8086, 0x7EC3, "Meteor Lake-P NHI1", TBT_HW_ICL,
      TBT_HW_OWN_AUTH_DONE, &TbtIcmProfileIceLake },
    /* Lunar Lake, Panther Lake, Wildcat Lake: icl_nhi_ops, no ICM row. */
    { 0x8086, 0xA833, "Lunar Lake NHI0", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0xA834, "Lunar Lake NHI1", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0xE333, "Panther Lake-M NHI0", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0xE334, "Panther Lake-M NHI1", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0xE433, "Panther Lake-P NHI0", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0xE434, "Panther Lake-P NHI1", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0x4D33, "Wildcat Lake NHI0", TBT_HW_ICL, TBT_HW_OWN_NONE, NULL },
    /* Barlow Ridge hosts: no ICM row. */
    { 0x8086, 0x5781, "JHL9580 Barlow Ridge Host 80G", TBT_HW_INTEL,
      TBT_HW_OWN_NONE, NULL },
    { 0x8086, 0x5784, "JHL9540 Barlow Ridge Host 40G", TBT_HW_INTEL,
      TBT_HW_OWN_NONE, NULL },
    /* AMD Pink Sardine, FreeBSD nhi_reg.h. */
    { 0x1022, 0x1668, "Pink Sardine USB4 NHI #1", 0, TBT_HW_OWN_NONE, NULL },
    { 0x1022, 0x1669, "Pink Sardine USB4 NHI #2", 0, TBT_HW_OWN_NONE, NULL }
};

static const TBT_HW_PROFILE tbtHwForced = {
    0x8086, 0, "unlisted Intel NHI (XhciCmMode 2)",
    TBT_HW_INTEL | TBT_HW_FORCED, TBT_HW_OWN_ICM_RUNNING,
    &TbtIcmProfileAlpineRidge
};

static const TBT_HW_PROFILE tbtHwClass = {
    0, 0, "USB4 host interface (class 0C0340)",
    TBT_HW_CLASS_MATCH, TBT_HW_OWN_NONE, NULL
};

const TBT_HW_PROFILE *TbtHwMatch(ULONG vendorId,
                                 ULONG deviceId,
                                 ULONG classCode,
                                 BOOLEAN forceIntel)
{
    ULONG i;

    for (i = 0; i < sizeof(tbtHwTable) / sizeof(tbtHwTable[0]); i++) {
        if (tbtHwTable[i].VendorId == vendorId &&
            tbtHwTable[i].DeviceId == deviceId) {
            return &tbtHwTable[i];
        }
    }
    if ((classCode & 0xFFFFFFUL) == TBT_HW_CLASS_USB4) {
        return &tbtHwClass;
    }
    if (forceIntel && vendorId == TBT_HW_VENDOR_INTEL) {
        return &tbtHwForced;
    }
    return NULL;
}

ULONG TbtHwRingFlags(const TBT_HW_PROFILE *hw)
{
    if (hw != NULL && (hw->Flags & TBT_HW_INTEL_INTERRUPTS) != 0) {
        return 0;
    }
    return TBT_RING_START_MASK_IMC;
}

/* ---------------------------------------------------------------------- */
/* Platform                                                               */
/* ---------------------------------------------------------------------- */

static VOID tbtHwLog(const TBT_HOST *host, const char *label, ULONG value)
{
    if (host != NULL && host->Log != NULL) {
        host->Log(host->Context, label, value);
    }
}

ULONG TbtHwForcePowerValue(ULONG vsCap22, BOOLEAN on)
{
    if (!on) {
        return vsCap22 & ~TBT_ICL_VS_CAP_22_FORCE_POWER;
    }
    return (vsCap22 & ~TBT_ICL_VS_CAP_22_DMA_DELAY_MASK) |
           (TBT_ICL_DMA_DELAY << TBT_ICL_VS_CAP_22_DMA_DELAY_SHIFT) |
           TBT_ICL_VS_CAP_22_FORCE_POWER;
}

LONG TbtHwPlatformInit(const TBT_HW_PROFILE *hw,
                       const TBT_CFG *cfg,
                       const TBT_HOST *host,
                       PTBT_HW_POWER power)
{
    ULONG v;
    ULONG i;

    if (hw == NULL || (hw->Flags & TBT_HW_ICL_POWER) == 0) {
        return TBT_OK;
    }
    v = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_22);
    if (v == 0xFFFFFFFFUL) {
        tbtHwLog(host, "tbt.hw.cfg-unreadable", TBT_ICL_VS_CAP_22);
        return TBT_E_HW;
    }
    if ((v & TBT_ICL_VS_CAP_22_FORCE_POWER) == 0) {
        power->ForcePowerSetHere = TRUE;
    }
    power->FwReadyFailed = FALSE;
    cfg->Write32(cfg->Context, TBT_ICL_VS_CAP_22, TbtHwForcePowerValue(v, TRUE));

    for (i = 0; i < TBT_ICL_FW_READY_POLLS; i++) {
        v = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_9);
        if (v != 0xFFFFFFFFUL && (v & TBT_ICL_VS_CAP_9_FW_READY) != 0) {
            break;
        }
        host->Sleep(host->Context, TBT_ICL_FW_READY_POLL_MS);
    }
    if (i == TBT_ICL_FW_READY_POLLS) {
        power->FwReadyFailed = TRUE;
        tbtHwLog(host, "tbt.hw.fw-not-ready", v);
        return TBT_E_TIMEOUT;
    }

    /* Linux pci.c icl_nhi_set_ltr: the same maximum for snoop and no-snoop. */
    v = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_16);
    if (v == 0xFFFFFFFFUL) {
        tbtHwLog(host, "tbt.hw.cfg-unreadable", TBT_ICL_VS_CAP_16);
        return TBT_E_HW;
    }
    v &= 0xFFFFUL;
    cfg->Write32(cfg->Context, TBT_ICL_VS_CAP_15, (v << 16) | v);
    return TBT_OK;
}

LONG TbtHwLcMailbox(const TBT_CFG *cfg, const TBT_HOST *host, ULONG cmd,
                    ULONG timeoutMs)
{
    ULONG v;
    ULONG waited;

    cfg->Write32(cfg->Context, TBT_ICL_VS_CAP_19,
                 ((cmd << TBT_ICL_VS_CAP_19_CMD_SHIFT) & TBT_ICL_VS_CAP_19_CMD_MASK) |
                 TBT_ICL_VS_CAP_19_VALID);
    for (waited = 0;; waited++) {
        v = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_18);
        if (v != 0xFFFFFFFFUL && (v & TBT_ICL_VS_CAP_18_DONE) != 0) {
            break;
        }
        if (waited >= timeoutMs) {
            tbtHwLog(host, "tbt.hw.lc-timeout", cmd);
            return TBT_E_TIMEOUT;
        }
        host->Sleep(host->Context, 1);
    }
    cfg->Write32(cfg->Context, TBT_ICL_VS_CAP_19, 0);
    return TBT_OK;
}

LONG TbtHwPlatformSuspend(const TBT_HW_PROFILE *hw,
                          const TBT_CFG *cfg,
                          const TBT_HOST *host,
                          BOOLEAN icmOwned,
                          BOOLEAN wake)
{
    if (hw == NULL || (hw->Flags & TBT_HW_ICL_POWER) == 0 || !icmOwned) {
        return TBT_OK;
    }
    return TbtHwLcMailbox(cfg, host,
                          wake ? TBT_ICL_LC_GO2SX : TBT_ICL_LC_GO2SX_NO_WAKE,
                          TBT_ICL_LC_TIMEOUT_MS);
}

LONG TbtHwPlatformShutdown(const TBT_HW_PROFILE *hw, const TBT_CFG *cfg,
                           PTBT_HW_POWER power, BOOLEAN mayClear)
{
    ULONG v;

    if (hw == NULL || (hw->Flags & TBT_HW_ICL_POWER) == 0 ||
        !power->ForcePowerSetHere) {
        return TBT_OK;
    }
    /* Record 22 section 4.2, settled over record 21 section 1.4.2 on
     * 2026-10-15: an inert instance leaves it set, even after its own
     * FW_READY poll failed; so does an active one with a router up. */
    if (!mayClear) {
        return TBT_OK;
    }
    v = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_22);
    if (v == 0xFFFFFFFFUL) {
        /* Not restored: the record keeps it for a later attempt. */
        return TBT_E_HW;
    }
    cfg->Write32(cfg->Context, TBT_ICL_VS_CAP_22, TbtHwForcePowerValue(v, FALSE));
    power->ForcePowerSetHere = FALSE;
    return TBT_OK;
}

BOOLEAN TbtHwIsApple(ULONG subsystemVendorId)
{
    return (BOOLEAN)(subsystemVendorId == TBT_HW_VENDOR_APPLE);
}

BOOLEAN TbtHwReadHostUuid(const TBT_HW_PROFILE *hw,
                          const TBT_CFG *cfg,
                          PULONG uuid)
{
    if (hw == NULL || (hw->Flags & TBT_HW_ICL_POWER) == 0) {
        return FALSE;
    }
    uuid[0] = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_10);
    uuid[1] = cfg->Read32(cfg->Context, TBT_ICL_VS_CAP_11);
    uuid[2] = 0xFFFFFFFFUL;
    uuid[3] = 0xFFFFFFFFUL;
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Selection                                                              */
/* ---------------------------------------------------------------------- */

BOOLEAN TbtHwSoftwareNhiVersion(ULONG caps)
{
    return (BOOLEAN)(TBT_HI_CAPS_VERSION(caps) == TBT_HI_VERSION_TBT3 ||
                     TBT_HI_CAPS_VERSION(caps) == TBT_HI_VERSION_2);
}

ULONG TbtHwRouterVersion(ULONG usb4Version)
{
    ULONG major;

    if ((usb4Version & ~0xFFUL) != 0) {
        return TBT_HW_ROUTER_RESERVED;
    }
    major = TBT_USB4_VERSION_MAJOR(usb4Version);
    if (major == TBT_USB4_MAJOR_1 || major == TBT_USB4_MAJOR_2) {
        return TBT_HW_ROUTER_USB4;
    }
    if (major == TBT_USB4_MAJOR_TBT3) {
        return TBT_HW_ROUTER_TBT3;
    }
    return TBT_HW_ROUTER_RESERVED;
}

/* The software admission, with test (d)'s interface version. */
static VOID tbtHwSoftware(PTBT_HW_SELECTION out, ULONG route,
                          BOOLEAN forced, const TBT_HOST *host)
{
    if (!TbtHwSoftwareNhiVersion(out->Caps)) {
        out->Outcome = TBT_SEL_INERT;
        out->Reason = TBT_INERT_NHI_VERSION;
        tbtHwLog(host, "tbt.hw.inert", TBT_INERT_NHI_VERSION);
        return;
    }
    out->Outcome = TBT_SEL_SOFTWARE;
    out->Route = route;
    out->Forced = forced;
    if (route == TBT_SW_ROUTE_ICM_DISABLED &&
        (out->FwStatus & TBT_ICM_FW_NVM_AUTH_DONE) != 0) {
        out->Experimental = TRUE;
        tbtHwLog(host, "tbt.hw.software-cm-experimental", out->FwStatus);
    }
    tbtHwLog(host, "tbt.hw.software-cm", route);
}

static VOID tbtHwInert(PTBT_HW_SELECTION out, ULONG reason,
                       const TBT_HOST *host)
{
    out->Outcome = TBT_SEL_INERT;
    out->Reason = reason;
    tbtHwLog(host, "tbt.hw.inert", reason);
}

static ULONG tbtHwRead(const TBT_HOST *host, ULONG off)
{
    return host->Read32(host->Context, off);
}

VOID TbtHwSelect(const TBT_HW_PROFILE *hw,
                 const TBT_HOST *host,
                 BOOLEAN platformReady,
                 BOOLEAN apple,
                 BOOLEAN forceSoftware,
                 PTBT_HW_SELECTION out)
{
    ULONG fw;
    ULONG m;
    ULONG i;
    TBT_DEADLINE d;

    out->Outcome = TBT_SEL_INERT;
    out->Reason = TBT_INERT_NONE;
    out->Caps = 0;
    out->FwStatus = 0;
    out->Mode = 0xFFFFFFFFUL;
    out->Route = TBT_SW_ROUTE_NONE;
    out->Experimental = FALSE;
    out->Forced = FALSE;

    if (hw == NULL || !platformReady) {
        tbtHwInert(out, TBT_INERT_PLATFORM, host);
        return;
    }
    if (apple) {
        tbtHwInert(out, TBT_INERT_APPLE, host);
        return;
    }
    out->Caps = tbtHwRead(host, TBT_HI_CAPS);
    tbtHwLog(host, "tbt.hw.caps", out->Caps);
    if (out->Caps == 0xFFFFFFFFUL || TBT_HI_CAPS_PATHS(out->Caps) == 0 ||
        TBT_HI_CAPS_PATHS(out->Caps) > TBT_HI_CAPS_PATHS_MAX) {
        tbtHwInert(out, TBT_INERT_NOT_DECODING, host);
        return;
    }

    /* No firmware CM by identity (decision B'1): admitted once (b), the
     * register checks and test (d)'s interface version passed. The Intel
     * firmware status is not read; XhciCmMode 3 changes nothing here. */
    if (hw->Icm == NULL || hw->Ownership == TBT_HW_OWN_NONE) {
        tbtHwSoftware(out, TBT_SW_ROUTE_IDENTITY, forceSoftware, host);
        return;
    }

    /* Falcon, Alpine and Titan Ridge and Ice Lake are not USB4 hosts:
     * XhciCmMode 3 is refused on them (decision B'3). */
    if (forceSoftware && hw->Ownership != TBT_HW_OWN_AUTH_DONE) {
        tbtHwInert(out, TBT_INERT_FORCED_UNSUPPORTED, host);
        return;
    }

    fw = tbtHwRead(host, TBT_ICM_FW_STATUS);
    if (fw == 0xFFFFFFFFUL) {
        out->FwStatus = fw;
        tbtHwInert(out, TBT_INERT_NOT_DECODING, host);
        return;
    }
    if ((fw & TBT_ICM_FW_ICM_ENABLED) == 0) {
        out->FwStatus = fw;
        tbtHwLog(host, "tbt.hw.fw-status", fw);
        /* A valid read of a disabled ICM on a Tiger Lake class or Maple
         * Ridge host admits the software CM (decision B'2, best effort;
         * NVM authentication gates only the ICM). Every other row needs its
         * ICM running: Linux icm.c icm_firmware_start returns at once only
         * when icm_firmware_running says so, and its other path restarts
         * the firmware, which this driver never does. */
        if (hw->Ownership == TBT_HW_OWN_AUTH_DONE) {
            tbtHwSoftware(out, TBT_SW_ROUTE_ICM_DISABLED, forceSoftware, host);
            return;
        }
        tbtHwInert(out, TBT_INERT_ICM_NOT_RUNNING, host);
        return;
    }
    if (forceSoftware) {
        out->FwStatus = fw;
        tbtHwInert(out, TBT_INERT_FORCED_ICM_EN, host);
        return;
    }

    /* The ICM candidate predicate of a Tiger Lake class row: NVM
     * authentication done within 10 ms (icm_tgl_is_supported). */
    if (hw->Ownership == TBT_HW_OWN_AUTH_DONE) {
        TbtDeadlineStart(&d, host, TBT_ICM_AUTH_CONFIRM_MS);
        while (fw != 0xFFFFFFFFUL && (fw & TBT_ICM_FW_NVM_AUTH_DONE) == 0 &&
               !TbtDeadlinePassed(&d, host)) {
            TbtDeadlineWait(&d, host);
            fw = tbtHwRead(host, TBT_ICM_FW_STATUS);
        }
        out->FwStatus = fw;
        if (fw == 0xFFFFFFFFUL || (fw & TBT_ICM_FW_NVM_AUTH_DONE) == 0) {
            tbtHwInert(out, TBT_INERT_NOT_AUTHENTICATED, host);
            return;
        }
        if ((fw & TBT_ICM_FW_ICM_ENABLED) == 0) {
            /* Enabled, then not: conflicting evidence (decision B'). */
            tbtHwInert(out, TBT_INERT_CONFLICT, host);
            return;
        }
    }
    out->FwStatus = fw;
    tbtHwLog(host, "tbt.hw.fw-status", fw);

    /* (d) The interface version (record 21 section 1.4.2 test 4): 0 on
     * Alpine Ridge [obs], below 0x40 on the other rows - this project's
     * policy, 0x40 being where Linux nhi.c nhi_reset starts resetting. */
    if (hw->Icm == &TbtIcmProfileAlpineRidge ?
            TBT_HI_CAPS_VERSION(out->Caps) != TBT_HI_VERSION_TBT3 :
            TBT_HI_CAPS_VERSION(out->Caps) >= TBT_HI_VERSION_2) {
        tbtHwInert(out, TBT_INERT_NHI_VERSION, host);
        return;
    }

    if ((hw->Icm->Caps & TBT_ICM_CAP_GET_MODE) == 0) {
        out->Outcome = TBT_SEL_ICM;
        return;
    }
    /* Linux icm.c icm_ar_get_mode, for Alpine Ridge, Titan Ridge and
     * Maple Ridge: NVM authentication (60 reads 50 ms apart), then the
     * mode; icm_firmware_init takes CM mode and safe mode only. */
    for (i = 0; i < TBT_ICM_AUTH_POLLS; i++) {
        if (fw != 0xFFFFFFFFUL && (fw & TBT_ICM_FW_NVM_AUTH_DONE) != 0) {
            break;
        }
        host->Sleep(host->Context, TBT_ICM_AUTH_POLL_MS);
        fw = tbtHwRead(host, TBT_ICM_FW_STATUS);
    }
    out->FwStatus = fw;
    if (fw == 0xFFFFFFFFUL || (fw & TBT_ICM_FW_NVM_AUTH_DONE) == 0) {
        tbtHwInert(out, TBT_INERT_NOT_AUTHENTICATED, host);
        return;
    }
    if ((fw & TBT_ICM_FW_ICM_ENABLED) == 0) {
        tbtHwInert(out, TBT_INERT_CONFLICT, host);
        return;
    }
    m = tbtHwRead(host, TBT_ICM_OUTMAIL);
    if (m == 0xFFFFFFFFUL) {
        tbtHwInert(out, TBT_INERT_NOT_DECODING, host);
        return;
    }
    out->Mode = TBT_ICM_OUTMAIL_MODE(m);
    tbtHwLog(host, "tbt.hw.mode", out->Mode);
    if (out->Mode == TBT_ICM_MODE_CM) {
        out->Outcome = TBT_SEL_ICM;
    } else if (out->Mode == TBT_ICM_MODE_SAFE) {
        out->Outcome = TBT_SEL_SAFE_MODE;
    } else {
        tbtHwInert(out, TBT_INERT_MODE, host);
    }
}
