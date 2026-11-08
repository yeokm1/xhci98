/*
 * test_net_frame.c - the USB4NET framing core (src/net/net_frame.c;
 * design record 35 sections 7.2 to 7.4 and 11.1, task 45.59 step S6):
 * the transmit split at 14, 60, 1514, 4084, 4085 and 65536 bytes, the
 * Frame ID rule (one id a packet, advanced only towards a peer with
 * prtcstns bit 1, wrapping at 0xFFFF), every receive drop, reassembly,
 * a short frame whose SOF PDF reads 0, and the receive segmentation of a
 * 64 KiB TCP segment over IPv4 and IPv6 with its refusals.
 */

#include <stdio.h>
#include <string.h>
#include "../src/net/net_frame.h"
#include "test_harness.h"

#define BIG 70000

static UCHAR pkt[BIG];
static UCHAR pkt2[BIG];
static UCHAR asm1[BIG];
static UCHAR wire[NET_FRAMES_MAX + 4][NET_BUF_LEN];
static ULONG wireLen[NET_FRAMES_MAX + 4];
static UCHAR piece[BIG];
static UCHAR joined[BIG];

static ULONG get16be(const UCHAR *p)
{
    return ((ULONG)p[0] << 8) | p[1];
}

static ULONG get32be(const UCHAR *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
           ((ULONG)p[2] << 8) | p[3];
}

static ULONG get32le(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static ULONG get16le(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static void put16be(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v >> 8);
    p[1] = (UCHAR)v;
}

static void put32be(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v >> 24);
    p[1] = (UCHAR)(v >> 16);
    p[2] = (UCHAR)(v >> 8);
    p[3] = (UCHAR)v;
}

/* A reference one's-complement sum, written apart from the core's:
 * every word added into a 32-bit total first, folded once at the end. */
static ULONG refSum(const UCHAR *d, ULONG n, ULONG start)
{
    ULONG s = start;
    ULONG i;

    for (i = 0; i + 1 < n; i += 2) {
        s += ((ULONG)d[i] << 8) | d[i + 1];
    }
    if (n & 1) {
        s += (ULONG)d[n - 1] << 8;
    }
    while (s >> 16) {
        s = (s & 0xFFFF) + (s >> 16);
    }
    return s;
}

/* The TCP checksum of the segment at frame+14+iplen, as a receiver
 * checks it: the sum with the field in place folds to 0xFFFF. */
static int tcpGood(const UCHAR *f, ULONG len, int ip6)
{
    const UCHAR *ip = f + 14;
    ULONG iplen = ip6 ? 40 : ((ULONG)(ip[0] & 15) * 4);
    ULONG seg = len - 14 - iplen;
    ULONG s;

    if (ip6) {
        s = refSum(ip + 8, 32, 0) + (seg >> 16) + (seg & 0xFFFF) + 6;
    } else {
        s = refSum(ip + 12, 8, 0) + seg + 6;
    }
    s = refSum(ip + iplen, seg, s);
    return s == 0xFFFF;
}

static int ip4Good(const UCHAR *f)
{
    const UCHAR *ip = f + 14;

    return refSum(ip, (ULONG)(ip[0] & 15) * 4, 0) == 0xFFFF;
}

static void fillPattern(UCHAR *p, ULONG n, ULONG seed)
{
    ULONG i;
    ULONG x = seed * 2654435761UL + 1;

    for (i = 0; i < n; i++) {
        x = x * 1103515245UL + 12345UL;
        p[i] = (UCHAR)(x >> 16);
    }
}

/* ------------------------------------------------------------------ */

static void testSums(void)
{
    static const UCHAR rfc1071[8] = {
        0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7
    };
    static const UCHAR ip4hdr[20] = {
        0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
        0xB8, 0x61, 0xC0, 0xA8, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0xC7
    };
    static UCHAR ones[65536];

    CHECK_EQ(NetSum16(rfc1071, 8, 0), 0xDDF2UL, "RFC 1071 3's sample sum");
    CHECK_EQ(NetSum16(ip4hdr, 20, 0), 0xFFFFUL,
             "an IPv4 header with its checksum sums to 0xFFFF");
    CHECK_EQ(NetSum16(rfc1071, 3, 0), refSum(rfc1071, 3, 0),
             "odd length pads the last byte as the high byte");
    memset(ones, 0xFF, sizeof(ones));
    CHECK_EQ(NetSum16(ones, sizeof(ones), 0xFFFF), 0xFFFFUL,
             "64 KiB of 0xFF does not overflow the accumulator");
    CHECK_EQ(NetSumFold(0x0001FFFFUL), 0x0001UL, "fold carries twice");
}

