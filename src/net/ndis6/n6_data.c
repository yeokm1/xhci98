/*
 * n6_data.c - xhcinet6.sys's data path (design record 35 sections 7.2 to
 * 7.6): the send queue of net buffer lists and its one sender, the pause's
 * drain, the receive callback's reassembly, segmentation and indication,
 * and the link.
 *
 * Lock (xhcinet6.h) covers the queue, the sender's flags, the pause state,
 * the link view and the receive pool's free list; it is never held across
 * a call into NDIS's completion, indication or status routines or into
 * the connection manager. NET_RX and NET_SEG are the receive callback's
 * alone, which the connection manager runs in one DPC; NET_TX the one
 * sender's.
 */

#include "xhcinet6.h"

/* A queued list's next net buffer to send, and its completion status. */
#define NET6_CUR_NB(l)  (*(PNET_BUFFER *)&NET_BUFFER_LIST_MINIPORT_RESERVED(l)[0])
#define NET6_STATUS(l)  (*(NDIS_STATUS *)&NET_BUFFER_LIST_MINIPORT_RESERVED(l)[1])

/* IRQL: any. */
VOID Net6Copy(PUCHAR dst, const UCHAR *src, ULONG n)
{
    while (n >= 4 && (((ULONG_PTR)dst | (ULONG_PTR)src) & 3) == 0) {
        *(ULONG UNALIGNED *)dst = *(const ULONG UNALIGNED *)src;
        dst += 4;
        src += 4;
        n -= 4;
    }
    while (n != 0) {
        *dst = *src;
        dst++;
        src++;
        n--;
    }
}

/* Through a volatile pointer: WDK 7.1's x86 compiler otherwise turns the
 * loop into a call to memset, a C runtime import. IRQL: any. */
VOID Net6Zero(PVOID dst, ULONG n)
{
    volatile UCHAR *p = (volatile UCHAR *)dst;

    while (n != 0) {
        *p = 0;
        p++;
        n--;
    }
}

/* Mbit/s to bit/s in a ULONG64 field, by 32-bit steps alone (the tree's
 * arithmetic rule): mbps * 1000 fits 32 bits below 4,294,967 Mbit/s,
 * and the second * 1000 is done in 16-bit halves. 0 stays 0, which NDIS
 * reads as unknown only through NDIS_LINK_SPEED_UNKNOWN, so the callers
 * pass a known rate. IRQL: any. */
VOID Net6Bps(ULONG mbps, ULONG64 *bps)
{
    ULARGE_INTEGER r;
    ULONG x;
    ULONG lo;
    ULONG hi;

    if (mbps > 4000000UL) {
        mbps = 4000000UL;
    }
    x = mbps * 1000UL;
    lo = (x & 0xFFFFUL) * 1000UL;
    hi = (x >> 16) * 1000UL;
    r.LowPart = lo + (hi << 16);
    r.HighPart = (hi >> 16) + ((r.LowPart < lo) ? 1UL : 0UL);
    ((PULARGE_INTEGER)bps)->LowPart = r.LowPart;
    ((PULARGE_INTEGER)bps)->HighPart = r.HighPart;
}

/* A 64-bit counter as a low and a high ULONG, by carry (the arithmetic
 * rule). Lock held, as by Net6Read64, so a reader never sees half a
 * carry. IRQL: <= DISPATCH_LEVEL. */
VOID Net6Count64(PULONG lo, PULONG hi, ULONG n)
{
    ULONG v = *lo + n;

    if (v < n) {
        *hi = *hi + 1;
    }
    *lo = v;
}

/* The pair into a ULONG64 field; Lock held. IRQL: <= DISPATCH_LEVEL. */
VOID Net6Read64(const ULONG *lo, const ULONG *hi, ULONG64 *out)
{
    ((PULARGE_INTEGER)out)->LowPart = *lo;
    ((PULARGE_INTEGER)out)->HighPart = *hi;
}

/* ------------------------------------------------------------------ */
/* The pause's drain                                                   */
/* ------------------------------------------------------------------ */

/* Lock held. 1 when a pending pause may complete now: nothing queued,
 * no context completing sends, no indication outstanding. The state
 * moves to PAUSED here, once. */
