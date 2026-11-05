/*
 * cm_oscp.c - the ASM4242 handover's known-board route on NT, its pure
 * half (roadmap-usb4.md task 46.12; design record 32 sections 12.8.15.11
 * and 12.8.15.13). See cm_oscp.h.
 *
 * DDK-free. C89, no allocation, no lock. IRQL: any.
 */

#include "cm_oscp.h"

#define CM_OSCP_STR_MAX     64UL
#define CM_OSCP_WNODE_ALL   64UL    /* WNODE_ALL_DATA through one pair */
#define CM_OSCP_F_FIXED     0x10UL  /* WNODE_FLAG_FIXED_INSTANCE_SIZE  */

/*
 * The boards read on a bench. Each string is held as its hash
 * (CmOscpHash), so no board is named here.
 *
 * 1. The owner's bench board (record 32 section 12.8.15.1): the T10 test
 *    image's reading of 2026-11-04 on 10 x64, the data byte that of the
 *    98 SE bench of 2026-11-02.
 */
const CM_OSCP_PROFILE CmOscpProfiles[] = {
    { { 0x14DB1022UL, 0x24211B21UL, 0x24231B21UL, 0x24251B21UL,
        0x24211B21UL },
      0xE1ADF17FUL, 0xD09C239EUL, 0x1EA8C1BBUL, 0x00UL },
/* The installer compiles this file under XHCI_HOST_TEST for its types
 * and with CM_OSCP_RELEASE, so it holds the release driver's profiles
 * alone (record 32 section 12.8.15.12). */
#if defined(XHCI_FLAVOUR_QEMU) || \
    (defined(XHCI_HOST_TEST) && !defined(CM_OSCP_RELEASE))
    /* The usb4-emu model under q35 (its root port 1B36:000C), given
     * -smbios type=2,manufacturer=usb4-emu,product=asm4242-model, SeaBIOS
     * its BIOS vendor. */
    { { 0x000C1B36UL, 0x24211B21UL, 0x24231B21UL, 0x24251B21UL,
        0x24211B21UL },
      0x107BA1E3UL, 0x3247844BUL, 0xA79C5F6CUL, 0x00UL },
#endif
};
const ULONG CmOscpProfileCount =
    sizeof(CmOscpProfiles) / sizeof(CmOscpProfiles[0]);

static ULONG cmOscpGet32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

ULONG CmOscpHash(const UCHAR *s, ULONG n)
{
    ULONG h;
    ULONG a;
    ULONG b;

    a = 0;
    b = n;
    while (a < b && s[a] == ' ') {
        a++;
    }
    while (b > a && s[b - 1] == ' ') {
        b--;
    }
    h = 0x811C9DC5UL;
    for (; a < b; a++) {
        h ^= s[a];
        h *= 0x01000193UL;
    }
    return h;
}

/* `n` bytes at `off` inside `len`, without wrapping. */
static BOOLEAN cmOscpIn(ULONG off, ULONG n, ULONG len)
{
    return (BOOLEAN)(off <= len && n <= len - off);
}

