/*
 * n5_data.c - xhcinet.sys's data path (design record 35 sections 7.2 to
 * 7.6 and 7.8): the send queue and its one sender, the reset, the receive
 * callback's reassembly, segmentation and indication, the link, and on
 * 98 SE and ME the deferral of every upcall to an NDIS timer's routine.
 *
 * Locks: TxLock over the send queue, the sender's flags and the reset's;
 * RxLock over the receive packets' free list and RxOut; UpLock (98 SE
 * and ME) over the deferred queues and the routine's flags, a leaf taken
 * under TxLock or alone. None is held across a call into NDIS's
 * completion or indication routines or into the connection manager. The
 * NET_RX and NET_SEG state is the receive callback's alone, which the
 * connection manager runs in one DPC.
 */

#include "xhcinet.h"

#define NET_NEXT(p) (*(PNDIS_PACKET *)&(p)->MiniportReserved[0])
#if NET_DEFER
#define NET_DONE_STATUS(p) \
    (*(NDIS_STATUS *)&(p)->MiniportReserved[sizeof(PVOID)])
#endif

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
/* The qemu flavour's checks                                           */
/* ------------------------------------------------------------------ */

#if NET_DIAG
/* A breach of the data path's invariants: counted, the first eight
 * logged with their value. IRQL: <= DISPATCH_LEVEL. */
static VOID netDgBad(PNET_ADAPTER a, const char *label, ULONG value)
{
    a->DgBad++;
    if (a->DgBad <= 8) {
        NetCmLog(a, label, value);
    }
}

/* Every 4096 upcalls, the sequence numbers and the counters. IRQL:
 * <= DISPATCH_LEVEL. */
static VOID netDgTick(PNET_ADAPTER a)
{
    ULONG n = a->DgIndSeq + a->DgCmpSeq;

    if ((LONG)(n - a->DgNextLog) < 0) {
        return;
    }
    a->DgNextLog = n + 4096UL;
    NetCmLog(a, "net.dg.ind", a->DgIndSeq);
    NetCmLog(a, "net.dg.cmp", a->DgCmpSeq);
    NetCmLog(a, "net.dg.acc", a->DgAccSeq);
    NetCmLog(a, "net.dg.cbup", a->DgCbUpcalls);
    NetCmLog(a, "net.dg.sendind", a->DgSendInInd);
    NetCmLog(a, "net.dg.bad", a->DgBad);
    NetCmLog(a, "net.dg.full", a->DgFull);
#if NET_DEFER
    NetCmLog(a, "net.dg.upruns", a->Upq.Runs);
#endif
}

/* An upcall about to be made. */
static VOID netDgUpcall(PNET_ADAPTER a)
{
    if (a->DgInCb != 0) {
        a->DgCbUpcalls++;
    }
}

/* A send accepted: tracked until its completion; a packet accepted again
 * before that is a breach. TxLock held. */
static VOID netDgAccept(PNET_ADAPTER a, PNDIS_PACKET p)
{
    ULONG i;
    ULONG slot = NET_DIAG_SENDS;

    a->DgAccSeq++;
    if (a->DgInInd != 0) {
        a->DgSendInInd++;
    }
    for (i = 0; i < NET_DIAG_SENDS; i++) {
        if (a->DgSends[i] == p) {
            netDgBad(a, "net.dg.dupsend", a->DgAccSeq);
            return;
        }
        if (a->DgSends[i] == NULL && slot == NET_DIAG_SENDS) {
            slot = i;
        }
    }
    if (slot == NET_DIAG_SENDS) {
        a->DgFull++;
        return;
    }
    a->DgSends[slot] = p;
}

/* A send about to be completed: one not tracked (a second completion)
 * is a breach while the table never overflowed. No lock held. */
static VOID netDgComplete(PNET_ADAPTER a, PNDIS_PACKET p)
{
    ULONG i;
    ULONG found = 0;

    NdisAcquireSpinLock(&a->TxLock);
    a->DgCmpSeq++;
    for (i = 0; i < NET_DIAG_SENDS; i++) {
        if (a->DgSends[i] == p) {
            a->DgSends[i] = NULL;
            found = 1;
            break;
        }
    }
    NdisReleaseSpinLock(&a->TxLock);
    if (!found && a->DgFull == 0) {
        netDgBad(a, "net.dg.cmpunk", a->DgCmpSeq);
    }
    netDgUpcall(a);
}

