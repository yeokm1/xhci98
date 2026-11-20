/*
 * test_sdeq.c - the field captures of task 50.20 as regression vectors
 * (design record 17 section 4.12.10, section 15.1).
 *
 * Each case is a stream cancel the driver made on an Intel 7 Series xHCI
 * (8086:1E31) with a four-stream UAS drive under 3.5.0.0, transcribed from
 * the stream trace (record 17 4.12.9) of the quick saves the reports
 * carried: the endpoint, the stream cancelled and its head TD, the TRB the
 * Stop Endpoint's Stopped event named, the Set TR Dequeue the cancel sent,
 * the other requests the endpoint held at the cancel (published TDs on its
 * other streams, counted from the trace's doorbells and completions), and
 * what followed: a TRB completed twice, Babble at the position just set,
 * or 4.12.8's c2 reading the position just set for one stream in another
 * stream's context. Addresses and codes only.
 *
 * Every stream ring of these endpoints is one 64-TRB segment (1 KiB) at
 * the stream array plus 1 KiB times the Stream ID, as the block layout
 * places it (xhci_stream.c, a ring at a multiple of its own size); the
 * reports' stream-context region shows the same arrays (data-in 5000h,
 * status 3000h, data-out A000h) and the later boot's moved data-in array.
 *
 * What is checked:
 * - the capture itself: the Stopped event lies on another stream's ring
 *   than the one cancelled (XhciStreamFind), and the foreign value c2 read
 *   is exactly the position the earlier cancel sent;
 * - flagged (the design): no cancel leads to a Set TR Dequeue, c2, a
 *   give-up, a cycle or a charge to the location's budget; each withholds
 *   its command and holds the endpoint (its pipe's debt); xhciuas's proof
 *   (UasStreamProof) takes the endpoint recycle - RESET_PIPE - when no other
 *   command was alive in the session, and the device reset otherwise; either
 *   replaces the held endpoints' contexts and releases the debt;
 * - without the contract (the control: an unflagged controller, or a client
 *   that does not declare on a flagged one): each cancel takes today's path; c2's
 *   placement of the foreign value on the captured ring is not holdable
 *   (XhciXferStopPosition), so 4.12.8 gives the device up and cycles it,
 *   as 3.5.0.0 did; a run of those cycles exhausts the location's budget
 *   and holds the port (XhciTolLocCharge), as the field's last session did.
 *
 * What it proves is the driver's decisions on these event sequences, not
 * the controller: on a flagged controller the sequence after the cancel is
 * never reached, because the command that the captures show starting it
 * is never sent.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_stream.h"
#include "../src/xhci_xfer.h"
#include "../src/xhci_tol.h"
#include "../src/uas/uas_iu.h"
#include "test_harness.h"

#define D_RING_TRBS     64UL
#define D_RING_BYTES    (D_RING_TRBS * 16UL)
#define D_STREAMS       4UL

/* What followed the cancel in the capture. */
#define D_NEXT_NONE     0UL     /* nothing of the fault's in the capture */
#define D_NEXT_DUP      1UL     /* a TRB of `Next` completed a second time */
#define D_NEXT_BABBLE   2UL     /* Babble Detected at `Next`               */
#define D_NEXT_FOREIGN  3UL     /* c2 read `Next` in stream `NextStream`   */

typedef struct _D_CANCEL {
    ULONG Dci;
    ULONG Array;        /* the endpoint's stream array                   */
    ULONG Stream;       /* X, the stream cancelled                        */
    ULONG Head;         /* its head TD's first TRB                        */
    ULONG Stopped;      /* the TRB the Stopped event named                */
    ULONG StopStream;   /* Y, the stream whose ring holds it              */
    ULONG Sent;         /* the Set TR Dequeue's pointer | SCT << 1 | DCS  */
    ULONG Others;       /* other streams' TDs alive on the endpoint       */
    ULONG Next;         /* D_NEXT_*: what followed, and its address      */
    ULONG NextKind;
    ULONG NextStream;
} D_CANCEL;

typedef struct _D_CASE {
    const char *Name;   /* the report's time stamp                        */
    const char *Form;
    ULONG Count;
    D_CANCEL Cancel[4];
    ULONG GaveUp;       /* 3.5.0.0 gave the device up after this capture  */
} D_CASE;

