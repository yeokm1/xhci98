/*
 * xhci_inj.c - 35-T.9's fault injection, the pure half (xhci_inj.h).
 *
 * The qemu flavour alone compiles it into the driver; the debug and release
 * images carry none of it. C89, pure: IRQL any.
 */

#include "xhci_inj.h"
#include "xhci_pipe.h"

#if defined(XHCI_FLAVOUR_QEMU) || defined(XHCI_HOST_TEST)

VOID XhciInjTriggerStart(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value)
{
    t->Seen = found ? 1UL : 0UL;
    t->Seq = found ? XHCI_INJ_GET_SEQ(value) : 0UL;
}

ULONG XhciInjTake(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value)
{
    ULONG seq;
    ULONG fault;

    if (!found) {
        return XHCI_INJ_TAKE_NONE;
    }
    seq = XHCI_INJ_GET_SEQ(value);
    if (t->Seen && t->Seq == seq) {
        return XHCI_INJ_TAKE_NONE;
    }
    t->Seen = 1;
    t->Seq = seq;
    fault = XHCI_INJ_GET_FAULT(value);
    if ((fault >= XHCI_INJ_LOST_IRQ && fault <= XHCI_INJ_LAST_BUILT) ||
        fault == XHCI_INJ_CLEAR) {
        return XHCI_INJ_TAKE_FIRE;
    }
    return XHCI_INJ_TAKE_UNKNOWN;
}

VOID XhciInjRegsClear(PXHCI_INJ_REGS r)
{
    r->PedPort = 0;
    r->PecHeld = 0;
    r->PedClear = 0;
    r->OcPort = 0;
    r->OcActive = 0;
    r->OcaHeld = 0;
    r->OccHeld = 0;
    r->OcPpWrites = 0;
    r->DeadLeft = 0;
    r->DeadActive = 0;
    r->NoProof = 0;
}

ULONG XhciInjPortFits(const XHCI_PORT_MAP *map, ULONG port, ULONG wanted,
                      ULONG portsc)
{
    ULONG need;

    if (wanted != 0 && port != wanted) {
        return 0;
    }
    if (!XhciPortIsManaged(map, port) || XhciPortIsUsb3(map, port)) {
        return 0;
    }
    need = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_PP;
    if (portsc == 0xFFFFFFFFUL || (portsc & need) != need) {
        return 0;
    }
    return 1;
}

VOID XhciInjArmPec(PXHCI_INJ_REGS r, ULONG port)
{
    r->PedPort = port;
    r->PecHeld = 1;
    r->PedClear = 1;
}

VOID XhciInjArmOc(PXHCI_INJ_REGS r, ULONG port)
{
    r->OcPort = port;
    r->OcActive = 1;
    r->OcaHeld = 1;
    r->OccHeld = 1;
    r->OcPpWrites = 0;
}

VOID XhciInjReleaseOc(PXHCI_INJ_REGS r)
{
    r->OcaHeld = 0;
}

VOID XhciInjArmDead(PXHCI_INJ_REGS r, ULONG reads, ULONG noProof)
{
    r->DeadLeft = (reads == 0) ? XHCI_INJ_DEAD_FOREVER : reads;
    r->DeadActive = 0;
    r->NoProof = noProof ? 1UL : 0UL;
}

/* The PED emulation's end, once neither half is answered. */
static VOID xhciInjPedEnd(PXHCI_INJ_REGS r)
{
    if (!r->PecHeld && !r->PedClear) {
        r->PedPort = 0;
    }
}

