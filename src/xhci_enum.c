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
 * C89, pure: IRQL any.
 */

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
}

static ULONG xhciEnumMps0Valid(ULONG speed, ULONG mps)
{
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
            port->State = XHCI_ENUM_DEBOUNCE;
            xhciEnumAct(action, XHCI_ENUM_ACT_DEBOUNCE, 0, 0);
        } else if (event->Kind == XHCI_ENUM_EV_RETRY_INTERNAL &&
                   port->State == XHCI_ENUM_FAILED &&
                   port->Retries < event->Value) {
            port->Retries++;
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
                !xhciEnumMps0Valid(port->Speed, event->Value)) {
                return xhciEnumFail(port, XHCI_ENUM_FAIL_DESCRIPTOR, action);
            }
            if (event->Value != port->Mps0) {
                port->Mps0 = event->Value;
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
