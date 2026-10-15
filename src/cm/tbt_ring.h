/*
 * tbt_ring.h - the host interface's ring 0, its pure half: transmit and
 * receive in Raw Mode, CRC-32C, and the control-packet transport with
 * sequence numbers, timeouts and retries (roadmap-usb4.md task 39-A.2;
 * design record 21 sections 4.2 and 4.3), grown for the USB4 software
 * connection manager (task 40-A.1; design record 22 sections 4.4, 5.1 to
 * 5.3 and 7.4): write requests, the notification classes, plain and
 * enhanced acknowledgements, hot plug acknowledgement and its queue, the
 * ring 0 takeover, the microsecond clock and the safe-point callback, and
 * maximum waits kept apart from minimum waits.
 *
 * Lifted from usb4-emu's tools/usb4cm/core/u4hi.c (MIT; its notice is in
 * tbt_ring.c) and rewritten over an injected accessor, TBT_HOST: the driver
 * supplies BAR0 reads and writes, the waits, the clocks and the log, and
 * owns the common buffer it hands to TbtRingStart; the host suites
 * (test\test_tbt_ring.c, test\u4_fakefab.c) supply a modelled host
 * interface. That seam is u4hi.c's own callback contract (mmio_rd, mmio_wr,
 * dma_alloc, udelay, log), kept as record 21 section 4.2 asks, with the
 * allocation moved out: the driver allocates once at start through
 * cm_pool.c and passes the block in.
 *
 * Naming: this directory's exported functions take the Tbt prefix and its
 * file-local ones tbt, its types TBT_, as xhciuas.sys's take Uas and uas.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic, no floating
 * point. The upper 32 bits of every hardware address are written 0.
 *
 * IRQL and locking, for every function here that takes a TBT_RING, a
 * TBT_CTL or a TBT_HOST: PASSIVE_LEVEL, on the connection-manager thread
 * only (record 21 section 4.4), because the waits it makes through
 * TBT_HOST.Sleep are thread waits; no lock is taken and none may be held by
 * the caller, since that thread is the only one that touches the rings.
 * TbtCrc32c, TbtCtlClassify and the TbtRoute helpers are pure and callable
 * at any IRQL.
 */

#ifndef TBT_RING_H
#define TBT_RING_H

#include "../xhci_compat.h"
#include "tbt_regs.h"

/* ---------------------------------------------------------------------- */
/* The accessor                                                           */
/* ---------------------------------------------------------------------- */

/*
 * Everything the pure core asks of the machine. Read32 and Write32 are one
 * aligned DW of BAR0 at a byte offset (READ_REGISTER_ULONG and
 * WRITE_REGISTER_ULONG in the driver). Stall busy-waits at least `us`
 * microseconds and is asked for 10 to 20 us at a time; Sleep waits at
 * least `ms` milliseconds on the thread. NowMs is a monotonic millisecond
 * clock that may wrap (every comparison is a wrap-safe subtraction) and may
 * be coarse. Log records one line, a label and a value (XhciLogAppend's
 * shape); it may be NULL. None of them may call back into this core.
 *
 * ClockUs and TickUs (design record 22 section 5.3): a monotonic 32-bit
 * microsecond counter compared only by unsigned difference (its wrap after
 * about 71 minutes is harmless), quantised to TickUs, the period of the
 * clock's tick (the driver: KeQueryTickCount times KeQueryTimeIncrement /
 * 10, read once at start). ClockUs may be NULL, and then NowMs times 1000
 * stands in; TickUs 0 means the clock is exact. A host initialised by
 * position before these two fields existed leaves them NULL and 0.
 */
