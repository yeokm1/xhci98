/*
 * cm_ports.c - the pure half of cm_reenum.c (cm_ports.h says what it
 * holds). C89, DDK-free. IRQL: any.
 *
 * The id lists are record 21 section 3.3's tables, which xhcicm.inf and
 * xhcicmf.inf match line for line (their source, the public PCI id
 * listing, is recorded there and in legal-provenance.md). A family groups
 * the NHIs that share a switch or a processor: the discrete parts by
 * generation (Falcon, Alpine, Titan and Maple Ridge, whose NHIs have
 * their own switch whatever the part's port count), the integrated ones
 * by processor (Ice Lake, Tiger Lake-LP, Tiger Lake-H, Alder Lake-P,
 * Raptor Lake-P, Meteor Lake), since an integrated tunnel root port is
 * matched only beside an NHI of its own processor (section 6.3 source 3).
 * Meteor Lake has no root port id in the listing: its set comes from the
 * override. Design record 22 section 3.7 adds the USB4-only hosts' rows
 * (task 40-0.2's ids, as both INFs carry them).
 */

#include "cm_ports.h"

#define CM_V 0x8086UL
#define CM_ID(dev) CM_PORTS_ID(CM_V, (dev))
#define CM_N(a) (sizeof(a) / sizeof((a)[0]))

static const ULONG cmFrNhi[] = { CM_ID(0x156A), CM_ID(0x156C) };
static const ULONG cmFrPort[] = { CM_ID(0x156B), CM_ID(0x156D) };
static const ULONG cmArNhi[] = { CM_ID(0x1575), CM_ID(0x1577), CM_ID(0x15BF),
                                 CM_ID(0x15D2), CM_ID(0x15D9) };
static const ULONG cmArPort[] = { CM_ID(0x1576), CM_ID(0x1578), CM_ID(0x15C0),
                                  CM_ID(0x15D3), CM_ID(0x15DA) };
static const ULONG cmTrNhi[] = { CM_ID(0x15E8), CM_ID(0x15EB) };
static const ULONG cmTrPort[] = { CM_ID(0x15E7), CM_ID(0x15EA) };
static const ULONG cmMrNhi[] = { CM_ID(0x1134), CM_ID(0x1137) };
static const ULONG cmMrPort[] = { CM_ID(0x1133), CM_ID(0x1136) };
static const ULONG cmIclNhi[] = { CM_ID(0x8A17), CM_ID(0x8A0D) };
static const ULONG cmIclRoot[] = { CM_ID(0x8A1D), CM_ID(0x8A1F), CM_ID(0x8A21),
                                   CM_ID(0x8A23) };
static const ULONG cmTglNhi[] = { CM_ID(0x9A1B), CM_ID(0x9A1D) };
static const ULONG cmTglRoot[] = { CM_ID(0x9A23), CM_ID(0x9A25), CM_ID(0x9A27),
                                   CM_ID(0x9A29) };
static const ULONG cmTglhNhi[] = { CM_ID(0x9A1F), CM_ID(0x9A21) };
static const ULONG cmTglhRoot[] = { CM_ID(0x9A2B), CM_ID(0x9A2D), CM_ID(0x9A2F),
                                    CM_ID(0x9A31) };
static const ULONG cmAdlNhi[] = { CM_ID(0x463E), CM_ID(0x466D) };
static const ULONG cmAdlRoot[] = { CM_ID(0x466E), CM_ID(0x463F), CM_ID(0x462F),
                                   CM_ID(0x461F) };
static const ULONG cmRplNhi[] = { CM_ID(0xA73E), CM_ID(0xA76D) };
static const ULONG cmRplRoot[] = { CM_ID(0xA76E), CM_ID(0xA71F), CM_ID(0xA72F) };
static const ULONG cmMtlNhi[] = { CM_ID(0x7EB2), CM_ID(0x7EC2), CM_ID(0x7EC3) };
/* Design record 22 section 3.7's USB4-only hosts (Phase 40): Lunar, Panther
 * and Wildcat Lake name no tunnel port in the listing; Barlow Ridge's host
 * bridges are its own switch's ports; AMD's USB4 PCIe tunnel ports are
 * root ports beside its two routers' NHIs (Pink Sardine 1668 and 1669,
 * FreeBSD nhi_reg.h), so ambiguous until the override assigns them. */
