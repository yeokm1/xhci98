/*
 * xhci_enum.c - the enumeration state machine (xhci_enum.h; design record 13
 * section 5.3).
 *
 * The transitions, in the order a device meets them:
 *
 *   Empty      --connect-->          Debounce   [debounce]
 *   Debounce   --still connected-->  Reset      [reset]
 *   Reset      --done, speed ok-->   EnableSlot [enable slot]
 *   EnableSlot --slot-->             Address    [address, EP0 at the speed's
 *                                                initial packet size]
 *   Address    --ok-->               Desc8      [GET_DESCRIPTOR(DEVICE, 8)]
 *   Desc8      --size differs-->     Evaluate   [Evaluate Context]
 *              --size agrees-->      Desc18     [GET_DESCRIPTOR(DEVICE, 18)]
 *   Evaluate   --ok-->               Desc18     [GET_DESCRIPTOR(DEVICE, 18)]
 *   Desc18     --18 bytes-->         Config9    [GET_DESCRIPTOR(CONFIG, 9)]
 *              --18 bytes, SS-->     Bos5       [GET_DESCRIPTOR(BOS, 5)]
 *   Bos5       --wTotalLength-->     BosFull    [GET_DESCRIPTOR(BOS, all)]
 *              --failed-->           Config9    (BosMissing)
 *   BosFull    --all of it, or not-> Config9
 *   Config9    --wTotalLength-->     ConfigFull [GET_DESCRIPTOR(CONFIG, all)]
 *   ConfigFull --all of it-->        Present    [create the PDO]
 *   Present    --PDO started-->      Bound
 *
 * A disconnect from any state with a slot disables it; with a PDO it reports
 * the PDO missing and waits in Gone for the PDO's remove. Any failed step
 * goes to Failed, disabling the slot it owned; a RETRY event then starts
 * once more from Reset (XHCI_ENUM_RETRIES), the rule the targets' own hub
 * drivers follow.
 *
 * There is no SET_ADDRESS anywhere - the bus addresses with Address Device -
 * and no speed lie: the speed the reset reports is the speed carried on.
 *
 * SuperSpeed (task 29-A.3) changes three things: EP0 starts at 512 and stays
 * there, since bMaxPacketSize0 must be 09h - the exponent - and nothing else
 * is legal; and the BOS descriptor is read after the device descriptor. A
 * BOS read that fails does not fail the enumeration: USB 3.2 makes the BOS
 * mandatory for such a device, but nothing this bus does at Phase 29 needs
 * it (U1/U2 are never enabled, so the exit latencies it carries go unused),
 * and a device that answers everything else is served without it, with
 * BosMissing saying so.
 *
 * C89, pure: IRQL any.
 */

#include "xhci.h"
#include "xhci_enum.h"

#define XHCI_ENUM_EV_RETRY_INTERNAL 0x100UL

ULONG XhciEnumInitialMps0(ULONG speed)
{
    switch (speed) {
    case XHCI_ENUM_SPEED_LOW:
        return 8UL;
    case XHCI_ENUM_SPEED_FULL:
        /* 64 first, then Evaluate Context to what the device says: an 8-byte
         * guess cannot carry the 8-byte read's own answer on every device,
         * and 64 is always enough for the first 8 bytes. */
        return 64UL;
    case XHCI_ENUM_SPEED_HIGH:
        return 64UL;
    case XHCI_ENUM_SPEED_SUPER:
        return 512UL;
    default:
        return 0UL;
    }
}

VOID XhciEnumReset(PXHCI_ENUM_PORT port)
{
    port->State = XHCI_ENUM_EMPTY;
    port->Speed = 0;
    port->SlotId = 0;
    port->Mps0 = 0;
    port->ConfigLength = 0;
    port->Retries = 0;
    port->FailCause = XHCI_ENUM_FAIL_NONE;
    port->PdoExists = 0;
    port->BosLength = 0;
    port->BosMissing = 0;
}

