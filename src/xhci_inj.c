/*
 * xhci_inj.c - 35-T.9's fault injection, the pure half (xhci_inj.h).
 *
 * The qemu flavour alone compiles it into the driver; the release image
 * carries none of it. C89, pure: IRQL any.
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
        fault == XHCI_INJ_QUERY_STOP ||
        (fault >= XHCI_INJ_RETRY_FIRST && fault <= XHCI_INJ_RETRY_LAST) ||
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
    return ((fault >= XHCI_INJ_LOST_IRQ && fault <= XHCI_INJ_LAST_BUILT &&
             fault != XHCI_INJ_OC_RELEASE) ||
            fault == XHCI_INJ_QUERY_STOP) ? 1UL : 0UL;
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
    case XHCI_INJ_BULK_TIMEOUT:
    case XHCI_INJ_BULK_STOP:
    case XHCI_INJ_BULK_ERROR:
    case XHCI_INJ_BULK_SHORT:
        return XHCI_INJ_KIND_BULK;
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
        fault == XHCI_INJ_HALT_HALTED || fault == XHCI_INJ_BULK_ERROR) {
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

ULONG XhciInjResetHoldMs(ULONG arg)
{
    if (arg == XHCI_INJ_ARG_FOREVER) {
        return 0xFFFFFFFFUL;
    }
    return arg * 10UL;
}

ULONG XhciInjResetHeld(ULONG holdMs, ULONG elapsedMs)
{
    if (holdMs == 0xFFFFFFFFUL) {
        return 1;
    }
    return (elapsedMs < holdMs) ? 1UL : 0UL;
}

ULONG XhciInjResetAnswer(ULONG opOffset, ULONG value, ULONG held, ULONG cnr)
{
    if (!held || value == 0xFFFFFFFFUL) {
        return value;
    }
    if (!cnr && opOffset == XHCI_OP_USBCMD) {
        return value | XHCI_USBCMD_HCRST;
    }
    if (cnr && opOffset == XHCI_OP_USBSTS) {
        return value | XHCI_USBSTS_CNR;
    }
    return value;
}

VOID XhciInjHostControllerEvent(XHCI_TRB *ev, ULONG cc)
{
    ev->Param0 = 0;
    ev->Param1 = 0;
    ev->Status = (cc & 0xFFUL) << 24;
    ev->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_HOST_CONTROLLER);
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
    e->Bulk = 0;
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
        if (e->Bulk && e->State == 0 &&
            XHCI_TRB_GET_EP_ID(cmd->Control) == e->Dci) {
            /* 36.22: see STOP_EP below. */
            return XHCI_INJ_CMD_SEND_END;
        }
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
    case XHCI_TRB_TYPE_STOP_EP:
        /* 36.22: the driver's own stop, reset or dequeue move of an
         * endpoint whose doorbell a bulk fault withholds - an abort or a
         * cancel taking its TD back, the stop skipped on an endpoint
         * already Stopped - ends the withholding once it succeeds; each is
         * sent as it is. */
        if (e->Bulk && e->State == 0 &&
            XHCI_TRB_GET_EP_ID(cmd->Control) == e->Dci) {
            return XHCI_INJ_CMD_SEND_END;
        }
        return XHCI_INJ_CMD_PASS;
    case XHCI_TRB_TYPE_SET_TR_DEQUEUE:
        if (e->Bulk && e->State == 0 &&
            XHCI_TRB_GET_EP_ID(cmd->Control) == e->Dci) {
            return XHCI_INJ_CMD_SEND_END;
        }
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

