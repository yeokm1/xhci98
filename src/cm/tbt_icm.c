/*
 * tbt_icm.c - the firmware connection manager client (see tbt_icm.h).
 *
 * Written for this project. The frames and sequences are ICM-PROTOCOL.md's
 * ("ICM n.n") and, under design record 20's rule since 2026-10-15, Linux
 * drivers/thunderbolt's ("Linux <file> <symbol>", mirror pinned at
 * c6859eed): layouts and sequences are cited, no function body is copied.
 * Every layout of a row other than Alpine Ridge is UNCAPTURED.
 *
 * IRQL: see tbt_icm.h's blanket - PASSIVE_LEVEL on the connection-manager
 * thread, no lock held, for everything that takes a TBT_ICM; the profile
 * rows' decoders and encoders and TbtIcmDecodeNames at any IRQL.
 */

#include "tbt_icm.h"

#define TBT_ICM_NO_SLOT 0xFFFFFFFFUL

static VOID tbtIcmLog(PTBT_ICM icm, const char *label, ULONG value)
{
    if (icm->Host != NULL && icm->Host->Log != NULL) {
        icm->Host->Log(icm->Host->Context, label, value);
    }
}

#define TBT_ICM_RD(icm, off) \
    ((icm)->Host->Read32((icm)->Host->Context, (off)))
#define TBT_ICM_WR(icm, off, v) \
    ((icm)->Host->Write32((icm)->Host->Context, (off), (v)))

static VOID tbtIcmZero(PVOID p, ULONG n)
{
    UCHAR *b;

    b = (UCHAR *)p;
    while (n-- != 0) {
        *b++ = 0;
    }
}

/* ====================================================================== */
/* The endpoint name block (ICM 4.4; Linux icm.c struct ep_name_entry)    */
/* ====================================================================== */

/* Byte k of a byte string is bits 8*(k%4)+7..8*(k%4) of DW k/4 (ICM 0). */
static ULONG tbtIcmByte(const ULONG *dw, ULONG k)
{
    return (dw[k / 4] >> (8 * (k % 4))) & 0xFFUL;
}

/* A NUL-terminated ASCII name, kept printable for the log. */
static VOID tbtIcmCopyName(const ULONG *dw, ULONG start, ULONG len, char *out)
{
    ULONG i;
    ULONG c;

    for (i = 0; i < len && i < TBT_ICM_NAME_CHARS - 1; i++) {
        c = tbtIcmByte(dw, start + i);
        if (c == 0) {
            break;
        }
        out[i] = (char)((c < 0x20 || c > 0x7E) ? '?' : c);
    }
    out[i] = 0;
}

VOID TbtIcmDecodeNames(const ULONG *dw, ULONG words, PTBT_ICM_NAMES out)
{
    ULONG bytes;
    ULONG off;
    ULONG len;
    ULONG type;

    tbtIcmZero(out, sizeof(*out));
    bytes = words * 4;
    off = 0;
    /* Each entry is length, type, data; the length counts the two header
     * bytes and a zero length ends the list. */
    while (off + 2 <= bytes) {
        len = tbtIcmByte(dw, off);
        if (len == 0) {
            break;
        }
        if (len < 2 || off + len > bytes) {
            out->Present |= TBT_ICM_NAMES_MALFORMED;
            break;
        }
        type = tbtIcmByte(dw, off + 1);
        if (type == TBT_ICM_NAME_VENDOR) {
            tbtIcmCopyName(dw, off + 2, len - 2, out->Vendor);
            out->Present |= TBT_ICM_NAMES_VENDOR;
        } else if (type == TBT_ICM_NAME_MODEL) {
            tbtIcmCopyName(dw, off + 2, len - 2, out->Model);
            out->Present |= TBT_ICM_NAMES_MODEL;
        } else if (type == TBT_ICM_NAME_INTEL && len >= 8) {
            /* struct intel_vss: vendor u16, model u16, mc u8, flags u8,
             * then (not read here) pci_devid u16 and nvm_version u32. */
            out->TbVendorId = tbtIcmByte(dw, off + 2) | (tbtIcmByte(dw, off + 3) << 8);
            out->TbModelId = tbtIcmByte(dw, off + 4) | (tbtIcmByte(dw, off + 5) << 8);
            out->IntelFlags = tbtIcmByte(dw, off + 7);
            out->Present |= TBT_ICM_NAMES_INTEL;
        }
        out->Entries++;
        off += len;
    }
}

/* ====================================================================== */
/* The rows                                                               */
/* ====================================================================== */

static VOID tbtIcmReadyHeader(const ULONG *dw, ULONG words, PTBT_ICM_READY out)
{
    tbtIcmZero(out, sizeof(*out));
    out->Words = words;
    out->Flags = TBT_ICM_HDR_FLAGS(dw[0]);
}

/* Linux tb_msgs.h struct icm_fr_pkg_driver_ready_response: romver,
 * ramver, security_level (u16, ICM_FR_SLEVEL_MASK). UNCAPTURED. */
static BOOLEAN tbtIcmFrReady(const ULONG *dw, ULONG words, PTBT_ICM_READY out)
{
    if (words < 2) {
        return FALSE;
    }
    tbtIcmReadyHeader(dw, words, out);
    if (words >= 2) {
        out->RomVersion = dw[1] & 0xFFUL;
        out->RamVersion = (dw[1] >> 8) & 0xFFUL;
        out->SecurityLevel = (dw[1] >> 16) & 0xFUL;
    }
    return TRUE;
}

/* ICM 4.1 [obs] T:2 `0x0100e203 0x28010b60`; Linux struct
 * icm_ar_pkg_driver_ready_response and ICM_AR_INFO_*: info bits 3:0 the
 * security level, 11:7 the boot ACL count, 13 boot ACL supported. */
static BOOLEAN tbtIcmArReady(const ULONG *dw, ULONG words, PTBT_ICM_READY out)
{
    ULONG info;

    if (words < 2) {
        return FALSE;
    }
    tbtIcmReadyHeader(dw, words, out);
    if (words >= 2) {
        info = dw[1] >> 16;
        out->RomVersion = dw[1] & 0xFFUL;
        out->RamVersion = (dw[1] >> 8) & 0xFFUL;
        out->SecurityLevel = info & 0xFUL;
        out->BootAclCount = (info >> 7) & 0x1FUL;
        out->BootAclSupported = (BOOLEAN)((info & 0x2000UL) != 0);
    }
    return TRUE;
}

/* Linux struct icm_tr_pkg_driver_ready_response (Titan Ridge, and Ice
 * Lake's icm_icl_driver_ready reads the same one): reserved1, info (level
 * 2:0, protocol version 6:4, boot ACL count 12:7), nvm_version, device_id.
 * UNCAPTURED. */
static BOOLEAN tbtIcmTrReady(const ULONG *dw, ULONG words, PTBT_ICM_READY out)
{
    ULONG info;

    if (words < 4) {
        return FALSE;
    }
    tbtIcmReadyHeader(dw, words, out);
    if (words >= 2) {
        info = dw[1] >> 16;
        out->SecurityLevel = info & 0x7UL;
        out->ProtocolVersion = (info >> 4) & 0x7UL;
        out->BootAclCount = (info >> 7) & 0x3FUL;
    }
    if (words >= 3) {
        out->NvmVersion = dw[2];
    }
    if (words >= 4) {
        out->DeviceId = dw[3] & 0xFFFFUL;
    }
    return TRUE;
}

/* Record 21 section 1.4.6: an all-zero UUID, or a name block that runs
 * past its frame, is a frame read with the wrong layout. */
static BOOLEAN tbtIcmAnnounceSane(const TBT_ICM_ANNOUNCE *a)
{
    if ((a->Uuid[0] | a->Uuid[1] | a->Uuid[2] | a->Uuid[3]) == 0) {
        return FALSE;
    }
    return (BOOLEAN)((a->Names.Present & TBT_ICM_NAMES_MALFORMED) == 0);
}

static BOOLEAN tbtIcmRouteZero(const TBT_ROUTE *r)
{
    return (BOOLEAN)((r->Hi & TBT_ROUTE_HI_TOPOLOGY) == 0 && r->Lo == 0);
}

static VOID tbtIcmAnnounceCommon(const ULONG *dw, PTBT_ICM_ANNOUNCE out)
{
    ULONG i;

    tbtIcmZero(out, sizeof(*out));
    out->EventFlags = TBT_ICM_HDR_FLAGS(dw[0]);
    for (i = 0; i < 4; i++) {
        out->Uuid[i] = dw[1 + i];
    }
}

static VOID tbtIcmLinkInfo(ULONG li, PTBT_ICM_ANNOUNCE out)
{
    out->LinkInfo = li;
    out->Approved = (BOOLEAN)((li & TBT_ICM_LI_APPROVED) != 0);
    out->Rejected = (BOOLEAN)((li & TBT_ICM_LI_REJECTED) != 0);
    out->Boot = (BOOLEAN)((li & TBT_ICM_LI_BOOT) != 0);
}

/*
 * ICM 4.4 [obs] T:9, T:27886, T:34163 (61 DWs); Linux struct
 * icm_fr_event_device_connected, which Falcon and Alpine Ridge share:
 * header, ep_uuid, connection_key, connection_id, link_info, ep_name[55].
 * The name block is decoded from whatever of it the frame carries.
 */
static BOOLEAN tbtIcmFrConnected(const ULONG *dw, ULONG words,
                                 PTBT_ICM_ANNOUNCE out)
{
    if (words < TBT_ICM_FR_CONNECTED_WORDS) {
        return FALSE;
    }
    tbtIcmAnnounceCommon(dw, out);
    out->Key = dw[5] & 0xFFUL;
    out->ConnectionId = (dw[5] >> 8) & 0xFFUL;
    tbtIcmLinkInfo(dw[5] >> 16, out);
    out->Link = TBT_ICM_LI_LINK(out->LinkInfo);
    out->Depth = TBT_ICM_LI_DEPTH(out->LinkInfo);
    TbtIcmDecodeNames(dw + 6, TBT_ICM_FR_CONNECTED_WORDS - 6, &out->Names);
    return tbtIcmAnnounceSane(out);
}

