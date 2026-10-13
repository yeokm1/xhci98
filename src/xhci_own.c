/*
 * xhci_own.c - who holds an endpoint's ring, the pure half (xhci_own.h;
 * design record 20 section 5; roadmap-hcd.md task 38.10).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_own.h"

/* xHCI 1.2c Table 6-90: Success. */
#define XHCI_OWN_CC_SUCCESS     1UL

VOID XhciOwnSelectBegin(PXHCI_OWN_SELECT s, ULONG rollback, ULONG single,
                        ULONG streams)
{
    s->Phase = XHCI_OWN_PH_CONFIGURE;
    s->Rollback = rollback ? 1UL : 0UL;
    s->Single = single ? 1UL : 0UL;
    s->Streams = streams ? 1UL : 0UL;
}

ULONG XhciOwnCommandEvent(ULONG code)
{
    if (code == XHCI_OWN_NOT_ISSUED_CODE) {
        return XHCI_OWN_EV_CMD_NOT_ISSUED;
    }
    if (code == 0) {
        return XHCI_OWN_EV_CMD_UNKNOWN;
    }
    if (code == XHCI_OWN_CC_SUCCESS) {
        return XHCI_OWN_EV_CMD_SUCCESS;
    }
    return XHCI_OWN_EV_CMD_REFUSED;
}

/* The end of a select: the phase closed and the answer given. */
static ULONG xhciOwnEnd(PXHCI_OWN_SELECT s, PULONG answer, ULONG ans,
                        ULONG actions)
{
    s->Phase = XHCI_OWN_PH_END;
    *answer = ans;
    return actions;
}

/*
 * The STALL on an interface with other settings, or a one-setting STALL
 * whose clears failed: the old setting re-instated when the select may and
 * no old pipe had streams; otherwise as an outcome not established - the
 * new endpoints held stale, the old objects freed (their rings were
 * superseded by the successful command), the device's STALL answered.
 */
static ULONG xhciOwnStalled(PXHCI_OWN_SELECT s, PULONG answer)
{
    if (s->Rollback && !s->Streams) {
        s->Phase = XHCI_OWN_PH_ROLLBACK;
        return XHCI_OWN_A_ROLLBACK;
    }
    return xhciOwnEnd(s, answer, XHCI_OWN_ANS_STALL,
                      XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD);
}

