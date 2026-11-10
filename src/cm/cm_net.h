/*
 * cm_net.h - the Thunderbolt networking service's driver half in
 * xhcicm.sys (design record 35 sections 5 and 6; roadmap-usb4.md task
 * 45.59, step S5): the network PDO, its interface (src\xhci_cmnet.h), the
 * data rings' memory, lock, poll timer and service DPC, and the operations
 * either connection manager's inter-domain links call (TBT_U4_NET,
 * tbt_u4cm.h; TBT_ICM_NET, tbt_icm.h, the same signatures). Included by
 * cm.h; the pure ring half is cm_netr.h.
 *
 * THE PDO (record 35 sections 6.1 and 6.2). A fourth kind of device object
 * (CM_KIND_NETPDO), the child of the NHI's FDO: created at the NHI's first
 * start whose XhciCmNet is 1 under either connection manager and reported
 * in its BusRelations at every such start; kept across an orderly
 * stop; reported no more by a start with XhciCmNet 0 or after a surprise
 * removal. Deleted at the NHI's removal once the PnP manager has removed
 * it, or at its own removal once the NHI has orphaned it (Windows 2000 and
 * later; on 98 SE and ME an orphan is left allocated, as more IRPs may
 * follow its removal there, hcd_pdo.c's measurement). An answer that
 * leaves it out retires it: never reported again, and no fresh PDO made
 * until its removal has come (section 6.6, task 48.16).
 *
 * THE INTERFACE. IRP_MN_QUERY_INTERFACE for GUID_XHCI_CM_NET (version 1)
 * at the PDO, and the same table from IRP_MJ_INTERNAL_DEVICE_CONTROL
 * CM_NET_IOCTL_INTERFACE (Parameters.Others.Argument1 the caller's
 * XHCI_CM_NET_INTERFACE with Header.Size and Header.Version set), the
 * fallback record 35 section 6.3 keeps for 98 SE should NTKERN not carry
 * the query between two WDM drivers. Either answer holds one reference the
 * caller releases with InterfaceDereference.
 *
 * LOCKS. CM_NET.Lock (a spin lock) guards the client, the service's state,
 * both rings and the reservation; never held across a callback into the
 * network driver nor across a wait. OwnerLock guards the receive service's
 * owner word alone (record 35 section 5.4). CM_NETPDO.Lock guards the PDO's
 * way to its NHI.
 */

#ifndef CM_NET_H
#define CM_NET_H

#include "../xhci_cmnet.h"
#include "cm_netr.h"

#define CM_KIND_NETPDO 0x504D4358UL  /* 'XCMP': the network PDO */

/* The 98 SE fallback's private code (record 35 section 6.3), the shared
 * header's; cm_net.c checks it against CTL_CODE. */
#define CM_NET_IOCTL_INTERFACE XHCI_CM_NET_IOCTL_INTERFACE

/* Bounds of the service's waits on the thread (record 35 sections 4.2,
 * 5.1 and 6.3). */
#define CM_NET_TX_DRAIN_MS      500UL   /* the transmit ring drained at down */
#define CM_NET_RES_WAIT_MS      1000UL  /* a reservation held: logged after */
#define CM_NET_ACK_MS           5000UL  /* Open and Close: the thread's pass */
#define CM_NET_CB_WAIT_MS       5000UL  /* Close: a callback running, logged */
#define CM_NET_REF_WAIT_MS      2000UL  /* a removal: the interface's holds  */

struct _CM_NHI;

/* The network PDO's extension. */
typedef struct _CM_NETPDO {
    CM_COMMON Common;       /* Kind CM_KIND_NETPDO; Pdo and Lower NULL     */
    KSPIN_LOCK Lock;        /* guards Gate                                 */
    CM_NETR_GATE Gate;      /* the way to the NHI (struct _CM_NHI *),      */
                            /* closed by whichever removal unlinks it      */
    LONG Refs;              /* the interface's holds                       */
    ULONG Started;
    ULONG RemoveReceived;
    ULONG Reported;         /* given in a BusRelations answer (PdoLock)    */
    ULONG Retired;          /* left out of a later one (PdoLock)           */
    LONG DeleteClaim;       /* the first to raise it to 1 deletes          */
    ULONG Deleted;
} CM_NETPDO, *PCM_NETPDO;

