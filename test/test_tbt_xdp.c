/*
 * test_tbt_xdp.c - host vectors for the inter-domain discovery protocol and
 * the USB4NET service (src\cm\tbt_xdp.c; roadmap-usb4.md task 45.59, step
 * S1; design record 35 sections 3, 4 and 11.1).
 *
 * The vectors are the USB4 Inter-Domain Service Protocol 2.0's own: the
 * UUID examples of its section 1.2 and Table 5-3, the sample properties
 * block of its Table 4-10 word for word, and the packet layouts of its
 * chapters 2 and 5. The peer engine is run against a second engine over a
 * modelled link with a virtual clock, and against packets shaped as
 * record 35 section 2.4 says Linux's thunderbolt-net shapes them (its
 * requests on PDF 7, the sender's UUID first, a Logout Response with an
 * id of its own); no Linux code is used.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_xdp.h"
#include "test_harness.h"

/* ------------------------------------------------------------------ */
/* UUIDs                                                              */
/* ------------------------------------------------------------------ */

static void test_uuid(void)
{
    static const UCHAR xdp[16] = {
        0xB6, 0x38, 0xD7, 0x0E, 0x42, 0xFF, 0x40, 0xBB,
        0x97, 0xC2, 0x90, 0xE2, 0xC0, 0xB2, 0xFF, 0x07
    };
    static const UCHAR net[16] = {
        0x79, 0x8F, 0x58, 0x9E, 0x36, 0x16, 0x8A, 0x47,
        0x97, 0xC6, 0x56, 0x64, 0xA9, 0x20, 0xC8, 0xDD
    };
    static const UCHAR dir[16] = {
        0xC6, 0x61, 0x89, 0xCA, 0x1C, 0xCE, 0x41, 0x95,
        0xBD, 0xB8, 0x49, 0x59, 0x2E, 0x5F, 0x5A, 0x4F
    };
    TBT_UUID u;
    TBT_UUID a;
    TBT_UUID b;
    UCHAR back[16];
    UCHAR x[16];

    TbtUuidFromBytes(&u, xdp);
    CHECK_EQ(u.Dw[0], 0x0ED738B6UL, "IDS 1.2 DW 0");
    CHECK_EQ(u.Dw[1], 0xBB40FF42UL, "IDS 1.2 DW 1");
    CHECK_EQ(u.Dw[2], 0xE290C297UL, "IDS 1.2 DW 2");
    CHECK_EQ(u.Dw[3], 0x07FFB2C0UL, "IDS 1.2 DW 3");
    CHECK(TbtUuidEqual(&u, &TbtXdpProtocolUuid), "the discovery UUID constant");
    TbtUuidFromBytes(&u, net);
    CHECK(TbtUuidEqual(&u, &TbtNetProtocolUuid), "Table 5-3's USB4NET UUID");
    TbtUuidFromBytes(&u, dir);
    CHECK(TbtUuidEqual(&u, &TbtNetDirectoryUuid), "Table 4-10's directory UUID");
    TbtUuidToBytes(&u, back);
    CHECK(memcmp(back, dir, 16) == 0, "round trip");

    /* IDS 3.4.1's example: 44444444-AAAA-BBBB-4444... is larger than
     * 44334444-AAAA-BBBB-4444...55... */
    memset(x, 0x44, 16);
    x[4] = 0xAA; x[5] = 0xAA; x[6] = 0xBB; x[7] = 0xBB;
    TbtUuidFromBytes(&a, x);
    x[1] = 0x33;
    x[14] = 0x55;
    TbtUuidFromBytes(&b, x);
    CHECK(TbtUuidCompare(&a, &b) > 0, "IDS 3.4.1 comparison");
    CHECK(TbtUuidCompare(&b, &a) < 0, "and the other way");
    CHECK(TbtUuidCompare(&a, &a) == 0, "equal");
}

/* ------------------------------------------------------------------ */
/* The sample block of IDS Table 4-10                                 */
/* ------------------------------------------------------------------ */

static const ULONG kSample[53] = {
    0x55584401UL, 0x00000018UL,
    0x76656e64UL, 0x6f726964UL, 0x76000001UL, 0x00000a27UL,
    0x76656e64UL, 0x6f726964UL, 0x74000003UL, 0x0000001aUL,
    0x64657669UL, 0x63656964UL, 0x76000001UL, 0x0000000aUL,
    0x64657669UL, 0x63656964UL, 0x74000003UL, 0x0000001dUL,
    0x6D617868UL, 0x6F706964UL, 0x76000001UL, 0x00000003UL,
    0x6e657477UL, 0x6f726b00UL, 0x44000014UL, 0x00000021UL,
    0x4170706cUL, 0x6520496eUL, 0x632e0000UL,
    0x4d616369UL, 0x6e746f73UL, 0x68000000UL,
    0x00000000UL,
    0xca8961c6UL, 0x9541ce1cUL, 0x5949b8bdUL, 0x4f5a5f2eUL,
    0x70727463UL, 0x69640000UL, 0x76000001UL, 0x00000001UL,
    0x70727463UL, 0x76657273UL, 0x76000001UL, 0x00000001UL,
    0x70727463UL, 0x72657673UL, 0x76000001UL, 0x00000001UL,
    0x70727463UL, 0x73746e73UL, 0x76000001UL, 0x00000000UL
};