ULONG XhciInjPortscRead(PXHCI_INJ_REGS r, ULONG port, ULONG raw)
{
    ULONG value;

    value = raw;
    if (raw == 0xFFFFFFFFUL || port == 0) {
        return raw;
    }
    if (r->PedClear && port == r->PedPort &&
        (raw & XHCI_PORTSC_CCS) == 0) {
        /* The device has gone: the real port is disabled now too. */
        r->PedClear = 0;
        xhciInjPedEnd(r);
    }
    if (r->PecHeld && port == r->PedPort) {
        value |= XHCI_PORTSC_PEC;
    }
    if (r->PedClear && port == r->PedPort) {
        /* QEMU ignores a PED write (hcd-xhci.c, xhci_port_write), so the
         * real port stays enabled; the driver must read it disabled. */
        value &= ~XHCI_PORTSC_PED;
    }
    if (r->OcActive && port == r->OcPort) {
        value &= ~XHCI_PORTSC_PP;
        if (r->OcaHeld) {
            value |= XHCI_PORTSC_OCA;
        }
        if (r->OccHeld) {
            value |= XHCI_PORTSC_OCC;
        }
    }
    return value;
}

ULONG XhciInjPortscWrite(PXHCI_INJ_REGS r, ULONG port, ULONG value)
{
    if (port == 0) {
        return value;
    }
    if (r->PecHeld && port == r->PedPort &&
        (value & XHCI_PORTSC_PEC) != 0) {
        r->PecHeld = 0;
        xhciInjPedEnd(r);
    }
    if (r->PedClear && port == r->PedPort &&
        (value & XHCI_PORTSC_PR) != 0) {
        /* The driver's port reset re-enables the real port. */
        r->PedClear = 0;
        xhciInjPedEnd(r);
    }
    if (r->OcActive && port == r->OcPort) {
        if ((value & XHCI_PORTSC_OCC) != 0) {
            r->OccHeld = 0;
        }
        if ((value & XHCI_PORTSC_PP) != 0) {
            r->OcPpWrites++;
            /* After the release the driver's repower ends the emulation:
             * the real port, which never lost power, is read from here. */
            if (!r->OcaHeld) {
                r->OcActive = 0;
                r->OccHeld = 0;
                r->OcPort = 0;
            }
        }
        /* The emulated power-off never reaches the real port: QEMU stores
         * PP as written, and a port it leaves unpowered is not the one the
         * emulation ends on. */
        value |= XHCI_PORTSC_PP;
    }
    return value;
}

ULONG XhciInjDeadPass(PXHCI_INJ_REGS r)
{
    if (r->DeadLeft == 0) {
        r->DeadActive = 0;
        return 0;
    }
    if (r->DeadLeft != XHCI_INJ_DEAD_FOREVER) {
        r->DeadLeft--;
    }
    r->DeadActive = 1;
    return 1;
}

ULONG XhciInjUsbsts(const XHCI_INJ_REGS *r, ULONG raw)
{
    return r->DeadActive ? 0xFFFFFFFFUL : raw;
}

ULONG XhciInjIrqDrop(ULONG armed, ULONG forever, ULONG taken, ULONG budget)
{
    if (!armed) {
        return 0;
    }
    if (forever) {
        return 1;
    }
    /* Unsigned: a count past 0x7FFFFFFF is past any budget, never under. */
    return (taken != 0 && taken <= budget) ? 1UL : 0UL;
}

ULONG XhciInjIrqSpent(ULONG armed, ULONG forever, ULONG taken, ULONG budget)
{
    return (armed && !forever && taken >= budget) ? 1UL : 0UL;
}

ULONG XhciInjPciCommand(const XHCI_INJ_REGS *r, ULONG command)
{
    if (r->NoProof && command != 0xFFFFUL) {
        return command | XHCI_INJ_PCI_BME;
    }
    return command;
}

ULONG XhciInjNeedsLive(ULONG fault)
{
    return (fault >= XHCI_INJ_LOST_IRQ && fault <= XHCI_INJ_LAST_BUILT &&
            fault != XHCI_INJ_OC_RELEASE) ? 1UL : 0UL;
}

ULONG XhciInjLostCount(ULONG arg)
{
    if (arg == 0) {
        return 1;
    }
    if (arg == XHCI_INJ_ARG_FOREVER) {
        return 0xFFFFFFFFUL;
    }
    return arg;
}

/* ----------------------------------------------------------------------- */
/* The endpoint faults                                                      */
/* ----------------------------------------------------------------------- */

