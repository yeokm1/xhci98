/*
 * xhci_enum.h - the enumeration state machine of xhci98.sys, one per port
 * (design record 13 section 5.3; roadmap-hcd.md tasks 26-A.3, 26-A.4 and
 * 26-A.9).
 *
 * A pure transition function over a port record and an event: it returns
 * the one action the caller must carry out and changes nothing else. The
 * caller - the controller thread (hcd_enum.c) - performs the action, waits
 * for its outcome, and feeds the outcome back as the next event. The machine
 * touches no register, calls no kernel service and holds no lock, so the
 * host suite drives it with no controller at all (test\test_enum.c), the
 * split xhci_vhub.c made for the virtual hub in the miniport.
 *
 * DDK-free: part of the pure core.
 */

#ifndef XHCI_ENUM_H
#define XHCI_ENUM_H

#include "xhci_compat.h"

/* States (section 5.3's table). */
#define XHCI_ENUM_EMPTY          0UL
#define XHCI_ENUM_DEBOUNCE       1UL
#define XHCI_ENUM_RESET          2UL
#define XHCI_ENUM_ENABLE_SLOT    3UL
#define XHCI_ENUM_ADDRESS        4UL
#define XHCI_ENUM_DESC8          5UL
#define XHCI_ENUM_EVALUATE       6UL
#define XHCI_ENUM_DESC18         7UL
#define XHCI_ENUM_CONFIG9        8UL
#define XHCI_ENUM_CONFIG_FULL    9UL
#define XHCI_ENUM_PRESENT        10UL
#define XHCI_ENUM_BOUND          11UL
#define XHCI_ENUM_GONE           12UL
#define XHCI_ENUM_FAILED         13UL
/* SuperSpeed only (task 29-A.3): the BOS descriptor, between the device
 * descriptor and the configuration. Numbered after the rest so the states
 * Phase 26 named keep their values. */
#define XHCI_ENUM_BOS5           14UL
#define XHCI_ENUM_BOS_FULL       15UL
#define XHCI_ENUM_STATE_COUNT    16UL

/* Events. Each carries the fields its outcome needs in XHCI_ENUM_EVENT. */
#define XHCI_ENUM_EV_CONNECT       1UL  /* CCS seen set on a change        */
#define XHCI_ENUM_EV_DISCONNECT    2UL  /* CCS seen clear on a change      */
#define XHCI_ENUM_EV_DEBOUNCED     3UL  /* Connected: still connected      */
#define XHCI_ENUM_EV_RESET_DONE    4UL  /* Ok, Speed                       */
#define XHCI_ENUM_EV_COMMAND_DONE  5UL  /* Ok, SlotId (Enable Slot)        */
#define XHCI_ENUM_EV_TRANSFER_DONE 6UL  /* Ok, Bytes, Value (see below)    */
#define XHCI_ENUM_EV_PDO_CREATED   7UL  /* Ok                              */
#define XHCI_ENUM_EV_PDO_STARTED   8UL
#define XHCI_ENUM_EV_PDO_REMOVED   9UL

/* Actions. */
#define XHCI_ENUM_ACT_NONE          0UL
#define XHCI_ENUM_ACT_DEBOUNCE      1UL /* wait the attach debounce, re-read  */
#define XHCI_ENUM_ACT_RESET         2UL /* reset the port, read its speed     */
#define XHCI_ENUM_ACT_ENABLE_SLOT   3UL
#define XHCI_ENUM_ACT_ADDRESS       4UL /* Address Device, EP0 at Mps0        */
#define XHCI_ENUM_ACT_GET_DEVICE    5UL /* GET_DESCRIPTOR(DEVICE), Length     */
#define XHCI_ENUM_ACT_EVALUATE      6UL /* Evaluate Context, EP0 at Mps0      */
#define XHCI_ENUM_ACT_GET_CONFIG    7UL /* GET_DESCRIPTOR(CONFIGURATION), Len */
#define XHCI_ENUM_ACT_CREATE_PDO    8UL
#define XHCI_ENUM_ACT_DISABLE_SLOT  9UL /* and drop what the slot owned       */
#define XHCI_ENUM_ACT_REPORT_GONE   10UL /* report the PDO missing            */
#define XHCI_ENUM_ACT_GET_BOS       11UL /* GET_DESCRIPTOR(BOS), Length       */

