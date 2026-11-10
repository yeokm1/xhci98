/*
 * cm_rirq.h - the ring interrupt of the NHI (design record 21 section 4.5,
 * record 22 section 5.4; roadmap-usb4.md task 45.22), its pure half: what
 * reads no OS state, kept apart so the host suite (test\test_cm_rirq.c)
 * reaches it. The driver half - the service routine, its DPC, the
 * connection, the arm, the self-test and the order down and up - is
 * cm_rirqos.c.
 *
 * WHAT IT IS. RX ring 0's Receive Data Buffer cause (bit N + 0 of the
 * interrupt status for N = Total Paths, Spec 12.5.1, Figure 12-8) ends the
 * connection-manager thread's idle wait early; the timed wait stays at its
 * period and polling stays the fallback. Here:
 *
 *   - the cause's geometry: its DW and bit, and the status DWs to read;
 *   - the service routine's outcome for the status it read;
 *   - its storm count (64 claims with no DPC between);
 *   - the mask's write by the host's contract and its read-back's proof;
 *   - the watchdog's suspicion and the slower storm, at each maintenance;
 *   - the end of the order down: disconnected, retained or disconnected
 *     unproven, and whether the activation then falls back for good;
 *   - whether the interrupt is wanted, and the wait to a deadline;
 *   - the networking data ring's Receive Data Buffer cause beside ring
 *     0's (record 35 section 5.5, task 48.38): its bit, the causes a
 *     status shows, the acknowledgement with auto-clear disabled, the
 *     fast storm's mask of both, and the per-cause slow-storm watchdog.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any (the service routine
 * calls CmRirqClassify and CmRirqBurstClaim at DIRQL).
 */

#ifndef CM_RIRQ_H
#define CM_RIRQ_H

#include "../xhci_compat.h"
#include "tbt_regs.h"

/* XhciCmRingInterrupt (record 21 section 4.5.4; cm_vals.h): 0 polling only,
 * as before; 1 the line interrupt where granted and proven, polling
 * otherwise. 2 and above are reserved (the message step) and refused. */
#define CM_RIRQ_VALUE_OFF        0UL
#define CM_RIRQ_VALUE_LINE       1UL
#define CM_RIRQ_VALUE_MAX        1UL
#define CM_RIRQ_VALUE_DEFAULT    CM_RIRQ_VALUE_OFF

/* IrqMode, the counter: what the activation runs on. */
#define CM_RIRQ_MODE_POLL        0UL
#define CM_RIRQ_MODE_LINE        1UL
#define CM_RIRQ_MODE_MESSAGE     2UL

/* Bounds (record 21 sections 4.5.3 and 4.5.5). */
#define CM_RIRQ_STORM_CLAIMS     64UL   /* claims with no DPC between      */
#define CM_RIRQ_SLOW_STORM       1000UL /* claims in one idle period with
                                           no frame drained              */
#define CM_RIRQ_SELFTEST_MS      100UL  /* the self-test's wait            */
#define CM_RIRQ_DPC_SLOW_MS      100UL  /* the down order's DPC drain, logged
                                           once it passes this            */
#define CM_RIRQ_SUSPECT_RUN      2UL    /* suspected misses in a row       */
/* ITR[0] while the networking data ring's cause is open, in 256 ns units:
 * 500, 128 us, Linux's tbnet_open throttle (record 35 section 5.5); 0, no
 * moderation, otherwise. */
#define CM_RIRQ_ITR_DATA         500UL

/* The most status DWs a host has: 3 x 21 causes. */
#define CM_RIRQ_MAX_DWORDS       TBT_HI_IMR_DWORDS(TBT_HI_CAPS_PATHS_MAX)

/* The cause and the status it lives in; the data ring's cause beside it
 * while that is open (record 35 section 5.5), DataBit 0 otherwise. */
typedef struct _CM_RIRQ_GEOM {
    ULONG Paths;            /* Total Paths, N                              */
    ULONG Dwords;           /* status DWs, ceiling(3N / 32)                */
    ULONG CauseDw;          /* the DW holding bit N + 0                    */
    ULONG CauseBit;         /* its mask                                    */
    ULONG DataRing;         /* the data ring r, 0 for none                 */
    ULONG DataDw;           /* the DW holding bit N + r                    */
    ULONG DataBit;          /* its mask, 0 for none                        */
} CM_RIRQ_GEOM;

/* The geometry for `paths` (1 to TBT_HI_CAPS_PATHS_MAX), no data cause;
 * FALSE, and the geometry zeroed, for any other. */
