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
#include "xhci_xport.h"
#include "xhci_bot.h"
#include "xhci_silent.h"
#include "xhci_regsrc.h"
#include "xhci_hub.h"
#include "xhci_sshub.h"
#include "xhci_link.h"
#include "xhci_susp.h"
#include "xhci_psw.h"
#include "xhci_pirq.h"
#include "xhci_stream.h"
#include "xhci_usbdx.h"
#include "xhci_counters.h"
#include "xhci_inj.h"
#include "xhci_xstat.h"
#include "xhci_xpipe.h"
#include "xhci98_xfer.h"
#include "xhci_poll.h"
#include "xhci_msi.h"
#include "xhci_hlpr.h"
#include "xhci_msg4.h"

/* Message-signalled interrupts (design records 15 section 4.3 and 18): the
 * qemu flavour's failed message connection, its switch that keeps the
 * requirements filter from stripping, and its no-line rewrite. */
#define HCD_VALUE_QEMU_MSG_FAIL L"XhciQemuMsgConnectFail"
#define HCD_VALUE_QEMU_NO_STRIP L"XhciMsi86NoStrip"
#define HCD_VALUE_QEMU_NO_LINE  L"XhciMsi86InjNoLine"

/*
 * The processor's time-stamp counter (task 36.21; xhci_xstat.h): the only
 * clock finer than the system time's tick on every target that needs no
 * import - the RDTSC instruction, which every processor these targets run
 * on has and which a kernel-mode caller may always execute. MSVC 6.0's
 * assembler predates the mnemonic, so x86 emits its two bytes; amd64 has
 * the compiler's intrinsic through wdm.h (hcd_io.c, HcdTscRead).
 * HcdTscStamp takes the full count and, from CPUID leaf 1, the initial
 * APIC ID of the processor that read it - no processor number is an
 * import on Windows 98 - so an interval is measured only within one
 * processor's clock (XhciXstatInterval). On a processor without an APIC
 * the ID reads 0, which is a uniprocessor's anyway. IRQL: any; below
 * DISPATCH_LEVEL HcdTscStamp raises to it for the ID and the count and
 * then restores the caller's IRQL, so both come from one processor.
 * HcdTscRead alone takes no IRQL step.
 */
VOID HcdTscRead(PULONG lo, PULONG hi);
/*
 * Task 47.30 (design record 13 section 11.10, point 9): how a start reached
 * its event ring, bits 23:16 of the evt.ring note. XhciEventRingTrbs 256
 * asks for the in-block ring (HCD_EVT_ROUTE_VALUE); task 49.35 adds a
 * controller below xHCI 1.0 held there (HCD_EVT_ROUTE_HELD) and the value
 * absent (HCD_EVT_ROUTE_DEFAULT). With the larger ring asked for, the qemu
 * flavour's XhciQemuEventRing 1 keeps the in-block ring
 * (HCD_EVT_ROUTE_TEST), 2 takes the wide block whatever the first one's
 * placement, 3 has both requests read as refused.
 */
#define HCD_EVT_ROUTE_BLOCK     0UL /* the 16 KiB block                  */
#define HCD_EVT_ROUTE_WIDE      1UL /* the 32 KiB block's window         */
#define HCD_EVT_ROUTE_REFUSED   2UL /* no block: the in-block ring       */
#define HCD_EVT_ROUTE_TEST      3UL /* the qemu value's in-block ring    */
#define HCD_EVT_ROUTE_VALUE     4UL /* XhciEventRingTrbs 256             */
#define HCD_EVT_ROUTE_HELD      5UL /* 49.35: the controller below 1.0   */
#define HCD_EVT_ROUTE_DEFAULT   6UL /* 49.35: the value absent           */
#define HCD_EVT_QEMU_NONE       0UL
#define HCD_EVT_QEMU_SMALL      1UL
#define HCD_EVT_QEMU_WIDE       2UL
#define HCD_EVT_QEMU_REFUSE     3UL
VOID HcdTscStamp(PXHCI_XSTAT_STAMP s);

/* One class driver's telemetry (IOCTL_XHCI98_TELEMETRY, xhci98_xfer.h):
 * the device it came from (Owner: the device record, compared and never
 * followed, NULL once the record is freed - HcdIoDeviceGone - when State
 * becomes departed), its slot, the sends taken since this device took the
 * record, the counter at the last, and the record as it came. Under the
 * controller lock. */
#define HCD_UAS_TELE_RECORDS    XHCI_SNAPSHOT_HCD_TELE_COUNT
typedef struct _HCD_UAS_TELE {
    PVOID Owner;
    ULONG State;                    /* XHCI_SNAPSHOT_HCD_TELE_S_*         */
    ULONG Slot;
    ULONG Taken;
    ULONG TscLo;
    ULONG TscHi;
    ULONG Word[XHCI98_TELE_WORDS];
    /* Issue 26: the same device's last failures (XHCI98_FAILURES, its
     * count then its ring), all 0 until one comes; zeroed when another
     * device takes the record. */
    ULONG Fail[XHCI98_FAIL_TOTAL_WORDS];
    /* Task 49.39: a version 2 record's times and ATA refusal bits, and the
     * failure count they belong to (0 with a version 1 record). */
    ULONG FailTimeCount;
    ULONG FailTimes[XHCI98_FAIL_RECORDS];
    ULONG FailAta;
} HCD_UAS_TELE, *PHCD_UAS_TELE;
XHCI_C_ASSERT(hcd_uas_tele_words,
              XHCI98_TELE_WORDS == XHCI_SNAPSHOT_HCD_TELE_BODY_WORDS);
XHCI_C_ASSERT(hcd_uas_fail_words,
              XHCI98_FAIL_RECORDS * XHCI98_FAIL_WORDS + 1 ==
                  XHCI_SNAPSHOT_HCD_UFAIL_RECORD_WORDS);

#define HCD_KIND_CONTROLLER_FDO 0x43464448UL /* 'HDFC' */
#define HCD_KIND_ROOTHUB_PDO    0x50524448UL /* 'HDRP' */
#define HCD_KIND_ROOTHUB_FDO    0x46524448UL /* 'HDRF' */
#define HCD_KIND_DEVICE_PDO     0x50444448UL /* 'HDDP' */
#define HCD_KIND_HUB_FDO        0x46484448UL /* 'HDHF' (task 33.4)  */
#define HCD_KIND_COMPOSITE_FDO  0x46434448UL /* 'HDCF' (task 36.43) */

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
    PDEVICE_OBJECT RetiredNext;     /* an orphaned PDO on Windows 98's
                                     * retired list (hcd_pdo.c)          */
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
/* Windows 2000 DDK inc\usbdi.h:232. */
#define HCD_USBD_STALL_PID          ((LONG)0xC0000004L)
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
/* What a record marked CancelRequested completes with: the status of the
 * abort that marked it (HcdIoPipeCancelAll, HcdIoPipeMarkAll) - a resume's
 * DEV_NOT_RESPONDING, say - or CANCELED for its IRP's own cancel. */
#define HCD_XFER_CANCEL_STATUS(x)                                           \
    ((x)->CancelStatus != 0 ? (x)->CancelStatus : HCD_USBD_CANCELED)
#define HCD_PIPE_XFERS      4UL
/* A stream's pipe (31-A.1) carries fewer: one request per stream at a time
 * is UAS's whole use of one (a tag per stream), and one more lets the next
 * be mapped while the first is on the ring. */
#define HCD_STREAM_XFERS    2UL
/* An isochronous pipe carries more (record 13 section 11.9, task 47.5):
 * every record is a request a client has handed over, so a stream of
 * short requests is held on the ring rather than waiting unmapped. The
 * event budget counts one underrun for each (XHCI_ISO_EVENTS_OVER_TRBS). */
#define HCD_ISO_PIPE_XFERS  8UL
/* Data elements per chunk (xhci_pipe.h XHCI_PIPE_CHUNK_ELEMENTS), and the
 * one slot the SG list's own declaration already holds beyond them. */
#define HCD_SG_ELEMENTS     32UL

/*
 * Task 48.33 (design record 13 section 11.11): every transfer's data
 * staged through common buffer the driver owns (hcd_stage.c). A TD holds
 * at most HCD_STAGE_TD_SLABS slabs of 64 KiB, two elements each at most
 * (cut at the 64 KB line), so HCD_SG_ELEMENTS covers it. The window MDLs
 * map at most HCD_STAGE_WINDOW bytes of a client's MDL at a time.
 */
#define HCD_STAGE_TD_SLABS  16UL
#define HCD_STAGE_WINDOWS   4UL
#define HCD_STAGE_WINDOW    0x10000UL
#define HCD_STAGE_S_NONE        0UL
#define HCD_STAGE_S_WAITING     1UL /* on StageQueue                      */
#define HCD_STAGE_S_FILLING     2UL /* its OUT copy running               */
#define HCD_STAGE_S_READY       3UL /* filled, or an IN, not published    */
#define HCD_STAGE_S_PUBLISHED   4UL /* on the ring                        */
#define HCD_STAGE_S_DRAINING    5UL /* its IN copy-out running            */
#define HCD_STAGE_S_COPY_PENDING 6UL /* a copy-out that could not map, on
                                      * StageCopyQueue                    */
#define HCD_STAGE_K_NONE        0UL
#define HCD_STAGE_K_SMALL       1UL /* the record's own small buffer      */
#define HCD_STAGE_K_SHARED      2UL /* slabs of the shared pool           */
#define HCD_STAGE_K_RESERVE     3UL /* its pipe's reserve                 */
#define HCD_STAGE_K_CONTROL     4UL /* the controller's control slab      */
#define HCD_STAGE_K_ISO         5UL /* its isochronous share              */

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
#define HCD_XFER_MAPPING    1UL     /* in staging or the map pump         */
#define HCD_XFER_ON_RING    2UL     /* published; the engine owns it      */
#define HCD_XFER_DONE       3UL     /* retired, on the done list          */
#define HCD_XFER_HELD       4UL     /* staged or not, waiting for its pipe */

/* Record 13 section 11.8, point 3: the event budget counts one underrun
 * for each record an isochronous pipe can publish while nothing drains,
 * and one more (section 11.9). */
XHCI_C_ASSERT(hcd_iso_events_over_trbs,
              XHCI_ISO_EVENTS_OVER_TRBS == HCD_ISO_PIPE_XFERS + 1UL);

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
    LONG CancelStatus;              /* an abort's own status for it, 0 for
                                     * CANCELED (HCD_XFER_CANCEL_STATUS) */
    ULONG ShortOk;                  /* USBD_SHORT_TRANSFER_OK was set     */
    ULONG Isoch;                    /* URB_FUNCTION_ISOCH_TRANSFER        */
    ULONG Asap;                     /* USBD_START_ISO_TRANSFER_ASAP       */
    ULONG Seq;                      /* its pipe's submission order        */
    ULONG Mapped;                   /* held with its chunk mapped         */
    /* 36.21: chunks of this URB published; the stamp at which software
     * retired its last chunk with data still to move, and the one at which
     * the next chunk was queued for mapping, each cleared when used or
     * when the URB ends; controller lock. */
    ULONG ChunksPublished;
    XHCI_XSTAT_STAMP Retire;
    XHCI_XSTAT_STAMP MapQueued;
    /* Task 47.30 (design record 13 section 11.10, Part B), an isochronous
     * record's: the stamp at its claim, and whether its IRP had waited on
     * the pipe's Waiting list; controller lock. */
    XHCI_XSTAT_STAMP IsoClaim;
    ULONG IsoWaited;
    /* 36.24 (design record 13 section 11.6), controller lock: the wide
     * entry an eligible record claimed when it was filled (hcdFill), given
     * back when its first mapping does not promote or at its completion;
     * Promoted once that mapping covered the whole URB, from when its TDs
     * are cut from the entry's list (Cursor) two at a time and its life is
     * Rec's - slot 0 is Xfer, slot 1 the entry's. A refill that could not
     * be published waits (RefillHeld, HCD_REFILL_*) for the pipe's resume
     * or another TD's retirement; RefillNext links the controller's list of
     * refills to publish after a completion pass (HcdIoRefillFlush). Probe
     * marks the register-free check's own two records (hcd_dma.c). */
    struct _HCD_WIDE_ENTRY *Wide;
    ULONG Promoted;
    ULONG RefillHeld;
    ULONG RefillQueued;
    struct _HCD_XFER *RefillNext;
    ULONG PipeCounted;
    ULONG Probe;
    XHCI_XPIPE_CURSOR Cursor;
    XHCI_XPIPE_REC Rec;
    struct _HCD_XFER_SG {
        USBPORT_SCATTER_GATHER_LIST List;
        USBPORT_SCATTER_GATHER_ELEMENT More[HCD_SG_ELEMENTS];
    } Sg;
    /* Task 48.33 (design record 13 section 11.11; hcd_stage.c): the TD's
     * staging, StageState HCD_STAGE_S_* and StageKind HCD_STAGE_K_* under
     * StageLock; StageSeg the addresses of its segments, StageSegBytes
     * each one's size (the last may be shorter); StageSlab the pool's
     * indices of the slabs it holds. SysVa: the URB's TransferBuffer, NULL
     * for an MDL. SmallVa and SmallPa: the record's own small buffer,
     * given with its pipe. StageEvent: the TD retired by its Transfer
     * Event, not taken off (controller lock). StageCopied: the IN chunk's
     * copy-out done; StageAbandon: a copy-out ended by a cancel or a drain
     * before it could map; StageFailAt: the first failed mapping of this
     * chunk (StageMapFails of them), on the controller's tolerance clock,
     * which no wall-clock change moves. StagePage: an isochronous share's
     * page offset, hcdIsoFill's fragment cut. */
    ULONG StageState;
    ULONG StageKind;
    ULONG StageCount;
    ULONG StageSegBytes;
    PUCHAR StageSeg[HCD_STAGE_TD_SLABS];
    ULONG StageSegPa[HCD_STAGE_TD_SLABS];
    USHORT StageSlab[HCD_STAGE_TD_SLABS];
    ULONG StageEvent;
    ULONG StageCopied;
    ULONG StageAbandon;
    ULONG StageWaited;              /* counted once as waiting            */
    ULONG StageMoved;               /* bytes its copies moved, failed or
                                     * not: the passes' 1 MiB bound      */
    ULONG StageMapFails;
    ULONG StageFailAt;              /* the tolerance clock's tick (100 ms) */
    ULONG StageIsoBase;             /* isochronous: packet 0's offset, the
                                     * share's byte 0                     */
    PUCHAR SysVa;
    PUCHAR SmallVa;
    ULONG SmallPa;
    ULONG StagePage;
} HCD_XFER, *PHCD_XFER;

#define HCD_REFILL_NONE     0UL
#define HCD_REFILL_PAUSE    1UL     /* the pipe paused, or a truncation   */
#define HCD_REFILL_BUSY     2UL     /* the ring had no room               */

/* The record a TD of the URB path belongs to: both TDs of a pipelined
 * record carry it as their TransferParameters (section 11.6, point 6). */
#define HcdIoXferOf(t)      ((PHCD_XFER)(t)->TransferParameters)

/*
 * Stage 2's wide entries (task 36.24; design record 13 section 11.6, point
 * 3): HCD_WIDE_ENTRIES of them, allocated with the controller at its start
 * when promotion is enabled and freed at its stop. Each holds one URB's
 * whole mapping - up to XHCI_XPIPE_WIDE_ELEMENTS page elements, a 2 MiB URB
 * at any alignment - its second TD and that TD's own list. Owner under the
 * controller lock.
 */
#define HCD_WIDE_ENTRIES    8UL
/*
 * XhciWideMap when the value is absent: off (owner, 2026-10-10, for 2.2.0.3
 * and 2.3.0.0: regression unlikely but
 * unquantified, a silent-corruption failure mode, no gate-code defect;
 * record 13 section 11.6). 1 in the key opts in. The one constant to
 * flip. Promotion still needs Windows 2000 or later, the
 * adapter's register-free grant (XHCI_XPIPE_FREE_GRANT), the start's
 * register-free check (hcd_dma.c, HcdDmaWideOpen) and, per URB, a NULL
 * map register base.
 */
#define HCD_WIDE_MAP_DEFAULT 0UL

typedef struct _HCD_WIDE_ENTRY {
    struct _HCD_XFER *Owner;        /* NULL: free                         */
    ULONG Count;                    /* Wide's elements in use             */
    XHCI_TRANSFER Xfer;             /* slot 1's TD                        */
    struct _HCD_WIDE_SG {
        USBPORT_SCATTER_GATHER_LIST List;
        USBPORT_SCATTER_GATHER_ELEMENT More[XHCI_XPIPE_TD_ELEMENTS];
    } Sg;                           /* slot 1's TD list                   */
    /* The URB's whole mapping, offsets from 0, as a list (36.25): on a big
     * ring it is the one TD's own list (record 13 section 11.7, point 3);
     * elsewhere the planner cuts TDs from its elements. */
    struct _HCD_WIDE_LIST {
        USBPORT_SCATTER_GATHER_LIST List;
        USBPORT_SCATTER_GATHER_ELEMENT More[XHCI_XPIPE_WIDE_ELEMENTS - 2];
    } WideList;
} HCD_WIDE_ENTRY, *PHCD_WIDE_ENTRY;

/* The wide list's elements run on from the list's own two, as the
 * record's do (hcd_sg_list_tail). */
XHCI_C_ASSERT(hcd_wide_list_tail,
              XHCI_OFFSET_OF(struct _HCD_WIDE_LIST, More) ==
                  XHCI_OFFSET_OF(USBPORT_SCATTER_GATHER_LIST, SgElement) +
                      2 * sizeof(USBPORT_SCATTER_GATHER_ELEMENT));

/* 36.25: the TRB scratch a one-TD publication on a big ring builds into,
 * beside the entries in the pool's one allocation (hcd_dma.c). */
#define HCD_WIDE_SCRATCH_TRBS (XHCI_XPIPE_WIDE_ELEMENTS + 2UL)

/* About 13 KiB an entry, about 104 KiB for the eight, on both
 * architectures; the bound the record states (section 11.6, point 3). */
XHCI_C_ASSERT(hcd_wide_entry_bytes, sizeof(HCD_WIDE_ENTRY) <= 14336);

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
    /* The device's sequence (an endpoint's pipe, never a stream's; under
     * the controller lock): a TD was published, or a surviving TD
     * restarted, on the endpoint or any stream of it since both ends last
     * restarted its sequence - the select that opened it, a RESET_PIPE or
     * RESET_PORT that succeeded, or a streams open or close whose
     * CLEAR_FEATURE(ENDPOINT_HALT) the device took (hcd_io.c, hcd_cfg.c).
     * A streams open or close of a used endpoint owes the device that
     * clear (hcdCfgStreamsSequence). */
    ULONG SeqUsed;
    /* Task 36.12 (an endpoint's pipe, never a stream's; the thread's): a
     * RESET_PIPE's Reset Endpoint restarted the host's toggle and left the
     * endpoint Stopped, and the device did not take its
     * CLEAR_FEATURE(ENDPOINT_HALT), so the device's half is owed. The pipe
     * holds one pause for it (Paused counts it) until the half is done
     * (hcdCfgReset) or the endpoint's context is replaced
     * (hcdCfgConfigureAdd: RESET_PORT, a recycle, a streams open or close),
     * either of which ends it (hcdCfgOwedEnd); a select or the device's
     * going frees the pipe with it. */
    ULONG ClearOwed;
    ULONG CancelPending;            /* a record of it was cancelled      */
    ULONG DrainPending;             /* a refused retire: stop and drain  */
    ULONG Paused;                   /* the thread is stopping or editing
                                     * it: nothing is published meanwhile */
    LIST_ENTRY Held;                /* records mapped while Paused, or
                                     * behind Exclusive                   */
    ULONG SuspendPause;             /* one of Paused is selective
                                     * suspend's gate (design record 14
                                     * section 3.2); controller lock      */
    ULONG SuspStopWatch;            /* the suspend's Stop Endpoint is out:
                                     * its Stopped event is read for     */
    ULONG SuspStopUntouched;        /* positive evidence that the head TD
                                     * moved nothing (hcd_dev.c); both
                                     * controller lock (36.35's rewind)  */
    struct _HCD_XFER *Exclusive;    /* a request split into chunks: none
                                     * other is published until it ends   */
    ULONG BigRing;                  /* 36.25: a stream's ring of
                                     * XHCI_STREAM_BIG_RING_TRBS - a
                                     * promoted URB goes as one TD        */
    ULONG TruncHeld;                /* 36.24: a pipelined record's
                                     * successor waits for the thread to
                                     * take it off - nothing rings this
                                     * pipe's doorbell; controller lock   */
    ULONG Seq;                      /* the last submission's order         */
    XHCI_PIPE_EP Ep;                /* its descriptor, decoded (xhci_pipe) */
    PHCD_ISO_BLOCK Iso;             /* isochronous: XferCount blocks,
                                     * IsoBlockBytes apart (hcdIsoBlock)  */
    ULONG IsoBlockBytes;            /* one block at IsoMaxPackets          */
    ULONG IsoMaxPackets;            /* a request's packet bound: 62 on a
                                     * pool ring, up to 128 on a page     */
    ULONG IsoRecordsPeak;           /* the most records out at once
                                     * (section 11.9's iso.recs);
                                     * controller lock                    */
    XHCI_ISO_TIMING IsoTiming;      /* section 11.10's stream timing;
                                     * controller lock                    */
    /* Record 13 section 11.8 (the thread's; read under the controller
     * lock): the ring page this pipe's ring lives in, NULL on a pool
     * ring, and what the pipe adds to the event budget's IsoCapOut - its
     * ring's capacity plus XHCI_ISO_EVENTS_OVER_TRBS - given back once,
     * when it can publish no more (hcdCfgIsoCountReturn). */
    PVOID IsoPageVa;
    PHYSICAL_ADDRESS IsoPagePa;
    ULONG IsoPageBytes;
    ULONG IsoCount;
    ULONG IsoNext;                  /* the frame after the last queued
                                     * packet's; controller lock          */
    ULONG IsoFlowFresh;             /* 36.57: the next isochronous request
                                     * starts a data flow (a new context, or
                                     * the ring ran dry), so may carry a
                                     * first-TD Frame ID without CFC
                                     * (XhciXferIsoFlowNext); controller
                                     * lock                               */
    ULONG RingWait;                 /* a record waits for ring room;
                                     * controller lock                    */
    /* 36.21: the stamp at which software found this ring empty with a URB
     * unfinished, until the next publication on it or that URB's end;
     * controller lock. */
    XHCI_XSTAT_STAMP Empty;
    /* Streams (31-A.1; xhci98_streams.h). An endpoint with streams open
     * has Streams, and takes no transfer itself; each stream is a pipe of
     * its own with Parent the endpoint's and StreamId 1..31, on a ring in
     * the endpoint's stream block. Streams and its Live under the
     * controller lock. */
    ULONG StreamId;                 /* 0: not a stream                    */
    ULONG StreamFault;              /* a stream's own event halted or
                                     * errored the endpoint: its recovery
                                     * owes it a Set TR Dequeue (hcd_cfg.c,
                                     * hcdCfgRecoverDequeue); controller
                                     * lock                               */
    /* Task 49.45 (design record 13 section 6.13.11): short IN transfers
     * completed as USBD_STATUS_ERROR_SHORT_TRANSFER on this pipe, the first
     * XHCI_USBDX_NOTE_SHORT_MAX noted; interlocked. */
    volatile LONG ShortRefused;
    struct _HCD_PIPE *Parent;       /* a stream's endpoint pipe           */
    struct _HCD_STREAMS *Streams;   /* the endpoint's open streams        */
    /* Task 38.10 (design record 20 section 5; the thread's): a closed pipe
     * whose ring an Endpoint Context may still name is held on its
     * device's Retained list, not freed, until a proof releases it - a
     * successful Configure Endpoint dropping its DCI, a Reset Device, the
     * slot's going (hcd_cfg.c, hcdCfgRetain). Pinned: held after a command
     * whose outcome was never seen, until the slot goes (the HCRST that
     * outcome requested). */
    struct _HCD_PIPE *RetainNext;
    ULONG Pinned;
    /* Task 48.33 (design record 13 section 11.11.1; hcd_stage.c): the
     * pipe's staging, taken when it opens and given back when it is freed.
     * StageVa: its block - the records' small buffers, then its page
     * reserve, or an isochronous pipe's shares - StageOwn when the pipe
     * allocated it (a stream's is a slice of its endpoint's block).
     * ReserveKind HCD_STAGE_RES_*: a page (ReserveVa, inside the block) or
     * a slab, the pool's ReserveSlab or one allocated for it (ReserveOwnVa).
     * ReserveBusy: a TD holds it, StageBlockPass: the admission pass that
     * found its head unable to move, both under StageLock. IsoShareBytes:
     * an isochronous record's share. */
    PUCHAR StageVa;
    PHYSICAL_ADDRESS StagePa;
    ULONG StageBytes;
    ULONG StageOwn;
    ULONG ReserveKind;
    PUCHAR ReserveVa;
    ULONG ReservePa;
    ULONG ReserveBytes;
    ULONG ReserveSlab;
    PUCHAR ReserveOwnVa;
    PHYSICAL_ADDRESS ReserveOwnPa;
    ULONG ReserveBusy;
    ULONG StageBlockPass;
    ULONG IsoShareBytes;
    ULONG XferCount;                /* records in Xfers: a stream's pipe is
                                     * allocated short (HCD_STREAM_XFERS),
                                     * an isochronous one long
                                     * (HCD_ISO_PIPE_XFERS)               */
    HCD_XFER Xfers[HCD_PIPE_XFERS]; /* last: see XferCount                */
} HCD_PIPE, *PHCD_PIPE;

