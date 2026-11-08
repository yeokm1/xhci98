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
#include "../xhci98_xfer.h"

#define UAS_POOL_TAG            ' saU'

#define UAS_MAX_LUNS            8
#define UAS_STATUS_BUFFER       288     /* holds a SENSE IU with all 252
                                         * sense bytes (16 + 252 = 268) */
#define UAS_MAX_TRANSFER        0x10000 /* the command size until the bus
                                         * answers, and whenever it cannot
                                         * (task 36.22; CommandBytes) */
#define UAS_DEFAULT_TIMEOUT     30      /* seconds, an SRB that names none */
#define UAS_TMF_TIMEOUT_MS      3000

/* IoSetCompletionRoutine without the ASSERT the checked DDK wraps it in,
 * which would import RtlAssert into the checked qemu build - a pair with
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
 * the I/O manager's IRP initialiser before each use and freed only at
 * stop (uas_mem.c, UasIrpReset). A transfer is not built again, and its
 * slot not recycled, while Busy or while CancelRefs says an IoCancelIrp on
 * it has not yet returned (uas_xport.c, "CANCELLATION").
 */
typedef struct _UAS_XFER {
    struct _UAS_FDO *Fdo;
    struct _UAS_SLOT *Slot;         /* NULL: the shared status reader */
    ULONG Kind;
    PIRP Irp;
    USHORT IrpSize;
    BOOLEAN Busy;                   /* submitted, completion not yet run */
    BOOLEAN Cancelling;             /* this driver cancelled it */
    LONG CancelRefs;                /* IoCancelIrp calls not yet returned */
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
    BOOLEAN Aborted;                /* ended by recovery or teardown, with
                                     * FinalSrbStatus; finished when idle */
    BOOLEAN ResponseOnly;           /* a RESPONSE IU ended a command */
    BOOLEAN TmfSignalled;
    BOOLEAN Finishing;              /* handed to uasFinish, by one path only */
    BOOLEAN Quarantine;             /* ended with no device-side termination:
                                     * its tag is held, not reused, until a
                                     * port reset or a new configuration */
    PIRP Request;                   /* the SRB's IRP (or the internal one) */
    PSCSI_REQUEST_BLOCK Srb;
    PIRP AbortIrp;                  /* the first ABORT_COMMAND admitted for
                                     * this command (UasEngineAbort), held
                                     * pending until the worker ends it;
                                     * NULL for each new command, so an
                                     * abort is never matched to a later
                                     * command that reuses the SRB */
    ULONG Lun;
    LONG TimeLeft;                  /* seconds */
    PMDL DataMdl;
    ULONG DataLength;
    ULONG Transferred;
    ULONG ScsiStatus;
    ULONG SenseLength;
    ULONG ResponseCode;
    ULONG FinalSrbStatus;           /* nonzero: overrides the status fold */
    /* Issue 26, XhciUasFastReset: the second it started (fdo->Seconds),
     * and the seconds its final IU has waited for its data cancel. */
    ULONG StartedAt;
    ULONG CancelWait;
    PKEVENT TmfDone;
    UCHAR Iu[UAS_IU_COMMAND_LENGTH];
    UCHAR Stat[UAS_STATUS_BUFFER];  /* streamed: this slot's status buffer */
    UAS_XFER Xfer[UAS_XFERS];
} UAS_SLOT, *PUAS_SLOT;

/*
 * One logical unit's PDO extension.
 *
 * Two rundowns. Requests counts the LUN's requests the engine holds (queued,
 * in flight, or waiting on recovery), under the FDO lock, with RequestsIdle
 * set when it is 0; Admit, also under that lock, closes admission, so a
 * PDO's STOP, SURPRISE_REMOVAL and REMOVE close it and wait the count out.
 * Busy counts the SCSI and device-control dispatches inside the driver,
 * which read Fdo: it starts at 1 (the bias), and the FDO's removal sets Gone
 * and waits it out before the parent's extension can go.
 */
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
    BOOLEAN Admit;                  /* FDO lock: new requests accepted */
    BOOLEAN Gone;                   /* the parent is going: no Fdo access */
    ULONG Requests;                 /* FDO lock */
    KEVENT RequestsIdle;
    LONG Busy;
    KEVENT BusyIdle;
    /* A close the recovery worker carries out (UasEngineCloseLun): the
     * LUN's commands in flight ended with CloseStatus, each by ABORT TASK
     * (escalating), so the device has dropped them before their tags go
     * back. FDO lock. */
    BOOLEAN CloseRequested;
    UCHAR CloseStatus;
    KEVENT CloseDone;
    UCHAR Inquiry[UAS_INQUIRY_LENGTH];
    /* Task 45.23 (design record 20 section 4.3.3): the LUN's identity as
     * the last sleep hold read it, or the last wake check that passed
     * (uas_xport.c); the power lock's. */
    UAS_LUN_ID Id;
} UAS_PDO, *PUAS_PDO;