static void test_sample_block(void)
{
    TBT_XDP_REMOTE r;
    ULONG b[60];

    CHECK_EQ(TbtXdpPropsParse(kSample, 53, &r), TBT_OK, "Table 4-10 parses");
    CHECK_EQ(r.VendorId, 0xa27UL, "vendor");
    CHECK_EQ(r.DeviceId, 0xaUL, "device");
    CHECK(strcmp(r.VendorText, "Apple Inc.") == 0, "vendor text");
    CHECK(strcmp(r.DeviceText, "Macintosh") == 0, "device text");
    CHECK(r.HasMaxHopId && r.MaxHopId == 3, "maxhopid");
    CHECK(r.Network, "network directory");
    CHECK(TbtUuidEqual(&r.NetDirectory, &TbtNetDirectoryUuid), "its UUID");
    CHECK_EQ(r.Prtcstns, 0UL, "prtcstns");
    CHECK_EQ(r.Prtcrevs, 1UL, "prtcrevs");

    /* Refusals, one at a time. */
    memcpy(b, kSample, sizeof(kSample));
    b[0] = 0x55584402UL;
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "bad header");
    memcpy(b, kSample, sizeof(kSample));
    b[1] = 0x00000060UL;
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "root past the block");
    memcpy(b, kSample, sizeof(kSample));
    b[9] = 0x00000034UL;        /* the text at 52, 3 long: past the end */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "text past the block");
    CHECK_EQ(r.FaultOffset, 6UL, "and where");
    memcpy(b, kSample, sizeof(kSample));
    b[9] = 0xFFFFFFFFUL;        /* an offset that would wrap a sum */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "wrapping offset");
    memcpy(b, kSample, sizeof(kSample));
    b[4] = 0x76000002UL;        /* an immediate of size 2 */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "immediate size");
    memcpy(b, kSample, sizeof(kSample));
    b[0x1C] = 0x632e2e2eUL;     /* "Apple Inc..." with no NUL in its 3 DWs */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "unterminated text");
    memcpy(b, kSample, sizeof(kSample));
    b[0x18] = 0x44000003UL;     /* a directory shorter than its UUID */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "short directory");
    CHECK_EQ(TbtXdpPropsParse(kSample, 501, &r), TBT_E_PARAM, "over 500 words");
    memcpy(b, kSample, sizeof(kSample));
    b[2] = 0x12345678UL;        /* vendorid's immediate key renamed */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_E_PARAM, "vendorid required");
    memcpy(b, kSample, sizeof(kSample));
    b[0x12] = 0x41414141UL;     /* maxhopid renamed: 15 taken */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_OK, "maxhopid optional");
    CHECK(!r.HasMaxHopId && r.MaxHopId == 15, "and 15 without it");
    memcpy(b, kSample, sizeof(kSample));
    b[0x28] = 2;                /* prtcid 2: not USB4NET */
    CHECK_EQ(TbtXdpPropsParse(b, 53, &r), TBT_OK, "other protocol parses");
    CHECK(!r.Network, "but is not USB4NET");
}

/* A self-nesting block: a directory whose one entry is itself. */
static void test_nesting(void)
{
    ULONG b[22];
    TBT_XDP_REMOTE r;

    memset(b, 0, sizeof(b));
    b[0] = TBT_XDP_ROOT_MAGIC;
    b[1] = 12;
    b[2] = 0x76656e64UL; b[3] = 0x6f726964UL; b[4] = 0x76000001UL; b[5] = 1;
    b[6] = 0x64657669UL; b[7] = 0x63656964UL; b[8] = 0x76000001UL; b[9] = 2;
    b[10] = 0x41414141UL; b[11] = 0x41414141UL; b[12] = 0x44000008UL; b[13] = 14;
    /* The directory at 14: its UUID at 14..17, its one entry at 18..21 is
     * the same directory again, so it would nest forever. */
    b[18] = 0x42424242UL; b[19] = 0x42424242UL; b[20] = 0x44000008UL; b[21] = 14;
    CHECK_EQ(TbtXdpPropsParse(b, 22, &r), TBT_E_PARAM, "endless nesting refused");
    b[20] = 0x76000001UL;
    CHECK_EQ(TbtXdpPropsParse(b, 22, &r), TBT_OK, "one level parses");
}

static void test_build_block(void)
{
    TBT_XDP_LOCAL l;
    ULONG b[TBT_XDP_LOCAL_MAX];
    ULONG n;
    TBT_XDP_REMOTE r;

    memset(&l, 0, sizeof(l));
    l.VendorId = 0;
    l.VendorText = "xHCI98";
    l.DeviceId = 1;
    l.DeviceText = "xhcicm";
    l.DeviceRevision = 0x03040000UL;
    l.MaxHopId = 31;
    l.Network = TRUE;
    l.Prtcstns = TBT_NET_STNS_E2E | TBT_NET_STNS_FRAME_ID;
    n = TbtXdpPropsBuild(&l, b, TBT_XDP_LOCAL_MAX);
    CHECK_EQ(n, 54UL, "record 35 section 3.7's 54 words");
    CHECK_EQ(b[0], TBT_XDP_ROOT_MAGIC, "header");
    CHECK_EQ(b[1], 28UL, "seven root entries");
    CHECK_EQ(TbtXdpPropsParse(b, n, &r), TBT_OK, "our block parses");
    CHECK(strcmp(r.VendorText, "xHCI98") == 0, "vendor text");
    CHECK(strcmp(r.DeviceText, "xhcicm") == 0, "device text");
    CHECK(r.Network && r.Prtcstns == 3, "network, prtcstns");
    CHECK(r.MaxHopId == 31, "maxhopid");
    l.Network = FALSE;
    n = TbtXdpPropsBuild(&l, b, TBT_XDP_LOCAL_MAX);
    CHECK_EQ(n, 30UL, "without the directory");
    CHECK_EQ(TbtXdpPropsParse(b, n, &r), TBT_OK, "parses");
    CHECK(!r.Network, "no network");
    CHECK_EQ(TbtXdpPropsBuild(&l, b, 20), 0UL, "refused when too small");
}

