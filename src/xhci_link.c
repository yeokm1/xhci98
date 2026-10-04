/*
 * xhci_link.c - the SuperSpeed link of a USB3 protocol root port (xhci_link.h;
 * roadmap-hcd.md tasks 29-A.2 and 29-A.5).
 *
 * The states, as xHCI 1.2c Figure 4-27 (p.279) draws a USB3 root port and
 * as PORTSC shows them (xhci-data-structures.md section 10.4, verified
 * against the figure and 4.19.1.2, p.279-286). The figure forces (PP, CCS,
 * PED, PR) on entry to each state:
 *
 *   Powered-off   PP = 0                       (0,0,0,0)
 *   Disconnected  PLS = RxDetect, CCS = 0      (1,0,0,0)
 *   Polling       PLS = Polling: link training, which on success takes the
 *                 port straight to Enabled with CCS, PED and CSC set - a
 *                 USB3 port needs no reset to be enabled, unlike a USB2 one;
 *                 a training failure goes to Disconnected, a configuration
 *                 failure to Error with CEC        (1,0,0,0)
 *   Enabled       CCS = 1, PED = 1, PLS = U0, U1, U2, U3, Resume or
 *                 Recovery                     (1,1,1,0)
 *   Reset         PR = 1 (a hot reset, PLS = Hot Reset) or a warm reset;
 *                 success to Enabled, failure to Disconnected with PRC set
 *                 either way                   (1,1,0,1)
 *   Error         PLS = Inactive (SS.Inactive), CCS = 0: left by PR or WPR
 *                 (to Reset), a disconnect, PED = 1 (to Disabled), PP = 0
 *                 or HCRST (1,0,0,0)
 *   Compliance    PLS = Compliance Mode: left by WPR or HCRST (to Reset),
 *                 PED = 1, PP = 0              (1,0,0,0)
 *   Disabled      PLS = Disabled after software wrote PED = 1 (SS.Disabled):
 *                 left only by a PLS = RxDetect write or HCRST, to
 *                 Disconnected, or by PP = 0; a disconnect does not leave
 *                 it and raises no CSC (4.19.1.2.3, p.280)   (1,0,0,0)
 *
 * PEC is never set on a USB3 port (Table 5-27, p.377).
 *
 * The bus's policy over them:
 *
 *   - an Enabled port is enumerated as it stands; the enumeration's own
 *     reset is a hot reset (PR) from U0, and a warm one (WPR) from any other
 *     state - U1, U2, U3, Error, Compliance, or trained with PED clear. That
 *     is this driver's policy, not the xHC's rule: PR is legal in every
 *     Enabled substate, and the xHC converts a hot reset to a warm one by
 *     itself only when the hot-reset handshake fails (4.19.5.1 footnote 66;
 *     Codex review of Phase 29, round 1, finding 5), which the caller sees
 *     as WRC at the reset's end and counts from that;
 *   - Error and Compliance are recovered with a warm reset when they are
 *     seen, at most XHCI_LINK_MAX_WARM_RESETS times for one connection, and
 *     then given up: the device, finding no SuperSpeed partner, connects on
 *     its USB 2.0 path (USB 3.2 7.5.1.2 and 10.18.1, USB 3.2 p.162-163 and
 *     p.456, verified: a peripheral's upstream port that cannot train
 *     reaches eSS.Disabled and connects on USB 2.0), and 29-A.5 counts it;
 *   - a resume from U3 is one PLS = U0 write, not USB2's Resume-then-U0;
 *   - a hold writes PED = 1 on a trained link; a release writes PLS =
 *     RxDetect on a Disabled one, and never a warm reset, which does not act
 *     on a Disabled port (29-A.5).
 *
 * C89, pure: IRQL any.
 */

#include "xhci.h"
#include "xhci_enum.h"
#include "xhci_link.h"

VOID XhciLinkInit(PXHCI_LINK_PORT link)
{
    if (link == NULL) {
        return;
    }
    link->WarmResets = 0;
    link->GaveUp = 0;
    link->LastState = XHCI_LINK_UNKNOWN;
    link->LastReset = XHCI_LINK_ACT_NONE;
}