/*
 * Linux struct icm_tr_event_device_connected (Titan Ridge, and Ice Lake's
 * icm_icl_device_connected): header, ep_uuid, route_hi, route_lo,
 * connection_id, reserved, link_info, ep_name[55] - 63 DWs. No key (ICM
 * 4.9). UNCAPTURED. Link and depth are the route's (Spec 6.4.2.2).
 */
static BOOLEAN tbtIcmTrConnected(const ULONG *dw, ULONG words,
                                 PTBT_ICM_ANNOUNCE out)
{
    if (words < TBT_ICM_TR_CONNECTED_WORDS) {
        return FALSE;
    }
    tbtIcmAnnounceCommon(dw, out);
    out->Route.Hi = dw[5];
    out->Route.Lo = dw[6];
    out->RouteValid = TRUE;
    out->ConnectionId = dw[7] & 0xFFUL;
    tbtIcmLinkInfo(dw[7] >> 16, out);
    out->Link = TbtRouteLevel(&out->Route, 0);
    out->Depth = TbtRouteDepth(&out->Route);
    TbtIcmDecodeNames(dw + 8, TBT_ICM_TR_CONNECTED_WORDS - 8, &out->Names);
    return (BOOLEAN)(!tbtIcmRouteZero(&out->Route) && tbtIcmAnnounceSane(out));
}

/* ICM 4.5 [obs] T:21087 `0x01000004 0x00110000`; Linux struct
 * icm_fr_event_device_disconnected, with icm_fr_device_disconnected's
 * bounds on link and depth. */
static BOOLEAN tbtIcmFrDisconnected(const ULONG *dw, ULONG words,
                                    PTBT_ICM_GONE out)
{
    ULONG li;

    if (words < 2) {
        return FALSE;
    }
    tbtIcmZero(out, sizeof(*out));
    li = dw[1] >> 16;
    out->Link = TBT_ICM_LI_LINK(li);
    out->Depth = TBT_ICM_LI_DEPTH(li);
    if (out->Link > TBT_ICM_MAX_LINK || out->Depth > TBT_ICM_MAX_DEPTH) {
        return FALSE;
    }
    return TRUE;
}

/* Linux struct icm_tr_event_device_disconnected: header, route_hi,
 * route_lo. UNCAPTURED. */
static BOOLEAN tbtIcmTrDisconnected(const ULONG *dw, ULONG words,
                                    PTBT_ICM_GONE out)
{
    if (words < 3) {
        return FALSE;
    }
    tbtIcmZero(out, sizeof(*out));
    out->Route.Hi = dw[1];
    out->Route.Lo = dw[2];
    out->RouteValid = TRUE;
    out->Link = TbtRouteLevel(&out->Route, 0);
    out->Depth = TbtRouteDepth(&out->Route);
    return (BOOLEAN)!tbtIcmRouteZero(&out->Route);
}

/* ICM 4.3 [obs] T:34153 `0x00000004 0xa0211100 0x003d0efc 0xffffffff
 * 0xffffffff 0x00000610`; Linux struct icm_fr_pkg_approve_device. */
static ULONG tbtIcmFrApprove(const TBT_ICM_DEVICE *dev, PULONG dw)
{
    ULONG i;

    dw[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_APPROVE);
    for (i = 0; i < 4; i++) {
        dw[1 + i] = dev->Uuid[i];
    }
    dw[5] = (dev->Key & 0xFFUL) | ((dev->ConnectionId & 0xFFUL) << 8);
    return 6;
}

/* Linux struct icm_tr_pkg_approve_device: header, ep_uuid, route_hi,
 * route_lo, connection_id, reserved1[3]. UNCAPTURED. */
static ULONG tbtIcmTrApprove(const TBT_ICM_DEVICE *dev, PULONG dw)
{
    ULONG i;

    dw[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_APPROVE);
    for (i = 0; i < 4; i++) {
        dw[1 + i] = dev->Uuid[i];
    }
    dw[5] = dev->Route.Hi;
    dw[6] = dev->Route.Lo;
    dw[7] = dev->ConnectionId & 0xFFUL;
    return 8;
}

/* Linux icm.c icm_icl_driver_ready reads the TR structure but takes only
 * the protocol version from it; no security level or boot-ACL count is
 * read on this row (record 21 section 1.4.4). UNCAPTURED. */
static BOOLEAN tbtIcmIclReady(const ULONG *dw, ULONG words, PTBT_ICM_READY out)
{
    if (words < 4) {
        return FALSE;
    }
    tbtIcmReadyHeader(dw, words, out);
    if (words >= 2) {
        out->ProtocolVersion = ((dw[1] >> 16) >> 4) & 0x7UL;
    }
    return TRUE;
}

/*
 * The rows. Capabilities follow Linux icm.c icm_probe's hooks per id and
 * the tb_cm_ops each one gets; driver ready's timing is each generation's
 * icm_*_driver_ready (ICM_TIMEOUT with ICM_RETRIES for Falcon and Alpine
 * Ridge, 250 ms with 10 retries for Titan Ridge, 20 s with ICM_RETRIES for
 * Ice Lake).
 */
const TBT_ICM_PROFILE TbtIcmProfileFalconRidge = {
    "Falcon Ridge",
    TBT_ICM_GEN_FALCON_RIDGE,
    TBT_ICM_CAP_TOPOLOGY | TBT_ICM_CAP_KEY | TBT_ICM_CAP_APPROVE |
        TBT_ICM_CAP_SAVE_DEVICES | TBT_ICM_CAP_SUSPEND_UNLOAD |
        TBT_ICM_CAP_EXTRA_PACKETS,
    0, TBT_ICM_RETRIES, 2,
    tbtIcmFrReady, tbtIcmFrConnected, tbtIcmFrDisconnected, tbtIcmFrApprove
};

const TBT_ICM_PROFILE TbtIcmProfileAlpineRidge = {
    "Alpine Ridge",
    TBT_ICM_GEN_ALPINE_RIDGE,
    TBT_ICM_CAP_GET_ROUTE | TBT_ICM_CAP_KEY | TBT_ICM_CAP_APPROVE |
        TBT_ICM_CAP_GET_MODE | TBT_ICM_CAP_SAVE_DEVICES |
        TBT_ICM_CAP_SUSPEND_UNLOAD | TBT_ICM_CAP_EXTRA_PACKETS,
    0, TBT_ICM_RETRIES, 2,
    tbtIcmArReady, tbtIcmFrConnected, tbtIcmFrDisconnected, tbtIcmFrApprove
};

const TBT_ICM_PROFILE TbtIcmProfileTitanRidge = {
    "Titan Ridge",
    TBT_ICM_GEN_TITAN_RIDGE,
    TBT_ICM_CAP_ROUTE_IN_EVENTS | TBT_ICM_CAP_APPROVE |
        TBT_ICM_CAP_GET_MODE | TBT_ICM_CAP_SUSPEND_UNLOAD |
        TBT_ICM_CAP_EXTRA_PACKETS,
    250UL, 10UL, 4,
    tbtIcmTrReady, tbtIcmTrConnected, tbtIcmTrDisconnected, tbtIcmTrApprove
};

/* icm_icl_ops has no approve_switch and no suspend: the firmware decides
 * which devices it tunnels, and a system suspend sends no mailbox. */
const TBT_ICM_PROFILE TbtIcmProfileIceLake = {
    "Ice Lake",
    TBT_ICM_GEN_ICE_LAKE,
    TBT_ICM_CAP_ROUTE_IN_EVENTS | TBT_ICM_CAP_FORCE_POWER |
        TBT_ICM_CAP_EXTRA_PACKETS,
    20000UL, TBT_ICM_RETRIES, 4,
    tbtIcmIclReady, tbtIcmTrConnected, tbtIcmTrDisconnected, NULL
};

/* ====================================================================== */
/* Set-up                                                                 */
/* ====================================================================== */

static VOID tbtIcmUnsolicited(PVOID context, const TBT_FRAME *frame);

VOID TbtIcmInit(PTBT_ICM icm,
                const TBT_ICM_PROFILE *profile,
                PTBT_RING ring,
                const TBT_HOST *host,
                ULONG ringFlags)
{
    tbtIcmZero(icm, sizeof(*icm));
    icm->Profile = profile;
    icm->RingFlags = ringFlags;
    icm->Ring = ring;
    icm->Host = host;
    icm->ApproveAll = TRUE;
    icm->CmdTimeoutMs = TBT_ICM_CMD_TIMEOUT_MS;
    icm->ApproveTimeoutMs = TBT_ICM_APPROVE_TIMEOUT_MS;
    icm->Retries = TBT_ICM_RETRIES;
    icm->WaitSlot = TBT_ICM_NO_SLOT;
    ring->Host = host;
    TbtRingSetUnsolicited(ring, tbtIcmUnsolicited, icm);
}

VOID TbtIcmSetLinkDown(PTBT_ICM icm, TBT_ICM_LINKDOWN fn, PVOID context)
{
    icm->LinkDown = fn;
    icm->LinkDownContext = context;
}

VOID TbtIcmSetApprovePolicy(PTBT_ICM icm, BOOLEAN approveAll)
{
    icm->ApproveAll = approveAll;
}

VOID TbtIcmSetApproveHook(PTBT_ICM icm, TBT_ICM_APPROVE_HOOK fn, PVOID context)
{
    icm->ApproveHook = fn;
    icm->ApproveHookContext = context;
}

static ULONG tbtIcmClamp(ULONG ms, ULONG def)
{
    if (ms == 0) {
        return def;
    }
    if (ms < TBT_ICM_TIMEOUT_MIN_MS) {
        return TBT_ICM_TIMEOUT_MIN_MS;
    }
    if (ms > TBT_ICM_TIMEOUT_MAX_MS) {
        return TBT_ICM_TIMEOUT_MAX_MS;
    }
    return ms;
}

VOID TbtIcmSetTimeouts(PTBT_ICM icm, ULONG cmdMs, ULONG approveMs)
{
    icm->CmdTimeoutMs = tbtIcmClamp(cmdMs, TBT_ICM_CMD_TIMEOUT_MS);
    icm->ApproveTimeoutMs = tbtIcmClamp(approveMs, TBT_ICM_APPROVE_TIMEOUT_MS);
}

/* ====================================================================== */
/* The table and the queues                                               */
/* ====================================================================== */