/* The bytes of a pipe with `n` records. A stream's pipe is allocated with
 * HCD_STREAM_XFERS and an isochronous pipe with HCD_ISO_PIPE_XFERS (record
 * 13 section 11.9), the declared HCD_PIPE_XFERS being every other pipe's,
 * the embedded Ep0Pipe's among them: nothing reads Xfers past XferCount. */
#define HCD_PIPE_BYTES(n)                                                    \
    ((ULONG)(FIELD_OFFSET(HCD_PIPE, Xfers) + (n) * sizeof(HCD_XFER)))
#define HCD_STREAM_PIPE_BYTES HCD_PIPE_BYTES(HCD_STREAM_XFERS)
#define HCD_ISO_PIPE_BYTES    HCD_PIPE_BYTES(HCD_ISO_PIPE_XFERS)
XHCI_C_ASSERT(hcd_iso_pipe_bytes, HCD_ISO_PIPE_BYTES >= sizeof(HCD_PIPE));
/* hcdCfgCancelPipe's walk holds two TDs a record of an ordinary pipe; an
 * isochronous record is one TD group (hcd_cfg.c). */
XHCI_C_ASSERT(hcd_cancel_tds, 2UL * HCD_PIPE_XFERS >= HCD_ISO_PIPE_XFERS);

/*
 * An endpoint's open streams (hcd_cfg.c, 31-A.1): the plan, the one common
 * buffer holding the Primary Stream Context Array and every stream's ring
 * (xhci_stream.h, XhciStreamLayout), and the pipes. Ring and Pipe are
 * indexed by Stream ID, [0] unused. Live is set once the Configure Endpoint
 * that installs the array has succeeded: only then does a stream's handle
 * resolve. Allocated and freed by the thread; read by the event DPC and the
 * dispatch under the controller lock.
 */
/* A retired isochronous ring page (record 13 section 11.8, point 8): the
 * allocation of a pipe freed while its endpoint's context may still name
 * the ring, on its device's IsoPagesRetired until a proof frees it. Pool,
 * the thread's. */
typedef struct _HCD_ISO_PAGE {
    struct _HCD_ISO_PAGE *Next;
    ULONG Dci;
    PVOID Va;
    PHYSICAL_ADDRESS Pa;
    ULONG Bytes;
} HCD_ISO_PAGE, *PHCD_ISO_PAGE;

typedef struct _HCD_STREAMS {
    ULONG Count;                    /* granted: Stream IDs 1..Count       */
    ULONG Dci;                      /* the endpoint's, for a retired block */
    ULONG Entries;
    ULONG MaxPStreams;
    ULONG Live;
    PVOID Va;
    PHYSICAL_ADDRESS Pa;
    XHCI_STREAM_LAYOUT Layout;
    ULONG RingTrbs;                 /* each stream's ring (36.25: 1024 on a
                                     * big block, else the pool's 64)    */
    PXHCI_RING Ring[XHCI_STREAM_MAX_ENTRIES];
    struct _HCD_PIPE *Pipe[XHCI_STREAM_MAX_ENTRIES];
    struct _HCD_STREAMS *Next;      /* the device's StreamsRetired list    */
    /* Task 48.33: every stream's staging - its records' small buffers and
     * its page reserve - in one block of the endpoint's (hcd_stage.c). */
    PUCHAR StageVa;
    PHYSICAL_ADDRESS StagePa;
    ULONG StageBytes;
} HCD_STREAMS, *PHCD_STREAMS;

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
    ULONG SpeedFallback;    /* Speed is issue 18's default ID, given behind
                             * a hub where the root port's USB 2.0 table
                             * names no such speed (HcdHubPlace); every
                             * decode of it asks HcdDeviceSpeedClass     */
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
    /* SuperSpeed (29-A.1, 29-A.3): the link rate the port trained at, in
     * kbit/s, whether it is SuperSpeedPlus, and the BOS descriptor, kept
     * whole (pool; NULL when none was read) with what XhciPipeParseBos
     * made of it - for the endpoint rules and the snapshot (29-A.6). */
    ULONG RateKbps;
    ULONG Plus;
    /* Its own SuperSpeed link's rank (XHCI_SS_RANK_*, xhci_sshub.h): on a
     * root port from the PSI rate and PORTLI at Address Device
     * (hcd_enum.c), behind a SuperSpeed hub from that hub's extended port
     * status (HcdHubPlace); 0 unknown, and 0 for a USB 2.0 device. And,
     * for an SS/SSP device behind a hub that outranks its link, that hub's
     * Slot ID and port for the Slot Context's Parent Hub Slot ID and Parent
     * Port Number (xHCI Table 6-6) - kept apart from TtSlot/TtPort, which
     * name a transaction translator that CLEAR_TT_BUFFER is sent to. */
    ULONG SsLinkRank;
    ULONG SsParentSlot;
    ULONG SsParentPort;
    PUCHAR Bos;
    ULONG BosLength;
    XHCI_PIPE_BOS BosInfo;
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
    ULONG ResumeRef;        /* task 45.23: the temporary pool reference
                             * its rings are held under while kept across
                             * a resume, before its new slot; 0 none
                             * (hcd_enum.c); thread                      */
    ULONG QuietMark;        /* task 45.23 (design record 20 section
                             * 4.3.3): its function's quiet declaration,
                             * valid while it equals the controller's
                             * MarkGen + 1; 0 none. Cleared by any transfer
                             * taken or parked off EP0 (HcdIoSubmit).
                             * Controller lock                           */
    ULONG ResumeEpoch;      /* one more at each restore across a
                             * re-initialization (hcd_enum.c); controller
                             * lock                                       */
    /* Task 49.34 (design record 20 section 4.3.4; hcd_bot.c): a Bulk-Only
     * drive's transport as the bus sees it, fed only with
     * XhciResumeKeepBot on (BotShape 1: M2's one interface, one SCSI
     * Bulk-Only setting, its two bulk pipes' DCIs); controller lock.
     * BotConfigured: a select has been served since the enumeration.
     * BotLatch: set by the keep's decision, cleared by any admission of a
     * function request; BotKeeping: a Bulk-Only keep in progress, its
     * decision's copy of the class driver's capacity answer in
     * BotRecorded. BotXfer: the medium check's own transfer (no IRP),
     * armed and done under the controller lock. */
    XHCI_BOT Bot;
    ULONG BotShape;
    ULONG BotDciIn;
    ULONG BotDciOut;
    ULONG BotConfigured;
    ULONG BotLatch;
    ULONG BotKeeping;
    XHCI_BOT_CAP BotRecorded;
    XHCI_TRANSFER BotXfer;
    ULONG BotXferArmed;
    ULONG BotXferDone;
    ULONG LinkKeepCount;    /* task 45.41 (design record 13 section 15,
                             * L2): kept across a link's recovery this
                             * many times since LinkKeepFirst, a tolerance-
                             * clock tick (XhciEnumLinkKeepRecent); thread */
    ULONG LinkKeepFirst;
    ULONG HoldAsked;        /* a send-back to USB 2.0 was accepted for it
                             * (HcdHoldRequestUsb2): no PDO, Present until
                             * the hold service disconnects or refuses it */
    ULONG HoldRefused;      /* that request was refused late: refused in
                             * place, never asked again (hcd_pdo.c)      */
    /* The serial number string as an instance id (33.2; hcd_enum.c,
     * HcdDeviceReadSerial), read once per enumeration before its first
     * PDO: HCD_SERIAL_*, and SerialId non-empty only when HCD_SERIAL_OK. */
    ULONG SerialState;
    char SerialId[XHCI_SERIAL_ID_BYTES];
    /* The URB path (hcd_io.c). Refs counts URB IRPs that hold the record,
     * taken under PdoListLock while the PDO still names it; Gone, under
     * the controller lock, refuses new submissions once the thread has
     * begun freeing it (hcdDeviceFree waits Refs out). */
    volatile LONG Refs;
    ULONG Gone;
    ULONG AbortAll;         /* the PDO is being removed: abort every pipe */
    ULONG Ep0Stuck;         /* the thread's EP0 record timed out, or its
                             * wait was abandoned for a cycle, and may
                             * still be queued: no control transfer until
                             * the device's slot is taken back            */
    /* 35-T.3/4 (design record 17 section 4.3). CycleMark: the cycle
     * wanted, set by the event path and taken by the thread under the
     * controller lock (HcdTolCycleMark). CycleAbandon: the thread gave its
     * own EP0 work on the device up for that cycle (HCD_CTL_ABANDONED);
     * Ep0Abandoned: and a TD of it was outstanding, so the scratch is held
     * (ScratchHeld) until the slot is taken back. Thread only. */
    XHCI_TOL_MARK CycleMark;
    /* An event on the slot carried a slot-fatal code (XhciXferSlotFatal):
     * set by the event DPC, taken by the thread (hcdSlotFatalService),
     * both under the controller lock. */
    ULONG SlotFatal;
    ULONG CycleAbandon;
    ULONG Ep0Abandoned;
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
    struct _HCD_PIPE *Retained;     /* task 38.10: closed pipes whose rings
                                     * the controller may still hold, the
                                     * stale endpoints' and the pinned
                                     * (HCD_PIPE.RetainNext); thread      */
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
    struct _HCD_ISO_PAGE *IsoPagesRetired; /* record 13 section 11.8:
                                     * isochronous ring pages of pipes
                                     * freed while their endpoint was not
                                     * Disabled, freed by the proof that
                                     * frees a retired stream block; thread */
    struct _HCD_STREAMS *StreamsRetired; /* stream blocks of pipes closed
                                     * while their endpoint was still
                                     * enabled: the controller may hold
                                     * the array until the slot goes
                                     * (hcd_cfg.c, 31-A.1); thread        */
    /* Selective suspend (design record 14, hcd_susp.c): the link machine,
     * written by the thread alone and read under the controller lock; the
     * slow IRPs deferred while SUSPENDED (section 3.3), under the
     * controller lock; and a wake event whose sequence the pass owes. */
    XHCI_SUSP_LINK Susp;
    LIST_ENTRY SlowDeferred;
    ULONG SuspWakeDue;
    /* The idle notification's batch count (section 6.2, 36.32): the last
     * batch formed for the device, 0 before the first; thread, under the
     * controller lock. */
    ULONG IdleBatch;
    /* Stage B (section 8.3, 36.38): the first interface of every USB 3
     * function of the active configuration (bit n: bInterfaceNumber n), and
     * those whose GetStatus(interface) answered Function Remote Wake
     * Capable. Written by the thread under the controller lock, cleared at
     * a configuration change's entry and written at its exit
     * (HcdSuspCfgExit); 0 for a new device and with the switch at 0. The
     * notification latch lives in Susp (FwPending). */
    ULONG FwFirsts;
    ULONG FwCapable;
    ULONG FwArmedAll;       /* the union of its PDOs' FwArmed, kept by the
                             * thread under the controller lock for the
                             * snapshot, which walks no PDO chain        */
    /* Its storage transport decision (roadmap-usb4 44.35; design record
     * 13 section 10.7, "The decision kept"), for the snapshot and the
     * query, which walk no PDO chain: the interfaces with a mass-storage
     * decision seen (bit n: interface n below 31, bit 31 a device PDO or
     * a higher number) and their count, and the lowest-numbered one's
     * decision. Written by the thread under the controller lock
     * (hcdXportKeep); XportCount 0: none. */
    ULONG XportSeen;
    ULONG XportCount;
    ULONG XportIface;       /* bInterfaceNumber, 0xFF a device PDO       */
    ULONG XportTransport;
    ULONG XportWhy;
    ULONG XportAlternate;
    ULONG XportInputs;      /* XHCI_XPORT_F_*                            */
    ULONG XportTriple;      /* class 7:0, subclass 15:8, protocol 23:16  */
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
    ULONG PnpStarted;               /* a START seen: PnP has its devnode, so
                                     * its properties may be asked (the
                                     * door's driver key, hcd_door.c)     */
    ULONG DeletePending;            /* on RemovedPdos, deleted at the next
                                     * relations answer (hcd_pdo.c)       */
    ULONG Dormant;                  /* listed with no device: stopped by PnP
                                     * across a controller stop, revived by
                                     * its device's re-enumeration        */
    ULONG Surprised;                /* IRP_MN_SURPRISE_REMOVAL seen        */
    ULONG ParentLetGo;              /* removed by PnP, then its hub's FDO
                                     * removed too: PnP has forgotten it,
                                     * no answer carries it again, and the
                                     * next hub FDO start cycles its port
                                     * (HcdDevicePdoLetGo, task 33.4)     */
    ULONG RepresentAsked;           /* its cycle asked in this start's
                                     * batch (HcdDevicePdoRepresent)      */
    ULONG Deleted;                 /* IoDeleteDevice called: once only     */
    ULONG Serial;                   /* the name's number; a port waits on it */
    ULONG Closing;                  /* stopping or removed: URBs refused  */
    ULONG Ghost;                    /* 98 SE and ME: started after its
                                     * device left, answering from its
                                     * copies (design record 13 section
                                     * 10.5.2); PdoListLock when set     */
    volatile LONG UrbsPending;      /* URB IRPs pended here and not yet
                                     * completed; REMOVE waits for 0      */
    volatile LONG CancelsUnserved;  /* waiting IRPs cancelled with no
                                     * controller (hcdWaitCancel)         */
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
    /* Set once, on 98 SE and ME, by a SELECT_CONFIGURATION reaching the
     * PDO after its device left (HcdIoIsoRelease, design record 13
     * section 18.5), under the cancel spin lock: from then on a departed
     * device's isochronous request is completed CANCELED rather than held.
     * Never cleared: a departed PDO is deleted at its REMOVE. */
    ULONG IsoReleased;
    /* Whether that release applies here - 98 SE or ME - read once at the
     * PDO's creation at PASSIVE_LEVEL (HcdIoRefusedInit), since
     * IoIsWdmVersionAvailable is PASSIVE_LEVEL only and a select may be
     * submitted at DISPATCH_LEVEL. */
    ULONG IsoReleaseOn;
    /* Abort horizons (HcdIoAbortMark, hcd_io.c), under the cancel spin
     * lock: every URB IRP is stamped at its dispatch with the next value
     * of SubmitSeq - a 64-bit count as a Lo/Hi pair; the IRP keeps its low
     * word in DriverContext[0] - and an ABORT_PIPE records the value its
     * own stamp took as its pipe's horizon, or, for a handle it cannot
     * place, every pipe's (AbortAll). A request whose stamp is at or below
     * its pipe's horizon was submitted before an abort of it and is
     * completed CANCELED rather than held (xhci_pipe.h, XhciSeqCovers). A
     * horizon is never cleared or evicted: a table that is full raises
     * AbortAll instead. */
    XHCI_SEQ64 SubmitSeq;
    XHCI_SEQ64 AbortAll;
    ULONG AbortCount;
    PVOID AbortPipe[HCD_PDO_ABORTS];
    XHCI_SEQ64 AbortHorizon[HCD_PDO_ABORTS];
    volatile LONG Busy;             /* dispatches inside hcd_urb.c, raised
                                     * before Controller is read; the
                                     * parent's release waits it out      */
    ULONG Port;                     /* its device's Location: the port
                                     * handshake (hcdPortNotify) names it */
    ULONG InstanceKey;              /* the instance id without a serial id
                                     * (XhciHubInstanceKey): the root port,
                                     * with the route above it behind hubs */
    ULONG Address;                  /* DEVICE_CAPABILITIES Address and
                                     * UINumber: the port on the parent it
                                     * is presented under (XhciHubPdoAddress,
                                     * section 10.11). Fixed at creation  */
    char SerialId[XHCI_SERIAL_ID_BYTES]; /* the instance id's serial (33.2,
                                     * XhciFuncInstanceId) and UniqueID
                                     * TRUE; empty for the location form.
                                     * Fixed at creation, and under
                                     * PdoListLock until listed          */
    char ReadSerialId[XHCI_SERIAL_ID_BYTES]; /* the serial id as read,
                                     * kept when a duplicate empties
                                     * SerialId: what a dormant group
                                     * named by its place is matched on  */
    char KeepSerialId[XHCI_SERIAL_ID_BYTES]; /* task 49.34: the serial its
                                     * device last read, refreshed at a
                                     * revival: what the keeps' twin check
                                     * (hcdSerialTwinLocked) compares, and
                                     * nothing else reads; PdoListLock   */
    ULONG SerialUnread;             /* every serial read failed
                                     * (HCD_SERIAL_FAILED): ReadSerialId
                                     * is unknown, not empty              */
    WCHAR Text[XHCI_TEXT_WCHARS];   /* DeviceTextDescription (33.6,
                                     * HcdDeviceReadText), NUL-terminated;
                                     * empty for "USB Device". Fixed at
                                     * creation, kept by a revived PDO   */
    ULONG RootPort;                 /* its device's Port and Route, fixed */
    ULONG Route;                    /* at creation (GET_TOPOLOGY_ADDRESS) */
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
    /* The storage transport the bus chose for this PDO's interface
     * (31-A.3; xhci_xport.h), fixed at creation so every id query answers
     * alike. Transport XHCI_XPORT_NONE: section 10.7's ids unchanged. */
    XHCI_XPORT Xport;
    ULONG XportInputs;              /* the XHCI_XPORT_F_* it was decided
                                       under, copied with Xport          */
    /* Every PDO of one device: the serial a port waits on and a cycle
     * names (Group, the first PDO's Serial; a lone device PDO's own), and
     * the chain HcdDevicePdoGone walks when the device leaves (Sibling,
     * written before the PDOs are listed, read only by the thread). */
    ULONG Group;
    struct _HCD_DEVICE_PDO *Sibling;
    /* What Windows XP onward asks (task 28-A.1, hcd_urb.c). IdleIrp: the
     * one IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION held, under the
     * cancel spin lock, until its client cancels it or the PDO stops;
     * IdlePending counts it from acceptance until its completion has
     * returned, and a stop or removal waits for 0. BusifSlot: the
     * USB_BUS_INTERFACE_USBDI context this PDO's clients were given, in
     * hcd_urb.c's static table, which outlives the PDO. PciBus and
     * PciAddress: the controller's location for GET_TOPOLOGY_ADDRESS, read
     * once at PASSIVE_LEVEL (PciRead set after both). */
    PIRP IdleIrp;
    volatile LONG IdlePending;
    PVOID BusifSlot;
    /* What the capability request 0x49104B answers for STATIC_STREAMS
     * (task 43.29; xhci_usbdx.h, XhciUsbdxMaxStreams): the most streams the
     * bus grants an endpoint on this controller, 0 when it does not
     * stream; latched at creation on the thread, so the dispatch reads no
     * register. */
    ULONG UsbdxStreams;
    /* Task 49.45 (design record 13 section 6.13.11; xhci_usbdx.h): the
     * notes of what the client asked, bounded per PDO and zeroed at its
     * IRP_MN_REMOVE_DEVICE. The two tables hold the QUERY_INTERFACE GUIDs
     * (Data1) and the refused internal IOCTL codes already noted, a slot
     * claimed by the interlocked count; the four counts claim the usbdx
     * notes and, at level 3, the per-URB lines of a UAS PDO. OwnClient: a
     * request of the bus's private interface (xhci98_streams.h,
     * xhci98_xfer.h) arrived, so the client is this package's own UAS
     * driver and the per-URB lines are not written. */
    ULONG NoteQi[XHCI_USBDX_NOTE_QI_SLOTS];
    volatile LONG NoteQiUsed;
    ULONG NoteIoctl[XHCI_USBDX_NOTE_IOCTL_SLOTS];
    volatile LONG NoteIoctlUsed;
    volatile LONG NoteV1;
    volatile LONG NoteCap;
    volatile LONG NoteStreams;
    volatile LONG NoteUrbs;
    volatile LONG OwnClient;
    ULONG PciBus;
    ULONG PciAddress;
    volatile ULONG PciRead;
    /* GUID_DEVINTERFACE_USB_DEVICE (task 36.17; hcd_door.c,
     * HcdDoorDeviceStart): the link IoRegisterDeviceInterface gave at the
     * first start, and whether it is enabled. Written on the PnP paths and
     * at the deletion only, all PASSIVE_LEVEL. */
    UNICODE_STRING UsbInterface;
    ULONG UsbInterfaceOn;
    /* Where it is presented (task 33.4; design record 13 section 10.11):
     * the Serial of the hub PDO whose FDO's BusRelations carry it, 0 for
     * the root hub's - the nearest hub above it that has a PDO
     * (XhciHubPresentedParent). Fixed at creation. */
    ULONG ParentSerial;
    /* A hub's own PDO (task 33.4): bound to this driver as a hub FDO, which
     * the bus still serves. What the hub FDO's door answers is copied here
     * at creation, so it never reads the thread's hub object: the hub
     * object's index (its ports' locations), its descriptor, whether it is
     * bus-powered, its multi-TT capability and state, and whether it is a
     * USB 3 hub's SuperSpeed half. HubLinked: the hub FDO's door name
     * exists (\DosDevices\XHCI98HUB<Serial>), under PdoListLock. */
    ULONG Hub;
    ULONG HubUsb3;
    ULONG HubIndex;
    XHCI_HUB_DESC HubDesc;
    ULONG HubBusPowered;
    ULONG HubMttCapable;
    ULONG HubMttOn;
    ULONG HubLinked;
    /* Selective suspend's slot (design record 14 section 4.1), under the
     * controller lock: the one pended D-IRP, the D-state asked and the one
     * the bus acts on (XHCI_SUSP_D*, PowerDeviceD0..D3), the epoch a
     * pended Dx was accepted in, admission closed, the change count, the
     * hold after a claimed wake, and the WAIT_WAKE held with its stamp.
     * PowerPending counts a D-IRP or WAIT_WAKE from acceptance until its
     * completion returns. Dispatch fills them (36.31); the thread reads
     * and settles them (hcd_susp.c). */
    PIRP PwrIrp;
    ULONG PwrWanted;
    ULONG PwrState;
    ULONG PwrDxEpoch;
    ULONG PwrClosed;
    ULONG PwrSeq;
    ULONG WokeHold;
    PIRP WaitWakeIrp;
    ULONG WakeStamp;
    volatile LONG PowerPending;
    ULONG PwrSwitch;                /* the controller's switch at the PDO's
                                     * creation or last START: what a
                                     * released controller's PDO answers by */
    volatile LONG PwrReport;        /* a test-and-set flag ordering the
                                     * PDO's power reports
                                     * (PoSetPowerState) with its D-IRP
                                     * admission (hcd_susp.c); PASSIVE  */