/* Transcribed from the stream traces; see the file's head. */
static const D_CASE dCases[] = {
    { "010559", "the later recovery's cancel (the replay before it in the "
                "capture has no initiating cancel: dDups)", 1, {
        { 5, 0x5000, 2, 0x5990, 0x5C80, 3, 0x59B2, 0,
          0, D_NEXT_NONE, 0 } }, 0 },
    { "035038", "the replay (stream 3's TRB 5F40h completed twice, "
                "stream 4's READ timed out)", 2, {
        { 5, 0x5000, 3, 0x5F20, 0x6390, 4, 0x5F33, 0,
          0x5F40, D_NEXT_DUP, 3 },
        { 5, 0x5000, 4, 0x6390, 0x5A60, 2, 0x63B2, 0,
          0, D_NEXT_NONE, 0 } }, 0 },
    { "023320", "Babble at the position just set", 1, {
        { 5, 0x5000, 1, 0x5460, 0x5B60, 2, 0x5472, 0,
          0x5470, D_NEXT_BABBLE, 1 } }, 0 },
    { "012801", "Babble at the position just set", 1, {
        { 5, 0x5000, 4, 0x60C0, 0x55B0, 1, 0x60D3, 0,
          0x60D0, D_NEXT_BABBLE, 4 } }, 0 },
    { "034046", "the write-back, Speccy's two refusals", 2, {
        { 5, 0x5000, 1, 0x5550, 0x6080, 4, 0x5563, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x5000, 2, 0x5AD0, 0x5560, 1, 0x5AE2, 0,
          0x5563, D_NEXT_FOREIGN, 4 } }, 1 },
    { "034530", "the write-back, Speccy's two refusals", 2, {
        { 5, 0x5000, 1, 0x5460, 0x6370, 4, 0x5473, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x5000, 2, 0x59D0, 0x5470, 1, 0x59E2, 0,
          0x5473, D_NEXT_FOREIGN, 4 } }, 1 },
    { "034959", "the write-back on the status endpoint, HWiNFO's "
                "recovery", 3, {
        { 5, 0x5000, 4, 0x6330, 0x54D0, 1, 0x6043, 0,
          0, D_NEXT_NONE, 0 },
        { 9, 0x3000, 1, 0x34B0, 0x3CC0, 3, 0x34C2, 3,
          0, D_NEXT_NONE, 0 },
        { 9, 0x3000, 2, 0x38B0, 0x34C0, 1, 0x38C2, 2,
          0x34C2, D_NEXT_FOREIGN, 3 } }, 1 },
    { "040507", "the write-back, MSI", 2, {
        { 5, 0x5000, 4, 0x6270, 0x5D60, 3, 0x6283, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x5000, 1, 0x5680, 0x6280, 4, 0x5693, 0,
          0x6283, D_NEXT_FOREIGN, 3 } }, 1 },
    { "040540", "the write-back on the status endpoint in one "
                "recovery, MSI", 4, {
        { 5, 0x5000, 2, 0x5870, 0x5D30, 3, 0x5883, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x5000, 3, 0x5D30, 0x5880, 2, 0x5E33, 0,
          0, D_NEXT_NONE, 0 },
        { 9, 0x3000, 1, 0x3770, 0x3B70, 2, 0x3782, 3,
          0, D_NEXT_NONE, 0 },
        { 9, 0x3000, 2, 0x3B70, 0x3780, 1, 0x3B82, 2,
          0x3782, D_NEXT_FOREIGN, 2 } }, 1 },
    { "040558", "the write-back, the data-in array moved", 2, {
        { 5, 0x86000, 4, 0x87220, 0x86E70, 3, 0x87232, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x86000, 1, 0x864B0, 0x87230, 4, 0x864C2, 0,
          0x87232, D_NEXT_FOREIGN, 3 } }, 1 },
    { "043201", "the write-back, the last session (1 of 3)", 2, {
        { 5, 0x5000, 4, 0x6130, 0x5DE0, 3, 0x6143, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x5000, 1, 0x5710, 0x6140, 4, 0x5722, 0,
          0x6143, D_NEXT_FOREIGN, 3 } }, 1 },
    { "043222", "the write-back, the last session (2 of 3)", 2, {
        { 5, 0x5000, 3, 0x5CB0, 0x5A20, 2, 0x5CC3, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x5000, 4, 0x63B0, 0x5CC0, 3, 0x63C3, 0,
          0x5CC3, D_NEXT_FOREIGN, 2 } }, 1 },
    { "043311", "the write-back, the last session (3 of 3), the hold", 2, {
        { 5, 0x87000, 3, 0x87FE0, 0x87A10, 2, 0x87C03, 0,
          0, D_NEXT_NONE, 0 },
        { 5, 0x87000, 4, 0x88000, 0x87C00, 3, 0x88013, 0,
          0x87C03, D_NEXT_FOREIGN, 2 } }, 1 }
};