const TBT_ICM_DEVICE *TbtIcmDevice(const TBT_ICM *icm, ULONG slot)
{
    if (slot >= TBT_ICM_MAX_DEVICES || !icm->Devices[slot].InUse) {
        return NULL;
    }
    return &icm->Devices[slot];
}

ULONG TbtIcmDeviceCount(const TBT_ICM *icm)
{
    ULONG i;
    ULONG n;

    n = 0;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse) {
            n++;
        }
    }
    return n;
}

static BOOLEAN tbtIcmUuidEqual(const ULONG *a, const ULONG *b)
{
    return (BOOLEAN)(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] &&
                     a[3] == b[3]);
}

/* Linux include/linux/thunderbolt.h tb_phy_port_from_link: two links per
 * physical port, links counted from 1. */
static ULONG tbtIcmPhyPort(ULONG link)
{
    return link == 0 ? 0xFFUL : (link - 1) / 2;
}

/* Linux icm.c dual_link_from_link: the other link of the same port. */
static ULONG tbtIcmDualLink(ULONG link)
{
    return link == 0 ? 0 : ((link - 1) ^ 1UL) + 1;
}

/* The slot at an address: a route on a row that addresses by route, else
 * a link and depth; `dual` also takes the same port's other link (Linux
 * icm.c icm_fr_device_connected's stale-switch lookup). */
static ULONG tbtIcmFindAt(PTBT_ICM icm, BOOLEAN byRoute, const TBT_ROUTE *route,
                          ULONG link, ULONG depth, BOOLEAN dual)
{
    ULONG i;
    PTBT_ICM_DEVICE d;

    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        d = &icm->Devices[i];
        if (!d->InUse) {
            continue;
        }
        if (byRoute) {
            if (d->RouteValid && TbtRouteEqual(&d->Route, route)) {
                return i;
            }
        } else if (d->Depth == depth &&
                   (d->Link == link ||
                    (dual && d->Link == tbtIcmDualLink(link) && link != 0))) {
            return i;
        }
    }
    return TBT_ICM_NO_SLOT;
}

static BOOLEAN tbtIcmByRoute(PTBT_ICM icm)
{
    return (BOOLEAN)((icm->Profile->Caps & TBT_ICM_CAP_ROUTE_IN_EVENTS) != 0);
}

/*
 * When the action queue is full, a queued TUNNEL_UP whose entry is gone is
 * the one action that can be given up (its TUNNEL_DOWN is queued or will
 * be). Anything else that does not fit is not dropped silently: the
 * driver is owed a TBT_ICM_ACT_RESYNC once the queue drains.
 */
static BOOLEAN tbtIcmMakeRoom(PTBT_ICM icm)
{
    ULONG i;
    ULONG keep;
    ULONG n;
    PTBT_ICM_ACTION a;

    n = icm->ActionCount;
    keep = 0;
    for (i = 0; i < n; i++) {
        a = &icm->Actions[(icm->ActionHead + i) % TBT_ICM_ACTION_QUEUE];
        if (a->Kind == TBT_ICM_ACT_TUNNEL_UP && !TbtIcmActionLive(icm, a)) {
            continue;
        }
        if (keep != i) {
            icm->Actions[(icm->ActionHead + keep) % TBT_ICM_ACTION_QUEUE] = *a;
        }
        keep++;
    }
    icm->ActionCount = keep;
    return (BOOLEAN)(keep < TBT_ICM_ACTION_QUEUE);
}

static VOID tbtIcmQueueAction(PTBT_ICM icm, ULONG kind, ULONG why, ULONG slot,
                              BOOLEAN linkDownSent)
{
    PTBT_ICM_ACTION a;
    const TBT_ICM_DEVICE *d;
    ULONG i;

    if (icm->ActionCount >= TBT_ICM_ACTION_QUEUE && !tbtIcmMakeRoom(icm)) {
        icm->Counters.ActionOverflows++;
        icm->ResyncPending = TRUE;
        tbtIcmLog(icm, "tbt.icm.action-overflow", kind);
        return;
    }
    d = &icm->Devices[slot];
    a = &icm->Actions[(icm->ActionHead + icm->ActionCount) % TBT_ICM_ACTION_QUEUE];
    icm->ActionCount++;
    a->Kind = kind;
    a->Why = why;
    a->Slot = slot;
    a->Serial = d->Serial;
    a->WasApproved = d->Approved;
    a->Boot = d->Boot;
    a->LinkDownSent = linkDownSent;
    a->RouteValid = d->RouteValid;
    for (i = 0; i < 4; i++) {
        a->Uuid[i] = d->Uuid[i];
    }
    a->Link = d->Link;
    a->Depth = d->Depth;
    a->Route = d->Route;
    if (kind == TBT_ICM_ACT_TUNNEL_UP) {
        icm->Counters.TunnelUps++;
    } else {
        icm->Counters.TunnelDowns++;
    }
}

BOOLEAN TbtIcmNextAction(PTBT_ICM icm, PTBT_ICM_ACTION out)
{
    if (icm->ActionCount == 0) {
        if (!icm->ResyncPending) {
            return FALSE;
        }
        icm->ResyncPending = FALSE;
        tbtIcmZero(out, sizeof(*out));
        out->Kind = TBT_ICM_ACT_RESYNC;
        out->Why = icm->ResyncWhy != 0 ? icm->ResyncWhy : TBT_ICM_WHY_LOST;
        icm->ResyncWhy = 0;
        out->Slot = TBT_ICM_NO_SLOT;
        return TRUE;
    }
    *out = icm->Actions[icm->ActionHead];
    icm->ActionHead = (icm->ActionHead + 1) % TBT_ICM_ACTION_QUEUE;
    icm->ActionCount--;
    return TRUE;
}

BOOLEAN TbtIcmActionLive(const TBT_ICM *icm, const TBT_ICM_ACTION *action)
{
    return (BOOLEAN)(icm->Bound && action->Slot < TBT_ICM_MAX_DEVICES &&
                     icm->Devices[action->Slot].InUse &&
                     icm->Devices[action->Slot].Serial == action->Serial);
}

/* An entry leaves the table: one TUNNEL_DOWN, and a wait for it ends. */
static VOID tbtIcmDrop(PTBT_ICM icm, ULONG slot, ULONG why, BOOLEAN linkDownSent)
{
    PTBT_ICM_DEVICE d;

    d = &icm->Devices[slot];
    tbtIcmQueueAction(icm, TBT_ICM_ACT_TUNNEL_DOWN, why, slot, linkDownSent);
    if (icm->WaitSerial != 0 && icm->WaitSlot == slot &&
        icm->WaitSerial == d->Serial) {
        icm->WaitGone = TRUE;
    }
    tbtIcmLog(icm, "tbt.icm.device-gone", (why << 8) | slot);
    d->InUse = FALSE;
    d->Serial = 0;
}

/*
 * Whether an address lies at or below a disconnected one. Linux icm.c's
 * disconnect handlers remove the switch through remove_switch, and
 * switch.c tb_switch_remove takes every switch below it with it. By route:
 * the disconnected route is a prefix (its levels equal, the rest deeper).
 * By link and depth: the same physical port, deeper or equal (a chain on
 * one port, Linux tb_phy_port_from_link).
 */
static BOOLEAN tbtIcmAtOrUnder(PTBT_ICM icm, BOOLEAN routeValid,
                               const TBT_ROUTE *route, ULONG link, ULONG depth,
                               const TBT_ICM_GONE *g)
{
    ULONG gd;
    ULONG i;

    if (tbtIcmByRoute(icm)) {
        if (!routeValid || !g->RouteValid) {
            return FALSE;
        }
        gd = TbtRouteDepth(&g->Route);
        if (gd == 0 || TbtRouteDepth(route) < gd) {
            return FALSE;
        }
        for (i = 0; i < gd; i++) {
            if (TbtRouteLevel(route, i) != TbtRouteLevel(&g->Route, i)) {
                return FALSE;
            }
        }
        return TRUE;
    }
    if (link == g->Link && depth == g->Depth) {
        return TRUE;
    }
    return (BOOLEAN)(depth > g->Depth &&
                     tbtIcmPhyPort(link) == tbtIcmPhyPort(g->Link));
}

static VOID tbtIcmEnqueue(PTBT_ICM icm, const TBT_ICM_ANNOUNCE *a)
{
    ULONG i;
    TBT_ICM_ANNOUNCE *q;

    /* The latest announcement of a device supersedes one still queued: the
     * earlier copy leaves the queue and the latest joins its tail, so the
     * events between them keep their order. */
    for (i = 0; i < icm->QueueCount; i++) {
        q = &icm->Queue[(icm->QueueHead + i) % TBT_ICM_EVENT_QUEUE];
        if (tbtIcmUuidEqual(q->Uuid, a->Uuid)) {
            for (; i + 1 < icm->QueueCount; i++) {
                icm->Queue[(icm->QueueHead + i) % TBT_ICM_EVENT_QUEUE] =
                    icm->Queue[(icm->QueueHead + i + 1) % TBT_ICM_EVENT_QUEUE];
            }
            icm->QueueCount--;
            break;
        }
    }
    if (icm->QueueCount >= TBT_ICM_EVENT_QUEUE) {
        /* An announcement is not acknowledged and is not sent again (ICM
         * 3 [obs]); only another driver ready makes the firmware announce
         * every attached device anew (ICM 6.2, 6.5 [obs]). The idle poll
         * does that (tbtIcmRediscover). */
        icm->Counters.QueueOverflows++;
        icm->RediscoverPending = TRUE;
        tbtIcmLog(icm, "tbt.icm.queue-overflow", a->LinkInfo);
        return;
    }
    icm->Queue[(icm->QueueHead + icm->QueueCount) % TBT_ICM_EVENT_QUEUE] = *a;
    icm->QueueCount++;
    icm->Counters.EventsQueued++;
}

/* A disconnect also cancels the queued announcements at or below it, which
 * would otherwise bring those devices back. */
static VOID tbtIcmPurgeQueued(PTBT_ICM icm, const TBT_ICM_GONE *g)
{
    ULONG i;
    ULONG n;
    ULONG keep;
    TBT_ICM_ANNOUNCE *a;

    n = icm->QueueCount;
    keep = 0;
    for (i = 0; i < n; i++) {
        a = &icm->Queue[(icm->QueueHead + i) % TBT_ICM_EVENT_QUEUE];
        if (!tbtIcmAtOrUnder(icm, a->RouteValid, &a->Route, a->Link, a->Depth, g)) {
            if (keep != i) {
                icm->Queue[(icm->QueueHead + keep) % TBT_ICM_EVENT_QUEUE] = *a;
            }
            keep++;
        }
    }
    icm->QueueCount = keep;
}