BOOLEAN CmRirqGeometry(ULONG paths, CM_RIRQ_GEOM *g);
/* Ring `ring`'s Receive Data Buffer cause, bit N + ring (transmit causes
 * 0 to N - 1, receive from N; Linux nhi.c ring_interrupt_index), set
 * beside ring 0's: TRUE for 1 to N - 1; FALSE, and none set, for any
 * other (0 clears it). */
BOOLEAN CmRirqGeomData(CM_RIRQ_GEOM *g, ULONG ring);

/* The causes a status shows, `v` every status DW as read (Dwords of
 * them): ring 0's, and the data ring's while it is in the geometry. */
#define CM_RIRQ_CAUSE_RING0      0x1UL
#define CM_RIRQ_CAUSE_DATA       0x2UL
ULONG CmRirqCauses(const CM_RIRQ_GEOM *g, const ULONG *v);

/* The service routine's outcome for the status DWs it read. */
#define CM_RIRQ_ISR_ONES         0UL    /* all ones: no answer, FALSE      */
#define CM_RIRQ_ISR_OTHER        1UL    /* no cause set: not this one's    */
#define CM_RIRQ_ISR_MINE         2UL    /* claimed                         */
ULONG CmRirqClassify(const CM_RIRQ_GEOM *g, const ULONG *v);

/*
 * The storm count, the service routine's alone: Burst claims since the
 * DPC last ran, read against the DPC's sequence (raised by the DPC each
 * time it runs). A changed sequence starts the count again at 1.
 * CmRirqBurstClaim counts one claim and returns TRUE at the claim that
 * reaches CM_RIRQ_STORM_CLAIMS: the routine masks the cause itself.
 */
typedef struct _CM_RIRQ_BURST {
    ULONG Burst;
    LONG Seq;
} CM_RIRQ_BURST;

VOID CmRirqBurstReset(CM_RIRQ_BURST *b, LONG seq);
BOOLEAN CmRirqBurstClaim(CM_RIRQ_BURST *b, LONG seq);

/*
 * The mask by the host's contract (record 21 sections 4.3 and 4.5.2): on
 * an Intel host (TBT_HW_INTEL_INTERRUPTS) a read-modify-write of the mask
 * register's DW, `imr` the value just read (all ones: no write); on every
 * other host one write of the bit to Interrupt Mask Set (open) or Clear.
 * CmRirqMaskProven: the mask register's DW read back shows the bit as
 * asked, and is not all ones.
 */
typedef struct _CM_RIRQ_WRITE {
    BOOLEAN Write;          /* FALSE: nothing may be written               */
    ULONG Offset;           /* BAR0 offset                                 */
    ULONG Value;
} CM_RIRQ_WRITE;

VOID CmRirqMaskWrite(const CM_RIRQ_GEOM *g, BOOLEAN intel, BOOLEAN open,
                     ULONG imr, CM_RIRQ_WRITE *w);
BOOLEAN CmRirqMaskProven(const CM_RIRQ_GEOM *g, BOOLEAN open, ULONG imr);
/* The same for the data ring's cause: `imr` its own DW (DataDw). */
VOID CmRirqDataMaskWrite(const CM_RIRQ_GEOM *g, BOOLEAN intel, BOOLEAN open,
                         ULONG imr, CM_RIRQ_WRITE *w);
BOOLEAN CmRirqDataMaskProven(const CM_RIRQ_GEOM *g, BOOLEAN open, ULONG imr);

/*
 * Both causes masked at once (the fast storm and the order down, record 35
 * section 5.5): `imr0` the mask DW of ring 0's cause and `imrData` the data
 * cause's as just read (the same value when they share a DW; used on Intel
 * alone). Into w[0] and w[1] the writes to make, in order: on Intel a
 * read-modify-write of each DW holding one (one write, both bits cleared,
 * when they share a DW); elsewhere each DW's bits to Interrupt Mask
 * Clear. Ring 0's alone when no data cause is open. The count of writes,
 * 0 to 2 (an all-ones DW on Intel: none for it). CmRirqBothMasked: the
 * read backs show every open cause masked.
 */
ULONG CmRirqMaskBoth(const CM_RIRQ_GEOM *g, BOOLEAN intel, ULONG imr0,
                     ULONG imrData, CM_RIRQ_WRITE *w);
BOOLEAN CmRirqBothMasked(const CM_RIRQ_GEOM *g, ULONG imr0, ULONG imrData);