static ULONG net6PauseDrained(PNET6_ADAPTER a)
{
    if (!a->PausePending || a->TxHead != NULL || a->TxActive != 0 ||
        a->RxOut != 0) {
        return 0;
    }
    a->PausePending = 0;
    a->State = NET6_PAUSED;
    return 1;
}

/*
 * The end of one context's completion work (TxActive raised under Lock
 * when it took its lists): Lock held on entry, released here, and the
 * pause completed when this was the last thing it waited for.
 * IRQL: <= DISPATCH_LEVEL.
 */
static VOID net6TxEnd(PNET6_ADAPTER a)
{
    ULONG done;

    a->TxActive--;
    done = net6PauseDrained(a);
    NdisReleaseSpinLock(&a->Lock);
    if (done) {
        Net6CmLog(a, "net.pause.done", 0);
        NdisMPauseComplete(a->Handle);
    }
}

/* ------------------------------------------------------------------ */
/* Send                                                                */
/* ------------------------------------------------------------------ */

/* Completes a chain of lists with status, one call, no lock held.
 * IRQL: <= DISPATCH_LEVEL. */
static VOID net6CompleteList(PNET6_ADAPTER a, PNET_BUFFER_LIST list,
                             NDIS_STATUS status)
{
    PNET_BUFFER_LIST l;

    if (list == NULL) {
        return;
    }
    for (l = list; l != NULL; l = NET_BUFFER_LIST_NEXT_NBL(l)) {
        NET_BUFFER_LIST_STATUS(l) = status;
    }
    NdisMSendNetBufferListsComplete(a->Handle, list, 0);
}

/* Detaches the whole queue; Lock held. */
static PNET_BUFFER_LIST net6Detach(PNET6_ADAPTER a)
{
    PNET_BUFFER_LIST list = a->TxHead;

    a->TxHead = NULL;
    a->TxTail = NULL;
    return list;
}

/* Puts a list back at the head; Lock held. */
static VOID net6PushHead(PNET6_ADAPTER a, PNET_BUFFER_LIST l)
{
    NET_BUFFER_LIST_NEXT_NBL(l) = a->TxHead;
    a->TxHead = l;
    if (a->TxTail == NULL) {
        a->TxTail = l;
    }
}

/* What a stopped queue completes with: paused, else the link down. */
static NDIS_STATUS net6StopStatus(PNET6_ADAPTER a)
{
    if (a->State != NET6_RUNNING) {
        return NDIS_STATUS_PAUSED;
    }
    return NDIS_STATUS_MEDIA_DISCONNECTED;
}

/* Lock held: the queue may be served. */
static ULONG net6CanSend(PNET6_ADAPTER a)
{
    return (a->State == NET6_RUNNING && a->LinkUp && a->CmOpen &&
            !a->Removed && !a->PowerD3) ? 1UL : 0UL;
}

#define NET6_SENT       0UL
#define NET6_FULL       1UL
#define NET6_FAILED     2UL

/* Frames one net buffer (one Ethernet frame) into a reservation and
 * posts it (section 7.2). IRQL: <= DISPATCH_LEVEL; the queue's one
 * sender. */
