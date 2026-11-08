/*
 * n5_data.c - xhcinet.sys's data path (design record 35 sections 7.2 to
 * 7.6): the send queue and its one sender, the reset, the receive
 * callback's reassembly, segmentation and indication, and the link.
 *
 * Locks: TxLock over the send queue, the sender's flags and the reset's;
 * RxLock over the receive packets' free list and RxOut. Neither is held
 * across a call into NDIS's completion or indication routines or into
 * the connection manager. The NET_RX and NET_SEG state is the receive
 * callback's alone, which the connection manager runs in one DPC.
 */

#include "xhcinet.h"

#define NET_NEXT(p) (*(PNDIS_PACKET *)&(p)->MiniportReserved[0])

/* IRQL: any. */
/* The destination volatile: an optimising compiler (WDK 7.1's cl 15 at
 * /Oxs) otherwise turns the loop into a call to the C runtime's memcpy, an
 * import no driver here may carry. */
VOID NetCopy(PUCHAR dst, const UCHAR *src, ULONG n)
{
    volatile UCHAR *d = (volatile UCHAR *)dst;

    while (n >= 4 && (((ULONG_PTR)d | (ULONG_PTR)src) & 3) == 0) {
        *(volatile ULONG *)d = *(const ULONG *)src;
        d += 4;
        src += 4;
        n -= 4;
    }
    while (n != 0) {
        *d = *src;
        d++;
        src++;
        n--;
    }
}

/* IRQL: any. */
/* The destination volatile: an optimising compiler (WDK 7.1's cl 15 at
 * /Oxs) otherwise turns the loop into a call to the C runtime's memset, an
 * import no driver here may carry. */
VOID NetZero(PVOID dst, ULONG n)
{
    volatile UCHAR *p = (volatile UCHAR *)dst;

    while (n != 0) {
        *p = 0;
        p++;
        n--;
    }
}

/* ------------------------------------------------------------------ */
/* Send                                                                */
/* ------------------------------------------------------------------ */

static VOID netCompleteList(PNET_ADAPTER a, PNDIS_PACKET list,
                            NDIS_STATUS status)
{
    PNDIS_PACKET next;

    while (list != NULL) {
        next = NET_NEXT(list);
        NET_NEXT(list) = NULL;
        NdisMSendComplete(a->Handle, list, status);
        list = next;
    }
}

/* Detaches the whole queue; TxLock held. */
static PNDIS_PACKET netDetach(PNET_ADAPTER a)
{
    PNDIS_PACKET list = a->TxHead;

    a->TxHead = NULL;
    a->TxTail = NULL;
    return list;
}

static VOID netResetDone(PNET_ADAPTER a);

/*
 * The end of one context's completion work (TxActive raised under TxLock
 * when it took its packets): TxLock held on entry, released here. When
 * it is the last and a reset waits, the reset completes, once, while this
 * context still counts in TxActive, so a halt cannot free the adapter
 * under it; a reset NDIS issues meanwhile is completed the same way. The
 * release of TxLock is the context's last access to the adapter here.
 * Returns 1 when the adapter is halting, so the caller touches it no
 * more. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG netTxEnd(PNET_ADAPTER a)
{
    ULONG halting;

    while (a->TxActive == 1 && a->ResetPending) {
        a->ResetPending = 0;
        a->ResetGate = 0;
        NdisReleaseSpinLock(&a->TxLock);
        netResetDone(a);
        NdisAcquireSpinLock(&a->TxLock);
    }
    a->TxActive--;
    halting = a->Halting;
    NdisReleaseSpinLock(&a->TxLock);
    return halting;
}

/* Completes every queued send with status. IRQL: <= DISPATCH_LEVEL. */
VOID NetFlushSends(PNET_ADAPTER a, NDIS_STATUS status)
{
    PNDIS_PACKET list;

    NdisAcquireSpinLock(&a->TxLock);
    list = netDetach(a);
    a->TxActive++;
    NdisReleaseSpinLock(&a->TxLock);
    netCompleteList(a, list, status);
    NdisAcquireSpinLock(&a->TxLock);
    (VOID)netTxEnd(a);
}