/* ====================================================================== */
/* Events                                                                 */
/* ====================================================================== */

static VOID tbtIcmConnected(PTBT_ICM icm, const TBT_ICM_ANNOUNCE *a);

/* Every entry at or below `g` leaves, deepest first, each with its
 * TUNNEL_DOWN and, when `linkDown`, its link-down upcall first. Returns
 * how many left. */
static ULONG tbtIcmDropSubtree(PTBT_ICM icm, const TBT_ICM_GONE *g, ULONG why,
                               BOOLEAN linkDown)
{
    ULONG slot;
    ULONG i;
    ULONG deepest;
    ULONG found;
    PTBT_ICM_DEVICE d;

    found = 0;
    for (;;) {
        slot = TBT_ICM_NO_SLOT;
        deepest = 0;
        for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
            d = &icm->Devices[i];
            if (d->InUse &&
                tbtIcmAtOrUnder(icm, d->RouteValid, &d->Route, d->Link, d->Depth, g) &&
                (slot == TBT_ICM_NO_SLOT || d->Depth > deepest)) {
                slot = i;
                deepest = d->Depth;
            }
        }
        if (slot == TBT_ICM_NO_SLOT) {
            break;
        }
        found++;
        if (linkDown && icm->LinkDown != NULL) {
            icm->LinkDown(icm->LinkDownContext, &icm->Devices[slot], icm->InWait);
        }
        tbtIcmDrop(icm, slot, why, (BOOLEAN)(linkDown && icm->LinkDown != NULL));
    }
    return found;
}

/* The address of a table entry, as a disconnect would name it. */
static VOID tbtIcmAddressOf(const TBT_ICM_DEVICE *d, PTBT_ICM_GONE g)
{
    g->Link = d->Link;
    g->Depth = d->Depth;
    g->Route = d->Route;
    g->RouteValid = d->RouteValid;
}

/* An entry being replaced takes its subtree with it (Linux icm.c:
 * remove_switch for a moved UUID or a stale occupant, and switch.c
 * tb_switch_remove's recursion). The queue is left alone: what is queued
 * belongs to the topology that replaces it. */
static VOID tbtIcmReplace(PTBT_ICM icm, ULONG slot)
{
    TBT_ICM_GONE g;

    tbtIcmAddressOf(&icm->Devices[slot], &g);
    if (tbtIcmByRoute(icm) && !g.RouteValid) {
        tbtIcmDrop(icm, slot, TBT_ICM_WHY_REPLACED, FALSE);
        return;
    }
    (VOID)tbtIcmDropSubtree(icm, &g, TBT_ICM_WHY_REPLACED, FALSE);
}

/* A device-disconnected event: the device and everything below it leave,
 * deepest first, each with its link-down upcall and its TUNNEL_DOWN. */
static BOOLEAN tbtIcmDisconnected(PTBT_ICM icm, const TBT_ICM_GONE *g)
{
    tbtIcmPurgeQueued(icm, g);
    if (tbtIcmDropSubtree(icm, g, TBT_ICM_WHY_DISCONNECTED, TRUE) == 0) {
        /* Record 21 section 1.4.6: some device of this domain is gone;
         * the tunnel-down sequence runs for every port of its set. */
        icm->ResyncPending = TRUE;
        icm->ResyncWhy = TBT_ICM_WHY_UNMATCHED;
        icm->Counters.DisconnectUnknown++;
        tbtIcmLog(icm, "tbt.icm.disconnect-unknown",
                  g->RouteValid ? g->Route.Lo : TBT_ICM_LI_MAKE(g->Link, g->Depth));
        return FALSE;
    }
    return TRUE;
}

/* Record 21 section 1.4.6: a frame a row cannot read goes to the log
 * whole - it is the capture that row is waiting for. */
static VOID tbtIcmLogRaw(PTBT_ICM icm, const TBT_FRAME *f)
{
    ULONG i;

    tbtIcmLog(icm, "tbt.icm.raw-words", f->Words);
    for (i = 0; i < f->Words; i++) {
        tbtIcmLog(icm, "tbt.icm.raw", f->Dw[i]);
    }
}

static VOID tbtIcmEvent(PTBT_ICM icm, const TBT_FRAME *f)
{
    ULONG code;
    ULONG i;
    TBT_ICM_ANNOUNCE a;
    TBT_ICM_GONE g;

    code = TBT_ICM_HDR_CODE(f->Dw[0]);
    if (code == TBT_ICM_EV_CONNECTED) {
        icm->Counters.EventsConnected++;
        /* Linux icm.c __icm_tr_device_connected drops the extra (QoS)
         * packet of a multi-packet announcement; index 0 carries it all. */
        if (TBT_ICM_HDR_INDEX(f->Dw[0]) != 0) {
            icm->Counters.EventsIgnored++;
            return;
        }
        if (!icm->Profile->DecodeConnected(f->Dw, f->Words, &a)) {
            icm->Counters.EventsMalformed++;
            tbtIcmLog(icm, "tbt.icm.connected-malformed", f->Words);
            tbtIcmLogRaw(icm, f);
            return;
        }
        a.RawWords = f->Words < TBT_FRAME_DWORDS ? f->Words : TBT_FRAME_DWORDS;
        for (i = 0; i < a.RawWords; i++) {
            a.Raw[i] = f->Dw[i];
        }
        icm->SettleExtend = TRUE;
        if (icm->InWait) {
            tbtIcmEnqueue(icm, &a);
        } else {
            tbtIcmConnected(icm, &a);
        }
    } else if (code == TBT_ICM_EV_DISCONNECTED) {
        icm->Counters.EventsDisconnected++;
        if (!icm->Profile->DecodeDisconnected(f->Dw, f->Words, &g)) {
            icm->Counters.EventsMalformed++;
            tbtIcmLog(icm, "tbt.icm.disconnected-malformed", f->Words);
            tbtIcmLogRaw(icm, f);
            return;
        }
        if (!tbtIcmDisconnected(icm, &g)) {
            tbtIcmLogRaw(icm, f);
        }
    } else if (code == TBT_ICM_EV_RTD3_VETO) {
        /* Linux tb_msgs.h struct icm_icl_event_rtd3_veto: veto_reason in
         * DW 1, nonzero to begin a veto (icm_icl_rtd3_veto). This driver
         * keeps the domain active anyway; recorded for the log. */
        icm->Counters.EventsIgnored++;
        icm->VetoActive = (BOOLEAN)(f->Words > 1 && f->Dw[1] != 0);
        tbtIcmLog(icm, "tbt.icm.rtd3-veto", f->Words > 1 ? f->Dw[1] : 0);
    } else if (code == TBT_ICM_EV_XD_CONNECTED ||
               code == TBT_ICM_EV_XD_DISCONNECTED ||
               code == TBT_ICM_EV_DP_CHANGED) {
        /* Host-to-host links and DisplayPort need nothing here. */
        icm->Counters.EventsIgnored++;
        tbtIcmLog(icm, "tbt.icm.event-ignored",
                  (code << 24) | (f->Words > 1 ? (f->Dw[1] & 0xFFFFFFUL) : 0));
    } else {
        icm->Counters.EventsUnknown++;
        tbtIcmLog(icm, "tbt.icm.event-unknown", f->Dw[0]);
    }
}

static VOID tbtIcmFrame(PTBT_ICM icm, const TBT_FRAME *f)
{
    if (f->Words == 0) {
        return;
    }
    if (f->Pdf == TBT_PDF_ICM_EVENT) {
        tbtIcmEvent(icm, f);
        return;
    }
    icm->Counters.Strays++;
    tbtIcmLog(icm, "tbt.icm.stray", (f->Pdf << 24) | (f->Dw[0] & 0xFFFFFFUL));
}

static VOID tbtIcmUnsolicited(PVOID context, const TBT_FRAME *frame)
{
    tbtIcmFrame((PTBT_ICM)context, frame);
}

/* ====================================================================== */
/* Falcon Ridge's topology (Linux icm.c icm_fr_get_route)                 */
/* ====================================================================== */

/* Keep what the route walk needs of one packet of struct
 * icm_fr_pkg_get_topology_response: DW 1 route_lo, DW 2 route_hi, DW 3
 * first_data (bit 0 ICM_SWITCH_USED) to switch_index (31:24), DWs 6..21
 * ports[16]. A short or out-of-range packet is not kept. UNCAPTURED. */
static VOID tbtIcmTopoStore(PTBT_ICM icm, const TBT_FRAME *f)
{
    ULONG idx;
    ULONG k;

    idx = TBT_ICM_HDR_INDEX(f->Dw[0]);
    if (idx >= TBT_ICM_TOPO_PACKETS || f->Words < 6 + TBT_ICM_TOPO_PORTS) {
        icm->Counters.EventsMalformed++;
        return;
    }
    icm->TopoRouteLo[idx] = f->Dw[1];
    icm->TopoRouteHi[idx] = f->Dw[2];
    icm->TopoInfo[idx] = f->Dw[3];
    for (k = 0; k < TBT_ICM_TOPO_PORTS; k++) {
        icm->TopoPorts[idx][k] = f->Dw[6 + k];
    }
    icm->TopoReceived |= 1UL << idx;
}

/* icm_fr_get_switch_index: a port entry of type TB_TYPE_PORT (1, in bits
 * 23:0, ICM_PORT_TYPE_MASK) names the switch in bits 31:24, 0xFF none. */
static ULONG tbtIcmTopoIndex(ULONG port)
{
    ULONG index;

    if ((port & 0x00FFFFFFUL) != 0x000001UL) {
        return 0;
    }
    index = port >> 24;
    return index != 0xFFUL ? index : 0;
}

/* The route of the switch at `link`, `depth`, walked as icm_fr_get_route
 * walks it, every index held to the packets that arrived. */