ULONG XhciInjAfterPA(ULONG basePA, ULONG lastIndex)
{
    return basePA + (lastIndex + 1UL) * XHCI_TRB_BYTES;
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

/* ----------------------------------------------------------------------- */
/* Task 50.30 step 2: a device or a command that stops answering            */
/* ----------------------------------------------------------------------- */

VOID XhciInjHangEnd(PXHCI_INJ_HANG h)
{
    h->Fault = 0;
    h->Port = 0;
    h->Behind = 0;
    h->Outcome = 0;
    h->Left = 0;
    h->ByBehaviour = 0;
    h->Armed = 0;
    h->HoldNext = 0;
    h->HoldType = 0;
    h->HoldSlot = 0;
    h->HeldPA = 0;
    h->NoOpPA = 0;
    h->StopOwedPA = 0;
    h->Recovery = 0;
    h->RecSlot = 0;
    h->HoldRec = 0;
    h->MarkSlot = 0;
    h->MarkTick = 0;
}

VOID XhciInjHangRingGone(PXHCI_INJ_HANG h)
{
    h->HoldNext = 0;
    h->HoldType = 0;
    h->HoldSlot = 0;
    h->HeldPA = 0;
    h->NoOpPA = 0;
    h->StopOwedPA = 0;
    /* Step 3: the latched slot is given back with the ring (an HCRST, a
     * stop, a rebuild), so a later device given the same slot number
     * never inherits the latch or the mark deadline; bit 7 stays armed for
     * the next primary. */
    h->RecSlot = 0;
    h->HoldRec = 0;
    h->MarkSlot = 0;
    h->MarkTick = 0;
}

VOID XhciInjHangArm(PXHCI_INJ_HANG h, ULONG fault, ULONG port, ULONG arg)
{
    ULONG withheld;

    withheld = h->Withheld;
    XhciInjHangEnd(h);
    h->Withheld = withheld;
    if (fault < XHCI_INJ_ADDRESS_HANG || fault > XHCI_INJ_RESETDEV_HANG) {
        return;
    }
    if (fault == XHCI_INJ_DEVICE_HANG && (port & XHCI_INJ_HG_BEHIND) != 0) {
        /* Its end is the root port's disconnect, which a device behind a
         * hub never shows: it would outlive the device and hang the hub's
         * other devices. A root port only. */
        return;
    }
    h->Fault = fault;
    h->Port = port & 0x7FUL;
    h->Behind = (port & XHCI_INJ_HG_BEHIND) != 0 ? 1UL : 0UL;
    h->Outcome = XHCI_INJ_HG_OUTCOME(arg);
    h->Left = XHCI_INJ_HG_COUNT(arg);
    if (h->Left == 0) {
        h->Left = 1;
    }
    h->ByBehaviour = (fault == XHCI_INJ_DEVICE_HANG &&
                      (arg & XHCI_INJ_HG_BIT7) != 0) ? 1UL : 0UL;
    h->Recovery = ((fault == XHCI_INJ_ADDRESS_HANG ||
                    fault == XHCI_INJ_EP0_HANG) &&
                   (arg & XHCI_INJ_HG_BIT7) != 0) ? 1UL : 0UL;
    if (fault == XHCI_INJ_DEVICE_HANG) {
        /* Until the place's disconnect: the count is not spent. */
        h->Left = XHCI_INJ_HG_FOREVER;
    }
    h->Armed = h->ByBehaviour ? 0UL : 1UL;
}

ULONG XhciInjHangCmdType(ULONG fault)
{
    switch (fault) {
    case XHCI_INJ_ADDRESS_HANG:
    case XHCI_INJ_DEVICE_HANG:
        return XHCI_TRB_TYPE_ADDRESS_DEVICE;
    case XHCI_INJ_CFGEP_HANG:
        return XHCI_TRB_TYPE_CONFIGURE_EP;
    case XHCI_INJ_RESETEP_HANG:
        return XHCI_TRB_TYPE_RESET_EP;
    case XHCI_INJ_SETDEQ_HANG:
        return XHCI_TRB_TYPE_SET_TR_DEQUEUE;
    case XHCI_INJ_EVAL_HANG:
        return XHCI_TRB_TYPE_EVALUATE_CONTEXT;
    case XHCI_INJ_RESETDEV_HANG:
        return XHCI_TRB_TYPE_RESET_DEVICE;
    default:
        return 0;
    }
}

VOID XhciInjHangLatch(PXHCI_INJ_HANG h, ULONG slot, ULONG now)
{
    if (!h->Recovery || slot == 0) {
        return;
    }
    h->RecSlot = slot;
    if (h->Fault == XHCI_INJ_EP0_HANG && h->Outcome == XHCI_INJ_OUT_MARK) {
        h->MarkSlot = slot;
        h->MarkTick = now + XHCI_INJ_HG_MARK_TICKS;
    }
}

ULONG XhciInjHangRecoveryFits(const XHCI_INJ_HANG *h, ULONG type, ULONG slot,
                              ULONG epId)
{
    if (h->Fault == 0 || !h->Recovery || h->RecSlot == 0 ||
        slot != h->RecSlot) {
        return 0;
    }
    if (type == XHCI_TRB_TYPE_DISABLE_SLOT) {
        return 1;
    }
    /* Outcome 7's recovery command is the cycle's Disable Slot: the
     * teardown may stop the abandoned transfer's EP0 first
     * (hcdDeviceStop), and that Stop must go out. */
    return (h->Fault == XHCI_INJ_EP0_HANG &&
            h->Outcome != XHCI_INJ_OUT_MARK &&
            type == XHCI_TRB_TYPE_STOP_EP && epId == 1)
               ? 1UL
               : 0UL;
}

ULONG XhciInjHangRecoveryTake(PXHCI_INJ_HANG h, ULONG type, ULONG slot,
                              ULONG epId)
{
    if (!XhciInjHangRecoveryFits(h, type, slot, epId)) {
        return 0;
    }
    h->RecSlot = 0;
    h->Withheld++;
    return 1;
}

ULONG XhciInjHangMarkDue(PXHCI_INJ_HANG h, ULONG slot, ULONG now)
{
    if (h->MarkSlot == 0 || slot != h->MarkSlot ||
        (LONG)(now - h->MarkTick) < 0) {
        return 0;
    }
    h->MarkSlot = 0;
    h->MarkTick = 0;
    return 1;
}

ULONG XhciInjHangActOutcome(ULONG outcome)
{
    return outcome == XHCI_INJ_OUT_MARK ? XHCI_INJ_OUT_FETCHED : outcome;
}

ULONG XhciInjRvRouteLets(ULONG point, ULONG standing, ULONG latched)
{
    if (point != XHCI_INJ_RV_RECOVERY) {
        return 0;
    }
    return ((standing | latched) != 0) ? 1UL : 0UL;
}

ULONG XhciInjHangFits(PXHCI_INJ_HANG h, ULONG port, ULONG route)
{
    if (h->Fault == 0 || port == 0) {
        return 0;
    }
    if (h->Port != 0 && h->Port != port) {
        return 0;
    }
    if ((h->Behind != 0) != (route != 0)) {
        return 0;
    }
    return 1;
}

/* One withholding made: the place latched where it was "the first that
 * fits", the count spent unless for ever. */
static VOID xhciInjHangSpend(PXHCI_INJ_HANG h, ULONG port)
{
    h->Port = port;
    h->Withheld++;
    if (h->Left != XHCI_INJ_HG_FOREVER && h->Left != 0) {
        h->Left--;
    }
}

ULONG XhciInjHangCmdFits(PXHCI_INJ_HANG h, ULONG type, ULONG port,
                         ULONG route)
{
    if (!h->Armed || h->Left == 0 || type == 0 ||
        XhciInjHangCmdType(h->Fault) != type ||
        !XhciInjHangFits(h, port, route)) {
        return 0;
    }
    return 1;
}

ULONG XhciInjHangCmd(PXHCI_INJ_HANG h, ULONG type, ULONG port, ULONG route)
{
    if (!XhciInjHangCmdFits(h, type, port, route)) {
        return 0;
    }
    xhciInjHangSpend(h, port);
    return 1;
}

ULONG XhciInjHangEp0(PXHCI_INJ_HANG h, ULONG port, ULONG route)
{
    if (!h->Armed || h->Left == 0 ||
        (h->Fault != XHCI_INJ_EP0_HANG && h->Fault != XHCI_INJ_DEVICE_HANG) ||
        !XhciInjHangFits(h, port, route)) {
        return 0;
    }
    xhciInjHangSpend(h, port);
    return 1;
}

ULONG XhciInjHangLate(PXHCI_INJ_HANG h, ULONG port, ULONG route,
                      ULONG withheld)
{
    if (!h->ByBehaviour || h->Armed || withheld ||
        !XhciInjHangFits(h, port, route)) {
        return 0;
    }
    h->Port = port;
    h->Armed = 1;
    return 1;
}

ULONG XhciInjHangGone(PXHCI_INJ_HANG h, ULONG port)
{
    if (h->Fault != XHCI_INJ_DEVICE_HANG || h->Port == 0 || h->Port != port ||
        (!h->Armed && h->ByBehaviour)) {
        return 0;
    }
    XhciInjHangEnd(h);
    return 1;
}

VOID XhciInjCommandEvent(XHCI_TRB *ev, ULONG pa, ULONG cc)
{
    ev->Param0 = pa;
    ev->Param1 = 0;
    ev->Status = (cc & 0xFFUL) << 24;
    ev->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_COMMAND_COMPLETION);
}