#define D_CASES (sizeof(dCases) / sizeof(dCases[0]))

/* A TRB completed twice, as captured: the stream ring it lies on, and the
 * stream whose doorbell came between. 010559's capture starts after the
 * cancel that set it up (it holds only the tail of an earlier placement),
 * so that replay is kept apart from every cancel; 035038's follows its
 * cancel in the capture and is that cancel's Next as well. */
typedef struct _D_DUP {
    const char *Name;
    ULONG Array;
    ULONG Trb;
    ULONG Stream;
    ULONG Doorbell;
} D_DUP;

static const D_DUP dDups[] = {
    { "010559", 0x5000, 0x5710, 1, 2 },   /* +46 ms, again at +62 ms */
    { "035038", 0x5000, 0x5F40, 3, 4 }    /* +828 ms, again at +843 ms */
};

/* One endpoint's four rings at their captured addresses. */
typedef struct _D_EP {
    XHCI_TRB Trb[D_STREAMS + 1][D_RING_TRBS];
    XHCI_RING Ring[D_STREAMS + 1];
    const XHCI_RING *Rings[D_STREAMS + 1];
} D_EP;

static D_EP dEp;

static void d_rings(ULONG array)
{
    ULONG s;

    memset(&dEp, 0, sizeof(dEp));
    dEp.Rings[0] = NULL;
    for (s = 1; s <= D_STREAMS; s++) {
        CHECK_EQ(XhciRingInit(&dEp.Ring[s], dEp.Trb[s],
                              array + s * D_RING_BYTES, D_RING_TRBS,
                              XHCI_RING_KIND_ENDPOINT),
                 XHCI_RING_OK, "a stream ring at its captured address");
        dEp.Rings[s] = &dEp.Ring[s];
    }
}

/* What a run of the cases did, counted. */
typedef struct _D_RUN {
    ULONG SetDequeues;
    ULONG GiveUps;
    ULONG Cycles;
    ULONG Charged;
    ULONG Refused;
    ULONG Withheld;
    ULONG Recycles;
    ULONG Resets;
} D_RUN;

/*
 * One case through the decisions. Without the contract, each cancel sends its Set TR
 * Dequeue, and a foreign reading after it is c2's: placed on the ring of
 * the stream it was read in, a position that ring cannot hold gives the
 * device up, which cycles it and charges the location. With the contract, each
 * cancel
 * withholds its command (XhciStreamFlagDecide) and sets its endpoint pipe's
 * debt; the trace after a Set TR Dequeue is never reached, since none is
 * sent. xhciuas's worker then decides, from the commands alive in the
 * session (another stream's TD is another command's), and its recovery
 * replaces every held endpoint's context.
 */