static LONG tbtIcmTopoRoute(PTBT_ICM icm, ULONG link, ULONG depth,
                            PTBT_ROUTE route)
{
    ULONG sw;
    ULONG index;
    ULONG i;
    ULONG j;

    if ((icm->TopoReceived & 1UL) == 0 || link >= TBT_ICM_TOPO_PORTS) {
        return TBT_E_HW;
    }
    index = tbtIcmTopoIndex(icm->TopoPorts[0][link]);
    if (index == 0 || index >= TBT_ICM_TOPO_PACKETS ||
        (icm->TopoReceived & (1UL << index)) == 0) {
        return TBT_E_HW;
    }
    sw = index;
    for (i = 1; i < depth; i++) {
        if ((icm->TopoInfo[sw] & 0x01UL) == 0) {
            return TBT_E_HW;
        }
        for (j = 0; j < TBT_ICM_TOPO_PORTS; j++) {
            index = tbtIcmTopoIndex(icm->TopoPorts[sw][j]);
            if (index > (icm->TopoInfo[sw] >> 24)) {
                break;
            }
        }
        if (j == TBT_ICM_TOPO_PORTS || index >= TBT_ICM_TOPO_PACKETS ||
            (icm->TopoReceived & (1UL << index)) == 0) {
            return TBT_E_HW;
        }
        sw = index;
    }
    route->Hi = icm->TopoRouteHi[sw];
    route->Lo = icm->TopoRouteLo[sw];
    return TBT_OK;
}

/* ====================================================================== */
/* Commands (ICM 3; Linux icm.c icm_request, icm_match)                   */
/* ====================================================================== */

/*
 * Send `req` and wait for the response of the same code, packet index 0,
 * that echoes request DWs 1..echo. Every event that lands meanwhile is
 * decoded at once (record 21 section 4.4). A timeout is retried up to
 * `retries` more times; the error flag is not. `slot`/`serial` name the
 * entry the command is for: if a disconnect drops it the wait ends with
 * TBT_E_GONE, and its late answer, failing the echo, is a stray.
 */
static LONG tbtIcmCommand(PTBT_ICM icm,
                          const ULONG *req,
                          ULONG words,
                          ULONG echo,
                          PULONG resp,
                          ULONG respMax,
                          PULONG respWords,
                          ULONG timeoutMs,
                          ULONG retries,
                          ULONG slot,
                          ULONG serial,
                          ULONG minWords,
                          BOOLEAN topology)
{
    ULONG attempt;
    ULONG frames;
    ULONG k;
    ULONG n;
    BOOLEAN match;
    LONG rc;
    TBT_FRAME f;
    TBT_DEADLINE d;

    if (icm->InWait) {
        return TBT_E_PARAM;
    }
    icm->InWait = TRUE;
    icm->WaitSlot = slot;
    icm->WaitSerial = serial;
    icm->WaitGone = FALSE;
    rc = TBT_E_TIMEOUT;
    *respWords = 0;
    for (attempt = 0; attempt <= retries; attempt++) {
        if (icm->WaitGone) {
            rc = TBT_E_GONE;
            break;
        }
        if (attempt != 0) {
            icm->Counters.Retries++;
        }
        if (topology) {
            /* Each attempt assembles its own answer: no packet of an
             * earlier one is mixed in. */
            icm->TopoReceived = 0;
        }
        rc = TbtRingSend(icm->Ring, TBT_PDF_ICM_CMD, req, words);
        if (rc != TBT_OK) {
            break;
        }
        icm->Counters.CommandsSent++;
        rc = TBT_E_TIMEOUT;
        TbtDeadlineStart(&d, icm->Host, timeoutMs);
        frames = 0;
        for (;;) {
            if (icm->WaitGone) {
                rc = TBT_E_GONE;
                break;
            }
            if (!TbtRingReceive(icm->Ring, &f)) {
                if (TbtDeadlinePassed(&d, icm->Host)) {
                    break;
                }
                TbtDeadlineWait(&d, icm->Host);
                continue;
            }
            if (++frames > TBT_WAIT_FRAME_BUDGET) {
                break;
            }
            if (topology && f.Words != 0 && f.Pdf == TBT_PDF_ICM_RESP &&
                TBT_ICM_HDR_CODE(f.Dw[0]) == TBT_ICM_HDR_CODE(req[0])) {
                /* Linux icm.c icm_copy places packet i at slot i. The
                 * answer is complete when all ICM_GET_TOPOLOGY_PACKETS have
                 * arrived, in whatever order; a packet of the wrong size or
                 * count is logged and not kept, and an answer still
                 * incomplete at the timeout is abandoned whole (record 21
                 * section 1.4.6). */
                icm->Counters.Responses++;
                if (f.Words < minWords ||
                    TBT_ICM_HDR_COUNT(f.Dw[0]) != TBT_ICM_TOPO_PACKETS ||
                    TBT_ICM_HDR_INDEX(f.Dw[0]) >= TBT_ICM_TOPO_PACKETS) {
                    icm->Counters.EventsMalformed++;
                    tbtIcmLogRaw(icm, &f);
                    continue;
                }
                if ((TBT_ICM_HDR_FLAGS(f.Dw[0]) & TBT_ICM_FLAG_ERROR) != 0) {
                    icm->Counters.ErrorFlags++;
                    tbtIcmLogRaw(icm, &f);
                    rc = TBT_E_ERROR_FLAG;
                    break;
                }
                tbtIcmTopoStore(icm, &f);
                if (icm->TopoReceived == TBT_ICM_TOPO_ALL) {
                    rc = TBT_OK;
                    break;
                }
                continue;
            }
            match = FALSE;
            if (f.Words != 0 && f.Pdf == TBT_PDF_ICM_RESP &&
                TBT_ICM_HDR_CODE(f.Dw[0]) == TBT_ICM_HDR_CODE(req[0]) &&
                TBT_ICM_HDR_INDEX(f.Dw[0]) == 0) {
                if (f.Words < minWords || f.Words <= echo) {
                    /* Shorter than the row's structure (record 21
                     * sections 1.4.6 and 5.2): logged whole, not acted on,
                     * its flags included; the wait goes on. */
                    icm->Counters.EventsMalformed++;
                    tbtIcmLog(icm, "tbt.icm.response-short", f.Words);
                    tbtIcmLogRaw(icm, &f);
                    if (TbtDeadlinePassed(&d, icm->Host)) {
                        break;
                    }
                    continue;
                }
                match = TRUE;
                for (k = 1; k <= echo; k++) {
                    if (f.Dw[k] != req[k]) {
                        match = FALSE;
                    }
                }
            }
            if (match) {
                n = f.Words < respMax ? f.Words : respMax;
                for (k = 0; k < n; k++) {
                    resp[k] = f.Dw[k];
                }
                *respWords = n;
                icm->Counters.Responses++;
                if ((TBT_ICM_HDR_FLAGS(f.Dw[0]) & TBT_ICM_FLAG_ERROR) != 0) {
                    icm->Counters.ErrorFlags++;
                    tbtIcmLog(icm, "tbt.icm.error-flag", f.Dw[0]);
                    rc = TBT_E_ERROR_FLAG;
                } else {
                    rc = TBT_OK;
                }
                break;
            }
            tbtIcmFrame(icm, &f);
            if (TbtDeadlinePassed(&d, icm->Host)) {
                break;
            }
        }
        if (rc != TBT_E_TIMEOUT) {
            break;
        }
        icm->Counters.Timeouts++;
        tbtIcmLog(icm, "tbt.icm.timeout", req[0]);
    }
    if (rc == TBT_E_TIMEOUT && icm->WaitGone) {
        rc = TBT_E_GONE;
    }
    icm->InWait = FALSE;
    icm->WaitSlot = TBT_ICM_NO_SLOT;
    icm->WaitSerial = 0;
    icm->WaitGone = FALSE;
    return rc;
}

/* ICM 4.2 [obs] T:833 `0x0000000a 0x00110000`, T:834 `0x0100000a
 * 0x00110000 0x00000000 0x00000001`; Linux struct icm_ar_pkg_get_route and
 * its response. */
static LONG tbtIcmGetRoute(PTBT_ICM icm, ULONG slot)
{
    PTBT_ICM_DEVICE d;
    ULONG req[2];
    ULONG resp[4];
    ULONG n;
    LONG rc;

    d = &icm->Devices[slot];
    req[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_GET_ROUTE);
    req[1] = TBT_ICM_LI_MAKE(d->Link, d->Depth) << 16;
    rc = tbtIcmCommand(icm, req, 2, 1, resp, 4, &n, icm->CmdTimeoutMs,
                       icm->Retries, slot, d->Serial, 4, FALSE);
    if (rc == TBT_OK && n < 4) {
        rc = TBT_E_HW;
    }
    if (rc == TBT_OK && d->InUse) {
        d->Route.Hi = resp[2];
        d->Route.Lo = resp[3];
        d->RouteValid = TRUE;
    }
    return rc;
}

/* Falcon Ridge: the whole topology, then the walk (icm_fr_get_route). */
static LONG tbtIcmGetTopoRoute(PTBT_ICM icm, ULONG slot)
{
    PTBT_ICM_DEVICE d;
    ULONG req[1];
    ULONG resp[1];
    ULONG n;
    LONG rc;
    TBT_ROUTE r;

    d = &icm->Devices[slot];
    icm->TopoReceived = 0;
    req[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_GET_TOPOLOGY);
    rc = tbtIcmCommand(icm, req, 1, 0, resp, 1, &n, icm->CmdTimeoutMs,
                       icm->Retries, slot, d->Serial, TBT_ICM_TOPO_WORDS, TRUE);
    if (rc != TBT_OK) {
        return rc;
    }
    if (!d->InUse) {
        return TBT_E_GONE;
    }
    rc = tbtIcmTopoRoute(icm, d->Link, d->Depth, &r);
    if (rc == TBT_OK) {
        d->Route = r;
        d->RouteValid = TRUE;
    }
    return rc;
}

/* The route, by whichever query the row has. */
static LONG tbtIcmRoute(PTBT_ICM icm, ULONG slot)
{
    if ((icm->Profile->Caps & TBT_ICM_CAP_TOPOLOGY) != 0) {
        return tbtIcmGetTopoRoute(icm, slot);
    }
    return tbtIcmGetRoute(icm, slot);
}

