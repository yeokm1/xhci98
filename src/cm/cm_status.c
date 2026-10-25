/*
 * cm_status.c - the status record's builder (cm_statb.h, cm_status.h;
 * design record 24 section 3.1). Written for this project.
 *
 * What is never copied: the ICM's Key, any approval or challenge field,
 * and all but the last 16 bits of a UUID (section 3.1: enough to tell two
 * identical devices apart, not enough to stand for the UUID).
 *
 * IRQL: any; no lock, no DDK call.
 */

#include "cm_statb.h"

static VOID cmStPut(UCHAR *buf, ULONG offset, ULONG value)
{
    buf[offset] = (UCHAR)(value & 0xFFUL);
    buf[offset + 1] = (UCHAR)((value >> 8) & 0xFFUL);
    buf[offset + 2] = (UCHAR)((value >> 16) & 0xFFUL);
    buf[offset + 3] = (UCHAR)((value >> 24) & 0xFFUL);
}

static VOID cmStZero(UCHAR *buf, ULONG n)
{
    while (n-- != 0) {
        *buf++ = 0;
    }
}

/* A name cut to CM_ST_NAME_BYTES - 1 characters of printable ASCII, a
 * character outside 0x20..0x7E written as '?', NUL-terminated and padded. */
static VOID cmStName(UCHAR *dst, const char *src, ULONG srcBytes)
{
    ULONG i;
    UCHAR c;

    cmStZero(dst, CM_ST_NAME_BYTES);
    for (i = 0; i < CM_ST_NAME_BYTES - 1 && i < srcBytes; i++) {
        c = (UCHAR)src[i];
        if (c == 0) {
            break;
        }
        dst[i] = (c >= 0x20 && c <= 0x7E) ? c : (UCHAR)'?';
    }
}

static ULONG cmStShortId(BOOLEAN valid, const ULONG *uuid)
{
    if (!valid) {
        return CM_ST_UNKNOWN;
    }
    return uuid[3] & 0xFFFFUL;
}

static ULONG cmStIcmApproval(const TBT_ICM_DEVICE *d)
{
    if (d->Approved) {
        return d->Boot ? CM_ST_APPR_BOOT : CM_ST_APPR_APPROVED;
    }
    if (d->ApproveFailed) {
        return CM_ST_APPR_FAILED;
    }
    if (d->ApproveUnknown) {
        return CM_ST_APPR_UNANSWERED;
    }
    return CM_ST_APPR_PENDING;
}

static ULONG cmStU4Approval(ULONG state)
{
    switch (state) {
    case TBT_U4_UP:
        return CM_ST_APPR_APPROVED;
    case TBT_U4_HELD:
    case TBT_U4_FAILED:
    case TBT_U4_UNSUPPORTED:
        return CM_ST_APPR_HELD;
    default:
        return CM_ST_APPR_PENDING;
    }
}

static ULONG cmStTunnel(const TBT_U4_TUNNEL *t, BOOLEAN failed,
                        BOOLEAN ejected)
{
    if (t->Up) {
        return t->Preserved ? CM_ST_TUN_PRESERVED : CM_ST_TUN_UP;
    }
    if (ejected) {
        return CM_ST_TUN_EJECTED;
    }
    if (failed) {
        return CM_ST_TUN_FAILED;
    }
    return CM_ST_TUN_NONE;
}

/* The tunnel port a USB4 port's PCIe tunnel reaches (record 22 section
 * 7.6), or CM_ST_UNKNOWN: one bind naming the port, not conflicted. */
static ULONG cmStU4Bridge(const XHCI_TBT_BINDS *binds, ULONG port)
{
    ULONG i;
    ULONG found;

    if (binds == NULL || binds->Overflow ||
        (binds->PortConflict & (1UL << (port - 1))) != 0) {
        return CM_ST_UNKNOWN;
    }
    found = CM_ST_UNKNOWN;
    for (i = 0; i < binds->Count && i < XHCI_TBT_MAX_BINDS; i++) {
        if (binds->Port[i] == port &&
            binds->How[i] != XHCI_TBT_CORR_CONFLICT &&
            binds->How[i] != XHCI_TBT_CORR_NONE) {
            if (found != CM_ST_UNKNOWN && found != binds->Bdf[i]) {
                return CM_ST_UNKNOWN;
            }
            found = binds->Bdf[i];
        }
    }
    return found;
}

