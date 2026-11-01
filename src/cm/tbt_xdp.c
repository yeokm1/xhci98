/*
 * tbt_xdp.c - the inter-domain discovery protocol and the USB4NET service,
 * their pure half (roadmap-usb4.md task 45.59, step S1; design record 35
 * sections 3 and 4). tbt_xdp.h has the contract.
 *
 * Written for this project from the USB4 Inter-Domain Service Protocol 2.0
 * ("IDS") and design record 35. Linux's drivers/thunderbolt and
 * drivers/net/thunderbolt were read for facts only (record 35 sections 2.3
 * and 2.4 cite them by symbol); no code is taken from either.
 *
 * IRQL: see tbt_xdp.h - the peer engine on the connection-manager thread
 * at PASSIVE_LEVEL, the rest pure.
 */

#include "tbt_xdp.h"

/* ---------------------------------------------------------------------- */
/* UUIDs                                                                  */
/* ---------------------------------------------------------------------- */

/* IDS 1.2's example and Table 5-3, and IDS 5.1.6's directory UUID. */
const TBT_UUID TbtXdpProtocolUuid = {
    { 0x0ED738B6UL, 0xBB40FF42UL, 0xE290C297UL, 0x07FFB2C0UL }
};
const TBT_UUID TbtNetProtocolUuid = {
    { 0x9E588F79UL, 0x478A1636UL, 0x6456C697UL, 0xDDC820A9UL }
};
const TBT_UUID TbtNetDirectoryUuid = {
    { 0xCA8961C6UL, 0x9541CE1CUL, 0x5949B8BDUL, 0x4F5A5F2EUL }
};

VOID TbtUuidFromBytes(PTBT_UUID uuid, const UCHAR *b)
{
    ULONG i;

    for (i = 0; i < 4; i++) {
        uuid->Dw[i] = (ULONG)b[4 * i] | ((ULONG)b[4 * i + 1] << 8) |
                      ((ULONG)b[4 * i + 2] << 16) | ((ULONG)b[4 * i + 3] << 24);
    }
}

VOID TbtUuidToBytes(const TBT_UUID *uuid, PUCHAR b)
{
    ULONG i;

    for (i = 0; i < 4; i++) {
        b[4 * i] = (UCHAR)(uuid->Dw[i] & 0xFF);
        b[4 * i + 1] = (UCHAR)((uuid->Dw[i] >> 8) & 0xFF);
        b[4 * i + 2] = (UCHAR)((uuid->Dw[i] >> 16) & 0xFF);
        b[4 * i + 3] = (UCHAR)((uuid->Dw[i] >> 24) & 0xFF);
    }
}

BOOLEAN TbtUuidEqual(const TBT_UUID *a, const TBT_UUID *b)
{
    return (BOOLEAN)(a->Dw[0] == b->Dw[0] && a->Dw[1] == b->Dw[1] &&
                     a->Dw[2] == b->Dw[2] && a->Dw[3] == b->Dw[3]);
}