typedef struct _TBT_HOST {
    PVOID Context;
    ULONG (*Read32)(PVOID context, ULONG offset);
    VOID  (*Write32)(PVOID context, ULONG offset, ULONG value);
    VOID  (*Stall)(PVOID context, ULONG microseconds);
    VOID  (*Sleep)(PVOID context, ULONG milliseconds);
    ULONG (*NowMs)(PVOID context);
    VOID  (*Log)(PVOID context, const char *label, ULONG value);
    ULONG (*ClockUs)(PVOID context);
    ULONG TickUs;
    /* TRUE once the host interface may no longer be accessed (cm_live.h:
     * the gate closed, or the device revoked): every deadline and wait of
     * the core ends at once, so no sequence keeps polling a register the
     * gate refuses. NULL: never. */
    BOOLEAN (*Cancelled)(PVOID context);
} TBT_HOST, *PTBT_HOST;

/* ---------------------------------------------------------------------- */
/* Results                                                                */
/* ---------------------------------------------------------------------- */

#define TBT_OK                  0L
#define TBT_E_TIMEOUT           (-1L)   /* no answer after every attempt */
#define TBT_E_NOTIFY            (-2L)   /* an error notification answered a request */
#define TBT_E_HW                (-3L)   /* BAR0 not decoding, a ring that does not move */
#define TBT_E_PARAM             (-4L)   /* a request the core refuses to send */
#define TBT_E_RING_FULL         (-5L)   /* no vacant TX descriptor (Spec 12.6.3.2.3) */
#define TBT_E_MEMORY            (-6L)   /* the block is too small or misaligned */
#define TBT_E_ERROR_FLAG        (-7L)   /* an ICM response with its error flag set */
#define TBT_E_GONE              (-8L)   /* the device a wait was for was disconnected */
#define TBT_E_STOPPED           (-9L)   /* the rings are not running */
#define TBT_E_REFUSED           (-10L)  /* a register check refuses the start */
#define TBT_E_UNSUPPORTED       (-11L)  /* a router operation answered Not Supported */
#define TBT_E_FAILED            (-12L)  /* a router operation's Status was not 0 */
#define TBT_E_BLOCKED           (-13L)  /* a router's Operation Valid never cleared */
#define TBT_E_STATE             (-14L)  /* a register read back not as required */

/* ---------------------------------------------------------------------- */
/* Ring geometry (record 21 section 4.2, record 22 section 4.4)           */
/* ---------------------------------------------------------------------- */

/* 10 descriptors each way and 256-byte buffers, the Alpine Ridge client's
 * own geometry [obs] ICM 2.1 (ring sizes 0x0000000a and 0x0100000a). */
#define TBT_RING_ENTRIES        10UL
#define TBT_RING_BUF_BYTES      256UL
#define TBT_FRAME_DWORDS        (TBT_RING_BUF_BYTES / 4UL)
/* The USB4 profile's 16 each way (record 22 section 4.4 step 3: usb4cm
 * u4hi.c, FreeBSD nhi.c nhi_alloc_ring0), and the most a start accepts. */
#define TBT_RING_U4_ENTRIES     16UL
#define TBT_RING_MAX_ENTRIES    16UL

/* The common buffer for `n` descriptors each way: TX ring, RX ring, TX
 * buffers, RX buffers, each ring 16-byte aligned (Tables 12-17, 12-22) and
 * each buffer DW aligned (Tables 12-6, 12-7). */
#define TBT_RING_DMA_BYTES_N(n) ((TBT_DESC_BYTES + TBT_RING_BUF_BYTES) * 2UL * (ULONG)(n))
/* The ten-entry layout, 5440 bytes. */
#define TBT_RING_TX_DESC_OFF    0UL
#define TBT_RING_RX_DESC_OFF    (TBT_RING_TX_DESC_OFF + TBT_DESC_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_TX_BUF_OFF     (TBT_RING_RX_DESC_OFF + TBT_DESC_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_RX_BUF_OFF     (TBT_RING_TX_BUF_OFF + TBT_RING_BUF_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_DMA_BYTES      (TBT_RING_RX_BUF_OFF + TBT_RING_BUF_BYTES * TBT_RING_ENTRIES)
/* The USB4 profile's, 8704 bytes. */
#define TBT_RING_U4_DMA_BYTES   TBT_RING_DMA_BYTES_N(TBT_RING_U4_ENTRIES)
#define TBT_RING_DMA_ALIGN      16UL