/* ------------------------------------------------------------------ */
/* Packets                                                            */
/* ------------------------------------------------------------------ */

static void test_packets(void)
{
    ULONG b[TBT_XDP_MAX_WORDS];
    TBT_ROUTE rt;
    TBT_XDP_PKT k;
    TBT_UUID a;
    TBT_UUID c;
    ULONG n;
    static const UCHAR mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
    ULONG i;

    rt.Hi = 0;
    rt.Lo = 1;
    memset(&a, 0, sizeof(a));
    a.Dw[0] = 0x11111111UL;
    memset(&c, 0, sizeof(c));
    c.Dw[0] = 0x22222222UL;

    n = TbtXdpBuildUuidRequest(b, &rt, 2, TBT_XDP_UUID_REQ);
    CHECK_EQ(n, 8UL, "UUID Request: IDS DW 1 to 8");
    CHECK_EQ(b[2], 5UL | (2UL << 27), "Data Size 5, SN 2");
    CHECK_EQ(b[3], 0x0ED738B6UL, "protocol UUID");
    CHECK_EQ(b[7], 12UL, "type 12");
    CHECK_EQ(TbtXdpDecode(6, b, n, &k), TBT_XDP_K_DISCOVERY, "decodes");
    CHECK(k.Type == 12 && k.Sn == 2 && !k.Response, "fields");
    CHECK_EQ(TbtXdpDecode(6, b, n - 1, &k), TBT_XDP_K_MALFORMED, "length mismatch");
    CHECK_EQ(TbtXdpBuildUuidRequest(b, &rt, 0, 3), 0UL, "not a UUID type");

    n = TbtXdpBuildUuidResponse(b, &rt, 1, &a, TRUE);
    CHECK_EQ(n, 14UL, "UUID Response with the route");
    b[0] |= TBT_ROUTE_HI_CM;
    CHECK_EQ(TbtXdpDecode(7, b, n, &k), TBT_XDP_K_DISCOVERY, "decodes");
    CHECK(TbtUuidEqual(&k.Src, &a) && k.HasSrcRoute && k.Route.Hi == 0,
          "source UUID, the CM bit cleared");
    CHECK_EQ(TbtXdpBuildUuidResponse(b, &rt, 1, &a, FALSE), 12UL,
             "to a Thunderbolt 3 request (IDS 6.1.3): no route, CRC at DW 13");

    n = TbtXdpBuildPropsRequest(b, &rt, 0, &a, &c, 45);
    CHECK_EQ(n, 17UL, "Properties Read Request: DW 1 to 17");
    CHECK_EQ(TbtXdpDecode(6, b, n, &k), TBT_XDP_K_DISCOVERY, "decodes");
    CHECK(k.Offset == 45 && TbtUuidEqual(&k.Src, &a) && TbtUuidEqual(&k.Dst, &c),
          "fields");

    n = TbtXdpBuildPropsResponse(b, &rt, 0, &a, &c, 0, kSample, 53, 0x1234UL);
    CHECK_EQ(n, 63UL, "a full response: 45 words of block");
    CHECK_EQ(b[16], 0UL | (53UL << 16), "offset 0, size 53");
    CHECK_EQ(TbtXdpDecode(7, b, n, &k), TBT_XDP_K_DISCOVERY, "decodes");
    CHECK(k.BlockSize == 53 && k.DataWords == 45 && k.Generation == 0x1234UL,
          "fields");
    n = TbtXdpBuildPropsResponse(b, &rt, 0, &a, &c, 45, kSample, 53, 0);
    CHECK_EQ(n, 26UL, "the last: 8 words");
    CHECK_EQ(TbtXdpBuildPropsResponse(b, &rt, 0, &a, &c, 53, kSample, 53, 0),
             0UL, "offset at the end refused");

    n = TbtXdpBuildLinkStatusResponse(b, &rt, 3, 3, 1);
    CHECK_EQ(b[9], 0x103UL, "SLW 1:0, TLW 9:8");
    CHECK_EQ(TbtXdpDecode(7, b, n, &k), TBT_XDP_K_DISCOVERY, "decodes");
    CHECK(k.Slw == 3 && k.Tlw == 1, "widths");
    n = TbtXdpBuildLinkChangeRequest(b, &rt, 0, TBT_XDP_WIDTH_DUAL);
    CHECK_EQ(TbtXdpDecode(6, b, n, &k), TBT_XDP_K_DISCOVERY, "change decodes");
    CHECK_EQ(k.Tlw, 3UL, "TLW");
    n = TbtXdpBuildError(b, &rt, 1, TBT_XDP_ERR_NOT_READY);
    CHECK_EQ(TbtXdpDecode(7, b, n, &k), TBT_XDP_K_DISCOVERY, "error decodes");
    CHECK(k.Type == TBT_XDP_ERROR && k.Error == 4, "Not Ready is 4");

    n = TbtNetBuildLogin(b, &rt, &a, &c, 7, 8);
    CHECK_EQ(n, 23UL, "Login: DW 1 to 23");
    CHECK_EQ(b[3], 0x9E588F79UL, "USB4NET UUID");
    CHECK_EQ(b[15], 0UL, "type Login");
    CHECK_EQ(b[16], 7UL, "Request ID");
    CHECK_EQ(b[17], 1UL, "revision");
    CHECK_EQ(b[18], 8UL, "Service HopID");
    CHECK_EQ(TbtXdpDecode(7, b, n, &k), TBT_XDP_K_NET,
             "a Login on PDF 7 is still USB4NET (record 35 section 2.4)");
    CHECK(k.Type == TBT_NET_LOGIN && k.ServiceHop == 8 && k.RequestId == 7,
          "fields");
    n = TbtNetBuildLoginResponse(b, &rt, &a, &c, 7, 0, mac);
    CHECK_EQ(n, 25UL, "Login Response: DW 1 to 25");
    CHECK_EQ(b[18], 0x02112233UL, "MAC Hi: the most significant 4 bytes");
    CHECK_EQ(b[19], 0x4455UL, "MAC Lo");
    CHECK_EQ(TbtXdpDecode(7, b, n, &k), TBT_XDP_K_NET, "decodes");
    for (i = 0; i < 6; i++) {
        CHECK_EQ(k.Mac[i], mac[i], "MAC byte");
    }
    CHECK_EQ(TbtNetBuildLogout(b, &rt, &a, &c, 9), 17UL, "Logout");
    CHECK_EQ(TbtNetBuildLogoutResponse(b, &rt, &a, &c, 9, 0), 18UL,
             "Logout Response");

    /* Another protocol's UUID: left for the caller (IDS chapter 5). */
    n = TbtXdpBuildUuidRequest(b, &rt, 0, TBT_XDP_UUID_REQ);
    b[3] ^= 1;
    CHECK_EQ(TbtXdpDecode(6, b, n, &k), TBT_XDP_K_OTHER, "another protocol");
}