static void testTxCounts(void)
{
    static const ULONG lens[6] = { 14, 60, 1514, 4084, 4085, 65536 };
    static const ULONG want[6] = { 1, 1, 1, 1, 2, 17 };
    ULONG i;
    ULONG why;

    for (i = 0; i < 6; i++) {
        CHECK_EQ(NetTxFrames(lens[i], NET_PACKET_MAX, 0x7, &why), want[i],
                 "frames a length takes");
        CHECK_EQ(why, 0UL, "no refusal");
    }
    CHECK_EQ(NetTxFrameData(4085, 0), 4084UL, "4085: first frame full");
    CHECK_EQ(NetTxFrameData(4085, 1), 1UL, "4085: second frame one byte");
    CHECK_EQ(NetTxFrameData(65536, 16), 65536UL - 16 * 4084,
             "65536: last frame the rest");
    CHECK_EQ(NetTxFrameData(65536, 17), 0UL, "past the last frame");

    CHECK_EQ(NetTxFrames(13, 1514, 0x7, &why), 0UL, "13 bytes refused");
    CHECK_EQ(why, NET_TXDROP_SHORT, "as short");
    CHECK_EQ(NetTxFrames(1515, 1514, 0x7, &why), 0UL,
             "past the adapter's maximum refused");
    CHECK_EQ(why, NET_TXDROP_LONG, "as long");
    CHECK_EQ(NetTxFrames(1515, 9014, 0x3, &why), 0UL,
             "past 1514 to a peer without bit 2 refused");
    CHECK_EQ(why, NET_TXDROP_PEER, "as the peer's");
    CHECK_EQ(NetTxFrames(1514, 9014, 0x0, &why), 1UL,
             "1514 to any peer");
    CHECK_EQ(NetTxFrames(65551, 70000, 0x7, &why), 0UL,
             "past 65550 refused whatever the maximum");
}

static void testTxHeader(void)
{
    UCHAR b[16];
    ULONG n;

    memset(b, 0xEE, sizeof(b));
    n = NetTxHeader(b, 4085, 1, 2, 0x1234);
    CHECK_EQ(n, 13UL, "header plus one data byte");
    CHECK_EQ(get32le(b), 1UL, "Frame Size the frame's data bytes");
    CHECK_EQ(get16le(b + 4), 1UL, "Frame Index");
    CHECK_EQ(get16le(b + 6), 0x1234UL, "Frame ID");
    CHECK_EQ(get32le(b + 8), 2UL, "Frame Count");
    CHECK_EQ(b[12], 0xEE, "nothing written past the header");
    CHECK_EQ(b[0], 0x01, "little-endian: low byte first");
}

static void testFrameId(void)
{
    NET_TX tx;
    ULONG i;

    NetTxInit(&tx);
    CHECK_EQ(NetTxPacketId(&tx, 0x7), 0UL, "starts at 0");
    CHECK_EQ(NetTxPacketId(&tx, 0x7), 1UL, "advances once a packet");
    CHECK_EQ(NetTxPacketId(&tx, 0x5), 0UL, "bit 1 clear: 0");
    CHECK_EQ(NetTxPacketId(&tx, 0x7), 2UL, "and not advanced by it");
    tx.FrameId = 0xFFFE;
    CHECK_EQ(NetTxPacketId(&tx, 0x2), 0xFFFEUL, "0xFFFE");
    CHECK_EQ(NetTxPacketId(&tx, 0x2), 0xFFFFUL, "0xFFFF");
    CHECK_EQ(NetTxPacketId(&tx, 0x2), 0UL, "wraps to 0");
    NetTxInit(&tx);
    for (i = 0; i < 70000UL; i++) {
        (void)NetTxPacketId(&tx, 0x2);
    }
    CHECK_EQ(tx.FrameId, 70000UL & 0xFFFFUL, "counter stays 16 bits");
}

/* Frames one Ethernet frame into wire[] the way the miniport does. */
static ULONG frameOut(const UCHAR *p, ULONG len, ULONG id)
{
    ULONG why;
    ULONG m = NetTxFrames(len, NET_PACKET_MAX, 0x7, &why);
    ULONG k;

    for (k = 0; k < m; k++) {
        wireLen[k] = NetTxHeader(wire[k], len, k, m, id);
        memcpy(wire[k] + NET_HDR_LEN, p + k * NET_FRAME_DATA,
               NetTxFrameData(len, k));
    }
    return m;
}

static void testRoundTrip(void)
{
    static const ULONG lens[6] = { 14, 60, 1514, 4084, 4085, 65536 };
    NET_RX rx;
    ULONG i;
    ULONG k;
    ULONG m;
    ULONG r;
    ULONG why;
    const UCHAR *out;
    ULONG outLen;

    NetRxInit(&rx, asm1, NET_PACKET_MAX);
    for (i = 0; i < 6; i++) {
        fillPattern(pkt, lens[i], i);
        m = frameOut(pkt, lens[i], 7 + i);
        r = NET_RX_HELD;
        for (k = 0; k < m; k++) {
            r = NetRxFrame(&rx, wire[k], wireLen[k], 0, &out, &outLen,
                           &why);
            if (k + 1 < m) {
                CHECK_EQ(r, NET_RX_HELD, "held until the last frame");
            }
        }
        CHECK_EQ(r, NET_RX_PACKET, "complete at the last frame");
        CHECK_EQ(outLen, lens[i], "the length back");
        CHECK(memcmp(out, pkt, lens[i]) == 0, "the bytes back");
    }
    for (i = 0; i < NET_RXDROP_REASONS; i++) {
        CHECK_EQ(rx.Drops[i], 0UL, "no drop on the round trip");
    }

    /* A short frame of one tunnelled packet reads SOF PDF 0 and EOF PDF
     * 2; neither PDF is checked. */
    fillPattern(pkt, 60, 99);
    (void)frameOut(pkt, 60, 0);
    r = NetRxFrame(&rx, wire[0], wireLen[0], 2UL << NET_RXF_EOF_SHIFT,
                   &out, &outLen, &why);
    CHECK_EQ(r, NET_RX_PACKET, "SOF PDF 0 accepted");
    CHECK(out == wire[0] + NET_HDR_LEN, "one frame: no copy");
    r = NetRxFrame(&rx, wire[0], wireLen[0],
                   (1UL << NET_RXF_SOF_SHIFT) | (2UL << NET_RXF_EOF_SHIFT),
                   &out, &outLen, &why);
    CHECK_EQ(r, NET_RX_PACKET, "SOF PDF 1 accepted");

    /* A descriptor longer than the header's Frame Size: the size wins. */
    r = NetRxFrame(&rx, wire[0], wireLen[0] + 100, 0, &out, &outLen, &why);
    CHECK_EQ(r, NET_RX_PACKET, "trailing bytes ignored");
    CHECK_EQ(outLen, 60UL, "Frame Size bytes passed up");
}

