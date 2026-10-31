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
    NTSTATUS status = NetCmIfAcquire(a->Lower, &a->Cm);

    if (status == STATUS_INSUFFICIENT_RESOURCES) {
        return NDIS_STATUS_RESOURCES;
    }
    if (!NT_SUCCESS(status)) {
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    a->CmHeld = 1;
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

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmQuery(PNET_ADAPTER a, PNET_CM_INFO i)
{
    if (a->CmHeld) {
        NetCmIfQuery(&a->Cm, i);
    } else {
        NetZero(i, sizeof(*i));
        i->Size = sizeof(*i);
    }
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmLog(PNET_ADAPTER a, const char *label, ULONG value)
{
    if (a->CmHeld) {
        a->Cm.Log(a->Cm.Header.Context, label, value);
    }
}