static void test_mac_hop(void)
{
    UCHAR m[6];
    ULONG used[4];

    /* UUID 0x0000000000000010: deviceID 1, the swaps 0. */
    TbtNetMacFromUuid(0x10UL, 0, 0, 1, m);
    CHECK_EQ(m[5], 0x11UL, "byte 5: deviceID << 4, port 1");
    CHECK_EQ(m[4], 0UL, "byte 4");
    CHECK_EQ(m[0], 0x02UL, "byte 0: locally administered, unicast");
    /* UUID high 0x8000 0100: authority bit 15 -> swapped to bit 0;
     * project bit 8 -> swapped to bit 7, whose & 0xFC is 0x80. */
    TbtNetMacFromUuid(0, 0x80000100UL, 0, 0, m);
    CHECK_EQ(m[2], 0x01UL, "byte 2: authority bits 1:0");
    CHECK_EQ(m[0], (0x02UL ^ 0x80UL), "byte 0: project's high byte XORed");
    CHECK_EQ(m[1], 0UL, "byte 1");

    memset(used, 0, sizeof(used));
    CHECK_EQ(TbtNetChooseTxHop(TRUE, 31, 63, used), 8UL, "8 on an empty link");
    used[0] = 1UL << 8;
    CHECK_EQ(TbtNetChooseTxHop(TRUE, 31, 63, used), 9UL, "the next free");
    CHECK_EQ(TbtNetChooseTxHop(TRUE, 8, 63, used), 0UL, "none within maxhopid");
    CHECK_EQ(TbtNetChooseTxHop(FALSE, 0, 63, used), 15UL,
             "no maxhopid: exactly 15 (IDS 6.3.1.1.1)");
    used[0] = 1UL << 15;
    CHECK_EQ(TbtNetChooseTxHop(FALSE, 0, 63, used), 0UL, "15 taken: none");
    CHECK_EQ(TbtNetChooseTxHop(FALSE, 0, 14, NULL), 0UL, "15 above the lane");
}

/* ------------------------------------------------------------------ */
/* Two engines over a modelled link                                   */
/* ------------------------------------------------------------------ */

#define QMAX 64

typedef struct PKTQ {
    ULONG Pdf[QMAX];
    ULONG Words[QMAX];
    ULONG Dw[QMAX][TBT_XDP_MAX_WORDS];
    ULONG Count;
} PKTQ;

typedef struct SIDE {
    TBT_XDP_PEER Peer;
    PKTQ Out;           /* what this side sent */
    int Lanes;          /* the negotiated width the link reports */
    int BondCalls;
    int Lane1Off;
    ULONG SetTarget;
    int Ups;
    int Downs;
    BOOLEAN DownGone;
    ULONG UpTx;
    ULONG UpRx;
    BOOLEAN Drop;       /* lose what this side sends */
    ULONG DropOnce;     /* lose the first discovery packet of this type */
    BOOLEAN SendFail;   /* the ring refuses every send */
    BOOLEAN Dual;       /* report a dual Supported Link Width */
} SIDE;

static ULONG gNow;

static ULONG fNow(PVOID c) { (void)c; return gNow; }

