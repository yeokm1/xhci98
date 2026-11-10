/*
 * n5_cm.c - xhcinet.sys's use of the connection manager's interface on
 * the network PDO (design record 35 sections 6.3 and 6.4): the table
 * asked once at the miniport's initialisation and held until the halt,
 * through the drivers' shared glue (..\net_cmif.c, which xhcinet6.sys
 * shares); Open, Close and Log here.
 *
 * IRQL: as each function says.
 */

#include "xhcinet.h"

/* IRQL: PASSIVE_LEVEL (MiniportInitialize). */
NDIS_STATUS NetCmAcquire(PNET_ADAPTER a)
{
    ULONG route;
    NTSTATUS status = NetCmIfAcquire(a->Lower, &a->Cm, &route);

    if (status == STATUS_INSUFFICIENT_RESOURCES) {
        return NDIS_STATUS_RESOURCES;
    }
    if (!NT_SUCCESS(status)) {
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    a->CmHeld = 1;
    /* Which way the table came (1 the query, 2 the 98 SE and ME
     * fallback's code): record 35 section 6.3's reading. */
    NetCmLog(a, "net.cm.route", route);
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL; after NetCmClose. */
VOID NetCmRelease(PNET_ADAPTER a)
{
    if (a->CmHeld) {
        a->CmHeld = 0;
        NetCmIfRelease(&a->Cm);
    }
}

/* IRQL: PASSIVE_LEVEL. The service's enable (section 6.3): callbacks
 * may run from the moment Open returns. */
NDIS_STATUS NetCmOpen(PNET_ADAPTER a)
{
    XHCI_CM_NET_CALLBACKS cb;
    XHCI_CM_NET_CONFIG cfg;
    LONG status;

    if (!a->CmHeld || a->CmOpen) {
        return NDIS_STATUS_SUCCESS;
    }
    cb.Size = sizeof(cb);
    cb.LinkChange = NetCbLinkChange;
    cb.Receive = NetCbReceive;
    cb.TxDone = NetCbTxDone;
    cb.Stats = NetCbStats;
    cfg.Size = sizeof(cfg);
    cfg.MaxFrame = a->MaxFrame;
    status = a->Cm.Open(a->Cm.Header.Context, &cb, a, &cfg);
    if (status != XHCI_CM_NET_OK) {
        NetCmLog(a, "net.open.fail", (ULONG)status);
        return NDIS_STATUS_FAILURE;
    }
    a->CmOpen = 1;
    NetCmLog(a, "net.open", a->MaxFrame);
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL. Close waits until no callback runs or can start
 * (section 6.3); the link is then down whatever it was. */
VOID NetCmClose(PNET_ADAPTER a)
{
    if (!a->CmOpen) {
        return;
    }
    a->Cm.Close(a->Cm.Header.Context);
    a->CmOpen = 0;
    NetCmLog(a, "net.close", a->TxOk);
}

/* The send errors, OID_GEN_XMIT_ERROR's value: the failed sends and the
 * three refusals of a frame's size. IRQL: any. */
ULONG NetTxErrorTotal(PNET_ADAPTER a)
{
    return a->TxErrors + a->TxDrops[NET_TXDROP_SHORT] +
           a->TxDrops[NET_TXDROP_LONG] + a->TxDrops[NET_TXDROP_PEER];
}

/* The receive errors, OID_GEN_RCV_ERROR's value: the core's drops and the
 * split's. IRQL: any. */
ULONG NetRxErrorTotal(PNET_ADAPTER a)
{
    ULONG v = 0;
    ULONG i;

    for (i = 0; i < NET_RXDROP_REASONS; i++) {
        v += a->Rx.Drops[i];
    }
    for (i = 0; i < NET_SEGDROP_REASONS; i++) {
        v += a->SegDrops[i];
    }
    return v;
}

/* The status query's counters (xhci_cmnet.h, design record 35 section
 * 17): the values the general statistics OIDs answer, read as they read
 * them, with no lock. On xhcicm.sys's thread, PASSIVE_LEVEL; a field the
 * caller's Size does not cover is left alone. */
VOID NTAPI NetCbStats(PVOID ctx, PXHCI_CM_NET_STATS s)
{
    PNET_ADAPTER a = (PNET_ADAPTER)ctx;

    if (s == NULL || s->Size < sizeof(XHCI_CM_NET_STATS)) {
        return;
    }
    s->Driver = XHCI_CM_NET_STATS_NDIS5;
    s->Mtu = a->Mtu;
    s->Flags = XHCI_CM_NET_STATS_F_BYTES32 |
               (a->PowerD3 ? XHCI_CM_NET_STATS_F_ASLEEP : 0UL);
    s->TxPackets = a->TxOk;
    s->TxBytesLo = a->TxBytes;
    s->TxBytesHi = 0;
    s->RxPackets = a->RxOk;
    s->RxBytesLo = a->RxBytes;
    s->RxBytesHi = 0;
    s->TxErrors = NetTxErrorTotal(a);
    s->RxErrors = NetRxErrorTotal(a);
    s->RxNoBuffer = a->RxNoBuffer;
    s->TxDown = a->TxDown;
    s->RxNotUp = a->RxNotUp;
    s->RxSplit = a->RxSplit;
    s->TxFullWaits = a->TxFullWaits;
}

/* IRQL: <= DISPATCH_LEVEL. */
ULONG NetCmQuery(PNET_ADAPTER a, PNET_CM_INFO i)
{
    if (a->CmHeld) {
        return NetCmIfQuery(&a->Cm, i);
    }
    NetZero(i, sizeof(*i));
    i->Size = sizeof(*i);
    return 0;
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmLog(PNET_ADAPTER a, const char *label, ULONG value)
{
    if (a->CmHeld) {
        a->Cm.Log(a->Cm.Header.Context, label, value);
    }
}