static const ULONG cmLnlNhi[] = { CM_ID(0xA833), CM_ID(0xA834) };
static const ULONG cmPtlNhi[] = { CM_ID(0xE333), CM_ID(0xE334), CM_ID(0xE433),
                                  CM_ID(0xE434) };
static const ULONG cmWclNhi[] = { CM_ID(0x4D33) };
static const ULONG cmBrNhi[] = { CM_ID(0x5781), CM_ID(0x5784) };
static const ULONG cmBrPort[] = { CM_ID(0x5780), CM_ID(0x5783) };
static const ULONG cmAmdNhi[] = { CM_PORTS_ID(0x1022, 0x1668),
                                  CM_PORTS_ID(0x1022, 0x1669) };
static const ULONG cmAmdRoot[] = { CM_PORTS_ID(0x1022, 0x14CD),
                                   CM_PORTS_ID(0x1022, 0x14EF),
                                   CM_PORTS_ID(0x1022, 0x150A) };
/* The emulator's generic USB4 host (usb4-emu usb4-generic, 1B36:00F4) and
 * its q35 tunnel port, a root port beside it. */
static const ULONG cmEmuNhi[] = { CM_PORTS_ID(0x1B36, 0x00F4) };
static const ULONG cmEmuRoot[] = { CM_PORTS_ID(0x1B36, 0x000C) };

/*
 * Bridge ids xhcicmf.inf names that no family carries: ASMedia's ASM4242
 * downstream port, whose host is matched by class alone (no NHI id to
 * pair it with), and QEMU's generic PCI-to-PCI bridge, the emulator's
 * i440FX tunnel bridge, which the SHPC source finds and which is far too
 * common an id to be a family's.
 */
static const ULONG cmUnpaired[] = { CM_PORTS_ID(0x1B21, 0x2423),
                                    CM_PORTS_ID(0x1B36, 0x0001) };

static const CM_FAMILY cmFamilies[] = {
    { "falcon-ridge", cmFrNhi, CM_N(cmFrNhi), cmFrPort, CM_N(cmFrPort),
      NULL, 0 },
    { "alpine-ridge", cmArNhi, CM_N(cmArNhi), cmArPort, CM_N(cmArPort),
      NULL, 0 },
    { "titan-ridge", cmTrNhi, CM_N(cmTrNhi), cmTrPort, CM_N(cmTrPort),
      NULL, 0 },
    { "maple-ridge", cmMrNhi, CM_N(cmMrNhi), cmMrPort, CM_N(cmMrPort),
      NULL, 0 },
    { "ice-lake", cmIclNhi, CM_N(cmIclNhi), NULL, 0,
      cmIclRoot, CM_N(cmIclRoot) },
    { "tiger-lake-lp", cmTglNhi, CM_N(cmTglNhi), NULL, 0,
      cmTglRoot, CM_N(cmTglRoot) },
    { "tiger-lake-h", cmTglhNhi, CM_N(cmTglhNhi), NULL, 0,
      cmTglhRoot, CM_N(cmTglhRoot) },
    { "alder-lake-p", cmAdlNhi, CM_N(cmAdlNhi), NULL, 0,
      cmAdlRoot, CM_N(cmAdlRoot) },
    { "raptor-lake-p", cmRplNhi, CM_N(cmRplNhi), NULL, 0,
      cmRplRoot, CM_N(cmRplRoot) },
    { "meteor-lake", cmMtlNhi, CM_N(cmMtlNhi), NULL, 0, NULL, 0 },
    { "lunar-lake", cmLnlNhi, CM_N(cmLnlNhi), NULL, 0, NULL, 0 },
    { "panther-lake", cmPtlNhi, CM_N(cmPtlNhi), NULL, 0, NULL, 0 },
    { "wildcat-lake", cmWclNhi, CM_N(cmWclNhi), NULL, 0, NULL, 0 },
    { "barlow-ridge", cmBrNhi, CM_N(cmBrNhi), cmBrPort, CM_N(cmBrPort),
      NULL, 0 },
    { "amd-usb4", cmAmdNhi, CM_N(cmAmdNhi), NULL, 0,
      cmAmdRoot, CM_N(cmAmdRoot) },
    { "usb4-emu", cmEmuNhi, CM_N(cmEmuNhi), NULL, 0,
      cmEmuRoot, CM_N(cmEmuRoot) }
};

