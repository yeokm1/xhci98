/*
 * xhci_link.h - the SuperSpeed link of a USB3 protocol root port, its pure
 * half (roadmap-hcd.md tasks 29-A.2 and 29-A.5; design record 13 section 2;
 * xhci-data-structures.md section 10, where every PORTSC fact this file
 * rests on is transcribed and verified against xHCI 1.2c and USB 3.2).
 *
 * Two things, both decisions over values with no register behind them, so
 * the host suite drives them with no controller (test\test_link.c):
 *
 *   - the link state machine of xHCI Figure 4-27 as the bus acts on it: what
 *     a USB3 port's PORTSC says its link is doing, and what the thread does
 *     about it - wait out Polling, enumerate an Enabled port, recover
 *     SS.Inactive and Compliance Mode with a warm reset, reset for
 *     enumeration hot or warm by the link's state (the hot-to-warm policy),
 *     resume from U3, give up after a bounded number of warm resets so the
 *     device can appear on its USB 2.0 companion (29-A.5's passive
 *     fallback);
 *   - 29-A.5's active fallback, the hold: a trained SuperSpeed port written
 *     PED = 1 so its link goes to SS.Disabled, held there while the device
 *     runs on the USB 2.0 side, and released - PLS = RxDetect with LWS, never
 *     a warm reset - only by the rules of future-plans/superspeed-storage-
 *     behind-a-switch.md section 6.1 (removed 2026-10-12; in git history at
 *     00a3b9e) as the roadmap tightened them: on the
 *     companion's disconnect, after a companion connect seen since the hold
 *     began, of the very device held (vendor id, product id and serial
 *     string). A hold with no identity, and an orphan's, last until the
 *     controller's next start. That is the specified behaviour, not a
 *     guess: a Disabled port is not left by a disconnect and has CCS = 0
 *     throughout, so it raises no CSC when the device goes (Figure 4-27,
 *     p.279; 4.19.1.2.3, p.280; xhci-data-structures.md section 10.4).
 *
 * The executor is hcd_enum.c: it reads PORTSC, asks here, performs the one
 * write asked for and feeds the outcome back. Nothing here touches a
 * register, takes a lock or calls a kernel service.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_LINK_H
#define XHCI_LINK_H

#include "xhci_compat.h"

/* What a USB3 port's PORTSC says its link is doing (XhciLinkClassify). */
#define XHCI_LINK_POWERED_OFF       0UL /* PP = 0                            */
#define XHCI_LINK_DISCONNECTED      1UL /* RxDetect, nothing trained         */
#define XHCI_LINK_POLLING           2UL /* training: wait for it             */
#define XHCI_LINK_ENABLED           3UL /* CCS, PED, U0/U1/U2 or Recovery    */
#define XHCI_LINK_SUSPENDED         4UL /* CCS, PED, U3                      */
#define XHCI_LINK_RESETTING         5UL /* PR, or PLS Hot Reset              */
#define XHCI_LINK_ERROR             6UL /* SS.Inactive: warm reset recovers  */
#define XHCI_LINK_COMPLIANCE        7UL /* Compliance Mode: warm reset       */
#define XHCI_LINK_DISABLED          8UL /* SS.Disabled: RxDetect re-arms     */
#define XHCI_LINK_NOT_ENABLED       9UL /* CCS with PED clear, otherwise
                                         * trained: reset it                 */
#define XHCI_LINK_UNKNOWN           10UL
#define XHCI_LINK_STATE_COUNT       11UL

/* What the thread wants of the link (XhciLinkDecide). */
#define XHCI_LINK_WANT_SERVICE      1UL /* a change was seen: recover if owed */
#define XHCI_LINK_WANT_RESET        2UL /* enumeration's port reset           */
#define XHCI_LINK_WANT_RESUME       3UL /* U3, or Resume with CCS and PED */
#define XHCI_LINK_WANT_SUSPEND      4UL
#define XHCI_LINK_WANT_HOLD         5UL /* 29-A.5: send it to SS.Disabled     */
#define XHCI_LINK_WANT_RELEASE      6UL /* 29-A.5: re-arm a held port         */