ULONG XhciLinkClassify(ULONG portsc)
{
    ULONG pls;
    ULONG connected;
    ULONG enabled;

    if (portsc == 0xFFFFFFFFUL) {
        return XHCI_LINK_UNKNOWN;
    }
    if ((portsc & XHCI_PORTSC_PP) == 0) {
        return XHCI_LINK_POWERED_OFF;
    }
    if ((portsc & XHCI_PORTSC_PR) != 0) {
        return XHCI_LINK_RESETTING;
    }
    connected = (portsc & XHCI_PORTSC_CCS) != 0;
    enabled = (portsc & XHCI_PORTSC_PED) != 0;
    pls = XHCI_PORTSC_GET_PLS(portsc);
    switch (pls) {
    case XHCI_PLS_HOT_RESET:
        return XHCI_LINK_RESETTING;
    case XHCI_PLS_INACTIVE:
        return XHCI_LINK_ERROR;
    case XHCI_PLS_COMPLIANCE:
        return XHCI_LINK_COMPLIANCE;
    case XHCI_PLS_DISABLED:
        return XHCI_LINK_DISABLED;
    case XHCI_PLS_POLLING:
        return XHCI_LINK_POLLING;
    case XHCI_PLS_RX_DETECT:
        return connected ? XHCI_LINK_UNKNOWN : XHCI_LINK_DISCONNECTED;
    case XHCI_PLS_U3:
        if (connected && enabled) {
            return XHCI_LINK_SUSPENDED;
        }
        return connected ? XHCI_LINK_NOT_ENABLED : XHCI_LINK_UNKNOWN;
    case XHCI_PLS_U0:
    case XHCI_PLS_U1:
    case XHCI_PLS_U2:
    case XHCI_PLS_RECOVERY:
        if (connected && enabled) {
            return XHCI_LINK_ENABLED;
        }
        return connected ? XHCI_LINK_NOT_ENABLED : XHCI_LINK_UNKNOWN;
    default:
        /* Test Mode, Resume (a USB2 encoding) and the reserved values. */
        return XHCI_LINK_UNKNOWN;
    }
}

static ULONG xhciLinkAct(PXHCI_LINK_ACTION action, ULONG kind,
                         ULONG converted)
{
    action->Kind = kind;
    action->Converted = converted;
    return kind;
}

/* A warm reset, while the connection has any left; the give-up otherwise. */
static ULONG xhciLinkWarm(PXHCI_LINK_PORT link, PXHCI_LINK_ACTION action,
                          ULONG converted)
{
    if (link->GaveUp) {
        return xhciLinkAct(action, XHCI_LINK_ACT_NONE, 0);
    }
    if (link->WarmResets >= XHCI_LINK_MAX_WARM_RESETS) {
        link->GaveUp = 1;
        return xhciLinkAct(action, XHCI_LINK_ACT_GIVE_UP, 0);
    }
    link->WarmResets++;
    link->LastReset = XHCI_LINK_ACT_WARM_RESET;
    return xhciLinkAct(action, XHCI_LINK_ACT_WARM_RESET, converted);
}

ULONG XhciLinkDecide(PXHCI_LINK_PORT link, ULONG portsc, ULONG want,
                     PXHCI_LINK_ACTION action)
{
    XHCI_LINK_ACTION scratch;
    ULONG state;

    if (action == NULL) {
        action = &scratch;
    }
    xhciLinkAct(action, XHCI_LINK_ACT_NONE, 0);
    if (link == NULL) {
        return XHCI_LINK_ACT_NONE;
    }
    state = XhciLinkClassify(portsc);
    link->LastState = state;
    /* A trained link, or none at all, starts the warm-reset budget again:
     * the next failure is a new connection's. */
    if (state == XHCI_LINK_ENABLED || state == XHCI_LINK_DISCONNECTED ||
        state == XHCI_LINK_POWERED_OFF) {
        link->WarmResets = 0;
        link->GaveUp = 0;
    }

    switch (want) {
    case XHCI_LINK_WANT_SERVICE:
        switch (state) {
        case XHCI_LINK_ERROR:
        case XHCI_LINK_COMPLIANCE:
            return xhciLinkWarm(link, action, 0);
        case XHCI_LINK_ENABLED:
            return xhciLinkAct(action, XHCI_LINK_ACT_READY, 0);
        case XHCI_LINK_POLLING:
        case XHCI_LINK_RESETTING:
            return xhciLinkAct(action, XHCI_LINK_ACT_WAIT, 0);
        case XHCI_LINK_DISCONNECTED:
            /* Cold Attach Status: far-end terminations seen in
             * Disconnected that the port could not take to Enabled
             * (asserted around D3, 4.19.8). "Software shall clear this bit
             * by writing a '1' to WPR" (Table 5-27, p.379), and 4.19.8
             * (p.304) has software "issue a Warm Port Reset (WPR) to any
             * port if it is asserted"; verified. */
            if ((portsc & XHCI_PORTSC_CAS) != 0) {
                return xhciLinkWarm(link, action, 0);
            }
            return XHCI_LINK_ACT_NONE;
        default:
            return XHCI_LINK_ACT_NONE;
        }

    case XHCI_LINK_WANT_RESET:
        switch (state) {
        case XHCI_LINK_ENABLED:
            if (XHCI_PORTSC_GET_PLS(portsc) == XHCI_PLS_U0) {
                link->LastReset = XHCI_LINK_ACT_HOT_RESET;
                return xhciLinkAct(action, XHCI_LINK_ACT_HOT_RESET, 0);
            }
            /* U1, U2 or Recovery: on its way to U0, where a hot reset can
             * start; the warm one does not have to wait for it. */
            return xhciLinkWarm(link, action, 1);
        case XHCI_LINK_SUSPENDED:
        case XHCI_LINK_NOT_ENABLED:
        case XHCI_LINK_ERROR:
        case XHCI_LINK_COMPLIANCE:
            return xhciLinkWarm(link, action, 1);
        default:
            /* Nothing trained to reset, a reset already running, or a
             * Disabled port a warm reset would not act on. */
            return XHCI_LINK_ACT_NONE;
        }

    case XHCI_LINK_WANT_RESUME:
        if (state == XHCI_LINK_SUSPENDED) {
            return xhciLinkAct(action, XHCI_LINK_ACT_SET_U0, 0);
        }
        return XHCI_LINK_ACT_NONE;

    case XHCI_LINK_WANT_SUSPEND:
        if (state == XHCI_LINK_ENABLED &&
            XHCI_PORTSC_GET_PLS(portsc) == XHCI_PLS_U0) {
            return xhciLinkAct(action, XHCI_LINK_ACT_SET_U3, 0);
        }
        return XHCI_LINK_ACT_NONE;

    case XHCI_LINK_WANT_HOLD:
        if (state == XHCI_LINK_ENABLED || state == XHCI_LINK_SUSPENDED) {
            return xhciLinkAct(action, XHCI_LINK_ACT_DISABLE, 0);
        }
        return XHCI_LINK_ACT_NONE;

    case XHCI_LINK_WANT_RELEASE:
        if (state == XHCI_LINK_DISABLED) {
            return xhciLinkAct(action, XHCI_LINK_ACT_RX_DETECT, 0);
        }
        return XHCI_LINK_ACT_NONE;

    default:
        return XHCI_LINK_ACT_NONE;
    }
}

