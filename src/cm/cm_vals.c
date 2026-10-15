/*
 * cm_vals.c - the pure half of xhcicm.sys's start (cm_vals.h): the role
 * a PDO's hardware ids name and the registry values' rules.
 *
 * IRQL: any. No lock, no DDK call; host-tested by test\test_cm.c.
 */

#include "cm_vals.h"

#define CM_RULE_SWITCH 0  /* out of range: refused, the default stands */
#define CM_RULE_CLAMP  1  /* out of range: held to the nearer bound    */

typedef struct _CM_RULE {
    ULONG Default;
    ULONG Min;
    ULONG Max;
    ULONG Kind;
} CM_RULE;

/* Record 21 section 8 and record 22 section 12, in CM_VAL_* order. */
static const CM_RULE cmRules[CM_VAL_COUNT] = {
    { 1,     0,    CM_MODE_MAX, CM_RULE_SWITCH }, /* XhciCmMode             */
    { 1,     0,    1,           CM_RULE_SWITCH }, /* XhciCmApprove          */
    { 1,     0,    1,           CM_RULE_SWITCH }, /* XhciCmReenum           */
    { 200,   50,   5000,        CM_RULE_CLAMP  }, /* XhciCmPollIdleMs       */
    { 5000,  1000, 60000,       CM_RULE_CLAMP  }, /* XhciCmCmdTimeoutMs     */
    { 10000, 1000, 60000,       CM_RULE_CLAMP  }, /* XhciCmApproveTimeoutMs */
    { 1,     0,    1,           CM_RULE_SWITCH }, /* XhciCmSafeEject        */
    { 0,     0,    0xFFFFFFFF,  CM_RULE_SWITCH }, /* XhciLogVerbosity       */
    { 1,     0,    1,           CM_RULE_SWITCH }, /* XhciCmUsb3Tunnel       */
    { 1,     0,    1,           CM_RULE_SWITCH }, /* XhciCmLaneBonding      */
    { 0,     0,    2,           CM_RULE_SWITCH }  /* XhciCmPreOsCleanup     */
};

static WCHAR cmUpper(WCHAR c)
{
    if (c >= 'a' && c <= 'z') {
        return (WCHAR)(c - 'a' + 'A');
    }
    return c;
}

static ULONG cmHexDigit(WCHAR c, ULONG *out)
{
    c = cmUpper(c);
    if (c >= '0' && c <= '9') {
        *out = (ULONG)(c - '0');
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (ULONG)(c - 'A' + 10);
        return 1;
    }
    return 0;
}

/*
 * The `digits` hex digits after the ASCII `tag` in `s`, into *out; returns
 * 1 when found. A tag followed by fewer digits, or by more where `exact`
 * asks for exactly `digits` (CC_0604 is a four-digit class, CC_0C0340 a
 * six-digit one), does not count.
 */
static ULONG cmHexAfter(const WCHAR *s, ULONG n, const char *tag,
                        ULONG digits, ULONG exact, ULONG *out)
{
    ULONG tagLen;
    ULONG i;
    ULONG j;
    ULONG v;
    ULONG d;

    tagLen = 0;
    while (tag[tagLen] != 0) {
        tagLen++;
    }
    for (i = 0; i + tagLen + digits <= n; i++) {
        for (j = 0; j < tagLen; j++) {
            if (cmUpper(s[i + j]) != (WCHAR)tag[j]) {
                break;
            }
        }
        if (j != tagLen) {
            continue;
        }
        v = 0;
        for (j = 0; j < digits; j++) {
            if (!cmHexDigit(s[i + tagLen + j], &d)) {
                break;
            }
            v = (v << 4) | d;
        }
        if (j != digits) {
            continue;
        }
        if (exact && i + tagLen + digits < n &&
            cmHexDigit(s[i + tagLen + digits], &d)) {
            continue;
        }
        *out = v;
        return 1;
    }
    return 0;
}

ULONG CmClassifyIds(const WCHAR *ids, ULONG chars, PCM_IDS out)
{
    ULONG start;
    ULONG len;
    ULONG first;
    ULONG bridge;
    ULONG haveClass;
    ULONG v;

    out->Role = CM_ROLE_NONE;
    out->Vendor = 0;
    out->Device = 0;
    out->ClassCode = 0;
    if (ids == NULL || chars == 0 || ids[0] == 0) {
        return CM_ROLE_NONE;
    }
    first = 1;
    bridge = 0;
    haveClass = 0;
    start = 0;
    while (start < chars && ids[start] != 0) {
        len = 0;
        while (start + len < chars && ids[start + len] != 0) {
            len++;
        }
        if (first) {
            if (cmHexAfter(ids + start, len, "VEN_", 4, 1, &v)) {
                out->Vendor = v;
            }
            if (cmHexAfter(ids + start, len, "DEV_", 4, 1, &v)) {
                out->Device = v;
            }
            first = 0;
        }
        if (cmHexAfter(ids + start, len, "&CC_", 4, 0, &v) && v == 0x0604) {
            bridge = 1;
        }
        if (!haveClass && cmHexAfter(ids + start, len, "CC_", 6, 1, &v)) {
            out->ClassCode = v;
            haveClass = 1;
        }
        start += len + 1;
    }
    out->Role = bridge ? CM_ROLE_FILTER : CM_ROLE_NHI;
    return out->Role;
}