ULONG CmOscpWmiRaw(const UCHAR *buf, ULONG len, const UCHAR **tables,
                   PULONG tlen)
{
    ULONG node;
    ULONG size;
    ULONG next;
    ULONG count;
    ULONG flags;
    ULONG instances;
    ULONG dataOff;
    ULONG dataLen;
    ULONG found;
    ULONG raw;

    node = 0;
    instances = 0;
    found = 0;
    dataOff = 0;
    dataLen = 0;
    for (;;) {
        if (!cmOscpIn(node, CM_OSCP_WNODE_ALL, len)) {
            return CM_OSCP_WNODE_SHORT;
        }
        size = cmOscpGet32(buf + node);
        next = cmOscpGet32(buf + node + 12);
        flags = cmOscpGet32(buf + node + 44);
        count = cmOscpGet32(buf + node + 52);
        if (size < CM_OSCP_WNODE_ALL || !cmOscpIn(node, size, len)) {
            return CM_OSCP_WNODE_SHORT;
        }
        if (count > 1UL || instances + count > 1UL) {
            return CM_OSCP_WNODE_COUNT;
        }
        if (count == 1UL) {
            found = node;
            if ((flags & CM_OSCP_F_FIXED) != 0) {
                dataOff = cmOscpGet32(buf + node + 48);
                dataLen = cmOscpGet32(buf + node + 60);
            } else {
                /* OffsetInstanceDataAndLength[0]: the offset at 60, the
                 * length at 64; the node must hold both. */
                if (size < 68UL) {
                    return CM_OSCP_WNODE_SHORT;
                }
                dataOff = cmOscpGet32(buf + node + 60);
                dataLen = cmOscpGet32(buf + node + 64);
            }
            if (!cmOscpIn(dataOff, dataLen, size)) {
                return CM_OSCP_WNODE_DATA;
            }
        }
        instances += count;
        if (next == 0) {
            break;
        }
        if (next < CM_OSCP_WNODE_ALL || node > 0xFFFFFFFFUL - next ||
            !cmOscpIn(node + next, CM_OSCP_WNODE_ALL, len)) {
            return CM_OSCP_WNODE_LINK;
        }
        node += next;
    }
    if (instances != 1UL) {
        return CM_OSCP_WNODE_COUNT;
    }
    if (dataLen < 8UL) {
        return CM_OSCP_RAW_LENGTH;
    }
    raw = cmOscpGet32(buf + found + dataOff + 4);
    if (raw > dataLen - 8UL) {
        return CM_OSCP_RAW_LENGTH;
    }
    *tables = buf + found + dataOff + 8;
    *tlen = raw;
    return CM_OSCP_OK;
}

/*
 * String `index` of the structure at `s` (formatted length `fl`), its
 * string set `setLen` bytes after it: its hash in *h, CM_OSCP_OK.
 */
static ULONG cmOscpString(const UCHAR *s, ULONG fl, ULONG setLen,
                          UCHAR index, PULONG h)
{
    ULONG at;
    ULONG n;
    ULONG k;

    if (index == 0) {
        return CM_OSCP_SM_INDEX;
    }
    at = fl;
    for (k = 1; ; k++) {
        if (at >= fl + setLen || s[at] == 0) {
            return CM_OSCP_SM_INDEX;
        }
        n = 0;
        while (at + n < fl + setLen && s[at + n] != 0) {
            n++;
        }
        if (k == index) {
            *h = CmOscpHash(s + at, n);
            return CM_OSCP_OK;
        }
        at += n + 1UL;
    }
}

/* Every string of the set (`setLen` bytes after the formatted area) at
 * most CM_OSCP_STR_MAX bytes. */
static BOOLEAN cmOscpSetShort(const UCHAR *s, ULONG fl, ULONG setLen)
{
    ULONG at;
    ULONG n;

    at = fl;
    n = 0;
    while (at < fl + setLen) {
        if (s[at] == 0) {
            n = 0;
        } else if (++n > CM_OSCP_STR_MAX) {
            return FALSE;
        }
        at++;
    }
    return TRUE;
}