static ULONG rxOne(NET_RX *rx, const UCHAR *f, ULONG len, ULONG flags,
                   ULONG *why)
{
    const UCHAR *out;
    ULONG outLen;

    return NetRxFrame(rx, f, len, flags, &out, &outLen, why);
}

static void hdr(UCHAR *b, ULONG size, ULONG index, ULONG id, ULONG count)
{
    b[0] = (UCHAR)size; b[1] = (UCHAR)(size >> 8);
    b[2] = (UCHAR)(size >> 16); b[3] = (UCHAR)(size >> 24);
    b[4] = (UCHAR)index; b[5] = (UCHAR)(index >> 8);
    b[6] = (UCHAR)id; b[7] = (UCHAR)(id >> 8);
    b[8] = (UCHAR)count; b[9] = (UCHAR)(count >> 8);
    b[10] = (UCHAR)(count >> 16); b[11] = (UCHAR)(count >> 24);
}

static void testRxDrops(void)
{
    NET_RX rx;
    UCHAR f[NET_BUF_LEN];
    ULONG why;
    ULONG k;

    NetRxInit(&rx, asm1, NET_PACKET_MAX);
    memset(f, 0x55, sizeof(f));

    hdr(f, 100, 0, 0, 1);
    CHECK_EQ(rxOne(&rx, f, 112, NET_RXF_CRC_ERROR, &why), NET_RX_DROPPED,
             "CRC Error dropped");
    CHECK_EQ(why, NET_RXDROP_CRC, "as CRC");
    CHECK_EQ(rxOne(&rx, f, 112, NET_RXF_OVERFLOW, &why), NET_RX_DROPPED,
             "Buffer Overflow dropped");
    CHECK_EQ(why, NET_RXDROP_OVERFLOW, "as overflow");
    CHECK_EQ(rxOne(&rx, f, 12, 0, &why), NET_RX_DROPPED,
             "a header and no data dropped");
    CHECK_EQ(why, NET_RXDROP_SHORT, "as short");
    CHECK_EQ(rxOne(&rx, f, 4097, 0, &why), NET_RX_DROPPED,
             "longer than a buffer dropped");

    hdr(f, 0, 0, 0, 1);
    CHECK_EQ(rxOne(&rx, f, 112, 0, &why), NET_RX_DROPPED, "size 0");
    CHECK_EQ(why, NET_RXDROP_SIZE, "as size");
    hdr(f, 101, 0, 0, 1);
    CHECK_EQ(rxOne(&rx, f, 112, 0, &why), NET_RX_DROPPED,
             "size past the data");
    CHECK_EQ(why, NET_RXDROP_SIZE, "as size");
    hdr(f, 100, 0, 0, 0);
    CHECK_EQ(rxOne(&rx, f, 112, 0, &why), NET_RX_DROPPED, "count 0");
    CHECK_EQ(why, NET_RXDROP_COUNT, "as count");
    hdr(f, 100, 0, 0, 18);
    CHECK_EQ(rxOne(&rx, f, 112, 0, &why), NET_RX_DROPPED, "count 18");
    CHECK_EQ(why, NET_RXDROP_COUNT, "as count");
    hdr(f, 100, 3, 0, 3);
    CHECK_EQ(rxOne(&rx, f, 112, 0, &why), NET_RX_DROPPED,
             "index not below count");
    CHECK_EQ(why, NET_RXDROP_INDEX, "as index");
    hdr(f, 13, 0, 0, 1);
    CHECK_EQ(rxOne(&rx, f, 25, 0, &why), NET_RX_DROPPED,
             "a 13-byte frame");
    CHECK_EQ(why, NET_RXDROP_RUNT, "as runt");
    hdr(f, 100, 1, 0, 2);
    CHECK_EQ(rxOne(&rx, f, 112, 0, &why), NET_RX_DROPPED,
             "a second frame with none started");
    CHECK_EQ(why, NET_RXDROP_ORPHAN, "as orphan");

    /* A gap: index 0 then 2. */
    hdr(f, 4084, 0, 5, 3);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_HELD, "start");
    hdr(f, 4084, 2, 5, 3);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_DROPPED, "gap");
    CHECK_EQ(why, NET_RXDROP_SEQUENCE, "as sequence");
    hdr(f, 4084, 2, 5, 3);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_DROPPED,
             "and the packet is gone");
    CHECK_EQ(why, NET_RXDROP_ORPHAN, "as orphan");

    /* A changed Count, then a changed Frame ID. */
    hdr(f, 4084, 0, 5, 3);
    (void)rxOne(&rx, f, 4096, 0, &why);
    hdr(f, 4084, 1, 5, 4);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_DROPPED, "count changed");
    CHECK_EQ(why, NET_RXDROP_SEQUENCE, "as sequence");
    hdr(f, 4084, 0, 5, 3);
    (void)rxOne(&rx, f, 4096, 0, &why);
    hdr(f, 4084, 1, 6, 3);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_DROPPED, "id changed");
    CHECK_EQ(why, NET_RXDROP_SEQUENCE, "as sequence");

    /* A repeated Index 0 cuts the packet in progress and starts anew. */
    k = rx.Drops[NET_RXDROP_RESTART];
    hdr(f, 4084, 0, 5, 2);
    (void)rxOne(&rx, f, 4096, 0, &why);
    hdr(f, 4084, 0, 6, 2);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_HELD, "restart taken");
    CHECK_EQ(rx.Drops[NET_RXDROP_RESTART], k + 1, "the cut one counted");
    hdr(f, 10, 1, 6, 2);
    CHECK_EQ(rxOne(&rx, f, 22, 0, &why), NET_RX_PACKET,
             "the new packet completes");
    CHECK_EQ(rx.Length, 0UL, "nothing held after");

    /* A one-frame packet arriving mid-packet also cuts it. */
    hdr(f, 4084, 0, 5, 2);
    (void)rxOne(&rx, f, 4096, 0, &why);
    hdr(f, 60, 0, 7, 1);
    CHECK_EQ(rxOne(&rx, f, 72, 0, &why), NET_RX_PACKET, "single passes");
    CHECK_EQ(rx.Drops[NET_RXDROP_RESTART], k + 2, "the cut one counted");

    /* 17 full frames sum past 65550. */
    for (k = 0; k < 17; k++) {
        hdr(f, 4084, k, 9, 17);
        why = 0;
        if (k < 16) {
            CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_HELD, "held");
        } else {
            CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_DROPPED,
                     "the 17th full frame passes 65550");
            CHECK_EQ(why, NET_RXDROP_TOOBIG, "as too big");
        }
    }

    /* A flagged frame mid-packet ends the packet: A[0], A[1] lost to a
     * CRC Error, B[0] lost to an overflow, then B[1] with the same Count
     * and Frame ID must not complete A with B's bytes. */
    k = rx.Drops[NET_RXDROP_SEQUENCE];
    hdr(f, 4084, 0, 0, 2);
    CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_HELD, "A[0]");
    hdr(f, 100, 1, 0, 2);
    CHECK_EQ(rxOne(&rx, f, 112, NET_RXF_CRC_ERROR, &why), NET_RX_DROPPED,
             "A[1] with CRC Error");
    CHECK_EQ(why, NET_RXDROP_CRC, "as CRC");
    CHECK_EQ(rx.Active, 0UL, "A abandoned");
    CHECK_EQ(rx.Drops[NET_RXDROP_SEQUENCE], k + 1, "A counted");
    hdr(f, 4084, 0, 0, 2);
    CHECK_EQ(rxOne(&rx, f, 4096, NET_RXF_OVERFLOW, &why), NET_RX_DROPPED,
             "B[0] with Buffer Overflow");
    CHECK_EQ(why, NET_RXDROP_OVERFLOW, "as overflow");
    hdr(f, 1, 1, 0, 2);
    CHECK_EQ(rxOne(&rx, f, 13, 0, &why), NET_RX_DROPPED,
             "B[1] completes nothing");
    CHECK_EQ(why, NET_RXDROP_ORPHAN, "as orphan");

    /* Every malformed frame ends the packet in progress too: A[0], a
     * malformed frame, B[0] malformed, then B[1] (same Count, ID 0). */
    {
        static const ULONG bad[4][4] = {
            /* size, index, count, length */
            { 100, 1, 2, 12 },      /* a header and no data  */
            { 0, 1, 2, 112 },       /* Frame Size 0          */
            { 100, 1, 18, 112 },    /* Frame Count 18        */
            { 100, 2, 2, 112 }      /* Index not below Count */
        };
        ULONG i;

        for (i = 0; i < 4; i++) {
            hdr(f, 4084, 0, 0, 2);
            CHECK_EQ(rxOne(&rx, f, 4096, 0, &why), NET_RX_HELD, "A[0]");
            hdr(f, bad[i][0], bad[i][1], 0, bad[i][2]);
            CHECK_EQ(rxOne(&rx, f, bad[i][3], 0, &why), NET_RX_DROPPED,
                     "a malformed frame");
            CHECK_EQ(rx.Active, 0UL, "A abandoned by it");
            hdr(f, 1, 1, 0, 2);
            CHECK_EQ(rxOne(&rx, f, 13, 0, &why), NET_RX_DROPPED,
                     "B[1] completes nothing");
            CHECK_EQ(why, NET_RXDROP_ORPHAN, "as orphan");
        }
    }

    /* A two-frame packet whose whole is below 14 bytes. */
    hdr(f, 5, 0, 1, 2);
    (void)rxOne(&rx, f, 17, 0, &why);
    hdr(f, 5, 1, 1, 2);
    CHECK_EQ(rxOne(&rx, f, 17, 0, &why), NET_RX_DROPPED, "10 bytes whole");
    CHECK_EQ(why, NET_RXDROP_RUNT, "as runt");

    NetRxReset(&rx);
    CHECK_EQ(rx.Active, 0UL, "reset leaves nothing in progress");
}

