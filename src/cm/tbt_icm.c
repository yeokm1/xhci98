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

/* The host-to-host links (record 35 section 3.10), defined below. */
static VOID tbtIcmXdEvent(PTBT_ICM icm, const TBT_FRAME *f, ULONG code);
static VOID tbtIcmXdDeviceAt(PTBT_ICM icm, const TBT_ICM_ANNOUNCE *a);
static VOID tbtIcmXdSink(PVOID context, const TBT_FRAME *frame);
static VOID tbtIcmXdRun(PTBT_ICM icm, BOOLEAN tick);
static VOID tbtIcmXdEndAll(PTBT_ICM icm, BOOLEAN gone, BOOLEAN quiet);
static VOID tbtIcmXdApply(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e);
static VOID tbtIcmXdGoneApply(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e);
static VOID tbtIcmXdMarkStale(PTBT_ICM icm, BOOLEAN on);
static VOID tbtIcmXdReconcile(PTBT_ICM icm);

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
 * A frame shorter than the whole of it is malformed (tbt_icm.h) and
 * refused; the name block is the frame's last 55 DWs.
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
    /* The firmware's fields, bounded as a disconnection's are
     * (tbtIcmFrDisconnected) before anything is looked up by them. */
    if (out->Link > TBT_ICM_MAX_LINK || out->Depth > TBT_ICM_MAX_DEPTH) {
        return FALSE;
    }
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

/* ---- host-to-host messages (record 35 section 3.10) ------------------ */

static VOID tbtIcmTakeUuid(PTBT_UUID u, const ULONG *dw)
{
    ULONG i;

    for (i = 0; i < 4; i++) {
        u->Dw[i] = dw[i];
    }
}

static VOID tbtIcmPutUuid(PULONG dw, const TBT_UUID *u)
{
    ULONG i;

    for (i = 0; i < 4; i++) {
        dw[i] = u->Dw[i];
    }
}

/* Linux tb_msgs.h struct icm_fr_event_xdomain_connected and
 * icm_tr_event_xdomain_connected, the same 14 DWs: header; reserved and
 * link_info (31:16); remote_uuid; local_uuid; local_route_hi, _lo;
 * remote_route_hi, _lo. A local route of 0 names this host and is
 * refused. */
static BOOLEAN tbtIcmXdConnectedCommon(const ULONG *dw, ULONG words,
                                       PTBT_ICM_XD_EVENT out)
{
    if (words < TBT_ICM_XD_CONNECTED_WORDS) {
        return FALSE;
    }
    tbtIcmZero(out, sizeof(*out));
    out->Kind = TBT_ICM_XE_CONNECTED;
    tbtIcmTakeUuid(&out->Remote, &dw[2]);
    tbtIcmTakeUuid(&out->Local, &dw[6]);
    out->Route.Hi = dw[10] & ~TBT_ROUTE_HI_CM;
    out->Route.Lo = dw[11];
    out->RemoteRoute.Hi = dw[12] & ~TBT_ROUTE_HI_CM;
    out->RemoteRoute.Lo = dw[13];
    return (BOOLEAN)(out->Route.Hi != 0 || out->Route.Lo != 0);
}

/* Falcon and Alpine Ridge key the link by its link info; Linux icm.c
 * icm_fr_xdomain_connected refuses a link above ICM_MAX_LINK or a depth
 * above TB_SWITCH_MAX_DEPTH. Link 0 names no physical port for the
 * mailbox teardown and is refused too. */
static BOOLEAN tbtIcmFrXdConnected(const ULONG *dw, ULONG words,
                                   PTBT_ICM_XD_EVENT out)
{
    ULONG li;

    if (!tbtIcmXdConnectedCommon(dw, words, out)) {
        return FALSE;
    }
    li = dw[1] >> 16;
    out->Link = TBT_ICM_LI_LINK(li);
    out->Depth = TBT_ICM_LI_DEPTH(li);
    return (BOOLEAN)(out->Link != 0 && out->Link <= TBT_ICM_MAX_LINK &&
                     out->Depth <= TBT_ICM_MAX_DEPTH);
}

/* Titan Ridge and Ice Lake key it by the route alone (Linux icm.c
 * icm_tr_xdomain_connected passes link and depth 0). */
static BOOLEAN tbtIcmTrXdConnected(const ULONG *dw, ULONG words,
                                   PTBT_ICM_XD_EVENT out)
{
    return tbtIcmXdConnectedCommon(dw, words, out);
}

/* struct icm_fr_event_xdomain_disconnected: header, reserved and
 * link_info, remote_uuid. */
static BOOLEAN tbtIcmFrXdDisconnected(const ULONG *dw, ULONG words,
                                      PTBT_ICM_XD_EVENT out)
{
    ULONG li;

    if (words < TBT_ICM_FR_XD_GONE_WORDS) {
        return FALSE;
    }
    tbtIcmZero(out, sizeof(*out));
    out->Kind = TBT_ICM_XE_DISCONNECTED;
    li = dw[1] >> 16;
    out->Link = TBT_ICM_LI_LINK(li);
    out->Depth = TBT_ICM_LI_DEPTH(li);
    tbtIcmTakeUuid(&out->Remote, &dw[2]);
    return TRUE;
}

/* struct icm_tr_event_xdomain_disconnected: header, route_hi, route_lo,
 * remote_uuid. */
static BOOLEAN tbtIcmTrXdDisconnected(const ULONG *dw, ULONG words,
                                      PTBT_ICM_XD_EVENT out)
{
    if (words < TBT_ICM_TR_XD_GONE_WORDS) {
        return FALSE;
    }
    tbtIcmZero(out, sizeof(*out));
    out->Kind = TBT_ICM_XE_DISCONNECTED;
    out->Route.Hi = dw[1] & ~TBT_ROUTE_HI_CM;
    out->Route.Lo = dw[2];
    tbtIcmTakeUuid(&out->Remote, &dw[3]);
    return TRUE;
}

/* The path numbers as struct icm_*_pkg_approve_xdomain packs them: two
 * u16 a DW, the first in bits 15:0. */
static ULONG tbtIcmXdPair(ULONG hop, ULONG ring)
{
    return (hop & 0xFFFFUL) | ((ring & 0xFFFFUL) << 16);
}

/* struct icm_fr_pkg_approve_xdomain, 8 DWs: header; reserved and
 * link_info (the record's link and depth, icm_fr_approve_xdomain_paths);
 * remote_uuid; transmit_path, transmit_ring; receive_path, receive_ring. */
static ULONG tbtIcmFrXdApprove(const TBT_ICM_XD_ADDR *addr, ULONG txHop,
                               ULONG txRing, ULONG rxHop, ULONG rxRing,
                               PULONG dw)
{
    dw[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_APPROVE_XD);
    dw[1] = TBT_ICM_LI_MAKE(addr->Link, addr->Depth) << 16;
    tbtIcmPutUuid(&dw[2], &addr->Remote);
    dw[6] = tbtIcmXdPair(txHop, txRing);
    dw[7] = tbtIcmXdPair(rxHop, rxRing);
    return 8;
}

/* struct icm_tr_pkg_approve_xdomain, 9 DWs: header; route_hi, route_lo;
 * remote_uuid; the four numbers. */
static ULONG tbtIcmTrXdApprove(const TBT_ICM_XD_ADDR *addr, ULONG txHop,
                               ULONG txRing, ULONG rxHop, ULONG rxRing,
                               PULONG dw)
{
    dw[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_APPROVE_XD);
    dw[1] = addr->Route.Hi;
    dw[2] = addr->Route.Lo;
    tbtIcmPutUuid(&dw[3], &addr->Remote);
    dw[7] = tbtIcmXdPair(txHop, txRing);
    dw[8] = tbtIcmXdPair(rxHop, rxRing);
    return 9;
}

/* struct icm_tr_pkg_disconnect_xdomain, 8 DWs: header; stage (7:0) and
 * reserved; route_hi, route_lo; remote_uuid. */
static ULONG tbtIcmTrXdTeardown(const TBT_ICM_XD_ADDR *addr, ULONG stage,
                                PULONG dw)
{
    dw[0] = TBT_ICM_REQUEST(TBT_ICM_CMD_DISCONNECT_XD);
    dw[1] = stage & 0xFFUL;
    dw[2] = addr->Route.Hi;
    dw[3] = addr->Route.Lo;
    tbtIcmPutUuid(&dw[4], &addr->Remote);
    return 8;
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
    tbtIcmFrReady, tbtIcmFrConnected, tbtIcmFrDisconnected, tbtIcmFrApprove,
    tbtIcmFrXdConnected, tbtIcmFrXdDisconnected, tbtIcmFrXdApprove, NULL
};

const TBT_ICM_PROFILE TbtIcmProfileAlpineRidge = {
    "Alpine Ridge",
    TBT_ICM_GEN_ALPINE_RIDGE,
    TBT_ICM_CAP_GET_ROUTE | TBT_ICM_CAP_KEY | TBT_ICM_CAP_APPROVE |
        TBT_ICM_CAP_GET_MODE | TBT_ICM_CAP_SAVE_DEVICES |
        TBT_ICM_CAP_SUSPEND_UNLOAD | TBT_ICM_CAP_EXTRA_PACKETS,
    0, TBT_ICM_RETRIES, 2,
    tbtIcmArReady, tbtIcmFrConnected, tbtIcmFrDisconnected, tbtIcmFrApprove,
    tbtIcmFrXdConnected, tbtIcmFrXdDisconnected, tbtIcmFrXdApprove, NULL
};

const TBT_ICM_PROFILE TbtIcmProfileTitanRidge = {
    "Titan Ridge",
    TBT_ICM_GEN_TITAN_RIDGE,
    TBT_ICM_CAP_ROUTE_IN_EVENTS | TBT_ICM_CAP_APPROVE |
        TBT_ICM_CAP_GET_MODE | TBT_ICM_CAP_SUSPEND_UNLOAD |
        TBT_ICM_CAP_EXTRA_PACKETS,
    250UL, 10UL, 4,
    tbtIcmTrReady, tbtIcmTrConnected, tbtIcmTrDisconnected, tbtIcmTrApprove,
    tbtIcmTrXdConnected, tbtIcmTrXdDisconnected, tbtIcmTrXdApprove,
    tbtIcmTrXdTeardown
};

/* icm_icl_ops has no approve_switch and no suspend: the firmware decides
 * which devices it tunnels, and a system suspend sends no mailbox. It has
 * Titan Ridge's approve_xdomain_paths and disconnect_xdomain_paths. */