#if defined(XHCI_FLAVOUR_QEMU)
    /* Record 15 section 7.2.12, leg T3 (qemu only): whether
     * XhciQemuSuspHoldDx's first skip of a pended Dx was noted. The
     * claiming path travels in the IRP (hcd_susp.c, HCD_SUSP_QEMU_MARK). */
    ULONG QemuHoldNoted;
#endif
    /* The idle notification (design record 14 sections 4.4 and 6; task
     * 36.32, hcd_susp.c), under the controller lock: stage A's held IRP
     * (today's stays IdleIrp, above) and its state (XHCI_SUSP_IDLE_*), the
     * callback and its context copied at submission, the batch the PDO is
     * in (0: none), its callback-queue entry with the batch and epoch it
     * was queued in, whether that entry is on the queue and whether it is
     * owned at all (queued, or claimed by the callback thread), and the
     * strongest settlement trigger (XHCI_SUSP_SET_*) that reached the IRP
     * while its callback ran. PwrStarted: started and not torn down since
     * (6.2's "started"). IdleRunning: a callback of the PDO's has not
     * returned; IdleQuarantined: its rundown could not finish, so the
     * object is kept (4.4's quarantine); both read without the lock by
     * the rundown. IdlePending (above) counts this IRP and its entry. */
    PIRP IdleSlot;
    ULONG IdleState;
    PVOID IdleCb;
    PVOID IdleCbContext;
    ULONG IdleBatchCur;
    LIST_ENTRY IdleEntry;
    ULONG IdleQueued;
    ULONG IdleEntryOwned;
    ULONG IdleEntryBatch;
    ULONG IdleEntryEpoch;
    ULONG IdleOwed;
    ULONG PwrStarted;
    volatile ULONG IdleRunning;
    volatile ULONG IdleQuarantined;
    volatile ULONG IdlePinned;      /* its callback was abandoned by a stop
                                     * without the DMA-halt proof         */
    /* A composite device's own PDO (task 36.43; design record 13 section
     * 10.12): the parent of its function PDOs, the group's first PDO, under
     * USB\XHCI98_COMPOSITE, bound to this driver as a composite FDO. Fixed at
     * creation. Presentation only: it carries no pipe and takes no URB. */
    ULONG Composite;
    /* Stage B (design record 14 sections 8.4 and 8.5, 36.38), the thread's
     * alone: FwArmed, its wake functions (bit n: first interface n) the
     * device took SetFeature(FUNCTION_SUSPEND) options 03h for and has not
     * taken options 0 for since; ArmNow, the decision's per-PDO arm intent,
     * copied in the decision's hold for the ARM step. Both cleared at the
     * unlink (HcdSuspUnlinkLocked) and in hcdDormantKeep; never by a STOP,
     * REMOVE or START of a PDO whose device stays linked. */
    ULONG FwArmed;
    ULONG ArmNow;
    /* The Windows 98 SE and ME silent install (task 38.2; design record 13
     * section 6.11.4): 0 or 1, written once by hcdSilentDecide before the
     * PDO is listed, from its final Config, kind and transport and the
     * controller's gate and table; IRP_MN_QUERY_CAPABILITIES copies it. */
    ULONG SilentInstall;
    /* A composite parent's record (task 38.12; design record 13 section
     * 10.14.2): the configuration descriptor its own composite FDO's
     * SELECT_CONFIGURATION carried, or NULL. Swapped under the
     * controller's PdoListLock by the parent's dispatch and its FDO
     * (HcdCompositeForget); read by the thread through a reference taken
     * under the same lock (HcdCompRecordRef). The PDO's deletion drops its
     * reference. */
    struct _HCD_COMP_RECORD *CompRecord;
} HCD_DEVICE_PDO, *PHCD_DEVICE_PDO;

/* A composite record (task 38.12): reference-counted, so a function select
 * on the thread keeps the bytes it reads while the parent's dispatch
 * replaces or forgets the record. Freed at the last reference, at <=
 * DISPATCH_LEVEL. */
typedef struct _HCD_COMP_RECORD {
    volatile LONG Refs;
    ULONG Length;
    UCHAR Bytes[1];
} HCD_COMP_RECORD, *PHCD_COMP_RECORD;

/* A PDO other PDOs are presented under (ParentSerial): a hub's (task 33.4)
 * or a composite device's (task 36.43). */
#define HcdPdoIsParent(pdo) ((pdo)->Hub || (pdo)->Composite)

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
    /* A root port (29-A.1, 29-A.2): the raw PSIV its last reset left, for
     * the Slot Context - the machine itself is fed the decoded class's
     * default ID - and, on a USB3 protocol port, its link's record. */
    ULONG LinkPsiv;
    XHCI_LINK_PORT Link;
    /* A USB3 root port's failing link (issue 29, design record 13 section
     * 21): its failures in the window, sent to USB 2.0 past the limit. */
    XHCI_LINK_FLAP Flap;
    /* A SuperSpeed hub's port: its pending send's failed attempts. */
    XHCI_LINK_DISABLE HubSsDisable;
    /* A SuperSpeed hub's port (30-A.1, hcd_sshub.c): the link its last
     * reset left, from the extended port status on a SuperSpeedPlus hub
     * (zero otherwise), for the Protocol Speed ID its device is given;
     * Link.WarmResets is that port's warm-reset budget. */
    XHCI_SSHUB_LINK HubSsLink;
    ULONG HubSsRecover;     /* its link wants a warm reset once the device
                             * it held is torn down (HcdSsHubPortRecover) */
    /* Given up and left in SS.Disabled (HcdHubPortDisable), which detects
     * nothing: re-armed to RxDetect after HubSsRearmWait more ticks of its
     * hub's re-arm timer, the wait doubling with each re-arm in a row
     * (HubSsRearms) up to a cap (hcd_hub.c, hcdHubRearmPorts). */
    ULONG HubSsRearmWait;
    ULONG HubSsRearms;
    /* Root port: PDO recreations hcdHoldResolve saw fail since the port
     * last read physically disconnected (hcd_enum.c). Nonzero, no device
     * on the port asks for a send-back again; at HCD_HOLD_RECOVER_TRIES
     * the device is left refused with no PDO. */
    ULONG HoldRecoverFails;
    /* Task 45.40 (design record 13 section 10.7, "Identity across a
     * re-enumeration"): a root port's identity record (XHCI_ID_PLACE) -
     * the last non-hub group listed for a device attached directly to it,
     * written once it is listed or revived (hcdIdRemember), the deferral's
     * latch, and the deferrals made and spent here; a place behind a hub
     * has its record in the controller's IdPlaces instead. Zero at
     * AddDevice, kept for the controller's life. Thread only. */
    XHCI_ID_PLACE Id;
    /* Root port: inspections in a row that read PORTSC as all ones. The
     * inspection is owed again at every pass until one reads it, and at
     * HCD_PORT_UNREADABLE_PASSES the controller goes to recovery
     * (hcd_enum.c, hcdPortChanged). */
    ULONG Unreadable;
    ULONG HubSsSeen;        /* a connection read since its last re-arm:
                             * an empty port after that is a departure of
                             * the device's own, which restarts the waits */
    ULONG ResumeTries;      /* a hub port: resumes failed in a row
                             * (XhciHubResumeOutcome); thread only       */
    ULONG ResumePending;    /* a hub port: a resume to try again at its
                             * next look, even if that look's GET_STATUS
                             * fails; thread only                        */
    /* Task 33.3: an enumeration that ended in a halt (a command or EP0
     * transfer that never completed) while a first answer waited, so the
     * port is not looked at again until no first answer waits and does
     * not hold the ports after it back. Thread only. */
    ULONG SettleDeferred;
    /* Task 33.3: a root port's SuperSpeed link warm-reset by an
     * inspection, its outcome not yet read (nonzero; counts the
     * inspections that found it still in reset); and a hub port's GET_STATUS
     * failures in a row, its look owed again up to HCD_HUB_LOOK_TRIES.
     * Thread only. */
    ULONG LinkRecovering;
    ULONG LookFails;
    /* 35-T.5 (design record 17 section 4.5). TolOc: a root port's
     * over-current episode; thread only. TolCompletions: transfers its
     * device completed with success or a short packet, counted by the event
     * path under the controller lock; TolCompletionsSeen, the thread's last
     * reading of it, for the location's stable progress. Each set by the
     * start (HcdEnumInit). The location's budget itself is the
     * extension's too: Tol.RootLoc for a root port, Tol.HubLoc by port
     * object for a hub's, set again when its hub is brought up. */
    XHCI_TOL_OC TolOc;
    ULONG TolCompletions;
    /* At XhciTolerance 0: a power loss already counted, until PP reads
     * set again (record 17 section 4.11). Set by the start. */
    ULONG TolOffPpLost;
    ULONG TolCompletionsSeen;
    /* 35-T.3/4: the location's connect generation, bumped by every
     * connect and disconnect fed at it, written by the thread and read by
     * the event path's cycle mark under the controller lock; and the
     * reason a pre-PDO cycle under way is for (XHCI_TOL_CYCLE_*), thread
     * only. Each set by the start (HcdEnumInit). CycleCharge: the one
     * re-enumeration charge of the cycle pending at ConnectGen, shared by
     * every producer of it (HcdTolLocCharge), thread only. */
    ULONG ConnectGen;
    ULONG CycleReason;
    XHCI_TOL_CYCLE_CHARGE CycleCharge;
    /* Task 35.3: a root port's enumeration notes and their budget
     * (xhci_enum.h), cleared by the start. Thread only. */
    XHCI_ENUM_NOTES Notes;
    /* Task 43.27 (record 13 section 8.4.1, rule 3): the VID:PID of the
     * device this attempt read the BOS of at High or Full Speed, kept here
     * because the attempt's teardown frees the device before a halt it
     * causes is seen. Thread only. */
    ULONG Usb2BosVidPid;
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
/* Task 45.40: the places behind hubs whose identity is kept, the snapshot
 * header's entry count (xhci.h). */
#define HCD_ID_PLACES       XHCI_SNAPSHOT_HCD_IDPLACE_COUNT
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
    /* The SuperSpeed half of a USB 3 hub (30-A.1, hcd_sshub.c): Desc is
     * filled from SsDesc so the shared code reads one shape. */
    ULONG Usb3;
    XHCI_SSHUB_DESC SsDesc;
    ULONG ExtStatus;        /* answers GET_PORT_STATUS type 2            */
    /* Ports given up in SS.Disabled awaiting their re-arm (bit n port n),
     * and the timer whose expiry is one tick of their waits (hcd_hub.c).
     * RearmArmed: the timer is set and must be cancelled before the object
     * is cleared. */
    ULONG RearmPorts;
    ULONG RearmArmed;
    KTIMER RearmTimer;
    /* Issue 29 (design record 13 section 21.5): ports whose send to the
     * hub's USB 2.0 half failed on the wire and is tried again on the
     * same tick (bit n port n; HcdHubFlapSend). */
    ULONG DisablePorts;
} HCD_HUB, *PHCD_HUB;

/*
 * 29-A.5's active fallback, the hold (hcd_enum.c; xhci_link.h): a device on
 * a SuperSpeed root port sent back to its USB 2.0 companion. A small table
 * rather than a field per port: holds are rare, and a port object is one of
 * HCD_PORT_COUNT. Every entry is the thread's, except that
 * HcdHoldRequestUsb2 claims one and marks it Pending under the controller
 * lock; a full table refuses the request, counted.
 */
#define HCD_MAX_HOLDS               8UL

/* PDO recreations after a refused send-back that may fail on one root port
 * before its device is left refused with no PDO (hcd_enum.c,
 * hcdHoldResolve); a bus policy number. */
#define HCD_HOLD_RECOVER_TRIES      3UL

/* Why a send-back was asked for (HcdHoldRequestUsb2's `reason`). */
#define HCD_HOLD_REASON_UAS_NO_STREAMS  1UL /* 31-A.3: UAS-only, no streams */
#define HCD_HOLD_REASON_SSP_REFUSED     2UL /* 29-A.1's SuperSpeedPlus value */
#define HCD_HOLD_REASON_OTHER           3UL
#define HCD_HOLD_REASON_LINK_FLAP       4UL /* issue 29: the bus's own, for a
                                             * link that keeps failing     */

typedef struct _HCD_HOLD {
    ULONG Used;             /* claimed                                   */
    ULONG Pending;          /* asked for; the thread has not acted yet   */
    ULONG Reason;           /* HCD_HOLD_REASON_*                         */
    ULONG Port;             /* the held SuperSpeed root port             */
    ULONG Unreadable;       /* passes its PORTSC read all ones while the
                             * request was pending                       */
    XHCI_LINK_HOLD Hold;    /* the pure state, xhci_link.c               */
    ULONG SettleGen;        /* SettleAsked when asked for (task 33.3)    */
} HCD_HOLD, *PHCD_HOLD;

/* A hub's port object, n from 1. */
#define HcdHubPort(hc, hub, n)                                               \
    (&(hc)->Ports[XHCI_MAX_ROOT_PORTS +                                      \
                  (hub)->Index * HCD_HUB_MAX_PORTS + (n) - 1UL])

/* URB functions counted one by one (hcd_urb.c); the Windows 2000 DDK's
 * highest is 0x002A (usbdi.h), and anything at or above this is counted as
 * unknown. */
#define HCD_URB_FUNCTIONS 0x40UL

/*
 * Windows 8's stream URBs (task 43.29, design record 13 section 6.13.2):
 * URB_FUNCTION_OPEN_STATIC_STREAMS with its _URB_OPEN_STATIC_STREAMS and
 * USBD_STREAM_INFORMATION, and URB_FUNCTION_CLOSE_STATIC_STREAMS, which
 * carries a _URB_PIPE_REQUEST. Declared here because neither DDK this tree
 * builds with has them (Microsoft's "_URB_OPEN_STATIC_STREAMS" and
 * "_USBD_STREAM_INFORMATION" pages give the members); the sizes are
 * asserted in hcd_urb.c.
 */
#define HCD_URB_OPEN_STATIC_STREAMS     0x0035
#define HCD_URB_CLOSE_STATIC_STREAMS    0x0036

typedef struct _HCD_STREAM_INFO {
    PVOID PipeHandle;
    ULONG StreamID;
    ULONG MaximumTransferSize;
    ULONG PipeFlags;
} HCD_STREAM_INFO, *PHCD_STREAM_INFO;

typedef struct _HCD_URB_OPEN_STREAMS {
    /* _URB_HEADER, spelled out so this header needs no usbdi.h. */
    USHORT Length;
    USHORT Function;
    LONG Status;
    PVOID UsbdDeviceHandle;
    ULONG UsbdFlags;
    PVOID PipeHandle;
    ULONG NumberOfStreams;
    USHORT StreamInfoVersion;
    USHORT StreamInfoSize;
    PHCD_STREAM_INFO Streams;
} HCD_URB_OPEN_STREAMS, *PHCD_URB_OPEN_STREAMS;

/* The enumeration's DMA scratch (hcd_dma.c), one common buffer: the
 * thread's control transfers use its first 4 KB, the limit a configuration
 * descriptor had before Phase 27, and each hub's status-change report has
 * its own slice after them (hcd_hub.c), so serving hubs takes nothing from
 * that limit (reviewed and revised after 23e7715). */
#define HCD_SCRATCH_CONTROL_BYTES 4096UL
#define HCD_HUB_STATUS_BYTES    XHCI_HUB_STATUS_MAX_BYTES
#define HCD_SCRATCH_BYTES                                                    \
    (HCD_SCRATCH_CONTROL_BYTES + HCD_MAX_HUBS * HCD_HUB_STATUS_BYTES)

/* Hub ports remembered as deferred for the first answer (task 33.3). */
#define HCD_SETTLE_DEFER_HUB 16UL

/* ExSetTimerResolution, called only through a pointer: the 32-bit image
 * may not import it (it loads on Windows 98), and the 64-bit image asks
 * MmGetSystemRoutineAddress (design record 15 section 7.2.3). The Windows
 * 2000 DDK's declaration. */
typedef ULONG (NTAPI *HCD_SET_TIMER_RESOLUTION)(ULONG DesiredTime,
                                                BOOLEAN SetResolution);

/*
 * The timer thread's whole world (design record 15 section 7.2.5): one per
 * controller, in its extension, which outlives every stop. Initialised once
 * (HcdTmrInitObjects) and reset at each start under the controller lock,
 * before the poller opens. Under the controller lock: Live, Stop, Bar,
 * Active, Gen, ObsEpoch and ObsPhase (the producers write them; the thread
 * reads them in one hold per step) and the counters. The thread's own:
 * State, HeldGen, Looked, Calls, HeldSince. The start's, before the thread
 * runs: Switch, Desired, Routine, Lookable, DelayMs and the thread's object
 * or kept handle, which the stop's join reads after the thread has left.
 */
typedef struct _HCD_TMR {
    KEVENT Event;                   /* synchronization: an input changed  */
    PVOID ThreadObject;             /* referenced at the creation         */
    HANDLE ThreadHandle;            /* kept when the reference failed     */
    ULONG Created;                  /* a thread to join                   */
    HCD_SET_TIMER_RESOLUTION Routine;
    ULONG Lookable;                 /* 32-bit NT 5: the thread's own look */
    ULONG Looked;                   /* that look made in this start       */
    ULONG Switch;                   /* XhciPollFineTimer in effect        */
    ULONG Desired;                  /* XhciPollActiveMs x 10000           */
    ULONG DelayMs;                  /* XhciQemuTmrDelayMs, qemu only      */
    ULONG StartSerial;              /* this start's, for the observer     */
    volatile ULONG Live;            /* a thread was created this start    */
    ULONG Stop;
    ULONG Bar;
    ULONG Active;                   /* the poller's last decision ACTIVE  */
    ULONG Gen;                      /* the boundary generation            */
    volatile ULONG ObsEpoch;        /* odd: a call in flight              */
    ULONG ObsPhase;                 /* XHCI_POLL_TMR_* of the even epoch  */
    ULONG State;                    /* XHCI_POLL_TMR_RELEASED or _HELD    */
    ULONG HeldGen;
    ULONG Calls;                    /* the calls made in this start       */
    LARGE_INTEGER HeldSince;
    XHCI_POLL_TMR_OBS Obs;          /* the poll DPC's, under PollLock     */
} HCD_TMR, *PHCD_TMR;

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
    UCHAR IntShare;             /* the descriptor's ShareDisposition (mode 4) */
    /* The raw list's interrupt resource and the delivery it gives, kept
     * for the routing quirk (task 36.3), which names the IRQ from the raw
     * resource and never from the translated vector. */
    XHCI_PIRQ_IRQ IntRaw;
    ULONG IntDelivery;
    PKINTERRUPT Interrupt;
    KDPC IsrDpc;
    volatile LONG DpcsInFlight;
    volatile ULONG DpcClosed;
    /* 35-T.1's start generation: bumped by every start, resume and in-place
     * recovery, each under the power gate (or before the thread runs), and
     * read by the backstop under it. Never reset. */
    ULONG TolStartGen;
    ULONG ControllerStarted;
    ULONG ResumeFailures;
    ULONG PowerRequestFailures;
    DEVICE_POWER_STATE PowerDirectWant;
    ULONG SuspendedInD0;
    KEVENT PowerGate;
    ULONG WakesWithoutPower;
    /* The system IRP's deferral, and the device IRP's D0 apart from it:
     * the power manager lets one of each be in flight (hcd_power.c). */
    WORK_QUEUE_ITEM PowerWork;
    PIRP PowerWorkIrp;
    ULONG PowerWorkKind;
    WORK_QUEUE_ITEM PowerWorkD0;
    PIRP PowerWorkD0Irp;
    PIRP PendingSystemIrp;
#if defined(XHCI_FLAVOUR_QEMU)
    /* Record 15 section 8.1 (task 45.27, qemu only): power work items
     * deferred and whose work routine has not begun (interlocked). */
    LONG QemuPowerDeferred;
