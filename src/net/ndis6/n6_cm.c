/*
 * n6_cm.c - xhcinet6.sys's use of the connection manager's interface on
 * the network PDO (design record 35 sections 6.3 and 6.4): the table
 * asked once at MiniportInitializeEx and held until MiniportHaltEx,
 * through the network drivers' shared glue (..\net_cmif.c, which
 * xhcinet.sys shares); Open, Close and Log here, each miniport's own.
 *
 * IRQL: as each function says.
 */

#include "xhcinet6.h"

/* IRQL: PASSIVE_LEVEL (MiniportInitializeEx). */
NDIS_STATUS Net6CmAcquire(PNET6_ADAPTER a)
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
    Net6CmLog(a, "net.cm.route", route);
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL; after Net6CmClose. */
VOID Net6CmRelease(PNET6_ADAPTER a)
{
    if (a->CmHeld) {
        a->CmHeld = 0;
        NetCmIfRelease(&a->Cm);
    }
}

/* IRQL: PASSIVE_LEVEL. The service's enable (section 6.3): callbacks
 * may run from the moment Open returns, before MiniportInitializeEx has
 * returned, so the callbacks indicate nothing until the adapter has been
 * restarted (Net6CbLinkChange). */
NDIS_STATUS Net6CmOpen(PNET6_ADAPTER a)
{
    XHCI_CM_NET_CALLBACKS cb;
    XHCI_CM_NET_CONFIG cfg;
    LONG status;

    if (!a->CmHeld || a->CmOpen) {
        return NDIS_STATUS_SUCCESS;
    }
    cb.Size = sizeof(cb);
    cb.LinkChange = Net6CbLinkChange;
    cb.Receive = Net6CbReceive;
    cb.TxDone = Net6CbTxDone;
    cfg.Size = sizeof(cfg);
    cfg.MaxFrame = a->MaxFrame;
    status = a->Cm.Open(a->Cm.Header.Context, &cb, a, &cfg);
    if (status != XHCI_CM_NET_OK) {
        Net6CmLog(a, "net.open.fail", (ULONG)status);
        return NDIS_STATUS_FAILURE;
    }
    a->CmOpen = 1;
    Net6CmLog(a, "net.open", a->MaxFrame);
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL. Close waits until no callback runs or can start
 * (section 6.3). */
VOID Net6CmClose(PNET6_ADAPTER a)
{
    if (!a->CmOpen) {
        return;
    }
    a->Cm.Close(a->Cm.Header.Context);
    a->CmOpen = 0;
    Net6CmLog(a, "net.close", a->TxOk);
}

/* IRQL: <= DISPATCH_LEVEL. */
ULONG Net6CmQuery(PNET6_ADAPTER a, PXHCI_CM_NET_INFO i)
{
    if (a->CmHeld) {
        return NetCmIfQuery(&a->Cm, i);
    }
    Net6Zero(i, sizeof(*i));
    i->Size = sizeof(*i);
    return 0;
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID Net6CmLog(PNET6_ADAPTER a, const char *label, ULONG value)
{
    if (a->CmHeld) {
        a->Cm.Log(a->Cm.Header.Context, label, value);
    }
}
