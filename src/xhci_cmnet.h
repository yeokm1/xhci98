/*
 * xhci_cmnet.h - the Thunderbolt networking service xhcicm.sys gives this
 * project's network drivers (roadmap-usb4.md task 45.59, step S5's
 * interface, given ahead of S5 for S6 and S7; design record 35 sections
 * 6.1 to 6.4). Shared by xhcicm.sys and the network drivers (NDIS 5 and
 * NDIS 6); this file defines only the interface's shape and its GUID.
 *
 * WHO ASKS WHOM. The network driver is the function driver of xhcicm.sys's
 * network PDO (hardware id XHCICM\TBNET, one per NHI, record 35 section
 * 6.2). At its initialisation it sends IRP_MN_QUERY_INTERFACE for
 * GUID_XHCI_CM_NET, Size sizeof(XHCI_CM_NET_INTERFACE), Version
 * XHCI_CM_NET_VERSION, to the device object below it (NDIS 5 and 6:
 * NdisMGetDeviceProperty's NextDeviceObject). The PDO answers with the
 * table below; any other GUID is passed back unanswered. A Size smaller
 * than this version's, or another Version, is refused
 * (STATUS_NOT_SUPPORTED). The interface's InterfaceReference and
 * InterfaceDereference count the network driver's hold; the PDO is not
 * deleted while it is held.
 *
 * THE 98 SE AND ME FALLBACK (record 35 sections 6.3 and 6.5). Whether
 * NTKERN carries a query-interface IRP built by one WDM driver to another
 * is unread there [inf], so the PDO also answers the same table to an
 * IRP_MJ_INTERNAL_DEVICE_CONTROL with XHCI_CM_NET_IOCTL_INTERFACE, on
 * every system. The caller sets Parameters.DeviceIoControl.IoControlCode
 * to that code and Parameters.Others.Argument1 to its own
 * XHCI_CM_NET_INTERFACE with Header.Size (sizeof(XHCI_CM_NET_INTERFACE))
 * and Header.Version (XHCI_CM_NET_VERSION) set; the PDO fills the table in
 * place and completes STATUS_SUCCESS with one reference held, as the query
 * does, or STATUS_NOT_SUPPORTED for a NULL table, a smaller Size or
 * another Version. The network drivers ask the query first and send this
 * code only where the query is not answered.
 *
 * THE SERVICE. Open is the service's enable and Close its disable: only
 * between them does xhcicm.sys publish the USB4NET directory, log in and
 * accept logins. The service is up (LinkChange TRUE) once both logins are
 * done and both DMA paths built, down (LinkChange FALSE) at a logout of
 * either side, an unplug, the NHI's stop or Close. Frames are copied both
 * ways: the network driver writes into transmit buffers it reserved and
 * copies each received frame out before Receive returns; no buffer of the
 * network stack ever reaches the host interface.
 *
 * A frame here is one USB4 frame of USB4NET (IDS 5.1.5; record 35 section
 * 7.2): its 12-byte frame header and its data, at most
 * XHCI_CM_NET_BUFFER_BYTES. Splitting an Ethernet packet into frames and
 * joining them is the network driver's.
 *
 * IRQL. Open and Close at PASSIVE_LEVEL; QueryInfo, TxReserve, TxCommit,
 * TxAbort and Log at IRQL <= DISPATCH_LEVEL. Callbacks: LinkChange and
 * Stats at PASSIVE_LEVEL on xhcicm.sys's thread; Receive and TxDone at
 * DISPATCH_LEVEL in xhcicm.sys's service DPC. No lock of xhcicm.sys is held
 * across a callback, and a callback must not call Open or Close.
 *
 * Every function and callback is NTAPI (__stdcall on x86), so the images
 * built by the 2000 DDK and the WDK agree whatever each build's default.
 * The header compiles in the drivers' and the host build; the INTERFACE-
 * headed table is left out of the host build.
 */

#ifndef XHCI_CMNET_H
#define XHCI_CMNET_H

#include "xhci_compat.h"

