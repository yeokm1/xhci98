/*
 * xhcinet.h - xhcinet.sys, the NDIS 5 miniport of Thunderbolt networking
 * (design record 35 sections 7.1 to 7.8, task 45.59 step S6): one
 * adapter per NHI, above xhcicm.sys's network PDO XHCICM\TBNET,
 * deserialized, with a WDM lower edge (NDIS_WDM) and no hardware of its
 * own: every frame goes through the connection manager's interface
 * (src\xhci_cmnet.h, record 35 section 6.3), framed by the pure core
 * (..\net_frame.c).
 *
 * The x86 image is NDIS 5.0 (98 SE to 7), the x64 one NDIS 5.1 (XP x64
 * to 7 x64); sources picks the define.
 */

#ifndef XHCINET_H
#define XHCINET_H

#include <ndis.h>
#include "..\net_frame.h"
#include "..\net_cmif.h"
#include "..\net_upq.h"

typedef XHCI_CM_NET_INFO NET_CM_INFO, *PNET_CM_INFO;

#define NET_TAG                 'tNhX'

/* Receive packets of the miniport's own pool (record 35 section 7.5),
 * each with one buffer of the adapter's maximum frame. */
#define NET_RX_PACKETS_SMALL    64UL    /* a maximum up to 16 KiB        */
#define NET_RX_PACKETS_LARGE    16UL    /* above it                      */
#define NET_RX_PACKETS_MAX      64UL
#define NET_RX_LARGE_FROM       16384UL

#define NET_MCAST_MAX           32UL

/* A send completed while the link is down (record 35 section 7.5 names
 * NDIS 6's NDIS_STATUS_MEDIA_DISCONNECTED; NDIS 5's headers have no such
 * send status, and NO_CABLE is the failure that says the same). */
#define NET_STATUS_DOWN         NDIS_STATUS_NO_CABLE

/*
 * The upcalls of 98 SE and ME (record 35 section 7.8): the x86 NDIS 5.0
 * image, on a system without WDM 1.10, makes no receive indication, send
 * completion or reset completion from the connection manager's callbacks
 * or from any other caller's context; each is queued and made by an NDIS
 * miniport timer's routine. The NDIS 5.1 image (x64, NT only) has none of
 * it.
 */
#if !defined(NDIS51_MINIPORT)
#define NET_DEFER               1
#else
#define NET_DEFER               0
#endif
#define NET_UP_RING_MAX         524288UL /* the frame ring, tried first  */
#define NET_UP_RING_MIN         131072UL /* and halved down to this      */
#define NET_UP_HALT_MS          2000UL  /* a quiescence's log period, ms */

/* The qemu flavour's checks of the data path (record 35 section 7.8):
 * counters, invariants and sequence numbers logged through the
 * connection manager; never in the release flavour. */
#if defined(XHCI_FLAVOUR_QEMU)
#define NET_DIAG                1
#else
#define NET_DIAG                0
#endif
#define NET_DIAG_SENDS          256UL   /* sends tracked for a duplicate  */

/* The work the adapter's thread does at PASSIVE_LEVEL. */
#define NET_WORK_NONE           0UL
#define NET_WORK_D0             1UL     /* OID_PNP_SET_POWER D0: Open    */
#define NET_WORK_D3             2UL     /* OID_PNP_SET_POWER D3: Close   */

