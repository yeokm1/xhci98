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

/* AddDevice's role since task 42-A.5 (record 21 section 7.4, revision 3,
 * decision M15): the NHI table and the USB4 class; a filter INF's bridge;
 * every other PDO the LowerFilters child. */
static void test_role_decide(void)
{
    static const char nhi[] = "PCI\\VEN_8086&DEV_15BF&CC_088000\0\0";
    static const char unlisted[] = "PCI\\VEN_8086&DEV_15DC&CC_088000\0\0";
    static const char usb4[] = "PCI\\VEN_1022&DEV_1668&CC_0C0340\0\0";
    static const char bridge[] = "PCI\\VEN_8086&DEV_15C0&CC_060400\0\0";
    static const char usp[] = "PCI\\VEN_1B21&DEV_2421&CC_060400\0\0";
    static const char nvme[] = "PCI\\VEN_1B36&DEV_0010&CC_010802\0\0";
    static const char amdOther[] = "PCI\\VEN_1022&DEV_1234&CC_088000\0\0";
    CM_IDS ids;

    classify(nhi, sizeof(nhi), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_NHI, "a listed NHI");
    classify(unlisted, sizeof(unlisted), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_CHILD,
             "an unlisted Intel 0880 function is no NHI: xhcicm.inf's contract and nothing wider");
    classify(usb4, sizeof(usb4), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_NHI, "the USB4 host class");
    classify(bridge, sizeof(bridge), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 1), CM_ROLE_FILTER, "a filter INF bridge");
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_CHILD,
             "a bridge the filter INF does not name is a child (a dock's upstream port)");
    classify(usp, sizeof(usp), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_CHILD, "the emulator's upstream port");
    classify(nvme, sizeof(nvme), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_CHILD, "an NVMe controller is a child");
    CHECK_EQ(CmRoleDecide(&ids, 1), CM_ROLE_CHILD,
             "a listed id that is not a bridge is no filter");
    classify(amdOther, sizeof(amdOther), &ids);
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_CHILD, "a non-Intel 0880 function is a child");
    CHECK_EQ(CmClassifyIds(NULL, 0, &ids), CM_ROLE_NONE, "no list");
    CHECK_EQ(CmRoleDecide(&ids, 0), CM_ROLE_CHILD,
             "an unreadable list is a child: a lower filter's AddDevice never fails");

    /* The role by position (design record 33 section 4.4, task 45.12). */
    classify(bridge, sizeof(bridge), &ids);
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 1), 0, 0), CM_ROLE_FILTER,
             "NT 6.x: a listed bridge's lower call stays the bridge filter (M15)");
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 1), 1, 0), CM_ROLE_FILTER,
             "NT 6.x: the upper call the same");
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 1), 1, 1), CM_ROLE_FILTER,
             "an INF-listed bridge with the PCI driver's FDO above: the bridge "
             "filter (its UpperFilters)");
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 1), 0, 1), CM_ROLE_CHILD,
             "an INF-listed bridge with nothing of the PCI driver's above: the "
             "child (its LowerFilters bridge mark)");
    classify(nhi, sizeof(nhi), &ids);
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 1), 0, 1), CM_ROLE_NHI,
             "the NHI with nothing above its PDO stays the NHI");
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 1), 1, 1), CM_ROLE_NHI,
             "the NHI whatever is above it");
    classify(usp, sizeof(usp), &ids);
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 0), 1, 1), CM_ROLE_CHILD,
             "an unlisted bridge is a child in either order");
    CHECK_EQ(CmRolePlace(CmRoleDecide(&ids, 0), 0, 1), CM_ROLE_CHILD,
             "an unlisted bridge, below the FDO");
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
    {
        /* Task 46.23: AMD's other integrated hosts, by id whatever the
         * class code reads. */
        static const ULONG amd[] = { 0x162E, 0x162F, 0x15C4, 0x15C5, 0x151C,
                                     0x151D, 0x158D, 0x158E, 0x1120, 0x1121,
                                     0x113B, 0x113C };
        ULONG savedClass = ids.ClassCode;
        ULONG a;
        ids.ClassCode = 0;
        for (a = 0; a < sizeof(amd) / sizeof(amd[0]); a++) {
            ids.Device = amd[a];
            CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "an AMD host by id");
        }
        ids.Device = 0x1125;
        CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN,
                 "Krackan's bridge id is no NHI's");
        ids.ClassCode = savedClass;
    }
    ids.Device = 0x15BF;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "an Intel id under AMD's vendor id");
    /* Task 45.46: the ASM4242's row, the profile its class gives it. */
    ids.Vendor = 0x1B21;
    ids.Device = 0x2425;
    ids.ClassCode = CM_CLASS_USB4_NHI;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "ASMedia ASM4242 2425");
    ids.ClassCode = 0x088000;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_USB4, "ASMedia ASM4242 2425 by its row");
    ids.Device = 0x2423;
    CHECK_EQ(CmProfileFor(&ids), CM_PROFILE_UNKNOWN, "the ASM4242's port is no NHI");
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
    CM_VALUES before;
    static const WCHAR bdf[] = { '0', '4', ':', '0', '0', '.', '0', 0 };
    WCHAR longer[CM_TUNNEL_BRIDGE_CHARS + 1];
    static const WCHAR autoLower[] = { 'a', 'u', 't', 'o', 0 };
    static const WCHAR autoMixed[] = { 'A', 'u', 'T', 'o' };
    static const WCHAR autoLonger[] = { 'a', 'u', 't', 'o', 'x', 0 };
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
    CHECK_EQ(v.Value[CM_VAL_NET], 0, "XhciCmNet defaults to 0 (record 35 section 8.1)");
    CHECK_EQ(v.Value[CM_VAL_NET_POLL_MS], 1, "XhciCmNetPollMs defaults to 1");
    CHECK_EQ(v.Value[CM_VAL_NET_RX_FRAMES], 128, "XhciCmNetRxFrames defaults to 128");
    CHECK_EQ(v.Value[CM_VAL_NET_TX_FRAMES], 128, "XhciCmNetTxFrames defaults to 128");
    CHECK_EQ(v.Value[CM_VAL_NET_E2E], 1, "XhciCmNetE2E defaults to 1");
    CHECK_EQ(v.Value[CM_VAL_NET_IRQ], 1, "XhciCmNetInterrupt defaults to 1");
    CHECK_EQ(v.Value[CM_VAL_INFER_AT_START], 0,
             "XhciCmInferAtStart defaults to 0 (record 22 section 7.6.2)");
    CHECK_EQ(v.Found | v.Refused | v.Clamped, 0, "nothing found yet");
    CHECK_EQ(v.TunnelBridgeChars, 0, "no bridge override");
    for (i = 0; i < CM_VAL_COUNT; i++) {
        CHECK_EQ(CmValueDefault(i), v.Value[i], "CmValueDefault matches the defaults");
    }
    CHECK_EQ(CmValueDefault(CM_VAL_COUNT), 0, "an index out of range");

    /* Record 32 sections 12.8.6 and 12.8.15.10: the handover's values. */
    CHECK_EQ(v.PlatformHandover, 1, "XhciCmAsm4242Handover defaults to 1");
    CHECK_EQ(v.HandoverStructural, 0, "XhciCmAsm4242Structural defaults to 0");
    CHECK_EQ(v.HandoverReads, 0, "XhciCmAsm4242Reads defaults to 0");
    CmValuesTakeHandoverReads(&v, 0, 0);
    CHECK(v.HandoverReads == 0 && !v.HandoverReadsFound, "Reads absent: 0");
    CmValuesTakeHandoverReads(&v, 1, 1);
    CHECK(v.HandoverReads == 1 && v.HandoverReadsFound &&
          !v.HandoverReadsRefused, "Reads 1 taken");
    CmValuesTakeHandoverReads(&v, 1, 2);
    CHECK(v.HandoverReads == 0 && v.HandoverReadsRefused &&
          v.HandoverReadsRaw == 2, "Reads 2 refused: 0 stands");
    CmValuesTakeHandoverReads(&v, 1, 0);
    CHECK(v.HandoverReads == 0 && !v.HandoverReadsRefused, "Reads 0 taken");
    CmValuesDefault(&v);

    /* Record 35 section 8.1: each refused out of its range, the default
     * standing; the bounds taken. */
    CmValuesTake(&v, CM_VAL_NET, 1, 1);
    CHECK_EQ(v.Value[CM_VAL_NET], 1, "XhciCmNet 1 taken");
    CmValuesTake(&v, CM_VAL_NET, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_NET], 0, "XhciCmNet 2 refused: 0 stands");
    CmValuesTake(&v, CM_VAL_NET_POLL_MS, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_NET_POLL_MS], 1, "XhciCmNetPollMs 0 refused");
    CmValuesTake(&v, CM_VAL_NET_POLL_MS, 1, 100);
    CHECK_EQ(v.Value[CM_VAL_NET_POLL_MS], 100, "XhciCmNetPollMs 100 taken");
    CmValuesTake(&v, CM_VAL_NET_POLL_MS, 1, 101);
    CHECK_EQ(v.Value[CM_VAL_NET_POLL_MS], 1, "XhciCmNetPollMs 101 refused: 1 stands");
    CmValuesTake(&v, CM_VAL_NET_RX_FRAMES, 1, 31);
    CHECK_EQ(v.Value[CM_VAL_NET_RX_FRAMES], 128, "XhciCmNetRxFrames 31 refused");
    CmValuesTake(&v, CM_VAL_NET_RX_FRAMES, 1, 32);
    CHECK_EQ(v.Value[CM_VAL_NET_RX_FRAMES], 32, "XhciCmNetRxFrames 32 taken");
    CmValuesTake(&v, CM_VAL_NET_TX_FRAMES, 1, 1024);
    CHECK_EQ(v.Value[CM_VAL_NET_TX_FRAMES], 1024, "XhciCmNetTxFrames 1024 taken");
    CmValuesTake(&v, CM_VAL_NET_TX_FRAMES, 1, 1025);
    CHECK_EQ(v.Value[CM_VAL_NET_TX_FRAMES], 128, "XhciCmNetTxFrames 1025 refused");
    CmValuesTake(&v, CM_VAL_NET_E2E, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_NET_E2E], 0, "XhciCmNetE2E 0 taken");
    CmValuesTake(&v, CM_VAL_NET_IRQ, 1, 0);
    CHECK_EQ(v.Value[CM_VAL_NET_IRQ], 0, "XhciCmNetInterrupt 0 taken");
    CmValuesTake(&v, CM_VAL_NET_IRQ, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_NET_IRQ], 1, "XhciCmNetInterrupt 2 refused: 1 stands");
    CmValuesTake(&v, CM_VAL_INFER_AT_START, 1, 1);
    CHECK_EQ(v.Value[CM_VAL_INFER_AT_START], 1, "XhciCmInferAtStart 1 taken");
    CmValuesTake(&v, CM_VAL_INFER_AT_START, 1, 2);
    CHECK_EQ(v.Value[CM_VAL_INFER_AT_START], 0,
             "XhciCmInferAtStart 2 refused: the default 0 stands");
    CmValuesDefault(&v);

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

    /* XhciCmBridgeReq (design record 33 section 9.1). */
    CHECK_EQ(v.BridgeReq, 1, "XhciCmBridgeReq defaults to 1");
    CmValuesTakeBridgeReq(&v, 1, 0);
    CHECK_EQ(v.BridgeReq, 0, "XhciCmBridgeReq 0 is taken");
    CmValuesTakeBridgeReq(&v, 1, 2);
    CHECK_EQ(v.BridgeReq, 2, "XhciCmBridgeReq 2 is taken");
    CmValuesTakeBridgeReq(&v, 1, 3);
    CHECK(v.BridgeReq == 1 && v.BridgeReqRefused,
          "XhciCmBridgeReq 3 is refused, 1 stands");
    CmValuesTakeBridgeReq(&v, 0, 0);
    CHECK(v.BridgeReq == 1 && !v.BridgeReqFound,
          "XhciCmBridgeReq absent reads as 1");

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
    before = v;
    CmValuesTake(&v, CM_VAL_COUNT, 1, 5);
    CHECK(memcmp(&before, &v, sizeof(v)) == 0,
          "an index out of range is ignored: nothing changed");

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
    /* Task 45.50: "auto", the install's written form, is no override in
     * any case, with or without its counted terminator; a longer word is
     * text, held for the parser. */
    CmValuesTakeBridge(&v, bdf, 7);
    CmValuesTakeBridge(&v, autoLower, 5);
    CHECK(v.TunnelBridgeChars == 0 && v.TunnelBridgeRefused == 0 &&
          v.TunnelBridge[0] == 0, "auto is no override");
    CmValuesTakeBridge(&v, autoMixed, 4);
    CHECK_EQ(v.TunnelBridgeChars, 0, "AuTo is no override");
    CmValuesTakeBridge(&v, autoLonger, 5);
    CHECK_EQ(v.TunnelBridgeChars, 5, "autox is text, not auto");
    CmValuesTakeBridge(&v, autoLonger, 3);
    CHECK_EQ(v.TunnelBridgeChars, 3, "aut is text, not auto");
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
     * table, since only the 98 SE and ME install writes it (task 44.46;
     * the INF gate reads its default from cm_vals.h); default on, 0 and 1
     * taken, anything else refused with the default standing. */
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

    /* XhciCmAsmediaPcie7Credits (task 44.41): 0 by default, 0 or 1 taken,
     * anything else refused with 0 standing. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_ASM_PCIE7_DEFAULT, 0, "XhciCmAsmediaPcie7Credits defaults to 0");
    CHECK_EQ(v.AsmPcie7, 0, "0 after CmValuesDefault");
    CmValuesTakeAsmPcie7(&v, 0, 1);
    CHECK_EQ(v.AsmPcie7 | v.AsmPcie7Found | v.AsmPcie7Refused, 0, "absent: 0, nothing found");
    CmValuesTakeAsmPcie7(&v, 1, 1);
    CHECK(v.AsmPcie7 == 1 && v.AsmPcie7Found == 1 && v.AsmPcie7Refused == 0, "1 is taken");
    CmValuesTakeAsmPcie7(&v, 1, 0);
    CHECK(v.AsmPcie7 == 0 && v.AsmPcie7Found == 1, "0 is taken");
    CmValuesTakeAsmPcie7(&v, 1, 7);
    CHECK(v.AsmPcie7 == 0 && v.AsmPcie7Refused == 1 && v.AsmPcie7Raw == 7,
          "7 is refused: 0 stands, the value recorded");
    CmValuesTakeAsmPcie7(&v, 1, 0xFFFFFFFFUL);
    CHECK_EQ(v.AsmPcie7, 0, "all-ones is refused");
    CmValuesTakeAsmPcie7(&v, 0, 0);
    CHECK_EQ(v.AsmPcie7Found | v.AsmPcie7Refused, 0, "a later absent read clears both");
    CHECK_EQ(v.Found | v.Refused, 0, "the rule table's bits untouched by it");

    /* XhciCmRingInterrupt (task 45.22, record 21 section 4.5.4): 0 by
     * default (owner, 2026-10-29), 0 or 1 taken, 2 and above refused. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_RING_IRQ_DEFAULT, 0, "XhciCmRingInterrupt defaults to 0");
    CHECK_EQ(v.RingIrq, 0, "0 after CmValuesDefault");
    CmValuesTakeRingIrq(&v, 0, 1);
    CHECK_EQ(v.RingIrq | v.RingIrqFound | v.RingIrqRefused, 0, "absent: 0, nothing found");
    CmValuesTakeRingIrq(&v, 1, 1);
    CHECK(v.RingIrq == 1 && v.RingIrqFound == 1 && v.RingIrqRefused == 0, "1 is taken");
    CmValuesTakeRingIrq(&v, 1, 0);
    CHECK(v.RingIrq == 0 && v.RingIrqFound == 1, "0 is taken");
    CmValuesTakeRingIrq(&v, 1, 2);
    CHECK(v.RingIrq == 2 && v.RingIrqRefused == 0 && v.RingIrqRaw == 2,
          "2 is taken (the message on 98 SE and ME, task 49.27)");
    CmValuesTakeRingIrq(&v, 1, 3);
    CHECK(v.RingIrq == 0 && v.RingIrqRefused == 1 && v.RingIrqRaw == 3,
          "3 is refused: 0 stands");
    CmValuesTakeRingIrq(&v, 1, 0xFFFFFFFFUL);
    CHECK_EQ(v.RingIrq, 0, "all-ones is refused");
    CmValuesTakeRingIrq(&v, 0, 0);
    CHECK_EQ(v.RingIrqFound | v.RingIrqRefused, 0, "a later absent read clears both");
    /* XhciCmUsb4V2 (task 45.20, decision U30): 1 by default, 0 or 1 taken,
     * anything else refused with 1 standing. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_USB4_V2_DEFAULT, 1, "XhciCmUsb4V2 defaults to 1");
    CHECK_EQ(v.Usb4V2, 1, "1 after CmValuesDefault");
    CmValuesTakeUsb4V2(&v, 0, 0);
    CHECK(v.Usb4V2 == 1 && v.Usb4V2Found == 0 && v.Usb4V2Refused == 0,
          "absent: 1, nothing found");
    CmValuesTakeUsb4V2(&v, 1, 0);
    CHECK(v.Usb4V2 == 0 && v.Usb4V2Found == 1 && v.Usb4V2Refused == 0, "0 is taken");
    CmValuesTakeUsb4V2(&v, 1, 1);
    CHECK(v.Usb4V2 == 1 && v.Usb4V2Found == 1, "1 is taken");
    CmValuesTakeUsb4V2(&v, 1, 2);
    CHECK(v.Usb4V2 == 1 && v.Usb4V2Refused == 1 && v.Usb4V2Raw == 2,
          "2 is refused: 1 stands, the value recorded");
    CmValuesTakeUsb4V2(&v, 1, 0xFFFFFFFFUL);
    CHECK_EQ(v.Usb4V2, 1, "all-ones is refused");
    CmValuesTakeUsb4V2(&v, 0, 0);
    CHECK_EQ(v.Usb4V2Found | v.Usb4V2Refused, 0, "a later absent read clears both");
    /* XhciCmGen4LinkRecovery (task 45.56, decision U33): the same rule. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_GEN4_ELR_DEFAULT, 1, "XhciCmGen4LinkRecovery defaults to 1");
    CHECK_EQ(v.Gen4Elr, 1, "1 after CmValuesDefault");
    CmValuesTakeGen4Elr(&v, 0, 0);
    CHECK(v.Gen4Elr == 1 && v.Gen4ElrFound == 0 && v.Gen4ElrRefused == 0,
          "absent: 1, nothing found");
    CmValuesTakeGen4Elr(&v, 1, 0);
    CHECK(v.Gen4Elr == 0 && v.Gen4ElrFound == 1 && v.Gen4ElrRefused == 0, "0 is taken");
    CmValuesTakeGen4Elr(&v, 1, 1);
    CHECK(v.Gen4Elr == 1 && v.Gen4ElrFound == 1, "1 is taken");
    CmValuesTakeGen4Elr(&v, 1, 2);
    CHECK(v.Gen4Elr == 1 && v.Gen4ElrRefused == 1 && v.Gen4ElrRaw == 2,
          "2 is refused: 1 stands, the value recorded");
    CmValuesTakeGen4Elr(&v, 1, 0xFFFFFFFFUL);
    CHECK_EQ(v.Gen4Elr, 1, "all-ones is refused");
    CmValuesTakeGen4Elr(&v, 0, 0);
    CHECK_EQ(v.Gen4ElrFound | v.Gen4ElrRefused, 0, "a later absent read clears both");
    /* XhciCmSleepEntry (task 48.35, decision U36): the same rule, its
     * default 1 since the emulator legs passed. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_SLEEP_ENTRY_DEFAULT, 1, "XhciCmSleepEntry defaults to 1");
    CHECK_EQ(v.SleepEntry, 1, "1 after CmValuesDefault");
    CmValuesTakeSleepEntry(&v, 0, 0);
    CHECK(v.SleepEntry == CM_SLEEP_ENTRY_DEFAULT && v.SleepEntryFound == 0 &&
          v.SleepEntryRefused == 0, "absent: the default, nothing found");
    CmValuesTakeSleepEntry(&v, 1, 1);
    CHECK(v.SleepEntry == 1 && v.SleepEntryFound == 1 && v.SleepEntryRefused == 0,
          "1 is taken");
    CmValuesTakeSleepEntry(&v, 1, 0);
    CHECK(v.SleepEntry == 0 && v.SleepEntryFound == 1, "0 is taken");
    CmValuesTakeSleepEntry(&v, 1, 2);
    CHECK(v.SleepEntry == CM_SLEEP_ENTRY_DEFAULT && v.SleepEntryRefused == 1 &&
          v.SleepEntryRaw == 2, "2 is refused: the default stands, the value recorded");
    CmValuesTakeSleepEntry(&v, 0, 0);
    CHECK_EQ(v.SleepEntryFound | v.SleepEntryRefused, 0, "a later absent read clears both");
    /* XhciCmWakePowerUp (task 49.46): the same rule, default 1. */
    CmValuesDefault(&v);
    CHECK_EQ(CM_WAKE_POWER_UP_DEFAULT, 1, "XhciCmWakePowerUp defaults to 1");
    CHECK_EQ(v.WakePowerUp, 1, "1 after CmValuesDefault");
    CmValuesTakeWakePowerUp(&v, 0, 0);
    CHECK(v.WakePowerUp == 1 && v.WakePowerUpFound == 0 &&
          v.WakePowerUpRefused == 0, "absent: the default, nothing found");
    CmValuesTakeWakePowerUp(&v, 1, 0);
    CHECK(v.WakePowerUp == 0 && v.WakePowerUpFound == 1, "0 is taken");
    CmValuesTakeWakePowerUp(&v, 1, 1);
    CHECK(v.WakePowerUp == 1 && v.WakePowerUpRefused == 0, "1 is taken");
    CmValuesTakeWakePowerUp(&v, 1, 2);
    CHECK(v.WakePowerUp == 1 && v.WakePowerUpRefused == 1 &&
          v.WakePowerUpRaw == 2, "2 is refused: the default stands, the value recorded");
    CmValuesTakeWakePowerUp(&v, 0, 0);
    CHECK_EQ(v.WakePowerUpFound | v.WakePowerUpRefused, 0, "a later absent read clears both");
    CHECK_EQ(v.Found | v.Refused, 0, "the rule table's bits untouched by it");
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

