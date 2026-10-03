/*
 * hcd.h - the WDM objects of xhci98.sys, the successor host controller driver
 * (design record 13 section 5.2; roadmap-hcd.md Phase 26).
 *
 * One driver object in two PnP roles. As the controller's function driver it
 * owns a controller FDO attached over the PCI PDO; as the root hub's it owns a
 * root-hub FDO attached over the root-hub PDO the controller FDO created. The
 * role is decided at AddDevice by whether this driver object created the PDO
 * (section 5.2), and every extension begins with HCD_COMMON so a dispatch
 * routine can tell which object it was handed.
 *
 * The controller FDO's extension embeds the miniport's XHCI_EXTENSION, which
 * the kept controller sequence (xhci_init.c, xhci_cmd.c, xhci_evt.c) runs on
 * unchanged; HcdControllerFromExt is the way back from it.
 *
 * DDK-dependent: this header is for the kernel files only. The pure core never
 * includes it.
 */

#ifndef HCD_H
#define HCD_H

#include "xhci_compat.h"
#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_enum.h"
#include "xhci_pipe.h"
#include "xhci_func.h"
#include "xhci_hub.h"
#include "xhci_counters.h"

#define HCD_KIND_CONTROLLER_FDO 0x43464448UL /* 'HDFC' */
#define HCD_KIND_ROOTHUB_PDO    0x50524448UL /* 'HDRP' */
#define HCD_KIND_ROOTHUB_FDO    0x46524448UL /* 'HDRF' */
#define HCD_KIND_DEVICE_PDO     0x50444448UL /* 'HDDP' */

/* The PnP state the dispatch routines gate on. Windows 98 can deliver a
 * REMOVE as the first PnP IRP after START (no SURPRISE_REMOVAL before it;
 * win98-wdm.md), so every state may move straight to HCD_PNP_REMOVED. */
#define HCD_PNP_ADDED            0UL
#define HCD_PNP_STARTED          1UL
#define HCD_PNP_STOP_PENDING     2UL
#define HCD_PNP_STOPPED          3UL
#define HCD_PNP_REMOVE_PENDING   4UL
#define HCD_PNP_SURPRISE_REMOVED 5UL
#define HCD_PNP_REMOVED          6UL

typedef struct _HCD_COMMON {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    ULONG PnpState;
    ULONG PnpStateBeforeQuery;
    DEVICE_POWER_STATE DevicePower;
    SYSTEM_POWER_STATE SystemPower;
} HCD_COMMON, *PHCD_COMMON;

/* The one-shot timer service's slots (hcd_svc.c). Four: the command
 * watchdog is the only user, and a new arm of the same callback supersedes
 * one still pending; the rest absorb callbacks already fired whose DPCs
 * have not yet freed their slots. */
#define HCD_TIMER_SLOTS         4UL
#define HCD_TIMER_CONTEXT_BYTES 32UL

struct _HCD_CONTROLLER;

typedef struct _HCD_TIMER {
    KTIMER Timer;
    KDPC Dpc;
    struct _HCD_CONTROLLER *Controller;
    volatile ULONG Busy;
    XHCI_ASYNC_TIMER_CALLBACK *Callback;
    ULONG ContextLength;            /* under TimerLock                    */
    UCHAR Context[HCD_TIMER_CONTEXT_BYTES];
} HCD_TIMER, *PHCD_TIMER;

/*
 * The URB transfer path (hcd_io.c, 26-A.5; design record 13 sections 6 and
 * 11.3). A pipe is one endpoint of one device; each carries a fixed set of
 * transfer records, so no transfer allocates (section 7.5 rule 3). A record
 * is the transfer engine's XHCI_TRANSFER first, so a retired engine record
 * leads back to it, then what the URB, the MDL and the map registers need.
 * An IRP no record can take waits on the pipe's Waiting list, linked through
 * its Tail.Overlay.ListEntry.
 */
#define HCD_PIPE_SIGNATURE  0x45504950UL    /* 'PIPE' */

/* USBD statuses the HCD's own files set, where usbdi.h is not included:
 * DEVICE_GONE and BUFFER_TOO_SMALL as WDK 7.1's inc\api\usb.h:459 and :451
 * define them (absent from the Windows 2000 DDK), the rest as the Windows
 * 2000 DDK's inc\usbdi.h does (lines 255, 263 and 279). */
#define HCD_USBD_DEVICE_GONE        ((LONG)0xC0007000L)
#define HCD_USBD_BUFFER_TOO_SMALL   ((LONG)0xC0003000L)
#define HCD_USBD_NO_MEMORY          ((LONG)0x80000100L)
#define HCD_USBD_ERROR_BUSY         ((LONG)0x80000400L)
#define HCD_USBD_INTERNAL_HC_ERROR  ((LONG)0x80000800L)
#define HCD_USBD_INVALID_PIPE       ((LONG)0x80000600L)
#define HCD_USBD_INVALID_PARAMETER  ((LONG)0x80000300L)
#define HCD_USBD_ERROR_SHORT_TRANSFER ((LONG)0x80000900L)
/* Windows 2000 DDK inc\usbdi.h:290 and :294. */
#define HCD_USBD_BAD_START_FRAME    ((LONG)0xC0000A00L)
#define HCD_USBD_ISOCH_REQUEST_FAILED ((LONG)0xC0000B00L)

/* HcdIoSubmit flags. */
#define HCD_IO_IN           0x1UL   /* data moves device to host          */
#define HCD_IO_SHORT_OK     0x2UL   /* USBD_SHORT_TRANSFER_OK              */
#define HCD_IO_ISOCH        0x4UL   /* URB_FUNCTION_ISOCH_TRANSFER         */
#define HCD_IO_ASAP         0x8UL   /* USBD_START_ISO_TRANSFER_ASAP        */

/* One transfer as a URB asks for it (hcd_urb.c, HcdUrbIoRequest): read
 * once at dispatch, and again from the URB when an IRP that waited on its
 * pipe is given a record (hcd_io.c). */
typedef struct _HCD_IO_REQUEST {
    PVOID Handle;                   /* the pipe; NULL is the default pipe  */
    ULONG Control;                  /* Setup holds the SETUP bytes         */
    UCHAR Setup[8];
    ULONG Flags;                    /* HCD_IO_*                            */
    PVOID Buffer;
    PMDL Mdl;                       /* wins over Buffer                    */
    ULONG Length;
    PULONG LengthOut;               /* the URB's TransferBufferLength      */
} HCD_IO_REQUEST, *PHCD_IO_REQUEST;
/* Windows 2000 DDK incSbdi.h:312: a success-class value there. */
#define HCD_USBD_CANCELED           ((LONG)0x00010000L)
#define HCD_PIPE_XFERS      4UL
/* Data elements per chunk (xhci_pipe.h XHCI_PIPE_CHUNK_ELEMENTS), and the
 * one slot the SG list's own declaration already holds beyond them. */