/* Why a port reached XHCI_ENUM_FAILED (XHCI_ENUM_PORT.FailCause). */
#define XHCI_ENUM_FAIL_NONE         0UL
#define XHCI_ENUM_FAIL_RESET        1UL
#define XHCI_ENUM_FAIL_NO_SLOT      2UL
#define XHCI_ENUM_FAIL_ADDRESS      3UL
#define XHCI_ENUM_FAIL_DESCRIPTOR   4UL
#define XHCI_ENUM_FAIL_CONFIG       5UL
#define XHCI_ENUM_FAIL_PDO          6UL
#define XHCI_ENUM_FAIL_SPEED        7UL

/*
 * The machine's speeds: the default Protocol Speed IDs (xHCI 7.2.1), as a
 * vocabulary of classes rather than a reading of PORTSC. The caller decodes
 * the port's raw PSIV through the controller's PSI table and hands the
 * machine the class's default ID; the raw PSIV goes to the Slot Context by
 * another path (hcd_enum.c). SUPER stands for every SuperSpeed-class rate,
 * SuperSpeedPlus included (29-A.1): EP0 and the BOS read are the same.
 */
#define XHCI_ENUM_SPEED_FULL        1UL
#define XHCI_ENUM_SPEED_LOW         2UL
#define XHCI_ENUM_SPEED_HIGH        3UL
#define XHCI_ENUM_SPEED_SUPER       4UL

/* The length of a USB device descriptor, and of a configuration descriptor's
 * own header (USB 2.0 9.6.1, 9.6.3). */
#define XHCI_ENUM_DEVICE_DESC_BYTES 18UL
#define XHCI_ENUM_CONFIG_HEAD_BYTES 9UL
/* The BOS descriptor's own header (USB 3.2 9.6.2): bLength 5, then
 * wTotalLength at offset 2. */
#define XHCI_ENUM_BOS_HEAD_BYTES    5UL

/* One retry of the whole reset-to-descriptors sequence on a failure, as the
 * targets' own hub drivers do (section 5.3's Failed row). */
#define XHCI_ENUM_RETRIES           1UL

typedef struct _XHCI_ENUM_PORT {
    ULONG State;
    ULONG Speed;            /* XHCI_ENUM_SPEED_*, from the reset        */
    ULONG SlotId;           /* 0 = none enabled                         */
    ULONG Mps0;             /* EP0 max packet size in use               */
    ULONG ConfigLength;     /* wTotalLength, from the 9-byte read       */
    ULONG Retries;          /* used of XHCI_ENUM_RETRIES                */
    ULONG FailCause;        /* XHCI_ENUM_FAIL_*                         */
    ULONG PdoExists;        /* a PDO has been created and not removed   */
    ULONG BosLength;        /* SuperSpeed: wTotalLength of the BOS      */
    ULONG BosMissing;       /* SuperSpeed: the BOS read failed, and the
                             * enumeration went on without it           */
} XHCI_ENUM_PORT, *PXHCI_ENUM_PORT;

typedef struct _XHCI_ENUM_EVENT {
    ULONG Kind;             /* XHCI_ENUM_EV_*                           */
    ULONG Ok;               /* the step succeeded                       */
    ULONG Speed;            /* RESET_DONE                               */
    ULONG SlotId;           /* COMMAND_DONE after Enable Slot           */
    ULONG Bytes;            /* TRANSFER_DONE: bytes received            */
    ULONG Value;            /* TRANSFER_DONE: bMaxPacketSize0 after the
                             * 8-byte read (at SuperSpeed the exponent,
                             * 9 for 512), wTotalLength after the
                             * 9-byte configuration read and after the
                             * 5-byte BOS read                          */
} XHCI_ENUM_EVENT, *PXHCI_ENUM_EVENT;

typedef struct _XHCI_ENUM_ACTION {
    ULONG Kind;             /* XHCI_ENUM_ACT_*                          */
    ULONG Length;           /* GET_DEVICE / GET_CONFIG: wLength         */
    ULONG Mps0;             /* ADDRESS / EVALUATE                       */
} XHCI_ENUM_ACTION, *PXHCI_ENUM_ACTION;

/* The EP0 packet size Address Device uses for a speed before the device
 * has said (section 10.2 step 7; the miniport's Finding 2 fix for Full
 * Speed), 512 at SuperSpeed (29-A.3). 0 for a speed the machine does not
 * take. */