/* The one write, or none, the decision asks for. */
#define XHCI_LINK_ACT_NONE          0UL /* nothing to do now                  */
#define XHCI_LINK_ACT_WAIT          1UL /* the link is still moving: look
                                         * again on its next change          */
#define XHCI_LINK_ACT_READY         2UL /* trained and enabled: enumerate     */
#define XHCI_LINK_ACT_HOT_RESET     3UL /* PR = 1                             */
#define XHCI_LINK_ACT_WARM_RESET    4UL /* WPR = 1                            */
#define XHCI_LINK_ACT_SET_U0        5UL /* PLS = U0 with LWS: a USB3 resume   */
#define XHCI_LINK_ACT_SET_U3        6UL /* PLS = U3 with LWS                  */
#define XHCI_LINK_ACT_DISABLE       7UL /* PED = 1: link to SS.Disabled       */
#define XHCI_LINK_ACT_RX_DETECT     8UL /* PLS = RxDetect with LWS            */
#define XHCI_LINK_ACT_GIVE_UP       9UL /* warm resets spent: leave it to the
                                         * USB 2.0 companion, counted         */

/*
 * Warm resets one connection may take before the link is given up. Three is
 * this driver's choice, not a specification number: enough to see a link
 * that trains on a second attempt, few enough that a device which cannot
 * train is on its USB 2.0 path within a second or so.
 */
#define XHCI_LINK_MAX_WARM_RESETS   3UL

typedef struct _XHCI_LINK_PORT {
    ULONG WarmResets;       /* spent since the link was last Enabled,
                             * powered off, or Disconnected without CAS  */
    ULONG GaveUp;           /* given up until the budget next refills     */
    ULONG LastState;        /* XHCI_LINK_*, as last classified            */
    ULONG LastReset;        /* the reset last asked: HOT_RESET, WARM_RESET */
    ULONG StuckCheck;       /* 38.10: the next reading is the first after a
                             * restoring resume, with XhciStuckLinkReset
                             * applying (XhciLinkPortFeed)                */
} XHCI_LINK_PORT, *PXHCI_LINK_PORT;

typedef struct _XHCI_LINK_ACTION {
    ULONG Kind;             /* XHCI_LINK_ACT_*                            */
    ULONG Converted;        /* the enumeration asked for a reset and this
                             * driver's policy chose a warm one, the link
                             * not being in U0. Policy, not the xHC's own
                             * hot-to-warm conversion, which happens after
                             * a failed hot-reset handshake (4.19.5.1) and
                             * shows only as WRC at the reset's end      */
} XHCI_LINK_ACTION, *PXHCI_LINK_ACTION;

/* Clear a port's record: at a start, and when its port is powered. */
VOID XhciLinkInit(PXHCI_LINK_PORT link);

/*
 * Task 38.10, XhciStuckLinkReset (design record 20 section 4.11; Linux's
 * XHCI_MISSING_CAS, xhci-hub.c:1820-1841): 1 when a USB3 root port reads
 * powered, not resetting, with CCS and CAS both clear and its link in
 * Polling - a port these controllers can leave so after a resume without
 * the Cold Attach Status that would announce it. Compliance, Linux's other
 * half, is already warm-reset whatever its CAS (29-A.2).
 */
ULONG XhciLinkStuckPolling(ULONG portsc);

/* The link state one PORTSC value of a USB3 port describes. */
ULONG XhciLinkClassify(ULONG portsc);

/* One decision. Returns the action's kind, also written to *action. A want
 * that does not apply to the link's state asks for nothing. */
ULONG XhciLinkDecide(PXHCI_LINK_PORT link, ULONG portsc, ULONG want,
                     PXHCI_LINK_ACTION action);