#define HCD_SG_ELEMENTS     32UL

/* An isochronous record's request in the transfer engine's input format
 * (xhci_usbport.h), which the engine was written against and no usbport
 * builds here: one per record of an isochronous pipe, allocated with the
 * pipe (hcd_cfg.c), so no transfer allocates. */
typedef struct _HCD_ISO_BLOCK {
    USBPORT_ISO_TRANSFER Block;
    USBPORT_ISO_PACKET More[XHCI_XFER_MAX_ISO_PACKETS - 1];
} HCD_ISO_BLOCK, *PHCD_ISO_BLOCK;

/* The engine indexes Packet[] past its declared one into More[], so More
 * must start exactly where Packet[1] would: no padding between them on
 * either architecture (roadmap-hcd.md task 28-A.2). Block is the first
 * member; MSVC 6.0 takes no nested member in a constant offset. */
XHCI_C_ASSERT(hcd_iso_block_tail,
              XHCI_OFFSET_OF(HCD_ISO_BLOCK, More) ==
                  XHCI_OFFSET_OF(USBPORT_ISO_TRANSFER, Packet) +
                      sizeof(USBPORT_ISO_PACKET));

#define HCD_XFER_FREE       0UL
#define HCD_XFER_MAPPING    1UL     /* in the map pump                    */
#define HCD_XFER_ON_RING    2UL     /* published; the engine owns it      */
#define HCD_XFER_DONE       3UL     /* retired, on the done list          */
#define HCD_XFER_HELD       4UL     /* mapped, waiting for its pipe       */

typedef struct _HCD_XFER {
    XHCI_TRANSFER Xfer;             /* first: CONTAINING_RECORD target    */
    LIST_ENTRY Link;                /* the map queue, then the done list  */
    struct _HCD_PIPE *Pipe;
    PIRP Irp;
    PVOID Urb;                      /* PURB; usbdi.h is hcd_io.c's alone  */
    ULONG State;                    /* HCD_XFER_*                          */
    ULONG Control;                  /* a control transfer (Setup used)    */
    ULONG In;                       /* data moves device to host          */
    XHCI_SETUP_PACKET Setup;
    PMDL Mdl;
    ULONG OwnMdl;                   /* built here; IoFreeMdl at the end   */
    ULONG Length;                   /* the URB's TransferBufferLength      */
    ULONG Offset;                   /* bytes of it moved by earlier chunks */
    ULONG Chunk;                    /* this chunk's bytes                  */
    PVOID MapBase;
    ULONG MapCount;
    LONG Status;                    /* USBD status before the engine's    */
    ULONG Engine;                   /* published: the engine's status and
                                     * byte count are the outcome          */
    PULONG LengthOut;               /* the URB's TransferBufferLength      */
    struct _HCD_DEVICE_PDO *Pdo;    /* whose stack sent it: its REMOVE waits */
    ULONG CancelRequested;          /* its IRP was cancelled (hcd_io.c)  */
    ULONG ShortOk;                  /* USBD_SHORT_TRANSFER_OK was set     */
    ULONG Isoch;                    /* URB_FUNCTION_ISOCH_TRANSFER        */
    ULONG Asap;                     /* USBD_START_ISO_TRANSFER_ASAP       */
    ULONG Seq;                      /* its pipe's submission order        */
    ULONG Mapped;                   /* held with its chunk mapped         */
    struct _HCD_XFER_SG {
        USBPORT_SCATTER_GATHER_LIST List;
        USBPORT_SCATTER_GATHER_ELEMENT More[HCD_SG_ELEMENTS];
    } Sg;
} HCD_XFER, *PHCD_XFER;

/* The same for the SG list: hcd_dma.c writes SgElement[n] for n up to
 * HCD_SG_ELEMENTS - 1, past the two the list declares. The list is
 * 8-aligned on amd64 (its CurrentVa), so a size that were not a multiple
 * of 8 there would leave a hole before More (task 28-A.2). List is the
 * first member, so its own offset of SgElement is the one that counts. */
XHCI_C_ASSERT(hcd_sg_list_tail,
              XHCI_OFFSET_OF(struct _HCD_XFER_SG, More) ==
                  XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElement) +
                      2 * sizeof(USBPORT_SCATTER_GATHER_ELEMENT));

typedef struct _HCD_PIPE {
    ULONG Signature;
    struct _HCD_USB_DEVICE *Device;
    ULONG Dci;
    ULONG EndpointAddress;
    ULONG TransferType;             /* XHCI_PIPE_XFER_*                    */
    ULONG MaxPacketSize;
    ULONG Interval;                 /* bInterval, as reported to clients  */
    ULONG Interface;                /* bInterfaceNumber it belongs to     */
    PXHCI_RING Ring;                /* EP0: the device's; else OwnRing    */
    PXHCI_TRANSFER_QUEUE Queue;     /* EP0: the device's; else OwnQueue   */
    XHCI_RING OwnRing;
    XHCI_TRANSFER_QUEUE OwnQueue;
    ULONG PoolIndex;
    LIST_ENTRY Waiting;             /* IRPs no record holds yet, in
                                     * submission order; controller lock  */
    ULONG Closed;                   /* deconfigured: no more submissions  */
    ULONG Halted;                   /* a STALL; the client resets the pipe */
    ULONG CancelPending;            /* a record of it was cancelled      */
    ULONG DrainPending;             /* a refused retire: stop and drain  */
    ULONG Paused;                   /* the thread is stopping or editing
                                     * it: nothing is published meanwhile */
    LIST_ENTRY Held;                /* records mapped while Paused, or
                                     * behind Exclusive                   */
    struct _HCD_XFER *Exclusive;    /* a request split into chunks: none
                                     * other is published until it ends   */
    ULONG Seq;                      /* the last submission's order         */
    XHCI_PIPE_EP Ep;                /* its descriptor, decoded (xhci_pipe) */
    PHCD_ISO_BLOCK Iso;             /* isochronous: HCD_PIPE_XFERS blocks  */
    ULONG IsoNext;                  /* the frame after the last queued
                                     * packet's; controller lock          */
    ULONG RingWait;                 /* a record waits for ring room;
                                     * controller lock                    */
    HCD_XFER Xfers[HCD_PIPE_XFERS];
} HCD_PIPE, *PHCD_PIPE;

/* A device the bus has addressed (hcd_enum.c; design record 13 section
 * 5.2's "device object (the bus's)"). Pool, per device, at enumeration
 * (section 7.5 rule 3). */
