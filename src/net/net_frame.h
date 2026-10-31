/*
 * net_frame.h - the USB4NET framing core of the Thunderbolt networking
 * drivers (design record 35 section 7, task 45.59 step S6): the transmit
 * split of an Ethernet frame into USB4 frames, the receive checks and
 * reassembly, and the receive segmentation of a TCP segment longer than
 * the adapter's maximum (section 7.4). Pure and DDK-free, host-tested by
 * test\test_net_frame.c; no NDIS type, no allocation, no lock: shared
 * unchanged by xhcinet.sys (NDIS 5) and xhcinet6.sys (NDIS 6).
 *
 * Wire facts: IDS 5.1.5 (the 12-byte little-endian header: Frame Size,
 * Frame Index, Frame ID, Frame Count) and Linux's thunderbolt-net read for
 * facts only (record 35 section 2.4, [tbnet]): 4084 data bytes a frame,
 * one Frame ID for every frame of one packet, advanced once a packet and
 * only towards a peer whose prtcstns has bit 1; no FCS and no padding.
 * The receive segmentation follows RFC 791, RFC 9293, RFC 3168 and RFC
 * 8200, written for this code from those documents.
 *
 * IRQL: any; the caller serialises each NET_TX and each NET_RX.
 */

#ifndef NET_FRAME_H
#define NET_FRAME_H

#include "../xhci_compat.h"

#define NET_HDR_LEN         12UL     /* the USB4NET frame header         */
#define NET_BUF_LEN         4096UL   /* one host-interface frame         */
#define NET_FRAME_DATA      4084UL   /* data bytes a full USB4 frame     */
#define NET_FRAMES_MAX      17UL     /* frames of the largest packet     */
#define NET_ETH_HDR         14UL
#define NET_ETH_MIN         14UL     /* the shortest frame passed up     */
#define NET_PACKET_MAX      65550UL  /* 64 KB payload + Ethernet header  */
#define NET_SMALL_MAX       1514UL   /* to a peer without prtcstns bit 2 */

/* XhciNetMtu (record 35 section 8.3). */
#define NET_MTU_DEFAULT     1500UL
#define NET_MTU_MIN         1500UL
#define NET_MTU_MAX         65522UL

/* The peer's prtcstns bits (IDS Table 5-2; record 35 section 2.1). */
#define NET_PRTCSTNS_E2E    0x1UL
#define NET_PRTCSTNS_FRAMEID 0x2UL
#define NET_PRTCSTNS_64K    0x4UL

/* The receive callback's flags (the interface's Receive, record 35
 * section 6.3). The PDF fields are carried for the log only: a frame of
 * one tunnelled packet reads SOF PDF 0 (Spec 12.4.2.1), so neither PDF
 * is a reason to drop. */
#define NET_RXF_CRC_ERROR   0x00000001UL
#define NET_RXF_OVERFLOW    0x00000002UL
#define NET_RXF_SOF_SHIFT   8
#define NET_RXF_EOF_SHIFT   12

/* ------------------------------------------------------------------ */
/* Transmit                                                            */
/* ------------------------------------------------------------------ */

/* NetTxFrames's refusals (it returns 0 and sets *Why). */
#define NET_TXDROP_SHORT    1UL   /* below 14 bytes                     */
#define NET_TXDROP_LONG     2UL   /* above the adapter's maximum        */
#define NET_TXDROP_PEER     3UL   /* above 1514 to a peer without bit 2 */
#define NET_TXDROP_REASONS  4UL

typedef struct _NET_TX {
    ULONG FrameId;      /* the next packet's Frame ID, 0 to 0xFFFF      */
} NET_TX, *PNET_TX;

void NetTxInit(NET_TX *Tx);

/*
 * The USB4 frames one Ethernet frame of Length bytes takes, 1 to 17,
 * or 0 with *Why set when it may not be sent: MaxFrame is the adapter's
 * maximum (its MTU plus 14), PeerStns the peer's prtcstns.
 */
ULONG NetTxFrames(ULONG Length, ULONG MaxFrame, ULONG PeerStns,
                  ULONG *Why);

/* The data bytes of frame Index of a Length-byte frame. */
ULONG NetTxFrameData(ULONG Length, ULONG Index);

/*
 * Writes frame Index's 12-byte header into Buf for a Length-byte frame
 * of Count frames sent with packet id Id (NetTxPacketId). Returns the
 * frame's whole length, header included; the caller copies
 * NetTxFrameData bytes of the Ethernet frame, from offset
 * Index * 4084, after the header.
 */
ULONG NetTxHeader(UCHAR *Buf, ULONG Length, ULONG Index, ULONG Count,
                  ULONG Id);

/*
 * The Frame ID of the packet about to be sent, and the counter advanced
 * past it: by one, wrapping at 0xFFFF, and only while the peer's
 * prtcstns has bit 1; with bit 1 clear every packet carries 0.
 * Called once a packet, after its reservation succeeded.
 */
ULONG NetTxPacketId(NET_TX *Tx, ULONG PeerStns);

/* ------------------------------------------------------------------ */
/* Receive                                                             */
/* ------------------------------------------------------------------ */