/* ------------------------------------------------------------------ */
/* Receive segmentation                                                */
/* ------------------------------------------------------------------ */

static const UCHAR macs[12] = {
    0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x02, 0x66, 0x77, 0x88, 0x99, 0xAA
};

/* A TCP segment over IPv4 (ip6 0) or IPv6, payload bytes of pattern,
 * opt bytes of TCP options, with good checksums. Returns its length. */
static ULONG makeTcp(UCHAR *f, int ip6, ULONG payload, ULONG opt,
                     ULONG flags, ULONG seq)
{
    UCHAR *ip = f + 14;
    UCHAR *tcp;
    ULONG iplen = ip6 ? 40 : 20;
    ULONG tcplen = 20 + opt;
    ULONG seg = tcplen + payload;
    ULONG s;
    ULONG i;

    memcpy(f, macs, 12);
    put16be(f + 12, ip6 ? 0x86DD : 0x0800);
    memset(ip, 0, iplen);
    if (ip6) {
        ip[0] = 0x60;
        put16be(ip + 4, seg);
        ip[6] = 6;
        ip[7] = 64;
        for (i = 0; i < 16; i++) {
            ip[8 + i] = (UCHAR)(0xFE + i);
            ip[24 + i] = (UCHAR)(0x80 + i);
        }
    } else {
        ip[0] = 0x45;
        put16be(ip + 2, iplen + seg);
        put16be(ip + 4, 0xFFFE);
        put16be(ip + 6, 0x4000);
        ip[8] = 64;
        ip[9] = 6;
        ip[12] = 192; ip[13] = 168; ip[14] = 7; ip[15] = 1;
        ip[16] = 192; ip[17] = 168; ip[18] = 7; ip[19] = 2;
        put16be(ip + 10, (~refSum(ip, 20, 0)) & 0xFFFF);
    }
    tcp = ip + iplen;
    memset(tcp, 0, tcplen);
    put16be(tcp, 445);
    put16be(tcp + 2, 50000);
    put32be(tcp + 4, seq);
    put32be(tcp + 8, 0x01020304);
    tcp[12] = (UCHAR)((tcplen / 4) << 4);
    tcp[13] = (UCHAR)flags;
    put16be(tcp + 14, 0xFFFF);
    for (i = 0; i < opt; i++) {
        tcp[20 + i] = (UCHAR)(i == opt - 1 ? 0 : 1);
    }
    fillPattern(tcp + tcplen, payload, payload + opt);
    if (ip6) {
        s = refSum(ip + 8, 32, 0) + (seg >> 16) + (seg & 0xFFFF) + 6;
    } else {
        s = refSum(ip + 12, 8, 0) + seg + 6;
    }
    s = refSum(tcp, seg, s);
    put16be(tcp + 16, (~s) & 0xFFFF);
    return 14 + seg + iplen;
}