static LONG fSend(PVOID c, ULONG pdf, const ULONG *dw, ULONG words)
{
    SIDE *s = (SIDE *)c;
    ULONG i;

    if (s->SendFail) {
        return TBT_E_RING_FULL;
    }
    if (s->DropOnce != 0 && words > TBT_XDP_W_TYPE &&
        dw[TBT_XDP_W_PROTO] == TbtXdpProtocolUuid.Dw[0] &&
        dw[TBT_XDP_W_TYPE] == s->DropOnce) {
        s->DropOnce = 0;
        return TBT_OK;
    }
    if (s->Drop || s->Out.Count >= QMAX) {
        return TBT_OK;
    }
    s->Out.Pdf[s->Out.Count] = pdf;
    s->Out.Words[s->Out.Count] = words;
    for (i = 0; i < words; i++) {
        s->Out.Dw[s->Out.Count][i] = dw[i];
    }
    s->Out.Count++;
    return TBT_OK;
}

static LONG fLinkRead(PVOID c, PULONG slw, PULONG tlw, PULONG lanes)
{
    SIDE *s = (SIDE *)c;

    *slw = s->Dual ? 3UL : 1UL;
    *tlw = s->SetTarget != 0 ? s->SetTarget : 1UL;
    *lanes = (ULONG)s->Lanes;
    return TBT_OK;
}

static LONG fSetTarget(PVOID c, ULONG tlw)
{
    ((SIDE *)c)->SetTarget = tlw;
    return TBT_OK;
}

static LONG fBond(PVOID c)
{
    ((SIDE *)c)->BondCalls++;
    return TBT_OK;
}

static VOID fLane1Off(PVOID c)
{
    ((SIDE *)c)->Lane1Off++;
}

static LONG fUp(PVOID c, ULONG tx, ULONG rx)
{
    SIDE *s = (SIDE *)c;

    s->Ups++;
    s->UpTx = tx;
    s->UpRx = rx;
    return TBT_OK;
}

static VOID fDown(PVOID c, BOOLEAN gone)
{
    SIDE *s = (SIDE *)c;

    s->Downs++;
    s->DownGone = gone;
}

static void sideInit(SIDE *s, int idx, BOOLEAN firmware, BOOLEAN bond,
                     const TBT_UUID *self, const TBT_UUID *remote)
{
    TBT_XDP_OPS ops;
    TBT_XDP_LOCAL l;
    TBT_ROUTE rt;
    UCHAR mac[6];

    memset(s, 0, sizeof(*s));
    s->Dual = TRUE;
    memset(&ops, 0, sizeof(ops));
    ops.Context = s;
    ops.Send = fSend;
    ops.NowMs = fNow;
    if (!firmware) {
        ops.LinkRead = fLinkRead;
        ops.LinkSetTarget = fSetTarget;
        ops.LinkBond = fBond;
        ops.LinkLane1Off = fLane1Off;
    }
    ops.ServiceUp = fUp;
    ops.ServiceDown = fDown;
    memset(&l, 0, sizeof(l));
    l.VendorId = 0;
    l.VendorText = "xHCI98";
    l.DeviceId = 1;
    l.DeviceRevision = 1;
    l.MaxHopId = 31;
    l.Network = TRUE;
    l.Prtcstns = TBT_NET_STNS_E2E | TBT_NET_STNS_FRAME_ID;
    rt.Hi = 0;
    rt.Lo = 1;
    memset(mac, idx, sizeof(mac));
    mac[0] = 0x02;
    TbtXdpPeerInit(&s->Peer, &ops, &rt, self, remote, firmware, bond, &l,
                   mac, 63, NULL);
}

/* Deliver what each side sent to the other, `rounds` times, ticking both
 * and advancing the clock by `stepMs` each round. */
static void run(SIDE *a, SIDE *b, int rounds, ULONG stepMs)
{
    int r;
    ULONG i;
    PKTQ qa;
    PKTQ qb;

    for (r = 0; r < rounds; r++) {
        TbtXdpPeerTick(&a->Peer);
        TbtXdpPeerTick(&b->Peer);
        qa = a->Out;
        qb = b->Out;
        a->Out.Count = 0;
        b->Out.Count = 0;
        for (i = 0; i < qa.Count; i++) {
            TbtXdpPeerOnPacket(&b->Peer, qa.Pdf[i], qa.Dw[i], qa.Words[i]);
        }
        for (i = 0; i < qb.Count; i++) {
            TbtXdpPeerOnPacket(&a->Peer, qb.Pdf[i], qb.Dw[i], qb.Words[i]);
        }
        if (a->BondCalls != 0) {
            a->Lanes = 2;
            b->Lanes = 2;
        }
        gNow += stepMs;
    }
}

static SIDE gA;
static SIDE gB;

static void uuids(TBT_UUID *lo, TBT_UUID *hi)
{
    memset(lo, 0, sizeof(*lo));
    memset(hi, 0, sizeof(*hi));
    lo->Dw[0] = 0x00000011UL;   /* canonical byte 0 is 0x11 */
    hi->Dw[0] = 0x00000022UL;   /* 0x22: the higher */
    lo->Dw[2] = hi->Dw[2] = 0xFFFFFFFFUL;
}