typedef struct _NET_ADAPTER {
    NDIS_HANDLE Handle;
    PDEVICE_OBJECT Lower;

    /* The connection manager's interface (n5_cm.c). */
    XHCI_CM_NET_INTERFACE Cm;
    ULONG CmHeld;           /* the interface is referenced                */
    ULONG CmOpen;           /* between a successful Open and its Close    */

    /* Configuration (XhciNetMtu) and what QueryInfo gave. */
    ULONG Mtu;
    ULONG MaxFrame;         /* Mtu + 14                                   */
    UCHAR Mac[6];
    ULONG LinkUp;
    ULONG LinkGen;          /* advanced at every LinkChange               */
    ULONG PeerStns;
    ULONG RateMbps;
    ULONG BigLogged;        /* "peer takes no large frames" logged        */

    /* The send queue (TxLock). One sender serves it at a time. */
    NDIS_SPIN_LOCK TxLock;
    PNDIS_PACKET TxHead;
    PNDIS_PACKET TxTail;
    ULONG TxServing;        /* the one sender, until its completion      */
    ULONG TxActive;         /* contexts completing sends (the sender, a
                               flush, a reset's drain), under TxLock       */
    ULONG TxKick;           /* work arrived while a sender served         */
    ULONG ResetGate;        /* a reset is in progress: no sender starts   */
    ULONG ResetPending;     /* the reset waits for the sender to finish   */
    ULONG Halting;
    NET_TX Tx;

    /* Receive (RxLock over the free list and RxOut). */
    NDIS_SPIN_LOCK RxLock;
    NDIS_HANDLE PacketPool;
    NDIS_HANDLE BufferPool;
    ULONG RxPackets;
    PUCHAR RxStore[NET_RX_PACKETS_MAX];
    PNDIS_PACKET RxAll[NET_RX_PACKETS_MAX];
    PNDIS_PACKET RxFree[NET_RX_PACKETS_MAX];
    ULONG RxFreeCount;
    ULONG RxOut;            /* indicated and not yet returned             */
    KEVENT RxDrained;       /* signalled when RxOut reaches 0 in a halt   */
    ULONG RxSeenGen;        /* the LinkGen the receive state is from      */
    PUCHAR RxAsm;           /* the 65550-byte reassembly buffer           */
    NET_RX Rx;              /* the receive callback's alone               */
    NET_SEG Seg;            /* the receive callback's alone               */

    /* The PASSIVE_LEVEL thread for the power OIDs. */
    KEVENT WorkEvent;
    KEVENT WorkExited;      /* the join on 98 SE and ME                   */
    PVOID WorkThread;       /* the join on NT (WDM 1.10 and later)        */
    ULONG WorkStop;
    ULONG WorkReq;          /* NET_WORK_*, under TxLock                   */

    /* OID state. */
    ULONG Filter;
    ULONG Lookahead;
    ULONG McCount;
    UCHAR Mcast[NET_MCAST_MAX][6];
    ULONG PowerD3;

    /* Counters (record 35 section 7.5). */
    ULONG TxOk;
    ULONG TxBytes;
    ULONG TxErrors;
    ULONG TxDrops[NET_TXDROP_REASONS];
    ULONG TxFullWaits;
    ULONG TxDown;           /* completed as disconnected                  */
    ULONG RxOk;
    ULONG RxBytes;
    ULONG RxNoBuffer;
    ULONG RxNotUp;          /* frames while the link was down             */
    ULONG RxSplit;          /* large segments split                       */
    ULONG SegDrops[NET_SEGDROP_REASONS];

#if NET_DEFER
    /* The deferred upcalls (UpLock over Upq, a leaf: no other lock is
     * taken under it). Upq's frames are copies, in UpRing, of the
     * Ethernet frames the receive callback assembled; its completed sends
     * are linked through NET_NEXT, each with its status in
     * NET_DONE_STATUS. */
    ULONG Defer;            /* set at initialisation, never changed       */
    NDIS_SPIN_LOCK UpLock;
    NET_UPQ Upq;
    PUCHAR UpRing;
    ULONG UpRingSize;
    ULONG UpGen;            /* advanced at each D3: older frames dropped  */
    NDIS_MINIPORT_TIMER UpTimer[2]; /* set in turn, never the one whose
                               routine may still be returning            */
    ULONG UpNext;
#endif
#if NET_DIAG
    ULONG DgInCb;           /* inside a connection manager callback       */
    ULONG DgInInd;          /* inside NdisMIndicateReceivePacket          */
    ULONG DgIndSeq;         /* indications made                           */
    ULONG DgCmpSeq;         /* send completions made                      */
    ULONG DgAccSeq;         /* sends accepted                             */
    ULONG DgCbUpcalls;      /* upcalls made inside a callback             */
    ULONG DgSendInInd;      /* sends entered inside an indication         */
    ULONG DgBad;            /* invariant breaches, every kind             */
    ULONG DgFull;           /* sends untracked, the table full            */
    ULONG DgNextLog;
    ULONG RxState[NET_RX_PACKETS_MAX];  /* 1 out (pending or indicated)  */
    PNDIS_PACKET DgSends[NET_DIAG_SENDS];
#endif
} NET_ADAPTER, *PNET_ADAPTER;

/* n5_cm.c - the interface glue. */
NDIS_STATUS NetCmAcquire(PNET_ADAPTER a);         /* PASSIVE */
VOID NetCmRelease(PNET_ADAPTER a);                /* PASSIVE */
NDIS_STATUS NetCmOpen(PNET_ADAPTER a);            /* PASSIVE */
VOID NetCmClose(PNET_ADAPTER a);                  /* PASSIVE */
/* 1 when the connection manager answered, else 0 and *i zeroed. */
ULONG NetCmQuery(PNET_ADAPTER a, PNET_CM_INFO i); /* <= DISPATCH */
VOID NetCmLog(PNET_ADAPTER a, const char *label, ULONG value);
ULONG NetTxErrorTotal(PNET_ADAPTER a);            /* any */
ULONG NetRxErrorTotal(PNET_ADAPTER a);            /* any */
VOID NTAPI NetCbStats(PVOID ctx, PXHCI_CM_NET_STATS s);      /* PASSIVE */

/* n5_data.c - the data path. */
VOID NetSendPackets(NDIS_HANDLE ctx, PPNDIS_PACKET packets, UINT count);
VOID NetReturnPacket(NDIS_HANDLE ctx, PNDIS_PACKET packet);
NDIS_STATUS NetReset(PBOOLEAN addressingReset, NDIS_HANDLE ctx);
VOID NetServe(PNET_ADAPTER a);                    /* <= DISPATCH */
VOID NetFlushSends(PNET_ADAPTER a, NDIS_STATUS status);
VOID NTAPI NetCbLinkChange(PVOID ctx, BOOLEAN up);           /* PASSIVE */
VOID NTAPI NetCbReceive(PVOID ctx, const VOID *frame, ULONG length,
                        ULONG flags);                        /* DISPATCH */
VOID NTAPI NetCbTxDone(PVOID ctx, ULONG count);              /* DISPATCH */
NDIS_STATUS NetRxAlloc(PNET_ADAPTER a);           /* PASSIVE */
VOID NetRxFree(PNET_ADAPTER a);                   /* PASSIVE */
#if NET_DEFER
NDIS_STATUS NetUpInit(PNET_ADAPTER a);            /* PASSIVE */
VOID NetUpFree(PNET_ADAPTER a);                   /* PASSIVE */
VOID NetUpStop(PNET_ADAPTER a);                   /* PASSIVE */
VOID NetUpQuiet(PNET_ADAPTER a);                  /* PASSIVE */
VOID NetUpWake(PNET_ADAPTER a);                   /* PASSIVE */
VOID NetUpSleep(PNET_ADAPTER a);                  /* <= DISPATCH, TxLock */
#endif

/* The miniport's own copy and fill: no C runtime import. */
VOID NetCopy(PUCHAR dst, const UCHAR *src, ULONG n);
VOID NetZero(PVOID dst, ULONG n);

#endif /* XHCINET_H */