const TBT_ICM_PROFILE TbtIcmProfileIceLake = {
    "Ice Lake",
    TBT_ICM_GEN_ICE_LAKE,
    TBT_ICM_CAP_ROUTE_IN_EVENTS | TBT_ICM_CAP_FORCE_POWER |
        TBT_ICM_CAP_EXTRA_PACKETS,
    20000UL, TBT_ICM_RETRIES, 4,
    tbtIcmIclReady, tbtIcmTrConnected, tbtIcmTrDisconnected, NULL,
    tbtIcmTrXdConnected, tbtIcmTrXdDisconnected, tbtIcmTrXdApprove,
    tbtIcmTrXdTeardown
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
    icm->ResumeBudgetMs = TBT_ICM_RESUME_BUDGET_MS;
    icm->Retries = TBT_ICM_RETRIES;
    icm->WaitSlot = TBT_ICM_NO_SLOT;
    ring->Host = host;
    TbtRingSetUnsolicited(ring, tbtIcmUnsolicited, icm);
    /* Record 35 section 3.10: every inter-domain packet, whoever reads the
     * ring, is held for the records (or, with XhciCmNet 0, dropped). */
    TbtRingSetXdomainSink(ring, tbtIcmXdSink, icm);
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

VOID TbtIcmSetResumeBudget(PTBT_ICM icm, ULONG ms)
{
    icm->ResumeBudgetMs = ms != 0 ? ms : TBT_ICM_RESUME_BUDGET_MS;
}

VOID TbtIcmSetReadVersion(PTBT_ICM icm, BOOLEAN on)
{
    icm->ReadVersion = on;
}

/*
 * What is left of the resume's budget, ms. What is spent is a sum of
 * intervals that do not overlap: the host's millisecond clock's progress
 * since it was last read (its differences taken modulo 2^32), plus, for
 * each wait, what it waited beyond the clock's progress across it
 * (tbtIcmCharge). A clock that stops, from the start or partway, or moves
 * less than a wait waited, still ends the budget, as every wait's own
 * deadline is ended by the waits it made (TbtDeadlinePassed); never
 * early, since a wait counts only what it waited. 0 once spent, the
 * budget then marked so.
 */
static ULONG tbtIcmBudgetLeft(PTBT_ICM icm)
{
    ULONG now;

    if (icm->BudgetSpent) {
        return 0;
    }
    now = icm->Host->NowMs(icm->Host->Context);
    icm->BudgetUsedMs += now - icm->BudgetMarkMs;
    icm->BudgetMarkMs = now;
    if (icm->BudgetUsedMs >= icm->BudgetMs) {
        icm->BudgetSpent = TRUE;
        return 0;
    }
    return icm->BudgetMs - icm->BudgetUsedMs;
}

/*
 * A wait of `ms` clipped to what is left of the resume's budget, when one
 * runs; 0 once it is spent. Outside a resume, `ms` unchanged.
 */
static ULONG tbtIcmClip(PTBT_ICM icm, ULONG ms)
{
    ULONG left;

    if (!icm->BudgetOn) {
        return ms;
    }
    left = tbtIcmBudgetLeft(icm);
    return ms < left ? ms : left;
}

/*
 * After a clipped wait that began at the clock's `startMs` and waited
 * `waitedMs`: what it waited beyond the clock's progress across it is
 * charged to the budget (that progress is the clock's share, counted by
 * tbtIcmBudgetLeft).
 */
static VOID tbtIcmCharge(PTBT_ICM icm, ULONG startMs, ULONG waitedMs)
{
    ULONG moved;

    if (!icm->BudgetOn) {
        return;
    }
    moved = icm->Host->NowMs(icm->Host->Context) - startMs;
    if (waitedMs > moved) {
        icm->BudgetUsedMs += waitedMs - moved;
    }
}

/* TRUE when a resume's budget runs and nothing is left of it. */
static BOOLEAN tbtIcmBudgetGone(PTBT_ICM icm)
{
    return (BOOLEAN)(icm->BudgetOn && tbtIcmBudgetLeft(icm) == 0);
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
    for (i = 0; i < icm->QueueCount && !a->IsXd; i++) {
        q = &icm->Queue[(icm->QueueHead + i) % TBT_ICM_EVENT_QUEUE];
        if (!q->IsXd && !q->ReplaceOnly && tbtIcmUuidEqual(q->Uuid, a->Uuid)) {
            if (icm->NetOn) {
                /* Its place-taking of a host there stays, in its turn
                 * (record 35 section 3.10). */
                q->ReplaceOnly = TRUE;
                break;
            }
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
        if (!a->IsXd && !a->ReplaceOnly &&
            tbtIcmAtOrUnder(icm, a->RouteValid, &a->Route, a->Link, a->Depth, g)) {
            if (!icm->NetOn) {
                continue;
            }
            /* Never acted on, but a host it took the place of is gone:
             * kept, in its turn, for that alone (record 35 section 3.10). */
            a->ReplaceOnly = TRUE;
        }
        if (keep != i) {
            icm->Queue[(icm->QueueHead + keep) % TBT_ICM_EVENT_QUEUE] = *a;
        }
        keep++;
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
        /* One order for every announcement (record 35 section 3.10):
         * acted on at once only with nothing older queued. */
        if (icm->InWait || icm->XdTaking || icm->QueueCount != 0) {
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
               code == TBT_ICM_EV_XD_DISCONNECTED) {
        tbtIcmXdEvent(icm, f, code);
    } else if (code == TBT_ICM_EV_DP_CHANGED) {
        /* DisplayPort needs nothing here. */
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
    if (f->Pdf == TBT_PDF_ICM_RESP && f->Words == 1 &&
        TBT_ICM_HDR_CODE(f->Dw[0]) == TBT_ICM_XD_ACK_CODE) {
        /* Run-45 45-E.15 [obs]: Alpine Ridge firmware answers every
         * inter-domain frame its host sends with one such DW; no command
         * of this driver's has code 8, so none waits for it. Counted, and
         * logged once a session. */
        if (!icm->XdAckLogged) {
            icm->XdAckLogged = TRUE;
            tbtIcmLog(icm, "tbt.icm.xd-ack", f->Dw[0]);
        }
        return;
    }
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
    BOOLEAN sent;
    ULONG published;
    LONG rc;
    TBT_FRAME f;
    TBT_DEADLINE d;

    if (icm->InWait) {
        return TBT_E_PARAM;
    }
    sent = FALSE;
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
        if (tbtIcmBudgetGone(icm)) {
            /* The resume's budget is spent: no further attempt. */
            rc = TBT_E_TIMEOUT;
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
        published = icm->Ring->Counters.Tx;
        rc = TbtRingSend(icm->Ring, TBT_PDF_ICM_CMD, req, words);
        if (icm->Ring->Counters.Tx != published) {
            /* The descriptor went to the host interface. A send that
             * then reports TBT_E_HW (not seen consumed in time) may still
             * be consumed and answered later (tbt_ring.h). */
            sent = TRUE;
        }
        if (rc != TBT_OK) {
            break;
        }
        icm->Counters.CommandsSent++;
        rc = TBT_E_TIMEOUT;
        TbtDeadlineStart(&d, icm->Host, tbtIcmClip(icm, timeoutMs));
        frames = 0;
        for (;;) {
            if (icm->WaitGone) {
                rc = TBT_E_GONE;
                break;
            }
            /* Tested before the take, so a frame past the budget stays on
             * the ring for the next service rather than being taken and
             * dropped undecoded. */
            if (frames >= TBT_WAIT_FRAME_BUDGET) {
                break;
            }
            if (!TbtRingReceive(icm->Ring, &f)) {
                if (TbtDeadlinePassed(&d, icm->Host)) {
                    break;
                }
                TbtDeadlineWait(&d, icm->Host);
                continue;
            }
            frames++;
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
        tbtIcmCharge(icm, d.StartMs, d.WaitedUs / 1000UL);
        if (rc != TBT_E_TIMEOUT) {
            break;
        }
        /* This attempt's answer may still come, and a retry's success
         * does not say which attempt it answered. */
        icm->Ambiguous = TRUE;
        icm->Counters.Timeouts++;
        tbtIcmLog(icm, "tbt.icm.timeout", req[0]);
    }
    if (rc == TBT_E_TIMEOUT && icm->WaitGone) {
        rc = TBT_E_GONE;
    }
    if (sent && rc != TBT_OK && rc != TBT_E_ERROR_FLAG &&
        rc != TBT_E_PARAM) {
        /* Sent and not answered (or abandoned): its answer may still
         * arrive, and no word of a driver-ready answer tells it from the
         * next one's. Only a ring restart ends that (TbtIcmRecover). A
         * command the ring refused before publishing it (full, stopped,
         * an unreadable index) was never sent and has no answer to come;
         * one published and not seen consumed was. */
        icm->Ambiguous = TRUE;
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

/* ====================================================================== */
/* The approval order at depth 2 and deeper (record 34 section 4.6)       */
/* ====================================================================== */

/* Whether entry `p` is entry `c`'s parent: by route, `c`'s route with its
 * last level removed; by link and depth, one level up on the same physical
 * port (the subtree removal's rule, tbtIcmAtOrUnder). */
static BOOLEAN tbtIcmIsParent(PTBT_ICM icm, ULONG p, ULONG c)
{
    const TBT_ICM_DEVICE *pd;
    const TBT_ICM_DEVICE *cd;
    ULONG depth;
    ULONG i;

    pd = &icm->Devices[p];
    cd = &icm->Devices[c];
    if (p == c || !pd->InUse || !cd->InUse || cd->Depth < 2) {
        return FALSE;
    }
    if (tbtIcmByRoute(icm)) {
        if (!pd->RouteValid || !cd->RouteValid) {
            return FALSE;
        }
        depth = TbtRouteDepth(&cd->Route);
        if (depth < 2 || TbtRouteDepth(&pd->Route) != depth - 1) {
            return FALSE;
        }
        for (i = 0; i + 1 < depth; i++) {
            if (TbtRouteLevel(&pd->Route, i) != TbtRouteLevel(&cd->Route, i)) {
                return FALSE;
            }
        }
        return TRUE;
    }
    return (BOOLEAN)(pd->Depth + 1 == cd->Depth &&
                     tbtIcmPhyPort(pd->Link) == tbtIcmPhyPort(cd->Link));
}

static ULONG tbtIcmParentOf(PTBT_ICM icm, ULONG slot)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (tbtIcmIsParent(icm, i, slot)) {
            return i;
        }
    }
    return TBT_ICM_NO_SLOT;
}

/* A parent's approval is established: approved, its outcome known, and
 * confirmed in this session (not Unconfirmed: kept across a resume, a
 * rediscovery or a recovery and not yet announced again). The host router
 * is a depth-1 entry's parent and always is. */
static BOOLEAN tbtIcmEstablished(const TBT_ICM_DEVICE *d)
{
    return (BOOLEAN)(d->InUse && d->Approved && !d->ApproveUnknown &&
                     !d->Unconfirmed);
}

/* An announcement of `uuid` is waiting in the queue. */
static BOOLEAN tbtIcmQueuedFor(PTBT_ICM icm, const ULONG *uuid)
{
    ULONG i;

    for (i = 0; i < icm->QueueCount; i++) {
        if (!icm->Queue[(icm->QueueHead + i) % TBT_ICM_EVENT_QUEUE].IsXd &&
            !icm->Queue[(icm->QueueHead + i) % TBT_ICM_EVENT_QUEUE].ReplaceOnly &&
            tbtIcmUuidEqual(icm->Queue[(icm->QueueHead + i) %
                                       TBT_ICM_EVENT_QUEUE].Uuid, uuid)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* An approve refused, failed or withheld for `slot`: each child owed
 * behind it is logged, and stays owed. */
static VOID tbtIcmOwedParentNote(PTBT_ICM icm, ULONG slot)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse && icm->Devices[i].ApproveOwed &&
            tbtIcmIsParent(icm, slot, i)) {
            tbtIcmLog(icm, "tbt.icm.approve-owed-parent", (slot << 8) | i);
        }
    }
}

static VOID tbtIcmPolicy(PTBT_ICM icm, ULONG slot, const TBT_ICM_ANNOUNCE *a);

/*
 * `slot`'s approval became established: each child owed behind it is asked
 * again, the policy first, in table order. A child owed behind another
 * occupancy of that place, one not confirmed in this session, one without
 * its route (record 21 section 5.5), or one with a newer announcement
 * still queued (whose refresh settles it with the latest fields) is left
 * for that announcement; one approved or of unknown outcome meanwhile
 * owes nothing. Every candidate is re-checked after the one before it,
 * whose approve may have waited through a disconnect.
 */
static VOID tbtIcmOwedAfter(PTBT_ICM icm, ULONG slot)
{
    ULONG serial;
    ULONG i;
    PTBT_ICM_DEVICE c;

    serial = icm->Devices[slot].Serial;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (!icm->Devices[slot].InUse || icm->Devices[slot].Serial != serial ||
            !tbtIcmEstablished(&icm->Devices[slot])) {
            return;
        }
        c = &icm->Devices[i];
        if (!c->InUse || !c->ApproveOwed || !tbtIcmIsParent(icm, slot, i)) {
            continue;
        }
        if (c->Approved || c->ApproveUnknown) {
            c->ApproveOwed = FALSE;
            c->OwedParent = 0;
            continue;
        }
        if (c->Unconfirmed || (!c->RouteValid && tbtIcmAsksRoute(icm)) ||
            tbtIcmQueuedFor(icm, c->Uuid)) {
            continue;
        }
        if (c->OwedParent != 0 && c->OwedParent != serial) {
            tbtIcmLog(icm, "tbt.icm.approve-owed-stale", (slot << 8) | i);
            continue;
        }
        c->ApproveOwed = FALSE;
        c->OwedParent = 0;
        tbtIcmPolicy(icm, i, NULL);
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
        tbtIcmOwedAfter(icm, slot);
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
        tbtIcmOwedParentNote(icm, slot);
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
    d->Unconfirmed = FALSE;
    if (a->RouteValid) {
        d->Route = a->Route;
        d->RouteValid = TRUE;
    }
}

/* Record 21 section 5.4: an announced, unapproved, unrejected device is
 * approved when the policy says so - on every such announcement, a refresh
 * included, so an approval that failed is tried again when the firmware
 * announces the device again. Below depth 1 only once its parent's
 * approval is established (record 34 section 4.6); `a` may be NULL. */
static VOID tbtIcmPolicy(PTBT_ICM icm, ULONG slot, const TBT_ICM_ANNOUNCE *a)
{
    PTBT_ICM_DEVICE d;
    BOOLEAN approve;
    ULONG parent;

    d = &icm->Devices[slot];
    d->ApproveOwed = FALSE;
    d->OwedParent = 0;
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
        tbtIcmOwedParentNote(icm, slot);
        return;
    }
    if (d->Depth > 1) {
        parent = tbtIcmParentOf(icm, slot);
        if (parent == TBT_ICM_NO_SLOT ||
            !tbtIcmEstablished(&icm->Devices[parent])) {
            d->ApproveOwed = TRUE;
            d->OwedParent = parent == TBT_ICM_NO_SLOT
                                ? 0 : icm->Devices[parent].Serial;
            icm->Counters.ApprovalsOwed++;
            tbtIcmLog(icm, "tbt.icm.approve-owed",
                      (slot << 8) |
                          (parent == TBT_ICM_NO_SLOT ? 0xFFUL : parent));
            /* The event as received, for the record a later failure of
             * the deferred approve refers to (record 21 section 1.4.6):
             * the slot above and its serial correlate them. */
            tbtIcmLog(icm, "tbt.icm.approve-owed-serial", d->Serial);
            if (a != NULL) {
                tbtIcmLogApprove(icm, NULL, 0, a);
            }
            return;
        }
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
         * answer was lost (record 21 section 6.5): nothing owed for it. */
        d->ApproveOwed = FALSE;
        d->OwedParent = 0;
        tbtIcmQueueAction(icm, TBT_ICM_ACT_TUNNEL_UP, TBT_ICM_WHY_ANNOUNCED,
                          slot, FALSE);
        tbtIcmOwedAfter(icm, slot);
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
    tbtIcmXdDeviceAt(icm, a);
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
         * a failed route request, an approve outcome unknown,
         * unapproved after an approve answered with the error flag, owed
         * behind its parent, or unapproved (withheld, owed) and now
         * announced approved, which settles it in place with its subtree
         * (record 34 section 4.6). */
        recovery = (BOOLEAN)(d->ApproveUnknown || d->ApproveFailed ||
                             d->ApproveOwed || (!d->Approved && a->Approved) ||
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
            d->Unconfirmed = FALSE;
            icm->Counters.Refreshes++;
            tbtIcmLog(icm, "tbt.icm.refresh", slot);
            if (!recovery && d->RouteValid) {
                /* The resume case (ICM 6.5 [obs]): nothing else but the
                 * children owed behind it, now it is confirmed. */
                tbtIcmOwedAfter(icm, slot);
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
                tbtIcmOwedAfter(icm, slot);
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

    for (n = 0; n < budget && icm->QueueCount != 0 && !tbtIcmBudgetGone(icm);
         n++) {
        a = icm->Queue[icm->QueueHead];
        icm->QueueHead = (icm->QueueHead + 1) % TBT_ICM_EVENT_QUEUE;
        icm->QueueCount--;
        if (a.ReplaceOnly) {
            tbtIcmXdDeviceAt(icm, &a);
        } else if (a.IsXd && a.Xd.Kind == TBT_ICM_XE_DISCONNECTED) {
            tbtIcmXdGoneApply(icm, &a.Xd);
        } else if (a.IsXd) {
            tbtIcmXdApply(icm, &a.Xd);
        } else {
            tbtIcmConnected(icm, &a);
        }
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
    TbtDeadlineStart(&d, icm->Host, tbtIcmClip(icm, ms));
    frames = 0;
    while (!TbtDeadlinePassed(&d, icm->Host)) {
        if (frames < TBT_WAIT_FRAME_BUDGET && TbtRingReceive(icm->Ring, &f)) {
            frames++;
            tbtIcmFrame(icm, &f);
            continue;
        }
        TbtDeadlineWait(&d, icm->Host);
    }
    tbtIcmCharge(icm, d.StartMs, d.WaitedUs / 1000UL);
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
    icm->XdCount = 0;
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
    tbtIcmXdEndAll(icm, TRUE, TRUE);
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
    } while (icm->SettleExtend && !tbtIcmBudgetGone(icm) &&
             windows < TBT_ICM_RECONCILE_MAX_MS / TBT_ICM_RECONCILE_MS);
    tbtIcmDrainQueue(icm, TBT_ICM_EVENT_QUEUE * 2);
    if (tbtIcmBudgetGone(icm)) {
        /* An entry may be unannounced only because the time ran out: the
         * resume is abandoned instead (TbtIcmResume), and nothing is
         * dropped. */
        return;
    }
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse && icm->Devices[i].Stale) {
            icm->Counters.Reconciled++;
            tbtIcmDrop(icm, i, TBT_ICM_WHY_RECONCILED, FALSE);
        }
    }
    tbtIcmXdReconcile(icm);
}

static LONG tbtIcmAnnounceAll(PTBT_ICM icm, BOOLEAN markStale);
static VOID tbtIcmFinishAnnounce(PTBT_ICM icm, BOOLEAN reconcile);

static LONG tbtIcmBringUpEx(PTBT_ICM icm, PVOID va, ULONG phys, ULONG bytes,
                            BOOLEAN allowAll, BOOLEAN reconcile);

static LONG tbtIcmBringUp(PTBT_ICM icm, PVOID va, ULONG phys, ULONG bytes,
                          BOOLEAN resume)
{
    return tbtIcmBringUpEx(icm, va, phys, bytes, (BOOLEAN)!resume, resume);
}

/* `allowAll`: mailbox 0x23 on the rows that read the mode (a bind, and a
 * full recovery of a power-cycled ICM). `reconcile`: every entry marked
 * first and reconciled against the announcements (a resume, and a full
 * recovery, which keeps the table of the session it replaces). */
static LONG tbtIcmBringUpEx(PTBT_ICM icm, PVOID va, ULONG phys, ULONG bytes,
                            BOOLEAN allowAll, BOOLEAN reconcile)
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
    /* A restarted ring delivers nothing of an earlier command, nor of an
     * earlier router read. */
    icm->Ambiguous = FALSE;
    icm->VersionStopped = FALSE;
    icm->XdFenced = FALSE;

    /* Linux icm.c icm_firmware_init sends allow-all only where it reads
     * the mode, and only at bind (ICM 6.5 [inf]: not on resume). */
    if (allowAll && (icm->Profile->Caps & TBT_ICM_CAP_GET_MODE) != 0) {
        rc = TbtIcmMailbox(icm, TBT_ICM_MBOX_ALLOW_ALL, 0);
        if (rc != TBT_OK) {
            tbtIcmFail(icm);
            return rc;
        }
    }

    icm->Session = TRUE;
    rc = tbtIcmAnnounceAll(icm, reconcile);
    if (rc != TBT_OK) {
        tbtIcmFail(icm);
        return rc;
    }
    icm->Bound = TRUE;
    tbtIcmFinishAnnounce(icm, reconcile);
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
    ULONG pollMs;
    ULONG pollAt;
    ULONG pollWaited;
    LONG rc;
    TBT_ROUTE root;

    if (markStale) {
        for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
            if (icm->Devices[i].InUse) {
                icm->Devices[i].Stale = TRUE;
                icm->Devices[i].Unconfirmed = TRUE;
            }
        }
        tbtIcmXdMarkStale(icm, TRUE);
    }
    icm->HostPollOk = FALSE;

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
        if (tbtIcmBudgetGone(icm)) {
            rc = TBT_E_TIMEOUT;
            break;
        }
        icm->Counters.HostPollTries++;
        pollMs = tbtIcmClip(icm, TBT_ICM_HOSTPOLL_TIMEOUT_MS);
        pollAt = icm->Host->NowMs(icm->Host->Context);
        pollWaited = icm->Ring->WaitedUs;
        rc = TbtRingCtlRead(icm->Ring, &root, TBT_CS_ROUTER, 0, 0, 1, &v,
                            pollMs, 0);
        /* What the read actually waited (the ring's own sum), whatever
         * ended it: a timeout, a frame budget spent, an answer. */
        tbtIcmCharge(icm, pollAt,
                     (icm->Ring->WaitedUs - pollWaited) / 1000UL);
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
    /* Until a poll is answered, one of its requests may still draw a
     * notification, which no router read could tell from its own
     * (record 21 section 5.3, "the stale answer"). */
    icm->HostPollOk = (BOOLEAN)(rc == TBT_OK);
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
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (icm->Xd[i].Used && icm->Xd[i].Stale) {
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
        tbtIcmXdMarkStale(icm, FALSE);
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
    LONG rc;

    if (icm->Ring->Va == NULL || icm->Started || icm->Failed) {
        return TBT_E_STOPPED;
    }
    tbtIcmXdEndAll(icm, TRUE, TRUE);    /* none outlives the suspend */
    icm->BudgetOn = TRUE;
    icm->BudgetSpent = FALSE;
    icm->BudgetUsedMs = 0;
    icm->BudgetMs = icm->ResumeBudgetMs;
    icm->BudgetMarkMs = icm->Host->NowMs(icm->Host->Context);
    rc = tbtIcmBringUp(icm, (PVOID)icm->Ring->Va, icm->Ring->Phys,
                       TBT_RING_DMA_BYTES, TRUE);
    /* Read once more: the last wait may have used what was left with no
     * check after it. */
    (VOID)tbtIcmBudgetGone(icm);
    if (icm->BudgetSpent) {
        /* Abandoned whole: whatever this resume reached (approvals sent,
         * a part of the queue drained) is not taken for a resume; its
         * queued work and actions are retired with the instance. */
        icm->Counters.ResumeBudgetExpired++;
        tbtIcmLog(icm, "tbt.icm.resume-budget-expired", icm->BudgetMs);
        if (!icm->Failed) {
            tbtIcmFail(icm);
        }
        rc = TBT_E_TIMEOUT;
    }
    icm->BudgetOn = FALSE;
    return rc;
}

/* Linux icm.c icm_suspend: save devices where the row has it, then driver
 * unloads; Ice Lake's ops have no suspend and send neither, so its session
 * stays open for the unbind's 0x07 (icm_stop). */
VOID TbtIcmSuspend(PTBT_ICM icm)
{
    if (!icm->Started) {
        return;
    }
    /* Record 35 section 3.10: every link's record ends while ring 0 still
     * runs (a Logout, the teardown); the firmware announces the link again
     * after the resume. */
    tbtIcmXdEndAll(icm, FALSE, FALSE);
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
    tbtIcmXdEndAll(icm, FALSE, FALSE);
    tbtIcmTearDown(icm);
}

VOID TbtIcmLost(PTBT_ICM icm)
{
    ULONG i;
    ULONG keep;
    ULONG n;
    PTBT_ICM_ACTION a;

    /* A queued TUNNEL_UP of the lost session is never delivered: the
     * DOWNs and RESYNCs already queued keep their order. */
    n = icm->ActionCount;
    keep = 0;
    for (i = 0; i < n; i++) {
        a = &icm->Actions[(icm->ActionHead + i) % TBT_ICM_ACTION_QUEUE];
        if (a->Kind == TBT_ICM_ACT_TUNNEL_UP) {
            continue;
        }
        if (keep != i) {
            icm->Actions[(icm->ActionHead + keep) % TBT_ICM_ACTION_QUEUE] = *a;
        }
        keep++;
    }
    icm->ActionCount = keep;
    icm->QueueCount = 0;
    icm->RediscoverPending = FALSE;
    icm->RediscoverRun = 0;
    icm->InWait = FALSE;
    tbtIcmXdEndAll(icm, TRUE, TRUE);
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
    tbtIcmXdEndAll(icm, TRUE, TRUE);
    icm->Counters.Recoveries++;
    if (!full && icm->Ambiguous) {
        /* An unanswered command's answer could still arrive and pass for
         * driver ready's: only the restart below ends that. */
        tbtIcmLog(icm, "tbt.icm.recover-escalated", 1);
        full = TRUE;
    }
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
        return tbtIcmBringUpEx(icm, (PVOID)icm->Ring->Va, icm->Ring->Phys,
                               TBT_RING_DMA_BYTES, TRUE, TRUE);
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
        tbtIcmXdMarkStale(icm, FALSE);
        return rc;
    }
    tbtIcmFinishAnnounce(icm, TRUE);
    return TBT_OK;
}

/* ====================================================================== */
/* The router's generation (record 21 section 5.3)                        */
/* ====================================================================== */

/* The verdict on ROUTER_CS_0 to 4 read at `route`: the header must be the
 * one a router enumerated at that route carries (TopologyID Valid, and
 * the TopologyID its route gives, Spec 8.2.1.1), and the USB4 Version is
 * decided by its major alone, as tbt_hw.c TbtHwRouterVersion does. */
static ULONG tbtIcmVersionOf(const ULONG *v, const TBT_ROUTE *route)
{
    ULONG major;

    if ((v[3] & TBT_RTR3_V) == 0 || v[2] != route->Lo ||
        (v[3] & TBT_RTR3_TOPOLOGY_MASK) !=
            (route->Hi & TBT_RTR3_TOPOLOGY_MASK)) {
        return TBT_ICM_VER_UNKNOWN;
    }
    major = TBT_USB4_VERSION_MAJOR(TBT_RTR4_VERSION(v[4]));
    if (major == TBT_USB4_MAJOR_1 || major == TBT_USB4_MAJOR_2) {
        return TBT_ICM_VER_USB4;
    }
    if (major == TBT_USB4_MAJOR_TBT3) {
        return TBT_ICM_VER_TBT3;
    }
    return TBT_ICM_VER_UNKNOWN;
}

/*
 * The oldest occupancy still owed its read: one control read of five DWs
 * at its route, one attempt of TBT_ICM_VERSION_TIMEOUT_MS. Counted as a
 * wait for its duration, so an announcement arriving meanwhile is queued
 * and a disconnect acts at once (section 5.2). An answer for an entry that
 * left, or whose slot another occupancy took, is dropped. A read sent and
 * not answered stops the reads until the rings restart: nothing in a Read
 * Response names the occupancy, and the two-bit sequence number would let
 * its late answer match a later read of the same header at the same route.
 */
static VOID tbtIcmReadVersion(PTBT_ICM icm)
{
    ULONG i;
    ULONG slot;
    ULONG serial;
    ULONG v[5];
    ULONG verdict;
    ULONG published;
    LONG rc;
    TBT_ROUTE route;
    PTBT_ICM_DEVICE d;

    if (!icm->ReadVersion || icm->VersionStopped || !icm->HostPollOk ||
        icm->InWait || icm->Failed) {
        return;
    }
    slot = TBT_ICM_NO_SLOT;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        d = &icm->Devices[i];
        if (d->InUse && d->RouteValid &&
            d->VersionState == TBT_ICM_VER_OWED &&
            (slot == TBT_ICM_NO_SLOT ||
             d->Serial - icm->Devices[slot].Serial >= 0x80000000UL)) {
            slot = i;
        }
    }
    if (slot == TBT_ICM_NO_SLOT) {
        return;
    }
    d = &icm->Devices[slot];
    serial = d->Serial;
    route = d->Route;
    icm->Counters.VersionReads++;
    icm->InWait = TRUE;
    icm->WaitSlot = slot;
    icm->WaitSerial = serial;
    icm->WaitGone = FALSE;
    published = icm->Ring->Counters.Tx;
    rc = TbtRingCtlRead(icm->Ring, &route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0,
                        5, v, TBT_ICM_VERSION_TIMEOUT_MS, 0);
    icm->InWait = FALSE;
    icm->WaitSlot = TBT_ICM_NO_SLOT;
    icm->WaitSerial = 0;
    icm->WaitGone = FALSE;
    if (rc != TBT_OK && rc != TBT_E_NOTIFY &&
        icm->Ring->Counters.Tx != published) {
        icm->VersionStopped = TRUE;
        tbtIcmLog(icm, "tbt.icm.version-stopped", slot);
    }
    if (!d->InUse || d->Serial != serial) {
        tbtIcmLog(icm, "tbt.icm.version-abandoned", slot);
        return;
    }
    if (rc != TBT_OK) {
        d->VersionState = TBT_ICM_VER_UNKNOWN;
        icm->Counters.VersionFailures++;
        tbtIcmLog(icm, "tbt.icm.version-failed",
                  (slot << 24) | ((ULONG)(-rc) << 8) |
                      (rc == TBT_E_NOTIFY ? (icm->Ring->LastEvent & 0xFFUL) : 0));
        return;
    }
    for (i = 0; i < 5; i++) {
        tbtIcmLog(icm, "tbt.icm.version-header", v[i]);
    }
    verdict = tbtIcmVersionOf(v, &route);
    d->VersionState = verdict;
    d->Usb4Version = TBT_RTR4_VERSION(v[4]);
    if (verdict == TBT_ICM_VER_UNKNOWN) {
        icm->Counters.VersionFailures++;
    }
    tbtIcmLog(icm, "tbt.icm.version",
              (slot << 24) | (verdict << 8) | d->Usb4Version);
}

/* ====================================================================== */
/* Host-to-host links (design record 35 section 3.10; task 45.59 S4)      */
/* ====================================================================== */

/*
 * With XhciCmNet 1 the firmware's inter-domain events start and end one
 * TBT_XDP_PEER (tbt_xdp.c, the pure engine, in its FirmwareCm mode) per
 * direct link; the firmware builds the two DMA paths on the approve and
 * tears them down on the row's teardown. A connected event takes its turn
 * among the device announcements (acted on at once outside a wait, else
 * queued with them), so the two kinds keep their order; a disconnect acts
 * on its record where it is decoded and the record ends at the next safe
 * point. The engines run only outside a command wait (tbtIcmXdRun, from
 * TbtIcmPoll and TbtIcmRingWake), so an engine call that sends a command
 * (the approve, a teardown) always finds the client idle.
 */

#define TBT_ICM_XD_SLOT(i)  (0x100UL + (i))     /* a wait's slot: never a device's */

static VOID tbtIcmXdEnd(PTBT_ICM icm, PTBT_ICM_XD x, BOOLEAN gone);
static VOID tbtIcmXdPurge(PTBT_ICM icm, const TBT_ROUTE *route);

/* Whether the records may touch the host interface: bound, the rings
 * running, not lost. */
static BOOLEAN tbtIcmXdAccess(const TBT_ICM *icm)
{
    return (BOOLEAN)(icm->Bound && icm->Started && !icm->XdQuiet &&
                     (icm->Host->Cancelled == NULL ||
                      !icm->Host->Cancelled(icm->Host->Context)));
}

static BOOLEAN tbtIcmXdFalcon(const TBT_ICM *icm)
{
    return (BOOLEAN)(icm->Profile->Generation == TBT_ICM_GEN_FALCON_RIDGE);
}

static ULONG tbtIcmXdNowMs(const TBT_ICM *icm)
{
    return icm->Host->NowMs(icm->Host->Context);
}

/*
 * Record 35 section 4.4 under the firmware CM: the MAC from the NHI's
 * 64-bit Device Serial Number where the driver read one, router id 0,
 * port 0; else a value of this instance from the clock and ring 0's bus
 * address [inf], logged. IDS 5.1.5.3's derivation makes either locally
 * administered and unicast.
 */
static VOID tbtIcmXdIdentity(PTBT_ICM icm)
{
    ULONG lo;
    ULONG hi;
    ULONG t;

    if (icm->NetMacValid) {
        return;
    }
    if (icm->NetSerialValid) {
        lo = icm->NetSerialLo;
        hi = icm->NetSerialHi;
    } else {
        t = TbtClockUs(icm->Host);
        lo = (t * 2654435761UL) ^ icm->Ring->Phys ^ 0x58484349UL;
        hi = ~(t ^ (icm->Ring->Phys << 7)) ^ 0x39384E54UL;
        tbtIcmLog(icm, "cm.xdp.mac.random", lo);
    }
    TbtNetMacFromUuid(lo, hi, 0, 0, icm->NetMac);
    icm->NetMacValid = TRUE;
}

/* Whether a record other than `x` holds the service (record 35 section
 * 13: one per NHI), from its rings' set-up until its paths are down. */
static BOOLEAN tbtIcmXdOtherUp(const TBT_ICM *icm, const TBT_ICM_XD *x)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (&icm->Xd[i] != x && icm->Xd[i].Used &&
            (icm->Xd[i].RingsUp || icm->Xd[i].Peer.PathsUp)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Record 35 section 4.2: the network driver's link goes down at the first
 * logout of either side. The engine takes the paths down only when its
 * Logout is answered or times out, so the link is lowered here, after each
 * engine call, as soon as the service is no longer up; the paths, the
 * rings and the NHI's one service stay held until ServiceDown. */
static VOID tbtIcmXdSync(PTBT_ICM icm, PTBT_ICM_XD x)
{
    if (x->Used && x->LinkUp && x->Peer.NetState != TBT_NET_S_UP) {
        x->LinkUp = FALSE;
        tbtIcmLog(icm, "cm.net.link.down", x->Slot + 1);
        if (icm->Net.Link != NULL) {
            icm->Net.Link(icm->Net.Context, x->Slot + 1, FALSE, NULL);
        }
    }
}

/* Whether record `x` offers the service: the network driver holds it
 * open, no other record's is up, and the firmware announced `x` (record
 * 35 section 3.16: an adopted record offers none). */
static BOOLEAN tbtIcmXdOffers(const TBT_ICM *icm, const TBT_ICM_XD *x,
                              BOOLEAN adopted)
{
    return (BOOLEAN)(icm->NetWanted && !adopted && !tbtIcmXdOtherUp(icm, x));
}

/* Each record offered the service as tbtIcmXdOffers says; a change
 * rebuilds that block and tells the peer (TbtXdpPeerSetNetWanted). */
static VOID tbtIcmXdOffer(PTBT_ICM icm)
{
    ULONG i;
    BOOLEAN want;
    PTBT_ICM_XD x;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        x = &icm->Xd[i];
        if (!x->Used) {
            continue;
        }
        want = tbtIcmXdOffers(icm, x, x->Adopted);
        if (want != x->Peer.NetWanted ||
            (want && x->Peer.LocalProps.Prtcstns != icm->NetPrtcstns)) {
            TbtXdpPeerSetNetWanted(&x->Peer, want, icm->NetPrtcstns);
            tbtIcmXdSync(icm, x);
        }
    }
}

/* ---- the teardown ------------------------------------------------------ */

/*
 * An approve or teardown command through tbtIcmCommand, matched by its
 * whole echo. `*left` TRUE when an answer to it may still come: an attempt
 * timed out (even if a retry was then answered, which does not say which
 * attempt it answered), or the request was published and the wait ended
 * without an answer (TBT_E_HW, TBT_E_STOPPED). Such an answer would match
 * a later command of the same words (record 35 section 3.10), so the
 * fence holds every approve until the rings restart.
 */
static LONG tbtIcmXdCommand(PTBT_ICM icm, const ULONG *req, ULONG words,
                            ULONG timeoutMs, ULONG slot, ULONG serial,
                            BOOLEAN *left)
{
    ULONG resp[TBT_ICM_XD_MAX_WORDS];
    ULONG n;
    ULONG tx;
    ULONG timeouts;
    LONG rc;

    tx = icm->Ring->Counters.Tx;
    timeouts = icm->Counters.Timeouts;
    rc = tbtIcmCommand(icm, req, words, words - 1, resp, TBT_ICM_XD_MAX_WORDS, &n,
                       timeoutMs, icm->Retries, slot, serial, words, FALSE);
    /* A wait a disconnect ended (TBT_E_GONE) leaves its answer to come
     * too: the paths went with the link, the answer did not. */
    *left = (BOOLEAN)(icm->Counters.Timeouts != timeouts ||
                      (icm->Ring->Counters.Tx != tx && rc != TBT_OK &&
                       rc != TBT_E_ERROR_FLAG));
    if (*left && !icm->XdFenced) {
        icm->XdFenced = TRUE;
        icm->XdFenceMs = tbtIcmXdNowMs(icm);
        tbtIcmLog(icm, "cm.net.fence", (ULONG)(-rc));
    }
    return rc;
}

/*
 * The row's teardown of the paths at `addr`: Falcon and Alpine Ridge the
 * NHI mailbox of the link's physical port, data 1 and then 2 (Linux icm.c
 * icm_fr_disconnect_xdomain_paths sends both whatever the first answers);
 * Titan Ridge and Ice Lake command 0x11, stage 1 and then 2, each matched
 * by its whole echo, no stage 2 after a stage 1 that failed
 * (icm_tr_disconnect_xdomain_paths). TBT_OK only when every step was
 * answered without the error flag; anything else leaves the paths
 * unproven gone (Linux takes the flag as a failure too).
 */
static LONG tbtIcmXdTeardownAt(PTBT_ICM icm, const TBT_ICM_XD_ADDR *addr,
                               ULONG slot, ULONG serial)
{
    ULONG req[TBT_ICM_XD_MAX_WORDS];
    ULONG words;
    ULONG stage;
    ULONG code;
    LONG rc;
    LONG rc2;
    BOOLEAN left;

    icm->XdCounters.Teardowns++;
    if (icm->Profile->EncodeXdTeardown == NULL) {
        code = tbtIcmPhyPort(addr->Link) == 0 ? TBT_ICM_MBOX_DISCONNECT_PA
                                              : TBT_ICM_MBOX_DISCONNECT_PB;
        rc = TbtIcmMailbox(icm, code, 1);
        icm->Host->Stall(icm->Host->Context, TBT_ICM_XD_GAP_US);
        rc2 = TbtIcmMailbox(icm, code, 2);
        if (rc == TBT_OK || (rc == TBT_E_ERROR_FLAG && rc2 != TBT_OK)) {
            rc = rc2;
        }
    } else {
        rc = TBT_OK;
        for (stage = 1; stage <= 2 && rc == TBT_OK; stage++) {
            if (stage == 2) {
                icm->Host->Stall(icm->Host->Context, TBT_ICM_XD_GAP_US);
            }
            words = icm->Profile->EncodeXdTeardown(addr, stage, req);
            rc = tbtIcmXdCommand(icm, req, words, icm->CmdTimeoutMs, slot,
                                 serial, &left);
        }
    }
    if (rc == TBT_E_ERROR_FLAG) {
        icm->XdCounters.TeardownsRefused++;
    } else if (rc != TBT_OK) {
        icm->XdCounters.TeardownsFailed++;
    }
    tbtIcmLog(icm, "cm.net.teardown", (addr->Link << 24) | (addr->Route.Lo & 0xFFFFUL) |
                                      (((ULONG)(-rc) & 0xFFUL) << 16));
    return rc;
}

/* A teardown not proven made (no answer, the error flag, not sent) is
 * owed: the next approve sends it first. One identity is kept; a second
 * replaces it, logged. */
static VOID tbtIcmXdOwe(PTBT_ICM icm, const TBT_ICM_XD_ADDR *addr)
{
    if (icm->XdOwed) {
        tbtIcmLog(icm, "cm.net.teardown.replaced", icm->XdOwedAddr.Link);
    }
    icm->XdOwed = TRUE;
    icm->XdOwedAddr = *addr;
    icm->XdCounters.TeardownsOwed++;
    tbtIcmLog(icm, "cm.net.teardown.owe", (addr->Link << 24) | (addr->Route.Lo & 0xFFFFUL));
}

/* The teardown of record `x`'s paths, where the firmware may still hold
 * them: approved or never answered. Also where the firmware announced the
 * link's end (record 35 section 3.17: Linux's thunderbolt-net disables
 * the paths at every end of a logged-in peer, a disconnect included, and
 * an Alpine Ridge firmware left holding them announced no peer at that
 * place again), and that one is never owed. Every teardown is sent
 * outside the record's waits, so an end the firmware announces while it
 * runs cannot cut it short between its two steps. */
static VOID tbtIcmXdTeardown(PTBT_ICM icm, PTBT_ICM_XD x)
{
    BOOLEAN held;
    LONG rc;

    held = (BOOLEAN)(x->Approved || x->ApproveUnknown);
    x->Approved = FALSE;
    x->ApproveUnknown = FALSE;
    if (!held) {
        return;
    }
    rc = TBT_E_STOPPED;
    if (tbtIcmXdAccess(icm)) {
        if (x->FwGone) {
            icm->XdCounters.TeardownsGone++;
            tbtIcmLog(icm, "cm.net.teardown.gone", x->Slot + 1);
        }
        rc = tbtIcmXdTeardownAt(icm, &x->Addr, TBT_ICM_NO_SLOT, 0);
    }
    if (rc != TBT_OK && !x->FwGone) {
        tbtIcmXdOwe(icm, &x->Addr);
    }
}

/* An owed teardown the firmware's own events settle: a disconnect of that
 * link, another host or a device announced at its place, each of which
 * takes its paths with it. `e` NULL: the device of `a`. */
static VOID tbtIcmXdOwedSettle(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e,
                               const TBT_ICM_ANNOUNCE *a)
{
    BOOLEAN hit;

    if (!icm->XdOwed) {
        return;
    }
    if (e == NULL) {
        hit = tbtIcmByRoute(icm) ?
              (BOOLEAN)(a->RouteValid && TbtRouteEqual(&icm->XdOwedAddr.Route, &a->Route)) :
              (BOOLEAN)(a->Depth == 1 &&
                        tbtIcmPhyPort(icm->XdOwedAddr.Link) == tbtIcmPhyPort(a->Link));
    } else if (tbtIcmByRoute(icm)) {
        hit = TbtRouteEqual(&icm->XdOwedAddr.Route, &e->Route);
    } else if (e->Kind == TBT_ICM_XE_DISCONNECTED) {
        hit = TbtUuidEqual(&icm->XdOwedAddr.Remote, &e->Remote);
    } else {
        hit = (BOOLEAN)(tbtIcmPhyPort(icm->XdOwedAddr.Link) == tbtIcmPhyPort(e->Link));
    }
    if (hit && (e == NULL || e->Kind == TBT_ICM_XE_DISCONNECTED ||
                !TbtUuidEqual(&icm->XdOwedAddr.Remote, &e->Remote))) {
        icm->XdOwed = FALSE;
        icm->XdCounters.OwedRetired++;
        tbtIcmLog(icm, "cm.net.teardown.settled", icm->XdOwedAddr.Link);
    }
}

/* ---- the engine's operations (TBT_XDP_OPS; Context is the record) ------ */

static LONG tbtIcmXdSend(PVOID context, ULONG pdf, const ULONG *dw, ULONG words)
{
    PTBT_ICM_XD x = (PTBT_ICM_XD)context;

    if (!tbtIcmXdAccess(x->Icm) || words == 0) {
        return TBT_E_STOPPED;
    }
    return TbtRingSend(x->Icm->Ring, pdf, dw, words);
}

static ULONG tbtIcmXdNow(PVOID context)
{
    return tbtIcmXdNowMs(((PTBT_ICM_XD)context)->Icm);
}

/* The ICM's own inter-domain counters (record 35 section 3.12): beside a
 * record's, at the Login's minute unanswered and at a record's end. */
static VOID tbtIcmXdLogCounters(PTBT_ICM icm)
{
    tbtIcmLog(icm, "cm.icm.xd.packets", icm->XdCounters.Packets);
    tbtIcmLog(icm, "cm.icm.xd.nopeer", icm->XdCounters.NoPeer);
    tbtIcmLog(icm, "cm.icm.xd.overflows", icm->XdCounters.PacketOverflows);
    tbtIcmLog(icm, "cm.icm.xd.nopeer.asks", icm->XdCounters.NoPeerAsks);
    tbtIcmLog(icm, "cm.icm.xd.nopeer.uuids", icm->XdCounters.NoPeerUuid);
    tbtIcmLog(icm, "cm.icm.xd.adopted", icm->XdCounters.Adopted);
    tbtIcmLog(icm, "cm.icm.xd.unannounced", icm->XdCounters.Unannounced);
}

static VOID tbtIcmXdLog(PVOID context, const char *label, ULONG value)
{
    PTBT_ICM_XD x = (PTBT_ICM_XD)context;
    const char *a = label;
    const char *s = "cm.net.n.logins";

    tbtIcmLog(x->Icm, label, ((x->Slot + 1) << 24) | (value & 0x00FFFFFFUL));
    /* After the engine's counters (TbtXdpPeerLogCounters), the ICM's. */
    while (*a != '\0' && *a == *s) {
        a++;
        s++;
    }
    if (*a == '\0' && *s == '\0') {
        tbtIcmXdLogCounters(x->Icm);
    }
}

/*
 * Record 35 section 3.10's service up, once both logins are done: the
 * rings (the driver's RingsUp), the approve 0x10 with the four numbers,
 * then the link up. One service per NHI; none without the service's
 * operations, while an owed teardown cannot be sent, or while an earlier
 * unanswered command fences the approve. The approve is matched by its
 * whole echo, as a device approve is (record 21 section 5.4).
 */
static LONG tbtIcmXdUp(PVOID context, ULONG txHop, ULONG rxHop)
{
    PTBT_ICM_XD x = (PTBT_ICM_XD)context;
    PTBT_ICM icm = x->Icm;
    ULONG req[TBT_ICM_XD_MAX_WORDS];
    ULONG words;
    BOOLEAN e2e;
    BOOLEAN left;
    LONG rc;

    if (!tbtIcmXdAccess(icm)) {
        return TBT_E_STOPPED;
    }
    if (x->FwGone || x->EndPending) {
        return TBT_E_GONE;
    }
    if (x->Adopted) {
        /* Record 35 section 3.16: the firmware answers an approve for a
         * peer it did not announce again and builds nothing, so no rings
         * and no approve until it is announced. */
        tbtIcmLog(icm, "cm.net.unannounced", x->Slot + 1);
        return TBT_E_STATE;
    }
    if (tbtIcmXdOtherUp(icm, x)) {
        tbtIcmLog(icm, "cm.net.busy", x->Slot + 1);
        return TBT_E_STATE;
    }
    if (icm->Net.RingsUp == NULL || icm->Net.RingsDown == NULL) {
        tbtIcmLog(icm, "cm.net.noservice", x->Slot + 1);
        return TBT_E_STATE;
    }
    if (icm->XdFenced) {
        icm->XdCounters.ApprovesFenced++;
        tbtIcmLog(icm, "cm.net.approve.fenced", x->Slot + 1);
        return TBT_E_STATE;
    }
    if (icm->XdOwed) {
        rc = tbtIcmXdTeardownAt(icm, &icm->XdOwedAddr, TBT_ICM_NO_SLOT, 0);
        if (rc != TBT_OK) {
            tbtIcmLog(icm, "cm.net.teardown.owed", icm->XdOwedAddr.Link);
            return TBT_E_STATE;
        }
        icm->XdOwed = FALSE;
        if (x->FwGone || x->EndPending || !x->Used) {
            return TBT_E_GONE;      /* the link ended during that wait */
        }
        if (icm->XdFenced) {
            /* That teardown left an answer to come. */
            icm->XdCounters.ApprovesFenced++;
            tbtIcmLog(icm, "cm.net.approve.fenced", x->Slot + 1);
            return TBT_E_STATE;
        }
    }
    e2e = (BOOLEAN)((icm->NetPrtcstns & x->Peer.RemoteInfo.Prtcstns &
                     TBT_NET_STNS_E2E) != 0);
    x->Ring = tbtIcmXdFalcon(icm) ? TBT_ICM_XD_RING_FR : TBT_ICM_XD_RING;
    x->TxHop = txHop;
    x->RxHop = rxHop;
    if (icm->Net.RingsUp(icm->Net.Context, x->Slot + 1, x->Ring, e2e) != TBT_OK) {
        tbtIcmLog(icm, "cm.net.rings", x->Slot + 1);
        return TBT_E_STATE;
    }
    x->RingsUp = TRUE;

    words = icm->Profile->EncodeXdApprove(&x->Addr, txHop, x->Ring, rxHop,
                                           x->Ring, req);
    icm->XdCounters.Approves++;
    rc = tbtIcmXdCommand(icm, req, words, icm->ApproveTimeoutMs,
                         TBT_ICM_XD_SLOT(x->Slot), x->Serial, &left);
    if (rc == TBT_OK && !left) {
        x->Approved = TRUE;
        icm->XdCounters.ApprovesOk++;
    } else {
        /* An answer after a timed-out attempt does not say which attempt
         * it answered: no service on it (record 35 section 3.10). */
        if (rc == TBT_E_ERROR_FLAG) {
            icm->XdCounters.ApprovesFailed++;
            tbtIcmLog(icm, "cm.net.approve.fail", x->Slot + 1);
        }
        if (left) {
            /* An attempt that may still be answered, or one published and
             * never answered: the firmware builds the paths while it
             * processes the request (as it builds a device's tunnel, ICM
             * 4.3 [obs]), so they may stand. */
            x->ApproveUnknown = TRUE;
            icm->XdCounters.ApprovesUnknown++;
            tbtIcmLog(icm, "cm.net.approve.unknown", x->Slot + 1);
        }
    }
    if (rc != TBT_OK || left || x->FwGone || x->EndPending) {
        tbtIcmXdTeardown(icm, x);
        x->RingsUp = FALSE;
        icm->Net.RingsDown(icm->Net.Context, x->Slot + 1, (BOOLEAN)!tbtIcmXdAccess(icm));
        return (rc != TBT_OK || left) ? TBT_E_STATE : TBT_E_GONE;
    }
    tbtIcmLog(icm, "cm.net.paths", ((x->Slot + 1) << 24) | (txHop << 16) |
                                   (rxHop << 8) | x->Ring);
    x->LinkUp = TRUE;
    if (icm->Net.Link != NULL) {
        icm->Net.Link(icm->Net.Context, x->Slot + 1, TRUE, x->Peer.PeerMac);
    }
    tbtIcmXdOffer(icm);         /* withdrawn from every other peer */
    return TBT_OK;
}

/* The service down: the link down where tbtIcmXdSync has not lowered it
 * yet, the teardown where the firmware may hold the paths, the rings
 * down. `peerGone` is the engine's; the teardown follows what the
 * firmware said, not it. */
static VOID tbtIcmXdDown(PVOID context, BOOLEAN peerGone)
{
    PTBT_ICM_XD x = (PTBT_ICM_XD)context;
    PTBT_ICM icm = x->Icm;

    tbtIcmLog(icm, "cm.net.down", ((x->Slot + 1) << 8) | (peerGone ? 1UL : 0UL) |
                                  (x->FwGone ? 2UL : 0UL));
    if (x->LinkUp) {
        x->LinkUp = FALSE;
        if (icm->Net.Link != NULL) {
            icm->Net.Link(icm->Net.Context, x->Slot + 1, FALSE, NULL);
        }
    }
    tbtIcmXdTeardown(icm, x);
    if (x->RingsUp) {
        x->RingsUp = FALSE;
        if (icm->Net.RingsDown != NULL) {
            /* Lost too where the access closed during the teardown. */
            icm->Net.RingsDown(icm->Net.Context, x->Slot + 1,
                               (BOOLEAN)!tbtIcmXdAccess(icm));
        }
    }
    tbtIcmXdOffer(icm);         /* offered again to the others */
}

static VOID tbtIcmXdReady(PVOID context, const TBT_XDP_REMOTE *remote)
{
    PTBT_ICM_XD x = (PTBT_ICM_XD)context;

    tbtIcmLog(x->Icm, "cm.xdp.peer", ((x->Slot + 1) << 24) |
                                     ((remote->VendorId & 0xFFFUL) << 12) |
                                     (remote->DeviceId & 0xFFFUL));
    if (x->Adopted) {
        /* Record 35 section 3.16: found, and not connected - the
         * firmware never announced it again. */
        x->Icm->XdCounters.Unannounced++;
        tbtIcmLog(x->Icm, "cm.xdp.adopt.unannounced",
                  ((x->Slot + 1) << 24) | (x->Addr.Route.Lo & 0xFFFFUL));
    }
}

/* ---- the records ------------------------------------------------------- */

/* A direct link (record 35 section 13): a local route of one level; on
 * Falcon and Alpine Ridge also depth 0, which names the host router as
 * the router the far host hangs off (Linux icm_fr_xdomain_connected finds
 * that router by the event's link and depth). */
static BOOLEAN tbtIcmXdDirect(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e)
{
    if ((e->Route.Hi & TBT_ROUTE_HI_TOPOLOGY) != 0 || (e->Route.Lo & ~0xFFUL) != 0 ||
        (e->Route.Lo & 0xFFUL) == 0) {
        return FALSE;
    }
    return (BOOLEAN)(tbtIcmByRoute(icm) || e->Depth == 0);
}

/* The same place as Linux keys it: Falcon and Alpine Ridge the depth and
 * the physical port; Titan Ridge and Ice Lake the route. */
static BOOLEAN tbtIcmXdSamePlace(PTBT_ICM icm, const TBT_ICM_XD *x,
                                 const TBT_ICM_XD_EVENT *e)
{
    if (tbtIcmByRoute(icm)) {
        return TbtRouteEqual(&x->Addr.Route, &e->Route);
    }
    return (BOOLEAN)(x->Addr.Depth == e->Depth &&
                     tbtIcmPhyPort(x->Addr.Link) == tbtIcmPhyPort(e->Link));
}

/* The record a disconnected event names: Falcon and Alpine Ridge by its
 * remote UUID (Linux icm_fr_xdomain_disconnected), Titan Ridge and Ice
 * Lake by its route (icm_tr_xdomain_disconnected). */
static BOOLEAN tbtIcmXdNamed(PTBT_ICM icm, const TBT_ICM_XD_ADDR *a,
                             const TBT_ICM_XD_EVENT *e)
{
    return tbtIcmByRoute(icm) ? TbtRouteEqual(&a->Route, &e->Route)
                              : TbtUuidEqual(&a->Remote, &e->Remote);
}

/* The firmware said the link at `x` ended: its paths are not torn down,
 * a wait on its approve or teardown ends, and the record ends at the next
 * safe point (tbtIcmXdRun) - a flag no queue can lose. */
static VOID tbtIcmXdMarkGone(PTBT_ICM icm, PTBT_ICM_XD x)
{
    x->FwGone = TRUE;
    x->EndPending = TRUE;
    if (icm->WaitSerial != 0 && icm->WaitSlot == TBT_ICM_XD_SLOT(x->Slot) &&
        icm->WaitSerial == x->Serial) {
        icm->WaitGone = TRUE;
    }
}

/* Whether the records may be changed now: not inside a command wait or a
 * driver's wait, and no engine running. */
static BOOLEAN tbtIcmXdNow2(const TBT_ICM *icm)
{
    return (BOOLEAN)(!icm->InWait && !icm->XdTaking);
}

/* The records whose end is pending, ended (peer gone: no Logout; the
 * firmware said the link went: no teardown). */
static VOID tbtIcmXdEndPending(PTBT_ICM icm)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (icm->Xd[i].Used && icm->Xd[i].EndPending) {
            /* The firmware said the link went, except where the end was
             * asked for as an orderly one (record 35 section 3.15). */
            tbtIcmXdEnd(icm, &icm->Xd[i], (BOOLEAN)!icm->Xd[i].EndGrace);
        }
    }
}