#endif

    /* The DMA adapter and the common buffer (hcd_dma.c). */
    PDMA_ADAPTER Dma;
    ULONG MapRegisters;
    ULONG MapGrantRaw;              /* IoGetDmaAdapter's grant, before any
                                     * qemu constraint (36.24's gate)     */
    PVOID CommonVa;
    PHYSICAL_ADDRESS CommonPa;
    ULONG CommonBytes;
    volatile ULONG CommonBufferPinned;
    ULONG CommonBuffersKept;
    /* Task 47.30 (design record 13 section 11.10, Part A): the larger event
     * ring's block, as allocated (freed by these), and the window in it the
     * ring uses, NULL and 0 when the start runs the in-block ring; set by
     * HcdDmaOpen, cleared by HcdDmaClose, read by HcdSvcEventRing. EvtWanted
     * is the value as the start read it (1 the larger ring, 0 the
     * in-block one) and EvtRoute how the start reached its ring
     * (HCD_EVT_ROUTE_*). Task 49.35 (record 13 section 11.10.1): EvtWanted
     * is XhciEventRingPick's answer, EvtPick the answer itself
     * (XHCI_EVT_PICK_*), EvtValueRead how XhciEventRingTrbs read
     * (XHCI_EVT_READ_*) and EvtHciVersion the HCIVERSION it was given
     * (0xFFFF unread). Outside the extension a start zeroes; not
     * serialized. */
    PVOID EvtBlockVa;
    PHYSICAL_ADDRESS EvtBlockPa;
    ULONG EvtBlockBytes;
    PVOID EvtRingVa;
    ULONG EvtRingPa;
    ULONG EvtWanted;
    ULONG EvtValueRaw;
    ULONG EvtRoute;
    ULONG EvtPick;
    ULONG EvtHciVersion;
    ULONG EvtValueRead;         /* XHCI_EVT_READ_*                 */

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
     * recurring arm must not be refused when the slots are full (batch
     * (c), reviewed and revised). FrameArmed under
     * TimerLock; the drain waits it to 0. */
    KTIMER FrameTimer;
    KDPC FrameDpc;
    volatile LONG FrameArmed;
    /* The tolerance clock (35-T, design record 17 section 4.0): a relative
     * 100 ms timer whose DPC advances Hc.Tol.Clock and arms it again, by
     * the frame sampler's rule - TolArmed under TimerLock, the drain waits
     * it to 0. */
    KTIMER TolTimer;
    KDPC TolDpc;
    volatile LONG TolArmed;

    /* The controller thread (hcd_ctl.c). */
    PVOID ThreadObject;
    PDEVICE_OBJECT RootHubPdo;
    KSPIN_LOCK PdoListLock;
    /* The firmware tables request (hcd_acpi.c, task 46.21; design record
     * 13 section 8.13): the set held (an HCD_ACPI_SET, NULL for none) and
     * the counts below under AcpiLock alone; AcpiGate lets one request at a
     * time read; AcpiTablesOn is XhciAcpiTables as the start read it. */
    KSPIN_LOCK AcpiLock;
    KEVENT AcpiGate;
    PVOID AcpiSet;
    ULONG AcpiGen;
    ULONG AcpiEpoch;
    ULONG AcpiClosed;       /* stopped: no request admitted until a start */
    ULONG AcpiTablesOn;
    PHCD_DEVICE_PDO DevicePdos;     /* listed: present, in the relations */
    PHCD_DEVICE_PDO GonePdos;       /* unlisted, awaiting their deletion */
    PHCD_DEVICE_PDO RemovedPdos;    /* removed by PnP, deleted at the next
                                     * BusRelations answer (hcd_pdo.c)    */
    ULONG StopPreserve;             /* an orderly PnP STOP is under way: the
                                     * drop keeps stopped PDOs dormant    */
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
    HCD_HOLD Holds[HCD_MAX_HOLDS];  /* 29-A.5 (hcd_enum.c)                */
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
    /* Windows 8's client contract (task 43.29, design record 13 section
     * 6.13.7): the streams count each new PDO latches (written by the
     * thread at hcdXportFlags). The stream URBs served and refused are
     * counted in Counters since task 49.45 (UsbdxStreamOpens and the two
     * beside it), so the USB Tree's report prints them. */
    ULONG UsbdxMaxStreams;

    /* DoneList (controller lock) holds the records the event DPC retired,
     * completed by the deferred work after the lock's release (hcd_io.c).
     *
     * Task 48.33 (design record 13 section 11.11; hcd_stage.c): staging.
     * StageLock is taken inside the controller lock, never the reverse, and
     * no copy runs under either. StageQueue holds the TDs waiting for
     * staging in arrival order, StageCopyQueue the IN copy-outs that could
     * not map. The shared pool's slabs (StageSlabCount of them, allocated
     * at the start), the control slab, the window MDLs. StageClosed: no
     * admission (set by an unproven stop and at every stop's drain).
     * StageOwners, StageDpcsInFlight and StageTimerSet: the processors'
     * staging work the stop joins (HcdStageDrain). */
    KSPIN_LOCK StageLock;
    LIST_ENTRY StageQueue;
    LIST_ENTRY StageCopyQueue;
    struct _HCD_STAGE_SLAB *StageSlabs;
    ULONG StageSlabCount;
    ULONG StageSlabsFree;
    PUCHAR StageCtlVa;
    PHYSICAL_ADDRESS StageCtlPa;
    ULONG StageCtlBusy;
    PMDL StageWin[HCD_STAGE_WINDOWS];
    ULONG StageWinBusy[HCD_STAGE_WINDOWS];
    ULONG StageNt;                  /* NT: the window maps may fail       */
    volatile LONG StageClosed;
    ULONG StagePassBusy;
    ULONG StagePassAgain;
    ULONG StagePassNo;
    volatile LONG StageOwners;
    volatile LONG StageDpcsInFlight;
    volatile LONG StageTimerSet;
    ULONG StageTimerArmed;
    ULONG StageRetryMs;
    KDPC StageDpc;
    KTIMER StageTimer;
    KDPC StageTimerDpc;
    /* XhciStagePoolKiB as the start read it (StagePoolFound 0: absent or
     * another type), and the qemu flavour's test switches. */
    ULONG StagePoolFound;
    ULONG StagePoolValue;
    /* XhciDmaStaging as the start read it (owner, 2026-11-10): 1 stages
     * every transfer (hcd_stage.c); 0, the default, maps the clients'
     * buffers through the map pump as 3.4.2.0 did, and a stop that cannot
     * prove the controller stopped keeps their requests (xhci_stage.c,
     * XhciStagingOn). Fixed for the start's life. */
    ULONG Staging;
    ULONG QemuStagePool;
    ULONG QemuStageFault;
    ULONG StageMapInject;
    /* XhciQemuStageMapProbe (qemu only): the bound of the one probe of how
     * many window mappings can be held at once, its own window MDLs (one a
     * mapping, StageProbeMdls of them) and the address each holds, and
     * whether it is still to run (record 13 section 11.11.4). */
    ULONG QemuMapProbe;
    PMDL *StageProbeMdl;
    PVOID *StageProbeVa;
    ULONG StageProbeMdls;
    volatile LONG StageProbeArmed;
    /* The readings (record 13 section 11.11.9), noted at the stop. */
    ULONG StageSlabsShort;
    ULONG StageSlabsHigh;
    ULONG StageNoReserve;
    ULONG StageWaits;
    ULONG StageControlWaits;
    ULONG StageReserveTakes;
    ULONG StageSmallTakes;
    ULONG StageSharedTakes;
    ULONG StageMapFails;
    ULONG StageGiveups;
    ULONG StageWindowMaps;
    ULONG StageCopyBytesLo;
    ULONG StageCopyBytesHi;
    ULONG StageCopyTicks;
    ULONG StageCopyLongest;
    ULONG StageTdBytesMax;
    ULONG StageTdElementsMax;
    ULONG StageForeignAddress;
    /* The qemu flavour's reading of the copies' sources (record 13 section
     * 11.11.11): copies through a URB's TransferBuffer, through an MDL's
     * MappedSystemVa, and those whose address lay below 0x80000000. */
    ULONG StageDirectBuffer;
    ULONG StageDirectMdl;
    ULONG StageDirectLow;
    ULONG IsoStageRefused;
#if defined(XHCI_FLAVOUR_QEMU)
    /* Task 49.29 (record 13 section 11.11.12), qemu only: the hold - the
     * state it keeps (XhciQemuStageFault bits 19:16, HCD_STAGE_HOLD_*), its
     * phase (HCD_STAGE_HOLD_IDLE, _HELD, _SPENT; StageLock), the record it
     * keeps, its polls and its timer - and the copies made for a record
     * already detached from its request (`stage.copy.detached`). */
    ULONG StageHoldPoint;
    ULONG StageHoldPhase;
    PHCD_XFER StageHoldX;
    ULONG StageHoldPolls;
    KTIMER StageHoldTimer;
    KDPC StageHoldDpc;
    ULONG StageCopyDetached;
    /* The DMA allocations this controller made, freed and kept, in number
     * and in pages (hcd_dma.c): made equals freed plus kept at the end of
     * HcdDmaClose. */
    ULONG QemuDmaMade;
    ULONG QemuDmaMadePages;
    ULONG QemuDmaFreed;
    ULONG QemuDmaFreedPages;
    ULONG QemuDmaKept;
    ULONG QemuDmaKeptPages;
#endif
    /* Task 48.33 (record 13 section 11.11.6, step 4): an unproven stop
     * released every child PDO's held requests (HcdIoParkCloseAll), and
     * none is held again until the next start; read under the cancel spin
     * lock by HcdIoPark. */
    volatile LONG ParkClosed;
    /* The map pump (hcd_dma.c, design record 13 section 11.3), run only
     * with XhciDmaStaging 0: records waiting for map registers, one
     * AllocateAdapterChannel outstanding at a time, restarted from MapDpc;
     * MapLock is innermost and is never held across a DMA_OPERATIONS
     * call. DoneList (controller lock) holds the records the event DPC
     * retired, completed by the deferred work after the lock's release
     * (hcd_io.c). */
    KSPIN_LOCK MapLock;
    LIST_ENTRY MapQueue;
    ULONG MapBusy;
    ULONG MapInCall;
    ULONG MapRefusals;
    /* Where the outstanding channel request is (HCD_MAP_PHASE_*, under
     * MapLock), and the quarantine of a request a fail-closed stop left
     * with the HAL (hcd_dma.c, HcdDmaMapDrain): sticky for the FDO's life,
     * so no start runs on it and its REMOVE keeps the device object. */
    ULONG MapPhase;
    ULONG MapQuarantined;
    ULONG MapsQuarantinedLate;      /* the HAL called a quarantined one  */
    KDPC MapDpc;
    KDPC MapDoneDpc;                /* the execution routine's release   */
    volatile LONG MapDpcsInFlight;
    ULONG MapsSynchronous;          /* execution routine ran inside the call */
    ULONG MapsDeferred;             /* ... or later                          */
    LIST_ENTRY DoneList;
    /* Stage 2 (task 36.24; hcd_dma.c, hcd_io.c). WideSwitch is XhciWideMap
     * as the start read it (HCD_WIDE_MAP_DEFAULT when absent); WideEnabled
     * is promotion on for this start - the switch, NT, the register-free
     * check passed and the pool allocated - and only then is WidePool
     * there. RefillHead lists the records whose next TD a completion pass
     * publishes once its walk is done. The probe records and their event
     * are the check's. Controller lock but the start's fields. */
    ULONG WideSwitch;
    ULONG WideEnabled;
    PHCD_WIDE_ENTRY WidePool;
    PXHCI_TRB WideScratch;          /* HCD_WIDE_SCRATCH_TRBS, after the
                                     * entries; NULL without a pool       */
    PHCD_XFER RefillHead;
    HCD_XFER WideProbe[2];
    KEVENT WideProbeEvent;
    volatile LONG WideProbeLeft;
    /* URBs that need commands (SELECT_CONFIGURATION, ABORT_PIPE,
     * RESET_PIPE) and IOCTL_INTERNAL_USB_RESET_PORT, which carries no URB,
     * pended and served by the thread (hcd_cfg.c); linked
     * through Tail.Overlay.ListEntry, the device in DriverContext[0];
     * controller lock. */
    LIST_ENTRY SlowIrps;
    ULONG CancelWork;               /* some pipe has CancelPending        */
    ULONG RetryWork;                /* some queue has RetryWanted (35-T.2,
                                     * HcdCfgRetryService)                */
    ULONG QemuStreamQuirk;          /* 36.54: QEMU's xHCI (1B36:000D), at
                                     * each start (record 17 4.12.4)      */
    struct _HCD_USB_DEVICE *StreamDeqRefused;
                                    /* issue 24: the device whose stream's
                                     * Set TR Dequeue the controller still
                                     * refused after the retries: its
                                     * failure cycles it, not the
                                     * controller (hcdCfgFault). Thread  */
    ULONG StreamDeqCycled;          /* issue 24: its cycle already queued */
    ULONG ThreadCommandDepth;       /* HcdThreadCommand calls in progress:
                                     * the resync runs at the outermost   */
    volatile LONG CancelsRunning;   /* cancel routines past the cancel lock */
    ULONG UrbsCompleted;
    ULONG UrbsGone;
    ULONG UrbsWaited;               /* parked on a pipe's Waiting list   */
    ULONG UrbsMdlShort;             /* length past the MDL: refused      */
    ULONG IsoStartsSia;             /* explicit StartFrame sent as SIA     */
    ULONG IsoStartsFirstFrameId;    /* no CFC: a flow's first TD's Frame ID */
    /* Design record 13 section 11.8 (task 43.39), under the controller lock:
     * the event budget's count of every open isochronous pipe at its
     * ring's capacity plus XHCI_ISO_EVENTS_OVER_TRBS (IsoCapOut), the
     * pages' telemetry (point
     * 10), and the one
     * scratch every isochronous publication builds into - TRBs and TD
     * lengths for the largest request a page takes. Not serialized. */
    ULONG IsoCapOut;
    ULONG IsoPagesBuilt;
    ULONG IsoPagesRefusedBudget;
    ULONG IsoPagesRefusedMemory;
    ULONG IsoPagesRetiredCount;
    ULONG IsoPageLargestTrbs;
    ULONG IsoPageLargestPackets;
    ULONG IsoBeiTds;
    /* Design record 13 section 10.5.2 (task 47.10): departed PDOs started
     * on 98 SE and ME, the requests their ghosts answered from the copies,
     * and the transfers they held. Interlocked; not serialized. */
    ULONG PdoGhostStarts;
    ULONG PdoGhostAnswers;
    ULONG PdoGhostHeld;
    /* URBs whose UrbHeader.Length stops short of the fields the bus reads
     * for their function, served all the same (hcd_urb.c, hcdSubmitUrb;
     * owner, 2026-11-10). Interlocked. */
    ULONG UrbsHeaderShort;
    XHCI_TRB IsoTrbs[XHCI_XFER_MAX_ISO_PAGE_TRBS];
    ULONG IsoTdLengths[XHCI_XFER_MAX_ISO_PAGE_PACKETS];
    ULONG WaitingKicks;            /* a waiter started by admission or a
                                     * cancel, not by a record's release  */
    ULONG DevicesKept;              /* freed with URBs left: DMA not stopped */
    ULONG Ep0Resets;
    ULONG EnumCommandsRefused;
    ULONG EnumCommandsTimedOut;
    ULONG EnumDisableFailures;
    ULONG EnumTransfersTimedOut;
    /* Of those, the ones whose device had left: no reset (task 43.53). */
    ULONG EnumTransfersDeparted;
    /* Task 36.4's busy retry (hcd_enum.c, hcdCommandBusy): commands that
     * read the engine not idle before their first submit or were answered
     * XHCI_CMD_BUSY, those then submitted, and the drains it queued.
     * Thread only. */
    ULONG EnumCommandsBusy;
    ULONG EnumCommandsBusyCleared;
    ULONG EnumCommandBusyDrains;
    /* Task 36.13: drains the thread queued while it waited for a
     * completion of its own, a command's or an EP0 transfer's (hcd_enum.c,
     * hcdThreadWaitDrain). Thread only. */
    ULONG EnumWaitDrains;
    /* Removal and the TT (27-A.3; hcd_enum.c, hcd_hub.c): endpoints
     * stopped so a leaving device's URBs complete before its slot goes,
     * hub ports given up after their attempts, CLEAR_TT_BUFFER sent and
     * refused. Thread only; outside the matrix's counter block. */
    ULONG TeardownStops;
    ULONG TeardownStopFailures;
    ULONG HubPortsGivenUp;
    ULONG SsHubPortsRearmed;        /* given-up SuperSpeed hub ports put
                                     * back to RxDetect (hcd_hub.c)       */
    ULONG HoldRecoverGiveUps;       /* devices left with no PDO after a
                                     * refused send-back's recreation
                                     * failed: at HCD_HOLD_RECOVER_TRIES,
                                     * or their re-enumeration, refused in
                                     * place, failing for good
                                     * (hcd_enum.c)                       */
    ULONG HubResumes;               /* hub ports resumed by the bus       */
    ULONG HubResumesFailed;         /* ... given up after their tries     */
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
    /* Task 45.41 (design record 13 section 15, L5): the cycle a function
     * driver asked after its own reset failed (IOCTL_XHCI98_RESUME_STATE,
     * XHCI98_RESUME_OP_CYCLE), charged to the location's re-enumeration
     * budget when the thread carries it out, as HcdCfgCycle charges a
     * failed RESET_PORT's. Cleared with PortCycle. Controller lock. */
    ULONG PortCycleCharge[HCD_PORT_WORDS];
    /* A composite FDO's start (HcdEnumRepresent, task 36.43): the let-go
     * function PDOs of the port's device re-presented in place, if it is
     * still the one that group serial stands for. */
    ULONG PortRepresent[HCD_PORT_WORDS];
    ULONG PortRepresentSerial[HCD_PORT_COUNT];
    /* Thread requests (hcd_enum.c), under the controller lock. */
    ULONG SlotsInvalidated;         /* HCRST took every slot              */
    /* An invalidation raised without proof the controller stopped (its
     * controllerStopped 0) and not yet followed by a completed HCRST
     * (XhciSlotInit): the drain waits for a proof (HcdEnumService). */
    ULONG SlotsUnproven;
    /* Task 45.23 (design record 20 section 4.3.2): devices kept across a
     * resume's re-initialization. ResumeInvalidating is set by the resume
     * around XhciResumeController (hcd_power.c); XhciSlotInvalidateAll
     * notes an invalidation it raises (ResumeSlotsInvalidated) and drops
     * any keep owed by an earlier one; ResumeKeepReady is set once that
     * resume succeeded - its HCRST done - and taken by the thread's
     * invalidation (HcdEnumService). Under the power gate. ResumeKeepOn is
     * XhciResumeKeep, read at the start (absent, or anything but a DWORD
     * 0, is on). The counts are the snapshot's log notes. */
    ULONG ResumeInvalidating;
    ULONG ResumeSlotsInvalidated;
    ULONG ResumeKeepReady;
    /* Set with ResumeKeepReady by the resume, cleared by the thread once
     * its pass has taken the invalidation (or by any other invalidation):
     * the D0 waits for it before it completes, so no function driver above
     * - started again only after its parent's D0 - submits to a device
     * the thread has not yet kept or dropped (hcd_power.c, hcdD0Finish). */
    volatile LONG ResumeKeepPending;
    ULONG ResumeKeepOn;
    ULONG ResumeKeepBot;            /* task 49.34: XhciResumeKeepBot's bits
                                     * (1 hibernation, 2 standby), at start */
    ULONG SuspendSleep;             /* the SYSTEM_POWER_STATE the last
                                     * suspend served (hcd_power.c)       */
    PMDL BotWin;                    /* task 49.34: the window over a
                                     * client's MDL a short Bulk-Only
                                     * answer is read through (hcd_stage.c,
                                     * HcdStageCopyHead), with the keep on */
    volatile LONG BotWinBusy;
#if defined(XHCI_FLAVOUR_QEMU)
    ULONG QemuBotDelayMs;           /* task 49.34's leg aid: the Bulk-Only
                                     * reopening delayed this long        */
#endif
    ULONG LinkKeepOn;               /* task 45.41: XhciLinkKeep, at start   */
    ULONG SsFlapFallback;           /* issue 29: XhciSsFlapFallback, at start */
    ULONG ResumeKept;               /* devices restored in place          */
    ULONG ResumeKeepRefused;        /* not kept, by XhciEnumResumeKeep    */
    ULONG ResumeKeepFailed;         /* kept, then dropped: the restore
                                     * failed                              */
    /* Task 45.23 (design record 20 section 4.3.3, IOCTL_XHCI98_RESUME_STATE
     * in hcd_urb.c): ResumePhase is XHCI98_RESUME_PENDING from the
     * controller's suspend (hcd_power.c, hcdSuspendSettled) to its resume's
     * end - in hcdResume when that failed (XHCI98_RESUME_FAILED) or did not
     * re-initialize, else at the end of the thread's invalidation pass
     * (HcdEnumService) - and MarkGen counts those ends, so a device's
     * QuietMark counts for one sleep only. Controller lock. */
    ULONG ResumePhase;
    ULONG MarkGen;
    /* The invalidation a terminal no recovery acts on raised for itself
     * (hcd_ctl.c, hcdTerminalRelease): once per lifetime. */
    ULONG TerminalReleased;
    /* A transfer was published on a controller not initialized since the
     * save gate armed (hcd_dev.c, XhciSlotSaveBusy; hcd_io.c,
     * HcdIoMapped): the saved image is not committed, or not restored. */
    ULONG SavePublished;
    ULONG EnumDetachRequested;      /* the root hub is going               */
    KEVENT EnumDetachDone;
    ULONG ScratchTainted;           /* a timed-out EP0 transfer may DMA    */
    /* 35-T.3 (design record 17 section 4.3): the slot whose EP0 transfer
     * the thread's wait abandoned for a cycle, 0 for none. The controller
     * may still own that TD and write the scratch, so no thread control
     * transfer goes out until that slot's Disable Slot completes or HCRST
     * takes it; a Disable Slot that fails sets ScratchTainted. Thread
     * only. ThreadEp0Dev: the device the thread's control transfer is
     * outstanding on, NULL between them, under the controller lock, so a
     * cycle mark for it can end the wait (HcdTolCycleMark). */
    ULONG ScratchHeld;
    struct _HCD_USB_DEVICE *ThreadEp0Dev;
    ULONG SlotSweep;                /* Abandoned records await Disable Slot */
    volatile ULONG CmdDonePA;       /* the completed command's TRB        */
    volatile ULONG PortEvents;

    /* Event delivery (hcd_poll.c; design record 15, roadmap-hcd.md 36.6
     * to 36.8). Values, delivery, reason and pin are settled at each start
     * (hcdStartBody) and kept outside the zeroed extension, which takes a
     * copy of the delivery. The poller: its timer and the KDPC only the
     * timer is given, PollLock (after the controller lock, never before),
     * the pure state and the interval observation under PollLock. The
     * period's inputs: PollPortsBusy, published by the thread under the
     * controller lock; PollLastEvents/PollLastEventTime, the service DPC's
     * own under the controller lock. PollContainAsked: the thread asked a
     * recovery to contain an owed retirement (record 15 section 4.2). */
    XHCI_POLL_VALUES PollValues;
    ULONG PollDelivery;
    ULONG PollReason;
    ULONG PollPinStatus;
    ULONG PollPin;
    KTIMER PollTimer;
    KDPC PollTimerDpc;
    KSPIN_LOCK PollLock;
    XHCI_POLL_STATE Poll;
    XHCI_POLL_OBS PollObs;
    ULONG PollPortsBusy;
    ULONG PollLastEvents;
    ULONG PollLastEventTime;
    ULONG PollEventSeen;
    ULONG PollContainAsked;
    ULONG PollOwedState;            /* XHCI_POLL_BUS_* of Poll.Owed; PollLock */

    /* Message-signalled interrupts on Vista and 7 (design record 15
     * section 4.3, task 36.15). The raw list's message descriptors and the
     * MessageCount the first carries (hcdParseResources); where the
     * IoConnectInterruptEx pair came from (HCD_MSI_ROUTINES_*) and the pair;
     * the connection IoConnectInterruptEx made - MsgVersion 0 for none,
     * else the CONNECT_* it returned - with its context, kept for
     * IoDisconnectInterruptEx at the release; the messages connected, the
     * capability read and the connection's status. Settled at each start,
     * outside the zeroed extension. */
    ULONG MsgDescriptors;
    ULONG MsgRawCount;
    ULONG MsgRoutines;
    PVOID MsgConnectEx;
    PVOID MsgDisconnectEx;
    ULONG MsgVersion;
    PVOID MsgContext;
    ULONG MsgConnected;
    ULONG MsgCapability;
    NTSTATUS MsgConnectStatus;
    ULONG QemuMsgFail;

    /* XhciInterruptMode 4 (design record 15 section 4.5, roadmap-usb4
     * task 49.24), settled at each start (hcd_msg4.c):
     * the system class (XHCI_MSG4_SYS_*), the values as read and the
     * claim they ask for; the connection - msi4nt's info block, kept
     * untyped so the header stays in its seam - and the object the
     * readiness flag's writes synchronise on (the KINTERRUPT, also
     * hc->Interrupt); the library's last status; the readiness flag
     * (Msg4Ready, written only under the ISR's exclusion); the bar on
     * re-enabling (Msg4Barred); the lost connection's cause (Msg4Lost,
     * HCD_MSG4_LOST_*, under the controller lock) and whether the
     * thread's half has run; the function's location; the removal mark
     * (a removal Windows reported); whether the stop proved the
     * controller halted; whether the start's No Op came by the message;
     * the stranded-event judge (controller lock) with the events drained
     * at its last look; strategy 1's stray run. */
    ULONG Msg4System;
    ULONG Msg4StrategyRead;
    ULONG Msg4IsaIrqRead;
    ULONG Msg4ShareRead;
    ULONG Msg4PoolsRead;
    ULONG Msg4InputFirstRead;
    ULONG Msg4InputLastRead;
    ULONG Msg4BudgetRead;
    ULONG Msg4Share;
    XHCI_MSG4_CLAIM Msg4Claim;
    PVOID Msg4Info;
    PVOID Msg4SyncObject;
    ULONG Msg4Status;
    volatile LONG Msg4Ready;
    ULONG Msg4Barred;
    LONG Msg4Lost;
    ULONG Msg4LostDone;
    ULONG Msg4QueryReleased;
    ULONG Msg4Bdf;
    ULONG Msg4BdfKnown;
    ULONG Msg4Removal;
    ULONG Msg4Gone;                 /* a removal seen: the flag is never
                                     * opened again this start; written
                                     * before the flag's clear, read under
                                     * the ISR's exclusion                */
    ULONG Msg4JudgeGen;             /* one more at every reset of the
                                     * judge, under the controller lock   */
    ULONG Msg4Halted;
    ULONG Msg4NoOpPassed;
    XHCI_MSG4_JUDGE Msg4Judge;
    ULONG Msg4JudgeEvents;
    ULONG Msg4StrayRun;
    /* NT 5 (hcd_msgnt.c): the ISA claim held apart from the connection;
     * whether the function implements Interrupt Disable (read after the
     * connect's arm); strategy 1's brake tripped by the routine for the
     * DPC to latch; the brake look owed and the tolerance tick of the last
     * one; the qemu flavour's XhciQemuMsg4Fail and the bit in force. */
    ULONG Msg4ClaimHeld;
    ULONG Msg4StrategyCache;    /* the filter's (hcd_msi86.c)            */
    ULONG Msg4IdImplemented;
    volatile LONG Msg4StrayTripped;
    volatile LONG Msg4LookOwed;
    ULONG Msg4LookAt;
    ULONG QemuMsg4Fail;
    volatile LONG Msg4InjectNow;

    /* The requirements filter (design record 18 section 4.2): the mode as
     * read at AddDevice and refreshed at each start (both system-thread
     * contexts), with the qemu switches beside it; the filter's record -
     * its sequence, the list's class and the outcome - with the mode it
     * used copied from that cache; and the last walk and classification.
     * PnP sends one PnP IRP at a time to a devnode, so the filter and the
     * start never run together. */
    ULONG ModeCacheRaw;
    ULONG ModeCacheFound;
    ULONG ModeCacheFrom;            /* HCD_MSI_FROM_*                      */
    ULONG QemuNoStrip;
    ULONG QemuInjNoLine;
    XHCI_MSI_FILTER_RECORD MsiFilter;
    ULONG FilterModeRaw;
    ULONG FilterModeFound;
    ULONG FilterModeFrom;
    ULONG FilterMode;
    XHCI_MSI_WALK FilterWalk;
    XHCI_MSI_CLASSIFY FilterClassify;
    ULONG FilterStripped;           /* descriptors removed by a REPLACED   */
    /* The last filter's received list, bounded (record 18 section 9):
     * HCD_MSI_SNAP_DESCRIPTORS descriptors of HCD_MSI_SNAP_WORDS words -
     * configuration << 16 | index, Option | Type << 8 | ShareDisposition
     * << 16, Flags, and the six words of u raw - and how many there were. */
    ULONG FilterSnapCount;
    ULONG FilterSnap[12][9];

    /* The x86 helper, xhcihlpr.sys (design record 18 sections 3, 4.1 and
     * 15.3 to 15.4): the binding, the last look's detail (its NTSTATUS, or
     * the channel test that refused it), the accepted answer - each
     * service's verdict (XHCI_HLPR_SVC_*) and its routines, NULL unless
     * that service was accepted - and the file object whose reference is
     * held while BOUND. The block is the query sent, in this nonpaged
     * extension. On Vista and 7 the filter and the start look, in the PnP
     * manager's serialisation; on Windows 2000 and XP only the timer
     * thread, between a start and its stop. */
    XHCI_HLPR_BIND HlprBind;
    NTSTATUS HlprStatus;
    ULONG HlprRefusal;
    XHCI_HLPR_QUERY HlprQuery;
    PFILE_OBJECT HlprFile;
    PVOID HlprConnectEx;
    PVOID HlprDisconnectEx;
    PVOID HlprSetTimer;
    ULONG HlprMsiVerdict;
    ULONG HlprTimerVerdict;
    ULONG HlprHelperVersion;
    ULONG HlprHelperMinor;
    ULONG HlprFlags;

    /* A finer system timer while polling (design record 15 section 7.2,
     * task 38.3; hcd_tmr.c). */
    HCD_TMR Tmr;

    /* Strict mode (hcd_strict.c): the command preconditions a conforming
     * xHC enforces and QEMU does not, checked before each command, in
     * every flavour since task 45.45 (design record 08 section 14).
     * StrictOn is XhciCommandCheck as the start read it (hcdReadValues),
     * latched for the started lifetime; the rest is the thread's alone.
     * StrictSlotEnabled is the half of the slot state the Output Slot
     * Context cannot say (Disabled and Enabled are both 0). The Seen maps
     * hold one note per (command with DW3 bit 9, slot and EP state) and
     * per (command, refusal code). The counters are the counter block's
     * Strict* words. */
    ULONG StrictOn;
    /* Task 49.39 (design record 17 section 4.12.8): XhciStreamDequeueRepair,
     * 1 the checks act (the default), 0 they note only. Latched at the
     * start. */
    ULONG StreamDeqRepair;
    ULONG StrictSlotEnabled[(XHCI_MAX_SLOTS + 1 + 31) / 32];
    ULONG StrictSeen[64][2];
    ULONG StrictRefusalSeen[64];