/* bMaxPacketSize0 as a size: at SuperSpeed the field is an exponent, and 9
 * (512) is the one value USB 3.2 9.6.1 allows; 0 for anything else. */
static ULONG xhciEnumMps0Of(ULONG speed, ULONG value)
{
    if (speed == XHCI_ENUM_SPEED_SUPER) {
        return value == 9UL ? 512UL : 0UL;
    }
    return value;
}

static ULONG xhciEnumMps0Valid(ULONG speed, ULONG mps)
{
    if (speed == XHCI_ENUM_SPEED_SUPER) {
        return mps == 512UL;
    }
    if (speed == XHCI_ENUM_SPEED_LOW) {
        return mps == 8UL;
    }
    if (speed == XHCI_ENUM_SPEED_HIGH) {
        return mps == 64UL;
    }
    return mps == 8UL || mps == 16UL || mps == 32UL || mps == 64UL;
}

static VOID xhciEnumAct(PXHCI_ENUM_ACTION action, ULONG kind, ULONG length,
                        ULONG mps0)
{
    action->Kind = kind;
    action->Length = length;
    action->Mps0 = mps0;
}

/* A failed step: Failed, with the slot the step owned given back. */
static ULONG xhciEnumFail(PXHCI_ENUM_PORT port, ULONG cause,
                          PXHCI_ENUM_ACTION action)
{
    port->FailCause = cause;
    port->State = XHCI_ENUM_FAILED;
    if (port->SlotId != 0) {
        port->SlotId = 0;
        xhciEnumAct(action, XHCI_ENUM_ACT_DISABLE_SLOT, 0, 0);
    }
    return port->State;
}

/* A disconnect: everything the port owns goes, in one action. */
static ULONG xhciEnumGone(PXHCI_ENUM_PORT port, PXHCI_ENUM_ACTION action)
{
    if (port->PdoExists) {
        port->SlotId = 0;
        port->State = XHCI_ENUM_GONE;
        xhciEnumAct(action, XHCI_ENUM_ACT_REPORT_GONE, 0, 0);
        return port->State;
    }
    if (port->SlotId != 0) {
        port->SlotId = 0;
        xhciEnumAct(action, XHCI_ENUM_ACT_DISABLE_SLOT, 0, 0);
    }
    port->State = XHCI_ENUM_EMPTY;
    port->Retries = 0;
    port->Speed = 0;
    port->Mps0 = 0;
    return port->State;
}