/* Under the ICM there is no per-device correspondence: the domain's
 * single active member when it has exactly one. */
static ULONG cmStOnlyActive(const CM_STATUS_IN *in)
{
    ULONG i;
    ULONG found;
    ULONG n;

    found = CM_ST_UNKNOWN;
    n = 0;
    for (i = 0; i < in->PortCount && i < CM_ST_MAX_PORTS; i++) {
        if (in->PortActive & (1UL << i)) {
            found = in->PortBdf[i];
            n++;
        }
    }
    return n == 1 ? found : CM_ST_UNKNOWN;
}

static UCHAR *cmStEntry(UCHAR *buf, ULONG *count, ULONG *flags)
{
    UCHAR *e;

    if (*count >= CM_ST_MAX_ROUTERS) {
        *flags |= CM_ST_F_TRUNCATED;
        return NULL;
    }
    e = buf + CM_ST_HDR_BYTES + *count * CM_ST_ENTRY_BYTES;
    cmStZero(e, CM_ST_ENTRY_BYTES);
    (*count)++;
    return e;
}

/* An ICM entry's generation (record 21 section 5.3): what the router's
 * USB4 Version said, and not known before the read or where it failed;
 * Thunderbolt 3, as before the read existed, where none was asked (the
 * driver asks on every row). */
static ULONG cmStIcmGen(const TBT_ICM *icm, const TBT_ICM_DEVICE *d)
{
    if (!icm->ReadVersion) {
        return CM_ST_GEN_TBT3;
    }
    switch (d->VersionState) {
    case TBT_ICM_VER_USB4:
        return CM_ST_GEN_USB4 | ((d->Usb4Version & 0xFFUL) << 8);
    case TBT_ICM_VER_TBT3:
        return CM_ST_GEN_TBT3;
    default:
        return CM_ST_GEN_UNKNOWN;
    }
}

static VOID cmStIcm(UCHAR *buf, const CM_STATUS_IN *in, const TBT_ICM *icm,
                    ULONG *count, ULONG *flags)
{
    ULONG i;
    UCHAR *e;
    const TBT_ICM_DEVICE *d;
    ULONG bridge;

    bridge = cmStOnlyActive(in);
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        d = &icm->Devices[i];
        if (!d->InUse) {
            continue;
        }
        e = cmStEntry(buf, count, flags);
        if (e == NULL) {
            return;
        }
        cmStPut(e, CM_ST_E_KIND, CM_ST_KIND_ICM);
        cmStPut(e, CM_ST_E_PLACE, (d->Link & 0xFFUL) | ((d->Depth & 0xFFUL) << 8));
        cmStPut(e, CM_ST_E_ROUTE_LO, d->RouteValid ? d->Route.Lo : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_ROUTE_HI, d->RouteValid ? d->Route.Hi : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_APPROVAL, cmStIcmApproval(d));
        cmStPut(e, CM_ST_E_IDS,
                (d->Names.Present & TBT_ICM_NAMES_INTEL) != 0
                    ? ((d->Names.TbVendorId & 0xFFFFUL) |
                       ((d->Names.TbModelId & 0xFFFFUL) << 16))
                    : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_GEN, cmStIcmGen(icm, d));
        cmStPut(e, CM_ST_E_LANES, CM_ST_LANES_UNKNOWN);
        /* A Thunderbolt 3 host carries USB inside the PCIe tunnel (the
         * device's own controller): it has no USB3 tunnel to report. */
        cmStPut(e, CM_ST_E_TUNNELS,
                (d->Approved ? CM_ST_TUN_UP : CM_ST_TUN_NONE) |
                    ((in->IcmNoUsb3 ? CM_ST_TUN_NONE : CM_ST_TUN_UNKNOWN)
                     << 4) |
                    (CM_ST_TUN_UNKNOWN << 8));
        cmStPut(e, CM_ST_E_BRIDGE, d->Approved ? bridge : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_SHORT_ID, cmStShortId(TRUE, d->Uuid));
        if (d->Names.Present & TBT_ICM_NAMES_VENDOR) {
            cmStName(e + CM_ST_E_VENDOR, d->Names.Vendor, TBT_ICM_NAME_CHARS);
        }
        if (d->Names.Present & TBT_ICM_NAMES_MODEL) {
            cmStName(e + CM_ST_E_MODEL, d->Names.Model, TBT_ICM_NAME_CHARS);
        }
    }
}