static ULONG net6SendOne(PNET6_ADAPTER a, PNET_BUFFER nb, ULONG stns)
{
    PUCHAR bufs[NET_FRAMES_MAX];
    ULONG lens[NET_FRAMES_MAX];
    XHCI_CM_NET_TOKEN token;
    PMDL mdl;
    ULONG off;
    ULONG total;
    ULONG m;
    ULONG k;
    ULONG id;
    ULONG why;
    ULONG left;
    ULONG take;
    ULONG have;
    PUCHAR dst;
    const UCHAR *src;

    total = NET_BUFFER_DATA_LENGTH(nb);
    m = NetTxFrames(total, a->MaxFrame, stns, &why);
    if (m == 0) {
        a->TxDrops[why]++;
        if (why == NET_TXDROP_PEER && !a->BigLogged) {
            a->BigLogged = 1;
            Net6CmLog(a, "net.peer.nobig", total);
        }
        return NET6_FAILED;
    }
    if (!NetCmIfReserve(&a->Cm, m, bufs, &token)) {
        return NET6_FULL;
    }
    id = NetTxPacketId(&a->Tx, stns);

    mdl = NET_BUFFER_CURRENT_MDL(nb);
    off = NET_BUFFER_CURRENT_MDL_OFFSET(nb);
    src = NULL;
    have = 0;
    for (k = 0; k < m; k++) {
        lens[k] = NetTxHeader(bufs[k], total, k, m, id);
        left = lens[k] - NET_HDR_LEN;
        dst = bufs[k] + NET_HDR_LEN;
        while (left != 0) {
            while (have == 0) {
                PVOID va;
                ULONG blen;

                if (mdl == NULL) {
                    NetCmIfAbort(&a->Cm, &token);
                    a->TxErrors++;
                    return NET6_FAILED;
                }
                va = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority);
                blen = MmGetMdlByteCount(mdl);
                if (va == NULL || off > blen) {
                    NetCmIfAbort(&a->Cm, &token);
                    a->TxErrors++;
                    return NET6_FAILED;
                }
                src = (const UCHAR *)va + off;
                have = blen - off;
                off = 0;
                mdl = mdl->Next;
            }
            take = left;
            if (take > have) {
                take = have;
            }
            Net6Copy(dst, src, take);
            dst += take;
            src += take;
            have -= take;
            left -= take;
        }
    }
    if (!NetCmIfCommit(&a->Cm, &token, m, lens)) {
        a->TxErrors++;
        return NET6_FAILED;
    }
    a->TxOk++;
    NdisAcquireSpinLock(&a->Lock);
    Net6Count64(&a->TxBytes, &a->TxBytesHi, total);
    NdisReleaseSpinLock(&a->Lock);
    return NET6_SENT;
}

/*
 * Serves the queue (record 35 section 7.5): one sender at a time
 * (TxServing, held until its list is completed), the head list taken off
 * the queue, its next net buffer framed and committed with Lock released,
 * the list put back at the head while it has more, and completed with no
 * lock held once its last is done. Every context that completes sends
 * counts itself in TxActive from the moment it takes them until they are
 * completed, so a pause completes only after the last. A full ring puts
 * the list back at the head until TxDone serves again; work that arrives
 * while a sender serves sets TxKick, which the sender reads under the
 * lock before it stops. With the adapter not Running, the link down, the
 * service closed or the device removed, the queue completes at once.
 * IRQL: <= DISPATCH_LEVEL.
 */
VOID Net6Serve(PNET6_ADAPTER a)
{
    PNET_BUFFER_LIST l;
    PNET_BUFFER_LIST list;
    PNET_BUFFER nb;
    NDIS_STATUS status;
    ULONG stns;
    ULONG r;
    ULONG stop;

    for (;;) {
        NdisAcquireSpinLock(&a->Lock);
        if (a->TxServing) {
            a->TxKick = 1;
            NdisReleaseSpinLock(&a->Lock);
            return;
        }
        l = a->TxHead;
        if (l == NULL) {
            NdisReleaseSpinLock(&a->Lock);
            return;
        }
        if (!net6CanSend(a)) {
            status = net6StopStatus(a);
            list = net6Detach(a);
            a->TxDown++;
            a->TxActive++;
            NdisReleaseSpinLock(&a->Lock);
            net6CompleteList(a, list, status);
            NdisAcquireSpinLock(&a->Lock);
            net6TxEnd(a);
            continue;
        }
        a->TxHead = NET_BUFFER_LIST_NEXT_NBL(l);
        if (a->TxHead == NULL) {
            a->TxTail = NULL;
        }
        NET_BUFFER_LIST_NEXT_NBL(l) = NULL;
        a->TxServing = 1;
        a->TxActive++;
        a->TxKick = 0;
        stns = a->PeerStns;
        NdisReleaseSpinLock(&a->Lock);

        nb = NET6_CUR_NB(l);
        r = (nb == NULL) ? NET6_SENT : net6SendOne(a, nb, stns);

        NdisAcquireSpinLock(&a->Lock);
        if (r == NET6_FULL) {
            net6PushHead(a, l);
            a->TxServing = 0;
            stop = (net6CanSend(a) && !a->TxKick) ? 1UL : 0UL;
            a->TxKick = 0;
            if (stop) {
                a->TxFullWaits++;
            }
            net6TxEnd(a);
            if (stop) {
                return;
            }
            continue;
        }
        if (r == NET6_FAILED) {
            NET6_STATUS(l) = NDIS_STATUS_FAILURE;
        }
        if (nb != NULL && NET_BUFFER_NEXT_NB(nb) != NULL) {
            NET6_CUR_NB(l) = NET_BUFFER_NEXT_NB(nb);
            net6PushHead(a, l);
            a->TxServing = 0;
            net6TxEnd(a);
            continue;
        }
        NdisReleaseSpinLock(&a->Lock);

        net6CompleteList(a, l, NET6_STATUS(l));

        NdisAcquireSpinLock(&a->Lock);
        a->TxServing = 0;
        net6TxEnd(a);
    }
}