/* Task 45.23: a deadline as elapsed time (uas_xport.c). */
typedef struct _UAS_DEADLINE {
    ULONG Last;                     /* the system time's low word, read  */
    ULONG Spent;                    /* 100 ns units summed               */
    ULONG Budget;                   /* milliseconds                      */
} UAS_DEADLINE, *PUAS_DEADLINE;

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
    BOOLEAN CycleOwed;              /* task 45.41: a failed reset's cycle
                                     * not yet taken by the bus; retried by
                                     * the recovery worker (uasCycleOwed) */

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
    ULONG Quarantined;              /* bit (tag - 1): held, see Quarantine */
    ULONG QueueDepth;               /* commands in flight at most */
    ULONG Active;                   /* commands (not TMFs) in flight */
    LIST_ENTRY Queue;               /* IRPs, linked by Tail.Overlay.ListEntry */
    LIST_ENTRY RecoveryRequests;    /* ABORT_COMMAND and reset SRBs held for
                                     * the recovery worker's outcome */
    PUAS_SLOT Slots;                /* UAS_MAX_TAGS + 1, indexed by tag */
    UAS_XFER SharedStatus;          /* streamless: the one status reader */
    PUCHAR SharedBuffer;
    ULONG Counters[8];

    /* Task 36.21's readings, under the lock: commands in flight at most,
     * those with a data phase, their lengths' sum, the longest, and the
     * commands by length and by the number in flight as each started
     * (xhci98_xfer.h's buckets). Kept across stops, as Counters are. */
    ULONG ActiveMax;
    ULONG DataCommands;
    ULONG DataLo;
    ULONG DataHi;
    ULONG LengthMax;
    ULONG Hist[XHCI98_TELE_BUCKETS];
    ULONG DepthHist[XHCI98_TELE_DEPTHS];
    /* The command size this driver advertises, in bytes, and where it came
     * from (XHCI98_TELE_SRC_*): the descriptor and the pipes read this one
     * stored value. */
    ULONG CommandBytes;
    ULONG PolicySource;
    /* Issue 26: XhciUasNoAtaPassThrough as the last start read it (lock):
     * ATA PASS-THROUGH and SMART answered here, never sent. */
    BOOLEAN NoAtaPt;
    /* Issue 26: XhciUasFastReset as the last start read it, and its
     * history (lock), kept across recovery passes: FastArmed from an
     * ABORT TASK the device answered at second FastAt until a command
     * ends GOOD (FastProgress), a device reset or a start. Seconds counts
     * the one-second ticks. */
    BOOLEAN FastReset;
    BOOLEAN FastArmed;
    BOOLEAN FastProgress;
    ULONG FastAt;
    ULONG Seconds;
    /* Starts whose question fell back to 64 KiB, by why, since load. */
    ULONG PolicyUnsupported;
    ULONG PolicyMalformed;
    ULONG PolicyTransport;
    /* The record handed to the bus (IOCTL_XHCI98_TELEMETRY) from the timer
     * at most once a second when any word of it differs from the last one
     * the bus took (TeleLast; a failed send leaves it, so the next tick
     * tries again): built under the lock, sent after it with this driver's
     * own IRP. TeleBusy (lock) from the send to the completion; TeleOff
     * once a bus has refused the request as unknown, so an older bus is
     * asked no more. TeleSubmitting (lock) from the build until the send's
     * IoCallDriver has returned: UasEngineStop waits it out, then cancels
     * a send still pending, and its rundown waits for the completion. */
    PIRP TeleIrp;
    USHORT TeleIrpSize;
    BOOLEAN TeleBusy;
    BOOLEAN TeleOff;
    BOOLEAN TeleSubmitting;
    ULONG TeleSequence;
    XHCI98_TELEMETRY TeleRecord;
    XHCI98_TELEMETRY TeleLast;
    /* Issue 26: the last failures (lock), sent on the same IRP when the
     * telemetry has nothing new: FailCount recorded since load, the ring
     * of XHCI98_FAIL_RECORDS entries, FailSent the count the bus last
     * took, FailOff once a bus refused the record (an older bus),
     * TeleFails while the send in flight is this one, and FailTurn after
     * a telemetry send, so new failures go next. */
    ULONG FailCount;
    ULONG FailSent;
    BOOLEAN FailOff;
    BOOLEAN TeleFails;
    BOOLEAN FailTurn;
    ULONG Fail[XHCI98_FAIL_RECORDS][XHCI98_FAIL_WORDS];
    XHCI98_FAILURES FailRecord;

    KTIMER Timer;
    KDPC TimerDpc;
    BOOLEAN TimerStop;
    KEVENT TimerDone;
    WORK_QUEUE_ITEM Recovery;

    /* Task 45.23 (design record 20 section 4.3.3): the sleep hold and the
     * wake check (uas_xport.c). PowerLock, a synchronization event, is held
     * by the sleep phase and by the wake check, so one never runs inside
     * the other. Holding (lock): uasPump starts only internal requests;
     * set by the sleep phase, kept across the sleep, released only by the
     * wake check of that hold (HoldCheck, lock: an S0 has asked for it).
     * WakeQueued (lock): WakeItem queued, with a rundown reference. The
     * rest is the power lock's: what the bus answered at the sleep
     * (BusKnown: the request is known; WithdrawOk; DeclareTried), the
     * epoch the last check verified, and the counts the bus's log is told
     * of. */
    KEVENT PowerLock;
    WORK_QUEUE_ITEM WakeItem;
    BOOLEAN Holding;
    BOOLEAN HoldCheck;
    BOOLEAN Sealed;                 /* lock: from the declaration's proof
                                     * to the wake check, no recovery is
                                     * queued (uasWantRecovery)           */
    BOOLEAN WakeQueued;
    BOOLEAN BusKnown;
    BOOLEAN WithdrawOk;
    BOOLEAN DeclareTried;
    ULONG Episode;
    ULONG VerifiedEpoch;

    /* The engine's rundown: 1 (the bias) plus one per transfer from build
     * to the end of its completion routine, per queued recovery worker to
     * its last access, and per timer DPC run. IoIdle is set at 0, which only
     * UasEngineStop's drain reaches. */
    LONG IoCount;
    KEVENT IoIdle;

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
#define UAS_CTX_EVENT           2   /* internal requests: their UAS_ICMD
                                     * (uas_xport.c), told at the finish  */