/* How long a posted TX descriptor may stay unconsumed (u4hi.c allowed 1000
 * polls 10 us apart; this is a clock deadline instead). Record 22 section
 * 5.2 bounds it at 10 ms for the USB4 profile (TbtCtl). */
#define TBT_TX_CONSUME_MS       100UL
#define TBT_TX_CONSUME_U4_US    10000UL

/* A wait polls with 20 us stalls for its first 2 ms, then sleeps 1 ms per
 * step: record 21 section 4.3's busy-poll for a round trip of about 156 us,
 * without spinning a 10 s approve. */
#define TBT_WAIT_STALL_US       20UL
#define TBT_WAIT_BUSY_US        2000UL
#define TBT_WAIT_SLEEP_MS       1UL

/* One wait never takes more frames than this, so a host interface that
 * floods the RX ring cannot hold a wait forever even with a stopped clock. */
#define TBT_WAIT_FRAME_BUDGET   1024UL

/* TbtRingStart flags */
/* Host Interface Reset first. Never automatic: the ICM client does not
 * reset (ICM 6.1), and the USB4 profile never does (record 22 section
 * 4.4 step 2); a later caller must decide it explicitly. */
#define TBT_RING_START_RESET_HI 0x00000001UL
/* Mask ring interrupts through Interrupt Mask Clear, the specification's
 * route for a host whose status does not clear itself; without it the
 * mask register is written 0 directly, Intel's (tbt_regs.h). */
#define TBT_RING_START_MASK_IMC 0x00000002UL
/* Record 22 section 4.4 step 2, the takeover: before any descriptor
 * address is written, TX and RX ring 0 are disabled as Guide 5.7.3 says -
 * Ring Control to its default, Base Address Low and High, Ring Size and
 * the index register to 0 - and at least 1 ms (a minimum wait, section
 * 5.3) passes. Whether ring 0 read enabled, and how many other rings
 * did, is logged and counted; no other ring is touched. */
#define TBT_RING_START_TAKEOVER 0x00000004UL
/* Ring 0 settle after disabling, Guide 5.7.3 and 3.5 note 1. */
#define TBT_RING_SETTLE_US      1000UL

/* ---------------------------------------------------------------------- */
/* Frames                                                                 */
/* ---------------------------------------------------------------------- */

/* One received buffer, decoded to host-order DWs with its trailer removed. */
#define TBT_FRAME_CRC_BAD       0x00000001UL    /* the trailer did not match */
#define TBT_FRAME_OVERFLOW      0x00000002UL    /* BO set, or a length past the buffer */
#define TBT_FRAME_MALFORMED     0x00000004UL    /* shorter than 8 bytes or not whole DWs */

typedef struct _TBT_FRAME {
    ULONG Pdf;          /* EOF PDF of the RX descriptor */
    ULONG Words;        /* DWs in Dw[], trailer excluded; 0 when unusable */
    ULONG Flags;        /* TBT_FRAME_* */
    ULONG Length;       /* bytes the host interface reported */
    ULONG Dw[TBT_FRAME_DWORDS];
} TBT_FRAME, *PTBT_FRAME;

/* Called for a frame a control transfer did not wait for. It may record
 * and decode. The ICM client's must not send on the ring; the USB4
 * transport's (TbtCtlInit) sends acknowledgements, which are not requests
 * (Guide 4.2 limits outstanding requests, not acknowledgements). */
typedef VOID (*TBT_UNSOLICITED)(PVOID context, const TBT_FRAME *frame);