/* NHI ids by profile (cm_vals.h): Intel's from Linux
 * drivers/thunderbolt/nhi.h, AMD's from FreeBSD sys/dev/thunderbolt
 * nhi_reg.h (DEVICE_PINK_SARDINE_0 and _1). */
typedef struct _CM_NHI_ROW {
    ULONG Device;
    ULONG Profile;
} CM_NHI_ROW;

static const CM_NHI_ROW cmAmdNhi[] = {
    { 0x1668, CM_PROFILE_USB4 },     /* Pink Sardine                      */
    { 0x1669, CM_PROFILE_USB4 }
};

static const CM_NHI_ROW cmIntelNhi[] = {
    { 0x156A, CM_PROFILE_ICM_FR },   /* Falcon Ridge 2C                   */
    { 0x156C, CM_PROFILE_ICM_FR },   /* Falcon Ridge 4C                   */
    { 0x1575, CM_PROFILE_ICM_AR },   /* Alpine Ridge 2C                   */
    { 0x1577, CM_PROFILE_ICM_AR },   /* Alpine Ridge 4C                   */
    { 0x15BF, CM_PROFILE_ICM_AR },   /* Alpine Ridge LP: captured [obs]   */
    { 0x15D2, CM_PROFILE_ICM_AR },   /* Alpine Ridge C 4C                 */
    { 0x15D9, CM_PROFILE_ICM_AR },   /* Alpine Ridge C 2C                 */
    { 0x15E8, CM_PROFILE_ICM_TR },   /* Titan Ridge 2C                    */
    { 0x15EB, CM_PROFILE_ICM_TR },   /* Titan Ridge 4C                    */
    { 0x1134, CM_PROFILE_ICM_MR },   /* Maple Ridge 2C                    */
    { 0x1137, CM_PROFILE_ICM_MR },   /* Maple Ridge 4C                    */
    { 0x8A0D, CM_PROFILE_ICM_ICL },  /* Ice Lake                          */
    { 0x8A17, CM_PROFILE_ICM_ICL },
    { 0x9A1B, CM_PROFILE_ICM_TGL },  /* Tiger Lake                        */
    { 0x9A1D, CM_PROFILE_ICM_TGL },
    { 0x9A1F, CM_PROFILE_ICM_TGL },  /* Tiger Lake H                      */
    { 0x9A21, CM_PROFILE_ICM_TGL },
    { 0x463E, CM_PROFILE_ICM_TGL },  /* Alder Lake                        */
    { 0x466D, CM_PROFILE_ICM_TGL },
    { 0xA73E, CM_PROFILE_ICM_TGL },  /* Raptor Lake                       */
    { 0xA76D, CM_PROFILE_ICM_TGL },
    { 0x7EB2, CM_PROFILE_ICM_TGL },  /* Meteor Lake M                     */
    { 0x7EC2, CM_PROFILE_ICM_TGL },  /* Meteor Lake P                     */
    { 0x7EC3, CM_PROFILE_ICM_TGL },
    { 0xA833, CM_PROFILE_USB4 },     /* Lunar Lake                        */
    { 0xA834, CM_PROFILE_USB4 },
    { 0xE333, CM_PROFILE_USB4 },     /* Panther Lake M                    */
    { 0xE334, CM_PROFILE_USB4 },
    { 0xE433, CM_PROFILE_USB4 },     /* Panther Lake P                    */
    { 0xE434, CM_PROFILE_USB4 },
    { 0x4D33, CM_PROFILE_USB4 },     /* Wildcat Lake                      */
    { 0x5781, CM_PROFILE_USB4 },     /* Barlow Ridge host 80G             */
    { 0x5784, CM_PROFILE_USB4 }      /* Barlow Ridge host 40G             */
};

#define CM_INTEL_VENDOR 0x8086
#define CM_AMD_VENDOR   0x1022