ULONG CmOscpSmbios(const UCHAR *t, ULONG len, CM_OSCP_SMBIOS *out)
{
    ULONG at;
    ULONG fl;
    ULONG set;
    ULONG seen0;
    ULONG seen2;
    ULONG r;
    UCHAR type;
    const UCHAR *s;

    at = 0;
    seen0 = 0;
    seen2 = 0;
    out->BiosVendor = 0;
    out->BiosVersion = 0;
    out->BiosDate = 0;
    out->BoardMaker = 0;
    out->BoardProduct = 0;
    while (at < len) {
        if (!cmOscpIn(at, 4UL, len)) {
            return CM_OSCP_SM_STRUCT;
        }
        s = t + at;
        type = s[0];
        fl = s[1];
        if (fl < 4UL || !cmOscpIn(at, fl, len)) {
            return CM_OSCP_SM_STRUCT;
        }
        /* The string set: up to and including the double NUL. */
        set = 0;
        for (;;) {
            if (!cmOscpIn(at + fl, set + 2UL, len)) {
                return CM_OSCP_SM_STRINGS;
            }
            if (s[fl + set] == 0 && s[fl + set + 1] == 0) {
                break;
            }
            set++;
        }
        if (type == 0) {
            if (seen0++ != 0) {
                return CM_OSCP_SM_TWICE;
            }
            if (fl < 9UL) {
                return CM_OSCP_SM_FIELD;
            }
            if (!cmOscpSetShort(s, fl, set + 1UL)) {
                return CM_OSCP_SM_LONG;
            }
            r = cmOscpString(s, fl, set + 1UL, s[4], &out->BiosVendor);
            if (r == CM_OSCP_OK) {
                r = cmOscpString(s, fl, set + 1UL, s[5], &out->BiosVersion);
            }
            if (r == CM_OSCP_OK) {
                r = cmOscpString(s, fl, set + 1UL, s[8], &out->BiosDate);
            }
            if (r != CM_OSCP_OK) {
                return r;
            }
        } else if (type == 2) {
            if (seen2++ != 0) {
                return CM_OSCP_SM_TWICE;
            }
            if (fl < 6UL) {
                return CM_OSCP_SM_FIELD;
            }
            if (!cmOscpSetShort(s, fl, set + 1UL)) {
                return CM_OSCP_SM_LONG;
            }
            r = cmOscpString(s, fl, set + 1UL, s[4], &out->BoardMaker);
            if (r == CM_OSCP_OK) {
                r = cmOscpString(s, fl, set + 1UL, s[5], &out->BoardProduct);
            }
            if (r != CM_OSCP_OK) {
                return r;
            }
        } else if (type == 127) {
            break;
        }
        at += fl + set + 2UL;
    }
    if (seen0 != 1 || seen2 != 1) {
        return CM_OSCP_SM_MISSING;
    }
    return CM_OSCP_OK;
}

ULONG CmOscpMatch(const CM_OSCP_PCI *pci, const CM_OSCP_SMBIOS *sm,
                  PULONG data)
{
    ULONG i;
    const CM_OSCP_PROFILE *p;

    for (i = 0; i < CmOscpProfileCount; i++) {
        p = &CmOscpProfiles[i];
        if (p->Pci.RootId == pci->RootId && p->Pci.UspId == pci->UspId &&
            p->Pci.DspId == pci->DspId && p->Pci.NhiId == pci->NhiId &&
            p->Pci.NhiSubsystem == pci->NhiSubsystem &&
            p->BiosVendor == sm->BiosVendor &&
            p->BoardMaker == sm->BoardMaker &&
            p->BoardProduct == sm->BoardProduct) {
            *data = p->Data;
            return i + 1UL;
        }
    }
    return 0;
}

ULONG CmOscpMemberRole(ULONG vendor, ULONG device, BOOLEAN bridge,
                       BOOLEAN pciAbove, BOOLEAN anyAbove)
{
    ULONG id;

    id = (vendor & 0xFFFFUL) | ((device & 0xFFFFUL) << 16);
    if (id == CM_OSCP_ID_USP && bridge && pciAbove) {
        return CM_OSCP_R_USP;
    }
    if (id == CM_OSCP_ID_DSP && bridge) {
        return CM_OSCP_R_DSP;
    }
    if (id == CM_OSCP_ID_XHCI && !bridge && anyAbove) {
        return CM_OSCP_R_XHCI;
    }
    return CM_OSCP_R_NONE;
}