/* ---------------------------------------------------------------------- */
/* The ring                                                               */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_RING_COUNTERS {
    ULONG Tx;               /* descriptors posted */
    ULONG Rx;               /* buffers taken back */
    ULONG RxUsable;         /* of those, delivered with Words > 0 */
    ULONG CrcErrors;        /* control packets dropped, ICM frames flagged */
    ULONG Overflows;
    ULONG Malformed;
    ULONG TxFull;
    ULONG TxStuck;          /* a TX descriptor never consumed */
    ULONG CtlReads;
    ULONG CtlRetries;
    ULONG CtlTimeouts;
    ULONG CtlNotify;        /* error notifications answering a request */
    ULONG CtlStale;         /* responses whose echo did not match */
    ULONG Unsolicited;      /* frames handed to the unsolicited callback */
    ULONG CtlWrites;
    ULONG Ring0WasEnabled;  /* takeover: ring 0 read enabled (TX or RX) */
    ULONG OtherRingsEnabled;/* takeover: rings 1 and up read enabled, left alone */
    ULONG StopUnverified;   /* a stop whose read-back did not show both
                               controls disabled: DmaMayRun kept */
} TBT_RING_COUNTERS;

typedef struct _TBT_RING {
    const TBT_HOST *Host;
    TBT_UNSOLICITED Unsolicited;
    PVOID UnsolicitedContext;

    volatile UCHAR *Va;     /* the common buffer, TBT_RING_DMA_BYTES_N(Entries) */
    ULONG Phys;             /* its bus address, below 4 GB */
    ULONG Entries;          /* descriptors each way, set at start */

    ULONG Paths;            /* Total Paths read at start */
    ULONG TxNext;           /* next TX descriptor to fill */
    ULONG RxNext;           /* next RX descriptor to examine */
    ULONG Seq;              /* 2-bit Sequence Number, Spec 6.4.2.3 */
    ULONG TxConsumeUs;      /* a request's consumption bound; 0: TBT_TX_CONSUME_MS */
    BOOLEAN Running;
    BOOLEAN Programmed;     /* the last start wrote ring registers */
    /* Set before the first write that enables a ring, cleared only by a
     * stop whose read-back, after the settle, shows both ring 0 controls
     * readable and disabled: until then the host interface may still DMA
     * into the common buffer, which is therefore never freed (cm_pool.c). */
    BOOLEAN DmaMayRun;

    /* The error notification that ended the last request with
     * TBT_E_NOTIFY (Spec 6.4.3): its event code, Event Info and the route
     * of the router that sent it. */
    ULONG LastEvent;
    ULONG LastEventInfo;
    ULONG LastEventRouteHi;
    ULONG LastEventRouteLo;

    TBT_RING_COUNTERS Counters;

    /* Every wait the ring's own loops took, summed (they only grow; a
     * caller takes differences): the waited-sum backstop of a caller's
     * budget when the clock does not move. */
    ULONG WaitedUs;
} TBT_RING, *PTBT_RING;

/* A route string as two DWs (Spec 6.4.2.2); level n is byte n of the 56-bit
 * TopologyID, so no 64-bit type is needed. */
typedef struct _TBT_ROUTE {
    ULONG Hi;               /* bits 63:32; only 55:32 are sent */
    ULONG Lo;               /* bits 31:0 */
} TBT_ROUTE, *PTBT_ROUTE;

/* A deadline: a clock limit, and the sum of the waits asked for, which is a
 * lower bound of the time passed, so either one ending the wait is safe and
 * a stopped clock cannot hang it. */
typedef struct _TBT_DEADLINE {
    ULONG StartMs;
    ULONG LimitMs;
    ULONG WaitedUs;
} TBT_DEADLINE, *PTBT_DEADLINE;

/*
 * A wait on the microsecond clock (record 22 section 5.3), of one of two
 * kinds. A MAXIMUM (a timeout, a deadline) has passed when the measured
 * difference reaches it, which may be up to a tick late, or when the waits
 * asked for add up to it. A MINIMUM (a settle, a hold, a gap) is satisfied
 * only when the measured difference reaches it plus one tick, because a
 * difference of n ticks may be as little as n - 1 ticks of real time, or
 * when the waits asked for add up to it - each wait takes at least what it
 * asked for, so that sum never runs ahead of real time.
 */
