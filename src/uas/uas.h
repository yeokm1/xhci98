/*
 * uas.h - xhciuas.sys, the project's USB Attached SCSI class driver
 * (roadmap-hcd.md task 31-A.2), a second binary with its own INF.
 *
 * THE STACK. The bus (xhci98.sys) creates a PDO for a USB device or
 * function whose interface is Mass Storage / SCSI / UAS (08/06/62) and
 * xhciuas.inf binds this driver to it as the function driver - the FDO
 * below. The FDO owns the four UAS pipes and is in turn a bus: it creates
 * one PDO per logical unit, with the storage ids the class INFs match
 * (uas_iu.c, UasBuildId), so the OS's own storage class driver loads on
 * each - disk.sys and classpnp on Windows 2000 and later, NUSB's USB
 * mapping port driver (usbntmap.inf, DevLoader *IOS) on Windows 98 SE.
 * That is the shape usbstor.sys gives its disks on every target, which is
 * the route 31-0 names; nothing of usbstor's is reused.
 *
 *   [disk.sys / IOS + USBMPHLP.PDR]   OS class layer, per LUN
 *         |  IRP_MJ_SCSI (SRBs), IOCTL_STORAGE_QUERY_PROPERTY
 *   [xhciuas LUN PDO]  uas_pdo.c
 *         .
 *   [xhciuas FDO]      uas_fdo.c (PnP), uas_xport.c (the transport engine)
 *         |  IOCTL_INTERNAL_USB_SUBMIT_URB, streams (uas_streams.h)
 *   [xhci98 device or function PDO]
 *
 * TRANSPORTS (UAS 1.0 section 4). Chosen at start from the configuration
 * the device reports: SuperSpeed endpoint companions present means
 * SuperSpeed, which is streamed - each command's tag is the stream id on the
 * status, data-in and data-out pipes, and up to the granted stream count of
 * commands are in flight. Otherwise High Speed (or Full Speed), which is
 * streamless - the status pipe carries READ READY / WRITE READY ahead of
 * the data phase and the SENSE or RESPONSE IU after it, the data pipes are
 * shared, and one command is in flight at a time.
 *
 * LOCKING. One spin lock per FDO (UAS_FDO.Lock) guards the request queue,
 * the tags, every slot and transfer field and the flags beside them. It is
 * innermost: no call out of the driver (IoCallDriver, IoCompleteRequest,
 * IoCancelIrp) is made holding it.
 */

#ifndef UAS_H
#define UAS_H

#include "../xhci_compat.h"
#include <usbdi.h>
#include <usbioctl.h>
#include <scsi.h>
#include <ntddscsi.h>
#include <ntddstor.h>
#include "uas_iu.h"
#include "uas_streams.h"

#define UAS_POOL_TAG            ' saU'

#define UAS_MAX_LUNS            8
#define UAS_STATUS_BUFFER       288     /* a SENSE IU with 252 sense bytes */
#define UAS_MAX_TRANSFER        0x10000 /* what the adapter descriptor says */
#define UAS_DEFAULT_TIMEOUT     30      /* seconds, an SRB that names none */
#define UAS_TMF_TIMEOUT_MS      3000

/* IoSetCompletionRoutine without the ASSERT the checked DDK wraps it in,
 * which would import RtlAssert into the debug and qemu builds - a pair with
 * no Windows 98 evidence (hcd.h's HCD_SET_COMPLETION_ALWAYS, for the same
 * reason). Invoked on success, error and cancel. */
#define UAS_SET_COMPLETION_ALWAYS(irp, routine, context)                     \
    do {                                                                     \
        PIO_STACK_LOCATION uasNext_ = IoGetNextIrpStackLocation(irp);        \
        uasNext_->CompletionRoutine = (routine);                             \
        uasNext_->Context = (context);                                       \
        uasNext_->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR |      \
                            SL_INVOKE_ON_CANCEL;                             \
    } while (0)

/* Which kind of device object an extension is. */
#define UAS_KIND_FDO            0x46736155UL    /* 'UasF' */
#define UAS_KIND_PDO            0x50736155UL    /* 'UasP' */

/* The transfers a slot owns. */
#define UAS_XFER_COMMAND        0
#define UAS_XFER_STATUS         1
#define UAS_XFER_DATA           2
#define UAS_XFERS               3