/*
 * A record for a direct link's connected event (record 35 sections 3.4
 * and 3.10): the engine given the event's local UUID as this domain's and
 * its local route as the peer's, the block of section 3.7 (maxhopid 15:
 * the firmware's lane adapter is not read), a Max Output HopID of 15 and
 * no HopID known in use, and started at PROPS. `adopted`: made from a
 * request with no announcement (sections 3.15 and 3.16), its block
 * without the service.
 */
static VOID tbtIcmXdBegin(PTBT_ICM icm, PTBT_ICM_XD x, ULONG slot,
                          const TBT_ICM_XD_EVENT *e, BOOLEAN adopted)
{
    TBT_XDP_OPS ops;
    TBT_XDP_LOCAL props;

    tbtIcmXdIdentity(icm);
    tbtIcmZero(x, sizeof(*x));
    x->Icm = icm;
    x->Slot = slot;
    icm->XdNextSerial++;
    if (icm->XdNextSerial == 0) {
        icm->XdNextSerial = 1;
    }
    x->Serial = icm->XdNextSerial;
    x->Addr.Link = e->Link;
    x->Addr.Depth = e->Depth;
    x->Addr.Route = e->Route;
    x->Addr.Remote = e->Remote;
    x->RemoteRoute = e->RemoteRoute;
    x->Local = e->Local;
    x->Adopted = adopted;

    tbtIcmZero(&ops, sizeof(ops));
    ops.Context = x;
    ops.Send = tbtIcmXdSend;
    ops.NowMs = tbtIcmXdNow;
    ops.Log = tbtIcmXdLog;
    ops.ServiceUp = tbtIcmXdUp;
    ops.ServiceDown = tbtIcmXdDown;
    ops.PeerReady = tbtIcmXdReady;
    tbtIcmZero(&props, sizeof(props));
    props.VendorId = 0;
    props.VendorText = "xHCI98";
    props.DeviceId = 1;
    props.DeviceText = "xhcicm";
    props.DeviceRevision = icm->NetRevision;
    props.MaxHopId = TBT_ICM_XD_MAXHOP;
    props.Network = tbtIcmXdOffers(icm, x, adopted);
    props.Prtcstns = icm->NetPrtcstns;
    TbtXdpPeerInit(&x->Peer, &ops, &x->Addr.Route, &e->Local, &e->Remote, TRUE,
                   FALSE, &props, icm->NetMac, TBT_ICM_XD_MAXHOP, NULL);
    x->Used = TRUE;
    TbtXdpPeerStart(&x->Peer);
    tbtIcmLog(icm, "cm.xdp.begin", ((slot + 1) << 24) | (e->Link << 16) |
                                   (e->Route.Lo & 0xFFFFUL));
}