/*
 * Whether a reset of either kind has finished, and how. `portsc` is read
 * after PRC was seen (or the wait ran out). 1 when the port came back
 * Enabled in U0, 0 otherwise; *warmSeen is WRC, which says the xHC ran a
 * warm reset whichever was asked (a hot reset it converted).
 */
ULONG XhciLinkResetDone(ULONG portsc, PULONG warmSeen);

/*
 * One root port's change, as the controller thread acts on it (hcd_enum.c,
 * hcdPortChanged): which events the port's enumeration machine is fed and
 * which link write, if any, is owed. `usb3` says the port is a USB3 protocol
 * port; `portsc` is the value read before its change bits were cleared;
 * `enumState` the machine's state (XHCI_ENUM_*). Returns XHCI_LINK_FEED_*
 * bits, fed in the order DISCONNECT, then the write in *action, then
 * CONNECT (the caller skips CONNECT on a halted controller).
 *
 * A USB 2.0 port, and a USB3 port whose link is usable, get the connect
 * rule of design record 13 section 5.3 unchanged. A USB3 port in SS.Inactive,
 * Compliance Mode, or Disconnected with Cold Attach Status gets the link
 * decision: whatever the machine held is fed a disconnect - a Failed machine
 * included, so the link's recovery finds it Empty and the trained link's
 * reset completion, which brings no CSC when CCS never dropped, starts a new
 * enumeration (Phase 29) - and the bounded warm reset or the give-up is the
 * action. The link's record is consulted on every change, so a disconnect
 * without Cold Attach Status, or a trained link, refills the warm-reset
 * budget; a Disconnected reading with CAS spends it and never refills it, so
 * a CAS that persists or returns after WPR is warm-reset at most
 * XHCI_LINK_MAX_WARM_RESETS times and then given up once.
 */
#define XHCI_LINK_FEED_DISCONNECT   0x1UL
#define XHCI_LINK_FEED_CONNECT      0x2UL

ULONG XhciLinkPortFeed(PXHCI_LINK_PORT link, ULONG usb3, ULONG portsc,
                       ULONG enumState, PXHCI_LINK_ACTION action);

/*
 * Task 45.41 (design record 13 section 15, L3): the warm reset
 * XhciLinkPortFeed charged to the budget is not written - the device is
 * kept and the restore's own reset decides and charges again - so the
 * charge is taken back: the count less one, never below 0. A give-up is
 * left as it is: XhciLinkPortFeed charges nothing when it gives up.
 */
VOID XhciLinkRefund(PXHCI_LINK_PORT link);

/*
 * Issue 29 (design record 13 section 21): a root port's link that keeps
 * failing - SS.Inactive or Compliance Mode, warm-reset, trained, failing
 * again - is counted here, and past XHCI_LINK_FLAP_LIMIT failures in one
 * window it is sent to its USB 2.0 companion by 29-A.5's hold (or, where no
 * hold can be had, given up) instead of being warm-reset for as long as the
 * device stays plugged in. 29-A.2's budget cannot bound it: every trained
 * reading refills that budget, and each of these warm resets ends trained.
 */
#define XHCI_LINK_FLAP_LIMIT        3UL     /* warm resets a window       */
#define XHCI_LINK_FLAP_WINDOW       600UL   /* tolerance-clock ticks, 60 s */

typedef struct _XHCI_LINK_FLAP {
    ULONG Count;            /* failures in the current window             */
    ULONG First;            /* the tick of the window's first             */
    ULONG Stopped;          /* given up: every failure until a clear      */
} XHCI_LINK_FLAP, *PXHCI_LINK_FLAP;

/* What one failure gets (XhciLinkFlapFailure). */
#define XHCI_FLAP_WARM              0UL /* warm-reset it, as before       */
#define XHCI_FLAP_SEND              1UL /* past the limit: send it to USB
                                         * 2.0 (the caller falls back to
                                         * XhciLinkFlapStop without one)  */
#define XHCI_FLAP_STOPPED           2UL /* stopped: give it up            */