static void splitCheck(int ip6, ULONG payload, ULONG opt, ULONG maxFrame,
                       ULONG flags)
{
    NET_SEG seg;
    ULONG len = makeTcp(pkt, ip6, payload, opt, flags, 0xFFFFF000UL);
    ULONG why;
    ULONG n;
    ULONG k = 0;
    ULONG got = 0;
    ULONG iplen = ip6 ? 40 : 20;
    ULONG hdrs = 14 + iplen + 20 + opt;
    ULONG piecePay;
    ULONG pieces;
    char what[96];

    CHECK(tcpGood(pkt, len, ip6), "the vector itself is good");
    CHECK_EQ(NetSegBegin(&seg, pkt, len, maxFrame, &why), 1UL,
             "a good large segment is taken");
    CHECK_EQ(why, 0UL, "no reason");
    piecePay = (maxFrame - hdrs) & ~1UL;
    pieces = (payload + piecePay - 1) / piecePay;
    CHECK_EQ(seg.Pieces, pieces, "pieces");
    memcpy(pkt2, pkt, len);

    while ((n = NetSegNext(&seg, piece)) != 0) {
        const UCHAR *pip = piece + 14;
        const UCHAR *ptcp = pip + iplen;
        ULONG pl = n - hdrs;

        sprintf(what, "ip6 %d max %lu piece %lu", ip6, maxFrame, k);
        CHECK(n <= maxFrame, what);
        CHECK(memcmp(piece, pkt, 12) == 0, "addresses kept");
        CHECK(tcpGood(piece, n, ip6), "each piece's TCP checksum");
        if (ip6) {
            CHECK_EQ(get16be(pip + 4), 20 + opt + pl, "payload length");
        } else {
            CHECK(ip4Good(piece), "each piece's IPv4 checksum");
            CHECK_EQ(get16be(pip + 2), iplen + 20 + opt + pl,
                     "total length");
            CHECK_EQ(get16be(pip + 4), (0xFFFE + k) & 0xFFFF,
                     "identification per piece, wrapping");
            CHECK_EQ(get16be(pip + 6), 0x4000UL, "DF kept");
        }
        CHECK_EQ(get32be(ptcp + 4), 0xFFFFF000UL + k * piecePay,
                 "sequence per piece, wrapping");
        CHECK(memcmp(ptcp + 20, pkt + 14 + iplen + 20, opt) == 0,
              "options copied to every piece");
        if (k + 1 < pieces) {
            CHECK_EQ(pl, piecePay, "an even piece");
            CHECK_EQ(ptcp[13] & 0x09, 0, "FIN and PSH only on the last");
        } else {
            CHECK_EQ(ptcp[13] & 0x09, flags & 0x09, "FIN, PSH on the last");
        }
        if (k != 0) {
            CHECK_EQ(ptcp[13] & 0x80, 0, "CWR only on the first");
        } else {
            CHECK_EQ(ptcp[13] & 0x80, flags & 0x80, "CWR on the first");
        }
        CHECK_EQ(ptcp[13] & 0x50, flags & 0x50, "ACK and ECE kept");
        memcpy(joined + got, piece + hdrs, pl);
        got += pl;
        k++;
    }
    CHECK_EQ(k, pieces, "every piece written");
    CHECK_EQ(got, payload, "the payload's bytes");
    CHECK(memcmp(joined, pkt + hdrs, payload) == 0, "the payload back");
    CHECK(memcmp(pkt, pkt2, len) == 0, "the original unchanged");
    CHECK_EQ(NetSegNext(&seg, piece), 0UL, "nothing after the last");
}