ULONG XhciEnumStep(PXHCI_ENUM_PORT port, const XHCI_ENUM_EVENT *event,
                   PXHCI_ENUM_ACTION action)
{
    ULONG ok;

    xhciEnumAct(action, XHCI_ENUM_ACT_NONE, 0, 0);
    if (port == NULL || event == NULL) {
        return XHCI_ENUM_EMPTY;
    }
    ok = event->Ok;

    if (event->Kind == XHCI_ENUM_EV_DISCONNECT) {
        if (port->State == XHCI_ENUM_EMPTY || port->State == XHCI_ENUM_GONE) {
            return port->State;
        }
        return xhciEnumGone(port, action);
    }

    switch (port->State) {
    case XHCI_ENUM_EMPTY:
    case XHCI_ENUM_FAILED:
        if (event->Kind == XHCI_ENUM_EV_CONNECT) {
            port->Retries = 0;
            port->FailCause = XHCI_ENUM_FAIL_NONE;
            port->BosLength = 0;
            port->BosMissing = 0;
            port->State = XHCI_ENUM_DEBOUNCE;
            xhciEnumAct(action, XHCI_ENUM_ACT_DEBOUNCE, 0, 0);
        } else if (event->Kind == XHCI_ENUM_EV_RETRY_INTERNAL &&
                   port->State == XHCI_ENUM_FAILED &&
                   port->Retries < event->Value) {
            port->Retries++;
            port->BosLength = 0;
            port->BosMissing = 0;
            port->State = XHCI_ENUM_RESET;
            xhciEnumAct(action, XHCI_ENUM_ACT_RESET, 0, 0);
        }
        break;

    case XHCI_ENUM_DEBOUNCE:
        if (event->Kind == XHCI_ENUM_EV_DEBOUNCED) {
            if (ok) {
                port->State = XHCI_ENUM_RESET;
                xhciEnumAct(action, XHCI_ENUM_ACT_RESET, 0, 0);
            } else {
                port->State = XHCI_ENUM_EMPTY;
            }
        }
        break;

    case XHCI_ENUM_RESET:
        if (event->Kind == XHCI_ENUM_EV_RESET_DONE) {
            if (!ok) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_RESET, action);
            }
            if (XhciEnumInitialMps0(event->Speed) == 0) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_SPEED, action);
            }
            port->Speed = event->Speed;
            port->Mps0 = XhciEnumInitialMps0(event->Speed);
            port->State = XHCI_ENUM_ENABLE_SLOT;
            xhciEnumAct(action, XHCI_ENUM_ACT_ENABLE_SLOT, 0, 0);
        }
        break;

    case XHCI_ENUM_ENABLE_SLOT:
        if (event->Kind == XHCI_ENUM_EV_COMMAND_DONE) {
            if (!ok || event->SlotId == 0) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_NO_SLOT, action);
            }
            port->SlotId = event->SlotId;
            port->State = XHCI_ENUM_ADDRESS;
            xhciEnumAct(action, XHCI_ENUM_ACT_ADDRESS, 0, port->Mps0);
        }
        break;

    case XHCI_ENUM_ADDRESS:
        if (event->Kind == XHCI_ENUM_EV_COMMAND_DONE) {
            if (!ok) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_ADDRESS, action);
            }
            port->State = XHCI_ENUM_DESC8;
            xhciEnumAct(action, XHCI_ENUM_ACT_GET_DEVICE, 8UL, 0);
        }
        break;

    case XHCI_ENUM_DESC8:
        if (event->Kind == XHCI_ENUM_EV_TRANSFER_DONE) {
            if (!ok || event->Bytes < 8UL ||
                !xhciEnumMps0Valid(port->Speed,
                                   xhciEnumMps0Of(port->Speed,
                                                  event->Value))) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_DESCRIPTOR, action);
            }
            if (xhciEnumMps0Of(port->Speed, event->Value) != port->Mps0) {
                port->Mps0 = xhciEnumMps0Of(port->Speed, event->Value);
                port->State = XHCI_ENUM_EVALUATE;
                xhciEnumAct(action, XHCI_ENUM_ACT_EVALUATE, 0, port->Mps0);
            } else {
                port->State = XHCI_ENUM_DESC18;
                xhciEnumAct(action, XHCI_ENUM_ACT_GET_DEVICE,
                            XHCI_ENUM_DEVICE_DESC_BYTES, 0);
            }
        }
        break;

    case XHCI_ENUM_EVALUATE:
        if (event->Kind == XHCI_ENUM_EV_COMMAND_DONE) {
            if (!ok) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_ADDRESS, action);
            }
            port->State = XHCI_ENUM_DESC18;
            xhciEnumAct(action, XHCI_ENUM_ACT_GET_DEVICE,
                        XHCI_ENUM_DEVICE_DESC_BYTES, 0);
        }
        break;

    case XHCI_ENUM_DESC18:
        if (event->Kind == XHCI_ENUM_EV_TRANSFER_DONE) {
            if (!ok || event->Bytes != XHCI_ENUM_DEVICE_DESC_BYTES) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_DESCRIPTOR, action);
            }
            if (port->Speed == XHCI_ENUM_SPEED_SUPER) {
                port->State = XHCI_ENUM_BOS5;
                xhciEnumAct(action, XHCI_ENUM_ACT_GET_BOS,
                            XHCI_ENUM_BOS_HEAD_BYTES, 0);
                break;
            }
            port->State = XHCI_ENUM_CONFIG9;
            xhciEnumAct(action, XHCI_ENUM_ACT_GET_CONFIG,
                        XHCI_ENUM_CONFIG_HEAD_BYTES, 0);
        }
        break;

    case XHCI_ENUM_BOS5:
        if (event->Kind == XHCI_ENUM_EV_TRANSFER_DONE) {
            if (ok && event->Bytes >= XHCI_ENUM_BOS_HEAD_BYTES &&
                event->Value >= XHCI_ENUM_BOS_HEAD_BYTES) {
                port->BosLength = event->Value;
                port->State = XHCI_ENUM_BOS_FULL;
                xhciEnumAct(action, XHCI_ENUM_ACT_GET_BOS, port->BosLength,
                            0);
                break;
            }
            port->BosMissing = 1;
            port->State = XHCI_ENUM_CONFIG9;
            xhciEnumAct(action, XHCI_ENUM_ACT_GET_CONFIG,
                        XHCI_ENUM_CONFIG_HEAD_BYTES, 0);
        }
        break;

    case XHCI_ENUM_BOS_FULL:
        if (event->Kind == XHCI_ENUM_EV_TRANSFER_DONE) {
            if (!ok || event->Bytes != port->BosLength) {
                port->BosMissing = 1;
            }
            port->State = XHCI_ENUM_CONFIG9;
            xhciEnumAct(action, XHCI_ENUM_ACT_GET_CONFIG,
                        XHCI_ENUM_CONFIG_HEAD_BYTES, 0);
        }
        break;

    case XHCI_ENUM_CONFIG9:
        if (event->Kind == XHCI_ENUM_EV_TRANSFER_DONE) {
            if (!ok || event->Bytes < XHCI_ENUM_CONFIG_HEAD_BYTES ||
                event->Value < XHCI_ENUM_CONFIG_HEAD_BYTES) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_CONFIG, action);
            }
            port->ConfigLength = event->Value;
            port->State = XHCI_ENUM_CONFIG_FULL;
            xhciEnumAct(action, XHCI_ENUM_ACT_GET_CONFIG, port->ConfigLength,
                        0);
        }
        break;

    case XHCI_ENUM_CONFIG_FULL:
        if (event->Kind == XHCI_ENUM_EV_TRANSFER_DONE) {
            if (!ok || event->Bytes != port->ConfigLength) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_CONFIG, action);
            }
            port->State = XHCI_ENUM_PRESENT;
            xhciEnumAct(action, XHCI_ENUM_ACT_CREATE_PDO, 0, 0);
        }
        break;

    case XHCI_ENUM_PRESENT:
        if (event->Kind == XHCI_ENUM_EV_PDO_CREATED) {
            if (!ok) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_PDO, action);
            }
            port->PdoExists = 1;
        } else if (event->Kind == XHCI_ENUM_EV_PDO_STARTED &&
                   port->PdoExists) {
            port->State = XHCI_ENUM_BOUND;
        }
        break;

    case XHCI_ENUM_GONE:
        if (event->Kind == XHCI_ENUM_EV_PDO_REMOVED) {
            port->PdoExists = 0;
            port->State = XHCI_ENUM_EMPTY;
            port->Retries = 0;
            port->Speed = 0;
            port->Mps0 = 0;
            port->BosLength = 0;
            port->BosMissing = 0;
        }
        break;

    default:
        break;
    }
    return port->State;
}

