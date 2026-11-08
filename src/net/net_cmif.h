/*
 * net_cmif.h - the network drivers' shared glue to the connection
 * manager's interface (src\xhci_cmnet.h; design record 35 sections 6.3
 * and 6.4, task 45.59): asking for the table with IRP_MN_QUERY_INTERFACE,
 * releasing it, and the thin calls over it. WDM alone, no NDIS type, so
 * xhcinet.sys (NDIS 5, step S6) and xhcinet6.sys (NDIS 6, step S7) share
 * it unchanged; each miniport keeps its own Open, Close and callbacks.
 *
 * IRQL: as each function says.
 */

#ifndef NET_CMIF_H
#define NET_CMIF_H

#include "../xhci_cmnet.h"

/* Asks the device object below for the table (the network PDO answers):
 * IRP_MN_QUERY_INTERFACE, then, where that is not answered, the 98 SE and
 * ME fallback's XHCI_CM_NET_IOCTL_INTERFACE (xhci_cmnet.h). STATUS_SUCCESS
 * with *Iface held (InterfaceReference taken by the PDO) and *Route 1
 * (the query) or 2 (the fallback), else an error, *Iface zeroed and
 * *Route 0. IRQL: PASSIVE_LEVEL. */
NTSTATUS NetCmIfAcquire(PDEVICE_OBJECT Lower, PXHCI_CM_NET_INTERFACE Iface,
                        PULONG Route);

/* Drops the hold; *Iface zeroed. IRQL: PASSIVE_LEVEL. */
VOID NetCmIfRelease(PXHCI_CM_NET_INTERFACE Iface);

/* QueryInfo with its Size set: 1 when answered, else 0 and the zeroed
 * answer (link down, no peer, no MAC). IRQL: <= DISPATCH_LEVEL. */
ULONG NetCmIfQuery(const XHCI_CM_NET_INTERFACE *Iface,
                   PXHCI_CM_NET_INFO Info);

/* The most an initialisation waits for this domain's MAC, which QueryInfo
 * refuses until the connection manager has read it at this start (design
 * record 35 section 6.5), in steps of NET_CMIF_MAC_STEP_MS. */
#define NET_CMIF_MAC_WAIT_MS    5000UL
#define NET_CMIF_MAC_STEP_MS    50UL

/* QueryInfo asked until it answers with a MAC that is not all zeros, at
 * most NET_CMIF_MAC_WAIT_MS / NET_CMIF_MAC_STEP_MS more times after the
 * first: 1 with *Info that answer, else 0 and *Info zeroed. IRQL:
 * PASSIVE_LEVEL. */
ULONG NetCmIfMacWait(const XHCI_CM_NET_INTERFACE *Iface,
                     PXHCI_CM_NET_INFO Info);

/* An adapter keeps the MAC its initialisation took until its halt; a
 * later start of the NHI under it may read another (a router whose UUID
 * read unusable, record 35 section 4.4), which the link-up callbacks log
 * as net.mac.moved. 1 when the six bytes differ. IRQL: any. */
ULONG NetCmIfMacMoved(const UCHAR *Kept, const XHCI_CM_NET_INFO *Info);

/* The link's total rate in Mbit/s (LinkMbps), 0 when unknown. IRQL: any. */
ULONG NetCmIfRateMbps(const XHCI_CM_NET_INFO *Info);

/* All or nothing: 1 with Count buffers and the token, else 0.
 * IRQL: <= DISPATCH_LEVEL; one reservation at a time (the caller's). */
ULONG NetCmIfReserve(const XHCI_CM_NET_INTERFACE *Iface, ULONG Count,
                     PUCHAR *Buffers, PXHCI_CM_NET_TOKEN Token);

/* 1 when posted; 0 when refused (a stale token), the reservation
 * released either way. IRQL: <= DISPATCH_LEVEL. */
ULONG NetCmIfCommit(const XHCI_CM_NET_INTERFACE *Iface,
                    const XHCI_CM_NET_TOKEN *Token, ULONG Count,
                    const ULONG *Lengths);

/* IRQL: <= DISPATCH_LEVEL. */
VOID NetCmIfAbort(const XHCI_CM_NET_INTERFACE *Iface,
                  const XHCI_CM_NET_TOKEN *Token);

#endif /* NET_CMIF_H */