static void testSegment(void)
{
    NET_SEG seg;
    ULONG len;
    ULONG why;

    splitCheck(0, 65536 - 54, 0, 1514, 0x18);
    splitCheck(0, 65536 - 54 - 12, 12, 1514, 0x99);
    splitCheck(1, 65536 - 74, 0, 1514, 0xD9);
    splitCheck(0, 65000, 0, 1515, 0x10);
    splitCheck(1, 30000, 32, 9014, 0x11);
    splitCheck(0, 1461, 0, 1514, 0x10);

    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    pkt[14 + 8] ^= 1;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "bad IP sum");
    CHECK_EQ(why, NET_SEGDROP_IPCSUM, "as IP checksum");

    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    pkt[len - 1] ^= 0x40;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "bad TCP sum");
    CHECK_EQ(why, NET_SEGDROP_TCPCSUM, "as TCP checksum");

    len = makeTcp(pkt, 1, 4000, 0, 0x10, 1);
    pkt[14 + 40 + 100] ^= 0x01;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "bad TCP sum over IPv6");
    CHECK_EQ(why, NET_SEGDROP_TCPCSUM, "as TCP checksum");

    len = makeTcp(pkt, 0, 4000, 0, 0x12, 1);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "SYN");
    CHECK_EQ(why, NET_SEGDROP_FLAGS, "as flags");
    len = makeTcp(pkt, 0, 4000, 0, 0x14, 1);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "RST");
    CHECK_EQ(why, NET_SEGDROP_FLAGS, "as flags");
    len = makeTcp(pkt, 1, 4000, 0, 0x30, 1);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "URG");
    CHECK_EQ(why, NET_SEGDROP_FLAGS, "as flags");

    /* 4004 + 20 bytes of TCP: a multiple of 8, as a fragment with MF
     * must carry. */
    len = makeTcp(pkt, 0, 4004, 0, 0x10, 1);
    put16be(pkt + 14 + 6, 0x2000);
    put16be(pkt + 14 + 10, 0);
    put16be(pkt + 14 + 10, (~refSum(pkt + 14, 20, 0)) & 0xFFFF);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 1UL,
             "a TCP fragment (MF set) is fragmented further");
    CHECK_EQ(seg.Frag, 1UL, "at the IP layer");

    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    pkt[14 + 9] = 17;
    put16be(pkt + 14 + 10, 0);
    put16be(pkt + 14 + 10, (~refSum(pkt + 14, 20, 0)) & 0xFFFF);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "UDP with Don't Fragment set");
    CHECK_EQ(why, NET_SEGDROP_DF, "as DF");

    len = makeTcp(pkt, 1, 4000, 0, 0x10, 1);
    pkt[14 + 6] = 0;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "an IPv6 extension header");
    CHECK_EQ(why, NET_SEGDROP_NOTTCP, "as not TCP");

    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    put16be(pkt + 12, 0x8100);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "VLAN tag");
    CHECK_EQ(why, NET_SEGDROP_NOTTCP, "as not TCP");

    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    CHECK_EQ(NetSegBegin(&seg, pkt, len + 2, 1514, &why), 0UL,
             "a frame longer than IP says");
    CHECK_EQ(why, NET_SEGDROP_LENGTH, "as length");
    len = makeTcp(pkt, 1, 4000, 0, 0x10, 1);
    CHECK_EQ(NetSegBegin(&seg, pkt, len - 2, 1514, &why), 0UL,
             "an IPv6 frame shorter than it says");
    CHECK_EQ(why, NET_SEGDROP_LENGTH, "as length");

    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    pkt[14 + 20 + 12] = 0x40;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "TCP data offset 4");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    pkt[14] = 0x44;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "IHL 4");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    CHECK_EQ(NetSegBegin(&seg, pkt, 14, 1514, &why), 0UL, "no IP header");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    len = makeTcp(pkt, 0, 4000, 0, 0x10, 1);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1000, &why), 0UL,
             "a maximum below 1514");
}