/* The reset's completion, from the last context completing sends: at
 * DISPATCH_LEVEL with no spin lock held (record 35 section 7.5). The
 * queue that gathered behind the gate is served by that context's own
 * loop (NetServe) once it is done. */
static VOID netResetDone(PNET_ADAPTER a)
{
    KIRQL old;

    KeRaiseIrql(DISPATCH_LEVEL, &old);
    NdisMResetComplete(a->Handle, NDIS_STATUS_SUCCESS, FALSE);
    KeLowerIrql(old);
    NetCmLog(a, "net.reset.done", 0);
}

#define NET_SENT        0UL
#define NET_FULL        1UL
#define NET_FAILED      2UL
#define NET_NOMAP       3UL     /* a send buffer could not be mapped      */

/* Frames one packet into a reservation and posts it (section 7.2).
 * IRQL: <= DISPATCH_LEVEL; the queue's one sender. */
static ULONG netSendOne(PNET_ADAPTER a, PNDIS_PACKET p, ULONG stns)
{
    PUCHAR bufs[NET_FRAMES_MAX];
    ULONG lens[NET_FRAMES_MAX];
    XHCI_CM_NET_TOKEN token;
    PNDIS_BUFFER b;
    PVOID va;
    UINT total;
    UINT blen;
    ULONG m;
    ULONG k;
    ULONG id;
    ULONG why;
    ULONG left;
    ULONG take;
    PUCHAR dst;
    const UCHAR *src;
    UINT have;

    NdisQueryPacket(p, NULL, NULL, &b, &total);
    m = NetTxFrames((ULONG)total, a->MaxFrame, stns, &why);
    if (m == 0) {
        a->TxDrops[why]++;
        if (why == NET_TXDROP_PEER && !a->BigLogged) {
            a->BigLogged = 1;
            NetCmLog(a, "net.peer.nobig", (ULONG)total);
        }
        return NET_FAILED;
    }
    if (!NetCmIfReserve(&a->Cm, m, bufs, &token)) {
        return NET_FULL;
    }
    id = NetTxPacketId(&a->Tx, stns);

    src = NULL;
    have = 0;
    for (k = 0; k < m; k++) {
        lens[k] = NetTxHeader(bufs[k], (ULONG)total, k, m, id);
        left = lens[k] - NET_HDR_LEN;
        dst = bufs[k] + NET_HDR_LEN;
        while (left != 0) {
            while (have == 0) {
                if (b == NULL) {
                    NetCmIfAbort(&a->Cm, &token);
                    a->TxErrors++;
                    return NET_FAILED;
                }
#if defined(NDIS51_MINIPORT)
                /* The caller's buffer, which may have no system mapping:
                 * on NT the plain macro maps it through
                 * MmGetSystemAddressForMdl, which bug checks when it
                 * cannot. The NDIS 5.0 image (98 SE to 7) has no Safe
                 * form and keeps the plain one. */
                NdisQueryBufferSafe(b, &va, &blen, NormalPagePriority);
                if (va == NULL) {
                    NetCmIfAbort(&a->Cm, &token);
                    a->TxErrors++;
                    return NET_NOMAP;
                }
#else
                NdisQueryBuffer(b, &va, &blen);
#endif
                src = (const UCHAR *)va;
                have = blen;
                NdisGetNextBuffer(b, &b);
            }
            take = left;
            if (take > (ULONG)have) {
                take = (ULONG)have;
            }
            NetCopy(dst, src, take);
            dst += take;
            src += take;
            have -= (UINT)take;
            left -= take;
        }
    }
    if (!NetCmIfCommit(&a->Cm, &token, m, lens)) {
        a->TxErrors++;
        return NET_FAILED;
    }
    a->TxOk++;
    a->TxBytes += (ULONG)total;
    return NET_SENT;
}

/*
 * Serves the queue: one sender at a time (TxServing, held until its
 * packet is completed), the head packet framed and committed with TxLock
 * released and completed with no lock held. Every context that completes
 * sends counts itself in TxActive from the moment it takes them until
 * they are completed, so a reset completes, and a halt frees, only after
 * the last of them. A full ring leaves the packet at the head until
 * TxDone serves again; work that arrives while a sender serves sets
 * TxKick, which the sender reads under the lock before it stops.
 * IRQL: <= DISPATCH_LEVEL.
 */
