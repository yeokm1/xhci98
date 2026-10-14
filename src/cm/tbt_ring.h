/*
 * tbt_ring.h - the host interface's ring 0, its pure half: transmit and
 * receive in Raw Mode, CRC-32C, and the control-packet read with sequence
 * numbers, timeouts and retries (roadmap-usb4.md task 39-A.2; design record
 * 21 sections 4.2 and 4.3).
 *
 * Lifted from usb4-emu's tools/usb4cm/core/u4hi.c (MIT; its notice is in
 * tbt_ring.c) and rewritten over an injected accessor, TBT_HOST: the driver
 * supplies BAR0 reads and writes, the waits, a millisecond clock and the
 * log, and owns the common buffer it hands to TbtRingStart; the host suite
 * (test\test_tbt_ring.c) supplies a modelled host interface. That seam is
 * u4hi.c's own callback contract (mmio_rd, mmio_wr, dma_alloc, udelay, log),
 * kept as record 21 section 4.2 asks, with the allocation moved out: the
 * driver allocates once at start through cm_pool.c and passes the block in.
 *
 * Naming: this directory's exported functions take the Tbt prefix and its
 * file-local ones tbt, its types TBT_, as xhciuas.sys's take Uas and uas.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic, no floating
 * point. The upper 32 bits of every hardware address are written 0.
 *
 * IRQL and locking, for every function here that takes a TBT_RING or a
 * TBT_HOST: PASSIVE_LEVEL, on the connection-manager thread only (record
 * 21 section 4.4), because the waits it makes through TBT_HOST.Sleep are
 * thread waits; no lock is taken and none may be held by the caller, since
 * that thread is the only one that touches the rings. TbtCrc32c and the
 * TbtRoute helpers are pure and callable at any IRQL.
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
 */
typedef struct _TBT_HOST {
    PVOID Context;
    ULONG (*Read32)(PVOID context, ULONG offset);
    VOID  (*Write32)(PVOID context, ULONG offset, ULONG value);
    VOID  (*Stall)(PVOID context, ULONG microseconds);
    VOID  (*Sleep)(PVOID context, ULONG milliseconds);
    ULONG (*NowMs)(PVOID context);
    VOID  (*Log)(PVOID context, const char *label, ULONG value);
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

/* ---------------------------------------------------------------------- */
/* Ring geometry (record 21 section 4.2)                                  */
/* ---------------------------------------------------------------------- */

/* 10 descriptors each way and 256-byte buffers, the Alpine Ridge client's
 * own geometry [obs] ICM 2.1 (ring sizes 0x0000000a and 0x0100000a). */
#define TBT_RING_ENTRIES        10UL
#define TBT_RING_BUF_BYTES      256UL
#define TBT_FRAME_DWORDS        (TBT_RING_BUF_BYTES / 4UL)

/* The common buffer: TX ring, RX ring, TX buffers, RX buffers, each ring
 * 16-byte aligned (Tables 12-17, 12-22) and each buffer DW aligned (Tables
 * 12-6, 12-7). 5440 bytes. */
#define TBT_RING_TX_DESC_OFF    0UL
#define TBT_RING_RX_DESC_OFF    (TBT_RING_TX_DESC_OFF + TBT_DESC_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_TX_BUF_OFF     (TBT_RING_RX_DESC_OFF + TBT_DESC_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_RX_BUF_OFF     (TBT_RING_TX_BUF_OFF + TBT_RING_BUF_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_DMA_BYTES      (TBT_RING_RX_BUF_OFF + TBT_RING_BUF_BYTES * TBT_RING_ENTRIES)
#define TBT_RING_DMA_ALIGN      16UL

/* How long a posted TX descriptor may stay unconsumed (u4hi.c allowed 1000
 * polls 10 us apart; this is a clock deadline instead). */
#define TBT_TX_CONSUME_MS       100UL

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
 * reset (ICM 6.1), and a later caller must decide it explicitly. */
#define TBT_RING_START_RESET_HI 0x00000001UL
/* Mask ring interrupts through Interrupt Mask Clear, the specification's
 * route for a host whose status does not clear itself; without it the
 * mask register is written 0 directly, Intel's (tbt_regs.h). */
#define TBT_RING_START_MASK_IMC 0x00000002UL

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

/* Called for a frame a control-packet read did not wait for. It may record
 * and decode; it must not send on the ring. */
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
    ULONG CtlNotify;        /* error notifications answering a read */
    ULONG CtlStale;         /* responses whose echo did not match */
    ULONG Unsolicited;      /* frames handed to the unsolicited callback */
} TBT_RING_COUNTERS;