/* A device in the table where a connected event puts a host (Linux
 * icm_fr_xdomain_connected and icm_tr_xdomain_connected remove the switch
 * at the route first): Titan Ridge and Ice Lake at the same route; Falcon
 * and Alpine Ridge at depth 1 on the same physical port. It leaves with its
 * subtree, each with its TUNNEL_DOWN. */
static VOID tbtIcmXdDropDevice(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e)
{
    ULONG i;
    PTBT_ICM_DEVICE d;

    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        d = &icm->Devices[i];
        if (!d->InUse) {
            continue;
        }
        if (tbtIcmByRoute(icm) ? (d->RouteValid && TbtRouteEqual(&d->Route, &e->Route))
                               : (d->Depth == 1 &&
                                  tbtIcmPhyPort(d->Link) == tbtIcmPhyPort(e->Link))) {
            icm->XdCounters.DevicesReplaced++;
            tbtIcmLog(icm, "cm.xdp.device-replaced", i);
            tbtIcmReplace(icm, i);
            return;
        }
    }
}

/* Record 35 section 3.15: whether two direct links are one place as
 * section 3.10 keys them - Falcon and Alpine Ridge the depth and the
 * physical port (a lane and its dual), Titan Ridge and Ice Lake the
 * route. */
static BOOLEAN tbtIcmXdOnePlace(PTBT_ICM icm, ULONG linkA, ULONG depthA,
                                const TBT_ROUTE *routeA, ULONG linkB,
                                ULONG depthB, const TBT_ROUTE *routeB)
{
    if (tbtIcmByRoute(icm)) {
        return TbtRouteEqual(routeA, routeB);
    }
    return (BOOLEAN)(depthA == depthB && tbtIcmPhyPort(linkA) == tbtIcmPhyPort(linkB));
}

