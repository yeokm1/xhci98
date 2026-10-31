/*
 * net_cmif.c - the network drivers' shared glue to the connection
 * manager's interface (net_cmif.h; design record 35 sections 6.3 and
 * 6.4). WDM alone, no NDIS type: xhcinet.sys and xhcinet6.sys compile it
 * unchanged.
 *
 * IRQL: as each function says.
 */

#include "net_cmif.h"

static const GUID netCmGuid = XHCI_CM_NET_GUID_INIT;

/* The destination volatile: an optimising compiler (WDK 7.1's cl 15 at
 * /Oxs) otherwise turns the loop into a call to the C runtime's memset, an
 * import no driver here may carry. */
static VOID netCmIfZero(PVOID p, ULONG n)
{
    volatile UCHAR *b = (volatile UCHAR *)p;

    while (n != 0) {
        *b = 0;
        b++;
        n--;
    }
}

static NTSTATUS NTAPI netCmIfDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                  PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* IRQL: PASSIVE_LEVEL. */
NTSTATUS NetCmIfAcquire(PDEVICE_OBJECT Lower, PXHCI_CM_NET_INTERFACE Iface)
{
    XHCI_CM_NET_INTERFACE got;
    PIO_STACK_LOCATION next;
    LARGE_INTEGER ms;
    KEVENT done;
    PIRP irp;
    NTSTATUS status;

    netCmIfZero(Iface, sizeof(*Iface));
    netCmIfZero(&got, sizeof(got));
    if (Lower == NULL) {
        return STATUS_NO_SUCH_DEVICE;
    }
    irp = IoAllocateIrp(Lower->StackSize, FALSE);
    if (irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_PNP;
    next->MinorFunction = IRP_MN_QUERY_INTERFACE;
    next->Parameters.QueryInterface.InterfaceType = &netCmGuid;
    next->Parameters.QueryInterface.Size = (USHORT)sizeof(got);
    next->Parameters.QueryInterface.Version = XHCI_CM_NET_VERSION;
    next->Parameters.QueryInterface.Interface = (PINTERFACE)&got;
    next->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoSetCompletionRoutine(irp, netCmIfDone, &done, TRUE, TRUE, TRUE);
    status = IoCallDriver(Lower, irp);
    if (status == STATUS_PENDING) {
        /* Windows 98 can return from the wait unsatisfied
         * (docs/usb-xhci-info/win98-wdm.md): wait again after 1 ms. */
        ms.QuadPart = -10000;
        while (KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                     NULL) != STATUS_SUCCESS) {
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &ms);
        }
    }
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);

    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (got.Header.Version != XHCI_CM_NET_VERSION ||
        got.Header.Size < sizeof(got) ||
        got.Open == NULL || got.Close == NULL || got.QueryInfo == NULL ||
        got.TxReserve == NULL || got.TxCommit == NULL ||
        got.TxAbort == NULL || got.Log == NULL) {
        if (got.Header.InterfaceDereference != NULL) {
            got.Header.InterfaceDereference(got.Header.Context);
        }
        return STATUS_NOT_SUPPORTED;
    }
    *Iface = got;
    return STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL. */
VOID NetCmIfRelease(PXHCI_CM_NET_INTERFACE Iface)
{
    PINTERFACE_DEREFERENCE deref = Iface->Header.InterfaceDereference;
    PVOID context = Iface->Header.Context;

    netCmIfZero(Iface, sizeof(*Iface));
    if (deref != NULL) {
        deref(context);
    }
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmIfQuery(const XHCI_CM_NET_INTERFACE *Iface,
                  PXHCI_CM_NET_INFO Info)
{
    netCmIfZero(Info, sizeof(*Info));
    Info->Size = sizeof(*Info);
    if (Iface->QueryInfo == NULL ||
        Iface->QueryInfo(Iface->Header.Context, Info) != XHCI_CM_NET_OK) {
        netCmIfZero(Info, sizeof(*Info));
        Info->Size = sizeof(*Info);
    }
}

/* LinkMbps is the total over every lane in use (40000 for a bonded Gen 3
 * link); Lanes is informational. A rate past 400 Gbit/s is taken as
 * unknown, which also keeps OID_GEN_LINK_SPEED's LinkMbps * 10000 within
 * a ULONG. IRQL: any. */
ULONG NetCmIfRateMbps(const XHCI_CM_NET_INFO *Info)
{
    if (Info->LinkMbps > 400000UL) {
        return 0;
    }
    return Info->LinkMbps;
}

/* IRQL: <= DISPATCH_LEVEL. */
ULONG NetCmIfReserve(const XHCI_CM_NET_INTERFACE *Iface, ULONG Count,
                     PUCHAR *Buffers, PXHCI_CM_NET_TOKEN Token)
{
    PVOID va[XHCI_CM_NET_TX_MAX_RESERVE];
    ULONG k;

    if (Count == 0 || Count > XHCI_CM_NET_TX_MAX_RESERVE) {
        return 0;
    }
    if (Iface->TxReserve(Iface->Header.Context, Count, va, Token) != Count) {
        return 0;
    }
    for (k = 0; k < Count; k++) {
        ((PUCHAR volatile *)Buffers)[k] = (PUCHAR)va[k];
    }
    return 1;
}

/* IRQL: <= DISPATCH_LEVEL. */
ULONG NetCmIfCommit(const XHCI_CM_NET_INTERFACE *Iface,
                    const XHCI_CM_NET_TOKEN *Token, ULONG Count,
                    const ULONG *Lengths)
{
    return (Iface->TxCommit(Iface->Header.Context, Token, Count, Lengths) ==
            XHCI_CM_NET_OK) ? 1UL : 0UL;
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmIfAbort(const XHCI_CM_NET_INTERFACE *Iface,
                  const XHCI_CM_NET_TOKEN *Token)
{
    Iface->TxAbort(Iface->Header.Context, Token);
}