/* The receive packet's state, 1 from its take to its return. RxLock
 * held. */
static VOID netDgRxState(PNET_ADAPTER a, PNDIS_PACKET p, ULONG from,
                         ULONG to, const char *label)
{
    ULONG i;

    for (i = 0; i < a->RxPackets; i++) {
        if (a->RxAll[i] == p) {
            break;
        }
    }
    if (i == a->RxPackets) {
        netDgBad(a, label, 0xFFFFFFFFUL);
        return;
    }
    if (a->RxState[i] != from) {
        netDgBad(a, label, i);
    }
    a->RxState[i] = to;
}
#endif

/* ------------------------------------------------------------------ */
/* The upcalls, and their deferral on 98 SE and ME                     */
/* ------------------------------------------------------------------ */

/* One send completion made. IRQL: <= DISPATCH_LEVEL, no lock held. */
static VOID netSendUp(PNET_ADAPTER a, PNDIS_PACKET p, NDIS_STATUS status)
{
#if NET_DIAG
    netDgComplete(a, p);
#endif
    NdisMSendComplete(a->Handle, p, status);
}

/* One receive indication made: a packet of the pool filled with `len`
 * bytes, sent up with NDIS_STATUS_RESOURCES when the pool runs low, so
 * the protocols copy it and it is back on return, and a long split never
 * empties the pool. IRQL: DISPATCH_LEVEL, no lock held. */
static VOID netIndicateUp(PNET_ADAPTER a, PNDIS_PACKET p, ULONG len,
                          ULONG low)
{
    NDIS_SET_PACKET_STATUS(p, low ? NDIS_STATUS_RESOURCES
                                  : NDIS_STATUS_SUCCESS);
#if NET_DIAG
    if (len < NET_ETH_HDR || len > a->MaxFrame) {
        netDgBad(a, "net.dg.len", len);
    }
    a->DgIndSeq++;
    netDgUpcall(a);
    a->DgInInd++;
#else
    UNREFERENCED_PARAMETER(len);
#endif
    NdisMIndicateReceivePacket(a->Handle, &p, 1);
#if NET_DIAG
    a->DgInInd--;
    netDgTick(a);
#endif
    if (low) {
        NetReturnPacket(a, p);
    }
}

#if NET_DEFER
/*
 * Why the upcalls are deferred on 98 SE and ME (record 35 section 7.8):
 * made straight from the connection manager's service DPC, a kernel DPC
 * that NTKERN runs, the indications and completions faulted the 98
 * TCP/IP (exceptions 0E in MSTCP) under sustained traffic with this
 * driver's own state intact, as the usb-wdm-free network drivers' did
 * from their USB completions until the same change there
 * (docs/testing/2026-11-07-98se-tcp.md in that repository). An NDIS
 * miniport timer's routine runs in NDIS's own context. Why the DPC
 * context is unsafe was not read: no Microsoft binary is.
 */

/* The completed sends' link (net_upq.h). */
static PVOID *netUpLink(PVOID item)
{
    return (PVOID *)&((PNDIS_PACKET)item)->MiniportReserved[0];
}

/* Sets the timer the queues asked for: the other one from the last set,
 * since a routine that has just found the queues empty may not have
 * returned yet. UpLock held. */
static VOID netUpSet(PNET_ADAPTER a)
{
    NdisMSetTimer(&a->UpTimer[a->UpNext], 0);
    a->UpNext ^= 1UL;
}

/* An assembled Ethernet frame copied into the ring for the routine, which
 * passes it up (split when longer than the maximum) as the receive
 * callback does elsewhere; refused and counted when the ring is full.
 * The receive packets are taken only by the routine, so a burst between
 * two of its runs costs ring bytes, not packets of the pool. IRQL:
 * DISPATCH_LEVEL (the receive callback). */