static void d_run(const D_CASE *c, ULONG flagged, PULONG debt,
                  PXHCI_TOL_LOC loc, D_RUN *run)
{
    const D_CANCEL *k;
    ULONG decision;
    ULONG active;
    ULONG index;
    ULONG at;
    ULONG dci;
    ULONG i;

    active = 0;
    for (i = 0; i < c->Count; i++) {
        k = &c->Cancel[i];
        d_rings(k->Array);
        active += k->Others;
        decision = XhciStreamFlagDecide(flagged, XHCI_STREAM_KIND_KEEP,
                                        XHCI_EP_STATE_STOPPED, 1);
        if (decision == XHCI_STREAM_FD_TODAY) {
            run->SetDequeues++;
            if (k->NextKind == D_NEXT_FOREIGN &&
                XhciXferStopPosition(dEp.Rings[k->NextStream], k->Next, 0,
                                     &index, &at) !=
                    XHCI_XFER_STOP_POS_HOLDABLE) {
                run->GiveUps++;
                run->Cycles++;
                if (XhciTolLocCharge(loc, XHCI_TOL_CHARGE_REENUM, 0)) {
                    run->Charged++;
                } else {
                    run->Refused++;
                }
                return;         /* the device is gone with this cancel */
            }
            continue;
        }
        CHECK_EQ(decision, XHCI_STREAM_FD_WITHHELD,
                 "flagged: the command withheld");
        CHECK(XhciStreamFlagSetsDebt(decision), "flagged: the debt set");
        CHECK_EQ(UasStreamOnCancel(1, 1, 0), UAS_SO_OWE | UAS_SO_ASK,
                 "xhciuas: owed, the worker asked for");
        (VOID)XhciStreamDebtSet(&debt[k->Dci]);
        run->Withheld++;
    }
    if (run->Withheld == 0) {
        return;
    }
    switch (UasStreamProof(active, 0, 0, 0, 1, 0, 0)) {
    case UAS_SP_RECYCLE:
        run->Recycles++;
        break;
    case UAS_SP_RESET:
        run->Resets++;
        break;
    default:
        CHECK(0, "xhciuas: the proof decides once the cancels are back");
        break;
    }
    /* RESET_PIPE's recycle of each held endpoint, or RESET_PORT's replay:
     * the same pipe objects given new contexts. */
    for (dci = 2; dci < 32; dci++) {
        (VOID)XhciStreamDebtReplaced(&debt[dci]);
    }
}

static void test_captures_themselves(void)
{
    const D_CANCEL *k;
    const D_CANCEL *first;
    ULONG c;
    ULONG i;

    for (c = 0; c < D_CASES; c++) {
        first = NULL;
        for (i = 0; i < dCases[c].Count; i++) {
            k = &dCases[c].Cancel[i];
            d_rings(k->Array);
            CHECK_EQ(XhciStreamFind(dEp.Rings, D_STREAMS + 1, k->Head),
                     k->Stream, "the head TD is on the cancelled stream");
            CHECK_EQ(XhciStreamFind(dEp.Rings, D_STREAMS + 1, k->Stopped),
                     k->StopStream, "the Stopped event's TRB is on Y's ring");
            CHECK(k->StopStream != k->Stream,
                  "the stop names another stream than the one cancelled");
            CHECK_EQ(XhciStreamFind(dEp.Rings, D_STREAMS + 1,
                                    k->Sent & ~0xFUL),
                     k->Stream, "the command named X's own ring");
            CHECK_EQ(k->Sent & 0xEUL, 2UL, "SCT 1, Primary TR");
            if (k->NextKind == D_NEXT_DUP || k->NextKind == D_NEXT_BABBLE) {
                CHECK_EQ(XhciStreamFind(dEp.Rings, D_STREAMS + 1, k->Next),
                         k->NextStream,
                         "the repeated or babbling TRB is on X's ring");
            }
            if (k->NextKind == D_NEXT_BABBLE) {
                CHECK_EQ(k->Next, k->Sent & ~0xFUL,
                         "Babble exactly at the position just set");
            }
            if (k->NextKind == D_NEXT_FOREIGN) {
                CHECK(first != NULL, "a foreign value follows an earlier "
                                     "cancel of the same session");
                if (first != NULL) {
                    CHECK_EQ(k->Next, first->Sent,
                             "c2 read exactly the position sent for the "
                             "earlier cancel's stream");
                    CHECK(k->NextStream != first->Stream,
                          "in another stream's context");
                }
                CHECK(XhciStreamFind(dEp.Rings, D_STREAMS + 1,
                                     k->Next & ~0xFUL) != k->NextStream,
                      "off the ring of the stream it was read in");
            }
            if (first == NULL || first->Dci != k->Dci) {
                first = k;
            }
        }
    }
}

/* The session's cancels with another command alive: the recoveries that
 * cancelled on the status endpoint. */
