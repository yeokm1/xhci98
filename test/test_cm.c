/*
 * test_cm.c - the pure half of xhcicm.sys's start (src/cm/cm_vals.c,
 * roadmap-usb4.md task 39-A.1): the role a PDO's hardware ids name, the
 * ids parsed out of them, the NHI profile table, and the registry values'
 * defaults, refusals and bounds (design record 21 sections 3.2 and 8).
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_vals.h"
#include "test_harness.h"

/* An ASCII multi-string ("a\0b\0\0") widened into `out`; returns WCHARs
 * including the final terminator. */
static ULONG widen(const char *s, ULONG bytes, WCHAR *out)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        out[i] = (WCHAR)(unsigned char)s[i];
    }
    return bytes;
}

static ULONG classify(const char *s, ULONG bytes, CM_IDS *ids)
{
    WCHAR w[256];
    ULONG n;

    n = widen(s, bytes, w);
    return CmClassifyIds(w, n, ids);
}

static void test_roles(void)
{
    static const char nhi[] =
        "PCI\\VEN_8086&DEV_15BF&SUBSYS_22D817AA&REV_02\0"
        "PCI\\VEN_8086&DEV_15BF&SUBSYS_22D817AA\0"
        "PCI\\VEN_8086&DEV_15BF&CC_088000\0"
        "PCI\\VEN_8086&DEV_15BF&CC_0880\0\0";
    static const char bridge[] =
        "PCI\\VEN_8086&DEV_15C0&SUBSYS_22D817AA&REV_02\0"
        "PCI\\VEN_8086&DEV_15C0&CC_060400\0"
        "PCI\\VEN_8086&DEV_15C0&CC_0604\0\0";
    static const char lower[] = "pci\\ven_8086&dev_15c0&cc_0604\0\0";
    static const char usb4[] =
        "PCI\\VEN_1022&DEV_1668&SUBSYS_00000000&REV_00\0"
        "PCI\\VEN_1022&DEV_1668&CC_0C0340\0\0";
    static const char subclass[] = "PCI\\VEN_8086&DEV_1234&CC_060900\0\0";
    CM_IDS ids;

    CHECK_EQ(classify(nhi, sizeof(nhi), &ids), CM_ROLE_NHI, "the P14s NHI is the NHI role");
    CHECK_EQ(ids.Vendor, 0x8086, "vendor from the first id");
    CHECK_EQ(ids.Device, 0x15BF, "device from the first id");
    CHECK_EQ(ids.ClassCode, 0x088000, "the six-digit class");
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_ICM_AR, "15BF is Alpine Ridge");

    CHECK_EQ(classify(bridge, sizeof(bridge), &ids), CM_ROLE_FILTER, "a 06/04 id is the filter role");
    CHECK_EQ(ids.Device, 0x15C0, "the bridge's device id");
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "a bridge has no NHI profile");
    CHECK_EQ(classify(lower, sizeof(lower), &ids), CM_ROLE_FILTER, "ids are matched without case");

    CHECK_EQ(classify(subclass, sizeof(subclass), &ids), CM_ROLE_NHI,
             "06/09 (a subtractive bridge) is not the tunnel bridge's class");

    CHECK_EQ(classify(usb4, sizeof(usb4), &ids), CM_ROLE_NHI, "an AMD USB4 NHI");
    CHECK_EQ(ids.ClassCode, 0x0C0340, "class 0C0340 from the second id");
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "an unlisted USB4 NHI is the software CM's");

    CHECK_EQ(CmClassifyIds(NULL, 0, &ids), CM_ROLE_NONE, "no list, no role");
    CHECK_EQ(classify("\0\0", 2, &ids), CM_ROLE_NONE, "an empty list, no role");
    CHECK_EQ(classify("PCI\\VEN_8086&DEV_15B\0\0", 22, &ids), CM_ROLE_NHI,
             "a short id is still the NHI role");
    CHECK_EQ(ids.Device, 0, "but names no device");
    /* No terminator inside the count: the parse stops at the count. */
    CHECK_EQ(classify("PCI\\VEN_8086&DEV_15C0&CC_0604", 29, &ids), CM_ROLE_FILTER,
             "an unterminated list ends at its count");
}