struct _UAS_FDO;
struct _UAS_SLOT;
struct _UAS_PDO;

/*
 * One bulk transfer. Its IRP is this driver's own memory, initialised with
 * IoInitializeIrp before each use and freed only at stop, so an IoCancelIrp
 * made after the lock is dropped can never reach freed memory (uas_xport.c,
 * "Cancellation").
 */
typedef struct _UAS_XFER {
    struct _UAS_FDO *Fdo;
    struct _UAS_SLOT *Slot;         /* NULL: the shared status reader */
    ULONG Kind;
    PIRP Irp;
    USHORT IrpSize;
    BOOLEAN Busy;                   /* submitted, completion not yet run */
    BOOLEAN Cancelling;             /* this driver cancelled it */
    struct _URB_BULK_OR_INTERRUPT_TRANSFER Urb;
} UAS_XFER, *PUAS_XFER;

/* Slot states. */
#define UAS_SLOT_FREE           0
#define UAS_SLOT_ACTIVE         1   /* IUs in flight */
#define UAS_SLOT_FINAL          2   /* the final IU (or failure) is in */

/*
 * One command or task management function in flight, indexed by its tag.
 */
typedef struct _UAS_SLOT {
    ULONG Tag;
    ULONG State;
    BOOLEAN Tmf;                    /* a TASK MANAGEMENT IU, not a command */
    BOOLEAN DataIn;
    BOOLEAN DataPosted;             /* the data transfer has been submitted */
    BOOLEAN NeedsData;
    BOOLEAN TimedOut;
    BOOLEAN Failed;                 /* a transfer failed: recovery owns it */
    BOOLEAN Aborted;                /* recovery completed it */
    BOOLEAN ResponseOnly;           /* a RESPONSE IU ended a command */
    BOOLEAN TmfSignalled;
    BOOLEAN Finishing;              /* handed to uasFinish, by one path only */
    PIRP Request;                   /* the SRB's IRP (or the internal one) */
    PSCSI_REQUEST_BLOCK Srb;
    ULONG Lun;
    LONG TimeLeft;                  /* seconds */
    PMDL DataMdl;
    ULONG DataLength;
    ULONG Transferred;
    ULONG ScsiStatus;
    ULONG SenseLength;
    ULONG ResponseCode;
    ULONG FinalSrbStatus;           /* nonzero: overrides the status fold */
    PKEVENT TmfDone;
    UCHAR Iu[UAS_IU_COMMAND_LENGTH];
    UCHAR Stat[UAS_STATUS_BUFFER];  /* streamed: this slot's status buffer */
    UAS_XFER Xfer[UAS_XFERS];
} UAS_SLOT, *PUAS_SLOT;

/* One logical unit's PDO extension. */
typedef struct _UAS_PDO {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    struct _UAS_FDO *Fdo;
    ULONG Lun;
    BOOLEAN Present;                /* reported in the FDO's BusRelations */
    BOOLEAN Started;
    BOOLEAN Removed;
    BOOLEAN Claimed;
    BOOLEAN QueueLocked;            /* SRB_FUNCTION_LOCK_QUEUE */
    UCHAR Inquiry[UAS_INQUIRY_LENGTH];
} UAS_PDO, *PUAS_PDO;

/* The FDO extension. */
typedef struct _UAS_FDO {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Lower;
    PDEVICE_OBJECT Pdo;
    KSPIN_LOCK Lock;

    BOOLEAN Started;
    BOOLEAN Gone;                   /* surprise-removed or removing */
    BOOLEAN Streamed;               /* SuperSpeed, streams open */
    BOOLEAN Recovering;
    BOOLEAN RecoveryQueued;
    BOOLEAN Dead;                   /* a reset failed: every SRB fails */
    BOOLEAN ResetRequested;         /* a LOGICAL UNIT RESET is wanted */
    ULONG ResetLun;

    /* The configuration this driver selected. */
    PUCHAR Config;
    ULONG ConfigLength;
    UAS_CONFIG_INFO Info;
    USBD_CONFIGURATION_HANDLE ConfigHandle;
    USBD_PIPE_HANDLE Pipe[UAS_PIPES];   /* by pipe id - 1 */
    char Serial[128];
    UAS_STREAMS Streams;

    /* The engine (uas_xport.c). */
    UAS_TAGS Tags;
    ULONG QueueDepth;               /* commands in flight at most */
    ULONG Active;                   /* commands (not TMFs) in flight */
    LIST_ENTRY Queue;               /* IRPs, linked by Tail.Overlay.ListEntry */
    PUAS_SLOT Slots;                /* UAS_MAX_TAGS + 1, indexed by tag */
    UAS_XFER SharedStatus;          /* streamless: the one status reader */
    PUCHAR SharedBuffer;
    ULONG Counters[8];

    KTIMER Timer;
    KDPC TimerDpc;
    BOOLEAN TimerStop;
    KEVENT TimerDone;
    WORK_QUEUE_ITEM Recovery;
    KEVENT RecoveryIdle;

    /* The logical units. */
    ULONG LunCount;
    PUAS_PDO Luns[UAS_MAX_LUNS];
} UAS_FDO, *PUAS_FDO;