typedef struct _TBT_WAIT {
    ULONG StartUs;
    ULONG LimitUs;
    ULONG WaitedUs;
    BOOLEAN Minimum;
} TBT_WAIT, *PTBT_WAIT;

/* ---------------------------------------------------------------------- */
/* API                                                                    */
/* ---------------------------------------------------------------------- */

/* The CRC of Spec 6.4.2.2 over `length` bytes. Any IRQL. */
ULONG TbtCrc32c(const UCHAR *bytes, ULONG length);

/* The deadline helpers every bounded wait of the ICM client is built on. */
VOID    TbtDeadlineStart(PTBT_DEADLINE d, const TBT_HOST *host, ULONG limitMs);
BOOLEAN TbtDeadlinePassed(const TBT_DEADLINE *d, const TBT_HOST *host);
VOID    TbtDeadlineWait(PTBT_DEADLINE d, const TBT_HOST *host);

/* The microsecond clock (ClockUs, or NowMs times 1000). */
ULONG   TbtClockUs(const TBT_HOST *host);
/* The waits every bound of record 22 section 5.2 is measured with.
 * TbtWaitDone: a maximum has passed, a minimum is satisfied. TbtWaitStep
 * waits `stepUs` (0: the default pacing of TbtDeadlineWait) - a stall
 * below a millisecond, a thread sleep from one up - and adds it to the
 * sum. TbtWaitMin waits out a whole minimum. */
VOID    TbtWaitStart(PTBT_WAIT w, const TBT_HOST *host, ULONG limitUs,
                     BOOLEAN minimum);
BOOLEAN TbtWaitDone(const TBT_WAIT *w, const TBT_HOST *host);
VOID    TbtWaitStep(PTBT_WAIT w, const TBT_HOST *host, ULONG stepUs);
VOID    TbtWaitMin(const TBT_HOST *host, ULONG minimumUs);
/* Microseconds since `w` started, by the clock. */
ULONG   TbtWaitElapsedUs(const TBT_WAIT *w, const TBT_HOST *host);

/* Route-string helpers (Spec 6.4.2.2, Figure 6-5). Any IRQL. */
ULONG   TbtRouteDepth(const TBT_ROUTE *route);      /* levels before the first 0 */
ULONG   TbtRouteLevel(const TBT_ROUTE *route, ULONG level); /* that level's Adapter # */
BOOLEAN TbtRouteEqual(const TBT_ROUTE *a, const TBT_ROUTE *b); /* TopologyID only */
/* `parent` extended by one level through `adapter` (Spec 6.4.2.2): FALSE
 * when the parent is already 7 levels deep or `adapter` is 0. */
BOOLEAN TbtRouteChild(const TBT_ROUTE *parent, ULONG adapter, PTBT_ROUTE child);

/*
 * Start ring 0: read Host Interface Capabilities (all ones, or 0 or more
 * than 21 paths, is TBT_E_HW), optionally reset the host interface or take
 * ring 0 over, mask every ring interrupt as `flags` says, carve `va`/`phys`
 * (TBT_RING_DMA_BYTES, 16-byte aligned), program TX ring 0 and RX ring 0 in
 * Raw Mode with `rxPdfMask`, and post every RX descriptor but one. `va`
 * must stay valid until TbtRingStop. Restartable: a second start after a
 * stop reprograms the same block from scratch. Ten descriptors each way.
 */
LONG TbtRingStart(PTBT_RING ring,
                  const TBT_HOST *host,
                  PVOID va,
                  ULONG phys,
                  ULONG bytes,
                  ULONG rxPdfMask,
                  ULONG flags);

/* The same with `entries` descriptors each way (2 to TBT_RING_MAX_ENTRIES)
 * and a block of TBT_RING_DMA_BYTES_N(entries). */
LONG TbtRingStartEx(PTBT_RING ring,
                    const TBT_HOST *host,
                    PVOID va,
                    ULONG phys,
                    ULONG bytes,
                    ULONG rxPdfMask,
                    ULONG flags,
                    ULONG entries);