ULONG XhciLinkResetDone(ULONG portsc, PULONG warmSeen)
{
    if (warmSeen != NULL) {
        *warmSeen = 0;
    }
    if (portsc == 0xFFFFFFFFUL) {
        return 0;
    }
    if (warmSeen != NULL) {
        *warmSeen = (portsc & XHCI_PORTSC_WRC) != 0 ? 1UL : 0UL;
    }
    return (portsc & XHCI_PORTSC_PRC) != 0 &&
           (portsc & XHCI_PORTSC_CCS) != 0 &&
           (portsc & XHCI_PORTSC_PED) != 0 &&
           XHCI_PORTSC_GET_PLS(portsc) == XHCI_PLS_U0;
}

ULONG XhciLinkPortFeed(PXHCI_LINK_PORT link, ULONG usb3, ULONG portsc,
                       ULONG enumState, PXHCI_LINK_ACTION action)
{
    XHCI_LINK_ACTION scratch;
    ULONG state;
    ULONG feed;

    if (action == NULL) {
        action = &scratch;
    }
    xhciLinkAct(action, XHCI_LINK_ACT_NONE, 0);
    feed = 0;
    if (usb3 && link != NULL) {
        state = XhciLinkClassify(portsc);
        (VOID)XhciLinkDecide(link, portsc, XHCI_LINK_WANT_SERVICE, action);
        if (state == XHCI_LINK_ERROR || state == XHCI_LINK_COMPLIANCE ||
            (state == XHCI_LINK_DISCONNECTED &&
             (portsc & XHCI_PORTSC_CAS) != 0)) {
            if (enumState != XHCI_ENUM_EMPTY && enumState != XHCI_ENUM_GONE) {
                feed |= XHCI_LINK_FEED_DISCONNECT;
            }
            return feed;
        }
        /* Usable: the decision's READY or WAIT asks for no write. */
        xhciLinkAct(action, XHCI_LINK_ACT_NONE, 0);
    }
    if ((portsc & XHCI_PORTSC_CSC) != 0) {
        if (enumState != XHCI_ENUM_EMPTY && enumState != XHCI_ENUM_FAILED &&
            enumState != XHCI_ENUM_GONE) {
            feed |= XHCI_LINK_FEED_DISCONNECT;
        }
        if ((portsc & XHCI_PORTSC_CCS) != 0) {
            feed |= XHCI_LINK_FEED_CONNECT;
        }
    } else if ((portsc & XHCI_PORTSC_CCS) == 0) {
        feed |= XHCI_LINK_FEED_DISCONNECT;
    } else if (enumState == XHCI_ENUM_EMPTY) {
        feed |= XHCI_LINK_FEED_CONNECT;
    }
    return feed;
}