BOOLEAN CmOscpCovered(const ULONG *wantBdf, const ULONG *wantRole, ULONG n,
                      const CM_OSCP_MEMBER *have, ULONG haveCount,
                      PULONG missing)
{
    ULONG i;
    ULONG k;
    BOOLEAN ok;

    for (i = 0; i < n; i++) {
        ok = FALSE;
        for (k = 0; k < haveCount && !ok; k++) {
            ok = (BOOLEAN)(have[k].Role == wantRole[i] &&
                           have[k].Located && have[k].Bdf == wantBdf[i] &&
                           have[k].Started && !have[k].Closed &&
                           !have[k].Gone);
        }
        if (!ok) {
            *missing = i;
            return FALSE;
        }
    }
    return TRUE;
}

ULONG CmOscpOwed(ULONG owed, ULONG state)
{
    if (state == 5UL) {
        return 1;
    }
    if (state >= 2UL && state <= 4UL) {
        return 0;
    }
    return owed;
}

ULONG CmOscgAtBoot(ULONG marker)
{
    if (marker == CM_OSCG_M_ABSENT) {
        return CM_OSCG_GO;
    }
    if (marker == CM_OSCG_M_PRESENT) {
        return CM_OSCG_SKIP_CLEAR;
    }
    return CM_OSCG_SKIP;
}

BOOLEAN CmOscgClears(ULONG event)
{
    return (BOOLEAN)(event == CM_OSCG_E_ROUTER ||
                     event == CM_OSCG_E_SHUTDOWN);
}

BOOLEAN CmOscgMaySend(BOOLEAN latched, BOOLEAN written, BOOLEAN flushed)
{
    return (BOOLEAN)(!latched && written && flushed);
}

ULONG CmOscgCleared(BOOLEAN deleted, BOOLEAN flushed)
{
    if (!deleted) {
        return CM_OSCG_C_KEPT;
    }
    return flushed ? CM_OSCG_C_DONE : CM_OSCG_C_LATCH;
}

/* ---- the read path (record 32 section 12.8.15.17) ------------------- */

BOOLEAN CmOscpRangeHit(const CM_OSCP_RANGE *r, ULONG n, ULONG phys,
                       ULONG bytes)
{
    ULONG lo;
    ULONG hi;
    ULONG endLo;
    ULONG endHi;
    ULONG i;

    if (bytes == 0 || phys > 0xFFFFFFFFUL - (bytes - 1UL)) {
        return TRUE;
    }
    /* The pages spanned: [lo, hi], hi the last byte of the last page. */
    lo = phys & ~0xFFFUL;
    hi = (phys + (bytes - 1UL)) | 0xFFFUL;
    for (i = 0; i < n; i++) {
        if (r[i].BaseHi != 0) {
            continue;
        }
        if (r[i].BytesLo == 0 && r[i].BytesHi == 0) {
            continue;
        }
        /* The range's last byte, held to 4 GB: base + bytes - 1. */
        endHi = r[i].BytesHi;
        endLo = r[i].BaseLo + r[i].BytesLo;
        if (endLo < r[i].BaseLo) {
            endHi++;
        }
        if (endLo == 0) {
            endHi--;
        }
        endLo--;
        if (endHi != 0) {
            endLo = 0xFFFFFFFFUL;
        }
        if (r[i].BaseLo <= hi && endLo >= lo) {
            return TRUE;
        }
    }
    return FALSE;
}

ULONG CmOscpReadRoute(ULONG have, PULONG need)
{
    *need = 0;
    if ((have & CM_OSCP_HAVE_EX2) != 0) {
        *need = CM_OSCP_HAVE_EX2 | CM_OSCP_HAVE_FWENV;
        return CM_OSCP_RT_B;
    }
    if ((have & CM_OSCP_SYS_130) != 0) {
        *need = CM_OSCP_HAVE_RANGES | CM_OSCP_HAVE_QUERY;
        return CM_OSCP_RT_C;
    }
    if ((have & CM_OSCP_SYS_X86) != 0) {
        *need = CM_OSCP_HAVE_RANGES;
        return CM_OSCP_RT_D;
    }
    return CM_OSCP_RT_NONE;
}