/* Stop ring 0: both Ring Control registers to their default 0, then the RX
 * Consumer Index to its default (Spec 12.6.3.3.3's note). Idempotent. */
VOID TbtRingStop(PTBT_RING ring);

/* The receiver of frames a control transfer does not want. */
VOID TbtRingSetUnsolicited(PTBT_RING ring, TBT_UNSOLICITED fn, PVOID context);

/*
 * Post one buffer of `words` DWs with PDF `pdf`, its CRC-32C trailer
 * appended (Raw Mode: the host interface adds none, Spec 12.3.2.2), and
 * wait up to TBT_TX_CONSUME_MS for the host interface to consume it (the
 * Consumer Index passing it, or DD). TBT_E_PARAM for a frame past the
 * buffer or a PDF above Eh, TBT_E_RING_FULL with no vacant descriptor,
 * TBT_E_HW if it is never consumed, TBT_E_STOPPED before a start.
 */
LONG TbtRingSend(PTBT_RING ring, ULONG pdf, const ULONG *dw, ULONG words);
/* The same, with the consumption bound in microseconds. */
LONG TbtRingSendUs(PTBT_RING ring, ULONG pdf, const ULONG *dw, ULONG words,
                   ULONG consumeUs);

/*
 * Take the next completed RX buffer, if any, into `frame` and give the
 * descriptor back. Returns TRUE when one was taken (Words may be 0 for a
 * frame that is unusable; its Flags say why), FALSE when none is pending.
 * A control packet (PDF below ICM event) with a bad trailer is dropped
 * (Words 0); an ICM frame's trailer is checked and flagged but the frame is
 * delivered, as record 21 section 5.2 has the client strip it unchecked.
 */
BOOLEAN TbtRingReceive(PTBT_RING ring, PTBT_FRAME frame);

/*
 * Read `count` DWs (1..60) of configuration space `cs`, adapter `adapter`,
 * DW address `address`, of the router at `route` (Spec 6.4.2.3), into
 * `out`. Each attempt takes a fresh Sequence Number and waits `timeoutMs`;
 * a timeout is retried up to `retries` more times. An error notification
 * whose code fails a request (TbtCtlClassify: ERR_CONN, ERR_ADDR, ERR_ADP,
 * ERR_ENUM, ERR_NUA, ERR_LEN, ERR_LOCK) ends it at once with TBT_E_NOTIFY
 * and LastEvent set; any other notification is an event and goes, like a
 * response with another route or PDF, to the unsolicited callback while
 * the wait goes on. A response whose echo does not match is a stale answer
 * to an earlier attempt and is dropped.
 */
LONG TbtRingCtlRead(PTBT_RING ring,
                    const TBT_ROUTE *route,
                    ULONG cs,
                    ULONG adapter,
                    ULONG address,
                    ULONG count,
                    PULONG out,
                    ULONG timeoutMs,
                    ULONG retries);

/*
 * The general form (Spec 6.4.2.3 to 6.4.2.6): `pdf` TBT_PDF_READ or
 * TBT_PDF_WRITE, `buf` the DWs read or the DWs to write, each attempt
 * bounded by `timeoutUs` on the microsecond clock (a maximum), `attempts`
 * in all (at least one), each request's descriptor consumed within
 * TxConsumeUs. A response echoes the request's Address, Size, CS and SN,
 * and its Adapter Num too outside the router space (Spec Tables 6-3 and
 * 6-5). In the router space the Adapter Num of a Read Response is the
 * arrival adapter (Table 6-3); a Write Response's should echo the
 * request's 0 (Table 6-5), but usb4-emu's fabric answers both with the
 * arrival adapter (hw/usb4/usb4-fabric.c send_response), so in that
 * space alone it is not compared - the route, address, size, space and
 * sequence number still are. A Write Response carries no data.
 */
LONG TbtRingCtlTransfer(PTBT_RING ring,
                        ULONG pdf,
                        const TBT_ROUTE *route,
                        ULONG cs,
                        ULONG adapter,
                        ULONG address,
                        ULONG count,
                        PULONG buf,
                        ULONG timeoutUs,
                        ULONG attempts);