static void test_profiles(void)
{
    static const struct { ULONG dev; ULONG profile; } rows[] = {
        { 0x156A, CM_PROFILE_ICM_FR },  { 0x156C, CM_PROFILE_ICM_FR },
        { 0x1575, CM_PROFILE_ICM_AR },  { 0x1577, CM_PROFILE_ICM_AR },
        { 0x15BF, CM_PROFILE_ICM_AR },  { 0x15D2, CM_PROFILE_ICM_AR },
        { 0x15D9, CM_PROFILE_ICM_AR },  { 0x15E8, CM_PROFILE_ICM_TR },
        { 0x15EB, CM_PROFILE_ICM_TR },  { 0x1134, CM_PROFILE_ICM_MR },
        { 0x1137, CM_PROFILE_ICM_MR },  { 0x8A0D, CM_PROFILE_ICM_ICL },
        { 0x8A17, CM_PROFILE_ICM_ICL }, { 0x9A1B, CM_PROFILE_ICM_TGL },
        { 0x9A1D, CM_PROFILE_ICM_TGL }, { 0x9A1F, CM_PROFILE_ICM_TGL },
        { 0x9A21, CM_PROFILE_ICM_TGL }, { 0x463E, CM_PROFILE_ICM_TGL },
        { 0x466D, CM_PROFILE_ICM_TGL }, { 0xA73E, CM_PROFILE_ICM_TGL },
        { 0xA76D, CM_PROFILE_ICM_TGL }, { 0x7EB2, CM_PROFILE_ICM_TGL },
        { 0x7EC2, CM_PROFILE_ICM_TGL }, { 0x7EC3, CM_PROFILE_ICM_TGL },
        { 0xA833, CM_PROFILE_USB4 },    { 0xA834, CM_PROFILE_USB4 },
        { 0xE333, CM_PROFILE_USB4 },    { 0xE334, CM_PROFILE_USB4 },
        { 0xE433, CM_PROFILE_USB4 },    { 0xE434, CM_PROFILE_USB4 },
        { 0x4D33, CM_PROFILE_USB4 },    { 0x5781, CM_PROFILE_USB4 },
        { 0x5784, CM_PROFILE_USB4 },
        /* USB-only Alpine Ridge NHIs and Win Ridge: not claimed. */
        { 0x15DC, CM_PROFILE_UNKNOWN }, { 0x15DD, CM_PROFILE_UNKNOWN },
        { 0x15DE, CM_PROFILE_UNKNOWN }, { 0x157D, CM_PROFILE_UNKNOWN }
    };
    CM_IDS ids;
    ULONG i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        ids.Role = CM_ROLE_NHI;
        ids.Vendor = 0x8086;
        ids.Device = rows[i].dev;
        ids.ClassCode = 0x088000;
        CHECK_EQ(CmProfileFor(&ids), rows[i].profile, "Intel NHI profile row");
    }
    ids.Role = CM_ROLE_NHI;
    ids.Vendor = 0x1B21;
    ids.Device = 0x15BF;
    ids.ClassCode = 0x088000;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "the device id is Intel's only");
    ids.ClassCode = CM_CLASS_USB4_NHI;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "any vendor's USB4 NHI");
    ids.Vendor = 0x1022;
    ids.ClassCode = 0x088000;
    ids.Device = 0x1668;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "AMD Pink Sardine 1668");
    ids.Device = 0x1669;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "AMD Pink Sardine 1669");
    ids.Device = 0x15BF;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "an Intel id under AMD's vendor id");
    ids.Vendor = 0x8086;
    ids.Device = 0x15BF;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_ICM_AR, "a listed id outranks the class");
    ids.Role = CM_ROLE_NONE;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "no role, no profile");
    ids.Role = CM_ROLE_NHI;
    ids.Vendor = 0;
    ids.Device = 0;
    ids.ClassCode = 0;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "ids unread (Windows 98)");
}