/*
 * The acknowledgement (record 21 section 4.5.2, record 35 section 5.5):
 * the read of every status DW is it where auto-clear is on; where Disable
 * ISR Auto-Clear was found set (`autoClearOff`), each DW read non-zero and
 * not all ones is written back to Interrupt Status Clear at its own
 * offset. Into w (room for `max`) in DW order; the count.
 */
ULONG CmRirqAckWrites(const CM_RIRQ_GEOM *g, BOOLEAN autoClearOff,
                      const ULONG *v, CM_RIRQ_WRITE *w, ULONG max);

/*
 * The watchdog (record 21 section 4.5.5), on the thread at each
 * maintenance pass while armed. A frame pending on RX ring 0 while the
 * claims are unchanged since the end of the last drain is a suspected
 * miss; CM_RIRQ_SUSPECT_RUN of them in a row ask for the self-test again.
 * More than CM_RIRQ_SLOW_STORM claims since the previous maintenance with
 * no frame taken from the ring meanwhile is the slower storm. The claims
 * given are those that saw ring 0's cause set.
 *
 * Per cause (record 35 section 5.5): with the data ring's cause open, its
 * claims and its ring's frames taken are watched the same way and apart,
 * by CmRirqWatchData after CmRirqWatchDataStart at its opening; either
 * cause's storm is the storm, whatever the other did.
 */
#define CM_RIRQ_WATCH_OK         0UL
#define CM_RIRQ_WATCH_SELFTEST   1UL
#define CM_RIRQ_WATCH_STORM      2UL

typedef struct _CM_RIRQ_WATCH {
    ULONG ClaimsAtDrain;    /* the claims at the end of the last drain     */
    ULONG Suspects;         /* suspected misses in a row                   */
    ULONG StormClaims;      /* the claims at the previous maintenance      */
    ULONG StormRx;          /* the ring's frames taken then                */
    ULONG DataOn;           /* the data cause watched                      */
    ULONG DataClaims;       /* its claims at the previous maintenance      */
    ULONG DataRx;           /* its ring's frames taken then                */
} CM_RIRQ_WATCH;

VOID CmRirqWatchStart(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx);
VOID CmRirqWatchDrained(CM_RIRQ_WATCH *w, ULONG claims);
ULONG CmRirqWatchPass(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx,
                      BOOLEAN pending);
/* The data cause watched from now (`on`), or no more. */
VOID CmRirqWatchDataStart(CM_RIRQ_WATCH *w, BOOLEAN on, ULONG claims,
                          ULONG rx);
/* At each maintenance pass, beside CmRirqWatchPass: TRUE for the data
 * cause's storm (never while it is not watched). */
BOOLEAN CmRirqWatchData(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx);

/*
 * The order down (record 21 section 4.5.6): why it runs, and how it ends
 * for a source proven quiet (`retired`) or not. A fall back to polling
 * and a containment entry retain an unretired interrupt (a function in D0
 * may still assert the line); a D3 and a stop (removal and surprise
 * removal included) disconnect it whatever the proof. CmRirqDownFails:
 * whether the activation's failure latch is set by that end.
 */
#define CM_RIRQ_DOWN_FALLBACK    1UL    /* a failed arm or self-test, lost,
                                           a storm                        */
#define CM_RIRQ_DOWN_CONTAIN     2UL    /* the client stopped polling: a
                                           containment, a loss, a failure */
#define CM_RIRQ_DOWN_D3          3UL    /* a D3, or a sleep in D0          */
#define CM_RIRQ_DOWN_STOP        4UL    /* stop, removal, surprise removal */

#define CM_RIRQ_END_DISCONNECT   1UL
#define CM_RIRQ_END_RETAIN       2UL
#define CM_RIRQ_END_UNPROVEN     3UL    /* disconnected, not proven quiet  */

ULONG CmRirqDownEnd(ULONG why, BOOLEAN retired);
BOOLEAN CmRirqDownFails(ULONG why, ULONG end);

/* Whether this activation tries the interrupt: the value 1, a line
 * resource, and the failure latch clear - on every system, 98 SE and ME
 * included since their route was read (section 4.5.4; task 48.26). */
BOOLEAN CmRirqWanted(ULONG value, BOOLEAN line, BOOLEAN failed);

/* The wait to an absolute deadline `due` on a millisecond clock that may
 * wrap: 0 when due, else what is left, at most `limit`. CmRirqDue: the
 * deadline has come. */
ULONG CmRirqLeftMs(ULONG now, ULONG due, ULONG limit);
BOOLEAN CmRirqDue(ULONG now, ULONG due);

#endif /* CM_RIRQ_H */