ULONG XhciInjHangEvents(ULONG outcome, ULONG heldPA, ULONG nextPA,
                        ULONG badPA, XHCI_TRB *ev, PULONG rewrite,
                        PULONG ring)
{
    *rewrite = 0;
    *ring = 0;
    switch (outcome) {
    case XHCI_INJ_OUT_STOP_HERE:
        XhciInjCommandEvent(&ev[0], heldPA, XHCI_CC_COMMAND_RING_STOPPED);
        return 1;
    case XHCI_INJ_OUT_FETCHED:
        *rewrite = 1;
        XhciInjCommandEvent(&ev[0], heldPA, XHCI_CC_COMMAND_ABORTED);
        XhciInjCommandEvent(&ev[1], nextPA, XHCI_CC_COMMAND_RING_STOPPED);
        return 2;
    case XHCI_INJ_OUT_FETCHED_NA:
        *rewrite = 1;
        XhciInjCommandEvent(&ev[0], nextPA, XHCI_CC_COMMAND_RING_STOPPED);
        return 1;
    case XHCI_INJ_OUT_ABORT_HERE:
        XhciInjCommandEvent(&ev[0], heldPA, XHCI_CC_COMMAND_ABORTED);
        XhciInjCommandEvent(&ev[1], heldPA, XHCI_CC_COMMAND_RING_STOPPED);
        return 2;
    case XHCI_INJ_OUT_RACE:
        *ring = 1;
        return 0;
    case XHCI_INJ_OUT_DIVERGE:
        XhciInjCommandEvent(&ev[0], badPA, XHCI_CC_COMMAND_RING_STOPPED);
        return 1;
    default:
        return 0;
    }
}