ULONG XhciOwnSelectStep(PXHCI_OWN_SELECT s, ULONG event, PULONG answer)
{
    *answer = XHCI_OWN_ANS_PENDING;
    switch (s->Phase) {
    case XHCI_OWN_PH_CONFIGURE:
        switch (event) {
        case XHCI_OWN_EV_CMD_NOT_ISSUED:
        case XHCI_OWN_EV_CMD_REFUSED:
            /* Nothing changed (4.6.6): the new rings were never seen, and
             * the old pipes go back with their own handles, the interface
             * at its old setting. Stale endpoints stay stale. */
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_COMMAND,
                              XHCI_OWN_A_FREE_NEW |
                                  XHCI_OWN_A_REATTACH_OLD);
        case XHCI_OWN_EV_CMD_SUCCESS:
            /* The old allocations and every stale one superseded, the new
             * owned. The old pipe objects are kept for a rollback; their
             * stream blocks are not, since a rollback re-adds an endpoint
             * without streams. */
            s->Phase = XHCI_OWN_PH_SETIF;
            return XHCI_OWN_A_STALE_CLEARED | XHCI_OWN_A_FREE_OLD_STREAMS |
                   XHCI_OWN_A_SET_INTERFACE;
        case XHCI_OWN_EV_CMD_UNKNOWN:
        default:
            break;
        }
        break;
    case XHCI_OWN_PH_SETIF:
        switch (event) {
        case XHCI_OWN_EV_SETIF_DONE:
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_SUCCESS,
                              XHCI_OWN_A_FREE_OLD | XHCI_OWN_A_PUBLISH_NEW);
        case XHCI_OWN_EV_SETIF_STALLED:
            if (s->Rollback && s->Single) {
                /* USB 2.0 and 3.2 9.4.10: an interface with only a default
                 * setting may STALL the request; it is carried out by the
                 * clears instead, as Linux's manual set_interface does
                 * (owner, 2026-10-14). */
                s->Phase = XHCI_OWN_PH_CLEARS;
                return XHCI_OWN_A_CLEAR_NEW;
            }
            return xhciOwnStalled(s, answer);
        case XHCI_OWN_EV_SETIF_FAILED:
            /* The device may or may not have moved: nothing re-instated on
             * a guess. */
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_FAILED,
                              XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD);
        default:
            break;
        }
        break;
    case XHCI_OWN_PH_CLEARS:
        switch (event) {
        case XHCI_OWN_EV_CLEARS_DONE:
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_SUCCESS,
                              XHCI_OWN_A_FREE_OLD | XHCI_OWN_A_PUBLISH_NEW);
        case XHCI_OWN_EV_CLEARS_FAILED:
            return xhciOwnStalled(s, answer);
        default:
            break;
        }
        break;
    case XHCI_OWN_PH_ROLLBACK:
        switch (event) {
        case XHCI_OWN_EV_CMD_NOT_ISSUED:
        case XHCI_OWN_EV_CMD_REFUSED:
            /* Nothing changed: the new endpoints stay installed, stale. */
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_STALL,
                              XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD);
        case XHCI_OWN_EV_CMD_SUCCESS:
            /* The old allocations owned again, the new superseded; the
             * device's toggles restarted before anything reopens. */
            s->Phase = XHCI_OWN_PH_RECLEARS;
            return XHCI_OWN_A_FREE_NEW | XHCI_OWN_A_CLEAR_OLD;
        case XHCI_OWN_EV_CMD_UNKNOWN:
        default:
            break;
        }
        break;
    case XHCI_OWN_PH_RECLEARS:
        switch (event) {
        case XHCI_OWN_EV_CLEARS_DONE:
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_STALL,
                              XHCI_OWN_A_REATTACH_OLD);
        case XHCI_OWN_EV_CLEARS_FAILED:
            /* Installed and owned, but not reopened over a device whose
             * toggles are in doubt. */
            return xhciOwnEnd(s, answer, XHCI_OWN_ANS_STALL,
                              XHCI_OWN_A_RETAIN_OLD);
        default:
            break;
        }
        break;
    default:
        break;
    }
    /* An unknown command outcome, or a step the phase did not wait for:
     * whatever is held - old, new or both - stays held until the slot goes.
     * After the step 3 command the old pipes are held, and after the
     * rollback both; before either ended nothing was released. */
    return xhciOwnEnd(s, answer,
                      (s->Phase == XHCI_OWN_PH_ROLLBACK ||
                       s->Phase == XHCI_OWN_PH_RECLEARS ||
                       s->Phase == XHCI_OWN_PH_CLEARS ||
                       s->Phase == XHCI_OWN_PH_SETIF)
                          ? XHCI_OWN_ANS_STALL
                          : XHCI_OWN_ANS_COMMAND,
                      XHCI_OWN_A_PIN_NEW | XHCI_OWN_A_PIN_OLD);
}

ULONG XhciOwnDropStep(ULONG event)
{
    switch (event) {
    case XHCI_OWN_EV_CMD_SUCCESS:
        return XHCI_OWN_A_STALE_CLEARED | XHCI_OWN_A_FREE_OLD_STREAMS |
               XHCI_OWN_A_FREE_OLD;
    case XHCI_OWN_EV_CMD_NOT_ISSUED:
    case XHCI_OWN_EV_CMD_REFUSED:
        return XHCI_OWN_A_RETAIN_OLD;
    default:
        return XHCI_OWN_A_PIN_OLD;
    }
}

ULONG XhciOwnSingleSetting(const UCHAR *config, ULONG length, ULONG number)
{
    ULONG total;
    ULONG at;
    ULONG len;
    ULONG seen;

    if (config == NULL || length < 9 || config[0] < 9 || config[1] != 2) {
        return 0;
    }
    total = (ULONG)config[2] | ((ULONG)config[3] << 8);
    if (total > length) {
        /* The bytes stop short of the advertised end: another setting may
         * lie past them. */
        return 0;
    }
    seen = 0;
    at = config[0];
    while (at < total) {
        if (total - at < 2) {
            return 0;
        }
        len = config[at];
        if (len < 2 || len > total - at) {
            return 0;
        }
        if (config[at + 1] == 4) {
            if (len < 9) {
                return 0;
            }
            if ((ULONG)config[at + 2] == number) {
                seen++;
            }
        }
        at += len;
    }
    return seen == 1;
}