static VOID netUpRx(PNET_ADAPTER a, const UCHAR *frame, ULONG len)
{
    ULONG r;

    NdisAcquireSpinLock(&a->UpLock);
    r = NetUpqRx(&a->Upq, frame, len, a->UpGen);
    if (r == 1) {
        netUpSet(a);
    }
    NdisReleaseSpinLock(&a->UpLock);
    if (r == 2) {
        a->RxNoBuffer++;
    }
}

/* A completed send queued for the routine, in order. IRQL:
 * <= DISPATCH_LEVEL. */
static VOID netUpDone(PNET_ADAPTER a, PNDIS_PACKET p, NDIS_STATUS status)
{
    NET_DONE_STATUS(p) = status;
    NdisAcquireSpinLock(&a->UpLock);
    if (NetUpqDone(&a->Upq, p, netUpLink) == 1) {
        netUpSet(a);
    }
    NdisReleaseSpinLock(&a->UpLock);
}

/* The reset's completion, queued behind every send completion queued
 * before it; the reset gate stays closed until the routine has made it.
 * TxLock held (UpLock is taken under it, never the other way). */
static VOID netUpReset(PNET_ADAPTER a)
{
    NdisAcquireSpinLock(&a->UpLock);
    if (NetUpqReset(&a->Upq) == 1) {
        netUpSet(a);
    }
    NdisReleaseSpinLock(&a->UpLock);
}

static VOID netDeliver(PNET_ADAPTER a, const UCHAR *pkt, ULONG len);

/*
 * The routine (an NDIS miniport timer's, DISPATCH_LEVEL): the queued send
 * completions, the oldest queued frame passed up (dropped once a halt or
 * a D3 began), then the reset's completion and the sends gathered behind
 * its gate, until a take finds every queue empty. Until then the queues
 * ask for no timer (a timer set again from inside its own routine is not
 * relied on), and what is queued meanwhile is delivered here. The final
 * release of UpLock is its last access to the adapter.
 */
static VOID netUpcalls(PVOID s1, PVOID ctx, PVOID s2, PVOID s3)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;
    PVOID item;
    PNDIS_PACKET done;
    PNDIS_PACKET next;
    const UCHAR *frame;
    ULONG len;
    ULONG tag;
    ULONG gen;
    ULONG more;
    ULONG reset;

    UNREFERENCED_PARAMETER(s1);
    UNREFERENCED_PARAMETER(s2);
    UNREFERENCED_PARAMETER(s3);
    NdisAcquireSpinLock(&a->UpLock);
    NetUpqEnter(&a->Upq);
    NdisReleaseSpinLock(&a->UpLock);
    for (;;) {
        NdisAcquireSpinLock(&a->UpLock);
        more = NetUpqTake(&a->Upq, &item, &reset, &frame, &len, &tag);
        gen = a->UpGen;
        NdisReleaseSpinLock(&a->UpLock);
        if (!more) {
            return;
        }
        done = (PNDIS_PACKET)item;

        while (done != NULL) {
            next = NET_NEXT(done);
            NET_NEXT(done) = NULL;
            netSendUp(a, done, NET_DONE_STATUS(done));
            done = next;
        }
        if (frame != NULL) {
            /* A frame from before a D3 is never passed up after it. */
            if (tag == gen && !a->Halting && !a->PowerD3) {
                netDeliver(a, frame, len);
            }
            NdisAcquireSpinLock(&a->UpLock);
            NetUpqRxPop(&a->Upq);
            NdisReleaseSpinLock(&a->UpLock);
        }
        if (reset) {
            /* The gate retired before the upcall, as netTxEnd does, so a
             * reset NDIS issues from inside it keeps its own gate. */
            NdisAcquireSpinLock(&a->TxLock);
            a->ResetGate = 0;
            NdisReleaseSpinLock(&a->TxLock);
            NdisMResetComplete(a->Handle, NDIS_STATUS_SUCCESS, FALSE);
            NetCmLog(a, "net.reset.done", 1);
            NetServe(a);
        }
    }
}