/* ----------------------------------------------------------------------- */
/* Task 50.30 step 3: 2D, the query-stop exercise (record 17 4.15.16 E)     */
/* ----------------------------------------------------------------------- */

VOID XhciInjQsStart(PXHCI_INJ_QSTOP q)
{
    q->State = XHCI_INJ_QS_IDLE;
    q->Arg = 0;
    q->Abort = 0;
    q->Terminal = 0;
    q->Released = 0;
    q->Due = 0;
    q->Closed = 0;
    q->HeldSlot = 0;
    q->HeldPA = 0;
}

ULONG XhciInjQsArmRefusal(const XHCI_INJ_QSTOP *q, ULONG started,
                          ULONG mode4, ULONG isolation)
{
    if (!started) {
        return XHCI_INJ_QS_R_STOPPED;
    }
    if (!mode4) {
        return XHCI_INJ_QS_R_MODE;
    }
    if (!isolation) {
        return XHCI_INJ_QS_R_ISOLATION;
    }
    if (q->State != XHCI_INJ_QS_IDLE) {
        return XHCI_INJ_QS_R_ACTIVE;
    }
    if (q->Busy != 0) {
        return XHCI_INJ_QS_R_BUSY;
    }
    if (q->Closed) {
        return XHCI_INJ_QS_R_CLOSED;
    }
    return XHCI_INJ_QS_R_OK;
}