ULONG XhciEnumInitialMps0(ULONG speed);

/* Put a port in XHCI_ENUM_EMPTY with nothing owned. */
VOID XhciEnumReset(PXHCI_ENUM_PORT port);

/* One transition. Returns the new state; *action says what to do next. An
 * event that does not apply to the current state changes nothing and asks
 * for nothing. */
ULONG XhciEnumStep(PXHCI_ENUM_PORT port, const XHCI_ENUM_EVENT *event,
                   PXHCI_ENUM_ACTION action);

/* After a failure: one more attempt from Reset while retries remain
 * (XHCI_ENUM_RETRIES), otherwise nothing. The caller sends it once the slot
 * the failed step owned has been disabled and the port still reads
 * connected. */
ULONG XhciEnumRetry(PXHCI_ENUM_PORT port, PXHCI_ENUM_ACTION action);

/* The same with the caller's limit in place of XHCI_ENUM_RETRIES: a hub's
 * port is given XHCI_HUB_PORT_ATTEMPTS attempts in all (xhci_hub.h). */
ULONG XhciEnumRetryUpTo(PXHCI_ENUM_PORT port, ULONG retries,
                        PXHCI_ENUM_ACTION action);

/*
 * The first answer's settle (task 33.3; design record 13 section 5.7). A
 * hub FDO's first BusRelations answer after its start waits, bounded, until
 * the controller thread has finished a pass begun after that start with
 * nothing left in flight: every port that read connected when it was looked
 * at enumerated to its PDOs or failed, every hub's first look at its ports
 * done, and every send-back to a USB 2.0 companion (29-A.5) asked for in the
 * window followed until the device showed there. Windows 2000's text-mode
 * Setup binds only what that first answer carries.
 *
 * Two bounds, both this driver's numbers (owner, 2026-10-04), scaled from
 * Microsoft's account of its own hub driver (the Microsoft USB blog, "How
 * does USB stack enumerate a device?": a 100 ms stable debounce given up
 * after 200 ms, a 5 s timeout per port reset, up to three tries 500 ms
 * apart, 10 ms reset recovery) and USB 2.0 9.2.6.4 (a request without data
 * done within 50 ms, a data stage begun within 500 ms): a normal device is
 * ready in 0.15 to 0.3 s and one retry takes 1 to 2 s. The total, one
 * deadline per FDO's first answer and not renewed per port, defaults to
 * 5 s; the answer goes as soon as the bus settles, so with nothing attached
 * it costs one look. The per-port budget, 2 s, covers the debounce limit,
 * one retry with its pause and the descriptor reads: a port still
 * enumerating past it, looked at between steps, is set aside until the
 * answer has gone (deferred, counted) so a slow device cannot hold back the
 * ports after it. The controller's driver key may set either, in ms, as a
 * REG_DWORD: XhciFirstEnumWaitMs (0 turns the wait off, held to 30 s) and
 * XhciFirstEnumPortMs (0 for no per-port budget, held to the total).
 * Text-mode Setup writes no driver-key value, so the defaults are what F6
 * Setup gets. The step is how often the waiter looks.
 */
#define XHCI_ENUM_SETTLE_DEFAULT_MS 5000UL
#define XHCI_ENUM_SETTLE_MAX_MS     30000UL
#define XHCI_ENUM_SETTLE_PORT_MS    2000UL
#define XHCI_ENUM_SETTLE_STEP_MS    20UL

/* The total deadline in ms from the driver-key value: the default when no
 * value was read (`found` 0), else the value held to the maximum; 0 means
 * no wait. */
ULONG XhciEnumSettleCap(ULONG found, ULONG value);

/* The per-port budget in ms from its driver-key value and the total: the
 * default when none was read, else the value; either held to the total. 0
 * means no per-port budget. */
ULONG XhciEnumSettlePortCap(ULONG found, ULONG value, ULONG total);

/* Milliseconds between two readings of the low 32 bits of the system time
 * (100 ns units), across one wrap of that word (429 s): the trace's figure
 * for a wait, never its bound - both bounds are relative timers, which a
 * change of the system time does not move (hcd_enum.c). */