/* Record 35 section 3.15: the place of a direct link's connected event,
 * one entry a place (one per record slot, a new place replacing them in
 * turn), its adoption budget renewed, for a record adopted there later. */
static VOID tbtIcmXdKeep(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e)
{
    ULONG i;
    ULONG pick = TBT_ICM_MAX_XD;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (icm->XdKnownValid[i] &&
            tbtIcmXdOnePlace(icm, icm->XdKnown[i].Link, icm->XdKnown[i].Depth,
                             &icm->XdKnown[i].Route, e->Link, e->Depth, &e->Route)) {
            pick = i;
            break;
        }
    }
    for (i = 0; pick == TBT_ICM_MAX_XD && i < TBT_ICM_MAX_XD; i++) {
        if (!icm->XdKnownValid[i]) {
            pick = i;
        }
    }
    if (pick == TBT_ICM_MAX_XD) {
        pick = icm->XdKnownNext % TBT_ICM_MAX_XD;
        icm->XdKnownNext = (pick + 1) % TBT_ICM_MAX_XD;
    }
    icm->XdKnown[pick] = *e;
    icm->XdKnownValid[pick] = TRUE;
    icm->XdKnownAdopts[pick] = 0;
}

/*
 * Record 35 section 3.10's keying of a connected event (Linux icm.c
 * icm_fr_xdomain_connected, icm_tr_xdomain_connected), in its turn among
 * the device announcements. A link that is not direct gets no record.
 */