static ULONG cmLookup(const CM_NHI_ROW *rows, ULONG count, ULONG device)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        if (rows[i].Device == device) {
            return rows[i].Profile;
        }
    }
    return CM_PROFILE_UNKNOWN;
}

ULONG CmProfileFor(const CM_IDS *ids)
{
    ULONG profile;

    if (ids->Role != CM_ROLE_NHI) {
        return CM_PROFILE_UNKNOWN;
    }
    profile = CM_PROFILE_UNKNOWN;
    if (ids->Vendor == CM_INTEL_VENDOR) {
        profile = cmLookup(cmIntelNhi,
                           sizeof(cmIntelNhi) / sizeof(cmIntelNhi[0]),
                           ids->Device);
    } else if (ids->Vendor == CM_AMD_VENDOR) {
        profile = cmLookup(cmAmdNhi, sizeof(cmAmdNhi) / sizeof(cmAmdNhi[0]),
                           ids->Device);
    }
    if (profile != CM_PROFILE_UNKNOWN) {
        return profile;
    }
    if (ids->ClassCode == CM_CLASS_USB4_NHI) {
        return CM_PROFILE_USB4;
    }
    return CM_PROFILE_UNKNOWN;
}

ULONG CmStartDecision(ULONG mode, ULONG profile, ULONG intel, ULONG built)
{
    if (mode == CM_MODE_OFF) {
        return CM_INERT_MODE_OFF;
    }
    if (mode == CM_MODE_FORCE_SW) {
        /* Only onto a host the software CM supports: the Thunderbolt 3
         * hosts' routers are not USB4 routers, and ids unread give no
         * host at all. */
        if (profile == CM_PROFILE_ICM_FR || profile == CM_PROFILE_ICM_AR ||
            profile == CM_PROFILE_ICM_TR || profile == CM_PROFILE_ICM_ICL) {
            return CM_INERT_FORCE_REFUSED;
        }
        if (profile != CM_PROFILE_UNKNOWN) {
            profile = CM_PROFILE_USB4;
        }
    } else if (profile == CM_PROFILE_UNKNOWN && mode == CM_MODE_FORCE_ICM &&
               intel) {
        profile = CM_PROFILE_ICM_AR;
    } else if (profile == CM_PROFILE_USB4 && mode == CM_MODE_FORCE_ICM) {
        /* The ICM experiment on a host with no ICM row (record 22 section
         * 12): refused, never the software CM in its place. */
        return CM_INERT_FORCE_ICM_REFUSED;
    }
    if (profile == CM_PROFILE_UNKNOWN) {
        return CM_INERT_PROFILE_UNKNOWN;
    }
    if (profile >= 32 || (built & (1UL << profile)) == 0) {
        return CM_INERT_NOT_BUILT;
    }
    return CM_INERT_NONE;
}

VOID CmValuesDefault(PCM_VALUES v)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)v;
    for (i = 0; i < sizeof(CM_VALUES); i++) {
        p[i] = 0;
    }
    for (i = 0; i < CM_VAL_COUNT; i++) {
        v->Value[i] = cmRules[i].Default;
    }
}

VOID CmValuesTake(PCM_VALUES v, ULONG index, ULONG found, ULONG raw)
{
    const CM_RULE *r;

    if (index >= CM_VAL_COUNT) {
        return;
    }
    r = &cmRules[index];
    v->Value[index] = r->Default;
    if (!found) {
        return;
    }
    v->Found |= 1UL << index;
    v->Raw[index] = raw;
    if (raw >= r->Min && raw <= r->Max) {
        v->Value[index] = raw;
    } else if (r->Kind == CM_RULE_CLAMP) {
        v->Value[index] = (raw < r->Min) ? r->Min : r->Max;
        v->Clamped |= 1UL << index;
    } else {
        v->Refused |= 1UL << index;
    }
}

VOID CmValuesTakeBridge(PCM_VALUES v, const WCHAR *text, ULONG chars)
{
    ULONG i;

    v->TunnelBridgeChars = 0;
    v->TunnelBridgeRefused = 0;
    v->TunnelBridge[0] = 0;
    if (text == NULL) {
        return;
    }
    while (chars != 0 && text[chars - 1] == 0) {
        chars--;
    }
    if (chars >= CM_TUNNEL_BRIDGE_CHARS) {
        v->TunnelBridgeRefused = 1;
        return;
    }
    for (i = 0; i < chars; i++) {
        v->TunnelBridge[i] = text[i];
    }
    v->TunnelBridge[chars] = 0;
    v->TunnelBridgeChars = chars;
}

ULONG CmValueDefault(ULONG index)
{
    if (index >= CM_VAL_COUNT) {
        return 0;
    }
    return cmRules[index].Default;
}