/* {7B2DADF2-FEED-49FB-ADD1-4C8A00E2E503} */
#define XHCI_CM_NET_GUID_DATA1  0x7B2DADF2UL
#define XHCI_CM_NET_GUID_DATA2  0xFEED
#define XHCI_CM_NET_GUID_DATA3  0x49FB
#define XHCI_CM_NET_GUID_DATA4 \
    { 0xAD, 0xD1, 0x4C, 0x8A, 0x00, 0xE2, 0xE5, 0x03 }

#define XHCI_CM_NET_VERSION     1

/* The fallback's private code: CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA59,
 * METHOD_NEITHER, FILE_ANY_ACCESS), written out so the host build needs
 * no CTL_CODE. */
#define XHCI_CM_NET_IOCTL_INTERFACE     0x00222967UL

/* Results. */
#define XHCI_CM_NET_OK          0L
#define XHCI_CM_NET_E_BUSY      (-1L)   /* Open: already open */
#define XHCI_CM_NET_E_STATE     (-2L)   /* not started, stopping, or a stale token */
#define XHCI_CM_NET_E_PARAM     (-3L)   /* a size, count or length out of range */

/* One USB4 frame's buffer: its header and data (record 35 sections 5.3,
 * 7.2). */
#define XHCI_CM_NET_BUFFER_BYTES        4096UL
/* The most descriptors one reservation may take: a 64 KiB packet split
 * into 4084-byte frames (record 35 section 7.2). */
#define XHCI_CM_NET_TX_MAX_RESERVE      17UL
/* The Ethernet frame sizes Open accepts (record 35 section 7.3): MTU 68
 * to 65522 plus the 14-byte header. */
#define XHCI_CM_NET_FRAME_MIN           82UL
#define XHCI_CM_NET_FRAME_MAX           65536UL
#define XHCI_CM_NET_FRAME_STANDARD      1514UL

/* Receive's Flags (from the receive descriptor, Spec Table 12-26): such a
 * frame is counted and dropped by the network driver. */
#define XHCI_CM_NET_RX_CRC_ERROR        0x00000001UL
#define XHCI_CM_NET_RX_OVERFLOW         0x00000002UL

/* prtcstns bits (IDS Table 5-2), as QueryInfo reports the peer's. */
#define XHCI_CM_NET_STNS_E2E            0x00000001UL
#define XHCI_CM_NET_STNS_FRAME_ID       0x00000002UL
#define XHCI_CM_NET_STNS_64K            0x00000004UL

/* ---------------------------------------------------------------------- */
/* What the network driver gives Open                                     */
/* ---------------------------------------------------------------------- */

typedef VOID (NTAPI *XHCI_CM_NET_LINK_CHANGE)(PVOID ClientContext, BOOLEAN Up);
/* `Frame` valid until the callback returns. */
typedef VOID (NTAPI *XHCI_CM_NET_RECEIVE)(PVOID ClientContext, const VOID *Frame,
                                          ULONG Length, ULONG Flags);
/* `Count` transmit descriptors reaped: a sender waiting on a full ring may
 * try again. */
typedef VOID (NTAPI *XHCI_CM_NET_TX_DONE)(PVOID ClientContext, ULONG Count);

/* The network driver's counters, for the status query the xHCI98 USB Tree
 * reads (design record 35 section 17, task 48.18): the caller sets Size;
 * the callback fills the fields Size covers, as its general statistics
 * OIDs would answer them. Every field a ULONG, so the query's client
 * block is these words in this order (cm_qnet.h CM_QN_C_*). */
#define XHCI_CM_NET_STATS_NDIS5         1UL     /* xhcinet.sys  */
#define XHCI_CM_NET_STATS_NDIS6         2UL     /* xhcinet6.sys */
#define XHCI_CM_NET_STATS_F_BYTES32     0x00000001UL    /* bytes wrap at 2^32 */
#define XHCI_CM_NET_STATS_F_ASLEEP      0x00000002UL    /* D3, or paused */