ULONG XhciInjKind(ULONG fault)
{
    switch (fault) {
    case XHCI_INJ_TRANSACTION:
    case XHCI_INJ_RESET_EP_FAIL:
    case XHCI_INJ_RACE_RING:
    case XHCI_INJ_RACE_SECOND:
        return XHCI_INJ_KIND_RETRY;
    case XHCI_INJ_HALT_HALTED:
    case XHCI_INJ_HALT_ERROR:
    case XHCI_INJ_HALT_STALE:
        return XHCI_INJ_KIND_HALT;
    case XHCI_INJ_REFUSED_CODE:
    case XHCI_INJ_EP_NOT_ENABLED:
        return XHCI_INJ_KIND_REFUSED;
    case XHCI_INJ_EP0_THREAD:
    case XHCI_INJ_EP0_PREPDO:
        return XHCI_INJ_KIND_EP0;
    default:
        return XHCI_INJ_KIND_OTHER;
    }
}

ULONG XhciInjHoldOf(ULONG fault)
{
    switch (fault) {
    case XHCI_INJ_RESET_EP_FAIL:
        return XHCI_INJ_HOLD_FAIL;
    case XHCI_INJ_RACE_RING:
        return XHCI_INJ_HOLD_RING;
    case XHCI_INJ_RACE_SECOND:
        return XHCI_INJ_HOLD_SECOND;
    default:
        return XHCI_INJ_HOLD_NONE;
    }
}

ULONG XhciInjHaltStateOf(ULONG fault)
{
    if (XhciInjKind(fault) == XHCI_INJ_KIND_RETRY ||
        fault == XHCI_INJ_HALT_HALTED) {
        return XHCI_EP_STATE_HALTED;
    }
    if (fault == XHCI_INJ_HALT_ERROR) {
        return XHCI_EP_STATE_ERROR;
    }
    return 0;
}

ULONG XhciInjSpend(PULONG left)
{
    if (*left == 0) {
        return 0;
    }
    if (*left != 0xFFFFFFFFUL) {
        (*left)--;
    }
    return 1;
}

ULONG XhciInjPipeFits(ULONG wanted, ULONG devPort, ULONG published,
                      ULONG isHub, ULONG gone, ULONG transferType,
                      ULONG endpointAddress, ULONG streams, ULONG busy,
                      ULONG queued, ULONG retryWanted)
{
    if (wanted != 0 && devPort != wanted) {
        return 0;
    }
    if (!published || isHub || gone || streams || busy || !queued ||
        retryWanted) {
        return 0;
    }
    /* The record's target: an interrupt IN QEMU NAKs while the HID device
     * is idle, so the TD sits at the dequeue with nothing moved. */
    return (transferType == XHCI_PIPE_XFER_INTERRUPT &&
            (endpointAddress & 0x80UL) != 0) ? 1UL : 0UL;
}

VOID XhciInjTransferEvent(XHCI_TRB *ev, ULONG pa, ULONG residual, ULONG cc,
                          ULONG slot, ULONG dci)
{
    ev->Param0 = pa;
    ev->Param1 = 0;
    ev->Status = ((cc & 0xFFUL) << 24) | (residual & 0x00FFFFFFUL);
    ev->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_TRANSFER_EVENT) |
                  XHCI_TRB_EP_ID(dci) | XHCI_TRB_SLOT_ID(slot);
}

VOID XhciInjEpClear(PXHCI_INJ_EP e)
{
    XhciInjEpEnd(e);
    e->Dropped = 0;
    e->StopSeen = 0;
    e->StopPA = 0;
    e->StopCode = 0;
    e->StopResidual = 0;
    e->EvPending = 0;
    e->Ev.Param0 = 0;
    e->Ev.Param1 = 0;
    e->Ev.Status = 0;
    e->Ev.Control = 0;
    e->Ep0Want = 0;
    e->Ep0Port = 0;
    e->Ep0Left = 0;
    e->Abandoned = 0;
    e->Repointed = 0;
}

VOID XhciInjEpEnd(PXHCI_INJ_EP e)
{
    e->Slot = 0;
    e->Dci = 0;
    e->State = 0;
    e->Block = 0;
    e->Hold = XHCI_INJ_HOLD_NONE;
    e->Watch = 0;
}