/* ---------------------------------------------------------------------- */
/* The USB4 control transport (record 22 sections 5.1 to 5.3, 7.4)        */
/* ---------------------------------------------------------------------- */

/* Record 22 section 5.1: 100 ms per attempt, 4 attempts, a fresh sequence
 * number each (Linux tb.c TB_TIMEOUT, ctl.c TB_CTL_RETRIES). */
#define TBT_CTL_TIMEOUT_US      100000UL
#define TBT_CTL_ATTEMPTS        4UL
#define TBT_CTL_HP_QUEUE        16UL
#define TBT_CTL_NOTE_QUEUE      16UL
#define TBT_CTL_DEFER_QUEUE     16UL
#define TBT_CTL_EVENT_CODES     40UL    /* counted by code, 0 to 39 */

/* TbtCtlClassify: what a notification's event code means (record 22
 * section 5.1's table; Spec Table 6-12, 6.4.3; Guide 7.7.2). */
#define TBT_NOTE_REQUEST        1UL     /* fails the outstanding request */
#define TBT_NOTE_EVENT          2UL     /* ERR_LINK, HEC, FC, PLUG: counted, acknowledged */
#define TBT_NOTE_ACK_IGNORE     3UL     /* DP_BW, PCIE_WAKE, DP_CON_CHANGE, DPTX_DISCOVERY */
#define TBT_NOTE_SILENT         4UL     /* ROP_CMPLT, POP_CMPLT, LINK_RECOVERY, ASYM_LINK: no acknowledgement */
#define TBT_NOTE_HP_ACK         5UL     /* a hot plug acknowledgement: only a CM sends one */
#define TBT_NOTE_UNKNOWN        6UL     /* a reserved code: counted, not acknowledged */

/* The acknowledgement form a router's notifications take (record 22
 * section 6.4), the connection manager's answer to TBT_CTL_ACK_FORM. */
#define TBT_ACK_PLAIN           0UL     /* Spec 6.4.2.8 */
#define TBT_ACK_ENHANCED        1UL     /* Spec 6.4.2.9: Sequence Bit capability enabled */
#define TBT_ACK_PENDING         2UL     /* Get Capabilities not answered yet: held */
#define TBT_ACK_NONE            3UL     /* capability state unknown: counted, never acknowledged */

typedef ULONG (*TBT_CTL_ACK_FORM)(PVOID context, const TBT_ROUTE *route);
/* A safe point (record 22 section 7.4): called by the connection manager's
 * own sequences between requests, never inside a transport wait. */
typedef VOID (*TBT_CTL_SAFE_POINT)(PVOID context);

/* A hot plug event, decoded and acknowledged (Spec 6.4.2.10). */
typedef struct _TBT_HP_EVENT {
    TBT_ROUTE Route;        /* the router that sent it, CM bit cleared */
    ULONG Adapter;
    BOOLEAN Unplug;
} TBT_HP_EVENT, *PTBT_HP_EVENT;

/* A notification the connection manager has to see: ERR_CONN, ERR_LOCK,
 * ERR_ENUM or ERR_NUA that arrived with no request waiting for it, and
 * ERR_PLUG. */
typedef struct _TBT_NOTE {
    TBT_ROUTE Route;
    ULONG Code;
    ULONG Info;
    BOOLEAN Sq;
} TBT_NOTE, *PTBT_NOTE;

typedef struct _TBT_CTL_COUNTERS {
    ULONG Requests;
    ULONG Retries;
    ULONG Timeouts;
    ULONG Notified;         /* requests ended by a request-error notification */
    ULONG Events[TBT_CTL_EVENT_CODES];
    ULONG EventsOther;      /* codes 40 and up */
    ULONG AcksPlain;
    ULONG AcksEnhanced;
    ULONG AcksWithheld;     /* TBT_ACK_NONE */
    ULONG AcksDeferred;     /* TBT_ACK_PENDING, held */
    ULONG AckFailures;
    ULONG HotPlugs;
    ULONG HotUnplugs;
    ULONG HotPlugDuplicates;
    ULONG HotPlugOverflows;
    ULONG HotPlugAckFailures;
    ULONG NoteOverflows;
    ULONG Strays;           /* frames nobody wanted */
    ULONG SafePoints;
} TBT_CTL_COUNTERS;