LONG TbtUuidCompare(const TBT_UUID *a, const TBT_UUID *b)
{
    UCHAR x[16];
    UCHAR y[16];
    ULONG i;

    TbtUuidToBytes(a, x);
    TbtUuidToBytes(b, y);
    for (i = 0; i < 16; i++) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1L : 1L;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Packets                                                                */
/* ---------------------------------------------------------------------- */

static VOID tbtXdpCopyUuid(PULONG dst, const TBT_UUID *u)
{
    dst[0] = u->Dw[0];
    dst[1] = u->Dw[1];
    dst[2] = u->Dw[2];
    dst[3] = u->Dw[3];
}

static VOID tbtXdpTakeUuid(PTBT_UUID u, const ULONG *src)
{
    u->Dw[0] = src[0];
    u->Dw[1] = src[1];
    u->Dw[2] = src[2];
    u->Dw[3] = src[3];
}

static VOID tbtXdpZero(PVOID p, ULONG bytes)
{
    PUCHAR c = (PUCHAR)p;

    while (bytes-- != 0) {
        *c++ = 0;
    }
}

/* The common words: the route (the CM bit 0, IDS 2.1 and Spec 6.4.2.11),
 * the Data Size and SN, the protocol UUID; the rest of `words` zeroed. */
static ULONG tbtXdpHeader(PULONG out, const TBT_ROUTE *route, ULONG sn,
                          const TBT_UUID *proto, ULONG words)
{
    ULONG i;

    for (i = 0; i < words; i++) {
        out[i] = 0;
    }
    out[TBT_XDP_W_ROUTE_HI] = route->Hi & 0x00FFFFFFUL;
    out[TBT_XDP_W_ROUTE_LO] = route->Lo;
    out[TBT_XDP_W_LEN] = ((words - 3UL) & TBT_XDP_LEN_SIZE_MASK) |
                         ((sn << TBT_XDP_LEN_SN_SHIFT) & TBT_XDP_LEN_SN_MASK);
    tbtXdpCopyUuid(&out[TBT_XDP_W_PROTO], proto);
    return words;
}

static ULONG tbtXdpDisc(PULONG out, const TBT_ROUTE *route, ULONG sn,
                        ULONG type, ULONG words)
{
    tbtXdpHeader(out, route, sn, &TbtXdpProtocolUuid, words);
    out[TBT_XDP_W_TYPE] = type;
    return words;
}

ULONG TbtXdpBuildUuidRequest(PULONG out, const TBT_ROUTE *route, ULONG sn,
                             ULONG type)
{
    if (type != TBT_XDP_UUID_REQ && type != TBT_XDP_UUID_REQ_TBT3) {
        return 0;
    }
    return tbtXdpDisc(out, route, sn, type, 8);
}

/* IDS Figure 2-3 names DW 13 Source Route Lo and DW 14 Hi; Linux puts the
 * high half first (record 35 section 2.3, tb_msgs.h
 * tb_xdp_uuid_response). Nothing here reads the field back, so the order
 * the reference peer uses is the one sent. */
ULONG TbtXdpBuildUuidResponse(PULONG out, const TBT_ROUTE *route, ULONG sn,
                              const TBT_UUID *self, BOOLEAN withRoute)
{
    ULONG words = withRoute ? 14UL : 12UL;

    tbtXdpDisc(out, route, sn, TBT_XDP_UUID_RESP, words);
    tbtXdpCopyUuid(&out[8], self);
    if (withRoute) {
        out[12] = route->Hi & 0x00FFFFFFUL;
        out[13] = route->Lo;
    }
    return words;
}

ULONG TbtXdpBuildPropsRequest(PULONG out, const TBT_ROUTE *route, ULONG sn,
                              const TBT_UUID *src, const TBT_UUID *dst,
                              ULONG offset)
{
    if (offset > 0xFFFFUL) {
        return 0;
    }
    tbtXdpDisc(out, route, sn, TBT_XDP_PROPS_REQ, 17);
    tbtXdpCopyUuid(&out[8], src);
    tbtXdpCopyUuid(&out[12], dst);
    out[16] = offset;
    return 17;
}

ULONG TbtXdpBuildPropsResponse(PULONG out, const TBT_ROUTE *route, ULONG sn,
                               const TBT_UUID *src, const TBT_UUID *dst,
                               ULONG offset, const ULONG *block,
                               ULONG blockWords, ULONG generation)
{
    ULONG n;
    ULONG i;

    if (offset >= blockWords || blockWords > 0xFFFFUL) {
        return 0;
    }
    n = blockWords - offset;
    if (n > TBT_XDP_PROPS_CHUNK) {
        n = TBT_XDP_PROPS_CHUNK;
    }
    tbtXdpDisc(out, route, sn, TBT_XDP_PROPS_RESP, 18 + n);
    tbtXdpCopyUuid(&out[8], src);
    tbtXdpCopyUuid(&out[12], dst);
    out[16] = (offset & 0xFFFFUL) | (blockWords << 16);
    out[17] = generation;
    for (i = 0; i < n; i++) {
        out[18 + i] = block[offset + i];
    }
    return 18 + n;
}

ULONG TbtXdpBuildChanged(PULONG out, const TBT_ROUTE *route, ULONG sn,
                         const TBT_UUID *src)
{
    tbtXdpDisc(out, route, sn, TBT_XDP_CHANGED, 12);
    tbtXdpCopyUuid(&out[8], src);
    return 12;
}

ULONG TbtXdpBuildChangedResponse(PULONG out, const TBT_ROUTE *route, ULONG sn)
{
    return tbtXdpDisc(out, route, sn, TBT_XDP_CHANGED_RESP, 8);
}

ULONG TbtXdpBuildError(PULONG out, const TBT_ROUTE *route, ULONG sn,
                       ULONG code)
{
    tbtXdpDisc(out, route, sn, TBT_XDP_ERROR, 9);
    out[8] = code;
    return 9;
}

ULONG TbtXdpBuildLinkStatusRequest(PULONG out, const TBT_ROUTE *route,
                                   ULONG sn)
{
    return tbtXdpDisc(out, route, sn, TBT_XDP_LINK_STATUS_REQ, 8);
}

/* IDS Table 2-7: SLW in DW 10 bits 1:0, TLW in 9:8. */
ULONG TbtXdpBuildLinkStatusResponse(PULONG out, const TBT_ROUTE *route,
                                    ULONG sn, ULONG slw, ULONG tlw)
{
    tbtXdpDisc(out, route, sn, TBT_XDP_LINK_STATUS_RESP, 10);
    out[8] = 0;
    out[9] = (slw & 0x3UL) | ((tlw & 0x3UL) << 8);
    return 10;
}

/* IDS Figure 2-11: TLW in the low bits of DW 9 (as Linux's
 * tb_xdp_link_state_change places it). */
ULONG TbtXdpBuildLinkChangeRequest(PULONG out, const TBT_ROUTE *route,
                                   ULONG sn, ULONG tlw)
{
    tbtXdpDisc(out, route, sn, TBT_XDP_LINK_CHANGE_REQ, 9);
    out[8] = tlw & 0x3UL;
    return 9;
}

ULONG TbtXdpBuildLinkChangeResponse(PULONG out, const TBT_ROUTE *route,
                                    ULONG sn, ULONG status)
{
    tbtXdpDisc(out, route, sn, TBT_XDP_LINK_CHANGE_RESP, 9);
    out[8] = status;
    return 9;
}

static ULONG tbtNetHeader(PULONG out, const TBT_ROUTE *route,
                          const TBT_UUID *requester, const TBT_UUID *responder,
                          ULONG type, ULONG requestId, ULONG words)
{
    /* SN is not used by USB4NET (IDS Table 5-3); 0 is sent. */
    tbtXdpHeader(out, route, 0, &TbtNetProtocolUuid, words);
    tbtXdpCopyUuid(&out[TBT_NET_W_REQUESTER], requester);
    tbtXdpCopyUuid(&out[TBT_NET_W_RESPONDER], responder);
    out[TBT_NET_W_TYPE] = type;
    out[TBT_NET_W_REQID] = requestId;
    return words;
}

ULONG TbtNetBuildLogin(PULONG out, const TBT_ROUTE *route,
                       const TBT_UUID *requester, const TBT_UUID *responder,
                       ULONG requestId, ULONG serviceHop)
{
    tbtNetHeader(out, route, requester, responder, TBT_NET_LOGIN, requestId,
                 23);
    out[17] = TBT_NET_SERVICE_REVISION;
    out[18] = serviceHop;
    return 23;
}

/* IDS Table 5-6: "Responder MAC Address Hi is most significant 4 bytes";
 * the MAC as a 48-bit number, its first byte the most significant, so DW
 * 19 holds bytes 0 to 3 and DW 20 bytes 4 and 5 in its low half. Linux
 * packs the bytes otherwise and never reads the field (record 35 section
 * 2.4 point 8). */
ULONG TbtNetBuildLoginResponse(PULONG out, const TBT_ROUTE *route,
                               const TBT_UUID *requester,
                               const TBT_UUID *responder, ULONG requestId,
                               ULONG status, const UCHAR *mac6)
{
    tbtNetHeader(out, route, requester, responder, TBT_NET_LOGIN_RESP,
                 requestId, 25);
    out[17] = status;
    out[18] = ((ULONG)mac6[0] << 24) | ((ULONG)mac6[1] << 16) |
              ((ULONG)mac6[2] << 8) | (ULONG)mac6[3];
    out[19] = ((ULONG)mac6[4] << 8) | (ULONG)mac6[5];
    out[20] = 6;
    return 25;
}

ULONG TbtNetBuildLogout(PULONG out, const TBT_ROUTE *route,
                        const TBT_UUID *requester, const TBT_UUID *responder,
                        ULONG requestId)
{
    return tbtNetHeader(out, route, requester, responder, TBT_NET_LOGOUT,
                        requestId, 17);
}

ULONG TbtNetBuildLogoutResponse(PULONG out, const TBT_ROUTE *route,
                                const TBT_UUID *requester,
                                const TBT_UUID *responder, ULONG requestId,
                                ULONG status)
{
    tbtNetHeader(out, route, requester, responder, TBT_NET_LOGOUT_RESP,
                 requestId, 18);
    out[17] = status;
    return 18;
}

ULONG TbtXdpDecode(ULONG pdf, const ULONG *dw, ULONG words, PTBT_XDP_PKT p)
{
    TBT_UUID proto;
    ULONG size;

    tbtXdpZero(p, sizeof(*p));
    p->Kind = TBT_XDP_K_MALFORMED;
    p->Response = (BOOLEAN)(pdf == TBT_PDF_XDOMAIN_RESP);
    if ((pdf != TBT_PDF_XDOMAIN_REQ && pdf != TBT_PDF_XDOMAIN_RESP) ||
        words < 8 || words > TBT_XDP_MAX_WORDS) {
        return p->Kind;
    }
    size = dw[TBT_XDP_W_LEN] & TBT_XDP_LEN_SIZE_MASK;
    if (size != words - 3UL) {
        return p->Kind;
    }
    p->Route.Hi = dw[TBT_XDP_W_ROUTE_HI] & ~TBT_ROUTE_HI_CM;
    p->Route.Lo = dw[TBT_XDP_W_ROUTE_LO];
    p->Sn = (dw[TBT_XDP_W_LEN] & TBT_XDP_LEN_SN_MASK) >> TBT_XDP_LEN_SN_SHIFT;
    tbtXdpTakeUuid(&proto, &dw[TBT_XDP_W_PROTO]);

    if (TbtUuidEqual(&proto, &TbtXdpProtocolUuid)) {
        p->Type = dw[TBT_XDP_W_TYPE];
        switch (p->Type) {
        case TBT_XDP_UUID_REQ:
        case TBT_XDP_UUID_REQ_TBT3:
        case TBT_XDP_CHANGED_RESP:
        case TBT_XDP_LINK_STATUS_REQ:
            break;
        case TBT_XDP_UUID_RESP:
            if (words < 12) {
                return p->Kind;
            }
            tbtXdpTakeUuid(&p->Src, &dw[8]);
            p->HasSrcRoute = (BOOLEAN)(words >= 14);
            break;
        case TBT_XDP_PROPS_REQ:
            if (words < 17) {
                return p->Kind;
            }
            tbtXdpTakeUuid(&p->Src, &dw[8]);
            tbtXdpTakeUuid(&p->Dst, &dw[12]);
            p->Offset = dw[16] & 0xFFFFUL;
            break;
        case TBT_XDP_PROPS_RESP:
            if (words < 18) {
                return p->Kind;
            }
            tbtXdpTakeUuid(&p->Src, &dw[8]);
            tbtXdpTakeUuid(&p->Dst, &dw[12]);
            p->Offset = dw[16] & 0xFFFFUL;
            p->BlockSize = dw[16] >> 16;
            p->Generation = dw[17];
            p->Data = &dw[18];
            p->DataWords = words - 18;
            break;
        case TBT_XDP_CHANGED:
            if (words < 12) {
                return p->Kind;
            }
            tbtXdpTakeUuid(&p->Src, &dw[8]);
            break;
        case TBT_XDP_ERROR:
            if (words < 9) {
                return p->Kind;
            }
            p->Error = dw[8];
            break;
        case TBT_XDP_LINK_STATUS_RESP:
            if (words < 10) {
                return p->Kind;
            }
            p->Status = dw[8];
            p->Slw = dw[9] & 0x3UL;
            p->Tlw = (dw[9] >> 8) & 0x3UL;
            break;
        case TBT_XDP_LINK_CHANGE_REQ:
            if (words < 9) {
                return p->Kind;
            }
            p->Tlw = dw[8] & 0x3UL;
            break;
        case TBT_XDP_LINK_CHANGE_RESP:
            if (words < 9) {
                return p->Kind;
            }
            p->Status = dw[8];
            break;
        default:
            /* Unknown or reserved: still a discovery packet, which the
             * responder answers with Unknown Packet Type (IDS 3.6). */
            break;
        }
        p->Kind = TBT_XDP_K_DISCOVERY;
        return p->Kind;
    }

    if (TbtUuidEqual(&proto, &TbtNetProtocolUuid)) {
        if (words < 17) {
            return p->Kind;
        }
        tbtXdpTakeUuid(&p->Requester, &dw[TBT_NET_W_REQUESTER]);
        tbtXdpTakeUuid(&p->Responder, &dw[TBT_NET_W_RESPONDER]);
        p->Type = dw[TBT_NET_W_TYPE];
        p->RequestId = dw[TBT_NET_W_REQID];
        switch (p->Type) {
        case TBT_NET_LOGIN:
            if (words < 23) {       /* IDS Figure 5-2: DW 1 to 23 */
                return p->Kind;
            }
            p->Revision = dw[17];
            p->ServiceHop = dw[18];
            break;
        case TBT_NET_LOGIN_RESP:
            if (words < 25) {       /* IDS Figure 5-3: DW 1 to 25 */
                return p->Kind;
            }
            p->NetStatus = dw[17];
            p->Mac[0] = (UCHAR)(dw[18] >> 24);
            p->Mac[1] = (UCHAR)(dw[18] >> 16);
            p->Mac[2] = (UCHAR)(dw[18] >> 8);
            p->Mac[3] = (UCHAR)dw[18];
            p->Mac[4] = (UCHAR)(dw[19] >> 8);
            p->Mac[5] = (UCHAR)dw[19];
            p->MacLength = dw[20];
            break;
        case TBT_NET_LOGOUT:
            break;
        case TBT_NET_LOGOUT_RESP:
            if (words < 18) {
                return p->Kind;
            }
            p->NetStatus = dw[17];
            break;
        default:
            return p->Kind;
        }
        p->Kind = TBT_XDP_K_NET;
        return p->Kind;
    }

    p->Kind = TBT_XDP_K_OTHER;
    return p->Kind;
}

/* ---------------------------------------------------------------------- */
/* The properties block                                                   */
/* ---------------------------------------------------------------------- */

/* A key's two DWs, four characters each, the first in bits 31:24 (IDS
 * Table 4-10). */
static VOID tbtXdpKey(PULONG dst, const char *key)
{
    ULONG i;
    ULONG c;

    dst[0] = 0;
    dst[1] = 0;
    for (i = 0; i < 8 && key[i] != 0; i++) {
        c = (ULONG)(UCHAR)key[i];
        dst[i / 4] |= c << (24 - 8 * (i % 4));
    }
}

static BOOLEAN tbtXdpKeyIs(const ULONG *entry, const char *key)
{
    ULONG k[2];

    tbtXdpKey(k, key);
    return (BOOLEAN)(entry[0] == k[0] && entry[1] == k[1]);
}

static ULONG tbtXdpStrLen(const char *s)
{
    ULONG n = 0;

    while (s[n] != 0) {
        n++;
    }
    return n;
}

/* A text's DWs (NUL included, zero padded): the words it takes. */
static ULONG tbtXdpTextWords(const char *s)
{
    return (tbtXdpStrLen(s) + 1UL + 3UL) / 4UL;
}

static VOID tbtXdpPutText(PULONG dst, const char *s)
{
    ULONG n = tbtXdpTextWords(s);
    ULONG len = tbtXdpStrLen(s);
    ULONG i;

    for (i = 0; i < n; i++) {
        dst[i] = 0;
    }
    for (i = 0; i < len; i++) {
        dst[i / 4] |= (ULONG)(UCHAR)s[i] << (24 - 8 * (i % 4));
    }
}

static VOID tbtXdpEntry(PULONG e, const char *key, ULONG type, ULONG size,
                        ULONG value)
{
    tbtXdpKey(e, key);
    e[2] = (type << 24) | (size & 0xFFFFUL);
    e[3] = value;
}

ULONG TbtXdpPropsBuild(const TBT_XDP_LOCAL *l, PULONG block, ULONG capacity)
{
    ULONG rootEntries;
    ULONG pos;
    ULONG e;
    ULONG vt = 0;
    ULONG dt = 0;
    ULONG vtOff = 0;
    ULONG dtOff = 0;
    ULONG dirOff = 0;
    ULONG total;

    if (l->VendorText != NULL) {
        if (tbtXdpStrLen(l->VendorText) > TBT_XDP_TEXT_MAX) {
            return 0;
        }
        vt = tbtXdpTextWords(l->VendorText);
    }
    if (l->DeviceText != NULL) {
        if (tbtXdpStrLen(l->DeviceText) > TBT_XDP_TEXT_MAX) {
            return 0;
        }
        dt = tbtXdpTextWords(l->DeviceText);
    }
    rootEntries = 4UL + (vt != 0) + (dt != 0) + (l->Network ? 1UL : 0UL);
    total = 2UL + rootEntries * 4UL + vt + dt + (l->Network ? 20UL : 0UL);
    if (total > capacity) {
        return 0;
    }

    pos = 2UL + rootEntries * 4UL;
    if (vt != 0) {
        vtOff = pos;
        pos += vt;
    }
    if (dt != 0) {
        dtOff = pos;
        pos += dt;
    }
    if (l->Network) {
        dirOff = pos;
    }

    block[0] = TBT_XDP_ROOT_MAGIC;
    block[1] = rootEntries * 4UL;
    e = 2;
    tbtXdpEntry(&block[e], "vendorid", TBT_XDP_ENTRY_IMMEDIATE, 1, l->VendorId);
    e += 4;
    if (vt != 0) {
        tbtXdpEntry(&block[e], "vendorid", TBT_XDP_ENTRY_TEXT, vt, vtOff);
        e += 4;
        tbtXdpPutText(&block[vtOff], l->VendorText);
    }
    tbtXdpEntry(&block[e], "deviceid", TBT_XDP_ENTRY_IMMEDIATE, 1, l->DeviceId);
    e += 4;
    if (dt != 0) {
        tbtXdpEntry(&block[e], "deviceid", TBT_XDP_ENTRY_TEXT, dt, dtOff);
        e += 4;
        tbtXdpPutText(&block[dtOff], l->DeviceText);
    }
    tbtXdpEntry(&block[e], "devicerv", TBT_XDP_ENTRY_IMMEDIATE, 1,
                l->DeviceRevision);
    e += 4;
    tbtXdpEntry(&block[e], "maxhopid", TBT_XDP_ENTRY_IMMEDIATE, 1,
                l->MaxHopId & 0x7FUL);
    e += 4;
    if (l->Network) {
        tbtXdpEntry(&block[e], "network", TBT_XDP_ENTRY_DIRECTORY, 20, dirOff);
        tbtXdpCopyUuid(&block[dirOff], &TbtNetDirectoryUuid);
        tbtXdpEntry(&block[dirOff + 4], "prtcid", TBT_XDP_ENTRY_IMMEDIATE, 1, 1);
        tbtXdpEntry(&block[dirOff + 8], "prtcvers", TBT_XDP_ENTRY_IMMEDIATE, 1, 1);
        tbtXdpEntry(&block[dirOff + 12], "prtcrevs", TBT_XDP_ENTRY_IMMEDIATE, 1, 1);
        tbtXdpEntry(&block[dirOff + 16], "prtcstns", TBT_XDP_ENTRY_IMMEDIATE, 1,
                    l->Prtcstns);
    }
    return total;
}

/* A text entry's characters into `dst` (at most TBT_XDP_TEXT_MAX), refusing
 * one with no NUL within its length (IDS 4.3.2.3, 4.3.6). */
static BOOLEAN tbtXdpGetText(const ULONG *block, ULONG off, ULONG len,
                             char *dst)
{
    ULONG i;
    ULONG n = len * 4UL;
    UCHAR c;

    for (i = 0; i < n; i++) {
        c = (UCHAR)(block[off + i / 4] >> (24 - 8 * (i % 4)));
        if (c == 0) {
            dst[i < TBT_XDP_TEXT_MAX ? i : TBT_XDP_TEXT_MAX] = 0;
            return TRUE;
        }
        if (i < TBT_XDP_TEXT_MAX) {
            dst[i] = (char)c;
        }
    }
    dst[0] = 0;
    return FALSE;
}

/* A directory to check, on the explicit stack below. */
typedef struct _TBT_XDP_DIRREF {
    ULONG Offset;   /* of its first entry */
    ULONG Words;    /* of its entries */
    ULONG Depth;
} TBT_XDP_DIRREF;

#define TBT_XDP_DIR_STACK   32UL

LONG TbtXdpPropsParse(const ULONG *block, ULONG words, PTBT_XDP_REMOTE r)
{
    TBT_XDP_DIRREF stack[TBT_XDP_DIR_STACK];
    ULONG top = 0;
    ULONG dirs = 0;
    ULONG rootLen;
    BOOLEAN haveVendor = FALSE;
    BOOLEAN haveDevice = FALSE;

    tbtXdpZero(r, sizeof(*r));
    r->MaxHopId = 15;
    if (words < 2 || words > TBT_XDP_PROPS_MAX || block[0] != TBT_XDP_ROOT_MAGIC) {
        r->FaultOffset = 0;
        return TBT_E_PARAM;
    }
    rootLen = block[1];
    if (rootLen > words - 2UL || (rootLen % 4UL) != 0) {
        r->FaultOffset = 1;
        return TBT_E_PARAM;
    }
    stack[top].Offset = 2;
    stack[top].Words = rootLen;
    stack[top].Depth = 0;
    top++;

    while (top != 0) {
        TBT_XDP_DIRREF d;
        ULONG i;
        BOOLEAN isRoot;

        top--;
        d = stack[top];
        isRoot = (BOOLEAN)(d.Depth == 0);
        dirs++;
        for (i = 0; i + 4UL <= d.Words; i += 4UL) {
            const ULONG *e = &block[d.Offset + i];
            ULONG type = e[2] >> 24;
            ULONG size = e[2] & 0xFFFFUL;
            ULONG value = e[3];
            ULONG at = d.Offset + i;

            if (type == TBT_XDP_ENTRY_IMMEDIATE) {
                if (size != 1) {
                    r->FaultOffset = at;
                    return TBT_E_PARAM;
                }
            } else if (type == TBT_XDP_ENTRY_DATA || type == TBT_XDP_ENTRY_TEXT ||
                       type == TBT_XDP_ENTRY_DIRECTORY) {
                /* offset + length within the block, tested without
                 * forming a sum that could wrap */
                if (size == 0 || value >= words || size > words - value) {
                    r->FaultOffset = at;
                    return TBT_E_PARAM;
                }
            } else {
                continue;   /* vendor or unknown type: ignored (IDS 4.2) */
            }

            if (type == TBT_XDP_ENTRY_DIRECTORY) {
                if (size < 4 || ((size - 4UL) % 4UL) != 0 ||
                    d.Depth + 1UL > TBT_XDP_MAX_DEPTH ||
                    top >= TBT_XDP_DIR_STACK || dirs > 64UL) {
                    r->FaultOffset = at;
                    return TBT_E_PARAM;
                }
                stack[top].Offset = value + 4UL;
                stack[top].Words = size - 4UL;
                stack[top].Depth = d.Depth + 1UL;
                top++;
                if (isRoot && tbtXdpKeyIs(e, "network") && !r->Network) {
                    /* Read now, the nesting check of its own entries
                     * done again when it is popped. */
                    ULONG j;
                    TBT_UUID u;
                    BOOLEAN isNet;
                    ULONG prtcid = 0;
                    ULONG prtcvers = 0;
                    ULONG prtcstns = 0;
                    ULONG prtcrevs = 0;

                    /* Each candidate's own fields (record 35 section 3.7). */
                    tbtXdpTakeUuid(&u, &block[value]);
                    for (j = 0; j + 4UL <= size - 4UL; j += 4UL) {
                        const ULONG *n = &block[value + 4UL + j];

                        if ((n[2] >> 24) != TBT_XDP_ENTRY_IMMEDIATE) {
                            continue;
                        }
                        if (tbtXdpKeyIs(n, "prtcid")) {
                            prtcid = n[3];
                        } else if (tbtXdpKeyIs(n, "prtcvers")) {
                            prtcvers = n[3];
                        } else if (tbtXdpKeyIs(n, "prtcstns")) {
                            prtcstns = n[3];
                        } else if (tbtXdpKeyIs(n, "prtcrevs")) {
                            prtcrevs = n[3];
                        }
                    }
                    isNet = (BOOLEAN)(prtcid == 1 && prtcvers == 1);
                    if (isNet) {
                        r->Network = TRUE;
                        r->NetDirectory = u;
                        r->Prtcstns = prtcstns;
                        r->Prtcrevs = prtcrevs;
                    }
                }
                continue;
            }

            if (!isRoot) {
                if (type == TBT_XDP_ENTRY_TEXT) {
                    char tmp[TBT_XDP_TEXT_MAX + 1];

                    if (!tbtXdpGetText(block, value, size, tmp)) {
                        r->FaultOffset = at;
                        return TBT_E_PARAM;
                    }
                }
                continue;
            }

            if (tbtXdpKeyIs(e, "vendorid")) {
                if (type == TBT_XDP_ENTRY_IMMEDIATE) {
                    r->VendorId = value;
                    haveVendor = TRUE;
                } else if (type == TBT_XDP_ENTRY_TEXT &&
                           !tbtXdpGetText(block, value, size, r->VendorText)) {
                    r->FaultOffset = at;
                    return TBT_E_PARAM;
                }
            } else if (tbtXdpKeyIs(e, "deviceid")) {
                if (type == TBT_XDP_ENTRY_IMMEDIATE) {
                    r->DeviceId = value;
                    haveDevice = TRUE;
                } else if (type == TBT_XDP_ENTRY_TEXT &&
                           !tbtXdpGetText(block, value, size, r->DeviceText)) {
                    r->FaultOffset = at;
                    return TBT_E_PARAM;
                }
            } else if (tbtXdpKeyIs(e, "devicerv") &&
                       type == TBT_XDP_ENTRY_IMMEDIATE) {
                r->DeviceRevision = value;
            } else if (tbtXdpKeyIs(e, "maxhopid") &&
                       type == TBT_XDP_ENTRY_IMMEDIATE) {
                r->HasMaxHopId = TRUE;
                r->MaxHopId = value & 0x7FUL;
            } else if (type == TBT_XDP_ENTRY_TEXT) {
                char tmp[TBT_XDP_TEXT_MAX + 1];

                if (!tbtXdpGetText(block, value, size, tmp)) {
                    r->FaultOffset = at;
                    return TBT_E_PARAM;
                }
            }
        }
        if ((d.Words % 4UL) != 0) {
            r->FaultOffset = d.Offset;
            return TBT_E_PARAM;
        }
    }
    if (!haveVendor || !haveDevice) {
        r->FaultOffset = 2;
        return TBT_E_PARAM;
    }
    return TBT_OK;
}

/* ---------------------------------------------------------------------- */
/* MAC and HopID                                                          */
/* ---------------------------------------------------------------------- */

static ULONG tbtBitSwap16(ULONG v)
{
    ULONG r = 0;
    ULONG i;

    for (i = 0; i < 16; i++) {
        if (v & (1UL << i)) {
            r |= 1UL << (15 - i);
        }
    }
    return r;
}

VOID TbtNetMacFromUuid(ULONG uuidLo, ULONG uuidHi, ULONG routerId,
                       ULONG portNumber, PUCHAR mac)
{
    ULONG deviceId = (uuidLo >> 4) & 0x0FFFFFFFUL;
    ULONG project = tbtBitSwap16(uuidHi & 0xFFFFUL);
    ULONG authority = tbtBitSwap16((uuidHi >> 16) & 0xFFFFUL);
    ULONG multicast = 0;
    ULONG local = 1;

    mac[5] = (UCHAR)(((deviceId << 4) & 0xF0UL) | ((routerId << 2) & 0x0CUL) |
                     (portNumber & 0x03UL));
    mac[4] = (UCHAR)((deviceId >> 4) & 0xFFUL);
    mac[3] = (UCHAR)((deviceId >> 12) & 0xFFUL);
    mac[2] = (UCHAR)(((deviceId >> 20) & 0xFFUL) ^ ((authority >> 0) & 0x03UL));
    mac[1] = (UCHAR)((authority >> 2) & 0xFFUL);
    /* As written: `a | b | c ^ d` is `(a | b | c) ^ d` in C. */
    mac[0] = (UCHAR)(((multicast << 0) | (local << 1) |
                      ((authority >> 8) & 0xFCUL)) ^
                     (project & 0xFCUL));
}

static BOOLEAN tbtHopUsed(const ULONG *inUse, ULONG hop)
{
    if (inUse == NULL || hop > 127) {
        return FALSE;
    }
    return (BOOLEAN)((inUse[hop / 32] & (1UL << (hop % 32))) != 0);
}

ULONG TbtNetChooseTxHop(BOOLEAN peerHasMax, ULONG peerMax, ULONG laneMaxOut,
                        const ULONG *inUse4)
{
    ULONG hop;
    ULONG limit;

    if (!peerHasMax) {
        /* IDS 6.3.1.1.1, Table 6-2: exactly 15 for a Thunderbolt 3 peer. */
        if (laneMaxOut < 15 || tbtHopUsed(inUse4, 15)) {
            return 0;
        }
        return 15;
    }
    limit = peerMax < laneMaxOut ? peerMax : laneMaxOut;
    if (limit > 127) {
        limit = 127;
    }
    for (hop = 8; hop <= limit; hop++) {
        if (!tbtHopUsed(inUse4, hop)) {
            return hop;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* The peer engine                                                        */
/* ---------------------------------------------------------------------- */

#define TBT_XDP_NET_TYPE(t)     (0x100UL | (t))

static ULONG tbtNow(PTBT_XDP_PEER p)
{
    return p->Ops.NowMs(p->Ops.Context);
}

static VOID tbtLog(PTBT_XDP_PEER p, const char *label, ULONG value)
{
    if (p->Ops.Log != NULL) {
        p->Ops.Log(p->Ops.Context, label, value);
    }
}

/* Whether `now` has reached `due`: a wrap-safe comparison. */
static BOOLEAN tbtReached(ULONG now, ULONG due)
{
    return (BOOLEAN)((LONG)(now - due) >= 0);
}

static VOID tbtSlotArm(PTBT_XDP_SLOT s, ULONG due)
{
    s->Armed = TRUE;
    s->DueMs = due;
}

static VOID tbtSlotReset(PTBT_XDP_SLOT s, ULONG now)
{
    tbtXdpZero(s, sizeof(*s));
    s->StartMs = now;
    tbtSlotArm(s, now);
}

static LONG tbtSend(PTBT_XDP_PEER p, ULONG pdf, const ULONG *dw, ULONG words)
{
    if (words == 0) {
        return TBT_E_PARAM;
    }
    p->Counters.Sent++;
    return p->Ops.Send(p->Ops.Context, pdf, dw, words);
}

static ULONG tbtNextSn(PTBT_XDP_PEER p)
{
    ULONG sn = p->NextSn;

    p->NextSn = (p->NextSn + 1UL) & 3UL;
    return sn;
}

static VOID tbtBuildLocal(PTBT_XDP_PEER p)
{
    p->LocalWords = TbtXdpPropsBuild(&p->LocalProps, p->LocalBlock,
                                     TBT_XDP_LOCAL_MAX);
    if (p->LocalWords == 0) {
        tbtLog(p, "cm.xdp.props.build", 0);
    }
}

static VOID tbtStartState(PTBT_XDP_PEER p, ULONG state)
{
    p->State = state;
    p->Sub = 0;
    tbtSlotReset(&p->Disc, tbtNow(p));
    tbtLog(p, "cm.xdp.state", state);
}

static VOID tbtStartProps(PTBT_XDP_PEER p)
{
    p->RemoteGot = 0;
    p->RemoteWords = 0;
    tbtStartState(p, TBT_XDP_S_PROPS);
}

/* Bonding abandoned, or never possible: lane 1 off where it was usable
 * (Guide 7.5.2), then the properties. */
static VOID tbtSkipBonding(PTBT_XDP_PEER p)
{
    if (p->BondingPossible && p->Ops.LinkLane1Off != NULL) {
        p->Ops.LinkLane1Off(p->Ops.Context);
    }
    p->Counters.BondSkipped++;
    tbtStartProps(p);
}

/* The paths, by their one owner flag (record 35 section 3.8): whatever
 * the protocol state, ServiceDown is called once for each ServiceUp. */
static VOID tbtPathsDown(PTBT_XDP_PEER p, BOOLEAN gone)
{
    if (p->PathsUp) {
        p->PathsUp = FALSE;
        if (p->Ops.ServiceDown != NULL) {
            p->Ops.ServiceDown(p->Ops.Context, gone);
        }
    }
}

/* The service ends now, without a Logout. */
static VOID tbtServiceDown(PTBT_XDP_PEER p, BOOLEAN gone)
{
    tbtPathsDown(p, gone);
    p->LoginSentOk = FALSE;
    p->LoginReceived = FALSE;
    p->PendingLogin = FALSE;
    p->LoginSlow = FALSE;
    p->NetState = TBT_NET_S_OFF;
    tbtXdpZero(&p->Net, sizeof(p->Net));
}

/* Our Logout: the paths torn down when it is answered or times out. A
 * Logout already out is left to run. */
static VOID tbtStartLogout(PTBT_XDP_PEER p)
{
    if (p->NetState == TBT_NET_S_LOGOUT) {
        return;
    }
    if (p->NetState == TBT_NET_S_OFF || p->NetState == TBT_NET_S_ABANDONED) {
        tbtPathsDown(p, FALSE);
        p->NetState = TBT_NET_S_OFF;
        return;
    }
    p->NetState = TBT_NET_S_LOGOUT;
    tbtSlotReset(&p->Net, tbtNow(p));
    p->Net.Type = TBT_XDP_NET_TYPE(TBT_NET_LOGOUT);
    p->Net.FirstId = p->NextRequestId;
    p->Net.LastId = p->NextRequestId;
    p->WaitPeer = TRUE;
}

static VOID tbtStartLogin(PTBT_XDP_PEER p);
static VOID tbtMaybeServiceUp(PTBT_XDP_PEER p);

static VOID tbtLogoutDone(PTBT_XDP_PEER p)
{
    BOOLEAN pending = p->PendingLogin;
    ULONG hop = p->PendingHop;

    tbtServiceDown(p, FALSE);
    if (p->Ending) {
        p->Ending = FALSE;
        p->State = TBT_XDP_S_IDLE;
        p->Disc.Active = FALSE;
        p->Disc.Armed = FALSE;
        return;
    }
    if (pending && p->NetWanted) {
        /* The peer logged in again while ours was out: that half stands,
         * and this side logs in in turn (IDS 5.1.1) - now when the peer's
         * block is read, or at READY by tbtNetPoll when a re-read is
         * still running. */
        p->WaitPeer = FALSE;
        p->LoginReceived = TRUE;
        p->RxHop = hop;
        if (p->State == TBT_XDP_S_READY && p->RemoteInfo.Network) {
            tbtStartLogin(p);
        }
    }
}

static VOID tbtStartLogin(PTBT_XDP_PEER p)
{
    ULONG now = tbtNow(p);

    p->TxHop = TbtNetChooseTxHop(p->RemoteInfo.HasMaxHopId,
                                 p->RemoteInfo.MaxHopId, p->LaneMaxOut,
                                 p->HopsInUse);
    if (p->TxHop == 0) {
        tbtLog(p, "cm.net.login.badhop", p->RemoteInfo.MaxHopId);
        p->NetState = TBT_NET_S_ABANDONED;
        return;
    }
    p->NetState = TBT_NET_S_LOGIN;
    p->LoginSentOk = FALSE;
    p->LoginSlow = FALSE;
    p->LoginIdle = FALSE;
    tbtSlotReset(&p->Net, now);
    p->Net.Type = TBT_XDP_NET_TYPE(TBT_NET_LOGIN);
    p->Net.FirstId = p->NextRequestId;
    p->Net.LastId = p->NextRequestId;
}

/* Both logins done: the paths (record 35 section 4.1). */
static VOID tbtMaybeServiceUp(PTBT_XDP_PEER p)
{
    LONG rc;

    if (p->NetState != TBT_NET_S_LOGIN || !p->LoginSentOk ||
        !p->LoginReceived || p->PathsUp) {
        return;
    }
    rc = TBT_E_PARAM;
    if (p->Ops.ServiceUp != NULL) {
        rc = p->Ops.ServiceUp(p->Ops.Context, p->TxHop, p->RxHop);
    }
    if (rc == TBT_OK) {
        p->PathsUp = TRUE;
        p->NetState = TBT_NET_S_UP;
        tbtXdpZero(&p->Net, sizeof(p->Net));
        p->Counters.ServicesUp++;
        tbtLog(p, "cm.net.up", (p->TxHop << 8) | p->RxHop);
        return;
    }
    p->Counters.ServiceFailures++;
    tbtLog(p, "cm.net.up.fail", (ULONG)rc);
    tbtStartLogout(p);
}

/* What READY owes the service. */
static VOID tbtNetPoll(PTBT_XDP_PEER p)
{
    if (p->State != TBT_XDP_S_READY) {
        return;
    }
    if (p->NetState == TBT_NET_S_OFF && p->NetWanted && !p->WaitPeer &&
        p->RemoteInfo.Network) {
        tbtStartLogin(p);
    }
}

VOID TbtXdpPeerInit(PTBT_XDP_PEER peer, const TBT_XDP_OPS *ops,
                    const TBT_ROUTE *route, const TBT_UUID *local,
                    const TBT_UUID *remote, BOOLEAN firmwareCm,
                    BOOLEAN bondingPossible, const TBT_XDP_LOCAL *props,
                    const UCHAR *mac6, ULONG laneMaxOut,
                    const ULONG *hopsInUse4)
{
    ULONG i;

    tbtXdpZero(peer, sizeof(*peer));
    peer->Ops = *ops;
    peer->Route = *route;
    peer->Local = *local;
    if (remote != NULL) {
        peer->Remote = *remote;
        peer->HaveRemote = TRUE;
    }
    peer->FirmwareCm = firmwareCm;
    peer->BondingPossible = (BOOLEAN)(bondingPossible && !firmwareCm &&
                                      ops->LinkRead != NULL);
    peer->LocalProps = *props;
    peer->LocalProps.VendorText = props->VendorText;
    peer->LocalProps.DeviceText = props->DeviceText;
    peer->NetWanted = props->Network;
    peer->LocalMaxHop = props->MaxHopId & 0x7FUL;
    peer->LaneMaxOut = laneMaxOut;
    if (hopsInUse4 != NULL) {
        for (i = 0; i < 4; i++) {
            peer->HopsInUse[i] = hopsInUse4[i];
        }
    }
    for (i = 0; i < 6; i++) {
        peer->Mac[i] = mac6[i];
    }
    /* IDS 2.5: a fresh value at each start of the protocol. */
    peer->LocalGeneration = ops->NowMs(ops->Context) ^ 0x5A5A0000UL;
    peer->NextRequestId = 1;
    tbtBuildLocal(peer);
}

/* The firmware CM's UUID Request (record 35 section 3.11), sent or
 * resent; its answer is taken by tbtOnPacket. A send the ring refuses is
 * a try spent, resent at the next deadline. */
static VOID tbtNudge(PTBT_XDP_PEER p, ULONG now)
{
    ULONG buf[TBT_XDP_MAX_WORDS];
    ULONG words;

    p->NudgeTries++;
    p->NudgeDueMs = now + TBT_XDP_TRY_MS;
    words = TbtXdpBuildUuidRequest(buf, &p->Route, tbtNextSn(p), TBT_XDP_UUID_REQ);
    if (words != 0) {
        (VOID)tbtSend(p, TBT_PDF_XDOMAIN_REQ, buf, words);
    }
    if (p->NudgeTries == 1) {
        tbtLog(p, "cm.xdp.uuid.nudge", p->Route.Lo);
    }
}

VOID TbtXdpPeerStart(PTBT_XDP_PEER p)
{
    p->Counters.Restarts++;
    tbtServiceDown(p, TRUE);
    p->WaitPeer = FALSE;
    p->RemoteValid = FALSE;
    if (p->FirmwareCm && p->HaveRemote) {
        /* Run-45 45-E.15: the firmware answers the far end's UUID Request
         * itself, and a far software CM that gave up before this host's
         * driver bound restarts its handshake only at a UUID Request. One
         * is sent beside the discovery, resent until answered. */
        p->NudgeDone = FALSE;
        p->NudgeTries = 0;
        tbtNudge(p, tbtNow(p));
        tbtStartProps(p);
    } else {
        p->HaveRemote = FALSE;
        tbtStartState(p, TBT_XDP_S_UUID);
    }
    p->Disc.DueMs = tbtNow(p) + TBT_XDP_FIRST_MS;
}

/* A discovery step gave up. */
static VOID tbtDiscFailed(PTBT_XDP_PEER p)
{
    tbtLog(p, "cm.xdp.fail", p->State);
    switch (p->State) {
    case TBT_XDP_S_LINK:
    case TBT_XDP_S_BOND_LOW:
    case TBT_XDP_S_BOND_HIGH:
        tbtSkipBonding(p);
        break;
    default:
        p->State = TBT_XDP_S_ERROR;
        p->Disc.Active = FALSE;
        p->Disc.Armed = FALSE;
        break;
    }
}

/* Send the current discovery step's request. */
static VOID tbtDiscSend(PTBT_XDP_PEER p, ULONG now)
{
    ULONG buf[TBT_XDP_MAX_WORDS];
    ULONG words = 0;
    ULONG sn;

    if (p->Disc.Tries > TBT_XDP_RESENDS) {
        tbtDiscFailed(p);
        return;
    }
    sn = tbtNextSn(p);
    switch (p->State) {
    case TBT_XDP_S_UUID:
        p->Disc.Type = TBT_XDP_UUID_REQ;
        words = TbtXdpBuildUuidRequest(buf, &p->Route, sn, TBT_XDP_UUID_REQ);
        break;
    case TBT_XDP_S_LINK:
        p->Disc.Type = TBT_XDP_LINK_STATUS_REQ;
        words = TbtXdpBuildLinkStatusRequest(buf, &p->Route, sn);
        break;
    case TBT_XDP_S_BOND_LOW:
        if (p->Sub == 0) {
            p->Disc.Type = TBT_XDP_LINK_CHANGE_REQ;
            words = TbtXdpBuildLinkChangeRequest(buf, &p->Route, sn,
                                                 TBT_XDP_WIDTH_DUAL);
        } else {
            p->Disc.Type = TBT_XDP_LINK_STATUS_REQ;
            words = TbtXdpBuildLinkStatusRequest(buf, &p->Route, sn);
        }
        break;
    case TBT_XDP_S_PROPS:
        p->Disc.Type = TBT_XDP_PROPS_REQ;
        p->Disc.Offset = p->RemoteGot;
        words = TbtXdpBuildPropsRequest(buf, &p->Route, sn, &p->Local,
                                        &p->Remote, p->RemoteGot);
        break;
    case TBT_XDP_S_READY:
        p->Disc.Type = TBT_XDP_CHANGED;
        words = TbtXdpBuildChanged(buf, &p->Route, sn, &p->Local);
        break;
    default:
        return;
    }
    if (p->Disc.Tries != 0) {
        p->Counters.Resends++;
    }
    p->Disc.Tries++;
    p->Disc.Sn = sn;
    p->Disc.Active = TRUE;
    tbtSlotArm(&p->Disc, now + TBT_XDP_TRY_MS);
    (VOID)tbtSend(p, TBT_PDF_XDOMAIN_REQ, buf, words);
}

/* BOND_LOW step 2 and BOND_HIGH after the Change Request: the width. */
static VOID tbtWidthCheck(PTBT_XDP_PEER p, ULONG now, ULONG limitMs)
{
    ULONG slw = 0;
    ULONG tlw = 0;
    ULONG lanes = 0;

    if (p->Ops.LinkRead != NULL &&
        p->Ops.LinkRead(p->Ops.Context, &slw, &tlw, &lanes) == TBT_OK &&
        lanes >= 2) {
        p->Counters.Bonded++;
        tbtLog(p, "cm.xdp.bonded", lanes);
        tbtStartProps(p);
        return;
    }
    if (tbtReached(now, p->Disc.StartMs + limitMs)) {
        tbtLog(p, "cm.xdp.bond.width", lanes);
        tbtSkipBonding(p);
    }
}

static VOID tbtDiscTick(PTBT_XDP_PEER p, ULONG now)
{
    switch (p->State) {
    case TBT_XDP_S_BOND_LOW:
        if (p->Sub == 2) {
            tbtWidthCheck(p, now, TBT_XDP_WIDTH_LOW_MS);
            return;
        }
        break;
    case TBT_XDP_S_BOND_HIGH:
        if (p->Sub == 1) {
            tbtWidthCheck(p, now, TBT_XDP_WIDTH_WAIT_MS);
        } else if (tbtReached(now, p->Disc.StartMs + TBT_XDP_BOND_WAIT_MS)) {
            tbtLog(p, "cm.xdp.bond.nochange", 0);
            tbtSkipBonding(p);
        }
        return;
    case TBT_XDP_S_READY:
        if (!p->ChangedOwed && !p->Disc.Active) {
            return;
        }
        if (!p->Disc.Active) {
            tbtSlotReset(&p->Disc, now);
        }
        break;
    case TBT_XDP_S_UUID:
    case TBT_XDP_S_LINK:
    case TBT_XDP_S_PROPS:
        break;
    default:
        return;
    }
    if (!p->Disc.Armed || !tbtReached(now, p->Disc.DueMs)) {
        return;
    }
    if (p->State == TBT_XDP_S_READY && p->Disc.Tries > TBT_XDP_RESENDS) {
        /* IDS 3.3 gives a Changed Notification ten resends and no more. */
        p->ChangedOwed = FALSE;
        p->Disc.Active = FALSE;
        p->Disc.Armed = FALSE;
        tbtLog(p, "cm.xdp.changed.none", 0);
        return;
    }
    if (p->State == TBT_XDP_S_READY) {
        p->ChangedOwed = FALSE;
    }
    tbtDiscSend(p, now);
}

static VOID tbtNetTick(PTBT_XDP_PEER p, ULONG now)
{
    ULONG buf[TBT_XDP_MAX_WORDS];
    ULONG words;

    if (p->NetState == TBT_NET_S_LOGIN) {
        if (p->LoginSentOk) {
            tbtMaybeServiceUp(p);
            return;
        }
        if (!p->Net.Armed || !tbtReached(now, p->Net.DueMs)) {
            return;
        }
        if (!p->LoginIdle && p->Net.Tries != 0 &&
            tbtReached(now, p->Net.StartMs + TBT_NET_LOGIN_FOR_MS)) {
            /* Not given up (record 35 section 4.1, revision 8): the
             * idle rate from here, logged once. */
            tbtLog(p, "cm.net.login.none", p->Net.Tries);
            p->LoginIdle = TRUE;
        }
        p->Net.LastId = p->NextRequestId;
        words = TbtNetBuildLogin(buf, &p->Route, &p->Local, &p->Remote,
                                 p->NextRequestId, p->TxHop);
        p->NextRequestId++;
        if (p->Net.Tries != 0) {
            p->Counters.Resends++;
        }
        p->Net.Tries++;
        p->Net.Active = TRUE;
        p->Counters.LoginsSent++;
        tbtSlotArm(&p->Net, now + (p->LoginIdle ? TBT_NET_LOGIN_IDLE_MS :
                                   (!p->LoginSlow &&
                                    (ULONG)(now - p->Net.StartMs) <
                                    TBT_NET_LOGIN_FAST_FOR_MS) ?
                                   TBT_NET_LOGIN_FAST_MS : TBT_NET_LOGIN_SLOW_MS));
        (VOID)tbtSend(p, TBT_PDF_XDOMAIN_REQ, buf, words);
        return;
    }
    if (p->NetState == TBT_NET_S_LOGOUT) {
        if (!p->Net.Armed || !tbtReached(now, p->Net.DueMs)) {
            return;
        }
        if (p->Net.Tries > TBT_XDP_RESENDS) {
            /* No answer is taken as an answer (IDS 5.1.2). */
            tbtLog(p, "cm.net.logout.none", 0);
            tbtLogoutDone(p);
            return;
        }
        p->Net.LastId = p->NextRequestId;
        words = TbtNetBuildLogout(buf, &p->Route, &p->Local, &p->Remote,
                                  p->NextRequestId);
        p->NextRequestId++;
        if (p->Net.Tries != 0) {
            p->Counters.Resends++;
        }
        p->Net.Tries++;
        p->Net.Active = TRUE;
        p->Counters.LogoutsSent++;
        tbtSlotArm(&p->Net, now + TBT_XDP_TRY_MS);
        (VOID)tbtSend(p, TBT_PDF_XDOMAIN_REQ, buf, words);
    }
}

VOID TbtXdpPeerTick(PTBT_XDP_PEER p)
{
    ULONG now = tbtNow(p);

    tbtDiscTick(p, now);
    if (p->FirmwareCm && !p->NudgeDone && p->NudgeTries != 0 &&
        p->NudgeTries <= TBT_XDP_RESENDS && tbtReached(now, p->NudgeDueMs)) {
        tbtNudge(p, now);
    }
    tbtNetPoll(p);
    tbtNetTick(p, now);
}

/* The block has been read whole. */
static VOID tbtPropsComplete(PTBT_XDP_PEER p)
{
    TBT_XDP_REMOTE info;
    BOOLEAN hadNet = (BOOLEAN)(p->RemoteValid && p->RemoteInfo.Network);

    p->Counters.PropsReads++;
    if (TbtXdpPropsParse(p->RemoteBlock, p->RemoteWords, &info) != TBT_OK) {
        p->Counters.PropsRefused++;
        tbtLog(p, "cm.xdp.props.bad", info.FaultOffset);
        p->State = TBT_XDP_S_ERROR;
        p->Disc.Active = FALSE;
        p->Disc.Armed = FALSE;
        return;
    }
    p->RemoteInfo = info;
    p->RemoteValid = TRUE;
    p->State = TBT_XDP_S_READY;
    p->Disc.Active = FALSE;
    p->Disc.Armed = FALSE;
    tbtLog(p, "cm.xdp.ready", (info.VendorId << 16) | (info.DeviceId & 0xFFFFUL));
    if (p->Ops.PeerReady != NULL) {
        p->Ops.PeerReady(p->Ops.Context, &p->RemoteInfo);
    }
    if (hadNet && !info.Network) {
        /* The directory went (IDS 3.3): the service only. */
        tbtStartLogout(p);
    }
    if (info.Network) {
        p->WaitPeer = FALSE;
    }
}

static VOID tbtOnPropsResponse(PTBT_XDP_PEER p, const TBT_XDP_PKT *k)
{
    ULONG n;
    ULONG i;

    if (k->Offset != p->Disc.Offset ||
        !TbtUuidEqual(&k->Src, &p->Remote) ||
        !TbtUuidEqual(&k->Dst, &p->Local)) {
        p->Counters.Strays++;
        return;
    }
    if (p->RemoteGot == 0) {
        if (k->BlockSize == 0 || k->BlockSize > TBT_XDP_PROPS_MAX) {
            p->Counters.PropsRefused++;
            tbtLog(p, "cm.xdp.props.size", k->BlockSize);
            p->State = TBT_XDP_S_ERROR;
            p->Disc.Active = FALSE;
            p->Disc.Armed = FALSE;
            return;
        }
        p->RemoteWords = k->BlockSize;
        p->RemoteGeneration = k->Generation;
    } else if (k->Generation != p->RemoteGeneration ||
               k->BlockSize != p->RemoteWords) {
        /* IDS 3.2: changed mid-read, read again from the start. */
        tbtLog(p, "cm.xdp.props.gen", k->Generation);
        p->RemoteGot = 0;
        p->Disc.Active = FALSE;
        tbtSlotArm(&p->Disc, tbtNow(p));
        p->Disc.Tries = 0;
        return;
    }
    n = k->DataWords;
    if (n > p->RemoteWords - p->RemoteGot) {
        n = p->RemoteWords - p->RemoteGot;
    }
    if (n == 0) {
        p->Counters.Malformed++;
        return;     /* resent at the deadline */
    }
    for (i = 0; i < n; i++) {
        p->RemoteBlock[p->RemoteGot + i] = k->Data[i];
    }
    p->RemoteGot += n;
    p->Disc.Active = FALSE;
    p->Disc.Tries = 0;
    if (p->RemoteGot >= p->RemoteWords) {
        tbtPropsComplete(p);
        return;
    }
    tbtSlotArm(&p->Disc, tbtNow(p));
}

/* A discovery response for the outstanding request. */
static VOID tbtOnDiscResponse(PTBT_XDP_PEER p, const TBT_XDP_PKT *k)
{
    ULONG want;

    if (!p->Disc.Active || k->Sn != p->Disc.Sn) {
        p->Counters.Strays++;
        return;
    }
    switch (p->Disc.Type) {
    case TBT_XDP_UUID_REQ:          want = TBT_XDP_UUID_RESP; break;
    case TBT_XDP_PROPS_REQ:         want = TBT_XDP_PROPS_RESP; break;
    case TBT_XDP_CHANGED:           want = TBT_XDP_CHANGED_RESP; break;
    case TBT_XDP_LINK_STATUS_REQ:   want = TBT_XDP_LINK_STATUS_RESP; break;
    case TBT_XDP_LINK_CHANGE_REQ:   want = TBT_XDP_LINK_CHANGE_RESP; break;
    default:                        want = 0; break;
    }
    if (k->Type == TBT_XDP_ERROR) {
        if (k->Error == TBT_XDP_ERR_NOT_READY) {
            return;     /* resent at the deadline, a try spent (IDS 3.6) */
        }
        p->Disc.Active = FALSE;
        if (p->State == TBT_XDP_S_READY) {
            p->Disc.Armed = FALSE;
            return;
        }
        tbtDiscFailed(p);
        return;
    }
    if (k->Type != want) {
        p->Counters.Strays++;
        return;
    }

    switch (p->State) {
    case TBT_XDP_S_UUID:
        p->Disc.Active = FALSE;
        if (TbtUuidEqual(&k->Src, &p->Local)) {
            /* Guide 3.3: a loop in this domain. */
            tbtLog(p, "cm.xdp.loop", 0);
            p->State = TBT_XDP_S_LOOP;
            p->Disc.Armed = FALSE;
            return;
        }
        p->Remote = k->Src;
        p->HaveRemote = TRUE;
        if (p->BondingPossible) {
            tbtStartState(p, TBT_XDP_S_LINK);
        } else {
            tbtStartProps(p);
        }
        return;
    case TBT_XDP_S_LINK:
        p->Disc.Active = FALSE;
        if (k->Status != 0 || (k->Slw & 0x2UL) == 0) {
            /* IDS 3.4.1: the peer cannot bond. */
            tbtSkipBonding(p);
            return;
        }
        if (TbtUuidCompare(&p->Local, &p->Remote) < 0) {
            tbtStartState(p, TBT_XDP_S_BOND_LOW);
        } else {
            tbtStartState(p, TBT_XDP_S_BOND_HIGH);
            p->Disc.Armed = FALSE;
        }
        return;
    case TBT_XDP_S_BOND_LOW:
        p->Disc.Active = FALSE;
        if (p->Sub == 0) {
            if (k->Status != 0) {
                tbtSkipBonding(p);
                return;
            }
            p->Sub = 1;
            p->Disc.Tries = 0;
            tbtSlotArm(&p->Disc, tbtNow(p));
            return;
        }
        if (k->Status != 0 || k->Tlw != TBT_XDP_WIDTH_DUAL) {
            return;     /* asked again at the deadline (IDS 3.4.1) */
        }
        p->Sub = 2;
        p->Disc.Armed = FALSE;
        p->Disc.StartMs = tbtNow(p);
        if (p->Ops.LinkSetTarget == NULL || p->Ops.LinkBond == NULL ||
            p->Ops.LinkSetTarget(p->Ops.Context, TBT_XDP_WIDTH_DUAL) != TBT_OK ||
            p->Ops.LinkBond(p->Ops.Context) != TBT_OK) {
            tbtSkipBonding(p);
        }
        return;
    case TBT_XDP_S_PROPS:
        tbtOnPropsResponse(p, k);
        return;
    case TBT_XDP_S_READY:
        p->Disc.Active = FALSE;
        p->Disc.Armed = FALSE;
        return;
    default:
        return;
    }
}

static LONG tbtAnswer(PTBT_XDP_PEER p, const ULONG *buf, ULONG words)
{
    p->Counters.Answered++;
    return tbtSend(p, TBT_PDF_XDOMAIN_RESP, buf, words);
}

static VOID tbtAnswerError(PTBT_XDP_PEER p, ULONG sn, ULONG code)
{
    ULONG buf[TBT_XDP_MAX_WORDS];

    p->Counters.ErrorsSent++;
    (VOID)tbtAnswer(p, buf, TbtXdpBuildError(buf, &p->Route, sn, code));
}

/* The responder (record 35 section 3.6). */
static VOID tbtOnDiscRequest(PTBT_XDP_PEER p, const TBT_XDP_PKT *k)
{
    ULONG buf[TBT_XDP_MAX_WORDS];
    ULONG slw;
    ULONG tlw;
    ULONG lanes;

    switch (k->Type) {
    case TBT_XDP_UUID_REQ:
    case TBT_XDP_UUID_REQ_TBT3:
        (VOID)tbtAnswer(p, buf, TbtXdpBuildUuidResponse(buf, &p->Route, k->Sn,
                                                  &p->Local,
                                                  (BOOLEAN)(k->Type == TBT_XDP_UUID_REQ)));
        if (p->State == TBT_XDP_S_ERROR || p->State == TBT_XDP_S_IDLE) {
            TbtXdpPeerStart(p);
        }
        return;
    case TBT_XDP_PROPS_REQ:
        if (!TbtUuidEqual(&k->Dst, &p->Local) || k->Offset >= p->LocalWords) {
            p->Counters.Malformed++;
            return;     /* an illegal field: dropped (IDS chapter 2) */
        }
        (VOID)tbtAnswer(p, buf, TbtXdpBuildPropsResponse(buf, &p->Route, k->Sn,
                                                   &p->Local, &k->Src,
                                                   k->Offset, p->LocalBlock,
                                                   p->LocalWords,
                                                   p->LocalGeneration));
        return;
    case TBT_XDP_CHANGED:
        (VOID)tbtAnswer(p, buf, TbtXdpBuildChangedResponse(buf, &p->Route, k->Sn));
        if (p->HaveRemote && !TbtUuidEqual(&k->Src, &p->Remote)) {
            /* IDS 3.7: another domain on this link; the old peer's
             * service ends without a Logout, the discovery restarts. */
            tbtLog(p, "cm.xdp.newpeer", k->Src.Dw[0]);
            if (p->FirmwareCm) {
                tbtServiceDown(p, TRUE);
                p->Remote = k->Src;
                p->WaitPeer = FALSE;
                tbtStartProps(p);
            } else {
                TbtXdpPeerStart(p);
            }
            return;
        }
        if (p->State == TBT_XDP_S_READY || p->State == TBT_XDP_S_ERROR) {
            tbtStartProps(p);
        }
        return;
    case TBT_XDP_LINK_STATUS_REQ:
        if (p->FirmwareCm || p->Ops.LinkRead == NULL) {
            tbtAnswerError(p, k->Sn, TBT_XDP_ERR_NOT_SUPPORTED);
            return;
        }
        slw = 0;
        tlw = 0;
        lanes = 0;
        if (p->Ops.LinkRead(p->Ops.Context, &slw, &tlw, &lanes) != TBT_OK ||
            (p->State == TBT_XDP_S_BOND_HIGH && p->Sub == 1 &&
             tlw != p->PeerTlw)) {
            /* IDS 3.4.1: Not Ready until the Target Link Width the peer
             * asked for reads back. */
            tbtAnswerError(p, k->Sn, TBT_XDP_ERR_NOT_READY);
            return;
        }
        (VOID)tbtAnswer(p, buf, TbtXdpBuildLinkStatusResponse(buf, &p->Route, k->Sn,
                                                        slw, tlw));
        return;
    case TBT_XDP_LINK_CHANGE_REQ:
        if (p->FirmwareCm || p->Ops.LinkSetTarget == NULL) {
            tbtAnswerError(p, k->Sn, TBT_XDP_ERR_NOT_SUPPORTED);
            return;
        }
        if (p->State != TBT_XDP_S_BOND_HIGH) {
            tbtAnswerError(p, k->Sn, TBT_XDP_ERR_NOT_READY);
            return;
        }
        if (p->Sub == 1 && k->Tlw == p->PeerTlw) {
            /* A resend whose answer was lost: answered again, the
             * width deadline left running (IDS 3.4.1's resends). */
            (VOID)tbtAnswer(p, buf, TbtXdpBuildLinkChangeResponse(buf, &p->Route,
                                                                k->Sn, 0));
            return;
        }
        if (p->Ops.LinkSetTarget(p->Ops.Context, k->Tlw) != TBT_OK) {
            tbtAnswerError(p, k->Sn, TBT_XDP_ERR_NOT_READY);
            return;
        }
        (VOID)tbtAnswer(p, buf, TbtXdpBuildLinkChangeResponse(buf, &p->Route, k->Sn, 0));
        p->PeerTlw = k->Tlw;
        p->Sub = 1;
        p->Disc.StartMs = tbtNow(p);
        return;
    default:
        tbtAnswerError(p, k->Sn, TBT_XDP_ERR_UNKNOWN_TYPE);
        return;
    }
}

static BOOLEAN tbtNetPair(const TBT_XDP_PEER *p, const TBT_XDP_PKT *k)
{
    return (BOOLEAN)((TbtUuidEqual(&k->Requester, &p->Local) &&
                      TbtUuidEqual(&k->Responder, &p->Remote)) ||
                     (TbtUuidEqual(&k->Requester, &p->Remote) &&
                      TbtUuidEqual(&k->Responder, &p->Local)));
}

static BOOLEAN tbtIdUsed(const TBT_XDP_SLOT *s, ULONG id)
{
    return (BOOLEAN)((ULONG)(id - s->FirstId) <= (ULONG)(s->LastId - s->FirstId));
}

/* The peer's Login (record 35 section 4.1). */
static VOID tbtOnLogin(PTBT_XDP_PEER p, const TBT_XDP_PKT *k)
{
    ULONG buf[TBT_XDP_MAX_WORDS];
    ULONG status = 0;
    ULONG hop = k->ServiceHop;

    p->Counters.LoginsReceived++;
    if (!p->NetWanted || !p->LocalProps.Network ||
        k->Revision != TBT_NET_SERVICE_REVISION) {
        status = TBT_NET_STATUS_REFUSED;
    } else if (hop < 8 || hop > p->LocalMaxHop || tbtHopUsed(p->HopsInUse, hop)) {
        tbtLog(p, "cm.net.login.badhop", hop);
        status = TBT_NET_STATUS_REFUSED;
    }
    if (tbtAnswer(p, buf, TbtNetBuildLoginResponse(buf, &p->Route, &p->Remote,
                                                   &p->Local, k->RequestId,
                                                   status, p->Mac)) != TBT_OK) {
        /* Not sent: that half is not done (IDS 5.1.1); the peer resends. */
        tbtLog(p, "cm.net.login.unsent", k->RequestId);
        return;
    }
    if (status != 0) {
        p->Counters.LoginsRefused++;
        return;
    }
    if (p->NetState == TBT_NET_S_LOGOUT) {
        /* Ours is out: the new login is kept for when it ends. */
        p->PendingLogin = TRUE;
        p->PendingHop = hop;
        return;
    }
    if (p->NetState == TBT_NET_S_UP) {
        if (hop == p->RxHop) {
            return;     /* answered again, nothing changes */
        }
        /* A peer that started over with another HopID: built afresh. */
        tbtServiceDown(p, FALSE);
    }
    p->WaitPeer = FALSE;
    if (p->NetState == TBT_NET_S_OFF || p->NetState == TBT_NET_S_ABANDONED) {
        if (p->State == TBT_XDP_S_READY && p->RemoteInfo.Network) {
            tbtStartLogin(p);   /* IDS 5.1.1: and log in in turn */
        }
    }
    p->LoginReceived = TRUE;
    p->RxHop = hop;
    tbtMaybeServiceUp(p);
}

static VOID tbtOnLogout(PTBT_XDP_PEER p, const TBT_XDP_PKT *k)
{
    ULONG buf[TBT_XDP_MAX_WORDS];

    p->Counters.LogoutsReceived++;
    (VOID)tbtAnswer(p, buf, TbtNetBuildLogoutResponse(buf, &p->Route,
                                                      &p->Remote, &p->Local,
                                                      k->RequestId, 0));
    p->PendingLogin = FALSE;
    if (p->NetState == TBT_NET_S_LOGOUT) {
        return;     /* ours is out already; its end tears down */
    }
    if (p->NetState == TBT_NET_S_UP || p->NetState == TBT_NET_S_LOGIN) {
        /* Record 35 section 4.2: both halves end now, and this side logs
         * out in turn. */
        tbtPathsDown(p, FALSE);
        tbtStartLogout(p);
        return;
    }
    p->LoginReceived = FALSE;
    p->WaitPeer = TRUE;
}

static VOID tbtOnNetResponse(PTBT_XDP_PEER p, const TBT_XDP_PKT *k)
{
    if (k->Type == TBT_NET_LOGIN_RESP) {
        if (p->NetState != TBT_NET_S_LOGIN || p->LoginSentOk ||
            !p->Net.Active || !tbtIdUsed(&p->Net, k->RequestId)) {
            p->Counters.Strays++;
            return;
        }
        if (k->NetStatus != 0) {
            p->Counters.LoginFailures++;
            tbtLog(p, "cm.net.login.refused", k->NetStatus);
            /* Record 35 section 4.1: resent at the slow rate until the
             * minute ends. */
            p->LoginSlow = TRUE;
            tbtSlotArm(&p->Net, tbtNow(p) + TBT_NET_LOGIN_SLOW_MS);
            return;
        }
        p->LoginSentOk = TRUE;
        p->Net.Active = FALSE;
        p->Net.Armed = FALSE;
        if (k->MacLength == 6) {
            ULONG i;

            for (i = 0; i < 6; i++) {
                p->PeerMac[i] = k->Mac[i];
            }
        }
        tbtMaybeServiceUp(p);
        return;
    }
    if (k->Type == TBT_NET_LOGOUT_RESP) {
        /* Linux answers a Logout with a new id of its own (record 35
         * section 2.4 point 1): not checked. */
        if (p->NetState != TBT_NET_S_LOGOUT || !p->Net.Active) {
            p->Counters.Strays++;
            return;
        }
        tbtLogoutDone(p);
        return;
    }
    p->Counters.Strays++;
}

BOOLEAN TbtXdpPeerOnPacket(PTBT_XDP_PEER p, ULONG pdf, const ULONG *dw,
                           ULONG words)
{
    TBT_XDP_PKT k;
    ULONG kind = TbtXdpDecode(pdf, dw, words, &k);

    if (kind == TBT_XDP_K_OTHER) {
        return FALSE;
    }
    p->Counters.Received++;
    if (kind == TBT_XDP_K_MALFORMED) {
        p->Counters.Malformed++;
        return TRUE;
    }
    if (!TbtRouteEqual(&k.Route, &p->Route)) {
        p->Counters.Strays++;
        return TRUE;
    }
    if (kind == TBT_XDP_K_DISCOVERY) {
        if (k.Response) {
            if (p->FirmwareCm && k.Type == TBT_XDP_UUID_RESP) {
                p->NudgeDone = TRUE;    /* the nudge's answer, not the discovery's */
            } else {
                tbtOnDiscResponse(p, &k);
            }
        } else {
            tbtOnDiscRequest(p, &k);
        }
        return TRUE;
    }
    /* USB4NET, by its type and never its PDF: thunderbolt-net sends its
     * requests on PDF 7 (record 35 section 2.4 point 1). */
    if (!p->HaveRemote || !tbtNetPair(p, &k)) {
        p->Counters.Strays++;
        return TRUE;
    }
    if (k.Type == TBT_NET_LOGIN) {
        if (!(TbtUuidEqual(&k.Requester, &p->Remote) &&
              TbtUuidEqual(&k.Responder, &p->Local))) {
            p->Counters.Strays++;
            return TRUE;
        }
        tbtOnLogin(p, &k);
    } else if (k.Type == TBT_NET_LOGOUT) {
        if (!(TbtUuidEqual(&k.Requester, &p->Remote) &&
              TbtUuidEqual(&k.Responder, &p->Local))) {
            p->Counters.Strays++;
            return TRUE;
        }
        tbtOnLogout(p, &k);
    } else {
        tbtOnNetResponse(p, &k);
    }
    return TRUE;
}

VOID TbtXdpPeerOnLinkEvent(PTBT_XDP_PEER p)
{
    ULONG now = tbtNow(p);

    if ((p->State == TBT_XDP_S_BOND_LOW && p->Sub == 2) ||
        (p->State == TBT_XDP_S_BOND_HIGH && p->Sub == 1)) {
        tbtWidthCheck(p, now, p->State == TBT_XDP_S_BOND_LOW ?
                      TBT_XDP_WIDTH_LOW_MS : TBT_XDP_WIDTH_WAIT_MS);
    }
}

VOID TbtXdpPeerSetNetWanted(PTBT_XDP_PEER p, BOOLEAN wanted, ULONG prtcstns)
{
    BOOLEAN changed = (BOOLEAN)(p->LocalProps.Network != wanted ||
                                (wanted && p->LocalProps.Prtcstns != prtcstns));

    p->NetWanted = wanted;
    p->LocalProps.Network = wanted;
    if (wanted) {
        p->LocalProps.Prtcstns = prtcstns;
        p->WaitPeer = FALSE;
    } else if (p->NetState != TBT_NET_S_OFF) {
        tbtStartLogout(p);
    }
    if (!changed) {
        return;
    }
    p->LocalGeneration++;
    tbtBuildLocal(p);
    if (p->HaveRemote && p->State != TBT_XDP_S_IDLE &&
        p->State != TBT_XDP_S_LOOP) {
        p->ChangedOwed = TRUE;
    }
}

VOID TbtXdpPeerEnd(PTBT_XDP_PEER p, BOOLEAN gone)
{
    if (!gone && p->NetState == TBT_NET_S_LOGOUT) {
        p->Ending = TRUE;       /* the Logout out already ends it */
        p->ChangedOwed = FALSE;
        return;
    }
    if (!gone && (p->NetState == TBT_NET_S_UP || p->NetState == TBT_NET_S_LOGIN)) {
        tbtStartLogout(p);
        p->Ending = TRUE;
        p->Disc.Active = FALSE;
        p->Disc.Armed = FALSE;
        p->ChangedOwed = FALSE;
        return;
    }
    tbtServiceDown(p, gone);
    tbtXdpZero(&p->Disc, sizeof(p->Disc));
    tbtXdpZero(&p->Net, sizeof(p->Net));
    p->ChangedOwed = FALSE;
    p->Ending = FALSE;
    p->State = TBT_XDP_S_IDLE;
}

BOOLEAN TbtXdpPeerQuiet(const TBT_XDP_PEER *p)
{
    return (BOOLEAN)(!p->Disc.Active && !p->Net.Active && !p->ChangedOwed &&
                     !p->PathsUp &&
                     p->NetState != TBT_NET_S_LOGOUT &&
                     p->NetState != TBT_NET_S_LOGIN);
}