VOID XhciInjEpWatch(PXHCI_INJ_EP e, ULONG slot, ULONG dci)
{
    e->Slot = slot;
    e->Dci = dci;
    e->State = 0;
    e->Block = 1;
    e->Hold = XHCI_INJ_HOLD_NONE;
    e->Watch = 1;
    e->StopSeen = 0;
    e->StopPA = 0;
    e->StopCode = 0;
    e->StopResidual = 0;
}

ULONG XhciInjEpSwallow(PXHCI_INJ_EP e, const XHCI_TRB *ev)
{
    ULONG cc;

    if (!e->Watch || e->Slot == 0 ||
        XHCI_TRB_GET_TYPE(ev->Control) != XHCI_TRB_TYPE_TRANSFER_EVENT ||
        XHCI_TRB_GET_SLOT_ID(ev->Control) != e->Slot ||
        XHCI_TRB_GET_EP_ID(ev->Control) != e->Dci) {
        return 0;
    }
    cc = XHCI_TRB_GET_COMPLETION(ev->Status);
    if (cc < XHCI_CC_STOPPED || cc > XHCI_CC_STOPPED_SHORT_PACKET) {
        return 0;
    }
    e->StopSeen = 1;
    e->StopPA = XHCI_EVENT_IS_EVENT_DATA(ev->Control) ? 0UL : ev->Param0;
    e->StopCode = cc;
    e->StopResidual = XHCI_TRB_GET_RESIDUAL(ev->Status);
    return 1;
}

ULONG XhciInjEpDoorbell(PXHCI_INJ_EP e, ULONG slot, ULONG value)
{
    if (!e->Block || e->Slot == 0 || slot != e->Slot ||
        (value & 0xFFUL) != e->Dci) {
        return 0;
    }
    e->Dropped++;
    return 1;
}

ULONG XhciInjEpState(const XHCI_INJ_EP *e, ULONG slot, ULONG dci, ULONG raw)
{
    if (e->Slot == 0 || e->State == 0 || slot != e->Slot || dci != e->Dci) {
        return raw;
    }
    return e->State;
}

ULONG XhciInjCommand(PXHCI_INJ_EP e, const XHCI_TRB *cmd, ULONG icDrop,
                     ULONG icAdd, ULONG retrySurvives, PULONG code)
{
    ULONG type;
    ULONG hold;

    type = XHCI_TRB_GET_TYPE(cmd->Control);
    if (e->Slot == 0 || XHCI_TRB_GET_SLOT_ID(cmd->Control) != e->Slot) {
        return XHCI_INJ_CMD_PASS;
    }
    switch (type) {
    case XHCI_TRB_TYPE_DISABLE_SLOT:
    case XHCI_TRB_TYPE_ADDRESS_DEVICE:
    case XHCI_TRB_TYPE_RESET_DEVICE:
        /* Every endpoint context of the slot is rewritten or gone - once
         * the command has done it. */
        return XHCI_INJ_CMD_SEND_END;
    case XHCI_TRB_TYPE_CONFIGURE_EP:
        /* A deconfigure, or a Drop or Add of this endpoint, rewrites its
         * context; one for another function's endpoints leaves it as the
         * controller keeps it, Halted or Error included. */
        if ((cmd->Control & XHCI_TRB_DC) != 0 ||
            ((icDrop | icAdd) & (1UL << e->Dci)) != 0) {
            return XHCI_INJ_CMD_SEND_END;
        }
        return XHCI_INJ_CMD_PASS;
    case XHCI_TRB_TYPE_RESET_EP:
        if (XHCI_TRB_GET_EP_ID(cmd->Control) != e->Dci || e->State == 0) {
            return XHCI_INJ_CMD_PASS;
        }
        if (e->State == XHCI_EP_STATE_ERROR) {
            *code = XHCI_CC_CONTEXT_STATE_ERROR;
            return XHCI_INJ_CMD_ANSWER;
        }
        hold = e->Hold;
        e->Hold = XHCI_INJ_HOLD_NONE;
        if (hold == XHCI_INJ_HOLD_FAIL) {
            *code = XHCI_CC_CONTEXT_STATE_ERROR;
            return XHCI_INJ_CMD_ANSWER;
        }
        /* A race is run only for the soft retry's own reset of the TD it
         * still holds: a reset that yields to an abort, a cancel or a
         * reset - or one at XhciTolerance 0 - may come after that TD was
         * retired, and a ring then would restart the controller on a TD
         * whose buffer has gone back. Such a reset is answered plainly,
         * the hold spent. */
        if (hold == XHCI_INJ_HOLD_RING && retrySurvives) {
            return XHCI_INJ_CMD_RING;
        }
        if (hold == XHCI_INJ_HOLD_SECOND && retrySurvives) {
            return XHCI_INJ_CMD_SECOND;
        }
        /* The real endpoint is already Stopped, the state a Reset
         * Endpoint leaves: nothing needs sending. */
        XhciInjEpEnd(e);
        *code = XHCI_CC_SUCCESS;
        return XHCI_INJ_CMD_ANSWER;
    case XHCI_TRB_TYPE_SET_TR_DEQUEUE:
        if (XHCI_TRB_GET_EP_ID(cmd->Control) != e->Dci || e->State == 0) {
            return XHCI_INJ_CMD_PASS;
        }
        if (e->State == XHCI_EP_STATE_HALTED) {
            *code = XHCI_CC_CONTEXT_STATE_ERROR;
            return XHCI_INJ_CMD_ANSWER;
        }
        return XHCI_INJ_CMD_SEND_END;
    default:
        return XHCI_INJ_CMD_PASS;
    }
}