/* The retry after a failure, as an event of its own so the caller decides
 * when - after the slot has been given back and the port re-read. The
 * event carries the retry limit in Value. */
ULONG XhciEnumRetryUpTo(PXHCI_ENUM_PORT port, ULONG retries,
                        PXHCI_ENUM_ACTION action)
{
    XHCI_ENUM_EVENT event;

    event.Kind = XHCI_ENUM_EV_RETRY_INTERNAL;
    event.Ok = 1;
    event.Speed = 0;
    event.SlotId = 0;
    event.Bytes = 0;
    event.Value = retries;
    return XhciEnumStep(port, &event, action);
}

ULONG XhciEnumRetry(PXHCI_ENUM_PORT port, PXHCI_ENUM_ACTION action)
{
    return XhciEnumRetryUpTo(port, XHCI_ENUM_RETRIES, action);
}

/* The first answer's settle (task 33.3; xhci_enum.h). */
ULONG XhciEnumAtRest(ULONG state)
{
    switch (state) {
    case XHCI_ENUM_EMPTY:
    case XHCI_ENUM_PRESENT:
    case XHCI_ENUM_BOUND:
    case XHCI_ENUM_GONE:
    case XHCI_ENUM_FAILED:
        return 1;
    default:
        return 0;
    }
}