typedef struct _TBT_CTL {
    PTBT_RING Ring;
    ULONG TimeoutUs;
    ULONG Attempts;

    TBT_CTL_ACK_FORM AckForm;
    PVOID AckContext;
    TBT_CTL_SAFE_POINT SafePoint;
    PVOID SafeContext;
    BOOLEAN InSafePoint;

    TBT_HP_EVENT HotPlug[TBT_CTL_HP_QUEUE];
    ULONG HpHead;
    ULONG HpCount;
    BOOLEAN HpLost;         /* an event did not fit: every port wants a check */

    TBT_NOTE Notes[TBT_CTL_NOTE_QUEUE];
    ULONG NoteHead;
    ULONG NoteCount;
    BOOLEAN NotesLost;

    TBT_NOTE Deferred[TBT_CTL_DEFER_QUEUE];     /* acknowledgements held */
    ULONG DeferCount;

    TBT_CTL_COUNTERS Counters;
} TBT_CTL, *PTBT_CTL;

/* What an event code means. Any IRQL. */
ULONG TbtCtlClassify(ULONG code);

/* Bind `ctl` to a started or stopped `ring` and install its receiver of
 * unsolicited frames; record 22's timeout and attempts, and its 10 ms bound
 * on a request descriptor's consumption (section 5.2). */
VOID TbtCtlInit(PTBT_CTL ctl, PTBT_RING ring);
VOID TbtCtlSetAckForm(PTBT_CTL ctl, TBT_CTL_ACK_FORM fn, PVOID context);
VOID TbtCtlSetSafePoint(PTBT_CTL ctl, TBT_CTL_SAFE_POINT fn, PVOID context);

/* One read or write request of record 22 section 5.1 (TbtRingCtlTransfer
 * with the transport's bounds). */
LONG TbtCtlRead(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs, ULONG adapter,
                ULONG address, ULONG count, PULONG out);
LONG TbtCtlWrite(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs,
                 ULONG adapter, ULONG address, ULONG count, const ULONG *in);
/* One DW. */
LONG TbtCtlRead1(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs,
                 ULONG adapter, ULONG address, PULONG out);
LONG TbtCtlWrite1(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs,
                  ULONG adapter, ULONG address, ULONG value);

/* Take every frame the RX ring holds, acknowledging and queueing as the
 * receiver does; sends acknowledgements only. */
VOID TbtCtlService(PTBT_CTL ctl);
/* The same, at most `maxFrames` frames: what is left stays on the ring for
 * the next call (an acknowledgement owed is never dropped). */
VOID TbtCtlServiceN(PTBT_CTL ctl, ULONG maxFrames);

/* The queues, oldest first. */
BOOLEAN TbtCtlNextHotPlug(PTBT_CTL ctl, PTBT_HP_EVENT out);
BOOLEAN TbtCtlNextNote(PTBT_CTL ctl, PTBT_NOTE out);

/* A router's acknowledgement form is settled (record 22 section 6.4):
 * the notifications held for `route` are acknowledged in that form, or
 * dropped uncounted-as-acknowledged under TBT_ACK_NONE. */
VOID TbtCtlSettle(PTBT_CTL ctl, const TBT_ROUTE *route);
/* Forget what is held for routes at or below `route` (an unplug). */
VOID TbtCtlForget(PTBT_CTL ctl, const TBT_ROUTE *route);

/* The safe point: the callback, unless one is already running. */
VOID TbtCtlSafePoint(PTBT_CTL ctl);

#endif /* TBT_RING_H */