typedef struct _HCD_USB_DEVICE {
    struct _HCD_CONTROLLER *Controller;
    ULONG Port;             /* root port, 1-based: the path starts there */
    ULONG Location;         /* its port object, hc->Ports[Location - 1]:
                             * the root port itself, or a hub's port     */
    ULONG SlotId;
    ULONG Speed;            /* the Protocol Speed ID: PORTSC's on a root
                             * port, looked up for its class behind a hub */
    /* Its place behind hubs (27-A.2; design record 13 section 10.4), fixed
     * at Enable Slot and written into every Slot Context it is given
     * (HcdDeviceSlotParams): all 0 on a root port. */
    ULONG Route;            /* Route String                               */
    ULONG Tier;             /* hubs above it                              */
    ULONG TtSlot;           /* Parent (TT) Hub Slot ID, FS/LS behind HS   */
    ULONG TtPort;           /* Parent (TT) Port Number                    */
    ULONG TtMulti;          /* that hub runs its multi-TT interface       */
    /* A hub the bus serves (hcd_hub.c): its hub object, and what its own
     * Slot Context says of it once configured (Hub = 1). The status-change
     * transfer's record lives here, not in the hub object, because it is
     * the device's queue that holds it until the slot goes. */
    struct _HCD_HUB *Hub;
    ULONG HubMarked;        /* Configure Endpoint carries the hub fields  */
    ULONG HubPorts;         /* Number of Ports: bNbrPorts                 */
    ULONG HubTtt;           /* TT Think Time                              */
    ULONG HubMtt;           /* its multi-TT interface is enabled          */
    XHCI_TRANSFER HubXfer;
    ULONG HubXferArmed;     /* on the ring; controller lock               */
    ULONG HubXferDone;      /* retired, not yet looked at; controller lock */
    ULONG Mps0;
    XHCI_RING Ep0;
    /* EP0 through the transfer engine (xhci_xfer.c): its queue, the one
     * record the thread's control transfers use, and that record's end,
     * under the controller lock (hcd_dev.c, hcd_enum.c). */
    XHCI_TRANSFER_QUEUE Ep0Queue;
    XHCI_TRANSFER Ep0Xfer;
    ULONG Ep0Done;
    UCHAR DeviceDesc[18];
    PUCHAR Config;          /* the whole configuration descriptor       */
    ULONG ConfigLength;
    PDEVICE_OBJECT Pdo;     /* 26-A.4's device PDO, once it exists; for a
                             * split device its first function PDO, the
                             * rest on that PDO's Sibling chain          */
    ULONG PdoGroup;         /* the Group of its PDO(s)                   */
    ULONG Split;            /* configured by the bus for function PDOs
                             * (HcdCfgParentConfigure)                   */
    ULONG IfaceUsed;        /* split: interfaces (bit n) whose endpoints
                             * were opened since the device last restarted
                             * their toggles; a function's re-select owes
                             * them SET_INTERFACE (hcd_cfg.c)            */
    ULONG Abandoned;        /* off its port with the slot still enabled:
                             * the next powered pass disables it        */
    /* The URB path (hcd_io.c). Refs counts URB IRPs that hold the record,
     * taken under PdoListLock while the PDO still names it; Gone, under
     * the controller lock, refuses new submissions once the thread has
     * begun freeing it (hcdDeviceFree waits Refs out). */
    volatile LONG Refs;
    ULONG Gone;
    ULONG AbortAll;         /* the PDO is being removed: abort every pipe */
    ULONG Ep0Stuck;         /* the thread's EP0 record timed out and is
                             * still queued: no control transfer until
                             * the reset frees the device                 */
    ULONG Ep0Halted;        /* a URB's control transfer stalled: the
                             * thread owes Reset Endpoint + Set TR
                             * Dequeue (hcd_enum.c), controller lock   */
    HCD_PIPE Ep0Pipe;
    /* The configuration (hcd_cfg.c, the thread): the open pipes by DCI,
     * read by the event DPC under the controller lock, the pool rings they
     * hold, and the configuration value SET_CONFIGURATION sent. */
    PHCD_PIPE Pipes[32];
    ULONG PoolRings;
    ULONG ConfigValue;
    PUCHAR Selected;                /* the client's configuration, copied
                                     * at SELECT_CONFIGURATION, for
                                     * SELECT_INTERFACE                   */
    ULONG SelectedLength;
    ULONG Stale;                    /* endpoints (DCI bits) enabled on the
                                     * controller with no pipe, after a
                                     * failed select; the next Configure
                                     * Endpoint drops them                */
    UCHAR Alternate[32];            /* each interface's alternate setting
                                     * by bInterfaceNumber, as the last
                                     * select left it: RESET_PORT replays
                                     * the nonzero ones (hcd_cfg.c)       */
    ULONG FuncRelease;              /* split: interfaces (bit n) of
                                     * function PDOs removed, whose pipes
                                     * and alternates the thread owes a
                                     * release (hcd_cfg.c); controller
                                     * lock                               */
    UCHAR FuncReleaseTries[32];     /* each interface's failed release
                                     * passes in a row, so a refused one
                                     * is retried, but not forever; reset
                                     * when its debt is settled or taken
                                     * over (hcd_cfg.c); thread           */
} HCD_USB_DEVICE, *PHCD_USB_DEVICE;

/* Distinct pipe handles a PDO keeps an abort horizon for: more than any
 * configuration opens (31 endpoints and the default pipe). */
#define HCD_PDO_ABORTS 32UL

/* A device PDO (hcd_pdo.c): one per enumerated device, a child of the root
 * hub. Carries its own copies of the descriptors, so it may outlive the
 * device record; Device is the record while the device is present. */