typedef struct _XHCI_CM_NET_STATS {
    ULONG Size;                         /* in: sizeof(XHCI_CM_NET_STATS) */
    ULONG Driver;                       /* XHCI_CM_NET_STATS_NDIS* */
    ULONG Mtu;
    ULONG Flags;                        /* XHCI_CM_NET_STATS_F_* */
    ULONG TxPackets;                    /* OID_GEN_XMIT_OK */
    ULONG TxBytesLo;
    ULONG TxBytesHi;
    ULONG RxPackets;                    /* OID_GEN_RCV_OK */
    ULONG RxBytesLo;
    ULONG RxBytesHi;
    ULONG TxErrors;                     /* OID_GEN_XMIT_ERROR */
    ULONG RxErrors;                     /* OID_GEN_RCV_ERROR */
    ULONG RxNoBuffer;                   /* OID_GEN_RCV_NO_BUFFER */
    ULONG TxDown;                       /* sends completed: link down */
    ULONG RxNotUp;                      /* frames while the link was down */
    ULONG RxSplit;                      /* large segments split */
    ULONG TxFullWaits;                  /* sends that waited for a descriptor */
} XHCI_CM_NET_STATS, *PXHCI_CM_NET_STATS;

/* On xhcicm.sys's thread at PASSIVE_LEVEL, no lock of xhcicm.sys held,
 * between Open and Close: copy the counters and return. It must not wait,
 * and must not call the interface. */
typedef VOID (NTAPI *XHCI_CM_NET_STATS_FN)(PVOID ClientContext,
                                           PXHCI_CM_NET_STATS Stats);

/* The first three callbacks alone: what Open accepts at the least. A
 * driver from before task 48.18 passes this Size and is never asked for
 * Stats; Stats is taken only from a Size that covers it. */
typedef struct _XHCI_CM_NET_CALLBACKS_V1 {
    ULONG Size;
    XHCI_CM_NET_LINK_CHANGE LinkChange;
    XHCI_CM_NET_RECEIVE Receive;
    XHCI_CM_NET_TX_DONE TxDone;
} XHCI_CM_NET_CALLBACKS_V1;

#define XHCI_CM_NET_CALLBACKS_V1_BYTES  sizeof(XHCI_CM_NET_CALLBACKS_V1)

typedef struct _XHCI_CM_NET_CALLBACKS {
    ULONG Size;                         /* sizeof(XHCI_CM_NET_CALLBACKS) */
    XHCI_CM_NET_LINK_CHANGE LinkChange;
    XHCI_CM_NET_RECEIVE Receive;
    XHCI_CM_NET_TX_DONE TxDone;
    XHCI_CM_NET_STATS_FN Stats;         /* may be NULL */
} XHCI_CM_NET_CALLBACKS, *PXHCI_CM_NET_CALLBACKS;

typedef struct _XHCI_CM_NET_CONFIG {
    ULONG Size;                         /* sizeof(XHCI_CM_NET_CONFIG) */
    /* The largest Ethernet frame the driver sends and accepts, its MTU
     * plus 14 (XHCI_CM_NET_FRAME_MIN to _MAX): above 1514 the published
     * prtcstns carries bit 2 (record 35 section 3.7). */
    ULONG MaxFrame;
} XHCI_CM_NET_CONFIG, *PXHCI_CM_NET_CONFIG;

/* ---------------------------------------------------------------------- */
/* What QueryInfo reports                                                 */
/* ---------------------------------------------------------------------- */

typedef struct _XHCI_CM_NET_INFO {
    ULONG Size;                         /* in: sizeof(XHCI_CM_NET_INFO) */
    BOOLEAN LinkUp;                     /* the service is up */
    UCHAR Mac[6];                       /* this adapter's (record 35 section 4.4) */
    UCHAR PeerMac[6];                   /* the peer's Login Response's; 0 while down */
    ULONG PeerPrtcstns;                 /* XHCI_CM_NET_STNS_*; 0 while down */
    /* The USB4 link's whole rate in Mb/s, every lane in use counted (40000
     * for a bonded Gen 3 link), not per lane; 0 when unknown. */
    ULONG LinkMbps;
    ULONG Lanes;                        /* 1 or 2; 0 when unknown */
    ULONG MaxFrame;                     /* the service's largest Ethernet frame */
    ULONG TxDescriptors;                /* the transmit ring's size, 0 while down */
    ULONG RxDescriptors;
} XHCI_CM_NET_INFO, *PXHCI_CM_NET_INFO;