ULONG XhciEnumSettleCap(ULONG found, ULONG value)
{
    if (!found) {
        return XHCI_ENUM_SETTLE_DEFAULT_MS;
    }
    return (value > XHCI_ENUM_SETTLE_MAX_MS) ? XHCI_ENUM_SETTLE_MAX_MS : value;
}

ULONG XhciEnumSettlePortCap(ULONG found, ULONG value, ULONG total)
{
    if (!found) {
        value = XHCI_ENUM_SETTLE_PORT_MS;
    }
    return (value > total) ? total : value;
}

ULONG XhciEnumElapsedMs(ULONG startLow, ULONG nowLow)
{
    return (nowLow - startLow) / 10000UL;
}

ULONG XhciEnumHoldInFlight(ULONG pending, ULONG kind, ULONG companion,
                           ULONG connectSeen)
{
    if (pending) {
        return 1;
    }
    return (kind != 0 && companion != 0 && !connectSeen) ? 1UL : 0UL;
}

ULONG XhciEnumSettleQuiet(ULONG enumerated, ULONG rootPending,
                          ULONG hubPending, ULONG inFlight)
{
    return (enumerated && !rootPending && !hubPending && !inFlight) ? 1UL
                                                                     : 0UL;
}

ULONG XhciEnumSettleReached(ULONG done, ULONG target)
{
    return ((done - target) < 0x80000000UL) ? 1UL : 0UL;
}

ULONG XhciEnumLetGo(ULONG listed, ULONG reported, ULONG removeReceived)
{
    return (listed && reported && removeReceived) ? 1UL : 0UL;
}

ULONG XhciEnumAnswerCarries(ULONG parentSerial, ULONG answering,
                            ULONG letGo)
{
    return (parentSerial == answering && !letGo) ? 1UL : 0UL;
}

/* ------------------------------------------------------------------ */
/* A root port's enumeration notes (task 35.3; xhci_enum.h)            */
/* ------------------------------------------------------------------ */

VOID XhciEnumNotesInit(PXHCI_ENUM_NOTES notes)
{
    notes->Used = 0;
    notes->Open = 0;
    notes->Suppressed = 0;
    notes->FailedLooked = 0;
}

ULONG XhciEnumNoteCharge(PXHCI_ENUM_NOTES notes)
{
    if (notes->Used < XHCI_ENUM_NOTE_BUDGET) {
        notes->Used++;
        return XHCI_ENUM_NOTE_YES;
    }
    notes->Suppressed++;
    if (notes->Used == XHCI_ENUM_NOTE_BUDGET) {
        notes->Used++;
        return XHCI_ENUM_NOTE_QUIET;
    }
    return XHCI_ENUM_NOTE_NO;
}

VOID XhciEnumNoteRefill(PXHCI_ENUM_NOTES notes)
{
    notes->Used = 0;
}