static void test_software_pair(void)
{
    TBT_UUID lo;
    TBT_UUID hi;

    uuids(&lo, &hi);
    gNow = 1000;
    sideInit(&gA, 1, FALSE, TRUE, &lo, NULL);
    sideInit(&gB, 2, FALSE, TRUE, &hi, NULL);
    gA.Lanes = gB.Lanes = 1;
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 200, 50);

    CHECK_EQ(gA.Peer.State, TBT_XDP_S_READY, "A ready");
    CHECK_EQ(gB.Peer.State, TBT_XDP_S_READY, "B ready");
    CHECK(TbtUuidEqual(&gA.Peer.Remote, &hi), "A learnt B's UUID");
    CHECK(gA.BondCalls == 1 && gB.BondCalls == 0,
          "the lower UUID bonds (IDS 3.4.1)");
    CHECK_EQ(gB.SetTarget, 3UL, "the higher set TLW x2 on the request");
    CHECK_EQ(gA.Peer.Counters.Bonded + gB.Peer.Counters.Bonded, 2UL,
             "both saw the width");
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "A's service up");
    CHECK_EQ(gB.Peer.NetState, TBT_NET_S_UP, "B's service up");
    CHECK(gA.Ups == 1 && gB.Ups == 1, "paths built once each");
    CHECK(gA.UpTx == 8 && gB.UpRx == 8, "A sends on 8, B receives on 8");
    CHECK(gB.UpTx == 8 && gA.UpRx == 8, "and the other way");
    CHECK(gA.Peer.PeerMac[5] == 2 && gB.Peer.PeerMac[5] == 1,
          "each learnt the other's MAC");

    /* A closes its service: Logout, B tears down and logs out in turn. */
    TbtXdpPeerSetNetWanted(&gA.Peer, FALSE, 0);
    run(&gA, &gB, 60, 50);
    CHECK(gA.Downs == 1 && gB.Downs == 1, "both down once");
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_OFF, "A off");
    CHECK_EQ(gB.Peer.NetState, TBT_NET_S_OFF, "B off");
    CHECK(!gB.Peer.RemoteInfo.Network, "B re-read A's block: no directory");
    CHECK_EQ(gB.Peer.State, TBT_XDP_S_READY, "B still ready");

    /* A opens again: the directory comes back, both log in again. */
    TbtXdpPeerSetNetWanted(&gA.Peer, TRUE, 3);
    run(&gA, &gB, 100, 50);
    CHECK(gA.Peer.NetState == TBT_NET_S_UP && gB.Peer.NetState == TBT_NET_S_UP,
          "up again");
    CHECK(gA.Ups == 2 && gB.Ups == 2, "built again");

    /* The cable pulled: no Logout, down with peerGone. */
    TbtXdpPeerEnd(&gA.Peer, TRUE);
    CHECK(gA.Downs == 2 && gA.DownGone, "A down, gone");
    CHECK_EQ(gA.Peer.State, TBT_XDP_S_IDLE, "A idle");
}

static void test_firmware_pair(void)
{
    TBT_UUID lo;
    TBT_UUID hi;

    uuids(&lo, &hi);
    gNow = 5000;
    sideInit(&gA, 1, TRUE, FALSE, &lo, &hi);
    sideInit(&gB, 2, TRUE, FALSE, &hi, &lo);
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 100, 50);
    CHECK(gA.Peer.Counters.Sent != 0, "A sent");
    CHECK_EQ(gA.Peer.State, TBT_XDP_S_READY, "firmware CM: straight to PROPS, READY");
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "service up");
    CHECK(gA.BondCalls == 0 && gA.Lane1Off == 0, "no link work");
}

static void test_single_lane(void)
{
    TBT_UUID lo;
    TBT_UUID hi;

    uuids(&lo, &hi);
    gNow = 9000;
    sideInit(&gA, 1, FALSE, TRUE, &lo, NULL);
    sideInit(&gB, 2, FALSE, FALSE, &hi, NULL);
    gB.Dual = FALSE;        /* the peer is single lane, so cannot bond */
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 200, 50);
    CHECK_EQ(gA.BondCalls, 0, "no bonding with an x1 peer");
    CHECK(gA.Lane1Off == 1, "A's lane 1 off");
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "still up");
}

static void test_no_answer(void)
{
    TBT_UUID lo;
    TBT_UUID hi;
    ULONG sent;

    uuids(&lo, &hi);
    gNow = 20000;
    sideInit(&gA, 1, FALSE, FALSE, &lo, NULL);
    sideInit(&gB, 2, FALSE, FALSE, &hi, NULL);
    gB.Drop = TRUE;
    TbtXdpPeerStart(&gA.Peer);
    run(&gA, &gB, 400, 100);
    sent = gA.Peer.Counters.Sent;
    CHECK_EQ(gA.Peer.State, TBT_XDP_S_ERROR, "no answer: ERROR");
    CHECK_EQ(sent, 11UL, "the first and ten resends (IDS 3.1)");
    /* A UUID Request from the peer restarts it (IDS 3.1). */
    gB.Drop = FALSE;
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 200, 50);
    CHECK_EQ(gA.Peer.State, TBT_XDP_S_READY, "restarted by the peer");
}

/* ------------------------------------------------------------------ */
/* Packets shaped as thunderbolt-net shapes them                      */
/* ------------------------------------------------------------------ */