#define NET_RXDROP_CRC      0UL   /* CRC Error set                      */
#define NET_RXDROP_OVERFLOW 1UL   /* Buffer Overflow set                */
#define NET_RXDROP_SHORT    2UL   /* 12 bytes or fewer                  */
#define NET_RXDROP_SIZE     3UL   /* Frame Size 0 or past the data      */
#define NET_RXDROP_COUNT    4UL   /* Frame Count 0 or above 17          */
#define NET_RXDROP_INDEX    5UL   /* Frame Index not below Count        */
#define NET_RXDROP_ORPHAN   6UL   /* a later frame with none started    */
#define NET_RXDROP_SEQUENCE 7UL   /* a gap, or Count or Frame ID changed */
#define NET_RXDROP_RESTART  8UL   /* a packet cut by a new Index 0      */
#define NET_RXDROP_TOOBIG   9UL   /* the packet past 65550 bytes        */
#define NET_RXDROP_RUNT     10UL  /* a whole packet below 14 bytes      */
#define NET_RXDROP_REASONS  11UL

/* NetRxFrame's outcomes. */
#define NET_RX_HELD         0UL   /* taken; the packet is not complete  */
#define NET_RX_PACKET       1UL   /* a whole Ethernet frame is ready    */
#define NET_RX_DROPPED      2UL   /* dropped and counted                */

typedef struct _NET_RX {
    UCHAR *Buffer;      /* the caller's reassembly buffer               */
    ULONG Capacity;     /* its size, at least NET_PACKET_MAX            */
    ULONG Active;       /* a multi-frame packet is in progress          */
    ULONG Count;        /* its Frame Count                              */
    ULONG Index;        /* the Index of its last frame taken            */
    ULONG Id;           /* its Frame ID                                 */
    ULONG Length;       /* bytes held                                   */
    ULONG Drops[NET_RXDROP_REASONS];
} NET_RX, *PNET_RX;

void NetRxInit(NET_RX *Rx, UCHAR *Buffer, ULONG Capacity);

/* Drops a packet in progress without counting it (the link went down). */
void NetRxReset(NET_RX *Rx);

/*
 * One USB4 frame (header and data, FrameLength bytes as the descriptor
 * wrote back) with the receive callback's Flags. On NET_RX_PACKET,
 * *Packet and *PacketLength name the whole Ethernet frame: inside Frame
 * for a one-frame packet, else Rx->Buffer; valid until the next call or
 * until Frame is reused. On NET_RX_DROPPED, *Why is the reason counted
 * (a frame that starts a packet while one is in progress counts the cut
 * packet as NET_RXDROP_RESTART and is itself taken, so that outcome is
 * not a drop of this frame).
 */
ULONG NetRxFrame(NET_RX *Rx, const UCHAR *Frame, ULONG FrameLength,
                 ULONG Flags, const UCHAR **Packet, ULONG *PacketLength,
                 ULONG *Why);

/* ------------------------------------------------------------------ */
/* Receive segmentation (record 35 section 7.4)                        */
/* ------------------------------------------------------------------ */

#define NET_SEGDROP_NOTTCP  0UL   /* not TCP over IPv4 or IPv6          */
#define NET_SEGDROP_HEADER  1UL   /* a header malformed or truncated    */
#define NET_SEGDROP_LENGTH  2UL   /* a stated length not the frame's    */
#define NET_SEGDROP_IPCSUM  3UL   /* the IPv4 header checksum           */
#define NET_SEGDROP_TCPCSUM 4UL   /* the TCP checksum                   */
#define NET_SEGDROP_FLAGS   5UL   /* SYN, RST or URG set                */
#define NET_SEGDROP_FRAGMENT 6UL  /* an IPv4 fragment                   */
#define NET_SEGDROP_PIECES  7UL   /* more pieces than NET_SEG_PIECES    */
#define NET_SEGDROP_REASONS 8UL

/* With MaxFrame at least 1514 and headers of at most 60 + 60 bytes, a
 * piece carries at least 1380 bytes, so 65536 bytes need at most 48. */
#define NET_SEG_PIECES      64UL

typedef struct _NET_SEG {
    const UCHAR *Frame; /* the original, unchanged                      */
    ULONG Length;
    ULONG Ip6;          /* 1 for IPv6, 0 for IPv4                       */
    ULONG IpLen;        /* the IP header's length                       */
    ULONG TcpLen;       /* the TCP header's length                      */
    ULONG Payload;      /* the TCP payload's bytes                      */
    ULONG Piece;        /* the payload bytes of every piece but the last */
    ULONG Pieces;       /* how many                                     */
    ULONG Next;         /* the next piece to write                      */
    ULONG Sums[NET_SEG_PIECES]; /* each piece's payload sum, unfolded   */
} NET_SEG, *PNET_SEG;

/*
 * Checks a Length-byte Ethernet frame longer than MaxFrame for the split
 * (section 7.4 steps 1 and 2: TCP over IPv4 not a fragment, or over IPv6
 * with no extension header, both checksums good, the stated lengths the
 * frame's, no SYN, RST or URG). Returns 1 and fills Seg, or 0 with *Why
 * one of NET_SEGDROP_*. Nothing is written.
 */
ULONG NetSegBegin(NET_SEG *Seg, const UCHAR *Frame, ULONG Length,
                  ULONG MaxFrame, ULONG *Why);

/*
 * Writes the next piece (at most MaxFrame bytes) into Out and returns
 * its length, or 0 when every piece is written. Out holds at least the
 * MaxFrame given to NetSegBegin.
 */
ULONG NetSegNext(NET_SEG *Seg, UCHAR *Out);

/* The one's-complement sum helpers, exported for the host vectors. */
ULONG NetSum16(const UCHAR *Data, ULONG Length, ULONG Sum);
ULONG NetSumFold(ULONG Sum);

#endif /* NET_FRAME_H */
