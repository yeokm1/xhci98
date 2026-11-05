/*
 * inst_board.c - an ASM4242 board's identity as Setup reads it (roadmap-
 * usb4.md task 46.12 on NT; design record 32 section 12.8.15.12). See
 * inst_board.h.
 *
 * Compiled with XHCI_HOST_TEST, as the host suites compile the driver's
 * pure files, for cm_oscp.h's types; and in the installer with
 * CM_OSCP_RELEASE, so cm_oscp.c holds the profiles of the release driver
 * and no emulator's.
 */

#include <string.h>
#include "inst_core.h"
#include "inst_board.h"
#include "../src/cm/cm_oscp.h"

/* The subsystem of a PCI id ("PCI\...&SUBSYS_ssssvvvv..." before the
 * instance's "\"), as the function's dword 2Ch holds it, or 0 when it
 * names none. */
static ULONG instBoardSubsys(const char *id)
{
    static const char key[] = "&SUBSYS_";
    const char *p;
    ULONG v = 0;
    int i;

    for (p = id + 4; *p != '\0' && *p != '\\'; p++) {
        if (strncmp(p, key, sizeof(key) - 1) != 0) {
            continue;
        }
        p += sizeof(key) - 1;
        for (i = 0; i < 8; i++) {
            char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= (ULONG)(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                v |= (ULONG)(c - 'A' + 10);
            } else if (c >= 'a' && c <= 'f') {
                v |= (ULONG)(c - 'a' + 10);
            } else {
                return 0;
            }
        }
        return v;
    }
    return 0;
}

static int instBoardId(const char *id, ULONG *out)
{
    INST_U32 vd;

    if (id == NULL || !inst_pci_vendev(id, &vd)) {
        return 0;
    }
    *out = (ULONG)vd;
    return 1;
}

int inst_board_nhi(const char *id)
{
    INST_U32 vd;

    return id != NULL && inst_pci_vendev(id, &vd) &&
           (ULONG)vd == CM_OSCP_ID_NHI;
}

static int instBoardRank(int c)
{
    switch (c) {
    case INST_BOARD_UNKNOWN:
        return 5;
    case INST_BOARD_PENDING:
        return 4;
    case INST_BOARD_LATER:
        return 3;
    case INST_BOARD_UNREAD:
        return 2;
    case INST_BOARD_KNOWN:
    case INST_BOARD_HELPER:
        return 1;
    default:
        return 0;
    }
}

int inst_board_worse(int a, int b)
{
    return instBoardRank(b) > instBoardRank(a) ? b : a;
}

int inst_board_query(int nt, int tbIn, int asmHost)
{
    return nt && tbIn && asmHost;
}

int inst_board_reads(int nt, unsigned long major, unsigned long minor)
{
    return nt && !(major == 5UL && minor == 0UL);
}

int inst_board_sendable(int x64, int usbInstalls, int cmInstalls,
                        int helperNow, int helperCmp)
{
    return x64 || usbInstalls || cmInstalls || (helperNow && helperCmp >= 0);
}

int inst_board_hlpr_cmp(unsigned long v1, unsigned long v2, unsigned long v3,
                        unsigned long v4)
{
    INST_U32 v[4];
    INST_U32 first[4];

    v[0] = v1;
    v[1] = v2;
    v[2] = v3;
    v[3] = v4;
    first[0] = INST_HLPR_ROUTINE_V1;
    first[1] = INST_HLPR_ROUTINE_V2;
    first[2] = INST_HLPR_ROUTINE_V3;
    first[3] = INST_HLPR_ROUTINE_V4;
    return inst_ver_cmp(v, first) >= 0 ? 0 : -1;
}

int inst_board_run_installs(int present, int refused, int runs, int held)
{
    return present && !refused && runs && !held;
}

int inst_board_shown(int tbIn, int nhiCount)
{
    return tbIn && nhiCount > 0;
}

int inst_board_class(const char *rootId, const char *uspId,
                     const char *dspId, const char *nhiId, int route,
                     const unsigned char *rsmb, unsigned long rsmbLen,
                     unsigned long *why, unsigned long *profile)
{
    CM_OSCP_PCI pci;
    CM_OSCP_SMBIOS sm;
    ULONG raw;
    ULONG r;
    ULONG data;
    ULONG n;

    *why = INST_BOARD_W_OK;
    *profile = 0;
    if (route == INST_BOARD_R_NONE) {
        *why = INST_BOARD_W_ROUTE;
        return INST_BOARD_UNREAD;
    }
    /* What Setup could not read leaves the board unchecked, not unknown:
     * the driver reads by its own routes (record 32 section 12.8.15.12). */
    if (!instBoardId(rootId, &pci.RootId) ||
        !instBoardId(uspId, &pci.UspId) ||
        !instBoardId(dspId, &pci.DspId) ||
        !instBoardId(nhiId, &pci.NhiId)) {
        *why = INST_BOARD_W_ID;
        return INST_BOARD_UNREAD;
    }
    pci.NhiSubsystem = instBoardSubsys(nhiId);
    if (route == INST_BOARD_R_PENDING) {
        return INST_BOARD_PENDING;
    }
    if (route == INST_BOARD_R_LATER) {
        return INST_BOARD_LATER;
    }
    if (route != INST_BOARD_R_READ || rsmb == NULL) {
        *why = INST_BOARD_W_READ;
        return INST_BOARD_UNREAD;
    }
    /* The firmware table routine's RawSMBIOSData: the block's four bytes,
     * a 32-bit length, then that many table bytes (record 32 section
     * 12.8.15.11, the instance's data as WMI gives it). */
    if (rsmbLen < 8UL) {
        *why = INST_BOARD_W_LENGTH;
        return INST_BOARD_UNKNOWN;
    }
    raw = (ULONG)rsmb[4] | ((ULONG)rsmb[5] << 8) | ((ULONG)rsmb[6] << 16) |
          ((ULONG)rsmb[7] << 24);
    if (raw > rsmbLen - 8UL) {
        *why = INST_BOARD_W_LENGTH;
        return INST_BOARD_UNKNOWN;
    }
    r = CmOscpSmbios(rsmb + 8, raw, &sm);
    if (r != CM_OSCP_OK) {
        *why = r;
        return INST_BOARD_UNKNOWN;
    }
    n = CmOscpMatch(&pci, &sm, &data);
    if (n == 0) {
        *why = INST_BOARD_W_MATCH;
        return INST_BOARD_UNKNOWN;
    }
    *profile = n;
    return INST_BOARD_KNOWN;
}