ULONG CmOscpAfterB(ULONG boot, ULONG scan)
{
    if (boot == CM_OSCP_S_ABSENT) {
        return CM_OSCP_RT_C;
    }
    if (boot != CM_OSCP_S_OBTAINED) {
        return CM_OSCP_RT_NONE;
    }
    if (scan == CM_OSCP_S_OBTAINED) {
        return CM_OSCP_RT_B;
    }
    if (scan == CM_OSCP_S_ABSENT) {
        return CM_OSCP_RT_C;
    }
    return CM_OSCP_RT_NONE;
}

ULONG CmOscpFwTable(BOOLEAN ok, BOOLEAN notFound, ULONG returned, ULONG cap,
                    const UCHAR *table)
{
    ULONG length;
    ULONG sum;
    ULONG i;

    if (!ok) {
        return notFound ? CM_OSCP_S_ABSENT : CM_OSCP_S_REFUSED;
    }
    if (returned < 36UL || returned > cap || table == NULL) {
        return CM_OSCP_S_REFUSED;
    }
    length = (ULONG)table[4] | ((ULONG)table[5] << 8) |
             ((ULONG)table[6] << 16) | ((ULONG)table[7] << 24);
    if (length != returned) {
        return CM_OSCP_S_REFUSED;
    }
    sum = 0;
    for (i = 0; i < length; i++) {
        sum += table[i];
    }
    return ((sum & 0xFFUL) == 0) ? CM_OSCP_S_OBTAINED : CM_OSCP_S_REFUSED;
}

BOOLEAN CmOscpSystem(BOOLEAN haveVer, ULONG build, BOOLEAN nt5)
{
    if (haveVer) {
        return (BOOLEAN)(build < 22000UL);
    }
    return (BOOLEAN)!nt5;
}

ULONG CmOscpBoardNext(ULONG kept, BOOLEAN reads, BOOLEAN latched,
                      BOOLEAN probed, ULONG flush)
{
    return CmOscpBoardNextEx(kept, reads, latched, FALSE, FALSE, probed,
                             flush);
}

ULONG CmOscpBoardNextEx(ULONG kept, BOOLEAN reads, BOOLEAN latched,
                        BOOLEAN ecam, BOOLEAN ecamKept, BOOLEAN probed,
                        ULONG flush)
{
    if (kept != CM_OSCP_K_UNKNOWN && kept != CM_OSCP_K_SEND) {
        return CM_OSCP_B_MATCH;
    }
    if (kept == CM_OSCP_K_UNKNOWN && !reads) {
        return CM_OSCP_B_UNKNOWN;
    }
    if (ecam && !reads) {
        return CM_OSCP_B_NOECAM;
    }
    if (latched) {
        return CM_OSCP_B_LATCHED;
    }
    if (ecam && flush == CM_OSCP_F_UNTESTED) {
        /* The key's test before the ECAM base's firmware reads. */
        return CM_OSCP_B_FLUSH;
    }
    if (ecam && flush != CM_OSCP_F_OK) {
        return CM_OSCP_B_DEFER;
    }
    if (ecam && !ecamKept) {
        return CM_OSCP_B_ECAM;
    }
    if (!probed) {
        return CM_OSCP_B_PROBE;
    }
    if (flush == CM_OSCP_F_UNTESTED) {
        return CM_OSCP_B_FLUSH;
    }
    if (flush != CM_OSCP_F_OK) {
        return CM_OSCP_B_DEFER;
    }
    return (kept == CM_OSCP_K_UNKNOWN) ? CM_OSCP_B_READ : CM_OSCP_B_SEND;
}

