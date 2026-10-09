/*
 * xhci_xstat.h - the transfer-size instrumentation and the command-size
 * value, their pure half (roadmap-hcd.md tasks 36.21 and 36.22).
 *
 * 36.21 measures what a large command costs before anything is changed:
 * the data URBs' lengths, how many chunks each became (hcd_io.c,
 * hcdPlanChunk), the map grant, and the gap between two chunks of one URB
 * in its two parts - software's time from a chunk's retirement to the next
 * chunk's publication, and the interval the ring stood empty in software's
 * view with that URB unfinished - plus the processor time the chunk path
 * spends. The bus has no clock finer than the system time's tick on every
 * target (KeQuerySystemTime is the one time import), so intervals are
 * processor time-stamp counts (hcd.h, HCD_TSC), and the start's and the
 * snapshot's (time-stamp, system time) pairs let a reader convert them.
 * Hardware starvation itself is not measured: a Transfer Event carries no
 * time, and the interval starts when software handles the completion.
 *
 * 36.22's value, XhciMaxTransferKB: the command size xhciuas.sys
 * advertises, 64 to 2048 KiB in steps of 4, default 1024; read once at the
 * controller's start (hcd_ctl.c) and answered through
 * IOCTL_XHCI98_TRANSFER_POLICY (xhci98_xfer.h).
 *
 * Everything here is ULONGs, so the snapshot copies XHCI_XSTAT word for
 * word into the HCD region's header (xhci.h, XHCI_SNAPSHOT_HCD_XSTAT_AT).
 * Sums wider than 32 bits are Lo/Hi pairs carried by hand: no 64-bit
 * arithmetic (AGENTS.md).
 *
 * DDK-free: part of the pure core. C89. IRQL: any; the callers hold the
 * controller lock over every update of one XHCI_XSTAT.
 */

#ifndef XHCI_XSTAT_H
#define XHCI_XSTAT_H

#include "xhci_compat.h"

/* Length buckets: up to 4 KiB, 16, 64, 128, 256, 512 KiB, 1 MiB, 2 MiB,
 * and above (xhci98_xfer.h's XHCI98_TELE_BUCKETS, the same nine). */
#define XHCI_XSTAT_BUCKETS          9UL
/* Chunks per URB: 1, 2, 3-8, 9-16, 17 and more. */
#define XHCI_XSTAT_CHUNK_BUCKETS    5UL
/* Commands in flight: 1, 2, 3-4, 5-8, 9-16, 17 and more. */
#define XHCI_XSTAT_DEPTH_BUCKETS    6UL

/* An interval longer than this many counts is not a gap but a counter
 * that went backwards or a processor whose counter is not this one's: it
 * is discarded and counted (XHCI_XSTAT.Discarded). */
#define XHCI_XSTAT_INTERVAL_MAX     0x7FFFFFFFUL

/* One measured interval: how many, their sum, the longest. */
typedef struct _XHCI_XSTAT_TIME {
    ULONG Count;
    ULONG SumLo;
    ULONG SumHi;
    ULONG Max;
} XHCI_XSTAT_TIME, *PXHCI_XSTAT_TIME;

/*
 * The controller's transfer-size readings since its start. The order is
 * the snapshot's (XHCISNAP names the words in this order; test_snap holds
 * the count): append, never reorder.
 */
