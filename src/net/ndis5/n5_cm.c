/*
 * n5_cm.c - xhcinet.sys's glue to the connection manager's interface on
 * the network PDO (design record 35 sections 6.3 and 6.4): asked once at
 * the miniport's initialisation with IRP_MN_QUERY_INTERFACE to the
 * device object below it, held until the halt; Open and Close.
 *
 * The one file that knows the interface's table (src\xhci_cmnet.h).
 *
 * IRQL: as each function says.
 */

#include "xhcinet.h"

static const GUID netCmGuid = XHCI_CM_NET_GUID_INIT;

static NTSTATUS netQiDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                          PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* IRQL: PASSIVE_LEVEL (MiniportInitialize). */
NDIS_STATUS NetCmAcquire(PNET_ADAPTER a)
{
    XHCI_CM_NET_INTERFACE iface;
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    NTSTATUS status;

    NetZero(&iface, sizeof(iface));
    irp = IoAllocateIrp(a->Lower->StackSize, FALSE);
    if (irp == NULL) {
        return NDIS_STATUS_RESOURCES;
    }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_PNP;
    next->MinorFunction = IRP_MN_QUERY_INTERFACE;
    next->Parameters.QueryInterface.InterfaceType = &netCmGuid;
    next->Parameters.QueryInterface.Size = (USHORT)sizeof(iface);
    next->Parameters.QueryInterface.Version = XHCI_CM_NET_VERSION;
    next->Parameters.QueryInterface.Interface = (PINTERFACE)&iface;
    next->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoSetCompletionRoutine(irp, netQiDone, &done, TRUE, TRUE, TRUE);
    status = IoCallDriver(a->Lower, irp);
    if (status == STATUS_PENDING) {
        /* Windows 98 can return from the wait unsatisfied. */
        while (KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                     NULL) != STATUS_SUCCESS) {
            NdisMSleep(1000);
        }
    }
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);

    if (!NT_SUCCESS(status)) {
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    if (iface.Header.Version != XHCI_CM_NET_VERSION ||
        iface.Header.Size < sizeof(iface) ||
        iface.Open == NULL || iface.Close == NULL ||
        iface.QueryInfo == NULL || iface.TxReserve == NULL ||
        iface.TxCommit == NULL || iface.TxAbort == NULL ||
        iface.Log == NULL) {
        if (iface.Header.InterfaceDereference != NULL) {
            iface.Header.InterfaceDereference(iface.Header.Context);
        }
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }
    a->Cm = iface;
    a->CmHeld = 1;
    return NDIS_STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL; after NetCmClose. */
VOID NetCmRelease(PNET_ADAPTER a)
{
    if (a->CmHeld) {
        a->CmHeld = 0;
        if (a->Cm.Header.InterfaceDereference != NULL) {
            a->Cm.Header.InterfaceDereference(a->Cm.Header.Context);
        }
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

/* IRQL: <= DISPATCH_LEVEL. A refusal leaves the zeroed answer: link
 * down, no peer. */
VOID NetCmQuery(PNET_ADAPTER a, PNET_CM_INFO i)
{
    NetZero(i, sizeof(*i));
    i->Size = sizeof(*i);
    if (a->CmHeld) {
        if (a->Cm.QueryInfo(a->Cm.Header.Context, i) != XHCI_CM_NET_OK) {
            NetZero(i, sizeof(*i));
            i->Size = sizeof(*i);
        }
    }
}

/* The link's rate for OID_GEN_LINK_SPEED: the USB4 link's rate times its
 * lanes, 0 when unknown (record 35 section 7.5). IRQL: any. */
ULONG NetCmRateMbps(const NET_CM_INFO *i)
{
    ULONG lanes = i->Lanes;

    if (lanes == 0) {
        lanes = 1;
    }
    if (i->LinkMbps > 200000UL) {
        return 0;
    }
    return i->LinkMbps * lanes;
}

/* All or nothing: 1 with count buffers and the token, else 0.
 * IRQL: <= DISPATCH_LEVEL; the queue's one sender. */
ULONG NetCmReserve(PNET_ADAPTER a, ULONG count, PUCHAR *bufs,
                   PXHCI_CM_NET_TOKEN token)
{
    PVOID va[XHCI_CM_NET_TX_MAX_RESERVE];
    ULONG k;

    if (count == 0 || count > XHCI_CM_NET_TX_MAX_RESERVE) {
        return 0;
    }
    if (a->Cm.TxReserve(a->Cm.Header.Context, count, va, token) != count) {
        return 0;
    }
    for (k = 0; k < count; k++) {
        bufs[k] = (PUCHAR)va[k];
    }
    return 1;
}

/* 1 when posted; 0 when refused (a stale token), the reservation
 * released either way. IRQL: <= DISPATCH_LEVEL. */
ULONG NetCmCommit(PNET_ADAPTER a, const XHCI_CM_NET_TOKEN *token,
                  ULONG count, const ULONG *lens)
{
    return (a->Cm.TxCommit(a->Cm.Header.Context, token, count, lens) ==
            XHCI_CM_NET_OK) ? 1UL : 0UL;
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmAbort(PNET_ADAPTER a, const XHCI_CM_NET_TOKEN *token)
{
    a->Cm.TxAbort(a->Cm.Header.Context, token);
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmLog(PNET_ADAPTER a, const char *label, ULONG value)
{
    if (a->CmHeld) {
        a->Cm.Log(a->Cm.Header.Context, label, value);
    }
}