ULONG XhciEnumElapsedMs(ULONG startLow, ULONG nowLow);
/* 1 when a port's machine is at rest - nothing in flight: Empty, Present
 * (its PDOs created, or a send-back pending, which XhciEnumHoldInFlight
 * covers), Bound, Gone or Failed. */
ULONG XhciEnumAtRest(ULONG state);

/* 1 while a send-back is in flight for the settle: asked for and not yet
 * acted on, or begun with a companion port that has not yet reported the
 * device's connect. A hold with no companion is the device's end there. */
ULONG XhciEnumHoldInFlight(ULONG pending, ULONG kind, ULONG companion,
                           ULONG connectSeen);

/* 1 when a pass may declare the bus settled: it enumerated (the root hub
 * started, the controller powered and not halted), and at its end no root
 * port change is owed, no hub port look is owed, and nothing is in
 * flight. Owed work on a port deferred for the settle does not count. */
ULONG XhciEnumSettleQuiet(ULONG enumerated, ULONG rootPending,
                          ULONG hubPending, ULONG inFlight);

/* 1 when the settled generation `done` has reached `target`, across the
 * counter's wrap (a generation more than half the range behind is not
 * reached). */
ULONG XhciEnumSettleReached(ULONG done, ULONG target);

/* A hub's devnode disabled (task 33.4; design record 13 sections 5.7 and
 * 10.11): when its FDO is removed, a child PDO still listed that PnP was
 * shown and has since removed is one PnP has forgotten with the hub's
 * subtree - 1 when the PDO's flags say so. Such a PDO is never carried
 * again: a hub FDO started on that PDO later cycles its port, so the
 * device comes back as a new PDO at the same instance id. */
ULONG XhciEnumLetGo(ULONG listed, ULONG reported, ULONG removeReceived);

/* 1 when a relations answer for the parent `answering` (0 the root hub,
 * else the hub PDO's serial) carries a listed PDO presented under
 * `parentSerial`: its own children, never one PnP let go of. */
ULONG XhciEnumAnswerCarries(ULONG parentSerial, ULONG answering,
                            ULONG letGo);

/*
 * A root port's enumeration notes (task 35.3; issue 11 section 7). 35.0's
 * dumps showed a trained link and no slot, and nothing in the log said why:
 * the cause had to be inferred from the code and an offset table built by
 * hand. These shipping XhciLogNote records name it, in every flavour, from
 * XhciLogVerbosity 2. Root ports only; one value per note, packed by the
 * functions below so the host suite checks every layout (test\test_enum.c).
 * Every field is masked to its width; the port is the xHCI port number.
 *
 *   enum.port.look   an inspection that fed the machine, asked for a link
 *                    action, or found the machine Failed:
 *                      31:24 port         23:20 machine state before it
 *                      19:16 link action (XHCI_LINK_ACT_*)
 *                      15:14 feed (bit 14 DISCONNECT, bit 15 CONNECT)
 *                      13:7  PORTSC change bits 23:17 (CSC at bit 7)
 *                      6:3   PORTSC PLS   2 PR   1 PED   0 CCS
 *   enum.port.reset  a reset's result:
 *                      31:24 port   23 ok   22:16 attempt (0 the first)
 *                      15:0  PORTSC bits 15:0 as the reset left it (speed
 *                            at 13:10, PLS at 8:5, PED, CCS)
 *   enum.port.speed  the raw speed ID and what it decoded to:
 *                      31:24 port   23:16 raw PSIV
 *                      15:8  class (XHCI_SPEED_*, 0 unknown)
 *                      7:0   source (XHCI_PSI_SOURCE_*: 1 listed in the
 *                            PSI table, 2 the defaults of a group with no
 *                            table, 3 the USB 3 fallback of task 35.1)
 *   enum.port.rate   the rate that ID means:
 *                      31:24 port   23 SuperSpeedPlus
 *                      22:0  Mbit/s (0 when the group names none)
 *   enum.port.slot   Enable Slot's completion:
 *                      31:24 port   23:16 completion code (0 for a command
 *                            that never completed)
 *                      15:8  attempt   7:0 Slot ID (0 none kept)
 *   enum.port.fail   a failed attempt:
 *                      31:24 port   23:16 cause (XHCI_ENUM_FAIL_*)
 *                      15:8  attempt   7:0 1 when no retry follows
 *   enum.port.end    where a run that reset the port left the machine:
 *                      31:24 port   23:16 state   15:8 cause
 *                      7:0   retries used
 *   enum.port.quiet  the port's budget is spent (31:24 port, 7:0 budget):
 *                    nothing more is noted for it until it enumerates.
 *
 * The bound. A burst - one look, or one run of the machine that reset the
 * port - is charged to the port's budget of XHCI_ENUM_NOTE_BUDGET when it
 * begins; a run notes at most two attempts of four or five notes each, so a
 * port costs the 16 KB ring at most some 2.5 KB between enumerations. A
 * port that enumerates (its PDO created) is given the whole budget back, so
 * a working port replugged any number of times keeps its notes and only a
 * port that fails or flaps falls quiet; the bursts it was refused are
 * counted (Suppressed) and published in the snapshot's HCD region. Thread
 * only, like the rest of a port's machine.
 */