static void test_values(void)
{
    CM_VALUES v;
    static const WCHAR bdf[] = { '0', '4', ':', '0', '0', '.', '0', 0 };
    WCHAR longer[CM_TUNNEL_BRIDGE_CHARS + 1];
    ULONG i;

    CmValuesDefault(&v);
    CHECK_EQ(v.Value[CM_VAL_MODE], 1, "XhciCmMode defaults to 1 (D11)");
    CHECK_EQ(v.Value[CM_VAL_APPROVE], 1, "XhciCmApprove defaults to 1 (D4)");
    CHECK_EQ(v.Value[CM_VAL_REENUM], 1, "XhciCmReenum defaults to 1");
    CHECK_EQ(v.Value[CM_VAL_POLL_IDLE_MS], 200, "XhciCmPollIdleMs defaults to 200");
    CHECK_EQ(v.Value[CM_VAL_CMD_TIMEOUT_MS], 5000, "XhciCmCmdTimeoutMs defaults to 5000");
    CHECK_EQ(v.Value[CM_VAL_APPROVE_TIMEOUT_MS], 10000, "XhciCmApproveTimeoutMs defaults to 10000");
    CHECK_EQ(v.Value[CM_VAL_SAFE_EJECT], 1, "XhciCmSafeEject defaults to 1 (D10)");
    CHECK_EQ(v.Value[CM_VAL_LOG_VERBOSITY], 2, "XhciCmLogVerbosity defaults to 2");
    CHECK_EQ(v.Value[CM_VAL_LOG_DEBUGVIEW], 0, "XhciCmLogDebugView defaults to 0");
    CHECK_EQ(v.Value[CM_VAL_USB3_TUNNEL], 1, "XhciCmUsb3Tunnel defaults to 1 (U3)");
    CHECK_EQ(v.Value[CM_VAL_LANE_BONDING], 1, "XhciCmLaneBonding defaults to 1 (U4)");
    CHECK_EQ(v.Value[CM_VAL_PREOS_CLEANUP], 0, "XhciCmPreOsCleanup defaults to 0 (U2)");
    CHECK_EQ(v.Found | v.Refused | v.Clamped, 0, "nothing found yet");
    CHECK_EQ(v.TunnelBridgeChars, 0, "no bridge override");
    for (i = 0; i < CM_VAL_COUNT; i++) {
        CHECK_EQ(CmValueDefault(i), v.Value[i], "CmValueDefault matches the defaults");
    }
    CHECK_EQ(CmValueDefault(CM_VAL_COUNT), 0, "an index out of range");

    CmValuesTake(&v, CM_VAL_MODE, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_MODE], 0, "mode 0 is taken");
    CmValuesTake(&v, CM_VAL_MODE, 1, 3);
    CHECK_EQ(v.Value[CM_VAL_MODE], 3, "mode 3 is taken (force the software CM)");
    CmValuesTake(&v, CM_VAL_MODE, 1, 4);
    CHECK_EQ(v.Value[CM_VAL_MODE], 1, "mode 4 is refused: the default stands");
    CHECK(v.Refused & (1UL << CM_VAL_MODE), "and the refusal is recorded");
    CHECK_EQ(v.Raw[CM_VAL_MODE], 4, "with what was read");
    CmValuesTake(&v, CM_VAL_APPROVE, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_APPROVE], 1, "approve 2 is refused");
    CmValuesTake(&v, CM_VAL_APPROVE, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_APPROVE], 0, "approve 0 is taken");
    CmValuesTake(&v, CM_VAL_APPROVE, 0, 0);
    CHECK_EQ(v.Value[CM_VAL_APPROVE], 1, "absent is the default");

    CmValuesTake(&v, CM_VAL_SAFE_EJECT, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_SAFE_EJECT], 0, "safe eject 0 is taken");
    CmValuesTake(&v, CM_VAL_SAFE_EJECT, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_SAFE_EJECT], 1, "safe eject 2 is refused");
    CmValuesTake(&v, CM_VAL_USB3_TUNNEL, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_USB3_TUNNEL], 0, "USB3 tunnel 0 is taken");
    CmValuesTake(&v, CM_VAL_USB3_TUNNEL, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_USB3_TUNNEL], 1, "USB3 tunnel 2 is refused");
    CmValuesTake(&v, CM_VAL_LANE_BONDING, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_LANE_BONDING], 0, "lane bonding 0 is taken");
    CmValuesTake(&v, CM_VAL_LANE_BONDING, 1, 7);
    CHECK_EQ(v.Value[CM_VAL_LANE_BONDING], 1, "lane bonding 7 is refused");
    CmValuesTake(&v, CM_VAL_PREOS_CLEANUP, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_PREOS_CLEANUP], 2, "pre-OS cleanup 2 is taken");
    CmValuesTake(&v, CM_VAL_PREOS_CLEANUP, 1, 3);
    CHECK_EQ(v.Value[CM_VAL_PREOS_CLEANUP], 0, "pre-OS cleanup 3 is refused");
    CHECK(v.Refused & (1UL << CM_VAL_PREOS_CLEANUP), "and the refusal is recorded");

    CmValuesTake(&v, CM_VAL_POLL_IDLE_MS, 1, 10);
    CHECK_EQ(v.Value[CM_VAL_POLL_IDLE_MS], 50, "the idle poll is held to 50 ms");
    CHECK(v.Clamped & (1UL << CM_VAL_POLL_IDLE_MS), "and the hold is recorded");
    CmValuesTake(&v, CM_VAL_POLL_IDLE_MS, 1, 100000);
    CHECK_EQ(v.Value[CM_VAL_POLL_IDLE_MS], 5000, "and to 5000 ms");
    CmValuesTake(&v, CM_VAL_CMD_TIMEOUT_MS, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_CMD_TIMEOUT_MS], 1000, "the command timeout floor");
    CmValuesTake(&v, CM_VAL_APPROVE_TIMEOUT_MS, 1, 0xFFFFFFFF);
    CHECK_EQ(v.Value[CM_VAL_APPROVE_TIMEOUT_MS], 60000, "the approve timeout ceiling");
    CmValuesTake(&v, CM_VAL_LOG_VERBOSITY, 1, 9);
    CHECK_EQ(v.Value[CM_VAL_LOG_VERBOSITY], 9, "the verbosity passes to the log's own refusal");
    CmValuesTake(&v, CM_VAL_COUNT, 1, 5);
    CHECK(1, "an index out of range is ignored");

    /* XhciCmLogDebugView: 0 and 1, anything else refused. */
    CmValuesTake(&v, CM_VAL_LOG_DEBUGVIEW, 1, 1);
    CHECK_EQ(v.Value[CM_VAL_LOG_DEBUGVIEW], 1, "DebugView 1 is taken");
    CmValuesTake(&v, CM_VAL_LOG_DEBUGVIEW, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_LOG_DEBUGVIEW], 0, "DebugView 2 is refused");
    CHECK(v.Refused & (1UL << CM_VAL_LOG_DEBUGVIEW), "and the refusal is recorded");
    CmValuesTake(&v, CM_VAL_LOG_DEBUGVIEW, 0, 0);
    CHECK_EQ(v.Value[CM_VAL_LOG_DEBUGVIEW], 0, "absent is 0");

    CmValuesDefault(&v);

    CmValuesTakeBridge(&v, bdf, 7);
    CHECK_EQ(v.TunnelBridgeChars, 7, "bb:dd.f taken");
    CHECK(v.TunnelBridge[0] == '0' && v.TunnelBridge[7] == 0, "and terminated");
    CmValuesTakeBridge(&v, bdf, 8);
    CHECK_EQ(v.TunnelBridgeChars, 7, "a counted terminator is dropped");
    CmValuesTakeBridge(&v, bdf, 0);
    CHECK_EQ(v.TunnelBridgeChars, 0, "empty is no override");
    for (i = 0; i <= CM_TUNNEL_BRIDGE_CHARS; i++) {
        longer[i] = 'x';
    }
    CmValuesTakeBridge(&v, longer, CM_TUNNEL_BRIDGE_CHARS);
    CHECK_EQ(v.TunnelBridgeChars, 0, "too long is no override");
    CHECK_EQ(v.TunnelBridgeRefused, 1, "and is recorded as refused");
    CmValuesTakeBridge(&v, longer, CM_TUNNEL_BRIDGE_CHARS - 1);
    CHECK_EQ(v.TunnelBridgeChars, CM_TUNNEL_BRIDGE_CHARS - 1, "the longest taken");
    CHECK_EQ(v.TunnelBridgeRefused, 0, "and the refusal cleared");

    /* XhciCmBridgeNumbering (record 21 section 6.1.1): not in the rule
     * table, since no INF writes it; default on, 0 and 1 taken, anything
     * else refused with the default standing. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_BRIDGE_NUMBERING_DEFAULT, 1, "XhciCmBridgeNumbering defaults to 1 (owner, 2026-10-16)");
    CHECK_EQ(v.BridgeNumbering, CM_BRIDGE_NUMBERING_DEFAULT, "the default after CmValuesDefault");
    CHECK_EQ(v.BridgeNumberingFound | v.BridgeNumberingRefused, 0, "nothing found or refused");
    CmValuesTakeBridgeNumbering(&v, 0, 0);
    CHECK_EQ(v.BridgeNumbering, CM_BRIDGE_NUMBERING_DEFAULT, "absent: the default");
    CHECK_EQ(v.BridgeNumberingFound, 0, "absent is not found");
    CmValuesTakeBridgeNumbering(&v, 1, 0);
    CHECK_EQ(v.BridgeNumbering, 0, "0 is taken: off");
    CHECK_EQ(v.BridgeNumberingFound, 1, "and found");
    CHECK_EQ(v.BridgeNumberingRefused, 0, "not refused");
    CmValuesTakeBridgeNumbering(&v, 1, 1);
    CHECK_EQ(v.BridgeNumbering, 1, "1 is taken: on");
    CmValuesTakeBridgeNumbering(&v, 1, 2);
    CHECK_EQ(v.BridgeNumbering, CM_BRIDGE_NUMBERING_DEFAULT, "2 is refused: the default stands");
    CHECK_EQ(v.BridgeNumberingRefused, 1, "and the refusal is recorded");
    CHECK_EQ(v.BridgeNumberingRaw, 2, "with what was read");
    CmValuesTakeBridgeNumbering(&v, 1, 0xFFFFFFFFUL);
    CHECK_EQ(v.BridgeNumbering, CM_BRIDGE_NUMBERING_DEFAULT, "all-ones is refused");
    CmValuesTakeBridgeNumbering(&v, 0, 0);
    CHECK_EQ(v.BridgeNumberingRefused | v.BridgeNumberingFound, 0, "a later absent read clears both");
    CHECK_EQ(v.Found | v.Refused, 0, "the rule table's bits untouched");
}

static void test_decision(void)
{
    ULONG ar;
    ULONG all;

    ar = 1UL << CM_PROFILE_ICM_AR;
    all = 0xFFFFFFFFUL;
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_AR, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "task 39-A.4 builds the Alpine Ridge client");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_FR, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "and Falcon Ridge's");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_TR, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "and Titan Ridge's");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_ICL, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "and Ice Lake's");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_USB4, 0, CM_PROFILES_BUILT),
             CM_INERT_NONE, "and the software CM's (task 40-A.6)");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_MR, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "and Maple Ridge's, on Titan Ridge's messages");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_TGL, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "and Tiger Lake's, on Ice Lake's");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_TGL, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "so mode 3 reaches the selection in this build");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_AR, 1, CM_PROFILES_BUILT),
             CM_INERT_FORCE_REFUSED, "and is still refused on Alpine Ridge");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_USB4, 0, CM_PROFILES_BUILT),
             CM_INERT_FORCE_ICM_REFUSED, "mode 2 on a USB4-only host: refused, never the software CM");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_USB4, 1, CM_PROFILES_BUILT),
             CM_INERT_FORCE_ICM_REFUSED, "mode 2 on an Intel USB4-only host: refused too");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_ICM_TGL, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "mode 2 on a Tiger Lake class host reaches the selection");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_UNKNOWN, 1, CM_PROFILES_BUILT),
             CM_INERT_NONE, "mode 2 on an unlisted Intel id runs AR");
    CHECK_EQ(CmStartDecision(0, CM_PROFILE_ICM_AR, 1, all), CM_INERT_MODE_OFF,
             "mode 0 is inert whatever is built");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_TGL, 1, ar), CM_INERT_NOT_BUILT,
             "mode 3 forces the software CM, not built yet: inert");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_TGL, 1, 1UL << CM_PROFILE_USB4),
             CM_INERT_NONE, "mode 3 on Tiger Lake to Meteor Lake once it is built");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_MR, 1, 1UL << CM_PROFILE_USB4),
             CM_INERT_NONE, "and on Maple Ridge");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_ICL, 1, all), CM_INERT_FORCE_REFUSED,
             "but never on Ice Lake, a generation 3 host router");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_USB4, 0, 1UL << CM_PROFILE_USB4),
             CM_INERT_NONE, "and on a USB4 host");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_AR, 1, all), CM_INERT_FORCE_REFUSED,
             "but never on Alpine Ridge");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_FR, 1, all), CM_INERT_FORCE_REFUSED,
             "or Falcon Ridge");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_ICM_TR, 1, all), CM_INERT_FORCE_REFUSED,
             "or Titan Ridge");
    CHECK_EQ(CmStartDecision(3, CM_PROFILE_UNKNOWN, 0, all), CM_INERT_PROFILE_UNKNOWN,
             "or a host whose ids were not read");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_UNKNOWN, 1, all), CM_INERT_PROFILE_UNKNOWN,
             "an unlisted id under auto is inert");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_UNKNOWN, 1, ar), CM_INERT_NONE,
             "mode 2 takes an unlisted Intel NHI as Alpine Ridge");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_UNKNOWN, 0, ar), CM_INERT_PROFILE_UNKNOWN,
             "but not another vendor's");
    CHECK_EQ(CmStartDecision(2, CM_PROFILE_ICM_TR, 1, ar), CM_INERT_NOT_BUILT,
             "and does not override a listed profile");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_ICM_AR, 1, ar), CM_INERT_NONE,
             "a built profile under auto runs");
    CHECK_EQ(CmStartDecision(1, CM_PROFILE_USB4, 0, ar), CM_INERT_NOT_BUILT,
             "the USB4 profile is inert until Phase 40");
    CHECK_EQ(CmStartDecision(1, 40, 1, all), CM_INERT_NOT_BUILT,
             "a profile past the mask is never built");
}

int main(void)
{
    test_roles();
    test_profiles();
    test_decision();
    test_values();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
