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
 * (task 40-0.2's ids, as both INFs carry them), and its section 18 the
 * ASMedia ASM4242's (task 44.40): its NHI is matched by the INFs' class
 * line alone, but a family keyed by the NHI's own id pairs it with its
 * switch's downstream ports, the hot-plug capable ones of which carry its
 * tunnels (record 22 section 18.1).
 */

#include "cm_ports.h"

#define CM_V 0x8086UL
#define CM_ID(dev) CM_PORTS_ID(CM_V, (dev))
#define CM_N(a) (sizeof(a) / sizeof((a)[0]))

/* Light Ridge and Cactus Ridge 4C (design record 22 section 19.4 item 4):
 * their switch ports carry the NHI's id; the discrete-switch source takes
 * type 1 siblings only, so the NHI itself is never a member. */
static const ULONG cmLrNhi[] = { CM_ID(0x1513) };
static const ULONG cmLrPort[] = { CM_ID(0x1513) };
static const ULONG cmCrNhi[] = { CM_ID(0x1547) };
static const ULONG cmCrPort[] = { CM_ID(0x1547) };
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
/* Task 46.23: AMD's other integrated hosts by processor, each beside the
 * PCIe USB4 bridge pci.ids 2026.10.31 names for that processor (Strix and
 * Strix Halo share 150A, Krackan and Krackan2 1125). Pink Sardine keeps
 * all three of its ports above: no source ties it to one processor. On a
 * Strix Halo machine read on 2026-11-03 the two 150A ports are root ports
 * on the root bus and each depends on one of the two NHIs, which sit
 * behind an internal bridge, not on that bus, so the root-port source
 * never takes them: the router's own mapping (record 22 section 7.6) or
 * the override does. */
static const ULONG cmRbNhi[] = { CM_PORTS_ID(0x1022, 0x162E),
                                 CM_PORTS_ID(0x1022, 0x162F) };
static const ULONG cmRbRoot[] = { CM_PORTS_ID(0x1022, 0x14CD) };
static const ULONG cmPhNhi[] = { CM_PORTS_ID(0x1022, 0x15C4),
                                 CM_PORTS_ID(0x1022, 0x15C5) };
static const ULONG cmPhRoot[] = { CM_PORTS_ID(0x1022, 0x14EF) };
static const ULONG cmStxNhi[] = { CM_PORTS_ID(0x1022, 0x151C),
                                  CM_PORTS_ID(0x1022, 0x151D),
                                  CM_PORTS_ID(0x1022, 0x158D),
                                  CM_PORTS_ID(0x1022, 0x158E) };
static const ULONG cmStxRoot[] = { CM_PORTS_ID(0x1022, 0x150A) };
static const ULONG cmKrkNhi[] = { CM_PORTS_ID(0x1022, 0x1120),
                                  CM_PORTS_ID(0x1022, 0x1121),
                                  CM_PORTS_ID(0x1022, 0x113B),
                                  CM_PORTS_ID(0x1022, 0x113C) };
static const ULONG cmKrkRoot[] = { CM_PORTS_ID(0x1022, 0x1125) };
/* ASMedia's ASM4242: host interface 2425 behind the downstream port 2423
 * of its own switch, whose two hot-plug capable 2423 ports are the
 * tunnel ports and whose two others hold the xHCI and the NHI. */
static const ULONG cmAsmNhi[] = { CM_PORTS_ID(0x1B21, 0x2425) };
static const ULONG cmAsmPort[] = { CM_PORTS_ID(0x1B21, 0x2423) };
/* The emulator's generic USB4 host (usb4-emu usb4-generic, 1B36:00F4) and
 * its q35 tunnel port, a root port beside it. */
static const ULONG cmEmuNhi[] = { CM_PORTS_ID(0x1B36, 0x00F4) };
static const ULONG cmEmuRoot[] = { CM_PORTS_ID(0x1B36, 0x000C) };

/*
 * The bridge id xhcicmf.inf names that no family carries: QEMU's generic
 * PCI-to-PCI bridge, the emulator's i440FX tunnel bridge, which the SHPC
 * source finds and which is far too common an id to be a family's.
 * (ASMedia's ASM4242 downstream port was here until task 44.40 gave it a
 * family.)
 */
static const ULONG cmUnpaired[] = { CM_PORTS_ID(0x1B36, 0x0001) };

static const CM_FAMILY cmFamilies[] = {
    { "light-ridge", cmLrNhi, CM_N(cmLrNhi), cmLrPort, CM_N(cmLrPort),
      NULL, 0 },
    { "cactus-ridge", cmCrNhi, CM_N(cmCrNhi), cmCrPort, CM_N(cmCrPort),
      NULL, 0 },
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
    { "amd-rembrandt", cmRbNhi, CM_N(cmRbNhi), NULL, 0,
      cmRbRoot, CM_N(cmRbRoot) },
    { "amd-phoenix", cmPhNhi, CM_N(cmPhNhi), NULL, 0,
      cmPhRoot, CM_N(cmPhRoot) },
    { "amd-strix", cmStxNhi, CM_N(cmStxNhi), NULL, 0,
      cmStxRoot, CM_N(cmStxRoot) },
    { "amd-krackan", cmKrkNhi, CM_N(cmKrkNhi), NULL, 0,
      cmKrkRoot, CM_N(cmKrkRoot) },
    { "asmedia-asm4242", cmAsmNhi, CM_N(cmAsmNhi), cmAsmPort,
      CM_N(cmAsmPort), NULL, 0 },
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

BOOLEAN CmPortsAcpiPort(ULONG id)
{
    ULONG f;
    ULONG i;

    for (f = 0; f < CM_N(cmFamilies); f++) {
        if (cmFamilies[f].NhiIdCount == 0 ||
            (cmFamilies[f].NhiIds[0] & 0xFFFFUL) != 0x1022UL) {
            continue;
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

ULONG CmPortsNewGeneration(ULONG fresh, ULONG oldCount, ULONG nowCount)
{
    if (fresh != 0 || nowCount > oldCount) {
        return 1;
    }
    return 0;
}

ULONG CmPortsStaleWhy(ULONG serialChanged, ULONG genChanged,
                      ULONG notReady, ULONG genNow, ULONG genEject)
{
    ULONG why;

    why = 0;
    if (serialChanged) {
        why |= CM_EJ_STALE_SERIAL;
    }
    if (genChanged) {
        why |= CM_EJ_STALE_GEN;
    }
    if (notReady) {
        why |= CM_EJ_STALE_STACK;
    }
    return why | ((genNow & 0xFFUL) << 8) | ((genEject & 0xFFUL) << 16);
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

ULONG CmPortsChildFirst(ULONG prevTruncated, ULONG hasAttached, ULONG isNew,
                        ULONG hasOurs)
{
    if (!isNew) {
        return (!hasOurs && !hasAttached) ? CM_CHILD_ATTACH : CM_CHILD_KEEP;
    }
    if (!hasAttached) {
        return CM_CHILD_ATTACH;
    }
    return prevTruncated ? CM_CHILD_SKIP : CM_CHILD_WALK;
}

/* ---- the tunnel id (record 31) ----------------------------------------- */

/* TRUE when list[from..from+n) is `lit`, ASCII case ignored. */
static BOOLEAN cmWideIs(const USHORT *list, ULONG from, ULONG n,
                        const char *lit)
{
    ULONG i;
    USHORT c;

    for (i = 0; i < n; i++) {
        if (lit[i] == 0) {
            return FALSE;
        }
        c = list[from + i];
        if (c >= 'a' && c <= 'z') {
            c = (USHORT)(c - 'a' + 'A');
        }
        if (c != (USHORT)(UCHAR)cmUpper(lit[i])) {
            return FALSE;
        }
    }
    return (BOOLEAN)(lit[n] == 0);
}

ULONG CmPortsTunnelIds(const USHORT *list, USHORT *out, ULONG outChars,
                       PULONG chars, PULONG kind)
{
    ULONG at;
    ULONG start;
    ULONG total;
    ULONG i;
    BOOLEAN present;
    static const char id[] = CM_TUNNEL_ID;

    *chars = 0;
    *kind = 0;
    if (list == NULL) {
        return CM_TID_LEAVE;
    }
    present = FALSE;
    at = 0;
    for (;;) {
        if (at >= CM_TID_MAX_CHARS) {
            *kind = 0;
            return CM_TID_OVER;
        }
        if (list[at] == 0) {
            break;
        }
        start = at;
        while (at < CM_TID_MAX_CHARS && list[at] != 0) {
            at++;
        }
        if (at >= CM_TID_MAX_CHARS) {
            *kind = 0;
            return CM_TID_OVER;
        }
        if (cmWideIs(list, start, at - start, "PCI\\CC_010802")) {
            *kind |= CM_TID_KIND_NVME;
        }
        if (cmWideIs(list, start, at - start, "PCI\\CC_0604")) {
            *kind |= CM_TID_KIND_BRIDGE;
        }
        if (cmWideIs(list, start, at - start, id)) {
            present = TRUE;
        }
        at++;
    }
    total = at + 1;
    if ((*kind & CM_TID_KIND_NVME) == 0) {
        *chars = total;
        return CM_TID_LEAVE;
    }
    if (present) {
        *chars = total;
        return CM_TID_PRESENT;
    }
    *chars = CM_TID_ID_CHARS + total;
    if (out != NULL && outChars >= *chars) {
        for (i = 0; i < CM_TID_ID_CHARS; i++) {
            out[i] = (USHORT)(UCHAR)id[i];
        }
        for (i = 0; i < total; i++) {
            out[CM_TID_ID_CHARS + i] = list[i];
        }
    }
    return CM_TID_TAG;
}

ULONG CmPortsChainReads(ULONG depth, ULONG isBridge)
{
    return (isBridge && depth < CM_TUNNEL_DEPTH_MAX) ? 1UL : 0UL;
}

ULONG CmPortsMayMark(ULONG located, ULONG locBus, ULONG active,
                     ULONG exclusive, ULONG secBusValid, ULONG secBus,
                     ULONG inMarks, ULONG safeEject)
{
    if (!located || !active || !exclusive || !secBusValid ||
        secBus != locBus || !inMarks) {
        return CM_MM_NO;
    }
    return safeEject ? CM_MM_YES : CM_MM_SEEN;
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

ULONG CmPortsRelease(ULONG markedByThis, ULONG reenumOn)
{
    if (!markedByThis) {
        return 0;
    }
    return CM_PORTS_REL_CLEAR | (reenumOn ? CM_PORTS_REL_INVALIDATE : 0);
}

BOOLEAN CmPortsSettleAgain(ULONG shown, ULONG waited, ULONG elapsedMs,
                           ULONG boundMs)
{
    return (BOOLEAN)(shown < waited && elapsedMs < boundMs);
}

ULONG CmPortsDeferredExpose(ULONG failed, ULONG filterRoute,
                            ULONG transport, ULONG barred)
{
    if (failed || !filterRoute || !transport) {
        return CM_PORTS_DX_NONE;
    }
    return barred ? CM_PORTS_DX_OWE : CM_PORTS_DX_EXPOSE;
}

const ULONG CM_PORTS_HDR_ORDER[CM_PORTS_HDR_WRITES] = {
    0x18UL, 0x1CUL, 0x20UL, 0x24UL, 0x28UL, 0x2CUL, 0x30UL, 0x10UL, 0x14UL,
    0x0CUL, 0x3CUL
};

BOOLEAN CmPortsHdrCopyValid(const ULONG *hdr, ULONG answered)
{
    ULONG vendor;
    ULONG primary;
    ULONG secondary;
    ULONG subordinate;

    if (hdr == NULL || answered != CM_PORTS_HDR_DWORDS) {
        return FALSE;
    }
    vendor = hdr[0] & 0xFFFFUL;
    if (hdr[0] == 0xFFFFFFFFUL || vendor == 0UL || vendor == 0xFFFFUL ||
        vendor == 0x0001UL) {
        return FALSE;
    }
    if (((hdr[0x0C / 4] >> 16) & 0x7FUL) != 1UL) {
        return FALSE;
    }
    primary = hdr[0x18 / 4] & 0xFFUL;
    secondary = (hdr[0x18 / 4] >> 8) & 0xFFUL;
    subordinate = (hdr[0x18 / 4] >> 16) & 0xFFUL;
    if (secondary == 0 || secondary <= primary || subordinate < secondary) {
        return FALSE;
    }
    return (BOOLEAN)((hdr[0x3C / 4] & (1UL << 22)) == 0);
}

BOOLEAN CmPortsHdrEligible(ULONG commandNow, ULONG busesNow)
{
    return (BOOLEAN)(commandNow != 0xFFFFFFFFUL && busesNow != 0xFFFFFFFFUL &&
                     (commandNow & 0x7UL) == 0 &&
                     (busesNow & 0x00FFFF00UL) == 0);
}

BOOLEAN CmPortsRwRestoreDue(ULONG outcome, ULONG done, ULONG elapsedMs)
{
    return (BOOLEAN)(outcome == CM_PORTS_RW_WAIT && done == 0 &&
                     elapsedMs >= CM_PORTS_RW_GRACE_MS);
}

ULONG CmPortsHdrRestoreValue(ULONG offset, ULONG copy, ULONG now)
{
    switch (offset) {
    case 0x04UL:
    case 0x1CUL:
        return copy & 0xFFFFUL;
    case 0x0CUL:
        return (copy & 0xFFFFUL) | (now & 0xFFFF0000UL & ~(1UL << 30));
    case 0x3CUL:
        return copy & ~(1UL << 26);
    default:
        return copy;
    }
}

BOOLEAN CmPortsHdrVerify(ULONG offset, ULONG copy, ULONG readBack)
{
    switch (offset) {
    case 0x04UL:
        return (BOOLEAN)((readBack & 0x7UL) == (copy & 0x7UL));
    case 0x18UL:
        return (BOOLEAN)((readBack & 0x00FFFFFFUL) == (copy & 0x00FFFFFFUL));
    case 0x1CUL:
        return (BOOLEAN)((readBack & 0xFFFFUL) == (copy & 0xFFFFUL));
    case 0x0CUL:
    case 0x3CUL:
        return TRUE;
    default:
        return (BOOLEAN)(readBack == copy);
    }
}

const ULONG CM_PORTS_EP_OFFSET[CM_PORTS_EP_DWORDS] = {
    0x00UL, 0x04UL, 0x0CUL, 0x10UL, 0x14UL, 0x18UL, 0x1CUL, 0x20UL, 0x24UL,
    0x3CUL
};

BOOLEAN CmPortsEpCopyValid(ULONG id, ULONG typeDword)
{
    ULONG vendor;

    vendor = id & 0xFFFFUL;
    if (id == 0xFFFFFFFFUL || vendor == 0UL || vendor == 0xFFFFUL ||
        vendor == 0x0001UL) {
        return FALSE;
    }
    return (BOOLEAN)(((typeDword >> 16) & 0x7FUL) == 0UL);
}

BOOLEAN CmPortsEpSnapValid(const ULONG *ep, ULONG idAgain)
{
    ULONG k;

    if (ep == NULL || !CmPortsEpCopyValid(ep[0], ep[2]) ||
        idAgain != ep[0]) {
        return FALSE;
    }
    for (k = 1; k < CM_PORTS_EP_DWORDS; k++) {
        if (ep[k] == 0xFFFFFFFFUL) {
            return FALSE;
        }
    }
    return TRUE;
}

static ULONG cmPortsBarAddr(ULONG bar)
{
    return (bar & 1UL) ? (bar & ~0x3UL) : (bar & ~0xFUL);
}

BOOLEAN CmPortsEpReset(ULONG cmd, const ULONG *bars)
{
    ULONG k;

    if (bars == NULL || cmd == 0xFFFFFFFFUL || (cmd & 0x7UL) != 0) {
        return FALSE;
    }
    for (k = 0; k < 6; k++) {
        if (bars[k] == 0xFFFFFFFFUL || cmPortsBarAddr(bars[k]) != 0) {
            return FALSE;
        }
        if ((bars[k] & 0x7UL) == 0x4UL) {
            /* A 64-bit memory BAR: its upper half is address, whole. */
            if (k + 1 >= 6 || bars[k + 1] != 0) {
                return FALSE;
            }
            k++;
        }
    }
    return TRUE;
}

BOOLEAN CmPortsEpVerify(ULONG copy, ULONG readBack)
{
    return (BOOLEAN)(readBack != 0xFFFFFFFFUL &&
                     cmPortsBarAddr(readBack) == cmPortsBarAddr(copy));
}

/* ---- record 21 section 6.1.5: a switch behind the tunnel port that came
 * back reset (task 49.30) ------------------------------------------------ */

/* An id that answers: not all ones, its vendor not 0000h, FFFFh or 0001h
 * (Configuration Request Retry Status). */
static BOOLEAN cmPortsIdAnswers(ULONG id)
{
    ULONG vendor;

    vendor = id & 0xFFFFUL;
    return (BOOLEAN)(id != 0xFFFFFFFFUL && vendor != 0UL &&
                     vendor != 0xFFFFUL && vendor != 0x0001UL);
}

BOOLEAN CmPortsSubNoted(ULONG id, ULONG typeDword, ULONG buses)
{
    return (BOOLEAN)(cmPortsIdAnswers(id) && typeDword != 0xFFFFFFFFUL &&
                     ((typeDword >> 16) & 0x7FUL) == 1UL &&
                     buses != 0xFFFFFFFFUL &&
                     ((buses >> 8) & 0xFFUL) != 0);
}

VOID CmPortsSubMerge(CM_PORTS_SUB *note, const CM_PORTS_SUB *fresh)
{
    CM_PORTS_SUB out;
    ULONG k;
    ULONG j;
    ULONG n;
    BOOLEAN again;

    out.Count = 0;
    for (k = 0; k < CM_PORTS_SUB_MAX; k++) {
        out.DevFn[k] = 0;
        out.Id[k] = 0;
    }
    n = fresh->Count < CM_PORTS_SUB_MAX ? fresh->Count : CM_PORTS_SUB_MAX;
    for (k = 0; k < n; k++) {
        out.DevFn[out.Count] = fresh->DevFn[k];
        out.Id[out.Count] = fresh->Id[k];
        out.Count++;
    }
    n = note->Count < CM_PORTS_SUB_MAX ? note->Count : CM_PORTS_SUB_MAX;
    for (k = 0; k < n && out.Count < CM_PORTS_SUB_MAX; k++) {
        again = FALSE;
        for (j = 0; j < out.Count; j++) {
            if (out.DevFn[j] == note->DevFn[k]) {
                again = TRUE;
            }
        }
        if (!again) {
            out.DevFn[out.Count] = note->DevFn[k];
            out.Id[out.Count] = note->Id[k];
            out.Count++;
        }
    }
    *note = out;
}

ULONG CmPortsSubRead(ULONG notedId, ULONG reads, ULONG id, ULONG typeDword,
                     ULONG cmd, ULONG buses, ULONG elapsedMs)
{
    ULONG vendor;

    if ((reads & CM_PORTS_SUB_R_ID) == 0) {
        return CM_PORTS_SUB_UNDECIDED;
    }
    vendor = id & 0xFFFFUL;
    if (id == 0xFFFFFFFFUL || vendor == 0xFFFFUL) {
        return elapsedMs >= CM_PORTS_SUB_ABSENT_MS ? CM_PORTS_SUB_ABSENT
                                                   : CM_PORTS_SUB_UNDECIDED;
    }
    if (vendor == 0x0001UL || vendor == 0UL) {
        return CM_PORTS_SUB_UNDECIDED;
    }
    if (id != notedId) {
        return CM_PORTS_SUB_OTHER;
    }
    if ((reads & CM_PORTS_SUB_R_TYPE) == 0 ||
        (reads & CM_PORTS_SUB_R_CMD) == 0 ||
        (reads & CM_PORTS_SUB_R_BUSES) == 0 || typeDword == 0xFFFFFFFFUL ||
        cmd == 0xFFFFFFFFUL || buses == 0xFFFFFFFFUL) {
        return CM_PORTS_SUB_UNDECIDED;
    }
    if (((typeDword >> 16) & 0x7FUL) != 1UL) {
        return CM_PORTS_SUB_OTHER;
    }
    if (CmPortsHdrEligible(cmd, buses)) {
        return CM_PORTS_SUB_LOST;
    }
    if (((buses >> 8) & 0xFFUL) != 0) {
        return CM_PORTS_SUB_CONFIGURED;
    }
    return CM_PORTS_SUB_UNDECIDED;
}

ULONG CmPortsSubVerdict(const ULONG *entries, ULONG count)
{
    ULONG k;
    ULONG verdict;

    verdict = CM_PORTS_SUBV_SETTLED;
    for (k = 0; k < count; k++) {
        if (entries[k] == CM_PORTS_SUB_LOST) {
            return CM_PORTS_SUBV_LOST;
        }
        if (entries[k] == CM_PORTS_SUB_UNDECIDED) {
            verdict = CM_PORTS_SUBV_UNDECIDED;
        }
    }
    return verdict;
}

ULONG CmPortsResetFn(ULONG reads, ULONG id, ULONG typeDword, ULONG buses)
{
    ULONG vendor;

    if ((reads & CM_PORTS_SUB_R_ID) == 0) {
        return CM_PORTS_RF_UNKNOWN;
    }
    vendor = id & 0xFFFFUL;
    if (id == 0xFFFFFFFFUL || vendor == 0xFFFFUL) {
        return CM_PORTS_RF_ABSENT;
    }
    if (vendor == 0x0001UL || vendor == 0UL ||
        (reads & CM_PORTS_SUB_R_TYPE) == 0 || typeDword == 0xFFFFFFFFUL) {
        return CM_PORTS_RF_UNKNOWN;
    }
    if (((typeDword >> 16) & 0x7FUL) != 1UL) {
        return CM_PORTS_RF_SAFE;
    }
    if ((reads & CM_PORTS_SUB_R_BUSES) == 0 || buses == 0xFFFFFFFFUL) {
        return CM_PORTS_RF_UNKNOWN;
    }
    return ((buses >> 8) & 0xFFUL) != 0 ? CM_PORTS_RF_SAFE : CM_PORTS_RF_BUS0;
}

ULONG CmPortsResetChildOf(const char *text, ULONG chars, ULONG id,
                          ULONG fbdf, ULONG leaf)
{
    ULONG path[CM_PORTS_PATH_MAX];
    ULONG cid;
    ULONG at;
    ULONG n;

    if (text == NULL) {
        return CM_PORTS_RC_UNREAD;
    }
    if (!CmPortsIdVenDev(text, chars, &cid) || cid != id) {
        return CM_PORTS_RC_NOMATCH;
    }
    if (CmPortsIdLocation(text, chars, &at)) {
        return at == fbdf ? CM_PORTS_RC_MATCH : CM_PORTS_RC_NOMATCH;
    }
    if (CmPortsIdPath(text, chars, path, CM_PORTS_PATH_MAX, &n) && n != 0) {
        return path[0] == leaf ? CM_PORTS_RC_MATCH : CM_PORTS_RC_NOMATCH;
    }
    return CM_PORTS_RC_UNREAD;
}

ULONG CmPortsResetChildAdd(ULONG acc, ULONG child)
{
    if (acc == CM_PORTS_RCL_FOUND || child == CM_PORTS_RC_MATCH) {
        return CM_PORTS_RCL_FOUND;
    }
    if (child == CM_PORTS_RC_UNREAD) {
        return CM_PORTS_RCL_UNKNOWN;
    }
    return acc;
}

ULONG CmPortsResetChildEnd(ULONG acc, BOOLEAN complete)
{
    if (acc == CM_PORTS_RCL_FOUND) {
        return CM_PORTS_RCL_FOUND;
    }
    return complete ? acc : CM_PORTS_RCL_UNKNOWN;
}

ULONG CmPortsResetJoin(ULONG verdict, ULONG fn, ULONG children)
{
    if (verdict == CM_PORTS_RU_RESET ||
        (fn == CM_PORTS_RF_BUS0 && children == CM_PORTS_RCL_FOUND)) {
        return CM_PORTS_RU_RESET;
    }
    if (fn == CM_PORTS_RF_UNKNOWN ||
        (fn == CM_PORTS_RF_BUS0 && children != CM_PORTS_RCL_NONE)) {
        return CM_PORTS_RU_UNKNOWN;
    }
    return verdict;
}

ULONG CmPortsResetBare(BOOLEAN complete, ULONG count)
{
    return (complete && count == 0) ? CM_PORTS_RU_SAFE : CM_PORTS_RU_UNKNOWN;
}

ULONG CmPortsLostStep(ULONG outcome, ULONG verdict, ULONG elapsedMs,
                      BOOLEAN tried, ULONG sinceTryMs)
{
    if (verdict == CM_PORTS_SUBV_UNDECIDED) {
        return elapsedMs >= CM_PORTS_RW_BOUND_MS ? CM_PORTS_LOST_END_KEEP
                                                 : CM_PORTS_LOST_WAIT;
    }
    if (verdict != CM_PORTS_SUBV_LOST) {
        return CM_PORTS_LOST_END;
    }
    if (elapsedMs >= CM_PORTS_RW_BOUND_MS) {
        return CM_PORTS_LOST_KEPT;
    }
    if (outcome != CM_PORTS_RW_RESTORED_HERE &&
        elapsedMs < CM_PORTS_RW_GRACE_MS) {
        /* Windows first. */
        return CM_PORTS_LOST_WAIT;
    }
    if (tried && sinceTryMs < CM_PORTS_SUB_RETRY_MS) {
        return CM_PORTS_LOST_WAIT;
    }
    return CM_PORTS_LOST_REMOVE;
}

BOOLEAN CmPortsResumeWaitWanted(BOOLEAN read, ULONG buses)
{
    return (BOOLEAN)(read && buses != 0xFFFFFFFFUL &&
                     ((buses >> 8) & 0xFFUL) != 0);
}

ULONG CmPortsResumeWait(BOOLEAN read, ULONG buses, ULONG elapsedMs,
                        ULONG boundMs)
{
    if (!read || buses == 0xFFFFFFFFUL) {
        return CM_PORTS_RW_UNREAD;
    }
    if (((buses >> 8) & 0xFFUL) != 0) {
        return CM_PORTS_RW_RESTORED;
    }
    if (elapsedMs >= boundMs) {
        return CM_PORTS_RW_TIMEOUT;
    }
    return CM_PORTS_RW_WAIT;
}

VOID CmPortsRwSuspend(CM_PORTS_RWS *s, ULONG mask)
{
    s->Wait = mask;
    s->Asleep = (mask != 0) ? 1UL : 0UL;
    s->Epoch++;
}

BOOLEAN CmPortsRwResumed(CM_PORTS_RWS *s, ULONG now)
{
    if (s->Asleep == 0) {
        return FALSE;
    }
    s->Asleep = 0;
    s->From = now;
    s->Epoch++;
    return TRUE;
}

BOOLEAN CmPortsRwClear(CM_PORTS_RWS *s, ULONG bit, ULONG epoch)
{
    if ((s->Wait & bit) == 0) {
        return TRUE;
    }
    if (s->Epoch != epoch || s->Asleep != 0) {
        return FALSE;
    }
    s->Wait &= ~bit;
    return TRUE;
}

ULONG CmPortsNiAdmit(BOOLEAN open, ULONG wait, ULONG bit)
{
    if (!open) {
        return CM_PORTS_NI_CLOSED;
    }
    if ((wait & bit) != 0) {
        return CM_PORTS_NI_HELD;
    }
    return CM_PORTS_NI_GO;
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

BOOLEAN CmPortsChainCurrent(BOOLEAN leaving, ULONG active, ULONG held,
                            ULONG genNow, ULONG taken, ULONG i)
{
    ULONG bit;

    if (i >= 32UL) {
        return FALSE;
    }
    bit = 1UL << i;
    return (BOOLEAN)(!leaving && genNow == taken && (active & bit) != 0 &&
                     (held & bit) == 0);
}

ULONG CmPortsTagOs(ULONG major, ULONG minor)
{
    return (ULONG)(major == 5UL || (major == 6UL && minor <= 3UL));
}

ULONG CmPortsKeptOs(ULONG major, ULONG minor)
{
    return (ULONG)(major == 6UL && minor == 3UL);
}

ULONG CmPortsKeptStep(ULONG state, ULONG event, PULONG action)
{
    *action = 0;
    if (state == CM_KEPT_DETACHED) {
        /* Detached is final; a removal whose forwarded request deleted the
         * object below still records its departure. */
        if (event == CM_KEPT_E_REMOVED) {
            *action = CM_KEPT_A_DEPART;
        }
        return CM_KEPT_DETACHED;
    }
    switch (event) {
    case CM_KEPT_E_CALLBACK:
        *action = CM_KEPT_A_DETACH | CM_KEPT_A_DROP_ATTACH | CM_KEPT_A_ABSENT;
        return CM_KEPT_DETACHED;
    case CM_KEPT_E_REMOVED:
        if (state != CM_KEPT_LOGICAL) {
            *action = CM_KEPT_A_DEPART | CM_KEPT_A_ABSENT;
        }
        return CM_KEPT_LOGICAL;
    case CM_KEPT_E_START:
        return state == CM_KEPT_LOGICAL ? CM_KEPT_STARTING : state;
    case CM_KEPT_E_START_OK:
        if (state == CM_KEPT_STARTING) {
            /* Presence and the departure's end together: the marks clear
             * a pending departure for any attachment present. */
            *action = CM_KEPT_A_PRESENT | CM_KEPT_A_END_DEPART;
            return CM_KEPT_ATTACHED;
        }
        return state;
    case CM_KEPT_E_START_FAIL:
        return state == CM_KEPT_STARTING ? CM_KEPT_LOGICAL : state;
    default:
        return state;
    }
}

ULONG CmPortsKeptAdmits(ULONG state)
{
    return (ULONG)(state != CM_KEPT_DETACHED);
}

ULONG CmPortsKeptLast(ULONG count, ULONG state, ULONG aboveP)
{
    if (count != 0 || state != CM_KEPT_DETACHED) {
        return CM_KEPT_L_NONE;
    }
    return aboveP ? CM_KEPT_L_QUEUE : CM_KEPT_L_DESTROY;
}

/* ---- the wake power-up (design record 21 section 6.3.1, task 49.46) ---- */

ULONG CmPortsWpuArm(ULONG dispatchEpoch, ULONG epoch, ULONG consumed,
                    ULONG failed)
{
    /* A D0 dispatched before the latest sleep is stale, and one epoch
     * arms once: a later failure in it (the rescan's) never re-arms. */
    return (ULONG)(failed != 0 && epoch != 0 && dispatchEpoch == epoch &&
                   consumed != epoch);
}

ULONG CmPortsWpuDue(ULONG elapsed)
{
    return (ULONG)(elapsed >= CM_WPU_DELAY_MS);
}

static BOOLEAN cmPortsEqiA(const WCHAR *a, const char *b, ULONG n)
{
    ULONG i;
    WCHAR x;
    WCHAR y;

    for (i = 0; i < n; i++) {
        x = a[i];
        y = (WCHAR)(UCHAR)b[i];
        if (x >= 'a' && x <= 'z') {
            x = (WCHAR)(x - ('a' - 'A'));
        }
        if (y >= 'a' && y <= 'z') {
            y = (WCHAR)(y - ('a' - 'A'));
        }
        if (x != y) {
            return FALSE;
        }
    }
    return TRUE;
}

ULONG CmPortsWpuInfSection(const WCHAR *text, ULONG chars)
{
    static const char base[] = "Stor98_Inst";
    static const char x86[] = ".NTx86";
    static const char amd64[] = ".NTamd64";

    if (text == NULL || chars < 11 || !cmPortsEqiA(text, base, 11)) {
        return CM_WPU_OWN_OTHER;
    }
    if (chars == 11 ||
        (chars == 17 && cmPortsEqiA(text + 11, x86, 6)) ||
        (chars == 19 && cmPortsEqiA(text + 11, amd64, 8))) {
        return CM_WPU_OWN_STOR98;
    }
    return CM_WPU_OWN_OTHER;
}

ULONG CmPortsWpuOwner(ULONG infSection, ULONG top)
{
    if (infSection == CM_WPU_OWN_OTHER || top == CM_WPU_OWN_OTHER) {
        return CM_WPU_OWN_OTHER;
    }
    if (infSection == CM_WPU_OWN_STOR98 && top == CM_WPU_OWN_STOR98) {
        return CM_WPU_OWN_STOR98;
    }
    return CM_WPU_OWN_UNKNOWN;
}

VOID CmPortsWpuSysPower(CM_WPU_SYS *s, ULONG isSet, ULONG toSleep)
{
    if (toSleep) {
        s->Halt = 1;
        s->HaltSeq++;
        if (isSet && s->Claim == 0) {
            s->Claim = 1;
            s->Epoch++;
        }
    } else if (isSet) {
        s->Claim = 0;
    }
}

VOID CmPortsWpuSysDown(CM_WPU_SYS *s)
{
    s->DownSeq++;
}

VOID CmPortsWpuDomStop(CM_WPU_DOM *d)
{
    d->StopLatch = 1;
}

VOID CmPortsWpuDomCancel(CM_WPU_DOM *d)
{
    d->StopLatch = 0;
}

VOID CmPortsWpuDomStart(CM_WPU_DOM *d)
{
    d->StopLatch = 0;
    d->PowerDown = 0;
    d->ResumeEpoch = 0;
    d->ResumeHaltSeq = 0;
    d->Resuming = 0;
    d->RelDownSeq = 0;
    d->RelHaltSeq = 0;
}

VOID CmPortsWpuDomPowerDown(CM_WPU_DOM *d)
{
    d->PowerDown = 1;
}

VOID CmPortsWpuDomResumeBegin(const CM_WPU_SYS *s, CM_WPU_DOM *d)
{
    d->ResumeEpoch = s->Epoch;
    d->ResumeHaltSeq = s->HaltSeq;
    d->Resuming = 1;
}

ULONG CmPortsWpuDomRelease(CM_WPU_SYS *s, CM_WPU_DOM *d)
{
    /* Only the release of this domain's own resume, with no sleep and no
     * sleep query since that resume began: a release that started before
     * a later sleep never adopts it, and a latched domain stays closed. */
    if (!d->Resuming || d->StopLatch || s->Epoch == 0 ||
        d->ResumeEpoch != s->Epoch || d->ResumeHaltSeq != s->HaltSeq) {
        return 0;
    }
    s->Halt = 0;
    d->PowerDown = 0;
    d->RelDownSeq = s->DownSeq;
    d->RelHaltSeq = s->HaltSeq;
    return 1;
}

ULONG CmPortsWpuDomOpen(const CM_WPU_SYS *s, const CM_WPU_DOM *d,
                        ULONG relEpoch)
{
    return (ULONG)(s->Epoch != 0 && !s->Halt && !d->StopLatch &&
                   !d->PowerDown && relEpoch == s->Epoch &&
                   d->RelDownSeq == s->DownSeq && d->RelHaltSeq == s->HaltSeq);
}

ULONG CmPortsWpuStep(const CM_WPU_IN *in, PULONG why)
{
    ULONG late;

    *why = 0;
    if (in->Epoch == 0 || in->FailEpoch != in->Epoch ||
        in->Consumed == in->Epoch) {
        return CM_WPU_V_IDLE;
    }
    if (in->Switch == 0) {
        *why = CM_WPU_WHY_OFF;
        return CM_WPU_V_SKIP;
    }
    if (in->Closed) {
        *why = CM_WPU_WHY_REMOVED;
        return CM_WPU_V_SKIP;
    }
    if (!in->Released || !CmPortsWpuDue(in->Elapsed)) {
        /* No release in this epoch yet (or the domain is suspended or
         * stopped): nothing is consumed; the next sleep ends the arm. */
        return CM_WPU_V_WAIT;
    }
    if (in->Owner == CM_WPU_OWN_OTHER) {
        *why = CM_WPU_WHY_NOT_STOR98;
        return CM_WPU_V_SKIP;
    }
    if (in->Outstanding) {
        /* An earlier request for its unit never completed: detected, not
         * cancelled, and never followed by a second one. */
        *why = CM_WPU_WHY_OUTSTANDING;
        return CM_WPU_V_SKIP;
    }
    late = (ULONG)(in->Elapsed > CM_WPU_WINDOW_MS);
    if (in->Units == 0) {
        return late ? CM_WPU_V_NONE : CM_WPU_V_WAIT;
    }
    if (in->Owner != CM_WPU_OWN_STOR98) {
        /* Not shown to be Stor98's: fails closed at the window's end. */
        if (late) {
            *why = CM_WPU_WHY_NOT_STOR98;
            return CM_WPU_V_SKIP;
        }
        return CM_WPU_V_WAIT;
    }
    if (in->Slots < in->Units) {
        if (late) {
            *why = CM_WPU_WHY_BUSY;
            return CM_WPU_V_SKIP;
        }
        return CM_WPU_V_WAIT;
    }
    return CM_WPU_V_ASK;
}