typedef struct _HCD_DEVICE_PDO {
    HCD_COMMON Common;
    struct _HCD_CONTROLLER *Controller;
    struct _HCD_DEVICE_PDO *Next;   /* listed or gone list, PdoListLock    */
    PHCD_USB_DEVICE Device;         /* the record while present (thread)   */
    /* The lifecycle, under PdoListLock (hcd_pdo.c):                         */
    ULONG Listed;                   /* on DevicePdos: in the relations      */
    ULONG Reported;                 /* returned in a BusRelations answer    */
    ULONG MissingReported;          /* omitted from one since it was gone   */
    ULONG RemoveReceived;           /* PnP's IRP_MN_REMOVE_DEVICE seen      */
    ULONG Deleted;                  /* IoDeleteDevice called: once only     */
    ULONG Serial;                   /* the name's number; a port waits on it */
    ULONG Closing;                  /* stopping or removed: URBs refused  */
    volatile LONG UrbsPending;      /* URB IRPs pended here and not yet
                                     * completed; REMOVE waits for 0      */
    /* Refusals completed at the next clock tick (HcdIoRefuseLater):
     * the list under the cancel spin lock. RefusedPending counts each
     * refusal and one more while the timer is armed or its DPC runs;
     * REMOVE and the PDO's deletion wait for 0. */
    LIST_ENTRY RefusedIrps;
    KTIMER RefuseTimer;
    KDPC RefuseDpc;
    ULONG RefuseArmed;
    volatile LONG RefusedPending;
    /* URB IRPs of a device that left, held until the client aborts or
     * cancels them or the PDO stops or goes (HcdIoPark, hcd_io.c): the
     * list under the cancel spin lock, each still counted in UrbsPending. */
    LIST_ENTRY ParkedIrps;
    ULONG ParkedCount;
    /* Abort horizons (HcdIoAbortMark, hcd_io.c), under the cancel spin
     * lock: every URB IRP is stamped with SubmitSeq at its dispatch, in
     * its DriverContext[0], and an ABORT_PIPE records the sequence it saw
     * as its pipe's horizon - or, for a handle it cannot place, every
     * pipe's (AbortAll). A request whose stamp is at or below its pipe's
     * horizon was submitted before an abort of it and is completed
     * CANCELED rather than held, by a wrap-safe order that retires a
     * horizon 2^30 submissions old (xhci_pipe.h, XHCI_PIPE_SEQ_AGE);
     * none younger is cleared or evicted, and a table that is full
     * raises AbortAll instead. */
    volatile LONG SubmitSeq;
    ULONG AbortAll;
    ULONG AbortCount;
    PVOID AbortPipe[HCD_PDO_ABORTS];
    ULONG AbortHorizon[HCD_PDO_ABORTS];
    volatile LONG Busy;             /* dispatches inside hcd_urb.c, raised
                                     * before Controller is read; the
                                     * parent's release waits it out      */
    ULONG Port;                     /* its device's Location: the port
                                     * handshake (hcdPortNotify) names it */
    ULONG InstanceKey;              /* the instance id and the address
                                     * (XhciHubInstanceKey): the root port,
                                     * with the route above it behind hubs */
    ULONG Speed;
    ULONG SpeedClass;               /* XHCI_SPEED_*, decoded at creation:
                                     * the raw Speed is a PSIV whose
                                     * meaning the port's protocol decides,
                                     * and the PortMap a restart rewrites
                                     * is not read at dispatch            */
    UCHAR DeviceDesc[18];
    PUCHAR Config;
    ULONG ConfigLength;
    /* A function PDO of a split device (26-A.7; design record 13 section
     * 10.9): Config is then the function's filtered configuration
     * descriptor, not the device's, and only the pipes of InterfaceMask's
     * interfaces are its own. Fixed at creation. */
    ULONG Function;
    ULONG InterfaceMask;            /* bit n: bInterfaceNumber n           */
    XHCI_FUNC Func;
    /* Every PDO of one device: the serial a port waits on and a cycle
     * names (Group, the first PDO's Serial; a lone device PDO's own), and
     * the chain HcdDevicePdoGone walks when the device leaves (Sibling,
     * written before the PDOs are listed, read only by the thread). */
    ULONG Group;
    struct _HCD_DEVICE_PDO *Sibling;
} HCD_DEVICE_PDO, *PHCD_DEVICE_PDO;

/* Whether a PDO may use a pipe of its device: a device PDO any, a function
 * PDO the shared EP0 and its own interfaces' (design record 13 section
 * 10.9). Controller lock held, as for any read of a pipe. */
#define HcdPdoOwnsPipe(pdo, dev, pipe)                                       \
    (!(pdo)->Function || (pipe) == &(dev)->Ep0Pipe ||                        \
     ((pipe)->Interface < 32UL &&                                            \
      ((pdo)->InterfaceMask & (1UL << (pipe)->Interface)) != 0))

/*
 * A port: its enumeration machine and the device on it. The first
 * XHCI_MAX_ROOT_PORTS are the root ports, PortId the xHCI port number; the
 * rest are the ports of the hubs inside the bus (27-A.1), HCD_HUB_MAX_PORTS
 * per hub object, PortId past the root ports' - a location only, Number the
 * port on its hub. A location names one port object for as long as its hub
 * object lives, so the PDO handshake (hcdPortNotify) reaches either kind.
 */
typedef struct _HCD_PORT {
    XHCI_ENUM_PORT Enum;
    ULONG PortId;           /* 1-based location; a root port's number    */
    PHCD_USB_DEVICE Device;
    ULONG AwaitSerial;      /* Gone: the PDO group whose deletion it
                             * waits for                                 */
    struct _HCD_HUB *Hub;   /* NULL for a root port                      */
    ULONG Number;           /* the port on its hub; a root port's PortId */
    struct _HCD_HUB *AwaitHub; /* Gone: the departed hub whose subtree's
                             * PDOs it waits for as well (hcd_enum.c)    */
    ULONG HubSpeedClass;    /* a hub port: the speed its reset reported  */
} HCD_PORT, *PHCD_PORT;

/*
 * A hub the bus serves (hcd_hub.c; design record 13 sections 5.2 and 10.3):
 * never a PDO. One per topology node at most (XHCI_TOPO_NODES), each with
 * HCD_HUB_MAX_PORTS port objects after the root ports. A hub whose device
 * left is Draining until every port of it has settled - each waiting, as a
 * root port does, for its own device's PDOs - and the port it sat on waits
 * for it (AwaitHub), so nothing is enumerated at that place before PnP has
 * let go of what was there. Thread only.
 */
#define HCD_MAX_HUBS        ((ULONG)XHCI_TOPO_NODES)
#define HCD_HUB_MAX_PORTS   XHCI_HUB_MAX_PORTS
#define HCD_PORT_COUNT      (XHCI_MAX_ROOT_PORTS + HCD_MAX_HUBS * HCD_HUB_MAX_PORTS)
#define HCD_PORT_WORDS      ((HCD_PORT_COUNT + 31UL) / 32UL)

typedef struct _HCD_HUB {
    ULONG Used;             /* allocated: live or draining               */
    ULONG Draining;         /* its device has left                       */
    ULONG Index;            /* in hc->Hubs[]                             */
    PHCD_USB_DEVICE Device; /* its record while live                     */
    PHCD_PORT Upstream;     /* the port it sits on                       */
    ULONG SlotId;           /* the topology graph's key while live       */
    ULONG SpeedClass;       /* XHCI_SPEED_*                              */
    ULONG Tier;             /* the graph's: 0 on a root port             */
    ULONG Refused;          /* too deep: addressed, never configured     */
    ULONG Ports;            /* managed (XhciHubManagedPorts)             */
    XHCI_HUB_DESC Desc;
    ULONG Alternate;        /* 1: the multi-TT interface is selected     */
    struct _HCD_PIPE *Status; /* the status-change pipe                  */
    ULONG StatusBytes;      /* the report's length                       */
    ULONG StatusFailures;   /* completions in a row that failed          */
    ULONG Polled;           /* the pipe is unusable: polled instead      */
    ULONG PollPasses;
    ULONG Changed;          /* bit 0 the hub, bit n port n: to look at   */
} HCD_HUB, *PHCD_HUB;