VOID NetServe(PNET_ADAPTER a)
{
    PNDIS_PACKET p;
    PNDIS_PACKET list;
    ULONG stns;
    ULONG r;
    ULONG down;
    ULONG reset;
    NDIS_STATUS status;

    for (;;) {
        NdisAcquireSpinLock(&a->TxLock);
        if (a->TxServing || a->ResetGate) {
            a->TxKick = 1;
            NdisReleaseSpinLock(&a->TxLock);
            return;
        }
        p = a->TxHead;
        if (p == NULL) {
            NdisReleaseSpinLock(&a->TxLock);
            return;
        }
        if (!a->LinkUp || !a->CmOpen || a->PowerD3 || a->Halting) {
            list = netDetach(a);
            a->TxDown++;
            a->TxActive++;
            NdisReleaseSpinLock(&a->TxLock);
            netCompleteList(a, list, NET_STATUS_DOWN);
            NdisAcquireSpinLock(&a->TxLock);
            if (netTxEnd(a)) {
                return;
            }
            continue;
        }
        a->TxHead = NET_NEXT(p);
        if (a->TxHead == NULL) {
            a->TxTail = NULL;
        }
        NET_NEXT(p) = NULL;
        a->TxServing = 1;
        a->TxActive++;
        a->TxKick = 0;
        stns = a->PeerStns;
        NdisReleaseSpinLock(&a->TxLock);

        r = netSendOne(a, p, stns);

        NdisAcquireSpinLock(&a->TxLock);
        /* A full ring keeps the packet only while it can still go: not
         * once the link is down, the service closed, a D3 begun or a
         * reset or halt under way, whose flushes may already have run. */
        down = (!a->LinkUp || !a->CmOpen || a->PowerD3 || a->Halting)
                   ? 1UL : 0UL;
        reset = a->ResetGate ? 1UL : (a->Halting ? 2UL : 0UL);
        if (r == NET_FULL && reset == 0 && !down) {
            NET_NEXT(p) = a->TxHead;
            a->TxHead = p;
            if (a->TxTail == NULL) {
                a->TxTail = p;
            }
            a->TxServing = 0;
            a->TxActive--;
            if (a->TxKick) {
                a->TxKick = 0;
                NdisReleaseSpinLock(&a->TxLock);
                continue;
            }
            a->TxFullWaits++;
            NdisReleaseSpinLock(&a->TxLock);
            return;
        }
        NdisReleaseSpinLock(&a->TxLock);

        if (r == NET_SENT) {
            status = NDIS_STATUS_SUCCESS;
        } else if (r == NET_FULL) {
            /* reset: 1 a reset's gate, 2 a halt, 0 the link down. */
            status = (reset == 1) ? NDIS_STATUS_RESET_IN_PROGRESS
                     : (reset == 2) ? NDIS_STATUS_FAILURE : NET_STATUS_DOWN;
        } else if (r == NET_NOMAP) {
            status = NDIS_STATUS_RESOURCES;
        } else {
            status = NDIS_STATUS_FAILURE;
        }
        NdisMSendComplete(a->Handle, p, status);

        NdisAcquireSpinLock(&a->TxLock);
        a->TxServing = 0;
        if (netTxEnd(a)) {
            return;
        }
    }
}

/* MiniportSendPackets. IRQL: <= DISPATCH_LEVEL. */
VOID NetSendPackets(NDIS_HANDLE ctx, PPNDIS_PACKET packets, UINT count)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    UINT i;

    NdisAcquireSpinLock(&a->TxLock);
    for (i = 0; i < count; i++) {
        PNDIS_PACKET p = packets[i];

        NDIS_SET_PACKET_STATUS(p, NDIS_STATUS_PENDING);
        NET_NEXT(p) = NULL;
        if (a->TxTail == NULL) {
            a->TxHead = p;
        } else {
            NET_NEXT(a->TxTail) = p;
        }
        a->TxTail = p;
    }
    NdisReleaseSpinLock(&a->TxLock);
    NetServe(a);
}