static void test_linux_shapes(void)
{
    TBT_UUID lo;
    TBT_UUID hi;
    ULONG b[TBT_XDP_MAX_WORDS];
    ULONG n;
    TBT_XDP_PKT k;
    ULONG i;
    BOOLEAN sawResp = FALSE;
    int downs;

    uuids(&lo, &hi);
    gNow = 30000;
    sideInit(&gA, 1, TRUE, FALSE, &lo, &hi);
    sideInit(&gB, 2, TRUE, FALSE, &hi, &lo);
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    gA.Peer.NetWanted = TRUE;
    /* Get A to READY with B's block, but stop B from logging in itself. */
    TbtXdpPeerSetNetWanted(&gB.Peer, TRUE, 7);
    run(&gA, &gB, 10, 50);
    gB.Drop = TRUE;
    run(&gA, &gB, 1, 50);
    gA.Out.Count = 0;

    /* Linux's Login: PDF 7, its own UUID first. */
    n = TbtNetBuildLogin(b, &gA.Peer.Route, &hi, &lo, 1, 9);
    CHECK(TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_RESP, b, n),
          "a PDF 7 Login is taken");
    for (i = 0; i < gA.Out.Count; i++) {
        if (TbtXdpDecode(gA.Out.Pdf[i], gA.Out.Dw[i], gA.Out.Words[i], &k) ==
                TBT_XDP_K_NET && k.Type == TBT_NET_LOGIN_RESP) {
            sawResp = TRUE;
            CHECK_EQ(k.RequestId, 1UL, "its id copied");
            CHECK_EQ(k.NetStatus, 0UL, "success");
        }
    }
    CHECK(sawResp, "answered");
    CHECK(gA.Peer.LoginReceived && gA.Peer.RxHop == 9, "A will receive on 9");

    /* A's own Login out; Linux answers with its UUID first. */
    run(&gA, &gB, 1, 50);
    CHECK(gA.Peer.Net.Active, "A's Login out");
    n = TbtNetBuildLoginResponse(b, &gA.Peer.Route, &hi, &lo,
                                 gA.Peer.Net.LastId, 0, gA.Peer.Mac);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_RESP, b, n);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "up with Linux's field order");
    CHECK(gA.UpRx == 9, "receives on Linux's HopID");

    /* A logs out; Linux answers with an id of its own. */
    downs = gA.Downs;
    TbtXdpPeerSetNetWanted(&gA.Peer, FALSE, 0);
    run(&gA, &gB, 1, 50);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_LOGOUT, "A logging out");
    n = TbtNetBuildLogoutResponse(b, &gA.Peer.Route, &hi, &lo, 777, 0);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_RESP, b, n);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_OFF, "taken despite its id");
    CHECK_EQ(gA.Downs, downs + 1, "torn down");

    /* A login with an out-of-range HopID is refused. */
    TbtXdpPeerSetNetWanted(&gA.Peer, TRUE, 3);
    gA.Out.Count = 0;
    n = TbtNetBuildLogin(b, &gA.Peer.Route, &hi, &lo, 2, 99);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_REQ, b, n);
    CHECK_EQ(gA.Peer.Counters.LoginsRefused, 1UL, "HopID 99 above maxhopid 31");
    CHECK(!gA.Peer.LoginReceived, "not taken");
}


/* Record 35 section 11.1's harder cases (S1's review). */
static void test_lost_and_failed(void)
{
    TBT_UUID lo;
    TBT_UUID hi;
    int downs;
    ULONG b[TBT_XDP_MAX_WORDS];
    ULONG n;

    uuids(&lo, &hi);

    /* The higher side's Change Response lost once: the resend is answered
     * again and bonding still completes. */
    gNow = 40000;
    sideInit(&gA, 1, FALSE, TRUE, &lo, NULL);
    sideInit(&gB, 2, FALSE, TRUE, &hi, NULL);
    gB.DropOnce = TBT_XDP_LINK_CHANGE_RESP;
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 200, 50);
    CHECK_EQ(gA.BondCalls, 1, "bonded despite the lost Change Response");
    CHECK_EQ(gA.Lane1Off + gB.Lane1Off, 0, "no lane disabled");
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "and up");

    /* The cable pulled while our Logout is out: the paths go once. */
    TbtXdpPeerSetNetWanted(&gA.Peer, FALSE, 0);
    TbtXdpPeerTick(&gA.Peer);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_LOGOUT, "logging out");
    TbtXdpPeerSetNetWanted(&gA.Peer, FALSE, 0);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_LOGOUT, "a second close changes nothing");
    downs = gA.Downs;
    TbtXdpPeerEnd(&gA.Peer, TRUE);
    CHECK_EQ(gA.Downs, downs + 1, "ServiceDown once on the unplug");
    CHECK(!gA.Peer.PathsUp, "no paths left");
    TbtXdpPeerEnd(&gA.Peer, TRUE);
    CHECK_EQ(gA.Downs, downs + 1, "and not again");

    /* A Logout never answered: ten resends, then the paths go. */
    gNow = 60000;
    sideInit(&gA, 1, TRUE, FALSE, &lo, &hi);
    sideInit(&gB, 2, TRUE, FALSE, &hi, &lo);
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 60, 50);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "up");
    gB.Drop = TRUE;
    TbtXdpPeerSetNetWanted(&gA.Peer, FALSE, 0);
    gB.Drop = FALSE;
    gB.Peer.NetWanted = FALSE;
    {
        int r;

        for (r = 0; r < 40; r++) {
            gA.Out.Count = 0;
            TbtXdpPeerTick(&gA.Peer);
            gNow += 500;
        }
    }
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_OFF, "off after the resends");
    CHECK(!gA.Peer.PathsUp, "paths down");
    CHECK_EQ(gA.Peer.Counters.LogoutsSent, 11UL, "the first and ten more");

    /* The Login Response cannot be sent: that half is not done. */
    gNow = 90000;
    sideInit(&gA, 1, TRUE, FALSE, &lo, &hi);
    sideInit(&gB, 2, TRUE, FALSE, &hi, &lo);
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    gB.Drop = TRUE;
    run(&gA, &gB, 1, 50);
    gB.Drop = FALSE;
    run(&gA, &gB, 6, 50);
    gA.SendFail = TRUE;
    n = TbtNetBuildLogin(b, &gA.Peer.Route, &hi, &lo, 50, 9);
    gA.Peer.LoginReceived = FALSE;
    gA.Peer.NetState = TBT_NET_S_LOGIN;
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_REQ, b, n);
    CHECK(!gA.Peer.LoginReceived, "an unsent success is not a login");
    gA.SendFail = FALSE;

    /* A refusal switches the resends to the slow rate. */
    gNow = 120000;
    sideInit(&gA, 1, TRUE, FALSE, &lo, &hi);
    sideInit(&gB, 2, TRUE, FALSE, &hi, &lo);
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    gB.Peer.NetWanted = FALSE;  /* advertises, but refuses logins */
    run(&gA, &gB, 20, 50);
    CHECK(gA.Peer.Counters.LoginFailures != 0, "refused");
    CHECK(gA.Peer.LoginSlow, "the slow rate");
    CHECK(gA.Peer.Net.DueMs - gNow <= TBT_NET_LOGIN_SLOW_MS, "next within a second");
}