ULONG CmPortsUnpairedCount(VOID)
{
    return (ULONG)CM_N(cmUnpaired);
}

ULONG CmPortsUnpairedAt(ULONG n)
{
    return n < CM_N(cmUnpaired) ? cmUnpaired[n] : 0;
}

BOOLEAN CmPortsFilterId(ULONG id)
{
    ULONG f;
    ULONG i;

    for (i = 0; i < CM_N(cmUnpaired); i++) {
        if (cmUnpaired[i] == id) {
            return TRUE;
        }
    }
    for (f = 0; f < CM_N(cmFamilies); f++) {
        for (i = 0; i < cmFamilies[f].SwitchPortIdCount; i++) {
            if (cmFamilies[f].SwitchPortIds[i] == id) {
                return TRUE;
            }
        }
        for (i = 0; i < cmFamilies[f].RootPortIdCount; i++) {
            if (cmFamilies[f].RootPortIds[i] == id) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

ULONG CmPortsFamilyCount(VOID)
{
    return (ULONG)CM_N(cmFamilies);
}

const CM_FAMILY *CmPortsFamilyAt(ULONG n)
{
    if (n >= CM_N(cmFamilies)) {
        return NULL;
    }
    return &cmFamilies[n];
}

const CM_FAMILY *CmPortsFamily(ULONG nhiId)
{
    ULONG f;
    ULONG i;

    for (f = 0; f < CM_N(cmFamilies); f++) {
        for (i = 0; i < cmFamilies[f].NhiIdCount; i++) {
            if (cmFamilies[f].NhiIds[i] == nhiId) {
                return &cmFamilies[f];
            }
        }
    }
    return NULL;
}

/* ---- Windows 98 device ids ------------------------------------------- */

static char cmUpper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

/* The hexadecimal digit's value, or 16 for a non-digit. */
static ULONG cmHex(char c)
{
    c = cmUpper(c);
    if (c >= '0' && c <= '9') {
        return (ULONG)(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return (ULONG)(c - 'A' + 10);
    }
    return 16;
}

/* The id's length: up to `chars` or the first NUL. */
static ULONG cmLen(const char *s, ULONG chars)
{
    ULONG n;

    n = 0;
    while (n < chars && s[n] != 0) {
        n++;
    }
    return n;
}

/* TRUE when s[at..] starts with `lit` (upper case), case ignored. */
static BOOLEAN cmAt(const char *s, ULONG len, ULONG at, const char *lit)
{
    ULONG i;

    for (i = 0; lit[i] != 0; i++) {
        if (at + i >= len || cmUpper(s[at + i]) != lit[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * One to `maxDigits` hexadecimal digits at s[*at..]; FALSE when there is
 * none or more than maxDigits. Advances *at past them.
 */
static BOOLEAN cmTakeHex(const char *s, ULONG len, PULONG at,
                         ULONG maxDigits, PULONG value)
{
    ULONG v;
    ULONG n;
    ULONG d;

    v = 0;
    n = 0;
    while (*at < len) {
        d = cmHex(s[*at]);
        if (d > 15) {
            break;
        }
        if (n == maxDigits) {
            return FALSE;
        }
        v = (v << 4) | d;
        n++;
        (*at)++;
    }
    if (n == 0) {
        return FALSE;
    }
    *value = v;
    return TRUE;
}

BOOLEAN CmPortsIdLocation(const char *id, ULONG chars, PULONG bdf)
{
    ULONG len;
    ULONG at;
    ULONG slash;
    ULONG bus;
    ULONG dev;
    ULONG fn;

    if (id == NULL || bdf == NULL) {
        return FALSE;
    }
    len = cmLen(id, chars);
    if (!cmAt(id, len, 0, "PCI\\")) {
        return FALSE;
    }
    /* The instance part follows the second backslash. */
    slash = 0;
    for (at = 4; at < len; at++) {
        if (id[at] == '\\') {
            slash = at;
            break;
        }
    }
    if (slash == 0) {
        return FALSE;
    }
    at = slash + 1;
    if (!cmAt(id, len, at, "BUS_")) {
        return FALSE;
    }
    at += 4;
    if (!cmTakeHex(id, len, &at, 2, &bus)) {
        return FALSE;
    }
    if (!cmAt(id, len, at, "&DEV_")) {
        return FALSE;
    }
    at += 5;
    if (!cmTakeHex(id, len, &at, 2, &dev) || dev > 0x1FUL) {
        return FALSE;
    }
    if (!cmAt(id, len, at, "&FUNC_")) {
        return FALSE;
    }
    at += 6;
    if (!cmTakeHex(id, len, &at, 2, &fn) || fn > 7UL) {
        return FALSE;
    }
    if (at != len) {
        return FALSE;
    }
    *bdf = (bus << 8) | (dev << 3) | fn;
    return TRUE;
}

BOOLEAN CmPortsIdPath(const char *id, ULONG chars, PULONG path,
                      ULONG capacity, PULONG count)
{
    ULONG len;
    ULONG at;
    ULONG slash;
    ULONG n;
    ULONG hi;
    ULONG lo;

    if (id == NULL || path == NULL || count == NULL) {
        return FALSE;
    }
    len = cmLen(id, chars);
    if (!cmAt(id, len, 0, "PCI\\")) {
        return FALSE;
    }
    slash = 0;
    for (at = 4; at < len; at++) {
        if (id[at] == '\\') {
            slash = at;
            break;
        }
    }
    if (slash == 0) {
        return FALSE;
    }
    at = slash + 1;
    if (len - at < 4 || ((len - at) & 1UL) != 0 ||
        (len - at) / 2 > capacity) {
        return FALSE;
    }
    n = 0;
    while (at < len) {
        hi = cmHex(id[at]);
        lo = cmHex(id[at + 1]);
        if (hi > 15 || lo > 15) {
            return FALSE;
        }
        path[n++] = (hi << 4) | lo;
        at += 2;
    }
    *count = n;
    return TRUE;
}

BOOLEAN CmPortsIdVenDev(const char *id, ULONG chars, PULONG id32)
{
    ULONG len;
    ULONG at;
    ULONG ven;
    ULONG dev;
    ULONG start;

    if (id == NULL || id32 == NULL) {
        return FALSE;
    }
    len = cmLen(id, chars);
    if (!cmAt(id, len, 0, "PCI\\VEN_")) {
        return FALSE;
    }
    at = 8;
    start = at;
    if (!cmTakeHex(id, len, &at, 4, &ven) || at - start != 4) {
        return FALSE;
    }
    if (!cmAt(id, len, at, "&DEV_")) {
        return FALSE;
    }
    at += 5;
    start = at;
    if (!cmTakeHex(id, len, &at, 4, &dev) || at - start != 4) {
        return FALSE;
    }
    *id32 = CM_PORTS_ID(ven, dev);
    return TRUE;
}

/* ---- the snapshot ----------------------------------------------------- */

ULONG CmPortsNewPdos(PVOID const *old, ULONG oldCount, PVOID const *now,
                     ULONG nowCount, PULONG isNew)
{
    ULONG i;
    ULONG j;
    ULONG fresh;

    fresh = 0;
    for (i = 0; i < nowCount; i++) {
        isNew[i] = 1;
        for (j = 0; j < oldCount; j++) {
            if (old[j] == now[i]) {
                isNew[i] = 0;
                break;
            }
        }
        fresh += isNew[i];
    }
    return fresh;
}

ULONG CmPortsChildAttach(ULONG prevTruncated, PVOID const *chain,
                         ULONG chainCount, PVOID busDriver, PVOID self)
{
    ULONG i;

    if (chainCount == 0) {
        return CM_CHILD_ATTACH;
    }
    if (prevTruncated || chainCount > CM_CHILD_CHAIN_MAX) {
        return CM_CHILD_SKIP;
    }
    for (i = 0; i < chainCount; i++) {
        if (chain[i] == NULL || chain[i] == busDriver || chain[i] == self) {
            return CM_CHILD_SKIP;
        }
    }
    return CM_CHILD_ATTACH_OVER;
}

ULONG CmPortsStackless(ULONG hasAttached, ULONG ownBusChild, ULONG ownAddDev,
                       ULONG aboveChild)
{
    if (!hasAttached) {
        return 1;
    }
    if (ownAddDev || !ownBusChild || aboveChild) {
        return 0;
    }
    return 1;
}

ULONG CmPortsChildFirst(ULONG prevTruncated, ULONG hasAttached)
{
    if (!hasAttached) {
        return CM_CHILD_ATTACH;
    }
    return prevTruncated ? CM_CHILD_SKIP : CM_CHILD_WALK;
}

/* ---- the hold's bound ------------------------------------------------- */

/* a + b, held to CM_PORTS_HOLD_MAX_MS. */
static ULONG cmAddCap(ULONG a, ULONG b)
{
    if (a >= CM_PORTS_HOLD_MAX_MS || b >= CM_PORTS_HOLD_MAX_MS ||
        a + b >= CM_PORTS_HOLD_MAX_MS) {
        return CM_PORTS_HOLD_MAX_MS;
    }
    return a + b;
}

/* a x b by repeated addition, held to CM_PORTS_HOLD_MAX_MS. */
static ULONG cmMulCap(ULONG a, ULONG b)
{
    ULONG r;
    ULONG i;

    r = 0;
    for (i = 0; i < b; i++) {
        r = cmAddCap(r, a);
        if (r == CM_PORTS_HOLD_MAX_MS) {
            break;
        }
    }
    return r;
}

/* 1 + n attempts, held to 1000. */
static ULONG cmAttempts(ULONG retries)
{
    return retries >= 1000UL ? 1000UL : retries + 1;
}

ULONG CmPortsKeepPresent(PULONG known, ULONG count, const ULONG *present,
                         ULONG presentCount)
{
    ULONG k;
    ULONG c;
    ULONG kept;

    kept = 0;
    for (k = 0; k < count; k++) {
        for (c = 0; c < presentCount; c++) {
            if (present[c] == known[k]) {
                known[kept++] = known[k];
                break;
            }
        }
    }
    return kept;
}

ULONG CmPortsHoldWatchdog(ULONG clientMs, BOOLEAN *over)
{
    *over = (BOOLEAN)(clientMs >= CM_PORTS_HOLD_MAX_MS ||
                      CM_PORTS_HOLD_MAX_MS - clientMs < CM_PORTS_HOLD_EXTRA_MS);
    return CM_PORTS_HOLD_MAX_MS;
}

ULONG CmPortsHoldBound(const CM_PORTS_RESUME *resume)
{
    ULONG bound;
    ULONG perDevice;

    bound = CM_PORTS_HOLD_PLATFORM_MS;
    bound = cmAddCap(bound, cmMulCap(resume->ReadyMs,
                                     cmAttempts(resume->ReadyRetries)));
    bound = cmAddCap(bound, cmMulCap(resume->HostPollMs,
                                     resume->HostPollTries >= 1000UL
                                         ? 1000UL
                                         : resume->HostPollTries));
    bound = cmAddCap(bound, resume->ReconcileMs);
    perDevice = cmAddCap(cmMulCap(resume->CmdMs, cmAttempts(resume->Retries)),
                         cmMulCap(resume->ApproveMs,
                                  cmAttempts(resume->Retries)));
    bound = cmAddCap(bound, cmMulCap(perDevice,
                                     resume->Devices >= 1000UL
                                         ? 1000UL
                                         : resume->Devices));
    return cmAddCap(bound, CM_PORTS_HOLD_EXTRA_MS);
}