/* A hub's port object, n from 1. */
#define HcdHubPort(hc, hub, n)                                               \
    (&(hc)->Ports[XHCI_MAX_ROOT_PORTS +                                      \
                  (hub)->Index * HCD_HUB_MAX_PORTS + (n) - 1UL])

/* URB functions counted one by one (hcd_urb.c); the Windows 2000 DDK's
 * highest is 0x002A (usbdi.h), and anything at or above this is counted as
 * unknown. */
#define HCD_URB_FUNCTIONS 0x40UL

/* The enumeration's DMA scratch (hcd_dma.c), one common buffer: the
 * thread's control transfers use its first 4 KB, the limit a configuration
 * descriptor had before Phase 27, and each hub's status-change report has
 * its own slice after them (hcd_hub.c), so serving hubs takes nothing from
 * that limit (Codex review of 23e7715, finding 8). */
#define HCD_SCRATCH_CONTROL_BYTES 4096UL
#define HCD_HUB_STATUS_BYTES    XHCI_HUB_STATUS_MAX_BYTES
#define HCD_SCRATCH_BYTES                                                    \
    (HCD_SCRATCH_CONTROL_BYTES + HCD_MAX_HUBS * HCD_HUB_STATUS_BYTES)

typedef struct _HCD_CONTROLLER {
    HCD_COMMON Common;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;

    /* IRPs inside the driver on this object, the remove handler's wait. The
     * IO_REMOVE_LOCK family is absent on Windows 98 SE (the allowlist's
     * [deny]), so this is the count it recommends instead. Starts at 1 and
     * the remove handler drops that bias. */
    LONG OutstandingIo;
    KEVENT RemoveEvent;

    /* Resources (hcd_ctl.c). */
    ULONG HaveMemory;
    ULONG HaveInterrupt;
    PHYSICAL_ADDRESS BarRaw;
    PHYSICAL_ADDRESS BarTranslated;
    ULONG BarLength;
    PVOID BarVa;
    ULONG IntVector;
    KIRQL IntIrql;
    KAFFINITY IntAffinity;
    KINTERRUPT_MODE IntMode;
    BOOLEAN IntShared;
    PKINTERRUPT Interrupt;
    KDPC IsrDpc;
    volatile LONG DpcsInFlight;
    volatile ULONG DpcClosed;
    ULONG ControllerStarted;
    ULONG ResumeFailures;
    ULONG PowerRequestFailures;
    DEVICE_POWER_STATE PowerDirectWant;
    ULONG SuspendedInD0;
    KEVENT PowerGate;
    ULONG WakesWithoutPower;
    WORK_QUEUE_ITEM PowerWork;
    PIRP PowerWorkIrp;
    ULONG PowerWorkKind;
    PIRP PendingSystemIrp;

    /* The DMA adapter and the common buffer (hcd_dma.c). */
    PDMA_ADAPTER Dma;
    ULONG MapRegisters;
    PVOID CommonVa;
    PHYSICAL_ADDRESS CommonPa;
    ULONG CommonBytes;
    volatile ULONG CommonBufferPinned;
    ULONG CommonBuffersKept;

    /* The service layer (hcd_svc.c). */
    ULONG ConfigLastStatus;
    ULONG ConfigLastInformation;
    KSPIN_LOCK TimerLock;
    HCD_TIMER Timers[HCD_TIMER_SLOTS];
    LONG TimersInFlight;            /* under TimerLock */
    ULONG TimersClosed;
    ULONG TimerArmsRefused;
    ULONG TimerArmsSuperseded;      /* a pending arm of the same callback
                                     * cancelled and its slot reused      */
    ULONG TimerArmsStale;           /* an arm older than one pending,
                                     * dropped (hcdArmIsOlder)            */
    KEVENT TimersIdle;
    /* The MFINDEX sampler's own timer, not a slot of the service's: a
     * recurring arm must not be refused when the slots are full (Codex
     * review of batch (c), round 13, finding 2). FrameArmed under
     * TimerLock; the drain waits it to 0. */
    KTIMER FrameTimer;
    KDPC FrameDpc;
    volatile LONG FrameArmed;

    /* The controller thread (hcd_ctl.c). */
    PVOID ThreadObject;
    PDEVICE_OBJECT RootHubPdo;
    KSPIN_LOCK PdoListLock;
    PHCD_DEVICE_PDO DevicePdos;     /* listed: present, in the relations */
    PHCD_DEVICE_PDO GonePdos;       /* unlisted, awaiting their deletion */
    ULONG RootHubStarted;           /* enumeration creates PDOs only then */
    volatile ULONG ThreadRunning;
    ULONG ThreadReferenceFailures;
    KEVENT ThreadExited;
    volatile ULONG ThreadStop;
    KEVENT WorkEvent;

    /* Enumeration (hcd_enum.c): the root ports, the device on each slot,
     * the DMA scratch, and the one EP0 transfer the thread waits for. */
    HCD_PORT Ports[HCD_PORT_COUNT];
    HCD_HUB Hubs[HCD_MAX_HUBS];     /* hcd_hub.c; thread only             */
    PHCD_USB_DEVICE SlotDevice[XHCI_MAX_SLOTS + 1];
    PVOID ScratchVa;
    PHYSICAL_ADDRESS ScratchPa;
    KEVENT XferDoneEvent;           /* a device's Ep0Done was set          */
    ULONG Ep0Recoveries;            /* EP0 halted or refused a retire      */
    ULONG FuncReleasesAbandoned;    /* function releases given up after
                                     * HCD_CFG_RELEASE_TRIES (hcd_cfg.c)  */

    /* The function-driver contract (hcd_urb.c): what arrived, counted. */
    ULONG UrbCount[HCD_URB_FUNCTIONS];
    ULONG UrbUnknown;
    ULONG IoctlUnknown;

    /* The map pump (hcd_dma.c, design record 13 section 11.3): records
     * waiting for map registers, one AllocateAdapterChannel outstanding at
     * a time, restarted from MapDpc; MapLock is innermost and is never held
     * across a DMA_OPERATIONS call. DoneList (controller lock) holds the
     * records the event DPC retired, completed by the deferred work after
     * the lock's release (hcd_io.c). */
    KSPIN_LOCK MapLock;
    LIST_ENTRY MapQueue;
    ULONG MapBusy;
    ULONG MapInCall;
    ULONG MapRefusals;
    KDPC MapDpc;
    KDPC MapDoneDpc;                /* the execution routine's release   */
    volatile LONG MapDpcsInFlight;
    ULONG MapsSynchronous;          /* execution routine ran inside the call */
    ULONG MapsDeferred;             /* ... or later                          */
    LIST_ENTRY DoneList;
    /* URBs that need commands (SELECT_CONFIGURATION, ABORT_PIPE,
     * RESET_PIPE) and IOCTL_INTERNAL_USB_RESET_PORT, which carries no URB,
     * pended and served by the thread (hcd_cfg.c); linked
     * through Tail.Overlay.ListEntry, the device in DriverContext[0];
     * controller lock. */
    LIST_ENTRY SlowIrps;
    ULONG CancelWork;               /* some pipe has CancelPending        */
    volatile LONG CancelsRunning;   /* cancel routines past the cancel lock */
    ULONG UrbsCompleted;
    ULONG UrbsGone;
    ULONG UrbsWaited;               /* parked on a pipe's Waiting list   */
    ULONG UrbsMdlShort;             /* length past the MDL: refused      */
    ULONG IsoBadStartFrames;        /* explicit StartFrame not schedulable */
    ULONG WaitingKicks;             /* a waiter started by admission or a
                                     * cancel, not by a record's release  */
    ULONG DevicesKept;              /* freed with URBs left: DMA not stopped */
    ULONG Ep0Resets;
    ULONG EnumCommandsRefused;
    ULONG EnumCommandsTimedOut;
    ULONG EnumDisableFailures;
    ULONG EnumTransfersTimedOut;
    /* Removal and the TT (27-A.3; hcd_enum.c, hcd_hub.c): endpoints
     * stopped so a leaving device's URBs complete before its slot goes,
     * hub ports given up after their attempts, CLEAR_TT_BUFFER sent and
     * refused. Thread only; outside the matrix's counter block. */
    ULONG TeardownStops;
    ULONG TeardownStopFailures;
    ULONG HubPortsGivenUp;
    ULONG TtBufferClears;
    ULONG TtBufferClearFailures;

    /* The device layer (hcd_dev.c). */
    KEVENT CmdDoneEvent;
    volatile ULONG CmdDoneCode;
    volatile ULONG CmdDoneControl;
    volatile ULONG CmdDoneLost;
    ULONG SlotFatalEvents;
    /* Per port bits (HCD_PORT_COUNT locations; PortChange the root ports'
     * alone), under the controller lock: changed (the event DPC), and the
     * PDO handshake (hcd_pdo.c). */
    ULONG PortChange[HCD_PORT_WORDS];
    ULONG PortPdoStarted[HCD_PORT_WORDS];
    ULONG PortPdoRemoved[HCD_PORT_WORDS];
    /* CYCLE_PORT, or a RESET_PORT that failed (HcdEnumCycle): the port's
     * device dropped and enumerated afresh if it is still the one that PDO
     * serial stands for. */
    ULONG PortCycle[HCD_PORT_WORDS];
    ULONG PortCycleSerial[HCD_PORT_COUNT];
    /* Thread requests (hcd_enum.c), under the controller lock. */
    ULONG SlotsInvalidated;         /* HCRST took every slot              */
    ULONG EnumDetachRequested;      /* the root hub is going               */
    KEVENT EnumDetachDone;
    ULONG ScratchTainted;           /* a timed-out EP0 transfer may DMA    */
    ULONG SlotSweep;                /* Abandoned records await Disable Slot */
    volatile ULONG CmdDonePA;       /* the completed command's TRB        */
    volatile ULONG PortEvents;

#if DBG
    /* Strict mode (hcd_strict.c): the command preconditions a conforming
     * xHC enforces and QEMU does not, checked before each command. Thread
     * only. StrictSlotEnabled is the half of the slot state the Output
     * Slot Context cannot say (Disabled and Enabled are both 0). The Seen
     * maps hold one trace per (command with DW3 bit 9, slot and EP state)
     * and per (command, refusal code). */
    ULONG StrictSlotEnabled[(XHCI_MAX_SLOTS + 1 + 31) / 32];
    ULONG StrictChecked;            /* commands checked                   */
    ULONG StrictViolations;         /* precondition the xHC would refuse  */
    ULONG StrictUndefined;          /* the spec's "undefined behavior"    */
    ULONG StrictRefusals;           /* answered SNE, Parameter or CSE     */
    ULONG StrictSeen[64][2];
    ULONG StrictRefusalSeen[64];
#endif
    /* The door (hcd_door.c, 26-A.8; design record 13 section 8): the
     * FDO's name's number, \DosDevices\HCD<n>, the host controller
     * interface, and the root hub's name GET_ROOT_HUB_NAME returns (under
     * PdoListLock). DoorGate is a synchronization event used as a mutex:
     * held by a start, a stop and a door request that reads the extension
     * or the registers, so a request never meets a half-built extension or
     * an unmapped BAR. Outside Hc, which every start zeroes. */
    ULONG FdoSerial;
    ULONG HcdIndex;
    ULONG HcdLinkMade;
    UNICODE_STRING HcInterface;
    ULONG HcInterfaceOn;
    WCHAR RootHubName[24];
    ULONG RootHubNameChars;
    KEVENT DoorGate;
    ULONG DoorRequests;
    ULONG DoorPassThru;
    /* Root-hub FDO requests inside this controller (hcd_rh.c): raised under
     * the root-hub PDO's ControllerLock while it still names this
     * controller, and drained by the orphaning before the storage goes. */
    LONG RootHubUsers;

    /* The controller lock (hcd_svc.h, HcdSvcControllerLock): created once at
     * AddDevice, outside Hc, which every start zeroes. */
    KSPIN_LOCK ControllerLock;

    /* The device matrix's counters (xhci_counters.h), zeroed at every
     * start; CountersStart numbers the starts, from 1 and across every
     * controller of this image (hcd_log.c), so the harness can
     * tell a restart from a block that did not move. */
    XHCIHC_COUNTERS Counters;
    ULONG CountersStart;

    /* The kept controller sequence's state, as the miniport's extension. */
    XHCI_EXTENSION Hc;
} HCD_CONTROLLER, *PHCD_CONTROLLER;

