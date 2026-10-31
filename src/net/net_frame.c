/*
 * net_frame.c - the USB4NET framing core (net_frame.h; design record 35
 * sections 7.2 to 7.4, task 45.59 step S6). Pure and DDK-free.
 *
 * IRQL: any; no lock, no allocation. The caller serialises each NET_TX
 * and NET_RX (the miniport's send queue owner and its receive DPC).
 */

#ifndef XHCI_HOST_TEST
#include <ndis.h>
#endif
#include "net_frame.h"

static ULONG netGet16be(const UCHAR *p)
{
    return ((ULONG)p[0] << 8) | (ULONG)p[1];
}

static ULONG netGet32be(const UCHAR *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
           ((ULONG)p[2] << 8) | (ULONG)p[3];
}

static void netPut16be(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)((v >> 8) & 0xFFUL);
    p[1] = (UCHAR)(v & 0xFFUL);
}

static void netPut32be(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)((v >> 24) & 0xFFUL);
    p[1] = (UCHAR)((v >> 16) & 0xFFUL);
    p[2] = (UCHAR)((v >> 8) & 0xFFUL);
    p[3] = (UCHAR)(v & 0xFFUL);
}

static ULONG netGet16le(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG netGet32le(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) |
           ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

static void netPut16le(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
}

static void netPut32le(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static void netCopy(UCHAR *dst, const UCHAR *src, ULONG n)
{
    while (n != 0) {
        *dst = *src;
        dst++;
        src++;
        n--;
    }
}

/* ------------------------------------------------------------------ */
/* Checksums (RFC 1071's one's-complement sum)                         */
/* ------------------------------------------------------------------ */

ULONG NetSumFold(ULONG Sum)
{
    while ((Sum >> 16) != 0) {
        Sum = (Sum & 0xFFFFUL) + (Sum >> 16);
    }
    return Sum;
}

/* Sum is a folded partial sum (at most 0xFFFF); so is the result. An odd
 * Length's last byte is the high byte of a word padded with zero. */
ULONG NetSum16(const UCHAR *Data, ULONG Length, ULONG Sum)
{
    ULONG i;

    for (i = 0; i + 1 < Length; i += 2) {
        Sum += netGet16be(Data + i);
        if ((Sum & 0x80000000UL) != 0) {
            Sum = NetSumFold(Sum);
        }
    }
    if (i < Length) {
        Sum += (ULONG)Data[i] << 8;
    }
    return NetSumFold(Sum);
}

/* ------------------------------------------------------------------ */
/* Transmit                                                            */
/* ------------------------------------------------------------------ */

void NetTxInit(NET_TX *Tx)
{
    Tx->FrameId = 0;
}

ULONG NetTxFrames(ULONG Length, ULONG MaxFrame, ULONG PeerStns, ULONG *Why)
{
    *Why = 0;
    if (Length < NET_ETH_MIN) {
        *Why = NET_TXDROP_SHORT;
        return 0;
    }
    if (Length > MaxFrame || Length > NET_PACKET_MAX) {
        *Why = NET_TXDROP_LONG;
        return 0;
    }
    if (Length > NET_SMALL_MAX && (PeerStns & NET_PRTCSTNS_64K) == 0) {
        *Why = NET_TXDROP_PEER;
        return 0;
    }
    return (Length + NET_FRAME_DATA - 1) / NET_FRAME_DATA;
}

ULONG NetTxFrameData(ULONG Length, ULONG Index)
{
    ULONG start = Index * NET_FRAME_DATA;

    if (start >= Length) {
        return 0;
    }
    if (Length - start > NET_FRAME_DATA) {
        return NET_FRAME_DATA;
    }
    return Length - start;
}

ULONG NetTxHeader(UCHAR *Buf, ULONG Length, ULONG Index, ULONG Count,
                  ULONG Id)
{
    ULONG size = NetTxFrameData(Length, Index);

    netPut32le(Buf, size);
    netPut16le(Buf + 4, Index);
    netPut16le(Buf + 6, Id);
    netPut32le(Buf + 8, Count);
    return NET_HDR_LEN + size;
}

ULONG NetTxPacketId(NET_TX *Tx, ULONG PeerStns)
{
    ULONG id;

    if ((PeerStns & NET_PRTCSTNS_FRAMEID) == 0) {
        return 0;
    }
    id = Tx->FrameId & 0xFFFFUL;
    Tx->FrameId = (id + 1) & 0xFFFFUL;
    return id;
}

/* ------------------------------------------------------------------ */
/* Receive                                                             */
/* ------------------------------------------------------------------ */

void NetRxInit(NET_RX *Rx, UCHAR *Buffer, ULONG Capacity)
{
    ULONG i;

    Rx->Buffer = Buffer;
    Rx->Capacity = Capacity;
    for (i = 0; i < NET_RXDROP_REASONS; i++) {
        Rx->Drops[i] = 0;
    }
    NetRxReset(Rx);
}

void NetRxReset(NET_RX *Rx)
{
    Rx->Active = 0;
    Rx->Count = 0;
    Rx->Index = 0;
    Rx->Id = 0;
    Rx->Length = 0;
}

static ULONG netRxDrop(NET_RX *Rx, ULONG reason, ULONG *Why)
{
    Rx->Drops[reason]++;
    *Why = reason;
    return NET_RX_DROPPED;
}

/* The packet in progress, if any, dropped as reason (counted). */
static void netRxAbandon(NET_RX *Rx, ULONG reason)
{
    if (Rx->Active) {
        Rx->Drops[reason]++;
    }
    NetRxReset(Rx);
}

ULONG NetRxFrame(NET_RX *Rx, const UCHAR *Frame, ULONG FrameLength,
                 ULONG Flags, const UCHAR **Packet, ULONG *PacketLength,
                 ULONG *Why)
{
    ULONG size;
    ULONG index;
    ULONG id;
    ULONG count;
    ULONG limit;

    *Packet = 0;
    *PacketLength = 0;
    *Why = 0;

    /* A frame the host interface flagged is not trusted at all: its Data
     * Length is not to be believed (Spec 12.3.2.1). It also ends the
     * packet in progress (counted as a sequence drop): a lost middle frame
     * would otherwise let a later packet's frame of the same Count and
     * Frame ID - every packet's ID is 0 towards a peer without prtcstns
     * bit 1 - complete it with another packet's bytes. */
    if ((Flags & (NET_RXF_CRC_ERROR | NET_RXF_OVERFLOW)) != 0) {
        netRxAbandon(Rx, NET_RXDROP_SEQUENCE);
        if ((Flags & NET_RXF_CRC_ERROR) != 0) {
            return netRxDrop(Rx, NET_RXDROP_CRC, Why);
        }
        return netRxDrop(Rx, NET_RXDROP_OVERFLOW, Why);
    }
    if (FrameLength <= NET_HDR_LEN || FrameLength > NET_BUF_LEN) {
        return netRxDrop(Rx, NET_RXDROP_SHORT, Why);
    }

    size = netGet32le(Frame);
    index = netGet16le(Frame + 4);
    id = netGet16le(Frame + 6);
    count = netGet32le(Frame + 8);

    if (size == 0 || size > FrameLength - NET_HDR_LEN) {
        return netRxDrop(Rx, NET_RXDROP_SIZE, Why);
    }
    if (count == 0 || count > NET_FRAMES_MAX) {
        return netRxDrop(Rx, NET_RXDROP_COUNT, Why);
    }
    if (index >= count) {
        return netRxDrop(Rx, NET_RXDROP_INDEX, Why);
    }

    if (index == 0) {
        netRxAbandon(Rx, NET_RXDROP_RESTART);
        if (count == 1) {
            if (size < NET_ETH_MIN) {
                return netRxDrop(Rx, NET_RXDROP_RUNT, Why);
            }
            *Packet = Frame + NET_HDR_LEN;
            *PacketLength = size;
            return NET_RX_PACKET;
        }
        limit = Rx->Capacity;
        if (limit > NET_PACKET_MAX) {
            limit = NET_PACKET_MAX;
        }
        if (size > limit) {
            return netRxDrop(Rx, NET_RXDROP_TOOBIG, Why);
        }
        netCopy(Rx->Buffer, Frame + NET_HDR_LEN, size);
        Rx->Active = 1;
        Rx->Count = count;
        Rx->Index = 0;
        Rx->Id = id;
        Rx->Length = size;
        return NET_RX_HELD;
    }

    if (!Rx->Active) {
        return netRxDrop(Rx, NET_RXDROP_ORPHAN, Why);
    }
    if (count != Rx->Count || index != Rx->Index + 1 || id != Rx->Id) {
        NetRxReset(Rx);
        return netRxDrop(Rx, NET_RXDROP_SEQUENCE, Why);
    }
    limit = Rx->Capacity;
    if (limit > NET_PACKET_MAX) {
        limit = NET_PACKET_MAX;
    }
    if (size > limit - Rx->Length) {
        NetRxReset(Rx);
        return netRxDrop(Rx, NET_RXDROP_TOOBIG, Why);
    }
    netCopy(Rx->Buffer + Rx->Length, Frame + NET_HDR_LEN, size);
    Rx->Length += size;
    Rx->Index = index;
    if (index + 1 < count) {
        return NET_RX_HELD;
    }

    size = Rx->Length;
    NetRxReset(Rx);
    if (size < NET_ETH_MIN) {
        return netRxDrop(Rx, NET_RXDROP_RUNT, Why);
    }
    *Packet = Rx->Buffer;
    *PacketLength = size;
    return NET_RX_PACKET;
}

/* ------------------------------------------------------------------ */
/* Receive segmentation                                                */
/* ------------------------------------------------------------------ */

#define NET_ETYPE_IP4       0x0800UL
#define NET_ETYPE_IP6       0x86DDUL
#define NET_PROTO_TCP       6UL
#define NET_IP6_HDR         40UL

#define NET_TCP_FIN         0x01UL
#define NET_TCP_SYN         0x02UL
#define NET_TCP_RST         0x04UL
#define NET_TCP_PSH         0x08UL
#define NET_TCP_URG         0x20UL
#define NET_TCP_CWR         0x80UL

/* The pseudo-header's sum (RFC 9293 section 3.1, RFC 8200 section 8.1)
 * for a TCP segment of TcpLength bytes, folded. */
static ULONG netPseudo(const NET_SEG *Seg, ULONG TcpLength)
{
    const UCHAR *ip = Seg->Frame + NET_ETH_HDR;
    ULONG sum;

    if (Seg->Ip6) {
        sum = NetSum16(ip + 8, 32, 0);
        sum += (TcpLength >> 16) + (TcpLength & 0xFFFFUL) + NET_PROTO_TCP;
    } else {
        sum = NetSum16(ip + 12, 8, 0);
        sum += TcpLength + NET_PROTO_TCP;
    }
    return NetSumFold(sum);
}

ULONG NetSegBegin(NET_SEG *Seg, const UCHAR *Frame, ULONG Length,
                  ULONG MaxFrame, ULONG *Why)
{
    const UCHAR *ip;
    const UCHAR *tcp;
    ULONG etype;
    ULONG ipBytes;
    ULONG seg;
    ULONG flags;
    ULONG sum;
    ULONG off;
    ULONG k;
    ULONG n;

    *Why = 0;
    Seg->Frame = Frame;
    Seg->Length = Length;
    Seg->Next = 0;
    Seg->Pieces = 0;

    if (Length <= NET_ETH_HDR) {
        *Why = NET_SEGDROP_HEADER;
        return 0;
    }
    etype = netGet16be(Frame + 12);
    ip = Frame + NET_ETH_HDR;
    ipBytes = Length - NET_ETH_HDR;

    if (etype == NET_ETYPE_IP4) {
        Seg->Ip6 = 0;
        if (ipBytes < 20 || (ip[0] >> 4) != 4) {
            *Why = NET_SEGDROP_HEADER;
            return 0;
        }
        Seg->IpLen = ((ULONG)ip[0] & 0x0FUL) * 4;
        if (Seg->IpLen < 20 || Seg->IpLen > ipBytes) {
            *Why = NET_SEGDROP_HEADER;
            return 0;
        }
        if (NetSum16(ip, Seg->IpLen, 0) != 0xFFFFUL) {
            *Why = NET_SEGDROP_IPCSUM;
            return 0;
        }
        if (netGet16be(ip + 2) != ipBytes) {
            *Why = NET_SEGDROP_LENGTH;
            return 0;
        }
        if ((netGet16be(ip + 6) & 0x3FFFUL) != 0) {
            *Why = NET_SEGDROP_FRAGMENT;
            return 0;
        }
        if ((ULONG)ip[9] != NET_PROTO_TCP) {
            *Why = NET_SEGDROP_NOTTCP;
            return 0;
        }
    } else if (etype == NET_ETYPE_IP6) {
        Seg->Ip6 = 1;
        Seg->IpLen = NET_IP6_HDR;
        if (ipBytes < NET_IP6_HDR || (ip[0] >> 4) != 6) {
            *Why = NET_SEGDROP_HEADER;
            return 0;
        }
        if ((ULONG)ip[6] != NET_PROTO_TCP) {
            *Why = NET_SEGDROP_NOTTCP;
            return 0;
        }
        if (netGet16be(ip + 4) != ipBytes - NET_IP6_HDR) {
            *Why = NET_SEGDROP_LENGTH;
            return 0;
        }
    } else {
        *Why = NET_SEGDROP_NOTTCP;
        return 0;
    }

    tcp = ip + Seg->IpLen;
    seg = ipBytes - Seg->IpLen;
    if (seg < 20) {
        *Why = NET_SEGDROP_HEADER;
        return 0;
    }
    Seg->TcpLen = ((ULONG)tcp[12] >> 4) * 4;
    if (Seg->TcpLen < 20 || Seg->TcpLen > seg) {
        *Why = NET_SEGDROP_HEADER;
        return 0;
    }
    Seg->Payload = seg - Seg->TcpLen;

    if (MaxFrame < NET_SMALL_MAX ||
        MaxFrame <= NET_ETH_HDR + Seg->IpLen + Seg->TcpLen + 1) {
        *Why = NET_SEGDROP_HEADER;
        return 0;
    }
    /* Even, so every piece starts at an even offset of the segment and
     * the pieces' sums add up to the whole payload's. */
    Seg->Piece = (MaxFrame - NET_ETH_HDR - Seg->IpLen - Seg->TcpLen) &
                 ~1UL;
    if (Seg->Payload == 0) {
        *Why = NET_SEGDROP_HEADER;
        return 0;
    }
    n = (Seg->Payload + Seg->Piece - 1) / Seg->Piece;
    if (n > NET_SEG_PIECES) {
        *Why = NET_SEGDROP_PIECES;
        return 0;
    }

    /* Step 1: the whole segment's checksum, the payload summed piece by
     * piece so each piece's sum is kept for its own checksum. */
    sum = netPseudo(Seg, seg);
    sum = NetSum16(tcp, Seg->TcpLen, sum);
    off = Seg->TcpLen;
    for (k = 0; k < n; k++) {
        ULONG len = Seg->Piece;

        if (len > seg - off) {
            len = seg - off;
        }
        Seg->Sums[k] = NetSum16(tcp + off, len, 0);
        sum = NetSumFold(sum + Seg->Sums[k]);
        off += len;
    }
    if (sum != 0xFFFFUL) {
        *Why = NET_SEGDROP_TCPCSUM;
        return 0;
    }

    /* Step 2: what a split would change the meaning of. */
    flags = (ULONG)tcp[13];
    if ((flags & (NET_TCP_SYN | NET_TCP_RST | NET_TCP_URG)) != 0) {
        *Why = NET_SEGDROP_FLAGS;
        return 0;
    }

    Seg->Pieces = n;
    return 1;
}

ULONG NetSegNext(NET_SEG *Seg, UCHAR *Out)
{
    const UCHAR *tcpIn;
    UCHAR *ip;
    UCHAR *tcp;
    ULONG k = Seg->Next;
    ULONG hdrs;
    ULONG len;
    ULONG off;
    ULONG flags;
    ULONG sum;

    if (k >= Seg->Pieces) {
        return 0;
    }
    hdrs = NET_ETH_HDR + Seg->IpLen + Seg->TcpLen;
    tcpIn = Seg->Frame + NET_ETH_HDR + Seg->IpLen;
    off = k * Seg->Piece;
    len = Seg->Payload - off;
    if (len > Seg->Piece) {
        len = Seg->Piece;
    }

    netCopy(Out, Seg->Frame, hdrs);
    netCopy(Out + hdrs, tcpIn + Seg->TcpLen + off, len);
    ip = Out + NET_ETH_HDR;
    tcp = ip + Seg->IpLen;

    if (Seg->Ip6) {
        netPut16be(ip + 4, Seg->TcpLen + len);
    } else {
        netPut16be(ip + 2, Seg->IpLen + Seg->TcpLen + len);
        netPut16be(ip + 4, (netGet16be(ip + 4) + k) & 0xFFFFUL);
        netPut16be(ip + 10, 0);
        netPut16be(ip + 10, (~NetSum16(ip, Seg->IpLen, 0)) & 0xFFFFUL);
    }

    netPut32be(tcp + 4, netGet32be(tcp + 4) + off);
    flags = (ULONG)tcp[13];
    if (k + 1 != Seg->Pieces) {
        flags &= ~(NET_TCP_FIN | NET_TCP_PSH);
    }
    if (k != 0) {
        flags &= ~NET_TCP_CWR;
    }
    tcp[13] = (UCHAR)flags;
    netPut16be(tcp + 16, 0);
    sum = netPseudo(Seg, Seg->TcpLen + len);
    sum = NetSum16(tcp, Seg->TcpLen, sum);
    sum = NetSumFold(sum + Seg->Sums[k]);
    netPut16be(tcp + 16, (~sum) & 0xFFFFUL);

    Seg->Next = k + 1;
    return hdrs + len;
}