static VOID cmStU4(UCHAR *buf, const TBT_U4CM *u4,
                   const XHCI_TBT_BINDS *binds, ULONG *count, ULONG *flags)
{
    ULONG i;
    UCHAR *e;
    const TBT_U4_PORT *p;
    const TBT_U4_ROUTER *r;
    const TBT_U4_LINK *l;
    ULONG lanes;

    for (i = 0; i < u4->PortCount && i < TBT_U4_MAX_PORTS; i++) {
        p = &u4->Ports[i];
        r = &p->Dev;
        if (p->State == TBT_U4_EMPTY && !r->Present) {
            continue;
        }
        e = cmStEntry(buf, count, flags);
        if (e == NULL) {
            return;
        }
        if (p->Bonded) {
            lanes = CM_ST_LANES_BONDED;
        } else if (p->SingleLane) {
            lanes = CM_ST_LANES_SINGLE;
        } else {
            lanes = CM_ST_LANES_UNKNOWN;
        }
        cmStPut(e, CM_ST_E_KIND, CM_ST_KIND_USB4_PORT);
        cmStPut(e, CM_ST_E_PLACE, (p->Index & 0xFFUL) | (1UL << 8));
        cmStPut(e, CM_ST_E_ROUTE_LO, r->Present ? r->Route.Lo : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_ROUTE_HI, r->Present ? r->Route.Hi : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_APPROVAL, cmStU4Approval(p->State));
        cmStPut(e, CM_ST_E_PORT_STATE,
                (p->State & 0xFFUL) | ((p->Why & 0xFFUL) << 8));
        cmStPut(e, CM_ST_E_IDS,
                r->Present ? ((r->Vendor & 0xFFFFUL) |
                              ((r->Device & 0xFFFFUL) << 16))
                           : CM_ST_UNKNOWN);
        if (!r->Present) {
            cmStPut(e, CM_ST_E_GEN, CM_ST_GEN_UNKNOWN);
        } else if (r->Tbt3) {
            cmStPut(e, CM_ST_E_GEN, CM_ST_GEN_TBT3);
        } else {
            cmStPut(e, CM_ST_E_GEN,
                    CM_ST_GEN_USB4 | ((r->Usb4Version & 0xFFUL) << 8));
        }
        cmStPut(e, CM_ST_E_LANES, lanes);
        cmStPut(e, CM_ST_E_TUNNELS,
                cmStTunnel(&p->Pcie, p->PcieFailed, p->Ejected) |
                    (cmStTunnel(&p->Usb3, p->Usb3Failed, FALSE) << 4) |
                    ((p->DpPreserved ? CM_ST_TUN_PRESERVED : CM_ST_TUN_NONE)
                     << 8));
        cmStPut(e, CM_ST_E_BRIDGE,
                p->Index >= 1 && p->Index <= 32 ? cmStU4Bridge(binds, p->Index)
                                                : CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_SHORT_ID,
                cmStShortId((BOOLEAN)(r->Present && r->UuidValid), r->Uuid));
        if (r->Present) {
            cmStName(e + CM_ST_E_VENDOR, r->VendorName, TBT_U4_NAME_LEN);
            cmStName(e + CM_ST_E_MODEL, r->ModelName, TBT_U4_NAME_LEN);
        }
    }
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        l = &u4->Links[i];
        if (!l->Used) {
            continue;
        }
        e = cmStEntry(buf, count, flags);
        if (e == NULL) {
            return;
        }
        cmStPut(e, CM_ST_E_KIND, CM_ST_KIND_USB4_LINK);
        cmStPut(e, CM_ST_E_PLACE, l->Port & 0xFFUL);
        cmStPut(e, CM_ST_E_ROUTE_LO, l->Child.Lo);
        cmStPut(e, CM_ST_E_ROUTE_HI, l->Child.Hi);
        cmStPut(e, CM_ST_E_APPROVAL,
                l->RemovalPending ? CM_ST_APPR_PENDING : CM_ST_APPR_APPROVED);
        cmStPut(e, CM_ST_E_IDS, CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_GEN, l->ChildTbt3 ? CM_ST_GEN_TBT3 : CM_ST_GEN_USB4);
        cmStPut(e, CM_ST_E_LANES, CM_ST_LANES_UNKNOWN);
        cmStPut(e, CM_ST_E_TUNNELS, CM_ST_TUN_UNKNOWN |
                                        (CM_ST_TUN_UNKNOWN << 4) |
                                        (CM_ST_TUN_UNKNOWN << 8));
        cmStPut(e, CM_ST_E_BRIDGE, CM_ST_UNKNOWN);
        cmStPut(e, CM_ST_E_SHORT_ID,
                cmStShortId(l->ChildUuidValid, l->ChildUuid));
    }
}

