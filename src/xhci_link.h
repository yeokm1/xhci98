/*
 * xhci_link.h - the SuperSpeed link of a USB3 protocol root port, its pure
 * half (roadmap-hcd.md tasks 29-A.2 and 29-A.5; design record 13 section 2;
 * xhci-data-structures.md section 10, where every PORTSC fact this file
 * rests on is transcribed and marked for verification against the PDF).
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
 *     behind-a-switch.md section 6.1 as the roadmap tightened them: on the
 *     companion's disconnect, after a companion connect seen since the hold
 *     began, of the very device held (vendor id, product id and serial
 *     string). A hold with no identity, and an orphan's, last until the
 *     controller's next start.
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
#define XHCI_LINK_WANT_RESUME       3UL
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
    ULONG WarmResets;       /* spent since the link was last Enabled or
                             * Disconnected                              */
    ULONG GaveUp;           /* given up until the link next leaves Error  */
    ULONG LastState;        /* XHCI_LINK_*, as last classified            */
    ULONG LastReset;        /* the reset last asked: HOT_RESET, WARM_RESET */
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
 * enumeration (Codex review of Phase 29, round 1, finding 2) - and the
 * bounded warm reset or the give-up is the action. The link's record is
 * consulted on every change, so a disconnect or a trained link refills the
 * warm-reset budget (finding 3).
 */
#define XHCI_LINK_FEED_DISCONNECT   0x1UL
#define XHCI_LINK_FEED_CONNECT      0x2UL

ULONG XhciLinkPortFeed(PXHCI_LINK_PORT link, ULONG usb3, ULONG portsc,
                       ULONG enumState, PXHCI_LINK_ACTION action);

/* ------------------------------------------------------------------ */
/* 29-A.5's hold                                                       */
/* ------------------------------------------------------------------ */

/* The serial string, as UTF-16 code units, kept up to this many. A longer
 * serial is truncated, and a truncated identity proves nothing: it neither
 * makes a hold identified nor matches one (Codex review of Phase 29, round
 * 1, note 6). USB string descriptors carry at most 126. */
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

/* Whether two identities are one device: both valid, the same vendor and
 * product id, and the same nonempty serial. */
ULONG XhciLinkSameDevice(const XHCI_LINK_IDENTITY *a,
                         const XHCI_LINK_IDENTITY *b);

#endif /* XHCI_LINK_H */
