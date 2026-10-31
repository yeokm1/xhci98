/*
 * xhcinet6.h - xhcinet6.sys, the NDIS 6.20 miniport of Thunderbolt
 * networking for Windows 8, 8.1 and 10, x86 and x64 (design record 35
 * sections 7.1 to 7.7 and 7.9, task 45.59 step S7): one adapter per NHI,
 * above xhcicm.sys's network PDO XHCICM\TBNET, with no hardware of its
 * own: every frame goes through the connection manager's interface
 * (src\xhci_cmnet.h, record 35 section 6.3) through the network drivers'
 * shared glue (..\net_cmif.c), framed by the shared pure core
 * (..\net_frame.c). xhcinet.sys (..\ndis5, step S6) is the NDIS 5 image
 * of the same design for 98 SE to 7; the two share both files and
 * nothing else.
 *
 * One lock (Lock) covers the send queue, the pause state, the link view
 * and the receive pool's free list, so the pause's drain condition
 * (no send queued or in flight, no indication outstanding) is read
 * whole under it. It is never held across a call into NDIS's completion,
 * indication or status routines or into the connection manager.
 */

#ifndef XHCINET6_H
#define XHCINET6_H

#include <ndis.h>
#include "..\net_frame.h"
#include "..\net_cmif.h"

#define NET6_TAG                'tNhX'

/* Receive net buffer lists of the miniport's own pool (record 35
 * section 7.5), each with one buffer of the adapter's maximum frame. */
#define NET6_RX_SMALL           64UL    /* a maximum up to 16 KiB        */
#define NET6_RX_LARGE           16UL    /* above it                      */
#define NET6_RX_MAX             64UL
#define NET6_RX_LARGE_FROM      16384UL

#define NET6_MCAST_MAX          32UL

/* The adapter's NDIS 6 state (record 35 section 7.5). */
#define NET6_PAUSED             0UL     /* after InitializeEx, after Pause */
#define NET6_RUNNING            1UL     /* after Restart                 */
#define NET6_PAUSING            2UL     /* Pause returned PENDING         */

/* The power work item's request (record 35 section 7.7). */
#define NET6_WORK_NONE          0UL
#define NET6_WORK_D0            1UL     /* OID_PNP_SET_POWER D0: Open    */
#define NET6_WORK_D3            2UL     /* OID_PNP_SET_POWER D3: Close   */

typedef struct _NET6_ADAPTER {
    NDIS_HANDLE Handle;
    PDEVICE_OBJECT Lower;

    /* The connection manager's interface (n6_cm.c). */
    XHCI_CM_NET_INTERFACE Cm;
    ULONG CmHeld;           /* the interface is referenced                */
    ULONG CmOpen;           /* between a successful Open and its Close    */

    /* Configuration (XhciNetMtu) and what QueryInfo gave. */
    ULONG Mtu;
    ULONG MaxFrame;         /* Mtu + 14                                   */
    UCHAR Mac[6];

    NDIS_SPIN_LOCK Lock;

    /* Under Lock: the NDIS state and the link. */
    ULONG State;            /* NET6_*                                     */
    ULONG PausePending;     /* Pause returned PENDING; the drain completes */
    ULONG Restarted;        /* Restart has run once: status may be indicated */
    ULONG Indicating;       /* a context is publishing the link state     */
    ULONG IndicateAgain;    /* the state changed while it published       */
    ULONG LinkUp;
    ULONG LinkGen;          /* advanced at every link change              */
    ULONG PeerStns;
    ULONG RateMbps;
    ULONG BigLogged;        /* "peer takes no large frames" logged        */
    ULONG Removed;          /* surprise-removed: nothing is sent          */

    /* Under Lock: the send queue of net buffer lists, one sender at a
     * time. Each queued list's MiniportReserved[0] is its next net
     * buffer to send and [1] its completion status. */
    PNET_BUFFER_LIST TxHead;
    PNET_BUFFER_LIST TxTail;
    ULONG TxServing;        /* the one sender, until its completion       */
    ULONG TxActive;         /* contexts completing sends                  */
    ULONG TxKick;           /* work arrived while a sender served         */
    NET_TX Tx;              /* the sender's alone                         */

    /* Receive: the free list and RxOut under Lock; the reassembly and
     * split state the receive callback's alone (one DPC). */
    NDIS_HANDLE NblPool;
    ULONG RxCount;
    PUCHAR RxStore[NET6_RX_MAX];
    PMDL RxMdl[NET6_RX_MAX];
    PNET_BUFFER_LIST RxAll[NET6_RX_MAX];
    PNET_BUFFER_LIST RxFree[NET6_RX_MAX];
    ULONG RxFreeCount;
    ULONG RxOut;            /* indicated and not yet returned             */
    ULONG RxSeenGen;        /* the LinkGen the receive state is from      */
    PUCHAR RxAsm;           /* the 65550-byte reassembly buffer           */
    NET_RX Rx;
    NET_SEG Seg;

    /* The power OID, pended to a work item at PASSIVE_LEVEL. */
    NDIS_HANDLE WorkItem;
    PNDIS_OID_REQUEST PowerOid;
    ULONG WorkReq;          /* NET6_WORK_*                                */
    ULONG PowerD3;

    /* OID state. */
    ULONG Filter;
    ULONG Lookahead;
    ULONG McCount;
    UCHAR Mcast[NET6_MCAST_MAX][6];

    /* NdisMSetMiniportAttributes reads these; kept for the adapter's
     * life. */
    NDIS_PM_CAPABILITIES Pm;

    /* Counters (record 35 section 7.5). */
    ULONG TxOk;
    ULONG TxBytes;          /* with TxBytesHi, a 64-bit count by carry  */
    ULONG TxBytesHi;
    ULONG TxErrors;
    ULONG TxDrops[NET_TXDROP_REASONS];
    ULONG TxFullWaits;
    ULONG TxDown;           /* completed as disconnected or paused        */
    ULONG RxOk;
    ULONG RxBytes;          /* with RxBytesHi, a 64-bit count by carry  */
    ULONG RxBytesHi;
    ULONG RxNoBuffer;
    ULONG RxNotUp;          /* frames while the link was down or paused   */
    ULONG RxSplit;          /* large segments split                       */
    ULONG SegDrops[NET_SEGDROP_REASONS];
} NET6_ADAPTER, *PNET6_ADAPTER;