/* ------------------------------------------------------------------ */
/* 29-A.5's hold                                                       */
/* ------------------------------------------------------------------ */

ULONG XhciLinkSameDevice(const XHCI_LINK_IDENTITY *a,
                         const XHCI_LINK_IDENTITY *b)
{
    ULONG n;
    ULONG i;

    if (a == NULL || b == NULL || !a->Valid || !b->Valid) {
        return 0;
    }
    if (a->Vendor != b->Vendor || a->Product != b->Product) {
        return 0;
    }
    /* No serial is no identity: two units of one model cannot be told
     * apart, so vendor and product id alone match nothing. */
    if (a->SerialLength == 0 || a->SerialLength != b->SerialLength ||
        a->SerialLength > XHCI_LINK_SERIAL_CHARS) {
        return 0;
    }
    n = a->SerialLength;
    for (i = 0; i < n; i++) {
        if (a->Serial[i] != b->Serial[i]) {
            return 0;
        }
    }
    return 1;
}

ULONG XhciHoldBegin(PXHCI_LINK_HOLD hold, ULONG port, ULONG companion,
                    const XHCI_LINK_IDENTITY *identity)
{
    ULONG i;

    if (hold == NULL) {
        return XHCI_HOLD_NONE;
    }
    hold->Port = port;
    hold->Companion = companion;
    hold->ConnectSeen = 0;
    hold->MatchSeen = 0;
    hold->OthersSeen = 0;
    hold->Held.Valid = 0;
    hold->Held.Vendor = 0;
    hold->Held.Product = 0;
    hold->Held.SerialLength = 0;
    for (i = 0; i < XHCI_LINK_SERIAL_CHARS; i++) {
        hold->Held.Serial[i] = 0;
    }
    if (identity != NULL) {
        hold->Held = *identity;
    }
    if (companion == 0) {
        hold->Kind = XHCI_HOLD_ORPHAN;
    } else if (!hold->Held.Valid || hold->Held.SerialLength == 0 ||
               hold->Held.SerialLength > XHCI_LINK_SERIAL_CHARS) {
        hold->Kind = XHCI_HOLD_UNIDENTIFIED;
    } else {
        hold->Kind = XHCI_HOLD_PAIRED;
    }
    return hold->Kind;
}

VOID XhciHoldCompanionConnect(PXHCI_LINK_HOLD hold)
{
    if (hold == NULL || hold->Kind == XHCI_HOLD_NONE) {
        return;
    }
    hold->ConnectSeen = 1;
    hold->MatchSeen = 0;
}

VOID XhciHoldCompanionIdentity(PXHCI_LINK_HOLD hold,
                               const XHCI_LINK_IDENTITY *identity)
{
    if (hold == NULL || hold->Kind == XHCI_HOLD_NONE || !hold->ConnectSeen) {
        return;
    }
    if (hold->Kind == XHCI_HOLD_PAIRED &&
        XhciLinkSameDevice(&hold->Held, identity)) {
        hold->MatchSeen = 1;
    } else {
        hold->OthersSeen++;
    }
}

ULONG XhciHoldCompanionPortsc(PXHCI_LINK_HOLD hold, ULONG portsc)
{
    ULONG answer;

    if (hold == NULL || hold->Kind == XHCI_HOLD_NONE ||
        portsc == 0xFFFFFFFFUL) {
        return XHCI_HOLD_KEEP;
    }
    answer = XHCI_HOLD_KEEP;
    if ((portsc & XHCI_PORTSC_CSC) != 0 ||
        (portsc & XHCI_PORTSC_CCS) == 0) {
        answer = XhciHoldCompanionDisconnect(hold);
    }
    if (answer == XHCI_HOLD_KEEP && (portsc & XHCI_PORTSC_CSC) != 0 &&
        (portsc & XHCI_PORTSC_CCS) != 0) {
        XhciHoldCompanionConnect(hold);
    }
    return answer;
}

ULONG XhciHoldCompanionDisconnect(PXHCI_LINK_HOLD hold)
{
    if (hold == NULL || hold->Kind == XHCI_HOLD_NONE) {
        return XHCI_HOLD_KEEP;
    }
    if (hold->Kind == XHCI_HOLD_PAIRED && hold->ConnectSeen &&
        hold->MatchSeen) {
        hold->Kind = XHCI_HOLD_NONE;
        hold->ConnectSeen = 0;
        hold->MatchSeen = 0;
        return XHCI_HOLD_RELEASE;
    }
    hold->ConnectSeen = 0;
    hold->MatchSeen = 0;
    return XHCI_HOLD_KEEP;
}