static BOOLEAN tbtIcmAsksRoute(PTBT_ICM icm)
{
    return (BOOLEAN)((icm->Profile->Caps &
                      (TBT_ICM_CAP_GET_ROUTE | TBT_ICM_CAP_TOPOLOGY)) != 0);
}

/* The capture an uncaptured row is waiting for (record 21 section 1.4.6):
 * the approve request and the announcement it answered, word by word. */
static VOID tbtIcmLogApprove(PTBT_ICM icm, const ULONG *req, ULONG words,
                             const TBT_ICM_ANNOUNCE *a)
{
    ULONG i;

    for (i = 0; i < words; i++) {
        tbtIcmLog(icm, "tbt.icm.approve-req", req[i]);
    }
    if (a != NULL) {
        for (i = 0; i < a->RawWords; i++) {
            tbtIcmLog(icm, "tbt.icm.approve-event", a->Raw[i]);
        }
    }
}

static VOID tbtIcmApprove(PTBT_ICM icm, ULONG slot, const TBT_ICM_ANNOUNCE *a)
{
    PTBT_ICM_DEVICE d;
    ULONG req[TBT_ICM_APPROVE_MAX_WORDS];
    ULONG resp[TBT_ICM_APPROVE_MAX_WORDS];
    ULONG words;
    ULONG n;
    LONG rc;

    d = &icm->Devices[slot];
    words = icm->Profile->EncodeApprove(d, req);
    icm->Counters.ApprovalsSent++;
    d->ApproveFailed = FALSE;
    /* The response repeats the request (ICM 4.3 [obs] T:34154; Linux
     * reads the reply as the request's own structure): every word must
     * match, so a late answer to an earlier connection of the same device
     * - another key, id or route - is never taken for this one. */
    rc = tbtIcmCommand(icm, req, words, words - 1, resp, TBT_ICM_APPROVE_MAX_WORDS, &n,
                       icm->ApproveTimeoutMs, icm->Retries, slot, d->Serial,
                       words, FALSE);
    if (rc == TBT_OK && d->InUse) {
        d->Approved = TRUE;
        icm->Counters.ApprovalsOk++;
        tbtIcmLog(icm, "tbt.icm.approved", slot);
        /* The firmware built the tunnel while it processed the request
         * (ICM 4.3 [obs]). */
        tbtIcmQueueAction(icm, TBT_ICM_ACT_TUNNEL_UP, TBT_ICM_WHY_APPROVED,
                          slot, FALSE);
    } else if (rc == TBT_E_GONE || !d->InUse) {
        icm->Counters.ApprovalsAbandoned++;
        tbtIcmLog(icm, "tbt.icm.approve-abandoned", slot);
    } else if (rc == TBT_E_TIMEOUT) {
        /* Record 21 section 1.4.6: no answer is not proof that no tunnel
         * exists - the firmware builds it while it processes the request,
         * before it answers (ICM 4.3 [obs]). The outcome is unknown until
         * an announcement, a disconnect or the reconciliation settles it,
         * no second approve is sent meanwhile, and the tunnel-up sequence
         * runs anyway (the driver re-enumerates the whole domain for it). */
        d->ApproveUnknown = TRUE;
        icm->Counters.ApprovalsUnknown++;
        tbtIcmLog(icm, "tbt.icm.approve-unknown", slot);
        tbtIcmLogApprove(icm, req, words, a);
        tbtIcmQueueAction(icm, TBT_ICM_ACT_TUNNEL_UP, TBT_ICM_WHY_APPROVE_UNKNOWN,
                          slot, FALSE);
    } else {
        /* The error flag (or a ring that would not send): unapproved,
         * kept; the next announcement tries again (record 21 5.3). */
        d->ApproveFailed = TRUE;
        icm->Counters.ApprovalsFailed++;
        tbtIcmLog(icm, "tbt.icm.approve-failed", (ULONG)(-rc));
        tbtIcmLogApprove(icm, req, words, a);
    }
}

/* ====================================================================== */
/* A device-connected event (record 21 section 5.3)                       */
/* ====================================================================== */

static VOID tbtIcmFill(PTBT_ICM_DEVICE d, const TBT_ICM_ANNOUNCE *a)
{
    ULONG i;

    for (i = 0; i < 4; i++) {
        d->Uuid[i] = a->Uuid[i];
    }
    d->Key = a->Key;
    d->ConnectionId = a->ConnectionId;
    d->Link = a->Link;
    d->Depth = a->Depth;
    d->EventFlags = a->EventFlags;
    d->Approved = a->Approved;
    d->Boot = a->Boot;
    d->Names = a->Names;
    d->Stale = FALSE;
    if (a->RouteValid) {
        d->Route = a->Route;
        d->RouteValid = TRUE;
    }
}

/* Record 21 section 5.4: an announced, unapproved, unrejected device is
 * approved when the policy says so - on every such announcement, a refresh
 * included, so an approval that failed is tried again when the firmware
 * announces the device again. */
static VOID tbtIcmPolicy(PTBT_ICM icm, ULONG slot, const TBT_ICM_ANNOUNCE *a)
{
    PTBT_ICM_DEVICE d;
    BOOLEAN approve;

    d = &icm->Devices[slot];
    if ((icm->Profile->Caps & TBT_ICM_CAP_APPROVE) == 0 ||
        icm->Profile->EncodeApprove == NULL) {
        icm->Counters.ApprovalsUnsupported++;
        tbtIcmLog(icm, "tbt.icm.approve-unsupported", slot);
        return;
    }
    if (icm->ApproveHook != NULL) {
        approve = icm->ApproveHook(icm->ApproveHookContext, d);
    } else {
        approve = icm->ApproveAll;
    }
    if (!approve) {
        icm->Counters.ApprovalsWithheld++;
        tbtIcmLog(icm, "tbt.icm.approve-withheld", slot);
        return;
    }
    tbtIcmApprove(icm, slot, a);
}

/* The route, where the row asks for it; FALSE (logged and counted) when
 * the request failed or the device left meanwhile. */
static BOOLEAN tbtIcmRouteOk(PTBT_ICM icm, ULONG slot)
{
    LONG rc;

    if (!tbtIcmAsksRoute(icm) || icm->Devices[slot].RouteValid) {
        return TRUE;
    }
    rc = tbtIcmRoute(icm, slot);
    if (rc == TBT_OK && icm->Devices[slot].InUse) {
        return TRUE;
    }
    if (rc != TBT_E_GONE && icm->Devices[slot].InUse) {
        icm->Counters.RouteFailures++;
        tbtIcmLog(icm, "tbt.icm.route-failed", (ULONG)(-rc));
    }
    return FALSE;
}

/*
 * After the route: an announced-approved device's tunnel is up; an
 * unapproved one goes to the policy - unless its route failed, which
 * leaves it unrouted and unapproved, kept, for the next announcement to
 * retry (record 21 section 5.5).
 */
static VOID tbtIcmSettle(PTBT_ICM icm, ULONG slot, BOOLEAN routed,
                         const TBT_ICM_ANNOUNCE *a)
{
    PTBT_ICM_DEVICE d;

    d = &icm->Devices[slot];
    if (!d->InUse) {
        return;
    }
    if (d->Approved) {
        /* Tunnelled before this driver bound it, or by an approve whose
         * answer was lost (record 21 section 6.5). */
        tbtIcmQueueAction(icm, TBT_ICM_ACT_TUNNEL_UP, TBT_ICM_WHY_ANNOUNCED,
                          slot, FALSE);
        return;
    }
    if (routed) {
        tbtIcmPolicy(icm, slot, a);
    }
}

static VOID tbtIcmConnected(PTBT_ICM icm, const TBT_ICM_ANNOUNCE *a)
{
    ULONG i;
    ULONG slot;
    BOOLEAN byRoute;
    BOOLEAN same;
    BOOLEAN recovery;
    BOOLEAN routed;
    PTBT_ICM_DEVICE d;

    byRoute = tbtIcmByRoute(icm);
    if (a->Rejected) {
        /* "Topology limit exceeded" (Linux icm.c); nothing to do. */
        icm->Counters.Rejected++;
        tbtIcmLog(icm, "tbt.icm.rejected", a->LinkInfo);
        return;
    }

    slot = TBT_ICM_NO_SLOT;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse &&
            tbtIcmUuidEqual(icm->Devices[i].Uuid, a->Uuid)) {
            slot = i;
            break;
        }
    }
    if (slot != TBT_ICM_NO_SLOT) {
        d = &icm->Devices[slot];
        /* The same place: Linux icm.c icm_fr_device_connected compares
         * depth and physical port, __icm_tr_device_connected the route. */
        if (byRoute) {
            same = (BOOLEAN)(d->RouteValid && TbtRouteEqual(&d->Route, &a->Route));
        } else {
            same = (BOOLEAN)(d->Depth == a->Depth &&
                             tbtIcmPhyPort(d->Link) == tbtIcmPhyPort(a->Link));
        }
        /* An entry needing recovery (record 21 section 5.3): unrouted by
         * a failed route request, an approve outcome unknown, or
         * unapproved after an approve answered with the error flag. */
        recovery = (BOOLEAN)(d->ApproveUnknown || d->ApproveFailed ||
                             (!d->RouteValid && tbtIcmAsksRoute(icm)));
        /* A device the firmware approved and now announces unapproved was
         * re-plugged unseen: a replacement, below, whatever else it needs.
         * Every other recovery (unrouted, unknown, failed) is settled in
         * place, its subtree kept. */
        if (same && (d->Approved == a->Approved || recovery) &&
            !(d->Approved && !a->Approved)) {
            if (!byRoute && d->Link != a->Link) {
                /* Enumerated through the port's other link: the route
                 * moved with it (Linux icm.c icm_fr_device_connected). */
                d->RouteValid = FALSE;
            }
            d->Key = a->Key;
            d->ConnectionId = a->ConnectionId;
            d->Boot = a->Boot;
            d->Link = a->Link;
            d->EventFlags = a->EventFlags;
            d->Stale = FALSE;
            icm->Counters.Refreshes++;
            tbtIcmLog(icm, "tbt.icm.refresh", slot);
            if (!recovery && d->RouteValid) {
                /* The resume case (ICM 6.5 [obs]): nothing else. */
                return;
            }
            routed = tbtIcmRouteOk(icm, slot);
            if (!d->InUse) {
                return;
            }
            /* This announcement settles the approval state: approved = 1
             * a tunnel up, 0 the policy (record 21 section 5.3). An entry
             * already approved and announced approved needed only its
             * route. */
            if (d->Approved && a->Approved) {
                return;
            }
            d->ApproveUnknown = FALSE;
            d->ApproveFailed = FALSE;
            d->Approved = a->Approved;
            tbtIcmSettle(icm, slot, routed, a);
            return;
        }
        /* Moved, or approved differently: the old entry is gone. */
        tbtIcmReplace(icm, slot);
    }

    /* Anything still at that address is stale (Linux icm.c: by link and
     * depth, the port's other link included, or by route). */
    slot = tbtIcmFindAt(icm, byRoute, &a->Route, a->Link, a->Depth, TRUE);
    if (slot != TBT_ICM_NO_SLOT) {
        tbtIcmReplace(icm, slot);
    }

    for (slot = 0; slot < TBT_ICM_MAX_DEVICES; slot++) {
        if (!icm->Devices[slot].InUse) {
            break;
        }
    }
    if (slot == TBT_ICM_MAX_DEVICES) {
        icm->Counters.TableFull++;
        tbtIcmLog(icm, "tbt.icm.table-full", a->LinkInfo);
        return;
    }
    d = &icm->Devices[slot];
    tbtIcmZero(d, sizeof(*d));
    tbtIcmFill(d, a);
    d->InUse = TRUE;
    icm->NextSerial++;
    if (icm->NextSerial == 0) {
        icm->NextSerial = 1;
    }
    d->Serial = icm->NextSerial;
    tbtIcmLog(icm, "tbt.icm.connected", (slot << 16) | a->LinkInfo);

    /* The route where the row asks for it (AR get route, FR the topology
     * query). A failure keeps the entry, unrouted and, if the firmware has
     * not approved it, unapproved (record 21 section 5.5). */
    routed = tbtIcmRouteOk(icm, slot);
    tbtIcmSettle(icm, slot, routed, a);
}