/* n6_cm.c - Open, Close and Log over the shared glue. */
NDIS_STATUS Net6CmAcquire(PNET6_ADAPTER a);           /* PASSIVE */
VOID Net6CmRelease(PNET6_ADAPTER a);                  /* PASSIVE */
NDIS_STATUS Net6CmOpen(PNET6_ADAPTER a);              /* PASSIVE */
VOID Net6CmClose(PNET6_ADAPTER a);                    /* PASSIVE */
VOID Net6CmQuery(PNET6_ADAPTER a, PXHCI_CM_NET_INFO i); /* <= DISPATCH */
VOID Net6CmLog(PNET6_ADAPTER a, const char *label, ULONG value);

/* n6_data.c - the data path. */
MINIPORT_SEND_NET_BUFFER_LISTS Net6SendNbls;
MINIPORT_RETURN_NET_BUFFER_LISTS Net6ReturnNbls;
MINIPORT_CANCEL_SEND Net6CancelSend;
VOID Net6Serve(PNET6_ADAPTER a);                      /* <= DISPATCH */
NDIS_STATUS Net6Pause(PNET6_ADAPTER a);               /* PASSIVE */
VOID Net6IndicateLink(PNET6_ADAPTER a);               /* <= DISPATCH */
VOID Net6LinkDown(PNET6_ADAPTER a);                   /* <= DISPATCH */
VOID NTAPI Net6CbLinkChange(PVOID ctx, BOOLEAN up);           /* PASSIVE */
VOID NTAPI Net6CbReceive(PVOID ctx, const VOID *frame, ULONG length,
                         ULONG flags);                        /* DISPATCH */
VOID NTAPI Net6CbTxDone(PVOID ctx, ULONG count);              /* DISPATCH */
NDIS_STATUS Net6RxAlloc(PNET6_ADAPTER a);             /* PASSIVE */
VOID Net6RxFree(PNET6_ADAPTER a);                     /* PASSIVE */
VOID Net6Bps(ULONG mbps, ULONG64 *bps);               /* any */
VOID Net6Count64(PULONG lo, PULONG hi, ULONG n);     /* the counter's one writer */
VOID Net6Read64(const ULONG *lo, const ULONG *hi, ULONG64 *out); /* any */
VOID Net6DrainSends(PNET6_ADAPTER a);                 /* PASSIVE */

/* n6_oid.c - the OID requests and the power work item. */
MINIPORT_OID_REQUEST Net6OidRequest;
MINIPORT_CANCEL_OID_REQUEST Net6CancelOid;
extern const NDIS_OID Net6Oids[];
extern const ULONG Net6OidsSize;

/* The miniport's own copy and fill: no C runtime import. */
VOID Net6Copy(PUCHAR dst, const UCHAR *src, ULONG n);
VOID Net6Zero(PVOID dst, ULONG n);

#endif /* XHCINET6_H */
