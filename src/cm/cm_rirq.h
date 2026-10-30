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
 *   - whether the interrupt is wanted, and the wait to a deadline.
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

/* The most status DWs a host has: 3 x 21 causes. */
#define CM_RIRQ_MAX_DWORDS       TBT_HI_IMR_DWORDS(TBT_HI_CAPS_PATHS_MAX)

/* The cause and the status it lives in. */
typedef struct _CM_RIRQ_GEOM {
    ULONG Paths;            /* Total Paths, N                              */
    ULONG Dwords;           /* status DWs, ceiling(3N / 32)                */
    ULONG CauseDw;          /* the DW holding bit N + 0                    */
    ULONG CauseBit;         /* its mask                                    */
} CM_RIRQ_GEOM;

/* The geometry for `paths` (1 to TBT_HI_CAPS_PATHS_MAX); FALSE, and the
 * geometry zeroed, for any other. */
BOOLEAN CmRirqGeometry(ULONG paths, CM_RIRQ_GEOM *g);

/* The service routine's outcome for the first status DW and the cause's
 * DW as it read them (the same value when CauseDw is 0). */
#define CM_RIRQ_ISR_ONES         0UL    /* all ones: no answer, FALSE      */
#define CM_RIRQ_ISR_OTHER        1UL    /* the cause clear: not this one's */
#define CM_RIRQ_ISR_MINE         2UL    /* claimed                         */
ULONG CmRirqClassify(const CM_RIRQ_GEOM *g, ULONG first, ULONG causeDw);

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

/*
 * The watchdog (record 21 section 4.5.5), on the thread at each
 * maintenance pass while armed. A frame pending on RX ring 0 while the
 * claims are unchanged since the end of the last drain is a suspected
 * miss; CM_RIRQ_SUSPECT_RUN of them in a row ask for the self-test again.
 * More than CM_RIRQ_SLOW_STORM claims since the previous maintenance with
 * no frame taken from the ring meanwhile is the slower storm.
 */
#define CM_RIRQ_WATCH_OK         0UL
#define CM_RIRQ_WATCH_SELFTEST   1UL
#define CM_RIRQ_WATCH_STORM      2UL

typedef struct _CM_RIRQ_WATCH {
    ULONG ClaimsAtDrain;    /* the claims at the end of the last drain     */
    ULONG Suspects;         /* suspected misses in a row                   */
    ULONG StormClaims;      /* the claims at the previous maintenance      */
    ULONG StormRx;          /* the ring's frames taken then                */
} CM_RIRQ_WATCH;

VOID CmRirqWatchStart(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx);
VOID CmRirqWatchDrained(CM_RIRQ_WATCH *w, ULONG claims);
ULONG CmRirqWatchPass(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx,
                      BOOLEAN pending);

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

/* Whether this activation tries the interrupt: the value 1, an NT system
 * (98 SE and ME only after their route is read, section 4.5.4), a line
 * resource, and the failure latch clear. */
BOOLEAN CmRirqWanted(ULONG value, BOOLEAN nt, BOOLEAN line, BOOLEAN failed);

/* The wait to an absolute deadline `due` on a millisecond clock that may
 * wrap: 0 when due, else what is left, at most `limit`. CmRirqDue: the
 * deadline has come. */
ULONG CmRirqLeftMs(ULONG now, ULONG due, ULONG limit);
BOOLEAN CmRirqDue(ULONG now, ULONG due);

#endif /* CM_RIRQ_H */