static VOID tbtIcmXdApply(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e)
{
    ULONG i;
    PTBT_ICM_XD x;

    /* This domain's UUID as the firmware names it, kept for the answers
     * a request with no record gets (record 35 section 3.14). */
    icm->XdLocal = e->Local;
    icm->XdLocalValid = TRUE;
    tbtIcmXdEndPending(icm);
    if (!tbtIcmXdDirect(icm, e)) {
        icm->XdCounters.Deep++;
        tbtIcmLog(icm, "cm.xdp.deep", (e->Depth << 24) | (e->Route.Lo & 0xFFFFFFUL));
        return;
    }
    tbtIcmXdKeep(icm, e);
    tbtIcmXdOwedSettle(icm, e, NULL);
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        x = &icm->Xd[i];
        if (!x->Used || !TbtUuidEqual(&x->Addr.Remote, &e->Remote)) {
            continue;
        }
        if (tbtIcmXdSamePlace(icm, x, e)) {
            /* Announced again (a driver ready): kept, its route and link
             * as the event gives them. */
            icm->XdCounters.Again++;
            x->Stale = FALSE;
            x->Addr.Link = e->Link;
            x->Addr.Route = e->Route;
            x->RemoteRoute = e->RemoteRoute;
            x->Peer.Route = e->Route;
            tbtIcmLog(icm, "cm.xdp.again", (i + 1) << 24);
            if (x->Adopted) {
                /* An adopted record announced: an ordinary one from now,
                 * its block rebuilt with the service and the peer told
                 * (record 35 section 3.16). */
                x->Adopted = FALSE;
                tbtIcmXdOffer(icm);
            }
            return;
        }
        icm->XdCounters.Replaced++;
        x->FwGone = TRUE;
        tbtIcmXdEnd(icm, x, TRUE);
    }
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        x = &icm->Xd[i];
        if (x->Used &&
            (tbtIcmByRoute(icm) ? TbtRouteEqual(&x->Addr.Route, &e->Route)
                                : (x->Addr.Depth == e->Depth &&
                                   (x->Addr.Link == e->Link ||
                                    x->Addr.Link == tbtIcmDualLink(e->Link))))) {
            icm->XdCounters.Replaced++;
            x->FwGone = TRUE;
            tbtIcmXdEnd(icm, x, TRUE);
        }
    }
    tbtIcmXdDropDevice(icm, e);
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (!icm->Xd[i].Used) {
            tbtIcmXdBegin(icm, &icm->Xd[i], i, e, FALSE);
            return;
        }
    }
    icm->XdCounters.Full++;
    tbtIcmLog(icm, "cm.xdp.full", e->Link);
}

/* A device announced where a record's host was (Linux icm_fr_device_
 * connected and __icm_tr_device_connected remove the XDomain there):
 * Titan Ridge and Ice Lake the same route, Falcon and Alpine Ridge depth 1
 * on the same physical port. That record ends, the link gone. Called as
 * the announcement is acted on, in its turn. */
static VOID tbtIcmXdDeviceAt(PTBT_ICM icm, const TBT_ICM_ANNOUNCE *a)
{
    ULONG i;
    PTBT_ICM_XD x;
    BOOLEAN hit;

    if (!icm->NetOn || a->Rejected) {
        return;
    }
    tbtIcmXdOwedSettle(icm, NULL, a);
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        /* A device is where that host was: no record is adopted there
         * (record 35 section 3.15). */
        if (icm->XdKnownValid[i] &&
            (tbtIcmByRoute(icm)
                 ? (a->RouteValid && TbtRouteEqual(&icm->XdKnown[i].Route, &a->Route))
                 : (a->Depth == 1 &&
                    tbtIcmPhyPort(icm->XdKnown[i].Link) == tbtIcmPhyPort(a->Link)))) {
            icm->XdKnownValid[i] = FALSE;
        }
    }
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        x = &icm->Xd[i];
        if (!x->Used) {
            continue;
        }
        if (tbtIcmByRoute(icm)) {
            hit = (BOOLEAN)(a->RouteValid && TbtRouteEqual(&x->Addr.Route, &a->Route));
        } else {
            hit = (BOOLEAN)(a->Depth == 1 &&
                            tbtIcmPhyPort(x->Addr.Link) == tbtIcmPhyPort(a->Link));
        }
        if (hit) {
            icm->XdCounters.Replaced++;
            tbtIcmXdMarkGone(icm, x);
        }
    }
    if (tbtIcmXdNow2(icm)) {
        tbtIcmXdEndPending(icm);
    }
}

/* A disconnect, where it is decoded: the record it names marked ended (a
 * flag no queue can lose; a wait on its approve or teardown ends), an owed
 * teardown of that link settled. The event itself takes its turn among
 * the announcements (tbtIcmXdGoneApply), so a connected event queued
 * before it is acted on first, with everything that one replaces. */