/*
 * MiniportReset (record 35 section 7.5): nothing below is reset. The gate
 * closes (no sender starts), the queue is completed with
 * NDIS_STATUS_RESET_IN_PROGRESS, counted in TxActive like any completion,
 * and the reset completes when the last context completing sends is done:
 * here, returning success, when that is this drain, else by
 * NdisMResetComplete from that last context. So no packet sent before the
 * reset is completed after it. IRQL: <= DISPATCH_LEVEL.
 */
NDIS_STATUS NetReset(PBOOLEAN addressingReset, NDIS_HANDLE ctx)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    PNDIS_PACKET list;
    ULONG pending;

    *addressingReset = FALSE;
    NdisAcquireSpinLock(&a->TxLock);
    a->ResetGate = 1;
    list = netDetach(a);
    a->TxActive++;
    NdisReleaseSpinLock(&a->TxLock);
    netCompleteList(a, list, NDIS_STATUS_RESET_IN_PROGRESS);

    NdisAcquireSpinLock(&a->TxLock);
    a->TxActive--;
    pending = (a->TxActive != 0) ? 1UL : 0UL;
    if (pending) {
        a->ResetPending = 1;
    } else {
        a->ResetGate = 0;
    }
    NdisReleaseSpinLock(&a->TxLock);
    NetCmLog(a, "net.reset", pending);
    if (pending) {
        return NDIS_STATUS_PENDING;
    }
    NetServe(a);
    return NDIS_STATUS_SUCCESS;
}

/* The connection manager's TxDone. IRQL: DISPATCH_LEVEL. */
VOID NTAPI NetCbTxDone(PVOID ctx, ULONG count)
{
    UNREFERENCED_PARAMETER(count);
    NetServe((PNET_ADAPTER)ctx);
}

/* ------------------------------------------------------------------ */
/* Receive                                                             */
/* ------------------------------------------------------------------ */