static ULONG d_busy(const D_CASE *c)
{
    ULONG i;

    for (i = 0; i < c->Count; i++) {
        if (c->Cancel[i].Others != 0) {
            return 1;
        }
    }
    return 0;
}

static void test_captures_dups(void)
{
    ULONG i;

    for (i = 0; i < sizeof(dDups) / sizeof(dDups[0]); i++) {
        d_rings(dDups[i].Array);
        CHECK_EQ(XhciStreamFind(dEp.Rings, D_STREAMS + 1, dDups[i].Trb),
                 dDups[i].Stream,
                 "the repeated completion lies on its own stream's ring");
        CHECK(dDups[i].Doorbell != dDups[i].Stream,
              "after another stream's doorbell");
    }
}

static void test_captures_flagged(void)
{
    XHCI_TOL_LOC loc;
    ULONG debt[32];
    D_RUN run;
    ULONG c;
    ULONG d;

    for (c = 0; c < D_CASES; c++) {
        for (d = 0; d < 32; d++) {
            debt[d] = 0;
        }
        XhciTolLocInit(&loc);
        memset(&run, 0, sizeof(run));
        d_run(&dCases[c], 1, debt, &loc, &run);
        CHECK_EQ(run.SetDequeues, 0UL, "flagged: no Set TR Dequeue");
        CHECK_EQ(run.GiveUps, 0UL, "flagged: no give-up");
        CHECK_EQ(run.Cycles, 0UL, "flagged: no cycle");
        CHECK_EQ(run.Charged + run.Refused, 0UL,
                 "flagged: nothing charged to the location");
        CHECK_EQ(run.Withheld, dCases[c].Count,
                 "flagged: every cancel withheld");
        CHECK_EQ(run.Recycles + run.Resets, 1UL,
                 "flagged: one recovery for the session");
        CHECK_EQ(run.Resets, d_busy(&dCases[c]),
                 "flagged: a reset only when another command was alive");
        for (d = 0; d < 32; d++) {
            CHECK_EQ(debt[d], 0UL, "flagged: nothing held at the end");
        }
        CHECK_EQ(loc.Hold, XHCI_TOL_HOLD_NONE, "flagged: no hold");
    }
}

/* The last session's give-ups, in order. */
static const D_CASE *dHold[3];

static void d_hold_init(void)
{
    static const char *const names[3] = { "043201", "043222", "043311" };
    ULONG c;
    ULONG i;

    for (i = 0; i < 3; i++) {
        dHold[i] = NULL;
        for (c = 0; c < D_CASES; c++) {
            if (strcmp(dCases[c].Name, names[i]) == 0) {
                dHold[i] = &dCases[c];
            }
        }
        CHECK(dHold[i] != NULL && dHold[i]->GaveUp, "a captured give-up");
    }
}

static void test_captures_control(void)
{
    XHCI_TOL_LOC loc;
    ULONG debt[32];
    D_RUN run;
    ULONG c;
    ULONG i;
    ULONG d;

    for (c = 0; c < D_CASES; c++) {
        for (d = 0; d < 32; d++) {
            debt[d] = 0;
        }
        XhciTolLocInit(&loc);
        memset(&run, 0, sizeof(run));
        d_run(&dCases[c], 0, debt, &loc, &run);
        CHECK_EQ(run.GiveUps, dCases[c].GaveUp,
                 "no contract: 4.12.8 gives up where 3.5.0.0 did");
        CHECK_EQ(run.SetDequeues, dCases[c].Count,
                 "no contract: every cancel sends its Set TR Dequeue");
        CHECK_EQ(run.Withheld, 0UL, "no contract: nothing withheld");
    }

    /* The run that ended in the hold: the last session's three captured
     * give-ups (043201, 043222, 043311), each a cycle charged to the same
     * location - the baseline the field showed, three accepted - and one
     * more give-up after them, refused, the port held. */
    CHECK_EQ(XHCI_TOL_REENUMS, 3UL, "the budget the field met: three");
    XhciTolLocInit(&loc);
    memset(&run, 0, sizeof(run));
    for (i = 0; i < 4; i++) {
        for (d = 0; d < 32; d++) {
            debt[d] = 0;
        }
        d_run(dHold[i < 3 ? i : 2], 0, debt, &loc, &run);
    }
    CHECK_EQ(run.Charged, 3UL, "no contract: three cycles charged");
    CHECK_EQ(run.Refused, 1UL, "no contract: the fourth refused");
    CHECK_EQ(loc.Hold, XHCI_TOL_HOLD_REENUMS, "no contract: the port held");

    XhciTolLocInit(&loc);
    memset(&run, 0, sizeof(run));
    for (i = 0; i < 4; i++) {
        for (d = 0; d < 32; d++) {
            debt[d] = 0;
        }
        d_run(dHold[i < 3 ? i : 2], 1, debt, &loc, &run);
    }
    CHECK_EQ(run.Charged + run.Refused, 0UL, "flagged: the same run, no "
                                             "charge");
    CHECK_EQ(loc.Hold, XHCI_TOL_HOLD_NONE, "flagged: no hold");
}