/* The root hub's PDO, created by the controller FDO (hcd_rh.c; design record
 * 13 sections 5.2 and 8.6). Deleted only by the controller's remove, or once
 * reported missing. */
typedef struct _HCD_ROOTHUB_PDO {
    HCD_COMMON Common;
    PHCD_CONTROLLER Controller;     /* NULL once orphaned (hcd_rh.c)      */
    KSPIN_LOCK ControllerLock;      /* Controller and RootHubUsers' raise  */
    ULONG ReportedMissing;
    ULONG Started;
    ULONG RemoveReceived;
    ULONG Reported;                 /* returned in a BusRelations answer    */
    ULONG Deleted;                  /* IoDeleteDevice called: once only     */
    ULONG Serial;                   /* \Device\XHCI98RH<Serial>            */
} HCD_ROOTHUB_PDO, *PHCD_ROOTHUB_PDO;

/* The root hub's FDO, this driver's second role, attached over the root-hub
 * PDO by AddDevice. Its BusRelations are the bus's device PDOs (26-A.4). */
typedef struct _HCD_ROOTHUB_FDO {
    HCD_COMMON Common;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    LONG OutstandingIo;
    KEVENT RemoveEvent;
    /* The door (hcd_door.c): \DosDevices\XHCI98RH<n> and the hub
     * interface. */
    ULONG LinkMade;
    UNICODE_STRING Interface;
    ULONG InterfaceOn;
} HCD_ROOTHUB_FDO, *PHCD_ROOTHUB_FDO;