/* IRQL: PASSIVE_LEVEL (MiniportInitialize). */
NDIS_STATUS NetRxAlloc(PNET_ADAPTER a)
{
    NDIS_PHYSICAL_ADDRESS high = NDIS_PHYSICAL_ADDRESS_CONST(-1, -1);
    NDIS_STATUS status;
    ULONG i;

    a->RxPackets = NET_RX_PACKETS_SMALL;
    if (a->MaxFrame > NET_RX_LARGE_FROM) {
        a->RxPackets = NET_RX_PACKETS_LARGE;
    }
    status = NdisAllocateMemory((PVOID *)&a->RxAsm, NET_PACKET_MAX, 0,
                                high);
    if (status != NDIS_STATUS_SUCCESS) {
        a->RxAsm = NULL;
        return NDIS_STATUS_RESOURCES;
    }
    NetRxInit(&a->Rx, a->RxAsm, NET_PACKET_MAX);
    NdisAllocatePacketPool(&status, &a->PacketPool, a->RxPackets,
                           PROTOCOL_RESERVED_SIZE_IN_PACKET);
    if (status != NDIS_STATUS_SUCCESS) {
        a->PacketPool = NULL;
        return NDIS_STATUS_RESOURCES;
    }
    NdisAllocateBufferPool(&status, &a->BufferPool, a->RxPackets);
    if (status != NDIS_STATUS_SUCCESS) {
        a->BufferPool = NULL;
        return NDIS_STATUS_RESOURCES;
    }
    for (i = 0; i < a->RxPackets; i++) {
        PNDIS_PACKET p;
        PNDIS_BUFFER b;

        status = NdisAllocateMemory((PVOID *)&a->RxStore[i], a->MaxFrame,
                                    0, high);
        if (status != NDIS_STATUS_SUCCESS) {
            a->RxStore[i] = NULL;
            return NDIS_STATUS_RESOURCES;
        }
        NdisAllocatePacket(&status, &p, a->PacketPool);
        if (status != NDIS_STATUS_SUCCESS) {
            return NDIS_STATUS_RESOURCES;
        }
        NdisAllocateBuffer(&status, &b, a->BufferPool, a->RxStore[i],
                           a->MaxFrame);
        if (status != NDIS_STATUS_SUCCESS) {
            NdisFreePacket(p);
            return NDIS_STATUS_RESOURCES;
        }
        NdisChainBufferAtFront(p, b);
        NDIS_SET_PACKET_HEADER_SIZE(p, NET_ETH_HDR);
        a->RxAll[i] = p;
        a->RxFree[a->RxFreeCount++] = p;
    }
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL; every packet returned. */
VOID NetRxFree(PNET_ADAPTER a)
{
    ULONG i;
    PNDIS_BUFFER b;

    for (i = 0; i < NET_RX_PACKETS_MAX; i++) {
        if (a->RxAll[i] != NULL) {
            NdisUnchainBufferAtFront(a->RxAll[i], &b);
            if (b != NULL) {
                NdisFreeBuffer(b);
            }
            NdisFreePacket(a->RxAll[i]);
            a->RxAll[i] = NULL;
        }
        if (a->RxStore[i] != NULL) {
            NdisFreeMemory(a->RxStore[i], a->MaxFrame, 0);
            a->RxStore[i] = NULL;
        }
    }
    a->RxFreeCount = 0;
    if (a->BufferPool != NULL) {
        NdisFreeBufferPool(a->BufferPool);
        a->BufferPool = NULL;
    }
    if (a->PacketPool != NULL) {
        NdisFreePacketPool(a->PacketPool);
        a->PacketPool = NULL;
    }
    if (a->RxAsm != NULL) {
        NdisFreeMemory(a->RxAsm, NET_PACKET_MAX, 0);
        a->RxAsm = NULL;
    }
}

/* Indicates one Ethernet frame, copied into a packet of the pool. When
 * the pool runs low the packet goes up with NDIS_STATUS_RESOURCES, so
 * the protocols copy it and it is back on return, and a long split never
 * empties the pool. IRQL: DISPATCH_LEVEL (the receive callback). */
static VOID netIndicate(PNET_ADAPTER a, const UCHAR *data, ULONG len)
{
    PNDIS_PACKET p;
    PNDIS_BUFFER b;
    PVOID va;
    UINT blen;
    ULONG low;

    NdisAcquireSpinLock(&a->RxLock);
    if (a->RxFreeCount == 0) {
        NdisReleaseSpinLock(&a->RxLock);
        a->RxNoBuffer++;
        return;
    }
    p = a->RxFree[--a->RxFreeCount];
    low = (a->RxFreeCount < a->RxPackets / 4) ? 1UL : 0UL;
    a->RxOut++;
    NdisReleaseSpinLock(&a->RxLock);

    NdisQueryPacket(p, NULL, NULL, &b, NULL);
    NdisQueryBuffer(b, &va, &blen);
    NetCopy((PUCHAR)va, data, len);
    NdisAdjustBufferLength(b, len);
    NdisRecalculatePacketCounts(p);
    NDIS_SET_PACKET_HEADER_SIZE(p, NET_ETH_HDR);
    NDIS_SET_PACKET_STATUS(p, low ? NDIS_STATUS_RESOURCES
                                  : NDIS_STATUS_SUCCESS);
    a->RxOk++;
    a->RxBytes += len;
    NdisMIndicateReceivePacket(a->Handle, &p, 1);
    if (low) {
        NetReturnPacket(a, p);
    }
}

/* MiniportReturnPacket, and the indication's own return of a packet sent
 * up with NDIS_STATUS_RESOURCES. IRQL: <= DISPATCH_LEVEL. */
VOID NetReturnPacket(NDIS_HANDLE ctx, PNDIS_PACKET packet)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    PNDIS_BUFFER b;

    NdisQueryPacket(packet, NULL, NULL, &b, NULL);
    NdisAdjustBufferLength(b, a->MaxFrame);
    NdisRecalculatePacketCounts(packet);
    /* The halt waits for RxDrained and then takes RxLock once, so the
     * event is set with the lock held and nothing of the adapter is
     * touched after its release. */
    NdisAcquireSpinLock(&a->RxLock);
    a->RxFree[a->RxFreeCount++] = packet;
    a->RxOut--;
    if (a->RxOut == 0 && a->Halting) {
        (VOID)KeSetEvent(&a->RxDrained, IO_NO_INCREMENT, FALSE);
    }
    NdisReleaseSpinLock(&a->RxLock);
}

/* An assembled frame: passed up within the maximum, else split (section
 * 7.4) or dropped. IRQL: DISPATCH_LEVEL. */