/* XHCITR mirrors the profiles' values (xhcitr\tr_core.h, TR_PROFILE_*;
 * record 25 section 17), since it cannot include cm_vals.h. */
static void test_profile_values(void)
{
    CHECK_EQ(CM_PROFILE_ICM_FR, 1, "FR is XHCITR's 1");
    CHECK_EQ(CM_PROFILE_ICM_AR, 2, "AR is XHCITR's 2");
    CHECK_EQ(CM_PROFILE_ICM_TR, 3, "TR is XHCITR's 3");
    CHECK_EQ(CM_PROFILE_ICM_ICL, 4, "ICL is XHCITR's 4");
    CHECK_EQ(CM_PROFILE_USB4, 5, "USB4 is XHCITR's 5");
    CHECK_EQ(CM_PROFILE_ICM_MR, 6, "MR is XHCITR's 6");
    CHECK_EQ(CM_PROFILE_ICM_TGL, 7, "TGL is XHCITR's 7");
    /* The pairs test_tr.c holds tr_profile_of_nhi to. */
    {
        static const ULONG pairs[][3] = {
            { 0x8086, 0x15BF, 2 }, { 0x8086, 0x15EB, 3 }, { 0x8086, 0x8A17, 4 },
            { 0x8086, 0x1137, 6 }, { 0x8086, 0x9A1B, 7 }, { 0x8086, 0x156A, 1 },
            { 0x8086, 0xA833, 5 }, { 0x1022, 0x1668, 5 }, { 0x8086, 0x1234, 0 }
        };
        CM_IDS ids;
        ULONG i;
        for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
            memset(&ids, 0, sizeof(ids));
            ids.Role = CM_ROLE_NHI;
            ids.Vendor = pairs[i][0];
            ids.Device = pairs[i][1];
            CHECK_EQ(CmProfileFor(&ids), pairs[i][2], "XHCITR's NHI table");
        }
    }
}

int main(void)
{
    test_roles();
    test_role_decide();
    test_profiles();
    test_profile_values();
    test_decision();
    test_values();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