static VOID tbtIcmDrainQueue(PTBT_ICM icm, ULONG budget)
{
    TBT_ICM_ANNOUNCE a;
    ULONG n;

    for (n = 0; n < budget && icm->QueueCount != 0; n++) {
        a = icm->Queue[icm->QueueHead];
        icm->QueueHead = (icm->QueueHead + 1) % TBT_ICM_EVENT_QUEUE;
        icm->QueueCount--;
        tbtIcmConnected(icm, &a);
    }
}

/* Service the RX ring as a wait does, for `ms`. */
static VOID tbtIcmServiceFor(PTBT_ICM icm, ULONG ms)
{
    TBT_DEADLINE d;
    TBT_FRAME f;
    BOOLEAN saved;
    ULONG frames;

    saved = icm->InWait;
    icm->InWait = TRUE;
    TbtDeadlineStart(&d, icm->Host, ms);
    frames = 0;
    while (!TbtDeadlinePassed(&d, icm->Host)) {
        if (frames < TBT_WAIT_FRAME_BUDGET && TbtRingReceive(icm->Ring, &f)) {
            frames++;
            tbtIcmFrame(icm, &f);
            continue;
        }
        TbtDeadlineWait(&d, icm->Host);
    }
    icm->InWait = saved;
}

/* ====================================================================== */
/* The mailbox (ICM 2.3; Linux nhi.c nhi_mailbox_cmd)                     */
/* ====================================================================== */

LONG TbtIcmMailbox(PTBT_ICM icm, ULONG code, ULONG data)
{
    ULONG v;
    TBT_DEADLINE d;

    icm->Counters.MailboxSent++;
    TBT_ICM_WR(icm, TBT_ICM_INMAIL_DATA, data);
    v = TBT_ICM_RD(icm, TBT_ICM_INMAIL_CMD);
    v &= ~(TBT_ICM_INMAIL_CODE_MASK | TBT_ICM_INMAIL_ERROR);
    v |= TBT_ICM_INMAIL_REQUEST | (code & TBT_ICM_INMAIL_CODE_MASK);
    TBT_ICM_WR(icm, TBT_ICM_INMAIL_CMD, v);

    TbtDeadlineStart(&d, icm->Host, TBT_ICM_MBOX_TIMEOUT_MS);
    for (;;) {
        v = TBT_ICM_RD(icm, TBT_ICM_INMAIL_CMD);
        if ((v & TBT_ICM_INMAIL_REQUEST) == 0) {
            break;
        }
        if (TbtDeadlinePassed(&d, icm->Host)) {
            icm->Counters.MailboxTimeouts++;
            tbtIcmLog(icm, "tbt.icm.mailbox-timeout", code);
            return TBT_E_TIMEOUT;
        }
        TbtDeadlineWait(&d, icm->Host);
    }
    if ((v & TBT_ICM_INMAIL_ERROR) != 0) {
        icm->Counters.MailboxErrors++;
        tbtIcmLog(icm, "tbt.icm.mailbox-error", code);
        return TBT_E_ERROR_FLAG;
    }
    icm->Counters.MailboxOk++;
    return TBT_OK;
}

/* ====================================================================== */
/* Bind, resume, suspend, unbind                                          */
/* ====================================================================== */

/* Queued announcements, actions and recovery requests belong to the
 * session that is ending: none of them is run after it. */
static VOID tbtIcmRetirePending(PTBT_ICM icm)
{
    icm->QueueCount = 0;
    icm->ActionCount = 0;
    icm->ResyncPending = FALSE;
    icm->ResyncWhy = 0;
    icm->RediscoverPending = FALSE;
    icm->RediscoverRun = 0;
}

/* A failure after activation (record 21 section 1.4.2): this driver's
 * rings stopped - nothing of theirs is pending afterwards, so the common
 * buffer may be freed - its queued work dropped, tunnels untouched. The
 * session stays recorded, so the stop sends 0x07 only if driver ready was
 * sent. */
static VOID tbtIcmFail(PTBT_ICM icm)
{
    if (icm->Started) {
        TbtRingStop(icm->Ring);
    }
    icm->Started = FALSE;
    icm->Bound = FALSE;
    icm->InWait = FALSE;
    tbtIcmRetirePending(icm);
    icm->Failed = TRUE;
}

/* Undo exactly the stages reached: the rings if they started, mailbox
 * 0x07 if driver ready was sent (orchestrator decision C). No tunnel is
 * touched, and nothing of the rings' is pending afterwards. */
static VOID tbtIcmTearDown(PTBT_ICM icm)
{
    if (icm->Started) {
        TbtRingStop(icm->Ring);
    }
    if (icm->Session) {
        (VOID)TbtIcmMailbox(icm, TBT_ICM_MBOX_DRIVER_UNLOADS, 0);
    }
    icm->Started = FALSE;
    icm->Session = FALSE;
    icm->Bound = FALSE;
    icm->InWait = FALSE;
    tbtIcmRetirePending(icm);
}

/* After driver ready on a resume: every entry not re-announced in the
 * window, pushed back by each announcement (Linux icm.c
 * icm_postpone_rescan), is gone. */
static VOID tbtIcmReconcile(PTBT_ICM icm)
{
    ULONG windows;
    ULONG i;

    /* A finite number of windows, so the bound holds with a stopped clock. */
    windows = 0;
    do {
        icm->SettleExtend = FALSE;
        tbtIcmServiceFor(icm, TBT_ICM_RECONCILE_MS);
        windows++;
    } while (icm->SettleExtend &&
             windows < TBT_ICM_RECONCILE_MAX_MS / TBT_ICM_RECONCILE_MS);
    tbtIcmDrainQueue(icm, TBT_ICM_EVENT_QUEUE * 2);
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse && icm->Devices[i].Stale) {
            icm->Counters.Reconciled++;
            tbtIcmDrop(icm, i, TBT_ICM_WHY_RECONCILED, FALSE);
        }
    }
}

static LONG tbtIcmAnnounceAll(PTBT_ICM icm, BOOLEAN markStale);
static VOID tbtIcmFinishAnnounce(PTBT_ICM icm, BOOLEAN reconcile);

static LONG tbtIcmBringUp(PTBT_ICM icm, PVOID va, ULONG phys, ULONG bytes,
                          BOOLEAN resume)
{
    LONG rc;

    rc = TbtRingStart(icm->Ring, icm->Host, va, phys, bytes,
                      TBT_RX_PDF_ALL_ONES, icm->RingFlags);
    if (rc != TBT_OK) {
        tbtIcmLog(icm, "tbt.icm.ring-start", (ULONG)(-rc));
        if (icm->Ring->Programmed) {
            /* Registers written, then refused: activated, so failed. */
            tbtIcmFail(icm);
        }
        return rc;
    }
    icm->Started = TRUE;

    /* Linux icm.c icm_firmware_init sends allow-all only where it reads
     * the mode, and only at bind (ICM 6.5 [inf]: not on resume). */
    if (!resume && (icm->Profile->Caps & TBT_ICM_CAP_GET_MODE) != 0) {
        rc = TbtIcmMailbox(icm, TBT_ICM_MBOX_ALLOW_ALL, 0);
        if (rc != TBT_OK) {
            tbtIcmFail(icm);
            return rc;
        }
    }

    icm->Session = TRUE;
    rc = tbtIcmAnnounceAll(icm, resume);
    if (rc != TBT_OK) {
        tbtIcmFail(icm);
        return rc;
    }
    icm->Bound = TRUE;
    tbtIcmFinishAnnounce(icm, resume);
    return TBT_OK;
}

/*
 * Driver ready and the host-router poll: the firmware then announces every
 * attached device (ICM 6.2 [obs]). With `markStale` every table entry is
 * first marked to be reconciled.
 */