#define UAS_CTX_PASS            3   /* a program's pass-through or SMART
                                     * request: its UAS_PCMD (uas_pdo.c);
                                     * NULL for every other request       */

/*
 * Task 45.52 (design record 13 section 16): one pass-through or SMART
 * request. The caller's own IRP is the engine's request, with this
 * record's SRB; the finish (UasPdoPassFinish) writes the outcome back and
 * completes the IRP. Claimed and Cancelled are the FDO lock's: Claimed once
 * a path has taken the IRP off the queue with its cancel routine cleared
 * (uasClaimQueued), Cancelled when the cancel routine took it.
 */
#define UAS_PCMD_PT             0   /* SCSI or ATA pass-through           */
#define UAS_PCMD_SMART          1   /* IOCTL_SCSI_MINIPORT, SCSIDISK      */

typedef struct _UAS_PCMD {
    struct _UAS_FDO *Fdo;
    PUAS_PDO Pdo;
    PIRP Irp;
    ULONG Form;
    ULONG OutLength;                /* the IRP's output length            */
    ULONG Room;                     /* SMART: the reply's room            */
    UAS_PT_PLAN Plan;
    UAS_SMART_PLAN Smart;
    PMDL UserMdl;                   /* direct forms: the caller's buffer  */
    PUCHAR Data;                    /* SMART: its data-in buffer          */
    BOOLEAN Claimed;
    BOOLEAN Cancelled;
    SCSI_REQUEST_BLOCK Srb;
    UCHAR Sense[UAS_PT_SENSE_MAX];
} UAS_PCMD, *PUAS_PCMD;

extern PDRIVER_OBJECT UasDriverObject;
extern BOOLEAN UasIsNt;

/* uas_mem.c - every pool and MDL call in the driver. PASSIVE or DISPATCH. */
VOID UasPoolInit(VOID);
PVOID UasAlloc(ULONG bytes);
VOID UasFree(PVOID p);
PMDL UasDataMdl(PIRP request, PSCSI_REQUEST_BLOCK srb);
VOID UasFreeMdl(PMDL mdl);
/* Task 45.52: the direct forms' buffer, locked in the caller's context
 * with its requestor mode (PASSIVE_LEVEL), and its release. */
NTSTATUS UasLockBuffer(PVOID address, ULONG length, KPROCESSOR_MODE mode,
                       BOOLEAN deviceWrites, PMDL *mdl);