static VOID tbtIcmXdGone(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (icm->Xd[i].Used && tbtIcmXdNamed(icm, &icm->Xd[i].Addr, e)) {
            tbtIcmXdMarkGone(icm, &icm->Xd[i]);
        }
    }
    tbtIcmXdOwedSettle(icm, e, NULL);
}

/* A disconnect in its turn: the record it names ends, the link gone. */
static VOID tbtIcmXdGoneApply(PTBT_ICM icm, const TBT_ICM_XD_EVENT *e)
{
    tbtIcmXdGone(icm, e);
    tbtIcmXdEndPending(icm);
}

/*
 * An inter-domain event as it lands. XhciCmNet 0: logged and ignored, as
 * before task 45.59. Else decoded by the row: a connected event acted on
 * at once where the records may change, else queued in order with the
 * device announcements; a disconnect handled by tbtIcmXdGone.
 */
static VOID tbtIcmXdEvent(PTBT_ICM icm, const TBT_FRAME *f, ULONG code)
{
    TBT_ICM_XD_EVENT e;
    TBT_ICM_ANNOUNCE a;
    BOOLEAN ok;

    if (!icm->NetOn) {
        icm->Counters.EventsIgnored++;
        tbtIcmLog(icm, "tbt.icm.event-ignored",
                  (code << 24) | (f->Words > 1 ? (f->Dw[1] & 0xFFFFFFUL) : 0));
        return;
    }
    if (TBT_ICM_HDR_INDEX(f->Dw[0]) != 0) {
        icm->Counters.EventsIgnored++;
        return;
    }
    if (code == TBT_ICM_EV_XD_CONNECTED) {
        ok = icm->Profile->DecodeXdConnected(f->Dw, f->Words, &e);
    } else {
        ok = icm->Profile->DecodeXdDisconnected(f->Dw, f->Words, &e);
    }
    if (!ok) {
        icm->XdCounters.Malformed++;
        icm->Counters.EventsMalformed++;
        tbtIcmLog(icm, "tbt.icm.xd-malformed", (code << 24) | f->Words);
        tbtIcmLogRaw(icm, f);
        return;
    }
    if (code == TBT_ICM_EV_XD_DISCONNECTED) {
        icm->XdCounters.Disconnected++;
        tbtIcmLog(icm, "cm.xdp.disconnected", (e.Link << 24) | (e.Route.Lo & 0xFFFFUL));
        tbtIcmXdGone(icm, &e);
        if (tbtIcmXdNow2(icm) && icm->QueueCount == 0) {
            tbtIcmXdEndPending(icm);
            return;
        }
        tbtIcmZero(&a, sizeof(a));
        a.IsXd = TRUE;
        a.Xd = e;
        tbtIcmEnqueue(icm, &a);
        return;
    }
    icm->XdCounters.Connected++;
    if (e.Route.Hi == 0 && e.Route.Lo < TBT_ICM_XD_NOPEER_ROUTES) {
        /* That route's host is announced: its budget again. Another
         * route's (every driver ready repeats them) is its own. */
        icm->XdNoPeerAskRun[e.Route.Lo] = 0;
    }
    tbtIcmLog(icm, "cm.xdp.connected", (e.Link << 24) | (e.Depth << 16) |
                                       (e.Route.Lo & 0xFFFFUL));
    if (tbtIcmXdNow2(icm) && icm->QueueCount == 0) {
        tbtIcmXdApply(icm, &e);     /* nothing older queued */
        return;
    }
    tbtIcmZero(&a, sizeof(a));
    a.IsXd = TRUE;
    a.Xd = e;
    tbtIcmEnqueue(icm, &a);
}

/* The packets held for a record's route, dropped as the record ends: they
 * are its peer's, and a record started later at that route is another
 * host's. */
static VOID tbtIcmXdPurge(PTBT_ICM icm, const TBT_ROUTE *route)
{
    ULONG i;
    ULONG n;
    ULONG keep;
    TBT_FRAME *f;
    TBT_ROUTE r;

    n = icm->XdCount;
    keep = 0;
    for (i = 0; i < n; i++) {
        f = &icm->XdQ[(icm->XdHead + i) % TBT_ICM_XD_QUEUE];
        if (f->Words >= 2) {
            r.Hi = f->Dw[0] & ~TBT_ROUTE_HI_CM;
            r.Lo = f->Dw[1];
            if (TbtRouteEqual(&r, route)) {
                continue;
            }
        }
        if (keep != i) {
            icm->XdQ[(icm->XdHead + keep) % TBT_ICM_XD_QUEUE] = *f;
        }
        keep++;
    }
    icm->XdCount = keep;
}

/*
 * A record ends (record 35 section 3.8): with `gone` FALSE and the host
 * interface there, one Logout sent where the service is up and not waited
 * for (IDS 5.1.2 takes no answer as one); then the engine ended, which
 * takes the service down (tbtIcmXdDown: the teardown where the firmware
 * may hold the paths), and the record freed.
 */
static VOID tbtIcmXdEnd(PTBT_ICM icm, PTBT_ICM_XD x, BOOLEAN gone)
{
    ULONG slot;

    if (!x->Used) {
        return;
    }
    if (!tbtIcmXdAccess(icm)) {
        gone = TRUE;
    }
    if (!gone) {
        TbtXdpPeerEnd(&x->Peer, FALSE);
        TbtXdpPeerTick(&x->Peer);
        tbtIcmXdSync(icm, x);
    }
    TbtXdpPeerEnd(&x->Peer, TRUE);
    if (x->LinkUp || x->Approved || x->ApproveUnknown || x->RingsUp) {
        /* What the engine no longer counts as its paths. */
        tbtIcmXdDown(x, gone);
    }
    tbtIcmXdPurge(icm, &x->Addr.Route);
    /* Record 35 section 3.12: what the record took and dropped. */
    TbtXdpPeerLogCounters(&x->Peer);
    slot = x->Slot;
    tbtIcmLog(icm, "cm.xdp.end", ((slot + 1) << 8) | (gone ? 1UL : 0UL) |
                                 (x->FwGone ? 2UL : 0UL));
    tbtIcmZero(x, sizeof(*x));
    tbtIcmXdOffer(icm);
}

/* Every record ended; `quiet`: the host interface is lost, nothing is
 * sent, and an approved link's teardown is owed. Asked while the engines
 * run, it is done when they return. The packets held go with them. */
static VOID tbtIcmXdEndAll(PTBT_ICM icm, BOOLEAN gone, BOOLEAN quiet)
{
    ULONG i;
    BOOLEAN saved;

    /* A session boundary: the places the firmware announced are
     * forgotten with the records (record 35 section 3.15). */
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        icm->XdKnownValid[i] = FALSE;
    }
    icm->XdAdoptPending = FALSE;
    if (icm->XdTaking) {
        icm->XdEndOwed = TRUE;
        icm->XdEndOwedGone = (BOOLEAN)(icm->XdEndOwedGone || gone || quiet);
        return;
    }
    saved = icm->XdQuiet;
    if (quiet) {
        icm->XdQuiet = TRUE;
    }
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        tbtIcmXdEnd(icm, &icm->Xd[i], gone);
    }
    icm->XdQuiet = saved;
    icm->XdCount = 0;
}

/* A rediscovery's driver ready (record 35 section 3.10, as Linux icm.c
 * marks XDomains unplugged and frees those not announced again): every
 * record marked, an announcement clearing it; then those still marked
 * after the reconciliation's window end, the link gone. */
static VOID tbtIcmXdMarkStale(PTBT_ICM icm, BOOLEAN on)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        /* An adopted record too (record 35 section 3.16: it carries no
         * service, so the reconciliation ending it costs nothing). */
        icm->Xd[i].Stale = (BOOLEAN)(on && icm->Xd[i].Used);
    }
}

static VOID tbtIcmXdReconcile(PTBT_ICM icm)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (icm->Xd[i].Used && icm->Xd[i].Stale) {
            icm->XdCounters.Reconciled++;
            tbtIcmXdMarkGone(icm, &icm->Xd[i]);
        }
    }
    if (tbtIcmXdNow2(icm)) {
        tbtIcmXdEndPending(icm);
    }
}

/*
 * The XDomain sink (tbt_ring.h TBT_XDOMAIN_SINK): called inside the ring's
 * receive, whoever reads it, so it only records. XhciCmNet 0: counted and
 * dropped unanswered (record 35 section 3.6), the first logged.
 */
static VOID tbtIcmXdSink(PVOID context, const TBT_FRAME *frame)
{
    PTBT_ICM icm = (PTBT_ICM)context;

    if (!icm->NetOn) {
        if (icm->XdCounters.PacketsIgnored++ == 0) {
            tbtIcmLog(icm, "cm.xdp.ignored", frame->Pdf);
        }
        return;
    }
    if (icm->XdCount >= TBT_ICM_XD_QUEUE) {
        icm->XdCounters.PacketOverflows++;
        return;
    }
    icm->XdQ[(icm->XdHead + icm->XdCount) % TBT_ICM_XD_QUEUE] = *frame;
    icm->XdCount++;
    icm->XdCounters.Packets++;
}

/*
 * Record 35 section 3.15 (the owner's bench, round 3): whether a packet
 * with no record is a Properties Read Request a record may be made from -
 * a discovery request of type 3 from a direct link where the firmware
 * announced a host since the bind, for this domain as that event named it
 * (its Destination UUID the event's local_uuid), from a domain that names
 * itself; a slot free; no record of that remote UUID or at that place;
 * no announcement queued. `*known` is the kept place's entry.
 */
static BOOLEAN tbtIcmXdAdoptable(PTBT_ICM icm, const TBT_FRAME *f, ULONG kind,
                                 const TBT_XDP_PKT *k, ULONG *known)
{
    ULONG i;
    ULONG hit = TBT_ICM_MAX_XD;
    BOOLEAN room = FALSE;
    const TBT_ICM_XD_EVENT *e;
    PTBT_ICM_XD x;

    if (!icm->NetOn || !icm->Bound || !tbtIcmXdAccess(icm) || icm->QueueCount != 0 ||
        f->Pdf != TBT_PDF_XDOMAIN_REQ || f->Words < 2 ||
        kind != TBT_XDP_K_DISCOVERY || k->Response || k->Type != TBT_XDP_PROPS_REQ ||
        k->Route.Hi != 0 || (k->Route.Lo & ~0xFFUL) != 0 || k->Route.Lo == 0) {
        return FALSE;
    }
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (icm->XdKnownValid[i] && TbtRouteEqual(&icm->XdKnown[i].Route, &k->Route)) {
            hit = i;
            break;
        }
    }
    if (hit == TBT_ICM_MAX_XD || !TbtUuidEqual(&k->Dst, &icm->XdKnown[hit].Local) ||
        !TbtUuidEqual(&k->Src, &icm->XdKnown[hit].Remote) ||
        icm->XdKnownAdopts[hit] >= TBT_ICM_XD_ADOPT_MAX) {
        return FALSE;
    }
    e = &icm->XdKnown[hit];
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        x = &icm->Xd[i];
        if (!x->Used) {
            room = TRUE;
            continue;
        }
        if (TbtUuidEqual(&x->Addr.Remote, &k->Src) ||
            tbtIcmXdOnePlace(icm, x->Addr.Link, x->Addr.Depth, &x->Addr.Route,
                             e->Link, e->Depth, &e->Route)) {
            return FALSE;
        }
    }
    if (!room) {
        return FALSE;
    }
    *known = hit;
    return TRUE;
}

/* Record 35 section 3.15: whether a packet is a Changed Notification
 * naming a domain other than `route`'s record holds - the one packet
 * that moves an engine's peer identity (tbt_xdp.c, IDS 3.7). */
static BOOLEAN tbtIcmXdOtherDomain(const TBT_FRAME *f, const TBT_UUID *remote)
{
    TBT_XDP_PKT k;

    if (TbtXdpDecode(f->Pdf, f->Dw, f->Words, &k) != TBT_XDP_K_DISCOVERY ||
        k.Response || k.Type != TBT_XDP_CHANGED) {
        return FALSE;
    }
    return (BOOLEAN)!TbtUuidEqual(&k.Src, remote);
}

/*
 * Record 35 section 3.15: the request kept by tbtIcmXdNoPeer, after the
 * packet loop, where records may change - checked again (the loop may
 * have changed what it was checked against), a record made at the kept
 * place with the request's Source UUID as the remote UUID, never marked
 * stale, then the request handed to its engine, which answers it.
 */