VOID XhciInjCommandSent(PXHCI_INJ_EP e, ULONG slot, ULONG code)
{
    if (e->Slot != 0 && slot == e->Slot && code == XHCI_CC_SUCCESS) {
        XhciInjEpEnd(e);
    }
}

ULONG XhciInjStopVerdict(ULONG ctxDeqPA, ULONG firstPA, ULONG afterPA,
                         ULONG single, ULONG firstLen, ULONG seen,
                         ULONG seenPA, ULONG seenCode, ULONG seenResidual)
{
    ULONG untouched;

    /* A Stopped event names the TRB the TD stopped in; code 26's length is
     * that TRB's residual, the other two say nothing exact. */
    untouched = (seen && seenPA == firstPA && seenCode == XHCI_CC_STOPPED &&
                 seenResidual == firstLen) ? 1UL : 0UL;
    if ((ctxDeqPA & XHCI_EP_DEQUEUE_MASK) == firstPA) {
        return (!seen || untouched) ? XHCI_INJ_STOP_OK : XHCI_INJ_STOP_BROKEN;
    }
    if (single && untouched && (ctxDeqPA & XHCI_EP_DEQUEUE_MASK) == afterPA) {
        return XHCI_INJ_STOP_REPOINT;
    }
    return XHCI_INJ_STOP_BROKEN;
}

VOID XhciInjEp0Arm(PXHCI_INJ_EP e, ULONG fault, ULONG port, ULONG left)
{
    e->Ep0Want = (left != 0) ? fault : 0UL;
    e->Ep0Port = port;
    e->Ep0Left = left;
}

ULONG XhciInjEp0Fires(PXHCI_INJ_EP e, ULONG devPort, ULONG published)
{
    if (e->Ep0Want == 0 || e->EvPending) {
        return 0;
    }
    if (e->Ep0Port != 0 && devPort != e->Ep0Port) {
        return 0;
    }
    if ((e->Ep0Want == XHCI_INJ_EP0_THREAD) != (published != 0)) {
        return 0;
    }
    if (!XhciInjSpend(&e->Ep0Left)) {
        e->Ep0Want = 0;
        return 0;
    }
    if (e->Ep0Left == 0) {
        e->Ep0Want = 0;
    }
    return 1;
}

#else /* debug and release: injection is not in the image */

typedef int xhciInjNotInImage;

#endif