#if defined(XHCI_FLAVOUR_QEMU)
    /* The qemu flavour's test aids, never in a published image (design
     * record 08). QemuPsiE460 is task 35.4's speed-table override, latched
     * at each start (hcd_ctl.c). The rest is 35-T.9's injection
     * (hcd_inj.c): InjLock is a leaf lock, taken at <= DISPATCH_LEVEL only,
     * over InjRegs; the lost-interrupt window is lock-free - the thread
     * arms and disarms it (InjIrqArmed, InjIrqBudget, InjIrqForever), the
     * ISR counts InjIrqTaken only while it is armed, and InjIrqDropped;
     * InjTrigger, InjReadAt and InjIrqNoted are the thread's. The endpoint
     * faults (35-T.2 to 35-T.4): InjEp under InjLock; InjEvDone signalled
     * by the drain that took an injected event; the rest the thread's -
     * the armed fault, its count and port, the phase of the injection in
     * flight and since when, its target (InjDev compared, never followed
     * once the slot no longer names it), and InjOwnCmd across the layer's
     * own commands, which its command hook lets through. */
    ULONG QemuPsiE460;
    /* Task 36.22: XhciQemuMapGrant as the start read it, 0 none; applied
     * to MapRegisters once the adapter has given its grant (hcd_ctl.c). */
    ULONG QemuMapGrant;
    /* Task 36.25: XhciQemuBigRefuse as the start read it - 1 refuses every
     * big stream block, forcing 11.7's fallback for a leg. */
    ULONG QemuBigRefuse;
    /* Record 13 section 11.8, point 2: XhciQemuIsoRing as the start read
     * it - 1 makes every non-zero-bandwidth isochronous pipe a candidate
     * for a ring page, so QEMU's Full Speed audio reaches one in a leg. */
    ULONG QemuIsoRing;
    /* Record 13 section 11.10, point 9: XhciQemuEventRing as the start
     * read it, before HcdDmaOpen (HCD_EVT_QEMU_*). */
    ULONG QemuEventRing;
    /* Record 15 section 7.2.12, leg T3: XhciQemuSuspHoldDx as the start
     * read it - 1 keeps the selective-suspend executor from claiming a
     * child's pended Dx, so a stop can meet it still pended. */
    ULONG QemuSuspHoldDx;
    /* Task 36.4: XhciQemuStartHold's end in tolerance ticks, latched at
     * each start (HcdInjStart), 0 none; ended early by HcdInjStartRelease. */
    volatile LONG InjStartHoldTicks;
    /* Record 15 section 11's switches, latched at each start: no interrupt
     * resource and pin 0; the HCD's pin read failing; and the dead line,
     * N + 1 in the value (0 off), latched N ms after the start's grant (at
     * once for N = 0, before the connection), from when the ISR queues no
     * drain and its claims are withheld from the watcher (QemuDeadClaims)
     * and every look reads asserted. */
    ULONG QemuNoInterrupt;
    ULONG QemuPinUnreadable;
    ULONG QemuDeadLine;
    volatile ULONG QemuDeadLatched;
    ULONG QemuDeadAt;
    volatile LONG QemuDeadClaims;
    KSPIN_LOCK InjLock;
    XHCI_INJ_REGS InjRegs;
    XHCI_INJ_TRIGGER InjTrigger;
    ULONG InjReadAt;
    volatile LONG InjIrqArmed;
    volatile LONG InjIrqTaken;
    volatile LONG InjIrqBudget;
    volatile LONG InjIrqForever;
    volatile LONG InjIrqDropped;
    LONG InjIrqNoted;
    XHCI_INJ_EP InjEp;
    KEVENT InjEvDone;
    ULONG InjFault;
    ULONG InjLeft;
    ULONG InjPort;
    ULONG InjPhase;
    ULONG InjSince;
    ULONG InjWaitNoted;
    struct _HCD_USB_DEVICE *InjDev;
    ULONG InjSlot;
    ULONG InjDci;
    ULONG InjOwnCmd;
    /* Task 36.12: fault 13's answer for the next device clear, HCD_CTL_*
     * plus 1, 0 none; the thread's (hcd_inj.c, HcdInjClearHalt). */
    ULONG InjClearHalt;
    /* Task 38.10: faults 18 and 19's answers, their argument plus 1, 0
     * none; the thread's (hcd_inj.c, HcdInjSetInterface and
     * HcdInjSelectConfigure). */
    ULONG InjSetInterface;
    ULONG InjSelectConfig;
    /* Issue 26: fault 1A's Stops still to answer, and the endpoint whose
     * next context read answers Running (Reads 1, 0 none). InjLock. */
    ULONG InjStopRace;
    ULONG InjStopRaceSlot;
    ULONG InjStopRaceDci;
    ULONG InjStopRaceReads;
    /* Task 49.35: fault 1B's slow reset - armed, the hold each HCRST takes
     * (ms, 0xFFFFFFFF for ever), CNR held instead of HCRST, and the hold
     * of the last HCRST written: running, and when (KeQuerySystemTime's
     * low part). InjLock. */
    ULONG InjResetArmed;
    ULONG InjResetHoldMs;
    ULONG InjResetCnr;
    ULONG InjResetHolding;
    ULONG InjResetStamp;
    /* Task 49.39: fault 1D's sends still to alter, and the cancel's stale
     * position while it sends (Slot 0 none); fault 1E's doorbells still to
     * ring. InjLock. */
    ULONG InjStaleDeq;
    ULONG InjStaleSlot;
    ULONG InjStaleDci;
    ULONG InjStaleStream;
    ULONG InjStalePa;
    ULONG InjStaleDcs;
    ULONG InjRestartRace;
    ULONG InjRepairRace;            /* task 49.39: fault 1F's doorbells */
    ULONG InjBulkStream;            /* 36.22: the held TD's stream, 0 none */
    /* BULK_TIMEOUT: armed to lose the target endpoint's
     * next TD's completion events; once one is lost, every event inside
     * that TD's TRBs. Controller lock (the drain's hook sets them). */
    ULONG InjBulkLose;
    ULONG InjBulkLost;
    ULONG InjBulkLostFirst;
    ULONG InjBulkLostToken;
    ULONG InjBulkLostEvents;
#endif
    /* The door (hcd_door.c, 26-A.8; design record 13 section 8): the
     * FDO's name's number, its \DosDevices name as made (HCD<n> on 98 SE
     * and ME, XHCI98HC<k> on NT; roadmap-usb4 44.81) and its HCD<m> alias
     * on 2000, XP and XP x64, the host controller
     * interface, and the root hub's name GET_ROOT_HUB_NAME returns (under
     * PdoListLock). DoorGate is a synchronization event used as a mutex:
     * held by a start, a stop and a door request that reads the extension
     * or the registers, so a request never meets a half-built extension or
     * an unmapped BAR. Outside Hc, which every start zeroes. */
    ULONG FdoSerial;
    ULONG HcdIndex;
    ULONG HcdLinkMade;
    WCHAR HcdLinkName[40];
    ULONG HcdLinkChars;
    WCHAR HcdAliasName[40];
    ULONG HcdAliasChars;
    ULONG HcdAliasIndex;
    ULONG HcdAliasMade;
    UNICODE_STRING HcInterface;
    ULONG HcInterfaceOn;
    WCHAR RootHubName[24];
    ULONG RootHubNameChars;
    KEVENT DoorGate;
    ULONG DoorRequests;
    ULONG DoorPassThru;
    /* Task 36.45: GET_DESCRIPTOR_FROM_NODE_CONNECTION requests the cache
     * cannot answer, pended for the thread (hcd_door.c); linked through
     * Tail.Overlay.ListEntry, the device record referenced in
     * DriverContext[0], the door FDO's I/O count and its event in [1] and
     * [2]. Open while the thread runs. Controller lock. */
    LIST_ENTRY DoorIrps;
    ULONG DoorQueueOpen;
    ULONG DoorForwarded;            /* sent to the device                  */
    ULONG DoorForwardFailed;        /* ... and not answered with success   */
    ULONG DoorWithdrawn;            /* past the deadline, taken off EP0    */
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
    /* Each refused device once (31-A.3), by where it sits
     * (XHCI_XPORT_AT_*), and the companion-port requests the 29-A.5 hold
     * did not take - all of them until its executor is wired. */
    ULONG XportRefusedAt[XHCI_XPORT_AT_COUNT];
    ULONG XportHoldsNotTaken;
    /* The serial-number instance ids (33.2; hcd_enum.c, hcd_pdo.c), by
     * the controller thread: devices named by their serial, devices whose
     * serial string was read but is no instance id, devices whose read
     * failed every try, and devices whose serial a listed PDO of the same
     * VID and PID already carried - each of the last three on the
     * location form. Never zeroed. */
    ULONG SerialIdsTaken;
    ULONG SerialIdsRefused;
    ULONG SerialReadsFailed;
    ULONG SerialIdsDuplicate;
    /* Task 45.40: arrivals deferred for a failed serial read at a port that
     * remembers their serial form, those a later read published, and
     * those whose reads were all spent. Never zeroed; thread only. */
    ULONG SerialDeferrals;
    ULONG SerialDeferOk;
    ULONG SerialDeferFailed;
    /* Task 45.40, behind hubs: each place's identity record, by its
     * instance key (XhciFuncIdPlaceTake), and the table's clock. Kept for
     * the controller's life, across a hub's removal and re-enumeration.
     * Thread only; the door reads it under the controller lock. */
    XHCI_ID_PLACE IdPlaces[HCD_ID_PLACES];
    ULONG IdPlaceClock;
    /* Fast polling (33.8; hcd_cfg.c, XhciPipeFastPoll): the
     * XhciFastPollFsLs mode read at each start (XHCI_PIPE_FAST_*), the
     * endpoints a select opened below Table 6-12's FS/LS range, and the
     * Configure Endpoint commands that refused one and were issued again at
     * the Table 6-12 Interval. The two counts are never zeroed. */
    ULONG FastPollMode;
    ULONG FastPollOpened;
    ULONG FastPollFallbacks;
    /* Task 42-A.17 (hcd_ctl.c hcdReadDword): how many of the start's
     * values were taken from the machine-wide key, zeroed before the
     * delivery's first read and noted as reg.machine after the quirks
     * and the interrupt route. */
    ULONG RegMachineWon;
    /* The driver's controllers, for the machine-wide serial check
     * (hcd_pdo.c, hcdSerialLock). */
    struct _HCD_CONTROLLER *SerialNext;

    /* The kept controller sequence's state, as the miniport's extension. */
    XHCI_EXTENSION Hc;

    /* The first answer's settle (task 33.3; hcd_enum.c, design record 13
     * section 5.7), after Hc so no offset the harness reads moves. A hub
     * FDO's start asks (SettleAsked, a generation); the thread settles a
     * generation at the end of a pass begun after it with nothing in
     * flight (SettleDone); a waiter whose deadline passes settles its own
     * (SettleDone only moves forward). Both under the controller lock.
     * SettleCapMs is the deadline and SettlePortMs the per-port budget,
     * from XhciFirstEnumWaitMs and XhciFirstEnumPortMs at each start.
     * The rest are counts, never zeroed: answers that waited, answers
     * whose deadline passed, answers cut short by a teardown, ports
     * deferred, and the last wait in ms. SettleDeferHub holds the hub ports
     * deferred, by physical path, so a recovery that rebuilds their hub
     * keeps them deferred (thread only). */
    ULONG SettleAsked;
    ULONG SettleDone;
    ULONG SettleCapMs;
    ULONG SettlePortMs;
    ULONG SettleWaits;
    ULONG SettleTimeouts;
    ULONG SettleAborts;
    ULONG SettleDeferrals;
    ULONG SettleLastMs;
    ULONG SettleDeferredNow;        /* a port deferred; thread only       */
    /* A root link's warm reset still in progress past this is the
     * controller's failure (hcd_enum.c): armed by the thread alone and
     * cancelled as it leaves (hcd_ctl.c). Initialised at AddDevice. */
    KTIMER LinkRecoverTimer;
    struct {
        ULONG RootPort;             /* 0: the entry is free               */
        ULONG Route;                /* the hub's own Route String         */
        ULONG Number;               /* the port on that hub               */
    } SettleDeferHub[HCD_SETTLE_DEFER_HUB];
    ULONG SettleDeferHubFull;       /* a deferral did not fit: every hub
                                     * port rebuilt is taken as deferred */
    /* The Intel port switchover (tasks 34.3 and 35.5; hcd_ctl.c,
     * xhci_psw.h, design record 16 section 7a). The counts are never
     * zeroed. PswLife.On is set by a start that decided to route, so each
     * return to D0 routes again; PswLife.Written is what those routes
     * wrote, which the stop, a refused start and a shutdown's D3 hand
     * back, and the stop and a refused start clear both. PASSIVE_LEVEL
     * only. */
    ULONG PswRoutes;
    ULONG PswReleases;
    ULONG PswFailures;
    XHCI_PSW_LIFE PswLife;
    /* The interrupt routing quirk (task 36.3; hcd_ctl.c, xhci_pirq.h): the
     * start's value and the last run's findings, with counts never zeroed,
     * published whole in the snapshot's HCD region; and the RCBA page a run
     * maps, NULL outside one. PASSIVE_LEVEL only, at the start under the
     * door gate and at a return to D0 under the power gate. */
    XHCI_PIRQ_STATE Pirq;
    PVOID PirqVa;
    /* The chipset-routed GSI offer (task 47.33, part 2; hcd_ctl.c,
     * xhci_pirq.h): XhciIntelNoRouteIrq as AddDevice read it, the offer
     * AddDevice's run made (on 2000, XP and XP x64), whether a filter
     * wrote it into the requirements (1 offered), the last start's
     * classification of its grant (HCD_NOROUTE_GRANT_*), and whether
     * Windows 2000's filter takes the interrupt out of the list (1; record
     * 18 section 16.4). The qemu flavour's injections beside them.
     * PASSIVE_LEVEL, AddDevice, the filter and the start, each a PnP
     * system-thread context. */
    ULONG NoRouteFound;
    ULONG NoRouteValue;
    XHCI_PIRQ_OFFER NoRoute;
    ULONG NoRouteOffered;
    ULONG NoRouteGrant;
    ULONG QemuNoRouteGsi;       /* XHCI_PIRQ_NO_GSI unless injected       */
    ULONG QemuLineConnFail;
    ULONG NoRouteStrip;
    ULONG QemuNoRouteListed;    /* 1: QEMU's controller taken as listed;
                                 * 2: the same, its run ended at a failed
                                 * mapping                                */
    ULONG QemuStripFail;        /* 1 no memory, 2 a refused shape, 3 a
                                 * refused walk                           */
    /* Selective suspend (design record 14; hcd_susp.c). SuspOn: the
     * switch, XhciSelectiveSuspend, read at start (36.33; 0 until then).
     * PowerEpoch and PowerDown: the controller's session and its leaving
     * D0 (section 4.4), under the controller lock. WakeSeq: the WAIT_WAKE
     * stamp counter (5.3), under the controller lock. SuspClockMs: the
     * machine's millisecond count, advanced by the executor's own waits
     * (section 3.10); thread only. */
    ULONG SuspOn;
    ULONG PowerEpoch;
    ULONG PowerDown;
    ULONG WakeSeq;
    ULONG SuspClockMs;
    /* The pass's copy of one device's slots, kept here rather than on the
     * enum thread's stack (16 functions, XHCI_FUNC_MAX); thread only, one
     * device at a time. */
    struct _HCD_DEVICE_PDO *SuspPdos[XHCI_FUNC_MAX];
    XHCI_SUSP_FUNC SuspFuncs[XHCI_FUNC_MAX];
    XHCI_SUSP_IDLE_FUNC SuspIdle[XHCI_FUNC_MAX];
    /* The idle notification (section 6, 36.32): the callback queue of PDO
     * entries (IdleEntry), under the controller lock; the callback
     * thread's context, its own allocation (4.4), set by the start while
     * the switch is on and cleared by the stop's handoff. Its counts are
     * in Counters (36.33). */
    LIST_ENTRY IdleQueue;
    struct _HCD_SUSP_CB *SuspCb;
    /* 36.33's bounded log lines: notes taken this start (hcdSuspNote), and
     * the port of the callback just claimed, noted outside the lock. */
    volatile LONG SuspNotes;
    ULONG SuspNoteCallPort;
    /* Issue 18: XhciUsb2HubSpeedFallback as the start latched it (1 on),
     * and the behind-hub speed notes taken this start (HcdHubPlace, the
     * thread's), bounded by HCD_HUB_SPEED_NOTE_BUDGET. */
    ULONG HubSpeedFallback;
    ULONG HubSpeedNotes;
    /* Task 43.27 (design record 13 section 8.4.1): XhciUsb2Bos as the
     * start latched it, 1 when a High or Full Speed device declaring
     * bcdUSB 0210h or more is asked for its BOS. */
    ULONG Usb2Bos;
    /* The devices whose read did not end cleanly (xhci_enum.h), kept for
     * this start: a controller recovery does not clear it. */
    XHCI_ENUM_BOS_MEMO Usb2BosMemo;
    /* The thread's claimed IRPs, completed by a pool of parked workers
     * (hcd_susp.c, hcdSuspDefer; record 14 section 3.12): the workers'
     * contexts and the pending IRPs, under SuspDoneLock; provisioned by
     * the start, grown by the grower only when every one is held, joined
     * by the stop.
     * Initialized by each start. */
    KSPIN_LOCK SuspDoneLock;
    LIST_ENTRY SuspDoneList;        /* every worker                      */
    LIST_ENTRY SuspDonePending;     /* claimed IRPs awaiting a worker     */
    ULONG SuspDoneCount;            /* workers on SuspDoneList            */
    ULONG SuspDoneIdle;             /* of them, parked                    */
    ULONG SuspDoneStop;             /* the stop: leave once none pending  */
    ULONG SuspDoneStalled;          /* the grower's stall watch ...       */
    ULONG SuspDoneStallSince;       /* ... from (system time low word)    */
    struct _HCD_SUSP_DONE *SuspGrower;  /* the thread that makes workers  */
    ULONG SuspGrowStop;
    volatile LONG SuspDoneHolds;    /* settles between claim and hand-off */
    volatile LONG SuspDoneClosed;   /* the stop's drain: no more of them  */
    /* The transfer size (tasks 36.21 and 36.22; xhci_xstat.h, hcd_io.c):
     * the readings since the start, zeroed and the value latched by each
     * start (hcd_ctl.c), and the class drivers' records (hcd_urb.c), kept
     * across starts. Controller lock, but CommandBytes, which only a start
     * writes, before any device PDO can ask. */
    XHCI_XSTAT Xstat;
    XHCI_XPIPE_STATS Xpipe;         /* 36.24; controller lock            */
    HCD_UAS_TELE UasTele[HCD_UAS_TELE_RECORDS];
    /* Task 49.39 (design record 17 section 4.12.9): the stream trace, live
     * and the last capture, and the capture's number, reason, time and the
     * live head at it. Controller lock. */
    ULONG TraceHead;
    ULONG Trace[XHCI_SNAPSHOT_TRACE_ENTRIES][XHCI_SNAPSHOT_TRACE_ENTRY_WORDS];
    ULONG TraceCap[XHCI_SNAPSHOT_TRACE_ENTRIES]
                  [XHCI_SNAPSHOT_TRACE_ENTRY_WORDS];
    ULONG TraceCapSeq;
    ULONG TraceCapWhy;
    ULONG TraceCapAt;
    ULONG TraceCapHead;
    /* The shutdown latch (roadmap-hcd 37.3, design record 13 section 5.8):
     * set in the controller-lock hold that sets PowerDown at a D3 whose
     * system state is PowerSystemShutdown, before the halt, and cleared by
     * a D0; read under the controller lock by every admission that would
     * otherwise leave a request waiting for a D0 (hcd_io.c, hcd_cfg.c).
     * Task 45.63 (record 13 section 5.8.1): set too by a D0 the bus below
     * failed (hcd_power.c, hcdD0Refused), and cleared by the next D0. */
    ULONG ShutLatched;
    /* Task 45.63: USBSTS read HCHalted on a decoding window right after a
     * suspend's halt, with the function still in D0 (hcdSuspendSettled);
     * cleared by every resume. The transfer flush's proof when a D0 the
     * bus fails leaves the function unreadable. Power gate. */
    ULONG SuspendHaltProven;
    ULONG D0Refusals;               /* D0s the bus below failed            */
    /* The Windows 98 SE and ME silent install (task 38.2; design record 13
     * section 6.11.3): the gate XhciSilentInstall (0 or 1), the
     * SilentClasses table and the query's status, written by each start's
     * hcdReadSilent while no controller thread exists, read with no lock
     * by the thread's hcdSilentDecide. On NT the gate stays 0 and the table
     * empty. Outside Hc, so the snapshot's dump layout is untouched. */
    ULONG SilentGate;
    NTSTATUS SilentStatus;
    XHCI_SILENT Silent;
    /* Linux's quirks (task 38.10, design record 20; xhci_quirk.h). The
     * controller's PCI identity as this FDO last read it - at AddDevice
     * after the attach on NT 6, and before each start's delivery is
     * settled - kept for the FDO's lifetime, since the device does not
     * change within it (section 4.1); and XhciMsiAvoid as read with the
     * mode's cache, for the requirements filter, which reads no registry.
     * PnP sends one PnP IRP at a time, so the filter and the start never
     * run together. The rest of the quirks' state is the extension's
     * (Hc.Quirk), zeroed with it at each start. */
    ULONG QuirkIdentity;
    ULONG QuirkIdentityValid;
    ULONG MsiAvoidCacheRaw;
    ULONG MsiAvoidCacheFound;       /* XHCI_QK_FOUND_*                     */
    ULONG MsiAvoidFilterStrips;     /* message descriptors the last filter
                                     * stripped at XhciMsiAvoid's asking  */
    /* The shutdown's log flush (section 6.4): the D3 path advances Wanted
     * and waits, bounded, for the thread to publish it as Acked after an
     * HcdLogFlush it began after capturing it; Sink is whether that flush
     * had the DebugView sink on. ShutFlushDone is set by the thread after
     * each publication. */
    LONG ShutFlushWanted;
    LONG ShutFlushAcked;
    ULONG ShutFlushSink;
    KEVENT ShutFlushDone;

    /* Record 17 section 4.13.6 (task 45.35): xhcicm.sys's isolation
     * statement for a controller behind a USB4 tunnel, taken at the start
     * before the thread runs and released after the thread is joined
     * (hcdIsoAcquire, HcdCtlIsoRelease); the thread's alone in between.
     * IsoIsolated NULL when none is held. */
    PVOID IsoContext;
    PINTERFACE_DEREFERENCE IsoDeref;
    ULONG (*IsoIsolated)(PVOID Context);
    ULONG IsoHeldStarts;    /* starts that took the interface              */
    ULONG IsoAsks;          /* the statement asked (hcdContainProof)       */
    ULONG IsoProofs;        /* ... and it was the proof                    */
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
    /* Task 33.3: the first BusRelations answer since the start waits for
     * the settle generation the start asked for. */
    ULONG SettlePending;
    ULONG SettleTarget;
} HCD_ROOTHUB_FDO, *PHCD_ROOTHUB_FDO;