/* MiniportSendNetBufferLists: every list queued, then served (with the
 * adapter not Running the serve completes them NDIS_STATUS_PAUSED).
 * IRQL: <= DISPATCH_LEVEL. */
VOID Net6SendNbls(NDIS_HANDLE ctx, PNET_BUFFER_LIST nbls,
                  NDIS_PORT_NUMBER port, ULONG flags)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;
    PNET_BUFFER_LIST l;
    PNET_BUFFER_LIST next;

    UNREFERENCED_PARAMETER(port);
    UNREFERENCED_PARAMETER(flags);

    NdisAcquireSpinLock(&a->Lock);
    for (l = nbls; l != NULL; l = next) {
        next = NET_BUFFER_LIST_NEXT_NBL(l);
        NET_BUFFER_LIST_NEXT_NBL(l) = NULL;
        NET6_CUR_NB(l) = NET_BUFFER_LIST_FIRST_NB(l);
        NET6_STATUS(l) = NDIS_STATUS_SUCCESS;
        if (a->TxTail == NULL) {
            a->TxHead = l;
        } else {
            NET_BUFFER_LIST_NEXT_NBL(a->TxTail) = l;
        }
        a->TxTail = l;
    }
    NdisReleaseSpinLock(&a->Lock);
    Net6Serve(a);
}

/* MiniportCancelSend: every queued list of that cancel id completed
 * NDIS_STATUS_SEND_ABORTED; the one a sender holds finishes as it is.
 * IRQL: <= DISPATCH_LEVEL. */
VOID Net6CancelSend(NDIS_HANDLE ctx, PVOID cancelId)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;
    PNET_BUFFER_LIST l;
    PNET_BUFFER_LIST next;
    PNET_BUFFER_LIST keep = NULL;
    PNET_BUFFER_LIST keepTail = NULL;
    PNET_BUFFER_LIST gone = NULL;
    PNET_BUFFER_LIST goneTail = NULL;

    NdisAcquireSpinLock(&a->Lock);
    for (l = a->TxHead; l != NULL; l = next) {
        next = NET_BUFFER_LIST_NEXT_NBL(l);
        NET_BUFFER_LIST_NEXT_NBL(l) = NULL;
        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(l) == cancelId) {
            if (goneTail == NULL) {
                gone = l;
            } else {
                NET_BUFFER_LIST_NEXT_NBL(goneTail) = l;
            }
            goneTail = l;
        } else {
            if (keepTail == NULL) {
                keep = l;
            } else {
                NET_BUFFER_LIST_NEXT_NBL(keepTail) = l;
            }
            keepTail = l;
        }
    }
    a->TxHead = keep;
    a->TxTail = keepTail;
    if (gone == NULL) {
        NdisReleaseSpinLock(&a->Lock);
        return;
    }
    a->TxActive++;
    NdisReleaseSpinLock(&a->Lock);
    net6CompleteList(a, gone, NDIS_STATUS_SEND_ABORTED);
    NdisAcquireSpinLock(&a->Lock);
    net6TxEnd(a);
}

