/*
 * net_cm.h - the network driver's own view of the interface xhcicm.sys
 * gives it on the network PDO (design record 35 section 6.3, task 45.59).
 *
 * PROVISIONAL. The interface's one definition is src\xhci_cmnet.h, owned
 * by step S5; until it is in this tree, this file states the table as
 * record 35 section 6.3 has it, and only n5_cm.c includes it, so the
 * header's arrival changes that one file. The GUID below is not the
 * interface's: with it no connection manager answers, the miniport's
 * initialisation fails cleanly (NDIS_STATUS_ADAPTER_NOT_FOUND) and
 * nothing else is touched.
 *
 * IRQL of each member as section 6.3's table states it.
 */

#ifndef NET_CM_H
#define NET_CM_H

#define NET_CM_GUID_DATA1   0x5E1D4F7AUL
#define NET_CM_GUID_DATA2   0x9C1B
#define NET_CM_GUID_DATA3   0x4C3E
#define NET_CM_GUID_DATA4 \
    { 0xA1, 0x77, 0x2B, 0x90, 0x5D, 0x6E, 0x13, 0xC4 }
#define NET_CM_GUID_INIT \
    { NET_CM_GUID_DATA1, NET_CM_GUID_DATA2, NET_CM_GUID_DATA3, \
      NET_CM_GUID_DATA4 }
#define NET_CM_VERSION      1

/* Callbacks the network driver hands Open. */
typedef VOID (*NET_CM_LINK_CHANGE)(PVOID ClientContext, ULONG Up);
typedef VOID (*NET_CM_RECEIVE)(PVOID ClientContext, const UCHAR *Frame,
                               ULONG Length, ULONG Flags);
typedef VOID (*NET_CM_TX_DONE)(PVOID ClientContext, ULONG Count);

typedef struct _NET_CM_CALLBACKS {
    NET_CM_LINK_CHANGE LinkChange;  /* PASSIVE, on the CM's thread       */
    NET_CM_RECEIVE Receive;         /* DISPATCH, the service DPC         */
    NET_CM_TX_DONE TxDone;          /* DISPATCH                          */
} NET_CM_CALLBACKS, *PNET_CM_CALLBACKS;

typedef struct _NET_CM_CONFIG {
    ULONG MaxFrame;     /* the largest Ethernet frame sent and accepted  */
} NET_CM_CONFIG, *PNET_CM_CONFIG;

typedef struct _NET_CM_INFO {
    UCHAR Mac[6];       /* this adapter's (record 35 section 4.4)        */
    UCHAR PeerMac[6];
    ULONG LinkUp;
    ULONG PeerStns;     /* the peer's prtcstns                           */
    ULONG RateMbps;     /* the link's rate and width; 0 when unknown     */
    ULONG MaxFrame;     /* the service's maximum frame                   */
} NET_CM_INFO, *PNET_CM_INFO;

typedef NTSTATUS (*NET_CM_OPEN)(PVOID Context,
                                const NET_CM_CALLBACKS *Callbacks,
                                PVOID ClientContext,
                                const NET_CM_CONFIG *Config);
typedef VOID (*NET_CM_CLOSE)(PVOID Context);
typedef VOID (*NET_CM_QUERY_INFO)(PVOID Context, PNET_CM_INFO Info);
/* Returns 1 with Count buffers (4096 bytes each) and a token, or 0. */
typedef ULONG (*NET_CM_TX_RESERVE)(PVOID Context, ULONG Count,
                                   PUCHAR *Buffers, PULONG Token);
/* Returns 1 when posted, 0 when refused (an earlier generation's token);
 * the reservation is released either way. */
typedef ULONG (*NET_CM_TX_COMMIT)(PVOID Context, ULONG Token, ULONG Count,
                                  const ULONG *Lengths);
typedef VOID (*NET_CM_TX_ABORT)(PVOID Context, ULONG Token);
typedef VOID (*NET_CM_LOG)(PVOID Context, const char *Label, ULONG Value);

typedef struct _NET_CM_INTERFACE {
    INTERFACE Header;
    NET_CM_OPEN Open;               /* PASSIVE                           */
    NET_CM_CLOSE Close;             /* PASSIVE                           */
    NET_CM_QUERY_INFO QueryInfo;    /* <= DISPATCH                       */
    NET_CM_TX_RESERVE TxReserve;    /* <= DISPATCH                       */
    NET_CM_TX_COMMIT TxCommit;      /* <= DISPATCH                       */
    NET_CM_TX_ABORT TxAbort;        /* <= DISPATCH                       */
    NET_CM_LOG Log;                 /* <= DISPATCH                       */
} NET_CM_INTERFACE, *PNET_CM_INTERFACE;

#endif /* NET_CM_H */