static VOID netDeliver(PNET_ADAPTER a, const UCHAR *pkt, ULONG len)
{
    ULONG why;
    ULONG n;
    PNDIS_PACKET p;
    PNDIS_BUFFER b;
    PVOID va;
    UINT blen;
    ULONG low;

    if (len <= a->MaxFrame) {
        netIndicate(a, pkt, len);
        return;
    }
    if (!NetSegBegin(&a->Seg, pkt, len, a->MaxFrame, &why)) {
        a->SegDrops[why]++;
        return;
    }
    a->RxSplit++;
    for (;;) {
        NdisAcquireSpinLock(&a->RxLock);
        if (a->RxFreeCount == 0) {
            NdisReleaseSpinLock(&a->RxLock);
            a->RxNoBuffer++;
            return;
        }
        p = a->RxFree[--a->RxFreeCount];
        low = (a->RxFreeCount < a->RxPackets / 4) ? 1UL : 0UL;
        a->RxOut++;
        NdisReleaseSpinLock(&a->RxLock);

        NdisQueryPacket(p, NULL, NULL, &b, NULL);
        NdisQueryBuffer(b, &va, &blen);
        n = NetSegNext(&a->Seg, (PUCHAR)va);
        if (n == 0) {
            NetReturnPacket(a, p);
            return;
        }
        NdisAdjustBufferLength(b, n);
        NdisRecalculatePacketCounts(p);
        NDIS_SET_PACKET_HEADER_SIZE(p, NET_ETH_HDR);
        NDIS_SET_PACKET_STATUS(p, low ? NDIS_STATUS_RESOURCES
                                      : NDIS_STATUS_SUCCESS);
        a->RxOk++;
        a->RxBytes += n;
        NdisMIndicateReceivePacket(a->Handle, &p, 1);
        if (low) {
            NetReturnPacket(a, p);
        }
    }
}

/* The connection manager's Receive: one USB4 frame, valid until this
 * returns. IRQL: DISPATCH_LEVEL, one at a time. */
VOID NTAPI NetCbReceive(PVOID ctx, const VOID *frame, ULONG length,
                        ULONG flags)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    const UCHAR *pkt;
    ULONG len;
    ULONG why;
    ULONG gen = a->LinkGen;

    if (gen != a->RxSeenGen) {
        a->RxSeenGen = gen;
        NetRxReset(&a->Rx);
    }
    if (!a->LinkUp || a->Halting || a->PowerD3) {
        a->RxNotUp++;
        return;
    }
    /* XHCI_CM_NET_RX_CRC_ERROR and _OVERFLOW are the core's bits. */
    if (NetRxFrame(&a->Rx, (const UCHAR *)frame, length,
                   flags & (NET_RXF_CRC_ERROR | NET_RXF_OVERFLOW), &pkt,
                   &len, &why) ==
        NET_RX_PACKET) {
        netDeliver(a, pkt, len);
    }
}

/* ------------------------------------------------------------------ */
/* Link                                                                */
/* ------------------------------------------------------------------ */

/* The connection manager's LinkChange (section 7.6). IRQL: PASSIVE_LEVEL,
 * on the connection manager's thread. */
VOID NTAPI NetCbLinkChange(PVOID ctx, BOOLEAN up)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    NET_CM_INFO info;

    if (NetCmQuery(a, &info) && up && NetCmIfMacMoved(a->Mac, &info)) {
        NetCmLog(a, "net.mac.moved",
                 ((ULONG)info.Mac[4] << 8) | (ULONG)info.Mac[5]);
    }
    NdisAcquireSpinLock(&a->TxLock);
    a->LinkGen++;
    a->PeerStns = info.PeerPrtcstns;
    a->RateMbps = NetCmIfRateMbps(&info);
    a->LinkUp = up ? 1UL : 0UL;
    a->BigLogged = 0;
    NdisReleaseSpinLock(&a->TxLock);
    NetCmLog(a, up ? "net.link.up" : "net.link.down", info.PeerPrtcstns);
    if (!up) {
        NetServe(a);
    }
    NdisMIndicateStatus(a->Handle, up ? NDIS_STATUS_MEDIA_CONNECT
                                      : NDIS_STATUS_MEDIA_DISCONNECT,
                        NULL, 0);
    NdisMIndicateStatusComplete(a->Handle);
}