BOOLEAN CmOscpKept(const CM_OSCP_KEPT *k, const CM_OSCP_KEPT *now)
{
    if (k->State == CM_OSCP_K_NONE) {
        return TRUE;
    }
    return (BOOLEAN)(k->NhiBdf == now->NhiBdf && k->DspBdf == now->DspBdf &&
                     k->UspBdf == now->UspBdf &&
                     k->RootBdf == now->RootBdf &&
                     k->Pci.RootId == now->Pci.RootId &&
                     k->Pci.UspId == now->Pci.UspId &&
                     k->Pci.DspId == now->Pci.DspId &&
                     k->Pci.NhiId == now->Pci.NhiId &&
                     k->Pci.NhiSubsystem == now->Pci.NhiSubsystem);
}

ULONG CmOscpProbe(BOOLEAN held, BOOLEAN usable, BOOLEAN aborted,
                  BOOLEAN route, BOOLEAN d0, BOOLEAN aliased, BOOLEAN mbRead,
                  ULONG mb, PULONG why)
{
    return CmOscpProbeEx(held, usable, aborted, route, d0, 0, aliased,
                         mbRead, mb, why);
}

ULONG CmOscpProbeEx(BOOLEAN held, BOOLEAN usable, BOOLEAN aborted,
                    BOOLEAN route, BOOLEAN d0, ULONG ecamWhy,
                    BOOLEAN aliased, BOOLEAN mbRead, ULONG mb, PULONG why)
{
    if (!held) {
        *why = 0x10EUL;
        return CM_OSCP_P_DEFER;
    }
    if (!usable) {
        *why = 0x10AUL;
        return CM_OSCP_P_DEFER;
    }
    if (aborted) {
        *why = 0x10BUL;
        return CM_OSCP_P_DEFER;
    }
    if (!route) {
        *why = 0x10BUL;
    } else if (!d0) {
        *why = 0x104UL;
    } else if (ecamWhy != 0) {
        *why = ecamWhy;
    } else if (!mbRead || mb == 0xFFFFFFFFUL) {
        *why = 0x109UL;
    } else if (aliased) {
        *why = 0x112UL;
    } else {
        *why = 0;
    }
    return *why;
}

BOOLEAN CmOscpAliased(ULONG d000, ULONG d100, ULONG d020, ULONG d420)
{
    return (BOOLEAN)(d100 == d000 || d420 == d020);
}

BOOLEAN CmOscpExtAllowed(BOOLEAN haveVer)
{
    return haveVer;
}

BOOLEAN CmOscpEcamSystem(BOOLEAN haveVer, ULONG major, ULONG minor,
                         BOOLEAN amd64)
{
    if (!haveVer) {
        return TRUE;
    }
    if (major != 5UL) {
        return FALSE;
    }
    return (BOOLEAN)((minor == 1UL && !amd64) || (minor == 2UL && amd64));
}

BOOLEAN CmOscpEcamOffset(ULONG offset, BOOLEAN write)
{
    if (offset == 0x420UL || offset == 0x430UL) {
        return TRUE;
    }
    if (write) {
        return FALSE;
    }
    return (BOOLEAN)(offset == 0UL || offset == 0x08UL || offset == 0x18UL ||
                     offset == 0x20UL || offset == 0x100UL);
}

ULONG CmOscpEcamCheck(const ULONG *route, const ULONG *ecam, ULONG sec,
                      ULONG d100, ULONG d420)
{
    ULONG i;

    if (ecam[0] != CM_OSCP_ID_USP) {
        return CM_OSCP_E_ID;
    }
    for (i = 0; i < 4UL; i++) {
        if (route[i] != ecam[i]) {
            return CM_OSCP_E_DIFFERS;
        }
    }
    if ((ecam[1] >> 16) != 0x0604UL) {
        return CM_OSCP_E_CLASS;
    }
    if (((ecam[2] >> 8) & 0xFFUL) != sec) {
        return CM_OSCP_E_BUS;
    }
    if (CmOscpAliased(ecam[0], d100, ecam[3], d420)) {
        return CM_OSCP_E_ALIAS;
    }
    return CM_OSCP_E_OK;
}