/* Counters, emitted at the NHI's stop as cm.net.* lines. */
typedef struct _CM_NET_COUNTERS {
    ULONG Opens;
    ULONG Closes;
    ULONG Services;         /* RingsUp that started both rings             */
    ULONG RingsFailed;
    ULONG LinkUps;
    ULONG LinkDowns;
    ULONG RxFrames;         /* handed to Receive                           */
    ULONG RxErrors;         /* of them with CRC Error or Buffer Overflow   */
    ULONG RxBadPdf;         /* completed with an EOF PDF not 2: dropped    */
    ULONG RxUnheld;         /* completed with no client or link: dropped   */
    ULONG TxFrames;         /* descriptors posted                          */
    ULONG TxReaped;
    ULONG TxRefused;        /* TxReserve answered 0                        */
    ULONG TxStale;          /* a commit with a stale token                 */
    ULONG TxDrainLeft;      /* descriptors not reaped at a down's 500 ms   */
    ULONG ResSlow;          /* a reservation still held after 1 s          */
    ULONG Passes;           /* receive service passes                      */
    ULONG Kicks;
    ULONG Coalesced;        /* a kick that found the DPC queued            */
    ULONG Contended;        /* a service entry that found an owner         */
    ULONG Timers;           /* poll timer expiries                         */
    ULONG StopUnverified;   /* Ring Valid not read back clear              */
    ULONG MemKept;          /* the rings' memory kept at a stop            */
    ULONG NoMem;
    ULONG IrqKicks;         /* kicks from the interrupt DPC (record 35 5.5) */
} CM_NET_COUNTERS;

typedef struct _CM_NET {
    /* The PDO's bookkeeping, under PdoLock (record 35 section 6.6): every
     * decision on Pdo, Report, Retired and a PDO's Reported and Retired,
     * and BusRelations' reference, made under it; no IRP sent, no device
     * made or deleted with it held. */
    KSPIN_LOCK PdoLock;
    PDEVICE_OBJECT Pdo;     /* the network PDO, NULL before the first      */
    ULONG Report;           /* this start reports it                       */
    PDEVICE_OBJECT Retired; /* left out of an answer, its removal not yet
                               come: never reported again, and no fresh
                               PDO made until it is gone                   */
    LONG PdoCalls;          /* a PDO's removal holding the NHI: taken
                               before it unlinks itself, dropped once its
                               gate is closed and idle; the NHI's removal
                               waits for 0                                 */
    KSPIN_LOCK Lock;

    /* The client (under Lock). */
    ULONG Opened;
    XHCI_CM_NET_CALLBACKS Cb;
    PVOID ClientContext;
    ULONG MaxFrame;
    LONG CbActive;          /* callbacks running                           */

    /* Which connection manager this start's operations were given to
     * (CmNetBindOps): 0 the software one (TbtU4SetNet), 1 the firmware
     * one (TbtIcmSetNet). */
    ULONG Fw;

    /* What the thread applies (TbtU4NetWanted or TbtIcmNetWanted), and its
     * acknowledgement. Applied is reset at each activation. */
    volatile ULONG Wanted;
    volatile ULONG WantedStns;
    ULONG Applied;
    ULONG AppliedStns;
    KEVENT Ack;

    /* The service (under Lock but where noted). */
    ULONG RingsOn;          /* both rings enabled by RingsUp               */
    ULONG Up;               /* Link up: sends accepted, frames delivered   */
    ULONG Closing;          /* the rundown: no re-arm, kick or service     */
    ULONG TimerOn;          /* the poll timer set and not yet expired      */
    ULONG Generation;       /* incremented at every end and Close          */
    ULONG Ring;
    ULONG E2E;
    ULONG Port;
    ULONG MacValid;
    UCHAR Mac[6];
    UCHAR PeerMac[6];
    ULONG PeerStns;
    ULONG LinkMbps;
    ULONG Lanes;
    CM_NETR Tx;
    CM_NETR Rx;
    CM_NETR_RES Res;
    ULONG PollMs;           /* XhciCmNetPollMs as this start read it       */
    ULONG BadPdfLogged;
    /* Receive descriptors taken by the service (every one with DD, any
     * kind), the data cause's progress for the watchdog (record 35
     * section 5.5): written under Lock by the service's owner, read
     * without it as a monotonic ULONG. */
    volatile ULONG RxTaken;

    /* The rings' memory (cm_pool.c): at the first service after a start,
     * kept until the stop; MemMayRun while a ring may still write it. */
    ULONG MemOn;
    ULONG MemMayRun;
    ULONG MemKept;          /* kept at a stop: the adapter is not put back */
    ULONG RxFrames;
    ULONG TxFrames;
    PVOID RxDescVa;
    PVOID TxDescVa;
    ULONG RxDescPa;
    ULONG TxDescPa;
    PVOID RxBufVa[CM_NETR_MAX_FRAMES];
    PVOID TxBufVa[CM_NETR_MAX_FRAMES];
    ULONG RxBufPa[CM_NETR_MAX_FRAMES];
    ULONG TxBufPa[CM_NETR_MAX_FRAMES];

    /* The poll timer, its DPC and the service DPC (record 35 section 5.4),
     * in the NHI's extension for its life. InFlight counts the timer's
     * share while it is set and the service's while it is queued or
     * running, plus one while the rings are on; Idle is set when it
     * reaches 0. */
    KDPC ServiceDpc;
    KDPC TimerDpc;
    KTIMER Timer;
    volatile LONG InFlight;
    KEVENT Idle;
    KSPIN_LOCK OwnerLock;
    ULONG Owner;
    ULONG Pending;
    ULONG Pinned;           /* the image pinned at the first timer arming  */

    CM_NET_COUNTERS Counters;
} CM_NET, *PCM_NET;

#endif /* CM_NET_H */