#define HcdControllerFromExt(ext) \
    CONTAINING_RECORD((ext), HCD_CONTROLLER, Hc)

/* IoSetCompletionRoutine without the ASSERT the checked DDK wraps it in,
 * which would import RtlAssert into the debug and qemu builds - a pair with
 * no Windows 98 evidence. Same three fields. */
#define HCD_SET_COMPLETION_ALWAYS(irp, routine, context)                     \
    do {                                                                     \
        PIO_STACK_LOCATION hcdNext_ = IoGetNextIrpStackLocation(irp);        \
        hcdNext_->CompletionRoutine = (routine);                             \
        hcdNext_->Context = (context);                                       \
        hcdNext_->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR |      \
                            SL_INVOKE_ON_CANCEL;                             \
    } while (0)

/* hcd_rh.c */
NTSTATUS HcdRootHubCreatePdo(PHCD_CONTROLLER hc);
VOID HcdRootHubDeletePdo(PHCD_CONTROLLER hc);
NTSTATUS HcdControllerBusRelations(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdRootHubPdoPnp(PHCD_ROOTHUB_PDO pdo, PIRP irp);
NTSTATUS HcdRootHubPdoPower(PHCD_ROOTHUB_PDO pdo, PIRP irp);
NTSTATUS HcdRootHubAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
NTSTATUS HcdRootHubFdoPnp(PHCD_ROOTHUB_FDO fdo, PIRP irp);
NTSTATUS HcdRootHubFdoPower(PHCD_ROOTHUB_FDO fdo, PIRP irp);
NTSTATUS HcdRootHubFdoDeviceControl(PHCD_ROOTHUB_FDO fdo, PIRP irp);

/* hcd_entry.c */
extern PDRIVER_OBJECT HcdDriverObject;
ULONG HcdIoEnter(PHCD_CONTROLLER hc);
VOID HcdIoLeave(PHCD_CONTROLLER hc);
NTSTATUS HcdPassDown(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);

/* hcd_pnp.c */
NTSTATUS HcdControllerPnp(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdForwardAndWait(PHCD_CONTROLLER hc, PIRP irp);

/* hcd_power.c */
NTSTATUS HcdControllerPower(PHCD_CONTROLLER hc, PIRP irp);

/* hcd_ctl.c */
NTSTATUS HcdStartController(PHCD_CONTROLLER hc, PIRP irp);
VOID HcdStopController(PHCD_CONTROLLER hc);
VOID HcdControllerInitObjects(PHCD_CONTROLLER hc);
VOID HcdThreadWake(PHCD_CONTROLLER hc);
VOID HcdControllerFail(PHCD_CONTROLLER hc);
VOID HcdPowerGateEnter(PHCD_CONTROLLER hc);
VOID HcdPowerGateLeave(PHCD_CONTROLLER hc);

/* hcd_enum.c */
VOID HcdEnumService(PHCD_CONTROLLER hc, ULONG powered);
VOID HcdEnumDetach(PHCD_CONTROLLER hc);
VOID HcdEnumAttach(PHCD_CONTROLLER hc);
VOID HcdEnumInit(PHCD_CONTROLLER hc);
VOID HcdEnumDrop(PHCD_CONTROLLER hc);
ULONG HcdThreadCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb, PULONG control);
ULONG HcdThreadControl(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       UCHAR requestType, UCHAR request, USHORT value,
                       USHORT index, ULONG length, PULONG bytes);
ULONG HcdThreadControlEx(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         UCHAR requestType, UCHAR request, USHORT value,
                         USHORT index, ULONG length, PULONG bytes,
                         PULONG stalled);
ULONG HcdThreadReaddress(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdEnumCycle(PHCD_CONTROLLER hc, ULONG port, ULONG serial);

/* hcd_hub.c */
VOID HcdDeviceSlotParams(PHCD_USB_DEVICE dev, ULONG withHub,
                         PXHCI_SLOT_PARAMS sp);
ULONG HcdDevicePipeSpeed(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdHubPlace(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG speedClass,
                  PHCD_USB_DEVICE dev);
ULONG HcdHubStart(PHCD_CONTROLLER hc, PHCD_PORT p, PHCD_USB_DEVICE dev);
VOID HcdHubForget(PHCD_CONTROLLER hc, PHCD_HUB hub);
VOID HcdHubFree(PHCD_CONTROLLER hc, PHCD_HUB hub);
ULONG HcdHubPortStatus(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                       PULONG status, PULONG change);
ULONG HcdHubPortDebounce(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n);
ULONG HcdHubPortReset(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                      PULONG speedClass);
ULONG HcdHubPortLook(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                     ULONG state, PXHCI_HUB_PORT_DECISION d);
VOID HcdHubCollect(PHCD_CONTROLLER hc, PHCD_HUB hub);
VOID HcdHubRearm(PHCD_CONTROLLER hc, PHCD_HUB hub);
VOID HcdHubXferRetired(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdHubPortDisable(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n);
ULONG HcdHubPathPresent(PHCD_CONTROLLER hc, PHCD_PORT q);
VOID HcdHubClearTt(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                   ULONG endpointAddress, ULONG type, ULONG addressZero);

/* hcd_strict.c: debug and qemu flavours only; nothing in release. */
#if DBG
typedef struct _HCD_STRICT_SNAP {
    ULONG Type;
    ULONG Key;                      /* Type, plus 32 when DW3 bit 9 is set */
    ULONG SlotId;
    ULONG Dci;
    ULONG SlotState;                /* XHCI_STRICT_SLOT_*                 */
    ULONG EpState;
    ULONG Verdict;                  /* XHCI_STRICT_*                      */
} HCD_STRICT_SNAP, *PHCD_STRICT_SNAP;

VOID HcdStrictBefore(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                     PHCD_STRICT_SNAP snap);
VOID HcdStrictAfter(PHCD_CONTROLLER hc, const HCD_STRICT_SNAP *snap,
                    ULONG code, ULONG control);
VOID HcdStrictForgetSlots(PHCD_CONTROLLER hc);
#endif

/* hcd_cfg.c */
NTSTATUS HcdCfgQueue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     struct _HCD_DEVICE_PDO *pdo, PIRP irp);
VOID HcdCfgReleaseFunction(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG interfaceMask);
VOID HcdCfgFlushDevice(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdCfgService(PHCD_CONTROLLER hc);
VOID HcdCfgCancelService(PHCD_CONTROLLER hc);
VOID HcdCfgDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
PHCD_PIPE HcdCfgPipe(PHCD_USB_DEVICE dev, PVOID handle);
ULONG HcdCfgParentConfigure(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
struct _HCD_PIPE *HcdCfgHubOpen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                const XHCI_PIPE_EP *ep, ULONG iface);

/* hcd_pdo.c */
NTSTATUS HcdDevicePdoCreate(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdDevicePdoGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdDevicePdoExists(PHCD_CONTROLLER hc, ULONG serial);
PDEVICE_RELATIONS HcdDevicePdoRelations(PHCD_CONTROLLER hc,
                                        PDEVICE_RELATIONS old);
VOID HcdDevicePdoReleaseAll(PHCD_CONTROLLER hc);
NTSTATUS HcdDevicePdoPnp(PHCD_DEVICE_PDO pdo, PIRP irp);
NTSTATUS HcdDevicePdoPower(PHCD_DEVICE_PDO pdo, PIRP irp);

/* hcd_urb.c */
NTSTATUS HcdDevicePdoInternalIoctl(PHCD_DEVICE_PDO pdo, PIRP irp);
LONG HcdUrbIoRequest(PVOID urb, PHCD_IO_REQUEST req);

/* hcd_io.c */
VOID HcdIoPipeInitEp0(PHCD_USB_DEVICE dev);
VOID HcdIoPipeInit(PHCD_PIPE pipe, PHCD_USB_DEVICE dev);
NTSTATUS HcdIoSubmit(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     struct _HCD_DEVICE_PDO *pdo, PIRP irp, PVOID urb,
                     const HCD_IO_REQUEST *req);
VOID HcdIoDrainPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd);
VOID HcdIoPipeRelease(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoPipePause(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoPipeResume(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
ULONG HcdIoPipeCancelAll(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoCancelPdo(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                    struct _HCD_DEVICE_PDO *pdo);
VOID HcdIoPipeWaitCancelled(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoWaitPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoMapped(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG ok);
VOID HcdIoRetired(PHCD_CONTROLLER hc, PXHCI_TRANSFER t);
VOID HcdIoDeferred(PHCD_CONTROLLER hc);
ULONG HcdIoDeviceDrain(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
NTSTATUS HcdIoRefuseLater(struct _HCD_DEVICE_PDO *pdo, PIRP irp, PVOID urb,
                          LONG usbd);
VOID HcdIoRefusedInit(struct _HCD_DEVICE_PDO *pdo);
ULONG HcdIoPark(struct _HCD_DEVICE_PDO *pdo, PIRP irp, PVOID urb,
                PVOID handle);
ULONG HcdIoParkedRelease(struct _HCD_DEVICE_PDO *pdo, ULONG aborted);
VOID HcdIoStamp(struct _HCD_DEVICE_PDO *pdo, PIRP irp);
VOID HcdIoAbortMark(struct _HCD_DEVICE_PDO *pdo, PIRP abortIrp,
                    PVOID handle, ULONG known);
VOID HcdIoRefusedDrain(struct _HCD_DEVICE_PDO *pdo);
ULONG HcdIoDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdIoIsoRefused(PVOID urb, LONG usbd);

/* hcd_dma.c */
NTSTATUS HcdDmaOpen(PHCD_CONTROLLER hc);
VOID HcdDmaClose(PHCD_CONTROLLER hc);
VOID HcdDmaInitObjects(PHCD_CONTROLLER hc);
VOID HcdDmaMapQueue(PHCD_CONTROLLER hc, PHCD_XFER x);
VOID HcdDmaMapKick(PHCD_CONTROLLER hc);
VOID HcdDmaUnmap(PHCD_CONTROLLER hc, PHCD_XFER x);
VOID HcdDmaMapDrain(PHCD_CONTROLLER hc);

/* hcd_svc.c (the services themselves are in hcd_svc.h) */
VOID HcdRelativeMs(PLARGE_INTEGER due, ULONG milliseconds);
VOID HcdTimersInit(PHCD_CONTROLLER hc);
VOID HcdTimersDrain(PHCD_CONTROLLER hc);
VOID HcdTimersOpen(PHCD_CONTROLLER hc);
VOID HcdFrameTimerStart(PHCD_CONTROLLER hc);

/* hcd_door.c */
VOID HcdDoorGateEnter(PHCD_CONTROLLER hc);
VOID HcdDoorGateLeave(PHCD_CONTROLLER hc);
VOID HcdDoorControllerStart(PHCD_CONTROLLER hc);
VOID HcdDoorControllerStop(PHCD_CONTROLLER hc);
VOID HcdDoorControllerRemove(PHCD_CONTROLLER hc);
VOID HcdDoorRootHubStart(PHCD_ROOTHUB_FDO fdo, PHCD_CONTROLLER hc);
VOID HcdDoorRootHubStop(PHCD_ROOTHUB_FDO fdo);
VOID HcdDoorRootHubRemove(PHCD_ROOTHUB_FDO fdo, PHCD_CONTROLLER hc);
NTSTATUS HcdDoorCreateClose(PIRP irp);
NTSTATUS HcdDoorControllerIoctl(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdDoorRootHubIoctl(PHCD_CONTROLLER hc, PIRP irp);

/* hcd_log.c */
VOID HcdLogFlush(PHCD_CONTROLLER hc, ULONG reason, ULONG counters);
VOID HcdCountersStart(PHCD_CONTROLLER hc);
VOID HcdCountersPoll(PHCD_CONTROLLER hc);

/* hcd_pool.c */
PVOID HcdPoolAlloc(ULONG bytes);
VOID HcdPoolFree(PVOID p);
ULONG HcdPoolOutstandingCount(VOID);
PVOID HcdPoolAllocHandedOff(ULONG bytes);
VOID HcdPoolFreeForeign(PVOID p);
PMDL HcdPoolMdlBuild(PVOID va, ULONG bytes);
VOID HcdPoolMdlFree(PMDL mdl);

#endif /* HCD_H */