#define XHCI_ENUM_NOTE_BUDGET       8UL

#define XHCI_ENUM_NOTE_NO           0UL /* nothing is noted              */
#define XHCI_ENUM_NOTE_YES          1UL /* the burst is noted            */
#define XHCI_ENUM_NOTE_QUIET        2UL /* only enum.port.quiet, once    */

typedef struct _XHCI_ENUM_NOTES {
    ULONG Used;             /* bursts charged since the last refill, held
                             * at XHCI_ENUM_NOTE_BUDGET + 1 once quiet  */
    ULONG Open;             /* the run in progress: 0 not yet charged,
                             * 1 noted, 2 refused                       */
    ULONG Suppressed;       /* bursts refused since the start           */
    ULONG FailedLooked;     /* a look at the Failed machine was noted
                             * and nothing has been fed since           */
} XHCI_ENUM_NOTES, *PXHCI_ENUM_NOTES;

/* Clear a port's budget and counts: at a start. */
VOID XhciEnumNotesInit(PXHCI_ENUM_NOTES notes);

/* Charge one burst. Returns XHCI_ENUM_NOTE_YES while the budget lasts,
 * XHCI_ENUM_NOTE_QUIET for the first burst past it, and XHCI_ENUM_NOTE_NO
 * after that; each refusal is counted. */
ULONG XhciEnumNoteCharge(PXHCI_ENUM_NOTES notes);

/* The port enumerated: the whole budget again. */
VOID XhciEnumNoteRefill(PXHCI_ENUM_NOTES notes);

/* Whether a look is worth a burst: it fed the machine (`feed`, the
 * XHCI_LINK_FEED_* bits) or acted on the link (`linkActed`), or it found
 * the machine Failed (`state`) for the first time since anything was fed -
 * a trained link whose change events keep coming to a machine that will
 * not try again is 35.0's case, and one look says so. */
ULONG XhciEnumNoteWantLook(PXHCI_ENUM_NOTES notes, ULONG state, ULONG feed,
                           ULONG linkActed);

/* A run of the machine that resets the port: charged once, at its first
 * reset; returns the charge's answer then and XHCI_ENUM_NOTE_NO at every
 * later call of the same run. XhciEnumNoteOn says whether the run's notes
 * are written; XhciEnumNoteFinish ends the run. */
ULONG XhciEnumNoteBegin(PXHCI_ENUM_NOTES notes);
ULONG XhciEnumNoteOn(const XHCI_ENUM_NOTES *notes);
VOID XhciEnumNoteFinish(PXHCI_ENUM_NOTES notes);

/* The packings above, one function a label. */
ULONG XhciEnumNoteLook(ULONG port, ULONG state, ULONG linkAction,
                       ULONG feed, ULONG portsc);
ULONG XhciEnumNoteReset(ULONG port, ULONG ok, ULONG attempt, ULONG portsc);
ULONG XhciEnumNoteSpeed(ULONG port, ULONG psiv, ULONG speedClass,
                        ULONG source);
ULONG XhciEnumNoteRate(ULONG port, ULONG kbps, ULONG plus);
ULONG XhciEnumNoteSlot(ULONG port, ULONG code, ULONG attempt, ULONG slotId);
ULONG XhciEnumNoteFail(ULONG port, ULONG cause, ULONG attempt, ULONG final);
ULONG XhciEnumNoteEnd(ULONG port, ULONG state, ULONG cause, ULONG retries);
ULONG XhciEnumNoteQuiet(ULONG port);


#endif /* XHCI_ENUM_H */