/* A Login accepted during our Logout while the peer's block is being read
 * again: kept, and the service comes up once the read ends. */
static void test_pending_login(void)
{
    TBT_UUID lo;
    TBT_UUID hi;
    ULONG b[TBT_XDP_MAX_WORDS];
    ULONG n;

    uuids(&lo, &hi);
    gNow = 150000;
    sideInit(&gA, 1, TRUE, FALSE, &lo, &hi);
    sideInit(&gB, 2, TRUE, FALSE, &hi, &lo);
    TbtXdpPeerStart(&gA.Peer);
    TbtXdpPeerStart(&gB.Peer);
    run(&gA, &gB, 60, 50);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "up");
    gB.Drop = TRUE;
    /* The peer logs out: A answers and logs out in turn. */
    n = TbtNetBuildLogout(b, &gA.Peer.Route, &hi, &lo, 70);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_RESP, b, n);
    TbtXdpPeerTick(&gA.Peer);
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_LOGOUT, "A logging out");
    /* The peer's block changed: A reads it again. */
    n = TbtXdpBuildChanged(b, &gA.Peer.Route, 0, &hi);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_REQ, b, n);
    CHECK_EQ(gA.Peer.State, TBT_XDP_S_PROPS, "re-reading");
    /* The peer logs in again: answered, kept. */
    n = TbtNetBuildLogin(b, &gA.Peer.Route, &hi, &lo, 71, 10);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_RESP, b, n);
    CHECK(gA.Peer.PendingLogin, "kept as pending");
    /* Our Logout answered before the read ends. */
    n = TbtNetBuildLogoutResponse(b, &gA.Peer.Route, &hi, &lo, 900, 0);
    TbtXdpPeerOnPacket(&gA.Peer, TBT_PDF_XDOMAIN_RESP, b, n);
    CHECK(gA.Peer.LoginReceived && gA.Peer.RxHop == 10, "that half stands");
    gB.Drop = FALSE;
    run(&gA, &gB, 60, 50);
    CHECK_EQ(gA.Peer.State, TBT_XDP_S_READY, "read again");
    CHECK_EQ(gA.Peer.NetState, TBT_NET_S_UP, "up again");
    CHECK(gA.Ups >= 2, "built again");
}

/* Two network directories: each candidate's own fields count. */
static void test_sibling_dirs(void)
{
    ULONG b[50];
    TBT_XDP_REMOTE r;
    ULONG i;

    memset(b, 0, sizeof(b));
    b[0] = TBT_XDP_ROOT_MAGIC;
    b[1] = 16;
    b[2] = 0x76656e64UL; b[3] = 0x6f726964UL; b[4] = 0x76000001UL; b[5] = 1;
    b[6] = 0x64657669UL; b[7] = 0x63656964UL; b[8] = 0x76000001UL; b[9] = 2;
    /* network at 18 (prtcid 1 only), network at 26 (prtcvers 1 only) */
    b[10] = 0x6e657477UL; b[11] = 0x6f726b00UL; b[12] = 0x44000008UL; b[13] = 18;
    b[14] = 0x6e657477UL; b[15] = 0x6f726b00UL; b[16] = 0x44000008UL; b[17] = 26;
    for (i = 18; i < 22; i++) {
        b[i] = 0x11111111UL;
    }
    b[22] = 0x70727463UL; b[23] = 0x69640000UL; b[24] = 0x76000001UL; b[25] = 1;
    for (i = 26; i < 30; i++) {
        b[i] = 0x22222222UL;
    }
    b[30] = 0x70727463UL; b[31] = 0x76657273UL; b[32] = 0x76000001UL; b[33] = 1;
    CHECK_EQ(TbtXdpPropsParse(b, 34, &r), TBT_OK, "parses");
    CHECK(!r.Network, "no candidate has both prtcid 1 and prtcvers 1");
}

int main(void)
{
    test_uuid();
    test_sample_block();
    test_nesting();
    test_build_block();
    test_packets();
    test_mac_hop();
    test_software_pair();
    test_firmware_pair();
    test_single_lane();
    test_no_answer();
    test_linux_shapes();
    test_lost_and_failed();
    test_sibling_dirs();
    test_pending_login();
    printf("test_tbt_xdp: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