/* ------------------------------------------------------------------ */
/* Receive IPv4 fragmentation (record 35 section 7.4, RFC 791 3.2)     */
/* ------------------------------------------------------------------ */

/* An IPv4 datagram of protocol proto: ihl header bytes (options from
 * opts, ihl - 20 of them), payload bytes of pattern, the fragment field
 * frag (flags and offset in 8-byte units). Good header checksum. */
static ULONG makeIp4(UCHAR *f, ULONG proto, ULONG payload, ULONG ihl,
                     const UCHAR *opts, ULONG frag)
{
    UCHAR *ip = f + 14;

    memcpy(f, macs, 12);
    put16be(f + 12, 0x0800);
    memset(ip, 0, ihl);
    ip[0] = (UCHAR)(0x40 | (ihl / 4));
    put16be(ip + 2, ihl + payload);
    put16be(ip + 4, 0xBEEF);
    put16be(ip + 6, frag);
    ip[8] = 128;
    ip[9] = (UCHAR)proto;
    ip[12] = 169; ip[13] = 254; ip[14] = 189; ip[15] = 101;
    ip[16] = 169; ip[17] = 254; ip[18] = 31; ip[19] = 186;
    if (ihl > 20) {
        memcpy(ip + 20, opts, ihl - 20);
    }
    put16be(ip + 10, (~refSum(ip, ihl, 0)) & 0xFFFF);
    fillPattern(ip + ihl, payload, payload + proto + ihl);
    return 14 + ihl + payload;
}

/* Splits the datagram in pkt and checks every fragment: within maxFrame,
 * a good header checksum, the identification and addresses kept, DF
 * clear, offsets contiguous in 8-byte units from the original's, MF on
 * all but the last and the original's on the last, the first header the
 * original's, the later ones laterOpts's options, the data joined back. */
static void fragCheck(ULONG proto, ULONG payload, ULONG ihl,
                      const UCHAR *opts, ULONG frag, ULONG maxFrame,
                      const UCHAR *laterOpts, ULONG laterIhl)
{
    NET_SEG seg;
    ULONG len = makeIp4(pkt, proto, payload, ihl, opts, frag);
    ULONG why;
    ULONG n;
    ULONG k = 0;
    ULONG got = 0;
    ULONG expectOff = (frag & 0x1FFF) * 8;
    char what[96];

    CHECK_EQ(NetSegBegin(&seg, pkt, len, maxFrame, &why), 1UL,
             "an oversize IPv4 datagram is taken for fragmentation");
    CHECK_EQ(why, 0UL, "no reason");
    CHECK_EQ(seg.Frag, 1UL, "as fragmentation");
    memcpy(pkt2, pkt, len);
    while ((n = NetSegNext(&seg, piece)) != 0) {
        const UCHAR *pip = piece + 14;
        ULONG hl = (ULONG)(pip[0] & 15) * 4;
        ULONG dl = n - 14 - hl;
        ULONG f = get16be(pip + 6);
        int last = (k + 1 == seg.Pieces);

        sprintf(what, "proto %lu len %lu max %lu piece %lu", proto,
                payload, maxFrame, k);
        CHECK(n <= maxFrame, what);
        CHECK(memcmp(piece, pkt, 14) == 0, "Ethernet header kept");
        CHECK(ip4Good(piece), "each fragment's header checksum");
        CHECK_EQ(get16be(pip + 2), hl + dl, "total length");
        CHECK_EQ(get16be(pip + 4), 0xBEEFUL, "identification kept");
        CHECK_EQ(pip[9], proto, "protocol kept");
        CHECK(memcmp(pip + 12, pkt + 14 + 12, 8) == 0, "addresses kept");
        CHECK_EQ(f & 0x4000, 0UL, "DF clear");
        CHECK_EQ((f & 0x1FFF) * 8, expectOff, "offset contiguous");
        if (!last) {
            CHECK_EQ(dl & 7, 0UL, "a multiple of 8 but the last");
            CHECK_EQ(f & 0x2000, 0x2000UL, "MF on all but the last");
        } else {
            CHECK_EQ(f & 0x2000, frag & 0x2000, "the original MF last");
        }
        if (k == 0) {
            CHECK_EQ(hl, ihl, "the first keeps the whole header");
            CHECK(memcmp(pip + 20, pkt + 14 + 20, ihl - 20) == 0,
                  "every option on the first");
        } else {
            CHECK_EQ(hl, laterIhl, "the later header's length");
            CHECK(laterIhl == 20 ||
                  memcmp(pip + 20, laterOpts, laterIhl - 20) == 0,
                  "only the copied options later");
        }
        memcpy(joined + got, pip + hl, dl);
        got += dl;
        expectOff += dl;
        k++;
    }
    CHECK_EQ(k, seg.Pieces, "every fragment written");
    CHECK_EQ(got, payload, "the payload's bytes");
    CHECK(memcmp(joined, pkt + 14 + ihl, payload) == 0, "the data back");
    CHECK(memcmp(pkt, pkt2, len) == 0, "the original unchanged");
    CHECK_EQ(NetSegNext(&seg, piece), 0UL, "nothing after the last");
}