VOID XhciLinkFlapInit(PXHCI_LINK_FLAP flap);

/* A sample (the PORTSC read before its changes were acknowledged) that
 * clears the record: PP clear, or a fresh attach - trained (CCS, PED, U0)
 * with CSC and neither WRC nor PRC, a link that trained with no reset of
 * the driver's. A warm reset's end (WRC, PRC) and an RxDetect reading do
 * not clear it. */
VOID XhciLinkFlapSample(PXHCI_LINK_FLAP flap, ULONG portsc);

/* One failure the inspection is about to warm-reset, at tick `now`
 * (unsigned arithmetic: the clock may wrap): counted in its window, a
 * failure after the window opening a new one at 1. Returns XHCI_FLAP_*. */
ULONG XhciLinkFlapFailure(PXHCI_LINK_FLAP flap, ULONG now);

/* The send could not be had: the record is stopped. Returns 1 when it was
 * not stopped before (the transition the note records). */
ULONG XhciLinkFlapStop(PXHCI_LINK_FLAP flap);

/* What a failure past the limit gets (F3, F4, F8): XHCI_FLAP_SEND only
 * with XhciSsFlapFallback on (`fallbackOn`) and a USB 2.0 path to send it
 * to (`usb2Path`: a root port's companion, a hub's USB 2.0 half);
 * XHCI_FLAP_STOPPED otherwise. A failure within the limit, or a stopped
 * one, keeps the verdict it had. Pure. */
ULONG XhciLinkFlapDecide(ULONG verdict, ULONG fallbackOn, ULONG usb2Path);

/*
 * Issue 29, a hub port's send (design record 13 section 21.5): the
 * SET_FEATURE(PORT_LINK_STATE) eSS.Disabled that sends it may fail on the
 * wire, and by then the port's change bits are acknowledged, its device
 * torn down and its recovery taken - an interrupt-driven hub reports
 * nothing more. So the send stays pending and is tried again on the
 * hub's own re-arm tick, at most XHCI_LINK_DISABLE_TRIES attempts in all;
 * after the last failure the link is given up as F4's stop gives it up.
 * XhciLinkDisableOutcome takes one attempt's result and says what
 * follows. Pure.
 */
#define XHCI_LINK_DISABLE_TRIES     3UL

#define XHCI_DISABLE_DONE           0UL /* sent: SS.Disabled             */
#define XHCI_DISABLE_RETRY          1UL /* failed: try again next tick   */
#define XHCI_DISABLE_GIVE_UP        2UL /* failed the last time: stop    */

typedef struct _XHCI_LINK_DISABLE {
    ULONG Tries;            /* attempts that failed since the send began */
} XHCI_LINK_DISABLE, *PXHCI_LINK_DISABLE;

VOID XhciLinkDisableInit(PXHCI_LINK_DISABLE d);
ULONG XhciLinkDisableOutcome(PXHCI_LINK_DISABLE d, ULONG ok);

/* A recovery that reaches the send again: 1 when an attempt is made now
 * (the record begun afresh), 0 while a send is already `pending`, whose
 * attempts and count stay the tick's. Pure. */
ULONG XhciLinkDisableEnter(PXHCI_LINK_DISABLE d, ULONG pending);

/* ------------------------------------------------------------------ */
/* 29-A.5's hold                                                       */
/* ------------------------------------------------------------------ */

/* The serial string, as UTF-16 code units, kept up to this many. A longer
 * serial is truncated, and a truncated identity proves nothing: it neither
 * makes a hold identified nor matches one (Phase 29). USB string descriptors
 * carry at most 126. */
#define XHCI_LINK_SERIAL_CHARS      64UL

typedef struct _XHCI_LINK_IDENTITY {
    ULONG Valid;            /* the descriptor read succeeded              */
    ULONG Vendor;
    ULONG Product;
    ULONG SerialLength;     /* code units; 0 = the device has no serial   */
    USHORT Serial[XHCI_LINK_SERIAL_CHARS];
} XHCI_LINK_IDENTITY, *PXHCI_LINK_IDENTITY;