ULONG XhciEnumNoteWantLook(PXHCI_ENUM_NOTES notes, ULONG state, ULONG feed,
                           ULONG linkActed)
{
    if (feed != 0 || linkActed) {
        notes->FailedLooked = 0;
        return 1;
    }
    if (state == XHCI_ENUM_FAILED && !notes->FailedLooked) {
        notes->FailedLooked = 1;
        return 1;
    }
    return 0;
}

ULONG XhciEnumNoteBegin(PXHCI_ENUM_NOTES notes)
{
    ULONG charged;

    if (notes->Open != 0) {
        return XHCI_ENUM_NOTE_NO;
    }
    charged = XhciEnumNoteCharge(notes);
    notes->Open = (charged == XHCI_ENUM_NOTE_YES) ? 1UL : 2UL;
    return charged;
}

ULONG XhciEnumNoteOn(const XHCI_ENUM_NOTES *notes)
{
    return (notes->Open == 1) ? 1UL : 0UL;
}

VOID XhciEnumNoteFinish(PXHCI_ENUM_NOTES notes)
{
    notes->Open = 0;
}

ULONG XhciEnumNoteLook(ULONG port, ULONG state, ULONG linkAction,
                       ULONG feed, ULONG portsc)
{
    return ((port & 0xFFUL) << 24) | ((state & 0xFUL) << 20) |
           ((linkAction & 0xFUL) << 16) | ((feed & 0x3UL) << 14) |
           (((portsc & XHCI_PORTSC_CHANGE_MASK) >> 17) << 7) |
           (XHCI_PORTSC_GET_PLS(portsc) << 3) |
           ((portsc & XHCI_PORTSC_PR) != 0 ? 0x4UL : 0UL) |
           (portsc & (XHCI_PORTSC_PED | XHCI_PORTSC_CCS));
}

ULONG XhciEnumNoteReset(ULONG port, ULONG ok, ULONG attempt, ULONG portsc)
{
    return ((port & 0xFFUL) << 24) | (ok ? 0x00800000UL : 0UL) |
           ((attempt & 0x7FUL) << 16) | (portsc & 0xFFFFUL);
}

ULONG XhciEnumNoteSpeed(ULONG port, ULONG psiv, ULONG speedClass,
                        ULONG source)
{
    return ((port & 0xFFUL) << 24) | ((psiv & 0xFFUL) << 16) |
           ((speedClass & 0xFFUL) << 8) | (source & 0xFFUL);
}

ULONG XhciEnumNoteRate(ULONG port, ULONG kbps, ULONG plus)
{
    ULONG units;

    /* 100 kbit/s units, so Low Speed's 1.5 Mbit/s is not read as 1 and 20
     * Gbit/s (200000) still fits; a rate past the field reads its top. */
    units = kbps / 100UL;
    if (units > 0x007FFFFFUL) {
        units = 0x007FFFFFUL;
    }
    return ((port & 0xFFUL) << 24) | (plus ? 0x00800000UL : 0UL) | units;
}

ULONG XhciEnumNoteSlot(ULONG port, ULONG code, ULONG attempt, ULONG slotId)
{
    return ((port & 0xFFUL) << 24) | ((code & 0xFFUL) << 16) |
           ((attempt & 0xFFUL) << 8) | (slotId & 0xFFUL);
}

ULONG XhciEnumNoteFail(ULONG port, ULONG cause, ULONG attempt, ULONG final)
{
    return ((port & 0xFFUL) << 24) | ((cause & 0xFFUL) << 16) |
           ((attempt & 0xFFUL) << 8) | (final ? 1UL : 0UL);
}

ULONG XhciEnumNoteEnd(ULONG port, ULONG state, ULONG cause, ULONG retries)
{
    return ((port & 0xFFUL) << 24) | ((state & 0xFFUL) << 16) |
           ((cause & 0xFFUL) << 8) | (retries & 0xFFUL);
}

ULONG XhciEnumNoteQuiet(ULONG port)
{
    return ((port & 0xFFUL) << 24) | (XHCI_ENUM_NOTE_BUDGET & 0xFFUL);
}