static void testFragment(void)
{
    /* Options: NOP, record route (0x07, not copied, 7 bytes), loose
     * source route (0x83, copied, 7 bytes), NOP: 20 + 16 = 36; the
     * later fragments carry the 7-byte copied option and one pad byte. */
    static const UCHAR opts[16] = {
        0x01, 0x07, 0x07, 0x04, 0, 0, 0, 0,
        0x83, 0x07, 0x04, 10, 0, 0, 1, 0x01
    };
    static const UCHAR laterOpts[8] = { 0x83, 0x07, 0x04, 10, 0, 0, 1, 0 };
    static const UCHAR badLen1[4] = { 0x83, 0x01, 0, 0 };
    static const UCHAR badPast[4] = { 0x83, 0x08, 0, 0 };
    static const UCHAR noCopy[4] = { 0x07, 0x03, 0x04, 0 };
    NET_SEG seg;
    ULONG len;
    ULONG why;

    fragCheck(17, 3008, 20, NULL, 0, 1514, NULL, 20);       /* UDP 3000 */
    fragCheck(1, 8008, 20, NULL, 0, 1514, NULL, 20);        /* ping 8000 */
    fragCheck(17, 61980, 20, NULL, 0, 1514, NULL, 20);      /* MTU 62000 */
    fragCheck(17, 65515, 20, NULL, 0, 1514, NULL, 20);      /* the most */
    fragCheck(17, 30000, 20, NULL, 0, 1515, NULL, 20);
    fragCheck(1, 30000, 20, NULL, 0, 9014, NULL, 20);
    fragCheck(17, 5000, 36, opts, 0, 1514, laterOpts, 28);
    fragCheck(17, 5000, 24, noCopy, 0, 1514, NULL, 20);
    fragCheck(17, 4000, 20, NULL, 0x2000 | 185, 1514, NULL, 20); /* middle */
    fragCheck(17, 4000, 20, NULL, 370, 1514, NULL, 20);     /* a last one */
    fragCheck(6, 4000, 20, NULL, 0x2000, 1514, NULL, 20);   /* TCP, MF set */
    fragCheck(17, 4000, 20, NULL, 0x8000, 1514, NULL, 20);  /* reserved */

    len = makeIp4(pkt, 17, 3008, 20, NULL, 0x4000);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "DF set");
    CHECK_EQ(why, NET_SEGDROP_DF, "as DF");
    len = makeIp4(pkt, 17, 3008, 20, NULL, 0);
    pkt[14 + 8] ^= 1;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "bad IP sum");
    CHECK_EQ(why, NET_SEGDROP_IPCSUM, "as IP checksum");
    len = makeIp4(pkt, 17, 3008, 20, NULL, 0);
    CHECK_EQ(NetSegBegin(&seg, pkt, len + 2, 1514, &why), 0UL,
             "a frame longer than its total length");
    CHECK_EQ(why, NET_SEGDROP_LENGTH, "as length");
    len = makeIp4(pkt, 17, 3008, 24, badLen1, 0);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "option len 1");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    len = makeIp4(pkt, 17, 3008, 24, badPast, 0);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "an option past the header");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    len = makeIp4(pkt, 17, 3008, 20, NULL, 8000);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "offset and payload past 65535");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    /* The reassembly bound with the header: 64000 + 1520 + 20 = 65540
     * refused; 64000 + 1512 + 20 = 65532 taken (a last fragment). */
    len = makeIp4(pkt, 17, 1520, 20, NULL, 8000);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "offset, payload and header past 65535");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    fragCheck(17, 1512, 20, NULL, 8000, 1514, NULL, 20);
    fragCheck(17, 65488, 20, NULL, 3, 1514, NULL, 20);       /* 65532 */
    len = makeIp4(pkt, 17, 3001, 20, NULL, 0x2000);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL,
             "MF with data not a multiple of 8");
    CHECK_EQ(why, NET_SEGDROP_HEADER, "as header");
    fragCheck(17, 3001, 20, NULL, 0, 1514, NULL, 20);  /* unaligned, MF 0 */
    len = makeIp4(pkt, 17, 3008, 20, NULL, 0);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1000, &why), 0UL,
             "a maximum below 1514");
    len = makeIp4(pkt, 17, 3008, 20, NULL, 0);
    put16be(pkt + 12, 0x0806);
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "ARP");
    CHECK_EQ(why, NET_SEGDROP_NOTTCP, "as not TCP");
    len = makeTcp(pkt, 1, 4000, 0, 0x10, 1);
    pkt[14 + 6] = 17;
    CHECK_EQ(NetSegBegin(&seg, pkt, len, 1514, &why), 0UL, "IPv6 UDP");
    CHECK_EQ(why, NET_SEGDROP_NOTTCP, "as not TCP");
}

int main(void)
{
    testSums();
    testTxCounts();
    testTxHeader();
    testFrameId();
    testRoundTrip();
    testRxDrops();
    testSegment();
    testFragment();
    printf("test_net_frame: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