/* An external hub's FDO, this driver's third role (task 33.4; design record
 * 13 section 10.11), attached over a hub's device PDO by AddDevice. Its
 * BusRelations are the PDOs presented under that hub; it reaches the
 * controller only through the PDO (HcdHubFdoController), never by a copy.
 * The same extension serves a composite device's FDO, the fourth role
 * (task 36.43, section 10.12; Common.Kind HCD_KIND_COMPOSITE_FDO): its
 * BusRelations are the device's function PDOs, and it has no door. */
typedef struct _HCD_HUB_FDO {
    HCD_COMMON Common;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    LONG OutstandingIo;
    KEVENT RemoveEvent;
    /* The door (hcd_door.c): \DosDevices\XHCI98HUB<serial> and the hub
     * interface. */
    ULONG LinkMade;
    UNICODE_STRING Interface;
    ULONG InterfaceOn;
    /* Hub IOCTLs admitted: opened by a START that succeeded, closed
     * before a STOP or SURPRISE_REMOVAL goes down, so none can keep the
     * PDO's Busy raised while its quiesce waits for it. */
    volatile LONG DoorOpen;
    /* Task 33.3: the first BusRelations answer since the start waits for
     * the settle generation the start asked for. */
    ULONG SettlePending;
    ULONG SettleTarget;
    /* System power (design record 13 section 10.12): the system IRP held
     * while the device IRP it asked for runs, and the work item that
     * completes it at PASSIVE_LEVEL when that IRP finished above it. One
     * system IRP at a time - the power manager serializes them. One item
     * is enough, unlike the controller's pair (hcdPowerDefer): a device
     * D0 that overlaps the system IRP completes in its own completion
     * routine (hcdHubD0Done), which needs no PASSIVE_LEVEL, so only the
     * system IRP ever queues it. */
    PIRP PendingSystemIrp;
    WORK_QUEUE_ITEM PowerWork;
} HCD_HUB_FDO, *PHCD_HUB_FDO;

#define HcdControllerFromExt(ext) \
    CONTAINING_RECORD((ext), HCD_CONTROLLER, Hc)

/* IoSetCompletionRoutine without the ASSERT the checked DDK wraps it in,
 * which would import RtlAssert into the checked qemu build - a pair with
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
NTSTATUS HcdRootHubFdoSystemControl(PHCD_ROOTHUB_FDO fdo, PIRP irp);

/* hcd_entry.c */
extern PDRIVER_OBJECT HcdDriverObject;
ULONG HcdIoEnter(PHCD_CONTROLLER hc);
VOID HcdIoLeave(PHCD_CONTROLLER hc);
NTSTATUS HcdPassDown(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);
#if defined(XHCI_FLAVOUR_QEMU)
/* Task 49.29 (record 13 section 11.11.12), qemu only: the per-request log
 * of the URB IRPs sent to a device PDO (hcd_urb.c), on while any start
 * has read XhciQemuStageFault bit 3, and its two totals. */
extern volatile LONG HcdQemuUrbLog;
extern volatile LONG HcdQemuUrbsIn;
extern volatile LONG HcdQemuUrbsOut;
VOID HcdQemuUrbIn(PIRP irp);
VOID HcdQemuUrbOut(PIRP irp);
#define HCD_QEMU_URB_OUT(irp) HcdQemuUrbOut(irp)
#else
#define HCD_QEMU_URB_OUT(irp)
#endif

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
ULONG HcdCtlProveDmaStopped(PXHCI_EXTENSION ext);
/* Record 17 section 4.13.6: the isolation statement's interface released,
 * idempotently, once the controller thread is joined (or never ran).
 * IRQL: PASSIVE_LEVEL. */
VOID HcdCtlIsoRelease(PHCD_CONTROLLER hc);
VOID HcdPowerGateEnter(PHCD_CONTROLLER hc);
VOID HcdPowerGateLeave(PHCD_CONTROLLER hc);
ULONG HcdCtlForceBulkOnly(PHCD_CONTROLLER hc);
ULONG HcdCtlBulkOnlyOwnId(PHCD_CONTROLLER hc);
ULONG HcdDrainPending(PHCD_CONTROLLER hc);
ULONG HcdCommandDrain(PHCD_CONTROLLER hc);
VOID HcdPswRoute(PHCD_CONTROLLER hc);
VOID HcdPswRelease(PHCD_CONTROLLER hc);
VOID HcdPirqResume(PHCD_CONTROLLER hc);
VOID HcdPirqStartAttempt(PHCD_CONTROLLER hc);
VOID HcdNoRouteAddDevice(PHCD_CONTROLLER hc);

/* The start's classification of the grant against the offer (task
 * 47.33, part 2; the note noroute.grant). */
#define HCD_NOROUTE_GRANT_NONE      0UL     /* memory alone                */
#define HCD_NOROUTE_GRANT_OFFERED   1UL     /* the offered GSI             */
#define HCD_NOROUTE_GRANT_OTHER     2UL     /* another line (the lower
                                             * configuration's, mostly)   */
ULONG HcdCtlDrainAdmitted(PHCD_CONTROLLER hc);

/* hcd_poll.c (design record 15) */
VOID HcdPollInitObjects(PHCD_CONTROLLER hc);
VOID HcdPollOpen(PHCD_CONTROLLER hc, ULONG role);
VOID HcdPollGrant(PHCD_CONTROLLER hc);
VOID HcdPollRevoke(PHCD_CONTROLLER hc);
VOID HcdPollClose(PHCD_CONTROLLER hc);
ULONG HcdPollContain(PHCD_CONTROLLER hc);
VOID HcdPollPeekLocked(PHCD_CONTROLLER hc);
VOID HcdPollPromoteLocked(PHCD_CONTROLLER hc, ULONG needMs);
VOID HcdPollPortsBusy(PHCD_CONTROLLER hc, ULONG busy);
VOID HcdPollThreadPass(PHCD_CONTROLLER hc);

/* hcd_enum.c */
VOID HcdEnumService(PHCD_CONTROLLER hc, ULONG powered);
VOID HcdEnumDetach(PHCD_CONTROLLER hc);
VOID HcdEnumContain(PHCD_CONTROLLER hc, ULONG proof);
/* 35-T.5's location budget, a root port's or a hub port's (thread only):
 * the charge for one action of kind XHCI_TOL_CHARGE_* - 1 to act, 0 when
 * the budget is spent and the location now held, and 1 uncharged at
 * XhciTolerance 0 - and one look at the port. 35-T.3 and 35-T.4's device
 * cycle charges its re-enumeration through HcdTolLocCharge. */
ULONG HcdTolLocCharge(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG kind);
/*
 * 35-T.3 and 35-T.4 (design record 17 section 4.3): mark the device to be
 * cycled for reason (XHCI_TOL_CYCLE_REFUSED_CODE, or _HALT_NO_TD on endpoint
 * dci, which the thread confirms by reading the endpoint's context), with
 * its location's connect generation, and wake the thread - and, when the
 * thread is waiting on its own control transfer to that device, end the
 * wait. The one entry point every mark goes through, the event path's and
 * any injection's. Nothing at XhciTolerance 0. IRQL: DISPATCH_LEVEL,
 * controller lock held.
 */
VOID HcdTolCycleMark(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG reason,
                     ULONG dci);
/* The device's mark read as the cycle service reads it - each halt with no
 * TD confirmed by its endpoint's context (XhciTolHaltOwner), a stale one
 * cleared - and the reason it is to be cycled for, XHCI_TOL_CYCLE_NONE for
 * none; a reason stays marked for the cycle service. Thread only. */
ULONG HcdTolCycleResolve(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdTolLocObserve(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG connected,
                      ULONG powered, ULONG changed);
VOID HcdTolLocRecovery(PHCD_CONTROLLER hc, PHCD_PORT p);
ULONG HcdEnumAttach(PHCD_CONTROLLER hc);
ULONG HcdEnumSettleAsk(PHCD_CONTROLLER hc);
ULONG HcdEnumSettleClock(VOID);
ULONG HcdEnumSettleArm(PHCD_CONTROLLER hc, PKTIMER deadline);
/* Why a first answer stopped waiting (HcdEnumSettleStep). */
#define HCD_SETTLE_DONE      0UL
#define HCD_SETTLE_TORNDOWN  1UL
#define HCD_SETTLE_DEADLINE  2UL
ULONG HcdEnumSettleStep(PHCD_CONTROLLER hc, ULONG target, PKTIMER deadline,
                        PULONG why);
VOID HcdEnumSettleEnd(PHCD_CONTROLLER hc, ULONG target, ULONG startLow,
                      ULONG why);
ULONG HcdEnumSettleDeferredAt(PHCD_CONTROLLER hc, ULONG rootPort,
                              ULONG route, ULONG number);
VOID HcdEnumInit(PHCD_CONTROLLER hc);
VOID HcdEnumDrop(PHCD_CONTROLLER hc);
ULONG HcdThreadCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb, PULONG control);
ULONG HcdThreadCommandRaw(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                          PULONG control);
/*
 * 29-A.5: ask for the device on a SuperSpeed root port to be sent back to
 * its USB 2.0 companion - its link written to SS.Disabled, its slot and PDOs
 * gone as on an unplug, and the port held while the device runs on the USB
 * 2.0 side, released only by the rules of xhci_link.h. Returns FALSE, with
 * nothing done, when the device is not on a SuperSpeed root port, the port
 * has no USB 2.0 companion (there is nowhere to send it), it is held
 * already, or no hold entry is free; each refusal is counted. The work is
 * the controller thread's next pass: `dev` may still be in use by the caller
 * when this returns. TRUE means "accepted for deferred execution": the
 * device record and its enumeration stay as they are until that pass, whose
 * identity read needs them; a caller that gets TRUE must not fail or tear
 * the device down itself (31-A.3's HcdDevicePdoCreate lists no PDO for it
 * and its machine waits in Present; Phase 31, reviewed and revised).
 * Sets dev->HoldAsked; a request the service later refuses for a
 * device still there ends with the device refused in place (hcd_enum.c,
 * hcdHoldResolve).
 * IRQL: <= DISPATCH_LEVEL, controller lock not held.
 */
BOOLEAN HcdHoldRequestUsb2(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG reason);

ULONG HcdThreadControl(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       UCHAR requestType, UCHAR request, USHORT value,
                       USHORT index, ULONG length, PULONG bytes);
ULONG HcdThreadControlEx(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         UCHAR requestType, UCHAR request, USHORT value,
                         USHORT index, ULONG length, PULONG bytes,
                         PULONG stalled);
/* What became of one thread control transfer (HcdThreadControlOutcome). */
#define HCD_CTL_DONE        0UL /* the device took it                    */
#define HCD_CTL_STALLED     1UL /* the device answered with a STALL      */
#define HCD_CTL_FAILED      2UL /* it completed with another error       */
#define HCD_CTL_NOT_SENT    3UL /* it never went out, or it timed out
                                 * (dev->Ep0Stuck, the reset requested,
                                 * or the device left: task 43.53)      */
#define HCD_CTL_ABANDONED   4UL /* abandoned for a device cycle (35-T.3):
                                 * dev->CycleAbandon, and nothing more is
                                 * sent to the device                     */
ULONG HcdThreadControlOutcome(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              UCHAR requestType, UCHAR request,
                              USHORT value, USHORT index, ULONG length,
                              PULONG bytes);
ULONG HcdThreadControlDoor(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           USHORT value, USHORT index, ULONG length,
                           PULONG bytes);
ULONG HcdThreadReaddress(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         ULONG linkKeep);
/* Task 45.41 (design record 13 section 15, L3): the whole configuration
 * descriptor and the serial number, where one was taken, read again and
 * compared with the record's, as a resume's keep identifies a device.
 * Thread only, powered. */