/* The connection manager's TxDone. IRQL: DISPATCH_LEVEL. */
VOID NTAPI Net6CbTxDone(PVOID ctx, ULONG count)
{
    UNREFERENCED_PARAMETER(count);
    Net6Serve((PNET6_ADAPTER)ctx);
}

/*
 * MiniportPause's work (record 35 section 7.5): no indication starts
 * once the state leaves Running; every queued send completes with
 * NDIS_STATUS_PAUSED; the pause completes now when nothing is in flight
 * and no indicated list is out, else returns NDIS_STATUS_PENDING and the
 * last of them calls NdisMPauseComplete (net6TxEnd, Net6ReturnNbls).
 * The service stays open. IRQL: PASSIVE_LEVEL.
 */
NDIS_STATUS Net6Pause(PNET6_ADAPTER a)
{
    PNET_BUFFER_LIST list;
    ULONG done;

    NdisAcquireSpinLock(&a->Lock);
    a->State = NET6_PAUSING;
    list = net6Detach(a);
    a->TxActive++;
    NdisReleaseSpinLock(&a->Lock);
    net6CompleteList(a, list, NDIS_STATUS_PAUSED);

    NdisAcquireSpinLock(&a->Lock);
    a->TxActive--;
    a->PausePending = 1;
    done = net6PauseDrained(a);
    if (!done) {
        /* Still waited for: the last context's net6TxEnd or return
         * completes it, the state staying PAUSING until then. */
        NdisReleaseSpinLock(&a->Lock);
        Net6CmLog(a, "net.pause.wait", 0);
        return NDIS_STATUS_PENDING;
    }
    NdisReleaseSpinLock(&a->Lock);
    Net6CmLog(a, "net.pause", 0);
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Receive                                                             */
/* ------------------------------------------------------------------ */

/* IRQL: PASSIVE_LEVEL (MiniportInitializeEx). */
NDIS_STATUS Net6RxAlloc(PNET6_ADAPTER a)
{
    NET_BUFFER_LIST_POOL_PARAMETERS pp;
    ULONG i;

    a->RxCount = NET6_RX_SMALL;
    if (a->MaxFrame > NET6_RX_LARGE_FROM) {
        a->RxCount = NET6_RX_LARGE;
    }
    a->RxAsm = (PUCHAR)NdisAllocateMemoryWithTagPriority(
        a->Handle, NET_PACKET_MAX, NET6_TAG, NormalPoolPriority);
    if (a->RxAsm == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    NetRxInit(&a->Rx, a->RxAsm, NET_PACKET_MAX);

    Net6Zero(&pp, sizeof(pp));
    pp.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    pp.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    pp.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    pp.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    pp.fAllocateNetBuffer = TRUE;
    pp.ContextSize = 0;
    pp.PoolTag = NET6_TAG;
    pp.DataSize = 0;
    a->NblPool = NdisAllocateNetBufferListPool(a->Handle, &pp);
    if (a->NblPool == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    for (i = 0; i < a->RxCount; i++) {
        PNET_BUFFER_LIST l;

        a->RxStore[i] = (PUCHAR)NdisAllocateMemoryWithTagPriority(
            a->Handle, a->MaxFrame, NET6_TAG, NormalPoolPriority);
        if (a->RxStore[i] == NULL) {
            return NDIS_STATUS_RESOURCES;
        }
        a->RxMdl[i] = NdisAllocateMdl(a->Handle, a->RxStore[i],
                                      a->MaxFrame);
        if (a->RxMdl[i] == NULL) {
            return NDIS_STATUS_RESOURCES;
        }
        l = NdisAllocateNetBufferAndNetBufferList(a->NblPool, 0, 0,
                                                  a->RxMdl[i], 0,
                                                  a->MaxFrame);
        if (l == NULL) {
            return NDIS_STATUS_RESOURCES;
        }
        l->SourceHandle = a->Handle;
        a->RxAll[i] = l;
        a->RxFree[a->RxFreeCount++] = l;
    }
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL; every list returned (a halt follows a pause). */
VOID Net6RxFree(PNET6_ADAPTER a)
{
    ULONG i;

    for (i = 0; i < NET6_RX_MAX; i++) {
        if (a->RxAll[i] != NULL) {
            NdisFreeNetBufferList(a->RxAll[i]);
            a->RxAll[i] = NULL;
        }
        if (a->RxMdl[i] != NULL) {
            NdisFreeMdl(a->RxMdl[i]);
            a->RxMdl[i] = NULL;
        }
        if (a->RxStore[i] != NULL) {
            NdisFreeMemory(a->RxStore[i], 0, 0);
            a->RxStore[i] = NULL;
        }
    }
    a->RxFreeCount = 0;
    if (a->NblPool != NULL) {
        NdisFreeNetBufferListPool(a->NblPool);
        a->NblPool = NULL;
    }
    if (a->RxAsm != NULL) {
        NdisFreeMemory(a->RxAsm, 0, 0);
        a->RxAsm = NULL;
    }
}

/* Takes a free list for an indication while Running; *low says the pool
 * runs low. NULL (counted) when none or not Running. IRQL: DISPATCH. */
static PNET_BUFFER_LIST net6RxTake(PNET6_ADAPTER a, PULONG low)
{
    PNET_BUFFER_LIST l;

    NdisAcquireSpinLock(&a->Lock);
    if (a->State != NET6_RUNNING) {
        NdisReleaseSpinLock(&a->Lock);
        a->RxNotUp++;
        return NULL;
    }
    if (a->RxFreeCount == 0) {
        NdisReleaseSpinLock(&a->Lock);
        a->RxNoBuffer++;
        return NULL;
    }
    l = a->RxFree[--a->RxFreeCount];
    *low = (a->RxFreeCount < a->RxCount / 4) ? 1UL : 0UL;
    a->RxOut++;
    NdisReleaseSpinLock(&a->Lock);
    return l;
}

/* One list back on the free list, its buffer restored to the whole
 * store, and a pending pause completed by the last. IRQL: <= DISPATCH. */
static VOID net6RxPut(PNET6_ADAPTER a, PNET_BUFFER_LIST l)
{
    PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(l);
    ULONG done;

    NdisAdjustMdlLength(NET_BUFFER_CURRENT_MDL(nb), a->MaxFrame);
    NET_BUFFER_DATA_LENGTH(nb) = a->MaxFrame;
    NET_BUFFER_LIST_NEXT_NBL(l) = NULL;
    NdisAcquireSpinLock(&a->Lock);
    a->RxFree[a->RxFreeCount++] = l;
    a->RxOut--;
    done = net6PauseDrained(a);
    NdisReleaseSpinLock(&a->Lock);
    if (done) {
        Net6CmLog(a, "net.pause.done", 1);
        NdisMPauseComplete(a->Handle);
    }
}

/* Indicates one taken list holding len bytes. With the pool low it goes
 * up with NDIS_RECEIVE_FLAGS_RESOURCES, so it is ours again when the
 * call returns, and a long split never empties the pool.
 * IRQL: DISPATCH_LEVEL (the receive callback). */
static VOID net6RxUp(PNET6_ADAPTER a, PNET_BUFFER_LIST l, ULONG len,
                     ULONG low)
{
    PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(l);
    ULONG flags = NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;

    NdisAdjustMdlLength(NET_BUFFER_CURRENT_MDL(nb), len);
    NET_BUFFER_DATA_LENGTH(nb) = len;
    NET_BUFFER_LIST_STATUS(l) = NDIS_STATUS_SUCCESS;
    NET_BUFFER_LIST_NEXT_NBL(l) = NULL;
    if (low) {
        flags |= NDIS_RECEIVE_FLAGS_RESOURCES;
    }
    NdisAcquireSpinLock(&a->Lock);
    a->RxOk++;
    Net6Count64(&a->RxBytes, &a->RxBytesHi, len);
    NdisReleaseSpinLock(&a->Lock);
    NdisMIndicateReceiveNetBufferLists(a->Handle, l,
                                       NDIS_DEFAULT_PORT_NUMBER, 1, flags);
    if (low) {
        net6RxPut(a, l);
    }
}

/* MiniportReturnNetBufferLists. IRQL: <= DISPATCH_LEVEL. */
VOID Net6ReturnNbls(NDIS_HANDLE ctx, PNET_BUFFER_LIST nbls, ULONG flags)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;
    PNET_BUFFER_LIST l;
    PNET_BUFFER_LIST next;

    UNREFERENCED_PARAMETER(flags);
    for (l = nbls; l != NULL; l = next) {
        next = NET_BUFFER_LIST_NEXT_NBL(l);
        net6RxPut(a, l);
    }
}

/* The store of a taken list. */
static PUCHAR net6RxStore(PNET_BUFFER_LIST l)
{
    PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(l);

    return (PUCHAR)MmGetMdlVirtualAddress(NET_BUFFER_CURRENT_MDL(nb));
}

/* An assembled frame: passed up within the maximum, else split (section
 * 7.4) or dropped. IRQL: DISPATCH_LEVEL. */
static VOID net6Deliver(PNET6_ADAPTER a, const UCHAR *pkt, ULONG len)
{
    PNET_BUFFER_LIST l;
    ULONG why;
    ULONG low;
    ULONG n;

    if (len <= a->MaxFrame) {
        l = net6RxTake(a, &low);
        if (l == NULL) {
            return;
        }
        Net6Copy(net6RxStore(l), pkt, len);
        net6RxUp(a, l, len, low);
        return;
    }
    if (!NetSegBegin(&a->Seg, pkt, len, a->MaxFrame, &why)) {
        a->SegDrops[why]++;
        return;
    }
    a->RxSplit++;
    for (;;) {
        l = net6RxTake(a, &low);
        if (l == NULL) {
            return;
        }
        n = NetSegNext(&a->Seg, net6RxStore(l));
        if (n == 0) {
            net6RxPut(a, l);
            return;
        }
        net6RxUp(a, l, n, low);
    }
}

/* The connection manager's Receive: one USB4 frame, valid until this
 * returns. Frames are taken into the reassembly whatever the state, so
 * a packet cut by a pause is counted as the core counts it; only a whole
 * Ethernet frame needs the adapter Running (net6RxTake).
 * IRQL: DISPATCH_LEVEL, one at a time. */
VOID NTAPI Net6CbReceive(PVOID ctx, const VOID *frame, ULONG length,
                         ULONG flags)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;
    const UCHAR *pkt;
    ULONG len;
    ULONG why;
    ULONG gen = a->LinkGen;

    if (gen != a->RxSeenGen) {
        a->RxSeenGen = gen;
        NetRxReset(&a->Rx);
    }
    if (!a->LinkUp || a->PowerD3) {
        a->RxNotUp++;
        return;
    }
    if (NetRxFrame(&a->Rx, (const UCHAR *)frame, length,
                   flags & (XHCI_CM_NET_RX_CRC_ERROR |
                            XHCI_CM_NET_RX_OVERFLOW),
                   &pkt, &len, &why) == NET_RX_PACKET) {
        net6Deliver(a, pkt, len);
    }
}

/* ------------------------------------------------------------------ */
/* Link                                                                */
/* ------------------------------------------------------------------ */

/* One NDIS_STATUS_LINK_STATE of the given view. IRQL: <= DISPATCH. */
static VOID net6IndicateOne(PNET6_ADAPTER a, ULONG up, ULONG mbps)
{
    NDIS_STATUS_INDICATION ind;
    NDIS_LINK_STATE ls;

    Net6Zero(&ls, sizeof(ls));
    ls.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    ls.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    ls.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    ls.MediaDuplexState = MediaDuplexStateFull;
    ls.PauseFunctions = NdisPauseFunctionsUnsupported;
    if (up) {
        ls.MediaConnectState = MediaConnectStateConnected;
        Net6Bps(mbps != 0 ? mbps : 10000UL, &ls.XmitLinkSpeed);
        ls.RcvLinkSpeed = ls.XmitLinkSpeed;
    } else {
        ls.MediaConnectState = MediaConnectStateDisconnected;
        ls.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        ls.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }

    Net6Zero(&ind, sizeof(ind));
    ind.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    ind.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    ind.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    ind.SourceHandle = a->Handle;
    ind.StatusCode = NDIS_STATUS_LINK_STATE;
    ind.StatusBuffer = &ls;
    ind.StatusBufferSize = sizeof(ls);
    NdisMIndicateStatusEx(a->Handle, &ind);
}

/*
 * NDIS_STATUS_LINK_STATE with the adapter's view (section 7.6), called
 * only once the adapter has been restarted at least once (the status
 * indication's contract excludes MiniportInitializeEx). One context
 * publishes at a time: a caller that finds another publishing marks the
 * view changed and leaves, and the publisher indicates again until the
 * view it indicated is the latest, so a stale view never lands last.
 * IRQL: <= DISPATCH_LEVEL.
 */
VOID Net6IndicateLink(PNET6_ADAPTER a)
{
    ULONG up;
    ULONG mbps;

    NdisAcquireSpinLock(&a->Lock);
    if (a->Indicating) {
        a->IndicateAgain = 1;
        NdisReleaseSpinLock(&a->Lock);
        return;
    }
    a->Indicating = 1;
    for (;;) {
        a->IndicateAgain = 0;
        up = a->LinkUp;
        mbps = a->RateMbps;
        NdisReleaseSpinLock(&a->Lock);

        net6IndicateOne(a, up, mbps);

        NdisAcquireSpinLock(&a->Lock);
        if (!a->IndicateAgain) {
            break;
        }
    }
    a->Indicating = 0;
    NdisReleaseSpinLock(&a->Lock);
}

/*
 * Waits until no send is queued and no context is completing sends (the
 * D3 path, record 35 section 7.7: NDIS 6.20 may set D3 on a Running
 * adapter, so a sender may still hold a list between its commit and its
 * completion). The link is down by then, so every list queued meanwhile
 * completes at once. IRQL: PASSIVE_LEVEL.
 */
VOID Net6DrainSends(PNET6_ADAPTER a)
{
    ULONG busy;

    for (;;) {
        Net6Serve(a);
        NdisAcquireSpinLock(&a->Lock);
        busy = (a->TxServing || a->TxActive != 0 || a->TxHead != NULL) ?
               1UL : 0UL;
        NdisReleaseSpinLock(&a->Lock);
        if (!busy) {
            return;
        }
        NdisMSleep(1000);
    }
}

/* The link taken down by this side (a Close at D3, a surprise removal):
 * the view down, the receive state of the old link dropped at the next
 * frame, and the queue completed. IRQL: <= DISPATCH_LEVEL. */
VOID Net6LinkDown(PNET6_ADAPTER a)
{
    NdisAcquireSpinLock(&a->Lock);
    a->LinkUp = 0;
    a->LinkGen++;
    NdisReleaseSpinLock(&a->Lock);
    Net6Serve(a);
}

/* The connection manager's LinkChange (section 7.6). The first may come
 * from inside Open, before MiniportInitializeEx returns: the view is
 * recorded and the indication left to MiniportRestart, which indicates
 * the state it finds. IRQL: PASSIVE_LEVEL, on the connection manager's
 * thread. */
VOID NTAPI Net6CbLinkChange(PVOID ctx, BOOLEAN up)
{
    PNET6_ADAPTER a = (PNET6_ADAPTER)ctx;
    XHCI_CM_NET_INFO info;
    ULONG started;

    if (Net6CmQuery(a, &info) && up && NetCmIfMacMoved(a->Mac, &info)) {
        Net6CmLog(a, "net.mac.moved",
                  ((ULONG)info.Mac[4] << 8) | (ULONG)info.Mac[5]);
    }
    NdisAcquireSpinLock(&a->Lock);
    a->LinkGen++;
    a->PeerStns = info.PeerPrtcstns;
    a->RateMbps = NetCmIfRateMbps(&info);
    a->LinkUp = up ? 1UL : 0UL;
    a->BigLogged = 0;
    started = (a->State != NET6_PAUSED || a->Restarted) ? 1UL : 0UL;
    NdisReleaseSpinLock(&a->Lock);
    Net6CmLog(a, up ? "net.link.up" : "net.link.down", info.PeerPrtcstns);
    if (!up) {
        Net6Serve(a);
    }
    if (started) {
        Net6IndicateLink(a);
    }
}