VOID UasUnlockBuffer(PMDL mdl);
PIRP UasIrpAlloc(CCHAR stackSize, PUSHORT size);
VOID UasIrpReset(PIRP irp, USHORT size, CCHAR stackSize);

/* uas_entry.c */
NTSTATUS UasCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);
VOID UasZero(PVOID p, ULONG n);
VOID UasCopy(PVOID dst, const VOID *src, ULONG n);
VOID UasWaitEvent(PKEVENT event);   /* PASSIVE_LEVEL */

/* uas_fdo.c - PASSIVE_LEVEL unless noted. */
NTSTATUS UasFdoPnp(PUAS_FDO fdo, PIRP irp);
NTSTATUS UasFdoPower(PUAS_FDO fdo, PIRP irp);
NTSTATUS UasSyncUrb(PUAS_FDO fdo, PURB urb);
NTSTATUS UasSyncIoctl(PUAS_FDO fdo, ULONG code);
NTSTATUS UasBusIoctl(PUAS_FDO fdo, ULONG code, PVOID arg1);

/* uas_pdo.c - PnP at PASSIVE; SCSI and device control at <= DISPATCH. */
NTSTATUS UasPdoCreate(PUAS_FDO fdo, ULONG lun, const UCHAR *inquiry,
                      PUAS_PDO *made);
NTSTATUS UasPdoPnp(PUAS_PDO pdo, PIRP irp);
NTSTATUS UasPdoPower(PUAS_PDO pdo, PIRP irp);
NTSTATUS UasPdoScsi(PUAS_PDO pdo, PIRP irp);
NTSTATUS UasPdoDeviceControl(PUAS_PDO pdo, PIRP irp);
/* <= DISPATCH_LEVEL, no lock held: a pass-through request's end, from the
 * engine's completion of it (uasCompleteRequest). Completes the IRP. */
VOID UasPdoPassFinish(PUAS_PCMD pc, PSCSI_REQUEST_BLOCK srb);

/* uas_xport.c - the engine. Init/Start/Stop/Internal/CloseLun at PASSIVE;
 * Submit, Abort, Reset, Flush, OpenLun and UnlockQueue at <= DISPATCH;
 * callers hold no lock. */
NTSTATUS UasEngineInit(PUAS_FDO fdo);
VOID UasEngineStart(PUAS_FDO fdo);
VOID UasEngineStop(PUAS_FDO fdo, UCHAR srbStatus);
VOID UasEngineFree(PUAS_FDO fdo);
NTSTATUS UasEngineSubmit(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                         PSCSI_REQUEST_BLOCK srb);
NTSTATUS UasInternalCommand(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                            ULONG cdbLength, PVOID buffer, ULONG length,
                            PULONG transferred, PUCHAR scsiStatus);
NTSTATUS UasInternalCommandEx(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                              ULONG cdbLength, PVOID buffer, ULONG length,
                              PULONG transferred, PUCHAR scsiStatus,
                              PUCHAR sense, PULONG senseLength,
                              PUAS_DEADLINE deadline);
/* Task 45.23 (design record 20 section 4.3.3): the sleep phase at the
 * FDO's system set-power to a sleeping state (PASSIVE_LEVEL), and the S0's
 * request for the wake check (<= DISPATCH_LEVEL). */
BOOLEAN UasEngineSleep(PUAS_FDO fdo);
BOOLEAN UasEngineHold(PUAS_FDO fdo);
VOID UasEngineRelease(PUAS_FDO fdo);
VOID UasEngineWake(PUAS_FDO fdo);
NTSTATUS UasEngineAbort(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb);
NTSTATUS UasEngineReset(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb);
NTSTATUS UasEngineFlush(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb);
VOID UasEngineUnlockQueue(PUAS_FDO fdo);
/* Issue 26: a request the PDO answered itself (XhciUasNoAtaPassThrough)
 * entered in the failure ring. <= DISPATCH_LEVEL, no lock held. */
VOID UasEngineNoteLocal(PUAS_FDO fdo, const UCHAR *cdb, ULONG origin,
                        ULONG length);
VOID UasEngineOpenLun(PUAS_FDO fdo, PUAS_PDO pdo);
VOID UasEngineCloseLun(PUAS_FDO fdo, PUAS_PDO pdo, UCHAR srbStatus);
/* Task 45.52: <= DISPATCH_LEVEL. Admits and queues a pass-through request
 * (its IRP marked pending, with a cancel routine while queued); FALSE when
 * admission refused it, nothing counted and the IRP left to the caller. */
BOOLEAN UasEngineSubmitPass(PUAS_FDO fdo, PUAS_PDO pdo, PUAS_PCMD pc);

#endif /* UAS_H */