ULONG HcdThreadSameIdentity(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* dev->SerialState (33.2): not read yet, no iSerialNumber, a usable serial
 * id, a string read that is no instance id, every read try failed. */
#define HCD_SERIAL_UNREAD   0UL
#define HCD_SERIAL_NONE     1UL
#define HCD_SERIAL_OK       2UL
#define HCD_SERIAL_REFUSED  3UL
#define HCD_SERIAL_FAILED   4UL
ULONG HcdDeviceReadSerial(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* Task 45.40 (design record 13 section 10.7, "Identity across a
 * re-enumeration"): a root port's deferred arrival read again in place,
 * after HcdDeviceReadSerial and before anything is configured;
 * HCD_SDEFER_* (hcd_enum.c). Thread only, powered. */
#define HCD_SDEFER_GO       0UL
#define HCD_SDEFER_FAIL     1UL
#define HCD_SDEFER_CANCEL   2UL
ULONG HcdDeviceSerialDefer(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* The identity record of port p's place (task 45.40): a root port's own,
 * or for a hub's port the IdPlaces entry of its place's instance key -
 * found, or with `take` taken (XhciFuncIdPlaceTake). NULL when a hub
 * port's place is not known, or not found without `take`. Thread only. */
PXHCI_ID_PLACE HcdIdPlace(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG take);
/* (idVendor << 16) | idProduct from the device descriptor. IRQL: any. */
ULONG HcdDeviceVidPid(PHCD_USB_DEVICE dev);
/* What one enumeration's text reads (33.6) carry from PDO to PDO: the
 * language id, once read, and the string indexes that gave nothing. */
typedef struct _HCD_TEXT_READ {
    ULONG LangidRead;
    ULONG Langid;
    ULONG Failed[8];            /* bit (index & 31) of word index >> 5 */
} HCD_TEXT_READ, *PHCD_TEXT_READ;
ULONG HcdDeviceReadText(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        const ULONG *indexes, ULONG count, ULONG flags,
                        PHCD_TEXT_READ state, WCHAR *out);
VOID HcdEnumCycle(PHCD_CONTROLLER hc, ULONG port, ULONG serial);
/* Task 36.43: ask the thread to re-present in place the let-go function
 * PDOs of the device on `port` whose group is `serial` (a composite FDO's
 * start; HcdDevicePdoRepresentFunctions). IRQL: <= DISPATCH_LEVEL. */
VOID HcdEnumRepresent(PHCD_CONTROLLER hc, ULONG port, ULONG serial);

/* hcd_hub.c */
VOID HcdDeviceSlotParams(PHCD_USB_DEVICE dev, ULONG withHub,
                         PXHCI_SLOT_PARAMS sp);
ULONG HcdDevicePipeSpeed(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdDeviceSpeedClass(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdDeviceSpeedClassOf(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            ULONG psiv);
ULONG HcdHubPlace(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG speedClass,
                  PHCD_USB_DEVICE dev);
ULONG HcdHubStart(PHCD_CONTROLLER hc, PHCD_PORT p, PHCD_USB_DEVICE dev);
VOID HcdHubForget(PHCD_CONTROLLER hc, PHCD_HUB hub);
VOID HcdHubFree(PHCD_CONTROLLER hc, PHCD_HUB hub);
ULONG HcdHubPortFeature(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                        ULONG set, ULONG selector);
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

/* hcd_bot.c (task 49.34, design record 20 section 4.3.4). The slow
 * requests' events, as HcdBotSlowEnd takes them. */
#define HCD_BOT_EV_SELECT_CONFIG    1UL
#define HCD_BOT_EV_SELECT_IFACE     2UL
#define HCD_BOT_EV_ABORT            3UL
#define HCD_BOT_EV_RESET_CLEAR      4UL /* RESET_PIPE, SYNC_CLEAR_STALL  */
#define HCD_BOT_EV_RESET_HOST       5UL /* SYNC_RESET_PIPE: host only    */
#define HCD_BOT_EV_RESET_PORT       6UL
/* A transfer admitted to the device (HcdIoSubmit): the latch cleared and
 * a Bulk-Only pipe's submission fed. Controller lock held. */
VOID HcdBotAdmitLocked(PHCD_USB_DEVICE dev, PHCD_PIPE pipe,
                       const HCD_IO_REQUEST *req);
/* A transfer's end, before its URB is written (HcdIoDeferred). No lock
 * held; IRQL <= DISPATCH_LEVEL. */
VOID HcdBotXferEnd(PHCD_CONTROLLER hc, struct _HCD_XFER *x, LONG status);
/* A slow request's end (the thread's dispatcher). Thread only. */
VOID HcdBotSlowEnd(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG event,
                   PHCD_PIPE pipe, LONG usbd);
/* BotXfer retired or drained. Controller lock held. */
VOID HcdBotXferRetired(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* The keep's inputs (XHCI_RKEEP_F_BOT_ON, _BOT, _BOT_SYNC). Thread. */
ULONG HcdBotFlags(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* The decision's commit: the machine, the device idle and the latch set in
 * one hold. 0 refuses the keep. Thread. */
ULONG HcdBotLatch(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* The restore's Bulk-Only part: the replay held paused and the medium
 * check; then, after the port's last look, the locked reopening. Each 0
 * for the drop. Thread, powered. */
ULONG HcdBotRestore(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdBotReopen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* Whether no request of a client is taken or waiting on any pipe of the
 * device (hcd_io.c), or queued for the thread (hcd_cfg.c). Controller lock
 * held. */
ULONG HcdIoDeviceIdleLocked(PHCD_USB_DEVICE dev);
ULONG HcdCfgDeviceQueuedLocked(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* HcdCfgResumeReplay's `unique` asking for the pipes left paused. */
#define HCD_REPLAY_HOLD             2UL
ULONG HcdHubPortDisable(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n);
VOID HcdHubFlapSend(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n);
ULONG HcdHubPathPresent(PHCD_CONTROLLER hc, PHCD_PORT q);
VOID HcdHubClearTt(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                   ULONG endpointAddress, ULONG type, ULONG addressZero);
VOID HcdHubSilence(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n);
ULONG HcdHubClassRequest(PHCD_CONTROLLER hc, PHCD_HUB hub, UCHAR type,
                         UCHAR request, USHORT value, USHORT index,
                         ULONG length, PULONG bytes, PULONG stalled);

/* hcd_sshub.c: the SuperSpeed half of a USB 3 hub (30-A.1) */
ULONG HcdSsHubConfigure(PHCD_CONTROLLER hc, PHCD_HUB hub);
ULONG HcdSsHubPortReset(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                        PULONG speedClass);
ULONG HcdSsHubPortLook(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                       ULONG state, PXHCI_HUB_PORT_DECISION d);
ULONG HcdSsHubPortRecover(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n);
VOID HcdSsHubAdoptSpeed(PHCD_CONTROLLER hc, PHCD_PORT p,
                        PHCD_USB_DEVICE dev);
ULONG HcdSsHubPsiv(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG rootPort,
                   PULONG psiv);
VOID HcdSsHubCountPair(PHCD_CONTROLLER hc, PHCD_HUB hub);

/* hcd_strict.c: every flavour, on unless XhciCommandCheck is 0. */
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
VOID HcdStrictStart(PHCD_CONTROLLER hc);

/* hcd_dev.c */
ULONG HcdDevRetryReplay(PHCD_CONTROLLER hc, PHCD_PIPE pipe, ULONG token);
VOID HcdTraceLocked(PHCD_CONTROLLER hc, ULONG kind, ULONG slot, ULONG dci,
                    ULONG stream, ULONG a, ULONG b);
VOID HcdTrace(PHCD_CONTROLLER hc, ULONG kind, ULONG slot, ULONG dci,
              ULONG stream, ULONG a, ULONG b);
VOID HcdTraceCapture(PHCD_CONTROLLER hc, ULONG why);
ULONG HcdDevStopSettle(PHCD_CONTROLLER hc, PHCD_PIPE pipe, ULONG deqLow,
                       ULONG deqHigh);

/* hcd_inj.c and hcd_ctl.c's qemu half: the qemu flavour only, nothing in
 * release (design record 17 section 5; xhci_inj.h). */
#if defined(XHCI_FLAVOUR_QEMU)
NTSTATUS HcdCtlQemuReadDword(PHCD_CONTROLLER hc, PCWSTR name, PULONG value);
NTSTATUS HcdCtlQemuReadMachineDword(PCWSTR name, PULONG value);
VOID HcdInjStart(PHCD_CONTROLLER hc);
VOID HcdInjPoll(PHCD_CONTROLLER hc);
ULONG HcdInjIsrDrop(PHCD_CONTROLLER hc);
ULONG HcdInjStartHeld(PHCD_CONTROLLER hc);
VOID HcdInjStartRelease(PHCD_CONTROLLER hc);
ULONG HcdInjContainNoProof(PHCD_CONTROLLER hc);
ULONG HcdInjUsbsts(PHCD_CONTROLLER hc, ULONG usbsts);
USHORT HcdInjPciCommand(PHCD_CONTROLLER hc, USHORT command);
VOID HcdInjService(PHCD_CONTROLLER hc);
ULONG HcdInjCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb, PULONG control,
                    PULONG code);
ULONG HcdInjEpState(PHCD_CONTROLLER hc, ULONG slot, ULONG dci, ULONG state);
ULONG HcdInjWithholds(PHCD_CONTROLLER hc, ULONG slot, ULONG dci);
VOID HcdInjEp0Doorbell(PHCD_CONTROLLER hc, struct _HCD_USB_DEVICE *dev);
ULONG HcdInjClearHalt(PHCD_CONTROLLER hc, ULONG slot, ULONG dci,
                      PULONG outcome);
ULONG HcdInjSetInterface(PHCD_CONTROLLER hc, ULONG slot, PULONG stalled);
ULONG HcdInjSelectConfigure(PHCD_CONTROLLER hc, ULONG rollback,
                            PULONG code);
VOID HcdInjStaleArm(PHCD_CONTROLLER hc, struct _HCD_USB_DEVICE *dev,
                    struct _HCD_PIPE *pipe, ULONG firstIndex);
VOID HcdInjRestartRace(PHCD_CONTROLLER hc, struct _HCD_USB_DEVICE *dev,
                       struct _HCD_PIPE *pipe);
VOID HcdInjRepairRace(PHCD_CONTROLLER hc, struct _HCD_USB_DEVICE *dev,
                      struct _HCD_PIPE *pipe);
#endif

/* hcd_cfg.c */
NTSTATUS HcdCfgQueue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     struct _HCD_DEVICE_PDO *pdo, PIRP irp);
VOID HcdCfgReleaseFunction(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG interfaceMask);
VOID HcdCfgFlushDevice(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdCfgFailDevice(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* A departing device's fallback cycle of its location, charged to the
 * location's budget (hcd_cfg.c, beside hcdCfgFault; task 43.53's late
 * thread transfer too). The caller has checked dev->Location and the PDO.
 * Thread only. */
VOID HcdCfgCycle(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* Task 45.41 (design record 13 section 15, L3 and L4): a device whose
 * SuperSpeed link failed, restored in place by RESET_PORT's sequence with
 * what its pipes held failed; 1 restored, 0 failed and its cycle asked
 * (HcdCfgCycle). Thread only, powered. */
ULONG HcdCfgLinkKeep(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* Task 45.23 (design record 20 section 4.3.2): a device kept across a
 * resume's re-initialization, quieted with its slot gone and replayed on
 * its new one (hcd_cfg.c). Thread only. */
VOID HcdCfgResumeQuiet(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdCfgResumeSettle(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdCfgResumeReplay(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         ULONG unique);
/* The shutdown latch's flush of the queued commands (37.3, record 13
 * section 5.8); returns how many it completed. IRQL: PASSIVE_LEVEL, the
 * power gate held. */
ULONG HcdCfgShutFlush(PHCD_CONTROLLER hc);
VOID HcdCfgService(PHCD_CONTROLLER hc);
VOID HcdCfgCancelService(PHCD_CONTROLLER hc);
VOID HcdCfgRetryService(PHCD_CONTROLLER hc);
/* HcdCfgQemuStreamResync's outcomes (36.54, record 17 section 4.12.4). */
#define HCD_RESYNC_CLEAN        0UL /* done, or nothing to do             */
#define HCD_RESYNC_RESTOPPED    1UL /* done after stopping it again        */
#define HCD_RESYNC_FAILED       2UL /* given up; the controller reset asked */
ULONG HcdCfgQemuStreamResync(PHCD_CONTROLLER hc, ULONG slotId, ULONG dci);
VOID HcdCfgDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
PHCD_PIPE HcdCfgPipe(PHCD_USB_DEVICE dev, PVOID handle);
ULONG HcdCfgParentConfigure(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
struct _HCD_PIPE *HcdCfgHubOpen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                const XHCI_PIPE_EP *ep, ULONG iface);

/* hcd_pdo.c */
NTSTATUS HcdDevicePdoCreate(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdDevicePdoGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdDevicePdoDormantAll(PHCD_CONTROLLER hc);
ULONG HcdDevicePdoExists(PHCD_CONTROLLER hc, ULONG serial);
PDEVICE_RELATIONS HcdDevicePdoRelations(PHCD_CONTROLLER hc,
                                        PDEVICE_RELATIONS old, ULONG parent);
VOID HcdRelationsGiveBack(PDEVICE_RELATIONS old);
VOID HcdDevicePdoReleaseAll(PHCD_CONTROLLER hc);
ULONG HcdDevicePdoLetGo(PHCD_CONTROLLER hc, ULONG hubSerial);
ULONG HcdDevicePdoRepresent(PHCD_CONTROLLER hc, ULONG hubSerial);
ULONG HcdDevicePdoRepresentAsk(PHCD_CONTROLLER hc, ULONG parentSerial);
ULONG HcdDevicePdoRepresentFunctions(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdPdoRetireInit(VOID);
VOID HcdSerialInit(VOID);
/* Task 49.34 (design record 20 section 4.3.4, M4): whether no other device
 * record's listed PDO group, on any controller of this driver, carries
 * dev's vendor and product id and its serial as read. HcdSerialHold takes
 * hcdSerialLock and leaves it held, for a reopening made under it (the
 * order hcdSerialLock, then the controller lock), released by
 * HcdSerialUnhold; HcdSerialUnique takes and releases it. Neither with a
 * PdoListLock or the controller lock held. IRQL: <= DISPATCH_LEVEL. */
ULONG HcdSerialHold(PHCD_USB_DEVICE dev, PKIRQL serialIrql);
VOID HcdSerialUnhold(KIRQL serialIrql);
ULONG HcdSerialUnique(PHCD_USB_DEVICE dev);
VOID HcdSerialControllerAdd(PHCD_CONTROLLER hc);
VOID HcdSerialControllerRemove(PHCD_CONTROLLER hc);
ULONG HcdPdoRetire(PDEVICE_OBJECT obj);
VOID HcdPdoReapRetired(VOID);
NTSTATUS HcdDevicePdoPnp(PHCD_DEVICE_PDO pdo, PIRP irp);
NTSTATUS HcdDevicePdoPower(PHCD_DEVICE_PDO pdo, PIRP irp);

/* hcd_urb.c */
NTSTATUS HcdDevicePdoInternalIoctl(PHCD_DEVICE_PDO pdo, PIRP irp);
NTSTATUS HcdCompositePdoInternalIoctl(PHCD_DEVICE_PDO pdo, PIRP irp);
VOID HcdCompositeForget(PHCD_DEVICE_PDO pdo);
VOID HcdCompositeSelectDown(PDEVICE_OBJECT lower, PHCD_DEVICE_PDO parent,
                            ULONG unconfigure);
PHCD_COMP_RECORD HcdCompRecordRef(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdCompRecordRelease(PHCD_COMP_RECORD rec);
LONG HcdUrbIoRequest(PVOID urb, PHCD_IO_REQUEST req);
ULONG HcdUrbIsUsbdiQuery(const GUID *guid);
NTSTATUS HcdUrbQueryInterface(PHCD_DEVICE_PDO pdo, PIRP irp);
ULONG HcdUrbIsUsbdxQuery(const GUID *guid);
/* Task 49.45: the frees refused as not the bus's, image-wide (the door's
 * UsbdxForeignFrees word). IRQL: any. */
ULONG HcdUrbUsbdxForeignFrees(VOID);
NTSTATUS HcdUrbQueryUsbdx(PHCD_DEVICE_PDO pdo, PIRP irp);
VOID HcdUrbIdleDrain(PHCD_DEVICE_PDO pdo);
VOID HcdUrbBusifRelease(PHCD_DEVICE_PDO pdo);
VOID HcdUrbInit(VOID);

/* hcd_io.c */
VOID HcdIoPipeInitEp0(PHCD_USB_DEVICE dev);
VOID HcdIoPipeInit(PHCD_PIPE pipe, PHCD_USB_DEVICE dev);
VOID HcdIoPipeInitCount(PHCD_PIPE pipe, PHCD_USB_DEVICE dev, ULONG records);
NTSTATUS HcdIoSubmit(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     struct _HCD_DEVICE_PDO *pdo, PIRP irp, PVOID urb,
                     const HCD_IO_REQUEST *req);
VOID HcdIoDrainPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd);
VOID HcdIoPipeRelease(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoPipePause(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoPipeResume(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
/* HcdIoPipeResume's count alone, its streams' too; the caller holds the
 * controller lock and calls HcdIoPipeRelease after it (task 49.34). */
VOID HcdIoPipeUnpauseLocked(PHCD_PIPE pipe);
/* `usbd`: what a promoted record the cancel ends reports (36.24). */
ULONG HcdIoPipeCancelAll(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd);
ULONG HcdIoPipeMarkAll(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd);
VOID HcdIoCancelPdo(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                    struct _HCD_DEVICE_PDO *pdo);
VOID HcdIoPipeWaitCancelled(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
ULONG HcdIoPipeWaitCancelledFor(PHCD_CONTROLLER hc, PHCD_PIPE pipe,
                                PULONG rounds);
VOID HcdIoWaitPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdIoMapped(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG ok);
VOID HcdIoRetired(PHCD_CONTROLLER hc, PXHCI_TRANSFER t);
/* The same for a TD software took off the queue (36.24: a removal, not a
 * device's end). */
VOID HcdIoRemoved(PHCD_CONTROLLER hc, PXHCI_TRANSFER t);
VOID HcdIoDeferred(PHCD_CONTROLLER hc);
/* 36.24 (hcd_io.c), controller lock held: the refills a completion pass
 * collected, published once its walk is done; a promoted record's stop (a
 * cancel, an abort, a removal) offered to its machine; and whether a TD on
 * a pipe's queue belongs to a record whose successor the thread takes off
 * (hcd_cfg.c, hcdCfgCancelPipe). */
VOID HcdIoRefillFlush(PHCD_CONTROLLER hc);
VOID HcdIoWideStop(PHCD_CONTROLLER hc, PHCD_XFER x, LONG usbd);
ULONG HcdIoTruncating(PXHCI_TRANSFER t);
ULONG HcdIoDeviceDrain(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
/* The shutdown latch's flush of every device's transfers, the halt proven
 * (37.3, record 13 section 5.8). IRQL: PASSIVE_LEVEL, the power gate
 * held. */
VOID HcdIoShutFlush(PHCD_CONTROLLER hc);
NTSTATUS HcdIoRefuseLater(struct _HCD_DEVICE_PDO *pdo, PIRP irp, PVOID urb,
                          LONG usbd);
VOID HcdIoRefusedInit(struct _HCD_DEVICE_PDO *pdo);
ULONG HcdIoPark(struct _HCD_DEVICE_PDO *pdo, PIRP irp, PVOID urb,
                PVOID handle, PVOID endpoint, ULONG departed);
ULONG HcdIoParkedRelease(struct _HCD_DEVICE_PDO *pdo, ULONG aborted);
ULONG HcdIoIsoRelease(struct _HCD_DEVICE_PDO *pdo);
/* HcdIoPark's endpoint for a request whose endpoint cannot be named any
 * more (35-T.6): an abort of any pipe of the PDO covers it. */
#define HCD_IO_ENDPOINT_ANY ((PVOID)(ULONG_PTR)1)
NTSTATUS HcdIoHoldUnreadable(struct _HCD_DEVICE_PDO *pdo, PIRP irp,
                             PVOID urb, PVOID handle, PVOID endpoint);
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
/* The map pump, with XhciDmaStaging 0 alone (design record 13 section
 * 11.3). */
VOID HcdDmaMapQueue(PHCD_CONTROLLER hc, PHCD_XFER x);
#define HCD_MAP_PHASE_IDLE      0UL     /* no channel request outstanding  */
#define HCD_MAP_PHASE_ASKED     1UL     /* asked, its routine not entered  */
#define HCD_MAP_PHASE_ROUTINE   2UL     /* entered, its release DPC owed   */
VOID HcdDmaMapKick(PHCD_CONTROLLER hc);
VOID HcdDmaUnmap(PHCD_CONTROLLER hc, PHCD_XFER x);
VOID HcdDmaMapDrain(PHCD_CONTROLLER hc);
/* 36.24: the register-free check and the wide pool at a start, and their
 * end at a stop (after HcdDmaMapDrain). With staging, task 48.33 withdrew
 * promotion (design record 13 section 11.11.2, as built): the pool is
 * made only with XhciDmaStaging 0. IRQL: PASSIVE_LEVEL. */
VOID HcdDmaWideOpen(PHCD_CONTROLLER hc);
VOID HcdDmaWideClose(PHCD_CONTROLLER hc);
/* Every common buffer after the controller's block: charged to the boot
 * ledger first, checked whole below 4 GB, NULL when either refuses; freed,
 * or kept and its charge turned kept while the controller may still be
 * mastering (CommonBufferPinned). HcdDmaStreamKeep keeps one a caller
 * cannot free. IRQL: PASSIVE_LEVEL. */
PVOID HcdDmaStreamAlloc(PHCD_CONTROLLER hc, ULONG bytes, PPHYSICAL_ADDRESS pa);
VOID HcdDmaStreamFree(PHCD_CONTROLLER hc, ULONG bytes, PHYSICAL_ADDRESS pa,
                      PVOID va);
VOID HcdDmaStreamKeep(PHCD_CONTROLLER hc, ULONG bytes);
/* The boot ledger (Q-STG.3; record 13 section 11.11.6), from DriverEntry.
 * IRQL: PASSIVE_LEVEL. */
VOID HcdDmaLedgerInit(PDRIVER_OBJECT driver);
/* The image kept for the boot (DriverUnload cleared under the ledger's
 * lock): mode 4's refusal table outlives every device object (design
 * record 15 section 4.5.4). IRQL: <= DISPATCH_LEVEL. */
VOID HcdDmaKeepImage(VOID);

/* hcd_msg4.c: XhciInterruptMode 4's lifetime, common to both systems
 * (design record 15 sections 4.5.3 to 4.5.9 and 4.5.16). */
/* Msg4Lost's causes (section 4.5.9). */
#define HCD_MSG4_LOST_NONE          0L
#define HCD_MSG4_LOST_BRAKED        1L
#define HCD_MSG4_LOST_STRANDED      2L
#define HCD_MSG4_LOST_REARM         3L
#define HCD_MSG4_LOST_RECONNECT     4L
/* Statuses of this driver's own, kept in Msg4Status beside the library's
 * (0 to 14): the quiet step's refusal (the step in 7:0),
 * the function's location not established (the matches in 7:0), and a
 * system this build does not connect. */
#define HCD_MSG4_ST_QUIET           0x0100UL
#define HCD_MSG4_ST_LOCATION        0x0200UL
#define HCD_MSG4_ST_NOT_BUILT       0x0300UL
#define HCD_MSG4_ST_READY           0x0400UL    /* the flag could not be set */
/* IRQL: PASSIVE_LEVEL, DriverEntry. */
VOID HcdMsg4Init(VOID);
/* The system class (XHCI_MSG4_SYS_*). IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4SystemClass(VOID);
/* Whether the refusal table refuses this function's start, any mode;
 * reads the location only when the table holds something. IRQL:
 * PASSIVE_LEVEL. */
ULONG HcdMsg4StartRefused(PHCD_CONTROLLER hc);
/* The connection at the start, from hcdConnect: the quiet step, the
 * seam's connect, the outcome. IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdMsg4Connect(PHCD_CONTROLLER hc);
/* Whether a mode-4 connection is live on this controller. IRQL: any. */
ULONG HcdMsg4Live(PHCD_CONTROLLER hc);
/* The readiness flag set (and the bar lifted, XHCI_EXT_FLAG_INTERRUPTS
 * set) or cleared (and the bar set, the flag cleared), under the ISR's
 * exclusion. IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4Open(PHCD_CONTROLLER hc);
VOID HcdMsg4Close(PHCD_CONTROLLER hc);
/* Every start's reset of the lifetime's state, whatever the mode, before
 * anything reads it; and the judge's fresh count at an in-place recovery.
 * IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4StartReset(PHCD_CONTROLLER hc);
VOID HcdMsg4JudgeRestart(PHCD_CONTROLLER hc);
/* The start's No Op by interrupt, after the interrupter is on and before
 * the thread runs: STATUS_SUCCESS or the start refused (reason 31).
 * IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdMsg4NoOpSeen(PHCD_CONTROLLER hc);
/* A start refused after the connect with a status of this driver's own:
 * the delivery none, the reason and the status published as every
 * refusal's are. IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdMsg4RefuseStart(PHCD_CONTROLLER hc, ULONG reason, ULONG status);
/* The release (section 4.5.4 and 4.5.7), from hcdRelease before the DPC
 * drain: the flag cleared, the disconnect, the quarantine or the
 * no-hardware form, the refusal table. IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4Release(PHCD_CONTROLLER hc);
/* The D0 step (power work item, the power gate held): the re-arm, then
 * the flag and the interrupter's flag for the resume; 1 when it failed
 * and the connection is lost. IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4Resume(PHCD_CONTROLLER hc);
/* The way to D3: the flag cleared, the bar set (causes masked by the
 * suspend). IRQL: <= DISPATCH_LEVEL on NT, PASSIVE on 98 SE and ME. */
VOID HcdMsg4Suspend(PHCD_CONTROLLER hc);
/* A connection lost while running (section 4.5.4): the latch, up to
 * DISPATCH_LEVEL, under no lock of the caller's; the thread's half at
 * PASSIVE_LEVEL from the controller thread. */
VOID HcdMsg4LostLatch(PHCD_CONTROLLER hc, LONG cause);
VOID HcdMsg4LostThread(PHCD_CONTROLLER hc);
/* The watchdog (section 4.5.6): the backstop's caller asks whether the
 * judge watches this controller now (controller lock held), and reports
 * a stranded drain; the DPC reports a drain's events; the tick asks the
 * judge's deadline. IRQL: DISPATCH_LEVEL. */
ULONG HcdMsg4WatchedLocked(PHCD_CONTROLLER hc);
ULONG HcdMsg4ServedLocked(PHCD_CONTROLLER hc);
VOID HcdMsg4Gone(PHCD_CONTROLLER hc, ULONG surprise);
VOID HcdMsg4StrandedLocked(PHCD_CONTROLLER hc, PULONG lost);
VOID HcdMsg4DrainLocked(PHCD_CONTROLLER hc, PULONG lost);
VOID HcdMsg4TickLocked(PHCD_CONTROLLER hc, PULONG lost);
/* hcd_ctl.c: the routine the seam's trampoline calls, message 0 only,
 * gated by the readiness flag (section 4.5.5); the capability read; the
 * function's location (NT: the properties; 98 SE and ME: the match walk).
 * IRQL: the routine at its delivery's; the others PASSIVE_LEVEL. */
BOOLEAN HcdCtlMsg4Isr(PHCD_CONTROLLER hc, ULONG messageId);
ULONG HcdCtlMsgCapability(PHCD_CONTROLLER hc);
ULONG HcdCtlMsg4Location(PHCD_CONTROLLER hc, PULONG bdf, PULONG matches);
ULONG HcdCtlLineShared(PHCD_CONTROLLER hc, PULONG bdf, PULONG id,
                       PULONG functions);
/* hcd_msg4.c for the seam: the refusal table's entry with the image kept,
 * and Bus Master Enable cleared before a quarantine. IRQL:
 * PASSIVE_LEVEL. */
VOID HcdMsg4EnterRefusal(PHCD_CONTROLLER hc);
VOID HcdMsg4ClearBusMaster(PHCD_CONTROLLER hc);
/* NT 5's query-stop and its cancel (section 4.5.4), from hcd_pnp.c in the
 * PnP IRP's thread; the controller thread's brake look (section 4.5.6);
 * the DPC's latch of strategy 1's brake; whether Windows is to be told the
 * device failed. IRQL: PASSIVE_LEVEL but HcdMsg4DpcBrake (DISPATCH). */
NTSTATUS HcdMsg4QueryStop(PHCD_CONTROLLER hc);
VOID HcdMsg4CancelStop(PHCD_CONTROLLER hc);
VOID HcdMsg4Look(PHCD_CONTROLLER hc);
VOID HcdMsg4DpcBrake(PHCD_CONTROLLER hc);
ULONG HcdMsg4Failed(PHCD_CONTROLLER hc);
/* hcd_msgnt.c: msi4nt's seam, 2000 to XP x64 (sections 4.5.3 and 4.5.4).
 * Each returns an MSGIRQ_STATUS_* value (or HCD_MSG4_ST_*). IRQL:
 * PASSIVE_LEVEL, but HcdMsgNtSync (<= DISPATCH_LEVEL). */
VOID HcdMsgNtInit(PUNICODE_STRING registryPath);
ULONG HcdMsgNtConnect(PHCD_CONTROLLER hc, ULONG reconnect, PULONG reason);
ULONG HcdMsgNtSync(PHCD_CONTROLLER hc, LONG ready);
ULONG HcdMsgNtReArm(PHCD_CONTROLLER hc);
ULONG HcdMsgNtDisconnect(PHCD_CONTROLLER hc, ULONG flags, ULONG injectBit);
VOID HcdMsgNtRelease(PHCD_CONTROLLER hc, ULONG silent);
VOID HcdMsgNtReleaseClaim(PHCD_CONTROLLER hc);
ULONG HcdMsgNtLook(PHCD_CONTROLLER hc);
VOID HcdMsgNtReportFailed(PHCD_CONTROLLER hc);
/* hcd_stage.c (task 48.33; design record 13 section 11.11) */
/* A slab of the shared pool. State HCD_STAGE_SLAB_*, under StageLock. */
typedef struct _HCD_STAGE_SLAB {
    PUCHAR Va;
    PHYSICAL_ADDRESS Pa;
    ULONG State;
} HCD_STAGE_SLAB, *PHCD_STAGE_SLAB;
#define HCD_STAGE_SLAB_FREE     0UL
#define HCD_STAGE_SLAB_TD       1UL     /* a TD holds it                   */
#define HCD_STAGE_SLAB_RESERVE  2UL     /* a bulk pipe's reserve           */
#define HCD_STAGE_RES_NONE      0UL
#define HCD_STAGE_RES_PAGE      1UL
#define HCD_STAGE_RES_SLAB      2UL
/* The qemu flavour's switches (exempt from the registry rule):
 * XhciQemuStagePool, the shared pool's slabs at most (absent: the
 * value's); XhciQemuStageFault, bit 0 every stop's quiesce reported not
 * proven, bits 15:8 window mappings failed (255 all of them);
 * XhciQemuStageMapProbe, the first window copy first maps its span through
 * that many window MDLs of the probe's own without unmapping, stopping at
 * the first failure, reports the count on the 0xE9 channel and unmaps them
 * (at most the cap below); XhciQemuStageFault bit 1, on Windows 98 and ME
 * only, an MDL's copy through a window even when the MDL has a system
 * address, so that MmMapLockedPages and the probe are reached there. */
#define HCD_STAGE_QEMU_PROBE_MAX 0x8000UL
#define HCD_STAGE_QEMU_POOL_NONE 0xFFFFFFFFUL
#define HCD_STAGE_QEMU_UNPROVEN  0x00000001UL
#define HCD_STAGE_QEMU_WINDOW98  0x00000002UL
#define HCD_STAGE_QEMU_MAPFAIL(v) (((v) >> 8) & 0xFFUL)
/* Task 49.29 (record 13 section 11.11.12): bit 2, the hold arms the
 * injection of all ones without the proof as it takes its record; bit 3,
 * the per-request log; bits 19:16, the state the hold keeps. */
#define HCD_STAGE_QEMU_CONTAIN   0x00000004UL
#define HCD_STAGE_QEMU_URBLOG    0x00000008UL
#define HCD_STAGE_QEMU_HOLD(v)   (((v) >> 16) & 0xFUL)
#define HCD_STAGE_HOLD_NONE      0UL
#define HCD_STAGE_HOLD_WAITING   1UL
#define HCD_STAGE_HOLD_FILLING   2UL
#define HCD_STAGE_HOLD_READY     3UL
#define HCD_STAGE_HOLD_DRAINING  4UL
#define HCD_STAGE_HOLD_COPY      5UL
#define HCD_STAGE_HOLD_DPC       6UL
#define HCD_STAGE_HOLD_IDLE      0UL
#define HCD_STAGE_HOLD_HELD      1UL
#define HCD_STAGE_HOLD_SPENT     2UL
VOID HcdStageInitObjects(PHCD_CONTROLLER hc);
NTSTATUS HcdStageOpen(PHCD_CONTROLLER hc);
VOID HcdStageDrain(PHCD_CONTROLLER hc);
VOID HcdStageClose(PHCD_CONTROLLER hc);
VOID HcdStageCloseAdmission(PHCD_CONTROLLER hc);
LONG HcdStagePipeOpen(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
VOID HcdStagePipeClose(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
ULONG HcdStageStreamsOpen(PHCD_CONTROLLER hc, PHCD_STREAMS st);
VOID HcdStageStreamsClose(PHCD_CONTROLLER hc, PHCD_STREAMS st);
ULONG HcdStageMulti(PHCD_XFER x);
VOID HcdStageSubmit(PHCD_CONTROLLER hc, PHCD_XFER x);
ULONG HcdStageIsoFill(PHCD_CONTROLLER hc, PHCD_XFER x);
ULONG HcdStageComplete(PHCD_CONTROLLER hc, PHCD_XFER x, PULONG budget);
ULONG HcdIoParkCloseAll(PHCD_CONTROLLER hc);
VOID HcdStageCancelLocked(PHCD_CONTROLLER hc, PHCD_XFER x, LONG usbd);
VOID HcdStagePublishedLocked(PHCD_CONTROLLER hc, PHCD_XFER x);
VOID HcdStageNotes(PHCD_CONTROLLER hc);

/* hcd_svc.c (the services themselves are in hcd_svc.h) */
VOID HcdRelativeMs(PLARGE_INTEGER due, ULONG milliseconds);
/* 0 waits until satisfied; a gate's bound, in unsatisfied returns. */
ULONG HcdWaitEvent(PKEVENT event, ULONG bound);
#define HCD_GATE_WAIT_BOUND 100UL
VOID HcdTimersInit(PHCD_CONTROLLER hc);
VOID HcdTimersDrain(PHCD_CONTROLLER hc);
VOID HcdTimersOpen(PHCD_CONTROLLER hc);
VOID HcdFrameTimerStart(PHCD_CONTROLLER hc);
VOID HcdTolClockStart(PHCD_CONTROLLER hc);
ULONG HcdTolNow(PHCD_CONTROLLER hc);
/* hcd_ctl.c, for the tolerance clock's DPC (task 36.55) */
VOID HcdCtlBackstopTick(PHCD_CONTROLLER hc);

/* hcd_door.c */
VOID HcdDoorInit(VOID);
VOID HcdDoorGateEnter(PHCD_CONTROLLER hc);
VOID HcdDoorGateLeave(PHCD_CONTROLLER hc);
VOID HcdDoorControllerStart(PHCD_CONTROLLER hc);
VOID HcdDoorControllerStop(PHCD_CONTROLLER hc);
/* hcd_acpi.c (task 46.21; design record 13 section 8.13): HcdAcpiInit at
 * AddDevice; HcdAcpiForget at the stop, surprise removal and removal;
 * HcdAcpiRequest for IOCTL_XHCI98_ACPI at PASSIVE_LEVEL. */
VOID HcdAcpiInit(PHCD_CONTROLLER hc);
VOID HcdAcpiForget(PHCD_CONTROLLER hc);
VOID HcdAcpiOpen(PHCD_CONTROLLER hc);
NTSTATUS HcdAcpiRequest(PHCD_CONTROLLER hc, PUCHAR buf, ULONG inLen,
                        ULONG outLen, PULONG_PTR info);
VOID HcdDoorControllerRemove(PHCD_CONTROLLER hc);
VOID HcdDoorRootHubStart(PHCD_ROOTHUB_FDO fdo, PHCD_CONTROLLER hc);
VOID HcdDoorRootHubStop(PHCD_ROOTHUB_FDO fdo);
VOID HcdDoorRootHubRemove(PHCD_ROOTHUB_FDO fdo, PHCD_CONTROLLER hc);
NTSTATUS HcdDoorCreateClose(PIRP irp);
NTSTATUS HcdDoorControllerIoctl(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdDoorRootHubIoctl(PHCD_CONTROLLER hc, PIRP irp, PLONG io,
                             PKEVENT ioDone);
VOID HcdDoorHubStart(PHCD_HUB_FDO fdo, PHCD_CONTROLLER hc);
VOID HcdDoorHubStop(PHCD_HUB_FDO fdo);
VOID HcdDoorHubRemove(PHCD_HUB_FDO fdo, PHCD_CONTROLLER hc);
NTSTATUS HcdDoorHubIoctl(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO hub, PIRP irp,
                         PLONG io, PKEVENT ioDone);
VOID HcdDoorService(PHCD_CONTROLLER hc, ULONG serve);
VOID HcdDoorFlushDevice(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdDoorQueueOpen(PHCD_CONTROLLER hc);
VOID HcdDoorQueueClose(PHCD_CONTROLLER hc);
VOID HcdDoorCleanup(PHCD_CONTROLLER hc, PIRP irp);
NTSTATUS HcdRootHubFdoCleanup(PHCD_ROOTHUB_FDO fdo, PIRP irp);
NTSTATUS HcdHubFdoCleanup(PHCD_HUB_FDO fdo, PIRP irp);
VOID HcdDoorDeviceStart(PHCD_DEVICE_PDO pdo);
VOID HcdDoorDeviceStop(PHCD_DEVICE_PDO pdo, ULONG release);

/* hcd_hubfdo.c (task 33.4) */
NTSTATUS HcdHubAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
NTSTATUS HcdHubFdoPnp(PHCD_HUB_FDO fdo, PIRP irp);
NTSTATUS HcdHubFdoPower(PHCD_HUB_FDO fdo, PIRP irp);
NTSTATUS HcdHubFdoDeviceControl(PHCD_HUB_FDO fdo, PIRP irp);
NTSTATUS HcdHubFdoSystemControl(PHCD_HUB_FDO fdo, PIRP irp);
/* hcd_log.c */
VOID HcdLogFlush(PHCD_CONTROLLER hc, ULONG reason, ULONG counters);
VOID HcdCountersStart(PHCD_CONTROLLER hc);
VOID HcdCountersPoll(PHCD_CONTROLLER hc);

NTSTATUS HcdReadValueDword(PHCD_CONTROLLER hc, PCWSTR name, PULONG value);

/* hcd_quirk.c: Linux's xHCI controller quirks, the FDO's half (task 38.10,
 * design record 20). IRQL as each function's comment there says. */
VOID HcdQuirkAddDevice(PHCD_CONTROLLER hc);
ULONG HcdQuirkFilterAvoid(PHCD_CONTROLLER hc);
VOID HcdQuirkStartBegin(PHCD_CONTROLLER hc);
ULONG HcdQuirkMsiAvoid(PHCD_CONTROLLER hc, ULONG interruptMode);
VOID HcdQuirkMsiRefused(PHCD_CONTROLLER hc);
VOID HcdQuirkPublishLocked(PHCD_CONTROLLER hc);
VOID HcdQuirkStart(PHCD_CONTROLLER hc);
VOID HcdQuirkAsmFlow(PHCD_CONTROLLER hc);
VOID HcdQuirkSuspend(PHCD_CONTROLLER hc);
ULONG HcdQuirkShutdown(PHCD_CONTROLLER hc);
VOID HcdQuirkShutdownFlush(PHCD_CONTROLLER hc);
VOID HcdQuirkFlushAck(PHCD_CONTROLLER hc, LONG ticket, ULONG emitted);
VOID HcdQuirkResumed(PHCD_CONTROLLER hc, ULONG restoresBefore);
ULONG HcdQuirkLinkNoOp(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdQuirkLinkPadded(PHCD_CONTROLLER hc, ULONG padded);

/* hcd_msi86.c: message-signalled interrupts' kernel half (design records
 * 15 section 4.3 and 18). */
#define HCD_MSI_ROUTINES_NONE       0UL     /* not looked for, or not NT 6 */
#define HCD_MSI_ROUTINES_SYSTEM     1UL     /* amd64: MmGetSystemRoutineAddress */
#define HCD_MSI_ROUTINES_HELPER     2UL     /* x86: the helper's answer    */
#define HCD_MSI_ROUTINES_NOT_FOUND  3UL     /* looked for, not found       */
#define HCD_MSI_ROUTINES_MSI4NT     4UL     /* mode 4: msi4nt's connection */
/* 5 belonged to an unreleased build and is not produced. */

#define HCD_MSI_FROM_NONE           0UL
#define HCD_MSI_FROM_ADDDEVICE      1UL
#define HCD_MSI_FROM_START          2UL

/* The x86 seam's routine types, WDK 7.1's (wdm.h), restated here because
 * the Windows 2000 DDK has none of them. */
#define HCD_CONNECT_LINE_BASED      2UL
#define HCD_CONNECT_MESSAGE_BASED   3UL

typedef BOOLEAN (NTAPI *HCD_MESSAGE_SERVICE_ROUTINE)(PKINTERRUPT Interrupt,
                                                     PVOID Context,
                                                     ULONG MessageId);

typedef struct _HCD_IO_CONNECT_MESSAGE_BASED {
    PDEVICE_OBJECT PhysicalDeviceObject;
    PVOID *ConnectionContext;
    HCD_MESSAGE_SERVICE_ROUTINE MessageServiceRoutine;
    PVOID ServiceContext;
    PKSPIN_LOCK SpinLock;
    KIRQL SynchronizeIrql;
    BOOLEAN FloatingSave;
    PKSERVICE_ROUTINE FallBackServiceRoutine;
} HCD_IO_CONNECT_MESSAGE_BASED;

/* IO_CONNECT_INTERRUPT_FULLY_SPECIFIED_PARAMETERS, the union's largest
 * member, so the whole union's size is WDK 7.1's. */
typedef struct _HCD_IO_CONNECT_FULLY_SPECIFIED {
    PDEVICE_OBJECT PhysicalDeviceObject;
    PKINTERRUPT *InterruptObject;
    PKSERVICE_ROUTINE ServiceRoutine;
    PVOID ServiceContext;
    PKSPIN_LOCK SpinLock;
    KIRQL SynchronizeIrql;
    BOOLEAN FloatingSave;
    BOOLEAN ShareVector;
    ULONG Vector;
    KIRQL Irql;
    KINTERRUPT_MODE InterruptMode;
    KAFFINITY ProcessorEnableMask;
    USHORT Group;
} HCD_IO_CONNECT_FULLY_SPECIFIED;

typedef struct _HCD_IO_CONNECT_PARAMETERS {
    ULONG Version;
    union {
        HCD_IO_CONNECT_FULLY_SPECIFIED FullySpecified;
        HCD_IO_CONNECT_MESSAGE_BASED MessageBased;
    } u;
} HCD_IO_CONNECT_PARAMETERS, *PHCD_IO_CONNECT_PARAMETERS;

typedef struct _HCD_IO_DISCONNECT_PARAMETERS {
    ULONG Version;
    PVOID ConnectionContext;
} HCD_IO_DISCONNECT_PARAMETERS, *PHCD_IO_DISCONNECT_PARAMETERS;

typedef struct _HCD_IO_MESSAGE_INFO_ENTRY {
    PHYSICAL_ADDRESS MessageAddress;
    KAFFINITY TargetProcessorSet;
    PKINTERRUPT InterruptObject;
    ULONG MessageData;
    ULONG Vector;
    KIRQL Irql;
    ULONG Mode;
    ULONG Polarity;
} HCD_IO_MESSAGE_INFO_ENTRY;

typedef struct _HCD_IO_MESSAGE_INFO {
    KIRQL UnifiedIrql;
    ULONG MessageCount;
    HCD_IO_MESSAGE_INFO_ENTRY MessageInfo[1];
} HCD_IO_MESSAGE_INFO, *PHCD_IO_MESSAGE_INFO;

typedef NTSTATUS (NTAPI *HCD_CONNECT_EX)(PHCD_IO_CONNECT_PARAMETERS p);
typedef VOID (NTAPI *HCD_DISCONNECT_EX)(PHCD_IO_DISCONNECT_PARAMETERS p);

VOID HcdMsiAddDevice(PHCD_CONTROLLER hc);
VOID HcdMsiStartValues(PHCD_CONTROLLER hc, ULONG modeFound, ULONG mode);
NTSTATUS HcdMsiFilter(PHCD_CONTROLLER hc, PIRP irp);
VOID HcdMsiStartLook(PHCD_CONTROLLER hc);
PVOID HcdHlprTimerRoutine(PHCD_CONTROLLER hc, ULONG look);
ULONG HcdMsiRoutines(PHCD_CONTROLLER hc, PVOID *connect, PVOID *disconnect);
ULONG HcdMsiNoLineOffered(PHCD_CONTROLLER hc);
ULONG HcdMsiSystem(VOID);
/* The name lookup for mode 4 (record 18 section 15.16): amd64
 * MmGetSystemRoutineAddress; x86 the helper's minor-1 routine, fetched by
 * HcdMsiFindRoutineLook (1 when it was). IRQL: PASSIVE_LEVEL. */
ULONG HcdMsiFindRoutineLook(PHCD_CONTROLLER hc);
PVOID HcdMsiResolve(PCWSTR name);
VOID HcdMsiRemove(PHCD_CONTROLLER hc);
VOID HcdMsiCountersStart(PHCD_CONTROLLER hc);
VOID HcdMsiTraceStart(PHCD_CONTROLLER hc, ULONG grant, NTSTATUS status);
VOID HcdMsiTraceLife(PHCD_CONTROLLER hc, ULONG event, ULONG a, ULONG b);

/* The lifecycle events' codes (record 18 section 9, kind L). */
#define HCD_MSI_LIFE_LOOK           1UL
#define HCD_MSI_LIFE_BIND           2UL
#define HCD_MSI_LIFE_REFUSE         3UL
#define HCD_MSI_LIFE_ROUTINES       4UL
#define HCD_MSI_LIFE_CONNECT        5UL
#define HCD_MSI_LIFE_DISCONNECT     6UL
#define HCD_MSI_LIFE_DROP           7UL
#define HCD_MSI_LIFE_REMOVE         8UL
#define HCD_MSI_LIFE_FILTER         9UL

/* hcd_tmr.c: a finer system timer while polling (design record 15 section
 * 7.2, task 38.3). The producers' calls hold the controller lock. */
VOID HcdTmrInitObjects(PHCD_CONTROLLER hc);
VOID HcdTmrStartReset(PHCD_CONTROLLER hc);
VOID HcdTmrCreate(PHCD_CONTROLLER hc);
VOID HcdTmrStopEarly(PHCD_CONTROLLER hc);
VOID HcdTmrJoin(PHCD_CONTROLLER hc);
VOID HcdTmrBar(PHCD_CONTROLLER hc);
VOID HcdTmrBoundaryLocked(PHCD_CONTROLLER hc);
VOID HcdTmrGrantLocked(PHCD_CONTROLLER hc);
VOID HcdTmrActiveLocked(PHCD_CONTROLLER hc, ULONG active);
VOID HcdTmrSignalLocked(PHCD_CONTROLLER hc);

/* hcd_susp.c (task 36.30; design record 14 section 3.10) */
VOID HcdSuspService(PHCD_CONTROLLER hc);
VOID HcdSuspDeviceInit(PHCD_USB_DEVICE dev, PHCD_PORT port);
VOID HcdSuspDeviceGone(PHCD_USB_DEVICE dev);
ULONG HcdSuspLinkActive(PHCD_USB_DEVICE dev);
ULONG HcdSuspDoorServes(PHCD_USB_DEVICE dev);
ULONG HcdSuspDefers(PHCD_USB_DEVICE dev, ULONG abortOnly);
ULONG HcdSuspOwnsPort(PHCD_CONTROLLER hc, PHCD_PORT q);
VOID HcdSuspRootSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG portsc,
                       ULONG acked);
VOID HcdSuspHubSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG status,
                      ULONG acked, ULONG lost);
/* 36.38, stage B (design record 14 section 8) */
VOID HcdSuspSsHubSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG status,
                        ULONG acked, ULONG lost);
VOID HcdSuspCfgEnter(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
VOID HcdSuspCfgExit(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                    const UCHAR *config, ULONG length, ULONG ok);
VOID HcdSuspNotificationLocked(PHCD_CONTROLLER hc, ULONG slotId, ULONG type,
                               ULONG iface);
/* 36.31, dispatch's half (design record 14 section 3.11) */
typedef struct _HCD_SUSP_SETTLE {
    struct _HCD_DEVICE_PDO *Pdo;
    PIRP Pwr;
    ULONG PwrD0;
    PIRP Wake;
    PIRP Idle;                      /* 36.32: completed CANCELLED        */
    ULONG IdleEntry;                /* a queued entry's reference to drop */
} HCD_SUSP_SETTLE, *PHCD_SUSP_SETTLE;
ULONG HcdSuspPdoSetPower(struct _HCD_DEVICE_PDO *pdo, PIRP irp,
                         NTSTATUS *status);
ULONG HcdSuspPdoWaitWake(struct _HCD_DEVICE_PDO *pdo, PIRP irp,
                         NTSTATUS *status);
VOID HcdSuspPdoCaps(struct _HCD_DEVICE_PDO *pdo, PDEVICE_CAPABILITIES caps);
VOID HcdSuspPdoClose(struct _HCD_DEVICE_PDO *pdo, ULONG removal);
VOID HcdSuspPdoRundown(struct _HCD_DEVICE_PDO *pdo);
ULONG HcdSuspPdoReopen(PHCD_CONTROLLER hc, struct _HCD_DEVICE_PDO *pdo);
ULONG HcdSuspUnlinkLocked(PHCD_CONTROLLER hc, struct _HCD_DEVICE_PDO *pdo,
                          PHCD_SUSP_SETTLE out);
VOID HcdSuspUnlinkFinish(PHCD_CONTROLLER hc, PHCD_SUSP_SETTLE s);
VOID HcdSuspDoneDrain(PHCD_CONTROLLER hc);
VOID HcdSuspDoneStart(PHCD_CONTROLLER hc);
VOID HcdSuspControllerDownLocked(PHCD_CONTROLLER hc);
#if defined(XHCI_FLAVOUR_QEMU)
VOID HcdSuspQemuStopNote(PHCD_CONTROLLER hc);
#endif
VOID HcdSuspControllerUp(PHCD_CONTROLLER hc);
VOID HcdSuspSettleStale(PHCD_CONTROLLER hc);
VOID HcdSuspSettleLatched(PHCD_CONTROLLER hc);
/* 36.32, the idle notification (design record 14 sections 4.4, 6, 3.12) */
ULONG HcdSuspIdleSubmit(struct _HCD_DEVICE_PDO *pdo, PHCD_CONTROLLER hc,
                        PIRP irp, PVOID callback, PVOID context,
                        NTSTATUS *status);
ULONG HcdSuspIdleSystem(struct _HCD_DEVICE_PDO *pdo, SYSTEM_POWER_STATE state);
ULONG HcdSuspIdleRundown(struct _HCD_DEVICE_PDO *pdo, ULONG mayWaitCallback);
NTSTATUS HcdSuspCbStart(PHCD_CONTROLLER hc);
VOID HcdSuspCbStop(PHCD_CONTROLLER hc);
VOID HcdSuspDriverInit(VOID);
VOID HcdSuspDriverUnload(VOID);
/* hcd_cfg.c's half of 3.3's deferral */
VOID HcdCfgUndefer(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
ULONG HcdCfgRewind(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, PHCD_PIPE pipe);
VOID HcdCfgFlushPdoDeferred(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            struct _HCD_DEVICE_PDO *pdo);

/* hcd_pool.c */
VOID HcdPoolInit(VOID);
PVOID HcdPoolAlloc(ULONG bytes);
PVOID HcdPoolAllocRequirements(ULONG bytes);
VOID HcdPoolFree(PVOID p);
PMDL HcdPoolWindowMdl(ULONG bytes);
VOID HcdPoolWindowMdlFree(PMDL mdl);
/* Task 49.34 (hcd_stage.c): the first `n` bytes (at most
 * HCD_BOT_COPY_BYTES) of a client's locked MDL copied to `to` through the
 * controller's own window, never the client's MDL changed; 0 when it is
 * not open, busy or will not map. IRQL: <= DISPATCH_LEVEL. */
#define HCD_BOT_COPY_BYTES  32UL
ULONG HcdStageCopyHead(PHCD_CONTROLLER hc, PMDL client, PUCHAR to, ULONG n);
ULONG HcdPoolOutstandingCount(VOID);
PVOID HcdPoolAllocHandedOff(ULONG bytes);

/* IRP_MN_QUERY_BUS_INFORMATION for a root hub, hub, device (`function` 0)
 * or function PDO (hcd_pdo.c, task 48.13). PASSIVE_LEVEL. */
NTSTATUS HcdPdoBusInformation(PIRP irp, ULONG function);
VOID HcdPoolFreeForeign(PVOID p);
PVOID HcdPoolAllocClient(ULONG bytes);
VOID HcdPoolFreeClient(PVOID p);
PVOID HcdPoolAllocAcpi(ULONG bytes);
VOID HcdPoolFreeAcpi(PVOID p);
ULONG HcdPoolClientCount(VOID);
VOID HcdPoolMdlFree(PMDL mdl);
PMDL HcdPoolMdlBuild(PVOID va, ULONG bytes);

#endif /* HCD_H */