/* Counters[] slots. */
#define UAS_CTR_COMMANDS        0
#define UAS_CTR_SENSE           1
#define UAS_CTR_PROTOCOL        2
#define UAS_CTR_TIMEOUTS        3
#define UAS_CTR_ABORTS          4
#define UAS_CTR_LUN_RESETS      5
#define UAS_CTR_PORT_RESETS     6
#define UAS_CTR_BUSY            7

/* DriverContext slots of a queued request IRP. */
#define UAS_CTX_PDO             0
#define UAS_CTX_SRB             1
#define UAS_CTX_EVENT           2   /* internal requests: signalled on finish */

extern PDRIVER_OBJECT UasDriverObject;

/* uas_mem.c - every pool and MDL call in the driver. PASSIVE or DISPATCH. */
PVOID UasAlloc(ULONG bytes);
VOID UasFree(PVOID p);
PMDL UasDataMdl(PIRP request, PSCSI_REQUEST_BLOCK srb);
VOID UasFreeMdl(PMDL mdl);
PIRP UasIrpAlloc(CCHAR stackSize, PUSHORT size);
VOID UasIrpReset(PIRP irp, USHORT size, CCHAR stackSize);

/* uas_entry.c */
NTSTATUS UasCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);
VOID UasZero(PVOID p, ULONG n);
VOID UasCopy(PVOID dst, const VOID *src, ULONG n);

/* uas_fdo.c - PASSIVE_LEVEL unless noted. */
NTSTATUS UasFdoPnp(PUAS_FDO fdo, PIRP irp);
NTSTATUS UasFdoPower(PUAS_FDO fdo, PIRP irp);
NTSTATUS UasSyncUrb(PUAS_FDO fdo, PURB urb);
NTSTATUS UasSyncIoctl(PUAS_FDO fdo, ULONG code);

/* uas_pdo.c - PnP at PASSIVE; SCSI and device control at <= DISPATCH. */
NTSTATUS UasPdoCreate(PUAS_FDO fdo, ULONG lun, const UCHAR *inquiry,
                      PUAS_PDO *made);
NTSTATUS UasPdoPnp(PUAS_PDO pdo, PIRP irp);
NTSTATUS UasPdoPower(PUAS_PDO pdo, PIRP irp);
NTSTATUS UasPdoScsi(PUAS_PDO pdo, PIRP irp);
NTSTATUS UasPdoDeviceControl(PUAS_PDO pdo, PIRP irp);

/* uas_xport.c - the engine. Init/Start/Stop/Internal at PASSIVE; Submit at
 * <= DISPATCH, caller holding no lock. */
NTSTATUS UasEngineInit(PUAS_FDO fdo);
VOID UasEngineStart(PUAS_FDO fdo);
VOID UasEngineStop(PUAS_FDO fdo, UCHAR srbStatus);
VOID UasEngineFree(PUAS_FDO fdo);
NTSTATUS UasEngineSubmit(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                         PSCSI_REQUEST_BLOCK srb);
NTSTATUS UasInternalCommand(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                            ULONG cdbLength, PVOID buffer, ULONG length,
                            PULONG transferred, PUCHAR scsiStatus);
VOID UasEngineAbortSrb(PUAS_FDO fdo, PSCSI_REQUEST_BLOCK victim);
VOID UasEngineResetLun(PUAS_FDO fdo, ULONG lun);
VOID UasEngineUnlockQueue(PUAS_FDO fdo);

#endif /* UAS_H */