static VOID tbtIcmXdAdopt(PTBT_ICM icm)
{
    TBT_FRAME f;
    TBT_XDP_PKT k;
    TBT_ICM_XD_EVENT e;
    PTBT_ICM_XD x;
    ULONG kind;
    ULONG known;
    ULONG i;

    /* An adopted record whose discovery failed (the engine at ERROR or
     * LOOP, no service) ends here, so the next request may adopt again
     * within the place's budget. */
    for (i = 0; i < TBT_ICM_MAX_XD && tbtIcmXdNow2(icm); i++) {
        x = &icm->Xd[i];
        if (x->Used && x->Adopted && !x->EndPending &&
            (x->Peer.State == TBT_XDP_S_ERROR || x->Peer.State == TBT_XDP_S_LOOP) &&
            x->Peer.NetState != TBT_NET_S_UP) {
            tbtIcmLog(icm, "cm.xdp.adopt.end", (i + 1) << 24);
            tbtIcmXdEnd(icm, x, FALSE);
        }
    }
    if (!icm->XdAdoptPending) {
        return;
    }
    icm->XdAdoptPending = FALSE;
    f = icm->XdAdopt;
    if (!tbtIcmXdNow2(icm)) {
        return;
    }
    kind = TbtXdpDecode(f.Pdf, f.Dw, f.Words, &k);
    if (!tbtIcmXdAdoptable(icm, &f, kind, &k, &known)) {
        return;
    }
    e = icm->XdKnown[known];
    e.Kind = TBT_ICM_XE_CONNECTED;
    for (i = 0; i < TBT_ICM_MAX_XD; i++) {
        if (!icm->Xd[i].Used) {
            break;
        }
    }
    if (i == TBT_ICM_MAX_XD) {
        return;
    }
    x = &icm->Xd[i];
    tbtIcmXdBegin(icm, x, i, &e, TRUE);
    icm->XdKnownAdopts[known]++;
    icm->XdCounters.Adopted++;
    tbtIcmLog(icm, "cm.xdp.adopt", ((i + 1) << 24) | (e.Link << 16) |
                                   (e.Route.Lo & 0xFFFFUL));
    icm->XdTaking = TRUE;
    (VOID)TbtXdpPeerOnPacket(&x->Peer, f.Pdf, f.Dw, f.Words);
    tbtIcmXdSync(icm, x);
    icm->XdTaking = FALSE;
    tbtIcmXdEndPending(icm);
    if (icm->XdEndOwed) {
        icm->XdEndOwed = FALSE;
        tbtIcmXdEndAll(icm, icm->XdEndOwedGone, icm->XdEndOwedGone);
        icm->XdEndOwedGone = FALSE;
    }
}

/* A packet for no record: a discovery request answered Not Ready (record
 * 35 section 3.1: no UUID of this domain's for that link yet), anything
 * else dropped. */
static VOID tbtIcmXdNoPeer(PTBT_ICM icm, const TBT_FRAME *f)
{
    TBT_XDP_PKT k;
    ULONG buf[TBT_XDP_MAX_WORDS];
    ULONG words;
    ULONG kind;
    ULONG known;

    icm->XdCounters.NoPeer++;
    if (icm->XdCounters.NoPeer <= TBT_XDP_DROPS_LOGGED) {
        /* Record 35 section 3.12: a packet for no record, its route. */
        tbtIcmLog(icm, "cm.icm.xd.nopeer.route",
                  ((f->Pdf & 0xFUL) << 24) | (f->Words >= 2 ? (f->Dw[1] & 0x00FFFFFFUL) : 0UL));
    }
    kind = TbtXdpDecode(f->Pdf, f->Dw, f->Words, &k);
    if (icm->XdCounters.NoPeer <= TBT_XDP_DROPS_LOGGED) {
        /* Record 35 section 3.14: what it is - the decoder's kind 19:16,
         * the type 15:8 (a discovery or USB4NET packet type), its SN. */
        tbtIcmLog(icm, "cm.icm.xd.nopeer.type",
                  ((kind & 0xFUL) << 16) | ((k.Type & 0xFFUL) << 8) | (k.Sn & 0xFFUL));
    }
    if (tbtIcmXdAdoptable(icm, f, kind, &k, &known)) {
        /* Record 35 section 3.15: a record is made from it after the
         * packet loop (tbtIcmXdAdopt); no Not Ready, no driver ready. */
        if (!icm->XdAdoptPending) {
            icm->XdAdoptPending = TRUE;
            icm->XdAdoptKnown = known;
            icm->XdAdopt = *f;
        }
        return;
    }
    /* Record 35 section 3.13 (the owner's bench, 2026-11-09): a request
     * from a host on a direct link (one route level) that has no record
     * means the firmware has not announced it - its own UUID Request at
     * the link's return went unanswered and it does not ask again - so
     * another driver ready makes it ask and announce, at most once each
     * TBT_ICM_XD_NOPEER_ASK_MS. */
    if (f->Pdf == TBT_PDF_XDOMAIN_REQ && f->Words >= 2 && icm->Bound &&
        (f->Dw[0] & ~TBT_ROUTE_HI_CM) == 0 && f->Dw[1] != 0 &&
        f->Dw[1] < TBT_ICM_XD_NOPEER_ROUTES &&
        icm->XdNoPeerAskRun[f->Dw[1]] < TBT_ICM_XD_NOPEER_ASK_MAX &&
        (!icm->XdNoPeerAsked ||
         (ULONG)(tbtIcmXdNowMs(icm) - icm->XdNoPeerAskMs) >= TBT_ICM_XD_NOPEER_ASK_MS)) {
        icm->XdNoPeerAsked = TRUE;
        icm->XdNoPeerAskRun[f->Dw[1]]++;
        icm->XdNoPeerAskMs = tbtIcmXdNowMs(icm);
        icm->XdCounters.NoPeerAsks++;
        icm->RediscoverPending = TRUE;
        tbtIcmLog(icm, "cm.icm.xd.nopeer.ask", f->Dw[1] & 0xFFUL);
    }
    if (kind != TBT_XDP_K_DISCOVERY || k.Response || !tbtIcmXdAccess(icm)) {
        return;
    }
    if ((k.Type == TBT_XDP_UUID_REQ || k.Type == TBT_XDP_UUID_REQ_TBT3) &&
        icm->XdLocalValid) {
        /* Record 35 section 3.14 (the owner's bench, 2026-11-09, plug
         * order 3): after the far end's replug its UUID Requests reach
         * this ring, unanswered by the firmware. Answered here with this
         * domain's UUID as the firmware's last connected event gave it,
         * as Linux answers a UUID Request at the domain's level
         * (xdomain.c tb_xdp_handle_request), so the far end's discovery
         * goes on and it can answer the firmware's own request. */
        words = TbtXdpBuildUuidResponse(buf, &k.Route, k.Sn, &icm->XdLocal,
                                        (BOOLEAN)(k.Type == TBT_XDP_UUID_REQ));
        if (words != 0 &&
            TbtRingSend(icm->Ring, TBT_PDF_XDOMAIN_RESP, buf, words) == TBT_OK) {
            if (icm->XdCounters.NoPeerUuid++ == 0) {
                tbtIcmLog(icm, "cm.icm.xd.nopeer.uuid", k.Route.Lo & 0x00FFFFFFUL);
            }
        }
        return;
    }
    words = TbtXdpBuildError(buf, &k.Route, k.Sn, TBT_XDP_ERR_NOT_READY);
    if (words != 0 && TbtRingSend(icm->Ring, TBT_PDF_XDOMAIN_RESP, buf, words) == TBT_OK) {
        icm->XdCounters.NotReady++;
    }
}

/*
 * The records' share of the thread, after the read in progress: the ends
 * pending, then the packets queued when the pass began (no more: one
 * answered may make requests whose receive queues more, tbt_u4cm.c's
 * tbtU4XdTake), each to the record at its route, then (`tick`, the idle
 * poll) each engine's resends and timeouts. Never inside a wait.
 */
static VOID tbtIcmXdRun(PTBT_ICM icm, BOOLEAN tick)
{
    TBT_FRAME f;
    TBT_ROUTE r;
    PTBT_ICM_XD x;
    ULONG i;
    ULONG left;

    if (!icm->NetOn || !icm->Bound || !tbtIcmXdNow2(icm)) {
        return;
    }
    tbtIcmXdEndPending(icm);
    icm->XdTaking = TRUE;
    left = icm->XdCount;
    while (left != 0 && icm->XdCount != 0 && !icm->XdEndOwed) {
        left--;
        f = icm->XdQ[icm->XdHead];
        icm->XdHead = (icm->XdHead + 1) % TBT_ICM_XD_QUEUE;
        icm->XdCount--;
        if (f.Words < 2) {
            continue;
        }
        r.Hi = f.Dw[0] & ~TBT_ROUTE_HI_CM;
        r.Lo = f.Dw[1];
        x = NULL;
        for (i = 0; i < TBT_ICM_MAX_XD; i++) {
            if (icm->Xd[i].Used && !icm->Xd[i].EndPending &&
                TbtRouteEqual(&icm->Xd[i].Addr.Route, &r)) {
                x = &icm->Xd[i];
                break;
            }
        }
        if (x == NULL) {
            tbtIcmXdNoPeer(icm, &f);
        } else if (x->Adopted && tbtIcmXdOtherDomain(&f, &x->Addr.Remote)) {
            /* Record 35 section 3.15: an adopted record holds the
             * computer the firmware announced at its place - the
             * identity a disconnected event and a promotion are keyed
             * by. A Changed Notification from another domain, which
             * would move an ordinary record's engine to that peer, ends
             * it instead (no service: no Logout, no teardown, 3.16);
             * the new domain needs an announcement, or a request of its
             * own at a place the firmware named. */
            icm->XdCounters.AdoptOther++;
            tbtIcmLog(icm, "cm.xdp.adopt.other", (x->Slot + 1) << 24);
            x->EndGrace = TRUE;
            x->EndPending = TRUE;
        } else {
            (VOID)TbtXdpPeerOnPacket(&x->Peer, f.Pdf, f.Dw, f.Words);
            tbtIcmXdSync(icm, x);
        }
    }
    if (tick) {
        for (i = 0; i < TBT_ICM_MAX_XD && !icm->XdEndOwed; i++) {
            x = &icm->Xd[i];
            if (x->Used && !x->EndPending) {
                TbtXdpPeerTick(&x->Peer);
                tbtIcmXdSync(icm, x);
            }
        }
    }
    icm->XdTaking = FALSE;
    tbtIcmXdEndPending(icm);
    if (icm->XdEndOwed) {
        icm->XdEndOwed = FALSE;
        tbtIcmXdEndAll(icm, icm->XdEndOwedGone, icm->XdEndOwedGone);
        icm->XdEndOwedGone = FALSE;
    }
    tbtIcmXdAdopt(icm);
}

/* ---- the driver's entry points ----------------------------------------- */

VOID TbtIcmSetNetConfig(PTBT_ICM icm, BOOLEAN net, ULONG revision)
{
    icm->NetOn = net;
    icm->NetRevision = revision;
}

VOID TbtIcmSetNet(PTBT_ICM icm, const TBT_ICM_NET *net)
{
    if (net != NULL) {
        icm->Net = *net;
    } else {
        tbtIcmZero(&icm->Net, sizeof(icm->Net));
    }
}

VOID TbtIcmSetNetSerial(PTBT_ICM icm, ULONG lo, ULONG hi)
{
    icm->NetSerialValid = TRUE;
    icm->NetSerialLo = lo;
    icm->NetSerialHi = hi;
    icm->NetMacValid = FALSE;
}

/* On Falcon Ridge the E2E bit is withdrawn from the offer: its quirk's
 * Transmit E2E HopID 1 (record 35 section 4.3) is not one the service's
 * RingsUp is told. */
VOID TbtIcmNetWanted(PTBT_ICM icm, BOOLEAN wanted, ULONG prtcstns)
{
    if (tbtIcmXdFalcon(icm)) {
        prtcstns &= ~TBT_NET_STNS_E2E;
    }
    icm->NetWanted = wanted;
    icm->NetPrtcstns = prtcstns;
    tbtIcmXdOffer(icm);
}

BOOLEAN TbtIcmNetIdentity(PTBT_ICM icm, PUCHAR mac6)
{
    ULONG i;

    if (!icm->NetOn || !icm->Bound) {
        return FALSE;
    }
    tbtIcmXdIdentity(icm);
    for (i = 0; i < 6; i++) {
        mac6[i] = icm->NetMac[i];
    }
    return TRUE;
}

const TBT_XDP_PEER *TbtIcmPeer(const TBT_ICM *icm, ULONG slot)
{
    if (slot >= TBT_ICM_MAX_XD || !icm->Xd[slot].Used) {
        return NULL;
    }
    return &icm->Xd[slot].Peer;
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
    tbtIcmReadVersion(icm);
    frames = 0;
    while (frames < TBT_ICM_POLL_FRAMES && TbtRingReceive(icm->Ring, &f)) {
        frames++;
        tbtIcmFrame(icm, &f);
    }
    tbtIcmDrainQueue(icm, TBT_ICM_POLL_EVENTS);
    if (icm->RediscoverPending && icm->Bound) {
        tbtIcmRediscover(icm);
    }
    tbtIcmXdRun(icm, TRUE);
}

VOID TbtIcmRingWake(PTBT_ICM icm)
{
    TBT_FRAME f;
    ULONG frames;

    if (!icm->Bound) {
        return;
    }
    frames = 0;
    while (frames < TBT_ICM_POLL_FRAMES && TbtRingReceive(icm->Ring, &f)) {
        frames++;
        tbtIcmFrame(icm, &f);
    }
    tbtIcmDrainQueue(icm, TBT_ICM_POLL_EVENTS);
    tbtIcmXdRun(icm, FALSE);
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