typedef struct _TBT_RING {
    const TBT_HOST *Host;
    TBT_UNSOLICITED Unsolicited;
    PVOID UnsolicitedContext;

    volatile UCHAR *Va;     /* the common buffer, TBT_RING_DMA_BYTES */
    ULONG Phys;             /* its bus address, below 4 GB */

    ULONG Paths;            /* Total Paths read at start */
    ULONG TxNext;           /* next TX descriptor to fill */
    ULONG RxNext;           /* next RX descriptor to examine */
    ULONG Seq;              /* 2-bit Sequence Number, Spec 6.4.2.3 */
    BOOLEAN Running;

    TBT_RING_COUNTERS Counters;
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

/* ---------------------------------------------------------------------- */
/* API                                                                    */
/* ---------------------------------------------------------------------- */

/* The CRC of Spec 6.4.2.2 over `length` bytes. Any IRQL. */
ULONG TbtCrc32c(const UCHAR *bytes, ULONG length);

/* The deadline helpers every bounded wait in this directory is built on. */
VOID    TbtDeadlineStart(PTBT_DEADLINE d, const TBT_HOST *host, ULONG limitMs);
BOOLEAN TbtDeadlinePassed(const TBT_DEADLINE *d, const TBT_HOST *host);
VOID    TbtDeadlineWait(PTBT_DEADLINE d, const TBT_HOST *host);

/* Route-string helpers (Spec 6.4.2.2, Figure 6-5). Any IRQL. */
ULONG   TbtRouteDepth(const TBT_ROUTE *route);      /* levels before the first 0 */
ULONG   TbtRouteLevel(const TBT_ROUTE *route, ULONG level); /* that level's Adapter # */
BOOLEAN TbtRouteEqual(const TBT_ROUTE *a, const TBT_ROUTE *b); /* TopologyID only */

/*
 * Start ring 0: read Host Interface Capabilities (all ones, or 0 or more
 * than 21 paths, is TBT_E_HW), optionally reset the host interface, mask
 * every ring interrupt as `flags` says, carve `va`/`phys` (TBT_RING_DMA_BYTES, 16-byte
 * aligned), program TX ring 0 and RX ring 0 in Raw Mode with `rxPdfMask`,
 * and post every RX descriptor but one. `va` must stay valid until
 * TbtRingStop. Restartable: a second start after a stop reprograms the same
 * block from scratch.
 */
LONG TbtRingStart(PTBT_RING ring,
                  const TBT_HOST *host,
                  PVOID va,
                  ULONG phys,
                  ULONG bytes,
                  ULONG rxPdfMask,
                  ULONG flags);

/* Stop ring 0: both Ring Control registers to their default 0, then the RX
 * Consumer Index to its default (Spec 12.6.3.3.3's note). Idempotent. */
VOID TbtRingStop(PTBT_RING ring);

/* The receiver of frames a control read does not want. */
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
 * a timeout is retried up to `retries` more times; an error notification
 * ends the read at once (TBT_E_NOTIFY). A response with another route or
 * PDF goes to the unsolicited callback; one whose echo does not match is
 * a stale answer to an earlier attempt and is dropped.
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

#endif /* TBT_RING_H */