/* A transmit reservation: the service's generation when it was made and
 * its first descriptor. A token of an earlier generation (the service
 * ended, perhaps came back, in between) posts nothing. */
typedef struct _XHCI_CM_NET_TOKEN {
    ULONG Generation;
    ULONG First;
    ULONG Count;
} XHCI_CM_NET_TOKEN, *PXHCI_CM_NET_TOKEN;

/* ---------------------------------------------------------------------- */
/* The table's functions (Context: the PDO's, from the INTERFACE header)  */
/* ---------------------------------------------------------------------- */

/* XHCI_CM_NET_OK, _E_BUSY when one is open, _E_STATE outside the PDO's
 * start, _E_PARAM for a size out of range. */
typedef LONG (NTAPI *XHCI_CM_NET_OPEN)(PVOID Context,
                                       const XHCI_CM_NET_CALLBACKS *Callbacks,
                                       PVOID ClientContext,
                                       const XHCI_CM_NET_CONFIG *Config);
/* Ends any service with a logout, withdraws the directory, refuses logins
 * until the next Open, waits until no callback is running or can start,
 * and drops the callbacks. */
typedef VOID (NTAPI *XHCI_CM_NET_CLOSE)(PVOID Context);
typedef LONG (NTAPI *XHCI_CM_NET_QUERY_INFO)(PVOID Context, PXHCI_CM_NET_INFO Info);
/* All or nothing: `Count` (1 to XHCI_CM_NET_TX_MAX_RESERVE) consecutive
 * transmit descriptors, their buffers' addresses in Buffers[0..Count-1]
 * (each XHCI_CM_NET_BUFFER_BYTES) and the token; returns Count, or 0 with
 * nothing reserved when fewer are free, another reservation is
 * outstanding (one at a time: the caller serialises its sends), the
 * service is closing or none is up. */
typedef ULONG (NTAPI *XHCI_CM_NET_TX_RESERVE)(PVOID Context, ULONG Count, PVOID *Buffers,
                                              PXHCI_CM_NET_TOKEN Token);
/* Posts the reservation, Lengths[0..Count-1] bytes each (12, the frame
 * header alone, to XHCI_CM_NET_BUFFER_BYTES), and writes the producer
 * index once; XHCI_CM_NET_OK. With a stale token: nothing posted, the reservation
 * that token names released all the same, XHCI_CM_NET_E_STATE (the frame
 * is completed as not sent). */
typedef LONG (NTAPI *XHCI_CM_NET_TX_COMMIT)(PVOID Context, const XHCI_CM_NET_TOKEN *Token,
                                            ULONG Count, const ULONG *Lengths);
/* Releases the reservation, of the current generation or an earlier one;
 * nothing posted. */
typedef VOID (NTAPI *XHCI_CM_NET_TX_ABORT)(PVOID Context, const XHCI_CM_NET_TOKEN *Token);
/* A line in xhcicm.sys's log ring (`Label` a static string). */
typedef VOID (NTAPI *XHCI_CM_NET_LOG)(PVOID Context, const char *Label, ULONG Value);

#if !defined(XHCI_HOST_TEST)
/* The INTERFACE header (Size, Version, Context, InterfaceReference,
 * InterfaceDereference), then the functions. */
typedef struct _XHCI_CM_NET_INTERFACE {
    INTERFACE Header;
    XHCI_CM_NET_OPEN Open;
    XHCI_CM_NET_CLOSE Close;
    XHCI_CM_NET_QUERY_INFO QueryInfo;
    XHCI_CM_NET_TX_RESERVE TxReserve;
    XHCI_CM_NET_TX_COMMIT TxCommit;
    XHCI_CM_NET_TX_ABORT TxAbort;
    XHCI_CM_NET_LOG Log;
} XHCI_CM_NET_INTERFACE, *PXHCI_CM_NET_INTERFACE;
#endif

/* An initializer for a `static const GUID` of each driver's own. */
#define XHCI_CM_NET_GUID_INIT \
    { XHCI_CM_NET_GUID_DATA1, XHCI_CM_NET_GUID_DATA2, \
      XHCI_CM_NET_GUID_DATA3, XHCI_CM_NET_GUID_DATA4 }

#endif /* XHCI_CMNET_H */