ULONG CmStatusBuild(UCHAR *buf, const CM_STATUS_IN *in, const TBT_ICM *icm,
                    const TBT_U4CM *u4, const XHCI_TBT_BINDS *binds)
{
    ULONG count;
    ULONG flags;
    ULONG ports;
    ULONG i;
    ULONG k;
    ULONG n;
    UCHAR *path;

    cmStZero(buf, CM_ST_HDR_BYTES);
    count = 0;
    flags = 0;
    if (in->Cm == CM_ST_CM_ICM && icm != NULL) {
        cmStIcm(buf, in, icm, &count, &flags);
    } else if (in->Cm == CM_ST_CM_USB4 && u4 != NULL) {
        cmStU4(buf, u4, binds, &count, &flags);
    }
    ports = in->PortCount > CM_ST_MAX_PORTS ? CM_ST_MAX_PORTS : in->PortCount;
    cmStPut(buf, CM_ST_O_MAGIC, CM_ST_MAGIC);
    cmStPut(buf, CM_ST_O_VERSION, CM_ST_VERSION);
    cmStPut(buf, CM_ST_O_BYTES, CM_ST_HDR_BYTES + count * CM_ST_ENTRY_BYTES);
    cmStPut(buf, CM_ST_O_NHI_ID, in->NhiId);
    cmStPut(buf, CM_ST_O_CM, in->Cm);
    cmStPut(buf, CM_ST_O_PROFILE, in->Profile);
    cmStPut(buf, CM_ST_O_STAGE, in->Stage);
    cmStPut(buf, CM_ST_O_INERT, in->Inert);
    if (in->Cm == CM_ST_CM_ICM && icm != NULL) {
        cmStPut(buf, CM_ST_O_SECURITY, icm->Ready.SecurityLevel);
        cmStPut(buf, CM_ST_O_FW_VERSION,
                icm->Ready.NvmVersion != 0 ? icm->Ready.NvmVersion
                                           : icm->Ready.RomVersion);
    } else {
        cmStPut(buf, CM_ST_O_SECURITY, CM_ST_UNKNOWN);
        cmStPut(buf, CM_ST_O_FW_VERSION, CM_ST_UNKNOWN);
    }
    cmStPut(buf, CM_ST_O_PORT_COUNT, ports);
    cmStPut(buf, CM_ST_O_PORT_ACTIVE,
            in->PortActive & ((1UL << CM_ST_MAX_PORTS) - 1));
    for (i = 0; i < CM_ST_MAX_PORTS; i++) {
        cmStPut(buf, CM_ST_O_PORTS + 4 * i,
                i < ports ? in->PortBdf[i] : CM_ST_UNKNOWN);
        path = buf + CM_ST_O_PATHS + CM_ST_PATH_BYTES * i;
        n = i < ports ? in->PathCount[i] : 0;
        if (n > CM_ST_PATH_MAX) {
            n = 0;
        }
        path[0] = (UCHAR)n;
        for (k = 0; k < n; k++) {
            path[1 + k] = (UCHAR)(in->Path[i][k] & 0xFFUL);
        }
    }
    cmStPut(buf, CM_ST_O_ROUTER_COUNT, count);
    cmStPut(buf, CM_ST_O_ENTRY_BYTES, CM_ST_ENTRY_BYTES);
    flags |= (in->Blocked & CM_ST_F_BLOCKED_MASK) << CM_ST_F_BLOCKED_SHIFT;
    flags |= ((in->Blocked >> 4) & CM_ST_F_BLOCKED_MASK)
             << CM_ST_F_EJECTING_SHIFT;
    cmStPut(buf, CM_ST_O_FLAGS, flags);
    cmStPut(buf, CM_ST_O_ECHO, in->Echo);
    return CM_ST_HDR_BYTES + count * CM_ST_ENTRY_BYTES;
}