VOID XhciInjQsArm(PXHCI_INJ_QSTOP q, ULONG arg)
{
    q->State = XHCI_INJ_QS_ARMED;
    q->Arg = arg & 0xFFUL;
    q->Abort = 0;
    q->Terminal = 0;
    q->Released = 0;
    q->Due = 0;
    q->HeldSlot = 0;
    q->HeldPA = 0;
}

ULONG XhciInjQsTrigger(PXHCI_INJ_QSTOP q, ULONG slot, ULONG pa, ULONG now)
{
    if (q->State != XHCI_INJ_QS_ARMED) {
        return 0;
    }
    q->State = XHCI_INJ_QS_TRIGGERED;
    q->Due = now + XHCI_INJ_QS_DELAY(q->Arg);
    q->HeldSlot = slot;
    q->HeldPA = pa;
    return 1;
}

ULONG XhciInjQsDebtFits(const XHCI_INJ_QSTOP *q, ULONG owed, ULONG slot,
                        ULONG pa)
{
    return (owed != 0 && q->HeldPA != 0 && slot == q->HeldSlot &&
            pa == q->HeldPA)
               ? 1UL
               : 0UL;
}

ULONG XhciInjQsStep(PXHCI_INJ_QSTOP q, ULONG now, ULONG ready, ULONG debt)
{
    switch (q->State) {
    case XHCI_INJ_QS_TRIGGERED:
        if (q->Abort) {
            /* A bracket ends a triggered exercise itself; never left
             * waiting here. */
            q->State = XHCI_INJ_QS_IDLE;
            return XHCI_INJ_QS_ENDED;
        }
        if ((LONG)(now - q->Due) < 0 || q->Busy != 0 || q->Closed ||
            !ready) {
            return XHCI_INJ_QS_NONE;
        }
        if ((q->Arg & XHCI_INJ_QS_WINDOW) != 0 && !debt) {
            q->State = XHCI_INJ_QS_IDLE;
            return XHCI_INJ_QS_NODEBT;
        }
        q->State = XHCI_INJ_QS_CALLING;
        return XHCI_INJ_QS_QUERY;
    case XHCI_INJ_QS_QUERIED:
        if (q->Abort && q->Terminal) {
            q->State = XHCI_INJ_QS_IDLE;
            q->Released = 0;
            return XHCI_INJ_QS_HANDOFF;
        }
        if (q->Abort || (LONG)(now - q->Due) >= 0) {
            q->State = XHCI_INJ_QS_CALLING;
            return XHCI_INJ_QS_CANCEL;
        }
        return XHCI_INJ_QS_NONE;
    default:
        return XHCI_INJ_QS_NONE;
    }
}

VOID XhciInjQsQueried(PXHCI_INJ_QSTOP q, ULONG released, ULONG now)
{
    q->State = XHCI_INJ_QS_QUERIED;
    q->Released = released ? 1UL : 0UL;
    q->Due = now + XHCI_INJ_QS_HOLD(q->Arg) * XHCI_INJ_QS_TICKS_PER_S;
}

VOID XhciInjQsEnded(PXHCI_INJ_QSTOP q)
{
    q->State = XHCI_INJ_QS_IDLE;
    q->Released = 0;
}

ULONG XhciInjQsBracket(PXHCI_INJ_QSTOP q, ULONG terminal)
{
    q->Busy++;
    if (terminal) {
        q->Closed = 1;
        q->Terminal = 1;
    }
    q->Abort = 1;
    switch (q->State) {
    case XHCI_INJ_QS_ARMED:
    case XHCI_INJ_QS_TRIGGERED:
        q->State = XHCI_INJ_QS_IDLE;
        return XHCI_INJ_QS_BR_ENDED;
    case XHCI_INJ_QS_CALLING:
    case XHCI_INJ_QS_QUERIED:
        return XHCI_INJ_QS_BR_WAIT;
    default:
        return XHCI_INJ_QS_BR_NOTHING;
    }
}

VOID XhciInjQsLeave(PXHCI_INJ_QSTOP q)
{
    if (q->Busy != 0) {
        q->Busy--;
    }
}

#else /* release: injection is not in the image */

typedef int xhciInjNotInImage;

#endif