static LONG tbtIcmAnnounceAll(PTBT_ICM icm, BOOLEAN markStale)
{
    ULONG req[1];
    ULONG resp[8];
    ULONG n;
    ULONG i;
    ULONG v;
    LONG rc;
    TBT_ROUTE root;

    if (markStale) {
        for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
            if (icm->Devices[i].InUse) {
                icm->Devices[i].Stale = TRUE;
            }
        }
    }

    req[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_DRIVER_READY);
    /* FR and AR: the command timeout, XhciCmCmdTimeoutMs; TR and ICL: the
     * row's own (record 21 section 1.4.4). */
    rc = tbtIcmCommand(icm, req, 1, 0, resp, 8, &n,
                       icm->Profile->ReadyTimeoutMs != 0 ?
                           icm->Profile->ReadyTimeoutMs : icm->CmdTimeoutMs,
                       icm->Profile->ReadyRetries, TBT_ICM_NO_SLOT, 0,
                       icm->Profile->ReadyWords, FALSE);
    if (rc != TBT_OK) {
        tbtIcmLog(icm, "tbt.icm.driver-ready-failed", (ULONG)(-rc));
        return rc;
    }
    if (!icm->Profile->DecodeReady(resp, n, &icm->Ready)) {
        tbtIcmZero(&icm->Ready, sizeof(icm->Ready));
    }
    tbtIcmLog(icm, "tbt.icm.ready-flags", icm->Ready.Flags);
    tbtIcmLog(icm, "tbt.icm.ready-word1", n > 1 ? resp[1] : 0);
    tbtIcmLog(icm, "tbt.icm.protocol-version", icm->Ready.ProtocolVersion);
    tbtIcmLog(icm, "tbt.icm.security-level", icm->Ready.SecurityLevel);
    tbtIcmLog(icm, "tbt.icm.boot-acl", (icm->Ready.BootAclSupported ? 0x100UL : 0) |
              icm->Ready.BootAclCount);

    /* The host-router poll (ICM 4.1 [inf], 6.2 [obs]: the firmware holds
     * it about 580 ms while it announces what is attached). Never answered
     * is logged, not fatal: the events are what matter. */
    root.Hi = 0;
    root.Lo = 0;
    icm->InWait = TRUE;
    for (i = 0; i < TBT_ICM_HOSTPOLL_TRIES; i++) {
        icm->Counters.HostPollTries++;
        rc = TbtRingCtlRead(icm->Ring, &root, TBT_CS_ROUTER, 0, 0, 1, &v,
                            TBT_ICM_HOSTPOLL_TIMEOUT_MS, 0);
        if (rc == TBT_OK) {
            icm->Counters.HostPollAnswered++;
            break;
        }
        if (rc != TBT_E_TIMEOUT && rc != TBT_E_NOTIFY) {
            break;
        }
        tbtIcmServiceFor(icm, TBT_ICM_HOSTPOLL_GAP_MS);
    }
    icm->InWait = FALSE;
    if (rc != TBT_OK) {
        tbtIcmLog(icm, "tbt.icm.host-poll-unanswered", i);
    }
    return TBT_OK;
}

/* After an announce-all: the reconciliation if entries were marked, then
 * the announcements collected. */
static VOID tbtIcmFinishAnnounce(PTBT_ICM icm, BOOLEAN reconcile)
{
    ULONG i;
    BOOLEAN any;

    any = FALSE;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse && icm->Devices[i].Stale) {
            any = TRUE;
        }
    }
    if (reconcile && any) {
        tbtIcmReconcile(icm);
    }
    tbtIcmDrainQueue(icm, TBT_ICM_EVENT_QUEUE * 2);
}

/*
 * An announcement was lost to a full queue: ask the firmware for all of
 * them again with another driver ready, as a resume does, without
 * restarting the rings or sending 0x23. Tunnels survive it (ICM 2.3, 7
 * [obs]). Bounded: TBT_ICM_REDISCOVER_MAX in a row, then logged and left.
 */
static VOID tbtIcmRediscover(PTBT_ICM icm)
{
    LONG rc;

    icm->RediscoverPending = FALSE;
    if (icm->RediscoverRun >= TBT_ICM_REDISCOVER_MAX) {
        icm->ResyncPending = TRUE;
        tbtIcmLog(icm, "tbt.icm.rediscover-gave-up", icm->RediscoverRun);
        return;
    }
    icm->RediscoverRun++;
    icm->Counters.Rediscoveries++;
    rc = tbtIcmAnnounceAll(icm, TRUE);
    if (rc != TBT_OK) {
        /* The rings still run and the session stays; the stale marks are
         * cleared so nothing is reconciled away on a failed request. */
        ULONG i;

        for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
            icm->Devices[i].Stale = FALSE;
        }
        return;
    }
    tbtIcmFinishAnnounce(icm, TRUE);
    if (!icm->RediscoverPending) {
        icm->RediscoverRun = 0;
    }
}

LONG TbtIcmStart(PTBT_ICM icm, PVOID va, ULONG phys, ULONG bytes)
{
    return tbtIcmBringUp(icm, va, phys, bytes, FALSE);
}

LONG TbtIcmResume(PTBT_ICM icm)
{
    if (icm->Ring->Va == NULL || icm->Started || icm->Failed) {
        return TBT_E_STOPPED;
    }
    return tbtIcmBringUp(icm, (PVOID)icm->Ring->Va, icm->Ring->Phys,
                         TBT_RING_DMA_BYTES, TRUE);
}

/* Linux icm.c icm_suspend: save devices where the row has it, then driver
 * unloads; Ice Lake's ops have no suspend and send neither, so its session
 * stays open for the unbind's 0x07 (icm_stop). */
VOID TbtIcmSuspend(PTBT_ICM icm)
{
    if (!icm->Started) {
        return;
    }
    TbtRingStop(icm->Ring);
    icm->Started = FALSE;
    icm->Bound = FALSE;
    /* An announcement of this session means nothing after the sleep: the
     * device may have left meanwhile (ICM 6.6 [obs]); the resume's
     * reconciliation asks again. */
    icm->QueueCount = 0;
    if (!icm->Session) {
        return;
    }
    if ((icm->Profile->Caps & TBT_ICM_CAP_SAVE_DEVICES) != 0) {
        (VOID)TbtIcmMailbox(icm, TBT_ICM_MBOX_SAVE_DEVICES, 0);
    }
    if ((icm->Profile->Caps & TBT_ICM_CAP_SUSPEND_UNLOAD) != 0) {
        (VOID)TbtIcmMailbox(icm, TBT_ICM_MBOX_DRIVER_UNLOADS, 0);
        icm->Session = FALSE;
    }
}

VOID TbtIcmUnbind(PTBT_ICM icm)
{
    tbtIcmTearDown(icm);
}

VOID TbtIcmLost(PTBT_ICM icm)
{
    ULONG i;

    icm->QueueCount = 0;
    icm->RediscoverPending = FALSE;
    icm->RediscoverRun = 0;
    icm->InWait = FALSE;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse) {
            tbtIcmDrop(icm, i, TBT_ICM_WHY_UNAVAILABLE, FALSE);
        }
    }
    icm->ResyncPending = TRUE;
    icm->ResyncWhy = TBT_ICM_WHY_UNAVAILABLE;
    icm->Counters.Lost++;
    tbtIcmLog(icm, "tbt.icm.lost", icm->ActionCount);
}

LONG TbtIcmRecover(PTBT_ICM icm, BOOLEAN full)
{
    LONG rc;
    ULONG i;

    if (icm->Ring->Va == NULL) {
        return TBT_E_STOPPED;
    }
    icm->Counters.Recoveries++;
    if (full) {
        if (icm->Started || icm->Ring->DmaMayRun) {
            TbtRingStop(icm->Ring);
        }
        if (icm->Ring->DmaMayRun) {
            /* No verified stop: the buffer is not reused (cm_live.h). */
            tbtIcmLog(icm, "tbt.icm.recover-unverified", 1);
            return TBT_E_STATE;
        }
        icm->Started = FALSE;
        icm->Bound = FALSE;
        icm->Session = FALSE;
        icm->Failed = FALSE;
        tbtIcmRetirePending(icm);
        return tbtIcmBringUp(icm, (PVOID)icm->Ring->Va, icm->Ring->Phys,
                             TBT_RING_DMA_BYTES, FALSE);
    }
    if (!icm->Started || !icm->Bound) {
        return TBT_E_STOPPED;
    }
    /* Frames the old session left on the ring are not this recovery's
     * answers: every completed one is taken back unread, then a quiet
     * period must pass with nothing more arriving before driver ready goes
     * out - a late answer to an earlier command would otherwise pass for
     * the new one. A ring that keeps delivering refuses the light recovery
     * (the full one restarts the rings). */
    {
        TBT_FRAME f;
        ULONG n;

        n = 0;
        while (n <= icm->Ring->Entries && TbtRingReceive(icm->Ring, &f)) {
            n++;
        }
        tbtIcmLog(icm, "tbt.icm.recover-drained", n);
        if (n > icm->Ring->Entries) {
            return TBT_E_STATE;
        }
        icm->Host->Sleep(icm->Host->Context, TBT_ICM_RECOVER_QUIET_MS);
        if (TbtRingReceive(icm->Ring, &f)) {
            tbtIcmLog(icm, "tbt.icm.recover-not-quiet", f.Pdf);
            return TBT_E_STATE;
        }
        tbtIcmRetirePending(icm);
    }
    rc = tbtIcmAnnounceAll(icm, TRUE);
    if (rc != TBT_OK) {
        for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
            icm->Devices[i].Stale = FALSE;
        }
        return rc;
    }
    tbtIcmFinishAnnounce(icm, TRUE);
    return TBT_OK;
}

/* ====================================================================== */
/* The idle poll                                                          */
/* ====================================================================== */

VOID TbtIcmPoll(PTBT_ICM icm)
{
    TBT_FRAME f;
    ULONG frames;

    if (!icm->Bound) {
        return;
    }
    icm->Counters.IdlePolls++;
    frames = 0;
    while (frames < TBT_ICM_POLL_FRAMES && TbtRingReceive(icm->Ring, &f)) {
        frames++;
        tbtIcmFrame(icm, &f);
    }
    tbtIcmDrainQueue(icm, TBT_ICM_POLL_EVENTS);
    if (icm->RediscoverPending && icm->Bound) {
        tbtIcmRediscover(icm);
    }
}

VOID TbtIcmService(PTBT_ICM icm)
{
    TBT_FRAME f;
    ULONG frames;
    BOOLEAN saved;

    if (!icm->Bound) {
        return;
    }
    saved = icm->InWait;
    icm->InWait = TRUE;
    frames = 0;
    while (frames < TBT_ICM_POLL_FRAMES && TbtRingReceive(icm->Ring, &f)) {
        frames++;
        tbtIcmFrame(icm, &f);
    }
    icm->InWait = saved;
}