BOOLEAN CmStatusChanged(const UCHAR *a, ULONG aBytes, const UCHAR *b,
                        ULONG bBytes)
{
    ULONG i;

    if (aBytes != bBytes) {
        return TRUE;
    }
    for (i = 0; i < aBytes; i++) {
        if ((i >= CM_ST_O_GENERATION && i < CM_ST_O_UPTIME_S + 4) ||
            (i >= CM_ST_O_SYSTIME_LO && i < CM_ST_O_SYSTIME_HI + 4)) {
            continue;
        }
        if (a[i] != b[i]) {
            return TRUE;
        }
    }
    return FALSE;
}

VOID CmStatusStamp(UCHAR *buf, ULONG generation, ULONG uptimeS,
                   ULONG systemLo, ULONG systemHi)
{
    cmStPut(buf, CM_ST_O_GENERATION, generation);
    cmStPut(buf, CM_ST_O_UPTIME_S, uptimeS);
    cmStPut(buf, CM_ST_O_SYSTIME_LO, systemLo);
    cmStPut(buf, CM_ST_O_SYSTIME_HI, systemHi);
}

/*
 * The Windows 98 instance path of the function at `bdf` (runs/run-39.md,
 * "98 SE's instance ids"; the walk cm_reenum.c's cmPciPath makes): its
 * devfn, each parent bridge's devfn up to the root bus, then the root bus
 * number. A parent is the type 1 function whose secondary bus is the
 * child's bus, found by reading every lower bus; a bus with no such parent
 * is a root bus, but only after a scan that read bus 0 whole. FALSE where
 * the path is unknown or longer than `room`.
 */
BOOLEAN CmStatusPciPath(const XHCI_TBT_CFG *cfg, ULONG bdf, PULONG path,
                        PULONG count, ULONG room)
{
    ULONG n;
    ULONG cur;
    ULONG bus;
    ULONG b;
    ULONG d;
    ULONG f;
    ULONG id;
    ULONG hdr;
    ULONG buses;
    ULONG parent;
    ULONG incomplete;

    n = 0;
    cur = bdf;
    while (n + 1 < room) {
        path[n++] = cur & 0xFFUL;
        bus = XHCI_TBT_BDF_BUS(cur);
        if (bus == 0) {
            path[n++] = 0;
            *count = n;
            return TRUE;
        }
        parent = XHCI_TBT_NO_BDF;
        incomplete = 0;
        for (b = 0; b < bus && parent == XHCI_TBT_NO_BDF; b++) {
            for (d = 0; d < 32UL && parent == XHCI_TBT_NO_BDF; d++) {
                for (f = 0; f < 8UL; f++) {
                    if (!cfg->Read(cfg->Context, XHCI_TBT_BDF(b, d, f), 0,
                                   &id)) {
                        if (b == 0) {
                            incomplete = 1;
                        }
                        d = 32UL;
                        break;
                    }
                    if (id == 0xFFFFFFFFUL) {
                        if (f == 0) {
                            break;
                        }
                        continue;
                    }
                    if (!cfg->Read(cfg->Context, XHCI_TBT_BDF(b, d, f),
                                   XHCI_TBT_CFG_HEADER, &hdr)) {
                        incomplete = 1;
                        break;
                    }
                    if (XHCI_TBT_HDR_LAYOUT(hdr >> 16) ==
                        XHCI_TBT_HDR_BRIDGE) {
                        /* A bridge whose bus numbers cannot be read might
                         * be the parent: the path stays unknown. */
                        if (!cfg->Read(cfg->Context, XHCI_TBT_BDF(b, d, f),
                                       XHCI_TBT_CFG_BUSES, &buses)) {
                            incomplete = 1;
                            break;
                        }
                        if (((buses >> 8) & 0xFFUL) == bus) {
                            parent = XHCI_TBT_BDF(b, d, f);
                            break;
                        }
                    }
                    if (f == 0 && ((hdr >> 16) & XHCI_TBT_HDR_MF) == 0) {
                        break;
                    }
                }
            }
        }
        if (parent == XHCI_TBT_NO_BDF) {
            if (incomplete) {
                return FALSE;
            }
            path[n++] = bus;
            *count = n;
            return TRUE;
        }
        cur = parent;
    }
    return FALSE;
}