/* The hold's kinds, which 29-A.5 counts apart. */
#define XHCI_HOLD_NONE              0UL
#define XHCI_HOLD_PAIRED            1UL /* identified, companion-paired:
                                         * releasable                       */
#define XHCI_HOLD_UNIDENTIFIED      2UL /* no serial, no descriptor yet, or
                                         * a failed identity read: until the
                                         * controller's next start          */
#define XHCI_HOLD_ORPHAN            3UL /* no USB 2.0 companion: likewise   */
#define XHCI_HOLD_FLAP              4UL /* issue 29: a failing link's, with
                                         * no identity: released by the
                                         * companion's first departure
                                         * after a connect                  */

typedef struct _XHCI_LINK_HOLD {
    ULONG Kind;             /* XHCI_HOLD_*                                */
    ULONG Port;             /* the held USB3 port                         */
    ULONG Companion;        /* its USB 2.0 companion, 0 for none          */
    ULONG ConnectSeen;      /* the companion reported a connect since the
                             * hold began, or since its last disconnect  */
    ULONG MatchSeen;        /* ...and the device that enumerated there is
                             * the held one                              */
    ULONG OthersSeen;       /* companion connects of some other device    */
    XHCI_LINK_IDENTITY Held;
} XHCI_LINK_HOLD, *PXHCI_LINK_HOLD;

/* What a companion event means for the hold. */
#define XHCI_HOLD_KEEP              0UL
#define XHCI_HOLD_RELEASE           1UL /* re-arm: XHCI_LINK_WANT_RELEASE  */

/* Begin a hold on `port`, whose companion is `companion` (0: none), of the
 * device `identity` describes (NULL: none read). Returns its kind. */
ULONG XhciHoldBegin(PXHCI_LINK_HOLD hold, ULONG port, ULONG companion,
                    const XHCI_LINK_IDENTITY *identity);

/* Begin issue 29's hold (XHCI_HOLD_FLAP) on `port`, whose companion is
 * `companion`; with no companion it is an orphan, as XhciHoldBegin makes
 * it (the caller sends no such port). Returns its kind. */
ULONG XhciHoldBeginFlap(PXHCI_LINK_HOLD hold, ULONG port, ULONG companion);

/* The companion port reported a connect. */
VOID XhciHoldCompanionConnect(PXHCI_LINK_HOLD hold);

/* The device that enumerated on the companion: `identity` its descriptor
 * read (Valid 0 when the read failed). Counted as another device unless it
 * is the held one. */
VOID XhciHoldCompanionIdentity(PXHCI_LINK_HOLD hold,
                               const XHCI_LINK_IDENTITY *identity);

/* The companion port reported a disconnect: KEEP or RELEASE. A release
 * ends the hold (Kind NONE); a keep forgets what the companion showed, so
 * the next device there is judged afresh. */
ULONG XhciHoldCompanionDisconnect(PXHCI_LINK_HOLD hold);

/*
 * One PORTSC change of the companion port, as the executor reads it
 * (hcd_enum.c): a device left - CSC, or CCS clear - is
 * XhciHoldCompanionDisconnect, and a device arrived - CSC with CCS - is
 * XhciHoldCompanionConnect, in that order, so a replug is a departure and
 * an arrival. Called before the companion's enumeration machine is fed, so
 * the connect is recorded before the device's descriptor is read. Returns
 * KEEP or RELEASE; after a RELEASE the arrival is not recorded, there being
 * no hold left to record it in.
 */
ULONG XhciHoldCompanionPortsc(PXHCI_LINK_HOLD hold, ULONG portsc);

/* Whether two identities are one device: both valid, the same vendor and
 * product id, and the same nonempty serial. */
ULONG XhciLinkSameDevice(const XHCI_LINK_IDENTITY *a,
                         const XHCI_LINK_IDENTITY *b);

#endif /* XHCI_LINK_H */