/* MiniportInitialize, after NdisMSetAttributesEx: the choice, the lock
 * and, on 98 SE and ME, the frame ring (512 KiB, the most one pass of the
 * connection manager's 128-frame receive ring carries, halved while it
 * cannot be had, down to 128 KiB, which holds the largest frame twice)
 * and the two timers. IRQL: PASSIVE_LEVEL. */
NDIS_STATUS NetUpInit(PNET_ADAPTER a)
{
    NDIS_PHYSICAL_ADDRESS high = NDIS_PHYSICAL_ADDRESS_CONST(-1, -1);
    ULONG size;

    NdisAllocateSpinLock(&a->UpLock);
    a->UpRing = NULL;
    a->UpRingSize = 0;
    NetUpqInit(&a->Upq, NULL, 0);
    a->Defer = IoIsWdmVersionAvailable(1, 0x10) ? 0UL : 1UL;
    if (!a->Defer) {
        return NDIS_STATUS_SUCCESS;
    }
    for (size = NET_UP_RING_MAX; size >= NET_UP_RING_MIN; size /= 2) {
        if (NdisAllocateMemory((PVOID *)&a->UpRing, size, 0, high) ==
            NDIS_STATUS_SUCCESS) {
            a->UpRingSize = size;
            break;
        }
        a->UpRing = NULL;
    }
    if (a->UpRing == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    NetUpqInit(&a->Upq, a->UpRing, a->UpRingSize);
    NdisMInitializeTimer(&a->UpTimer[0], a->Handle, netUpcalls, a);
    NdisMInitializeTimer(&a->UpTimer[1], a->Handle, netUpcalls, a);
    return NDIS_STATUS_SUCCESS;
}

/* The frame ring freed (netFreeAdapter). IRQL: PASSIVE_LEVEL. */
VOID NetUpFree(PNET_ADAPTER a)
{
    if (a->UpRing != NULL) {
        NdisFreeMemory(a->UpRing, a->UpRingSize, 0);
        a->UpRing = NULL;
    }
}

/* Until no timer is set and the routine is not running. IRQL:
 * PASSIVE_LEVEL. */
static VOID netUpWait(PNET_ADAPTER a)
{
    ULONG busy;

    for (;;) {
        NdisAcquireSpinLock(&a->UpLock);
        busy = NetUpqBusy(&a->Upq);
        NdisReleaseSpinLock(&a->UpLock);
        if (!busy) {
            break;
        }
        NdisMSleep(1000);
    }
}

/*
 * A D3's or a halt's quiescence (after Close and the sends quiet): a
 * timer set again for anything queued while delivery was stopped; then,
 * however long it takes, until the routine has made every queued
 * completion and returned and no timer is set, logged as `label` every
 * NET_UP_HALT_MS of waiting; the queues stopped in that same locked step,
 * so no timer is asked for after it; then both timers cancelled and a
 * routine still returning waited for. Nothing is completed here.
 * IRQL: PASSIVE_LEVEL.
 */
static VOID netUpQuiesce(PNET_ADAPTER a, const char *label)
{
    BOOLEAN c0 = FALSE;
    BOOLEAN c1 = FALSE;
    ULONG quiet;
    ULONG waited;

    NdisAcquireSpinLock(&a->UpLock);
    if (NetUpqResume(&a->Upq) == 1) {
        netUpSet(a);
    }
    NdisReleaseSpinLock(&a->UpLock);
    for (waited = 1;; waited++) {
        NdisAcquireSpinLock(&a->UpLock);
        quiet = NetUpqQuiesce(&a->Upq);
        NdisReleaseSpinLock(&a->UpLock);
        if (quiet) {
            break;
        }
        if (waited % NET_UP_HALT_MS == 0) {
            NetCmLog(a, label, waited);
        }
        NdisMSleep(1000);
    }
    NdisMCancelTimer(&a->UpTimer[0], &c0);
    NdisMCancelTimer(&a->UpTimer[1], &c1);
    if (c0 || c1) {
        NdisAcquireSpinLock(&a->UpLock);
        NetUpqCancelled(&a->Upq);
        NdisReleaseSpinLock(&a->UpLock);
    }
    netUpWait(a);
}

/* A D3 asked (OID_PNP_SET_POWER, with PowerD3 set): the frames queued
 * so far are of the old generation and never passed up, even if the
 * routine runs only after the D0. IRQL: <= DISPATCH_LEVEL, TxLock held
 * (UpLock under it). */
VOID NetUpSleep(PNET_ADAPTER a)
{
    if (a->Defer) {
        NdisAcquireSpinLock(&a->UpLock);
        a->UpGen++;
        NdisReleaseSpinLock(&a->UpLock);
    }
}

/* A D3 (the adapter's thread, after the sends are quiet): the request
 * stays pending until the routine has made every queued completion (the
 * frames dropped) and both timers are cancelled, as OID_PNP_SET_POWER
 * asks before a sleep state; delivery stays stopped until NetUpWake.
 * IRQL: PASSIVE_LEVEL. */
VOID NetUpQuiet(PNET_ADAPTER a)
{
    if (a->Defer) {
        netUpQuiesce(a, "net.up.d3.wait");
    }
}

/* The D0 (the adapter's thread, before Open): delivery again, a timer set
 * for anything queued while asleep. IRQL: PASSIVE_LEVEL. */
VOID NetUpWake(PNET_ADAPTER a)
{
    if (!a->Defer) {
        return;
    }
    NdisAcquireSpinLock(&a->UpLock);
    if (NetUpqResume(&a->Upq) == 1) {
        netUpSet(a);
    }
    NdisReleaseSpinLock(&a->UpLock);
}

/*
 * MiniportHalt, after Close and the sends quiet, so nothing more is
 * queued: what is queued goes up from the routine as every upcall here
 * does (the frames dropped, Halting is set), however long that takes -
 * no send is completed from the halt itself - then the timers cancelled
 * and never set again (netUpQuiesce). IRQL: PASSIVE_LEVEL.
 */
VOID NetUpStop(PNET_ADAPTER a)
{
    if (!a->Defer) {
        return;
    }
    netUpQuiesce(a, "net.up.halt.wait");
    NetCmLog(a, "net.up.runs", a->Upq.Runs);
    NetCmLog(a, "net.up.refused", a->Upq.Refused);
}
#endif

/* A send's completion: made now, or on 98 SE and ME queued for the
 * routine. IRQL: <= DISPATCH_LEVEL, no lock held. */
static VOID netSendDone(PNET_ADAPTER a, PNDIS_PACKET p, NDIS_STATUS status)
{
#if NET_DEFER
    if (a->Defer) {
        netUpDone(a, p, status);
        return;
    }
#endif
    netSendUp(a, p, status);
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
        netSendDone(a, list, status);
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
#if NET_DEFER
        if (a->Defer) {
            netUpReset(a);
            continue;
        }
#endif
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
        netSendDone(a, p, status);

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
#if NET_DIAG
        netDgAccept(a, p);
#endif
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
 * NdisMResetComplete from that last context; on 98 SE and ME always by
 * the routine, after the completions queued before it. So no packet sent
 * before the reset is completed after it. IRQL: <= DISPATCH_LEVEL.
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
#if NET_DEFER
    } else if (a->Defer) {
        netUpReset(a);
        pending = 2;
#endif
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
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;

    UNREFERENCED_PARAMETER(count);
#if NET_DIAG
    a->DgInCb++;
#endif
    NetServe(a);
#if NET_DIAG
    a->DgInCb--;
#endif
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

/* A packet of the pool, counted in RxOut, its buffer in *b and *va;
 * NULL when none is free. *low: the pool runs low. IRQL: DISPATCH_LEVEL
 * (the receive callback, or on 98 SE and ME the routine). */
static PNDIS_PACKET netRxTake(PNET_ADAPTER a, PNDIS_BUFFER *b, PVOID *va,
                              PULONG low)
{
    PNDIS_PACKET p;
    UINT blen;

    NdisAcquireSpinLock(&a->RxLock);
    if (a->RxFreeCount == 0) {
        NdisReleaseSpinLock(&a->RxLock);
        a->RxNoBuffer++;
        return NULL;
    }
    p = a->RxFree[--a->RxFreeCount];
    *low = (a->RxFreeCount < a->RxPackets / 4) ? 1UL : 0UL;
    a->RxOut++;
#if NET_DIAG
    netDgRxState(a, p, 0, 1, "net.dg.rxtake");
#endif
    NdisReleaseSpinLock(&a->RxLock);

    NdisQueryPacket(p, NULL, NULL, b, NULL);
    NdisQueryBuffer(*b, va, &blen);
    return p;
}

/* A taken packet filled with `len` bytes, indicated. IRQL: DISPATCH_LEVEL
 * (the receive callback, or on 98 SE and ME the routine). */
static VOID netRxGive(PNET_ADAPTER a, PNDIS_PACKET p, PNDIS_BUFFER b,
                      ULONG len, ULONG low)
{
    NdisAdjustBufferLength(b, len);
    NdisRecalculatePacketCounts(p);
    NDIS_SET_PACKET_HEADER_SIZE(p, NET_ETH_HDR);
    a->RxOk++;
    a->RxBytes += len;
    netIndicateUp(a, p, len, low);
}

/* One Ethernet frame, copied into a packet of the pool and passed up.
 * IRQL: DISPATCH_LEVEL (the receive callback, or the routine). */
static VOID netIndicate(PNET_ADAPTER a, const UCHAR *data, ULONG len)
{
    PNDIS_PACKET p;
    PNDIS_BUFFER b;
    PVOID va;
    ULONG low;

    p = netRxTake(a, &b, &va, &low);
    if (p == NULL) {
        return;
    }
    NetCopy((PUCHAR)va, data, len);
    netRxGive(a, p, b, len, low);
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
#if NET_DIAG
    netDgRxState(a, packet, 1, 0, "net.dg.rxret");
#endif
    a->RxFree[a->RxFreeCount++] = packet;
    a->RxOut--;
    if (a->RxOut == 0 && a->Halting) {
        (VOID)KeSetEvent(&a->RxDrained, IO_NO_INCREMENT, FALSE);
    }
    NdisReleaseSpinLock(&a->RxLock);
}

/* An assembled frame: passed up within the maximum, else split (section
 * 7.4) or dropped. IRQL: DISPATCH_LEVEL (the receive callback, or on 98
 * SE and ME the routine, which alone then uses the NET_SEG state). */
static VOID netDeliver(PNET_ADAPTER a, const UCHAR *pkt, ULONG len)
{
    ULONG why;
    ULONG n;
    PNDIS_PACKET p;
    PNDIS_BUFFER b;
    PVOID va;
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
        p = netRxTake(a, &b, &va, &low);
        if (p == NULL) {
            return;
        }
        n = NetSegNext(&a->Seg, (PUCHAR)va);
        if (n == 0) {
            NetReturnPacket(a, p);
            return;
        }
        netRxGive(a, p, b, n, low);
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
#if NET_DIAG
    a->DgInCb++;
#endif
    /* XHCI_CM_NET_RX_CRC_ERROR and _OVERFLOW are the core's bits. */
    if (NetRxFrame(&a->Rx, (const UCHAR *)frame, length,
                   flags & (NET_RXF_CRC_ERROR | NET_RXF_OVERFLOW), &pkt,
                   &len, &why) ==
        NET_RX_PACKET) {
#if NET_DEFER
        if (a->Defer) {
            netUpRx(a, pkt, len);
        } else
#endif
        netDeliver(a, pkt, len);
    }
#if NET_DIAG
    a->DgInCb--;
#endif
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
#if NET_DIAG
        a->DgInCb++;
#endif
        NetServe(a);
#if NET_DIAG
        a->DgInCb--;
#endif
    }
    NdisMIndicateStatus(a->Handle, up ? NDIS_STATUS_MEDIA_CONNECT
                                      : NDIS_STATUS_MEDIA_DISCONNECT,
                        NULL, 0);
    NdisMIndicateStatusComplete(a->Handle);
}