typedef struct _XHCI_XSTAT {
    /* The value (36.22): what the start read and what it took. */
    ULONG ValueWhy;             /* XHCI_XPOL_WHY_*                        */
    ULONG ValueRaw;             /* the DWORD read, 0 when none was        */
    ULONG CommandBytes;         /* the command size in effect             */
    ULONG PolicyQueries;        /* IOCTL_XHCI98_TRANSFER_POLICY answered  */
    /* The map grant: what IoGetDmaAdapter gave, and what a chunk may span
     * (the grant, held to the SG elements a record carries). */
    ULONG MapGrant;
    ULONG ChunkPages;
    /* Bulk and interrupt data URBs completed, by length. */
    ULONG Urbs;
    ULONG BytesLo;
    ULONG BytesHi;
    ULONG LengthMax;
    ULONG Hist[XHCI_XSTAT_BUCKETS];
    /* Their chunks: published, the URBs that took more than one, the most
     * one took, and the URBs by count. */
    ULONG Chunks;
    ULONG MultiChunk;
    ULONG ChunksMax;
    ULONG ChunkHist[XHCI_XSTAT_CHUNK_BUCKETS];
    /* The gap's two parts (time-stamp counts): a chunk's retirement to the
     * next chunk's publication, and the ring empty in software's view with
     * its URB unfinished, to the next publication on that ring. */
    XHCI_XSTAT_TIME Gap;
    XHCI_XSTAT_TIME Empty;
    /* Processor time: a data chunk's publication (HcdIoMapped), and one
     * pass of the deferred work over a retired data record (HcdIoDeferred,
     * the next chunk's planning and queueing or the URB's completion). */
    XHCI_XSTAT_TIME Publish;
    XHCI_XSTAT_TIME Complete;
    ULONG Discarded;            /* intervals out of range                 */
    /* The clocks, for converting the counts: the start's pair, and the
     * pair the snapshot reads (written by the door as it copies). */
    ULONG TscStartLo;
    ULONG TscStartHi;
    ULONG TimeStartLo;          /* KeQuerySystemTime, 100 ns              */
    ULONG TimeStartHi;
    ULONG TscNowLo;
    ULONG TscNowHi;
    ULONG TimeNowLo;
    ULONG TimeNowHi;
    /* The class driver's records (HCD_UAS_TELE): sends taken, and sends
     * refused (malformed, or every record held by another device). */
    ULONG TeleTaken;
    ULONG TeleRefused;
} XHCI_XSTAT, *PXHCI_XSTAT;

#define XHCI_XSTAT_WORDS            54UL

/* How the start's XhciMaxTransferKB read went. */
#define XHCI_XPOL_WHY_UNREAD        0UL /* no start has read it           */
#define XHCI_XPOL_WHY_USED          1UL /* a DWORD in range, on a step    */
#define XHCI_XPOL_WHY_ABSENT        2UL /* no such value: the default     */
#define XHCI_XPOL_WHY_TYPE          3UL /* not a four-byte DWORD          */
#define XHCI_XPOL_WHY_RANGE         4UL /* below 64 or above 2048         */
#define XHCI_XPOL_WHY_STEP          5UL /* not a multiple of 4            */

/* What the registry read found, for XhciXpolValue. */
#define XHCI_XPOL_FOUND_NONE        0UL
#define XHCI_XPOL_FOUND_DWORD       1UL
#define XHCI_XPOL_FOUND_OTHER       2UL /* present, another type or size  */

#define XHCI_XPOL_MIN_KB            64UL
#define XHCI_XPOL_MAX_KB            2048UL
#define XHCI_XPOL_STEP_KB           4UL
#define XHCI_XPOL_DEFAULT_KB        1024UL

/* The command size, in bytes, for what the start read (`found` an
 * XHCI_XPOL_FOUND_*, `value` the DWORD when there was one); *why says
 * which way it went. Never outside 64 KiB to 2 MiB. */
ULONG XhciXpolValue(ULONG found, ULONG value, PULONG why);

/* The bucket of a length (0 to XHCI_XSTAT_BUCKETS - 1), of a URB's chunk
 * count (0 to XHCI_XSTAT_CHUNK_BUCKETS - 1), and of a number of commands
 * in flight (0 to XHCI_XSTAT_DEPTH_BUCKETS - 1; 0 reads as 1). */
ULONG XhciXstatBucket(ULONG bytes);
ULONG XhciXstatChunkBucket(ULONG chunks);
ULONG XhciXstatDepthBucket(ULONG active);

/* *lo/*hi += v, the carry by hand. */
VOID XhciXstatAdd64(PULONG lo, PULONG hi, ULONG v);

/* `to - from` as a count, into *t when it is an interval at all (at most
 * XHCI_XSTAT_INTERVAL_MAX); otherwise *discarded is bumped. Either
 * argument 0 means "not stamped" and records nothing. Returns 1 when
 * recorded. */
ULONG XhciXstatInterval(PXHCI_XSTAT_TIME t, PULONG discarded, ULONG from,
                        ULONG to);

/* A data URB's end: its length and the chunks published for it. */
VOID XhciXstatUrb(PXHCI_XSTAT s, ULONG length, ULONG chunks);

#endif /* XHCI_XSTAT_H */