/* c2's control on a sound stream: the captured enqueue of a stream with no
 * TD queued, at its own DCS, is a position the ring can hold (034046,
 * stream 4 placed at 6082h, a cycle-0 lap). */
static void test_control_sound(void)
{
    ULONG index;
    ULONG at;

    d_rings(0x5000);
    dEp.Ring[4].Enqueue = (0x6080UL - 0x6000UL) / 16UL;
    dEp.Ring[4].Dequeue = dEp.Ring[4].Enqueue;
    dEp.Ring[4].Cycle = 0;
    CHECK_EQ(XhciXferStopPosition(&dEp.Ring[4], 0x6082UL, 0, &index, &at),
             XHCI_XFER_STOP_POS_HOLDABLE,
             "stream 4 at its own enqueue: holdable");
    CHECK_EQ(XhciXferStopPosition(&dEp.Ring[4], 0x5563UL, 0, &index, &at),
             XHCI_XFER_STOP_POS_INVALID,
             "stream 4 holding stream 1's position: not");
}

/*
 * Task 50.30 step 3 (design record 17 section 4.15.5a, hcdCfgDeqReadBack's
 * resend): the resend's result as the read-back classifies it, the verdict
 * taken from the command's result as hcdCfgDeqReadBack takes it
 * (XhciTolCmdVerdict). An aborted or left resend is removed at once, before
 * the state the restop rule would read; every other result keeps its row.
 */
static void test_resend_verdict(void)
{
    static const ULONG states[] = {
        XHCI_EP_STATE_STOPPED, XHCI_EP_STATE_RUNNING, XHCI_EP_STATE_HALTED,
        XHCI_EP_STATE_ERROR
    };
    ULONG i;
    ULONG code;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        code = XHCI_CC_COMMAND_ABORTED;
        CHECK_EQ(XhciStreamResendAfter(code, states[i],
                                       XhciTolCmdVerdict(code)),
                 XHCI_STREAM_RS_REMOVED, "aborted resend: removed");
        code = XHCI_TOL_CMD_LEFT;
        CHECK_EQ(XhciStreamResendAfter(code, states[i],
                                       XhciTolCmdVerdict(code)),
                 XHCI_STREAM_RS_REMOVED, "left resend: removed");
        code = XHCI_CC_TRB_ERROR;
        CHECK_EQ(XhciStreamResendAfter(code, states[i],
                                       XhciTolCmdVerdict(code)),
                 XHCI_STREAM_RS_GIVEUP, "an error code: today's give-up");
    }
    code = XHCI_CC_CONTEXT_STATE_ERROR;
    CHECK_EQ(XhciStreamResendAfter(code, XHCI_EP_STATE_RUNNING,
                                   XhciTolCmdVerdict(code)),
             XHCI_STREAM_RS_RESTARTED, "Context State Error, Running: kept");
    code = XHCI_CC_SUCCESS;
    CHECK_EQ(XhciStreamResendAfter(code, XHCI_EP_STATE_STOPPED,
                                   XhciTolCmdVerdict(code)),
             XHCI_STREAM_RS_AGAIN, "success: read back again");
}

int main(void)
{
    d_hold_init();
    test_captures_themselves();
    test_captures_dups();
    test_captures_flagged();
    test_captures_control();
    test_control_sound();
    test_resend_verdict();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
