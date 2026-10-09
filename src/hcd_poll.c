/*
 * hcd_poll.c - polled event delivery and the dead-line watcher, the kernel
 * half (roadmap-hcd.md tasks 36.7 and 36.8; design record 15 sections 4.2,
 * 5, 6 and 7.1). The decisions are xhci_poll.c's; this file owns the
 * timer, its DPC, PollLock, the register and configuration access, and
 * calls them.
 *
 * Lock order: the controller lock, then PollLock; never the reverse.
 * KeSynchronizeExecution (the interrupt object's lock, at DIRQL) is taken
 * with the controller lock held or with no lock, never with PollLock held.
 * Neither spin lock is held across a drain or a configuration request.
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* The pure core's copies of the register bits (xhci_poll.h) are xhci.h's. */
XHCI_C_ASSERT(hcd_poll_inte, XHCI_POLL_USBCMD_INTE == XHCI_USBCMD_INTE);
XHCI_C_ASSERT(hcd_poll_hcrst, XHCI_POLL_USBCMD_HCRST == XHCI_USBCMD_HCRST);
XHCI_C_ASSERT(hcd_poll_cnr, XHCI_POLL_USBSTS_CNR == XHCI_USBSTS_CNR);
XHCI_C_ASSERT(hcd_poll_ip, XHCI_POLL_IMAN_IP == XHCI_IMAN_IP);
XHCI_C_ASSERT(hcd_poll_ie, XHCI_POLL_IMAN_IE == XHCI_IMAN_IE);

/* PCI Command, bit 10: Interrupt Disable (PCI 3.0 6.2.2). */
#define HCD_PCI_COMMAND_INTX_DISABLE 0x0400U

static VOID NTAPI hcdPollDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                             PVOID Arg2);
static VOID hcdPollQemuLatch(PHCD_CONTROLLER hc, ULONG now);

/* The low word of the system time, in 100 ns units. IRQL: any. */
static ULONG hcdPollNow(VOID)
{
    LARGE_INTEGER now;

    KeQuerySystemTime(&now);
    return now.LowPart;
}

/* The owner's KeSetTimer, with the period and the state that chose it
 * recorded together (record 15 section 8). IRQL: <= DISPATCH_LEVEL,
 * PollLock held. */
static VOID hcdPollArm(PHCD_CONTROLLER hc, ULONG ms, ULONG state)
{
    LARGE_INTEGER due;

    HcdRelativeMs(&due, ms);
    (VOID)KeSetTimer(&hc->PollTimer, due, &hc->PollTimerDpc);
    hc->Counters.PollPeriodLast = ms;
    hc->Counters.PollStateLast = state;
}

/* The state a first arm (a kick, or a kick owed) stands for. */
static ULONG hcdPollFirstState(PHCD_CONTROLLER hc)
{
    return (hc->Poll.Role == XHCI_POLL_ROLE_WATCH) ? XHCI_POLL_BUS_WATCH
                                                   : XHCI_POLL_BUS_ACTIVE;
}

/* Once, beside the controller's other objects. IRQL: PASSIVE_LEVEL. */
VOID HcdPollInitObjects(PHCD_CONTROLLER hc)
{
    KeInitializeTimer(&hc->PollTimer);
    KeInitializeDpc(&hc->PollTimerDpc, hcdPollDpc, hc);
    KeInitializeSpinLock(&hc->PollLock);
    XhciPollInit(&hc->Poll);
}

/*
 * A start whose delivery is a poll (SERVE), or whose effective mode is 0
 * under a line (WATCH): CLOSED becomes PARKED; the grant arms it. Only
 * after the previous release saw CLOSED. IRQL: PASSIVE_LEVEL.
 */
VOID HcdPollOpen(PHCD_CONTROLLER hc, ULONG role)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->PollLock, &oldIrql);
    if (XhciPollOpen(&hc->Poll, role)) {
        hc->PollObs.LastValid = 0;
        hc->PollObs.Last = 0;
        hc->PollObs.MinMs = XHCI_POLL_NOT_FOUND;
        hc->PollObs.MaxMs = 0;
        hc->PollObs.Discarded = 0;
        hc->PollLastEvents = 0;
        hc->PollLastEventTime = 0;
        hc->PollEventSeen = 0;
        hc->PollContainAsked = 0;
        hc->Counters.PollRole = role;
    }
    KeReleaseSpinLock(&hc->PollLock, oldIrql);
}

/* The poller's admission to the hardware (record 15 section 5.3).
 * IRQL: DISPATCH_LEVEL, controller lock held. */
static ULONG hcdPollAdmitted(PHCD_CONTROLLER hc)
{
    return (hc->Hc.PollService && !hc->Hc.ControllerFailed &&
            HcdCtlDrainAdmitted(hc))
               ? 1UL
               : 0UL;
}

/*
 * The grant (record 15 section 5.3): after the start's or a resume's D0
 * is published, or a recovery succeeded, with the power gate held. Sets
 * PollService only on a controller the poller's whole admission takes,
 * advances the generation, and kicks. IRQL: PASSIVE_LEVEL, power gate
 * held by the caller.
 */
VOID HcdPollGrant(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    KIRQL pollIrql;
    ULONG ms;
    ULONG now;

    ext = &hc->Hc;
    now = hcdPollNow();
    XhciControllerLockAcquire(ext, &oldIrql);
    if ((ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
        (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0 &&
        HcdCtlDrainAdmitted(hc)) {
        ext->PollService = 1;
        ext->PollServiceGen++;
#if defined(XHCI_FLAVOUR_QEMU)
        if (hc->QemuDeadLine > 1 && !hc->QemuDeadLatched) {
            hc->QemuDeadAt =
                now + (hc->QemuDeadLine - 1) * XHCI_POLL_TICKS_PER_MS;
        }
#endif
        KeAcquireSpinLock(&hc->PollLock, &pollIrql);
        if (XhciPollKick(&hc->Poll, now, hc->PollValues.ActiveMs, &ms)) {
            hcdPollArm(hc, ms, hcdPollFirstState(hc));
            hc->Counters.PollKicks++;
        } else if (hc->Poll.KickOwed) {
            hc->Counters.PollKicks++;
        }
        KeReleaseSpinLock(&hc->PollLock, pollIrql);
    }
    XhciControllerLockRelease(ext, oldIrql);
}

/* A lifecycle caller's early revocation (record 15 section 4.2, the
 * containment's step 2). IRQL: <= DISPATCH_LEVEL. */
VOID HcdPollRevoke(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Hc.PollService = 0;
    hc->Hc.PollServiceGen++;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * The close, first in the release, before the interrupt is disconnected
 * (record 15 section 5.2). Waits for CLOSED, then takes and drops
 * PollLock once more so the DPC's last release of it has happened.
 * IRQL: PASSIVE_LEVEL.
 */
VOID HcdPollClose(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    LARGE_INTEGER due;
    ULONG step;
    ULONG closed;

    for (;;) {
        KeAcquireSpinLock(&hc->PollLock, &oldIrql);
        step = XhciPollCloseBegin(&hc->Poll);
        if (step == XHCI_POLL_C_CANCEL) {
            (VOID)XhciPollCloseDone(&hc->Poll,
                                    KeCancelTimer(&hc->PollTimer) ? 1UL
                                                                  : 0UL);
        }
        closed = (hc->Poll.State == XHCI_POLL_S_CLOSED) ? 1UL : 0UL;
        KeReleaseSpinLock(&hc->PollLock, oldIrql);
        if (closed) {
            break;
        }
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    KeAcquireSpinLock(&hc->PollLock, &oldIrql);
    KeReleaseSpinLock(&hc->PollLock, oldIrql);
}

/* ------------------------------------------------------------------ */
/* The line's retirement (record 15 section 4.2, step 3)              */
/* ------------------------------------------------------------------ */

typedef struct _HCD_POLL_SYNC {
    PHCD_CONTROLLER Hc;
    ULONG Result;
} HCD_POLL_SYNC, *PHCD_POLL_SYNC;

/* XhciPollRetire's register access. IRQL: DIRQL. */
static ULONG hcdPollRegRead(PVOID context, ULONG reg)
{
    PXHCI_EXTENSION ext;

    ext = &((PHCD_CONTROLLER)context)->Hc;
    if (reg == XHCI_POLL_REG_USBCMD) {
        return XhciReadOp(ext, XHCI_OP_USBCMD);
    }
    if (reg == XHCI_POLL_REG_USBSTS) {
        return XhciReadOp(ext, XHCI_OP_USBSTS);
    }
    return XhciReadIr0(ext, XHCI_IR_IMAN);
}

/* INTE cleared from the validated value, RsvdP preserved, HCRST never
 * carried - XhciMaskInterrupts' own write. IRQL: DIRQL. */
static VOID hcdPollClearInte(PVOID context, ULONG usbcmd)
{
    XhciWriteOp(&((PHCD_CONTROLLER)context)->Hc, XHCI_OP_USBCMD,
                (usbcmd & XHCI_USBCMD_RSVDP_MASK) |
                    ((usbcmd & XHCI_USBCMD_DEFINED_MASK) &
                     ~(XHCI_USBCMD_INTE | XHCI_USBCMD_HCRST)));
}

static VOID hcdPollWriteIman(PVOID context, ULONG value)
{
    XhciWriteIr0(&((PHCD_CONTROLLER)context)->Hc, XHCI_IR_IMAN, value);
}

/* Under the interrupt object's lock: the retirement, and LineRetired
 * published inside it. IRQL: DIRQL. */
static BOOLEAN NTAPI hcdPollRetireSync(PVOID Context)
{
    PHCD_POLL_SYNC sync;
    XHCI_POLL_REGS io;

    sync = (PHCD_POLL_SYNC)Context;
    io.Context = sync->Hc;
    io.Read = hcdPollRegRead;
    io.ClearInte = hcdPollClearInte;
    io.WriteIman = hcdPollWriteIman;
    sync->Result = XhciPollRetire(&io);
    if (sync->Result == XHCI_POLL_RETIRE_PROVEN) {
        sync->Hc->Hc.LineRetired = 1;
    }
    return TRUE;
}

/* Under the interrupt object's lock: a proof made outside it (the PCI
 * Interrupt Disable read back) published. IRQL: DIRQL. */
static BOOLEAN NTAPI hcdPollPublishSync(PVOID Context)
{
    ((PHCD_POLL_SYNC)Context)->Hc->Hc.LineRetired = 1;
    return TRUE;
}

/*
 * One DIRQL retirement attempt, on a valid register layout only. Returns
 * whether LineRetired now holds. IRQL: <= DISPATCH_LEVEL, controller lock
 * held (it reads HcInfoStatus under it).
 */
static ULONG hcdPollRetireLocked(PHCD_CONTROLLER hc, PULONG result)
{
    PXHCI_EXTENSION ext;
    HCD_POLL_SYNC sync;

    ext = &hc->Hc;
    if (result != NULL) {
        *result = XHCI_POLL_RETIRE_ENABLES;
    }
    if (!ext->LineMoved || ext->LineRetired) {
        return ext->LineRetired;
    }
    if (hc->Interrupt == NULL || ext->HcInfoStatus != XHCI_HC_OK) {
        return 0;
    }
    sync.Hc = hc;
    sync.Result = XHCI_POLL_RETIRE_ENABLES;
    (VOID)KeSynchronizeExecution(hc->Interrupt, hcdPollRetireSync, &sync);
    if (result != NULL) {
        *result = sync.Result;
    }
    hc->Counters.LineRetireAttempts++;
    if (ext->LineRetired) {
        ext->LineRetireState = XHCI_POLL_RETIRED_ACK;
        hc->Counters.LineRetireState = XHCI_POLL_RETIRED_ACK;
        XhciLogNoteLocked(ext, "intr.retired", XHCI_POLL_RETIRED_ACK);
    }
    return ext->LineRetired;
}

/*
 * The PASSIVE containment (record 15 section 4.2): the DIRQL retirement
 * once more, then PCI Interrupt Disable, read back. Returns 1 when the
 * line is retired (or nothing is owed), 0 when it is still owed, 2 when
 * the controller is not started in D0 and nothing was tried. IRQL:
 * PASSIVE_LEVEL, the power gate held, no spin lock held; a lifecycle
 * caller revokes first (HcdPollRevoke).
 */
ULONG HcdPollContain(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    HCD_POLL_SYNC sync;
    KIRQL oldIrql;
    USHORT command;
    ULONG retired;

    ext = &hc->Hc;
    if (!ext->LineMoved || ext->LineRetired) {
        return 1;
    }
    if (!hc->ControllerStarted || hc->Common.DevicePower != PowerDeviceD0 ||
        hc->SuspendedInD0) {
        XhciLogNote(ext, "intr.contain.unpowered", 1);
        return 2;
    }
    XhciControllerLockAcquire(ext, &oldIrql);
    retired = hcdPollRetireLocked(hc, NULL);
    XhciControllerLockRelease(ext, oldIrql);
    if (retired) {
        return 1;
    }

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command, sizeof(USHORT)) !=
            MP_STATUS_SUCCESS ||
        command == 0xFFFFU) {
        return 0;
    }
    if ((command & HCD_PCI_COMMAND_INTX_DISABLE) == 0) {
        command = (USHORT)(command | HCD_PCI_COMMAND_INTX_DISABLE);
        if (XhciWritePciConfig(ext, XHCI_PCI_COMMAND, &command,
                               sizeof(USHORT)) != MP_STATUS_SUCCESS) {
            return 0;
        }
        command = 0;
        if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                              sizeof(USHORT)) != MP_STATUS_SUCCESS ||
            command == 0xFFFFU ||
            (command & HCD_PCI_COMMAND_INTX_DISABLE) == 0) {
            return 0;
        }
    }
    sync.Hc = hc;
    sync.Result = 0;
    if (hc->Interrupt != NULL) {
        (VOID)KeSynchronizeExecution(hc->Interrupt, hcdPollPublishSync,
                                     &sync);
    } else {
        ext->LineRetired = 1;
    }
    XhciControllerLockAcquire(ext, &oldIrql);
    ext->LineRetireState = XHCI_POLL_RETIRED_INTX_DISABLE;
    hc->Counters.LineRetireState = XHCI_POLL_RETIRED_INTX_DISABLE;
    XhciLogNoteLocked(ext, "intr.retired", XHCI_POLL_RETIRED_INTX_DISABLE);
    XhciControllerLockRelease(ext, oldIrql);
    return 1;
}

/*
 * The controller thread's share, once per pass (hcdPoll): an owed
 * retirement contained, and the recovery asked for once when it cannot
 * be (containment step 3); and a peek. IRQL: PASSIVE_LEVEL.
 */
VOID HcdPollThreadPass(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG contained;

    ext = &hc->Hc;
    if (ext->LineMoved && !ext->LineRetired) {
        HcdPowerGateEnter(hc);
        contained = HcdPollContain(hc);
        HcdPowerGateLeave(hc);
        if (contained == 0 && !hc->PollContainAsked) {
            hc->PollContainAsked = 1;
            XhciLogNote(ext, "intr.retire.recover", 1);
            XhciRequestControllerReset(ext);
        }
    }
    XhciControllerLockAcquire(ext, &oldIrql);
    hcdPollQemuLatch(hc, hcdPollNow());
    HcdPollPeekLocked(hc);
    XhciControllerLockRelease(ext, oldIrql);
}

/* ------------------------------------------------------------------ */
/* Peeks and promotions (record 15 sections 6.2 and 7.1)               */
/* ------------------------------------------------------------------ */

/*
 * An event the ring holds and no drain has taken, under a poll delivery
 * only: IsrDpc queued through the admission rule, never drained inline.
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID HcdPollPeekLocked(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL pollIrql;

    ext = &hc->Hc;
    if (ext->EventDelivery != XHCI_POLL_DELIVERY_POLL || !ext->PollService) {
        return;
    }
    hc->Counters.PollPeeks++;
    if (!hcdPollAdmitted(hc) || !XhciEventRingPending(&ext->EventRing)) {
        return;
    }
    hc->Counters.PollPeekHits++;
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    if (XhciPollPeekAdmit(&hc->Poll)) {
        (VOID)InterlockedIncrement(&hc->DpcsInFlight);
        if (hc->DpcClosed || !KeInsertQueueDpc(&hc->IsrDpc, NULL, NULL)) {
            (VOID)InterlockedDecrement(&hc->DpcsInFlight);
        }
    }
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
}

/*
 * A need of `needMs` brings the next poll earlier, never later, under a
 * poll delivery only. IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID HcdPollPromoteLocked(PHCD_CONTROLLER hc, ULONG needMs)
{
    KIRQL pollIrql;
    ULONG now;
    ULONG deadline;
    ULONG step;
    ULONG ms;
    ULONG displaced;
    ULONG cancelled;

    if (hc->Hc.EventDelivery != XHCI_POLL_DELIVERY_POLL) {
        return;
    }
    if (needMs < hc->PollValues.ActiveMs) {
        needMs = hc->PollValues.ActiveMs;
    }
    now = hcdPollNow();
    deadline = now + needMs * XHCI_POLL_TICKS_PER_MS;
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    step = XhciPollPromote(&hc->Poll, deadline);
    if (step == XHCI_POLL_P_CANCEL) {
        cancelled = KeCancelTimer(&hc->PollTimer) ? 1UL : 0UL;
        if (XhciPollPromoteDone(&hc->Poll, deadline, cancelled, now,
                                hc->PollValues.ActiveMs,
                                hc->PollValues.IdleMs, &ms, &displaced)) {
            hcdPollArm(hc, ms,
                       (needMs <= hc->PollValues.ActiveMs)
                           ? XHCI_POLL_BUS_ACTIVE
                           : XHCI_POLL_BUS_PERIODIC);
            if (displaced) {
                hc->Counters.PollArmsDisplaced++;
            }
        }
        hc->Counters.PollPromotions++;
    } else if (step == XHCI_POLL_P_OWED) {
        hc->Counters.PollPromotions++;
    }
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
}

/*
 * Every doorbell, after the write (xhci_pci.c, XhciWriteDoorbell): the
 * promotion its need asks for, then a peek. Doorbell 0 is a command, DCI 1
 * a control endpoint; an interrupt endpoint needs its service interval,
 * and anything the lookup cannot place the active period. IRQL:
 * DISPATCH_LEVEL, controller lock held (every caller holds it).
 */
VOID HcdSvcDoorbell(PXHCI_EXTENSION ext, ULONG slot, ULONG value)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    ULONG dci;
    ULONG need;

    if (ext->EventDelivery != XHCI_POLL_DELIVERY_POLL) {
        return;
    }
    hc = HcdControllerFromExt(ext);
    need = 0;
    if (slot != 0 && slot <= XHCI_MAX_SLOTS) {
        dev = hc->SlotDevice[slot];
        dci = value & 0xFFUL;
        if (dev != NULL && dci > 1 && dci < 32) {
            pipe = dev->Pipes[dci];
            if (pipe != NULL &&
                pipe->TransferType == XHCI_PIPE_XFER_INTERRUPT) {
                /* A hub's status endpoint never asks for less than the
                 * idle period (record 15 section 6.1). */
                need = (dev->Hub != NULL)
                           ? hc->PollValues.IdleMs
                           : XhciPollIntervalMs(pipe->Ep.Interval);
            }
        }
    }
    HcdPollPromoteLocked(hc, need);
    HcdPollPeekLocked(hc);
}

/* The thread's count of ports in reset or enumeration (record 15 section
 * 6.1). IRQL: PASSIVE_LEVEL. */
VOID HcdPollPortsBusy(PHCD_CONTROLLER hc, ULONG busy)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->PollPortsBusy = busy;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * The qemu flavour's delayed dead line (record 15 section 11), latched by
 * whichever looks first - the watcher's DPC or the controller thread, so
 * mode 1, which has no watcher, latches too. IRQL: DISPATCH_LEVEL,
 * controller lock held.
 */
static VOID hcdPollQemuLatch(PHCD_CONTROLLER hc, ULONG now)
{
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuDeadLine > 1 && !hc->QemuDeadLatched &&
        hc->Hc.PollService && (LONG)(now - hc->QemuDeadAt) >= 0) {
        hc->QemuDeadLatched = 1;
        XhciLogNoteLocked(&hc->Hc, "qemu.inj.deadline", 1);
    }
#else
    UNREFERENCED_PARAMETER(hc);
    UNREFERENCED_PARAMETER(now);
#endif
}

/* ------------------------------------------------------------------ */
/* The timer's DPC (record 15 sections 4.2, 5.2 and 6.1)              */
/* ------------------------------------------------------------------ */

/* The bus as this pass's drain left it. IRQL: DISPATCH_LEVEL, controller
 * lock held. */
static VOID hcdPollBus(PHCD_CONTROLLER hc, PXHCI_POLL_BUS bus)
{
    PXHCI_EXTENSION ext;
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    PHCD_PIPE sp;
    ULONG slots;
    ULONG i;
    ULONG dci;
    ULONG id;
    ULONG ms;

    ext = &hc->Hc;
    bus->Command = (ext->CommandState != XHCI_CMD_STATE_IDLE) ? 1UL : 0UL;
    bus->Async = 0;
    bus->PeriodicMs = 0;
    bus->PortsBusy = (hc->PollPortsBusy != 0) ? 1UL : 0UL;
    slots = ext->HcInfo.MaxSlots;
    if (slots > XHCI_MAX_SLOTS) {
        slots = XHCI_MAX_SLOTS;
    }
    for (i = 1; i <= slots && !bus->Async; i++) {
        dev = hc->SlotDevice[i];
        if (dev == NULL) {
            continue;
        }
        if (dev->Ep0Queue.Count != 0) {
            bus->Async = 1;
            break;
        }
        for (dci = 2; dci < 32 && !bus->Async; dci++) {
            pipe = dev->Pipes[dci];
            if (pipe == NULL) {
                continue;
            }
            if (pipe->Streams != NULL) {
                for (id = 1; id <= pipe->Streams->Count &&
                             id < XHCI_STREAM_MAX_ENTRIES;
                     id++) {
                    sp = pipe->Streams->Pipe[id];
                    if (sp != NULL && sp->Queue != NULL &&
                        sp->Queue->Count != 0) {
                        bus->Async = 1;
                        break;
                    }
                }
                continue;
            }
            if (pipe->Queue == NULL || pipe->Queue->Count == 0) {
                continue;
            }
            if (pipe->TransferType != XHCI_PIPE_XFER_INTERRUPT) {
                bus->Async = 1;
                break;
            }
            if (dev->Hub != NULL) {
                /* A hub's status endpoint: nothing shorter than the idle
                 * period (record 15 section 6.1). */
                continue;
            }
            ms = XhciPollIntervalMs(pipe->Ep.Interval);
            if (ms == 0) {
                ms = 1;
            }
            if (bus->PeriodicMs == 0 || ms < bus->PeriodicMs) {
                bus->PeriodicMs = ms;
            }
        }
    }
}

/*
 * One look (record 15 section 4.2): IP, IE and INTE all 1. Two reads,
 * no write. IRQL: DISPATCH_LEVEL, controller lock held.
 */
static ULONG hcdPollLook(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG iman;
    ULONG usbcmd;

    ext = &hc->Hc;
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuDeadLatched) {
        return 1;
    }
#endif
    iman = XhciReadIr0(ext, XHCI_IR_IMAN);
    if (iman == 0xFFFFFFFFUL ||
        (iman & (XHCI_IMAN_IP | XHCI_IMAN_IE)) !=
            (XHCI_IMAN_IP | XHCI_IMAN_IE)) {
        return 0;
    }
    usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
    if (usbcmd == 0xFFFFFFFFUL || (usbcmd & XHCI_USBCMD_INTE) == 0) {
        return 0;
    }
    return 1;
}

/*
 * The move (record 15 section 4.2), by the watcher's DPC as the owner:
 * the delivery and reason, the re-arm closed off, LineMoved, the routing
 * quirk's delivery, the retirement under the ISR's lock, and the role.
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
static VOID hcdPollMove(PHCD_CONTROLLER hc, ULONG pairs)
{
    PXHCI_EXTENSION ext;
    KIRQL pollIrql;
    ULONG result;

    ext = &hc->Hc;
    ext->EventDelivery = XHCI_POLL_DELIVERY_POLL;
    hc->PollDelivery = XHCI_POLL_DELIVERY_POLL;
    hc->PollReason = XHCI_POLL_R_AUTO_LINE_DEAD;
    hc->Counters.IntrDelivery = XHCI_POLL_DELIVERY_POLL;
    hc->Counters.IntrReason = XHCI_POLL_R_AUTO_LINE_DEAD;
    ext->Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
    ext->LineMoved = 1;
    ext->LineRetireState = XHCI_POLL_RETIRED_OWED;
    hc->Counters.LineRetireState = XHCI_POLL_RETIRED_OWED;
    hc->IntDelivery = XHCI_PIRQ_DELIVERY_POLL;
    hc->Counters.LineMoves = 1;
    XhciLogNoteLocked(ext, "intr.moved", pairs);
    (VOID)hcdPollRetireLocked(hc, &result);
    /* The mask's own outcome, apart from the IP step's (record 15 section
     * 8): both enables proven down unless the first step failed. */
    hc->Counters.LineMoveMasked =
        (result == XHCI_POLL_RETIRE_PROVEN || result == XHCI_POLL_RETIRE_IP)
            ? 1UL
            : 0UL;
    XhciLogNoteLocked(ext, "intr.moved.masked", hc->Counters.LineMoveMasked);
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    XhciPollServe(&hc->Poll);
    hc->Counters.PollRole = XHCI_POLL_ROLE_SERVE;
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
}

static VOID NTAPI hcdPollDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                             PVOID Arg2)
{
    PHCD_CONTROLLER hc;
    PXHCI_EXTENSION ext;
    XHCI_POLL_BUS bus;
    KIRQL oldIrql;
    KIRQL pollIrql;
    ULONG take;
    ULONG role;
    ULONG now;
    ULONG decision;
    ULONG state;
    ULONG ms;
    ULONG displaced;
    ULONG asserted;
    ULONG claims;
    ULONG dead;
    ULONG pairs;
    ULONG drained;
    ULONG bounds;
    ULONG portEvents;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    hc = (PHCD_CONTROLLER)Context;
    ext = &hc->Hc;
    now = hcdPollNow();
    decision = hc->PollValues.IdleMs;
    state = XHCI_POLL_BUS_IDLE;

    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    take = XhciPollTake(&hc->Poll);
    role = hc->Poll.Role;
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
    if (take == XHCI_POLL_TAKE_NONE) {
        return;
    }
    if (take == XHCI_POLL_TAKE_CLOSING) {
        goto epilogue;
    }

    XhciControllerLockAcquire(ext, &oldIrql);
    if (!hcdPollAdmitted(hc)) {
        KeAcquireSpinLock(&hc->PollLock, &pollIrql);
        XhciPollPark(&hc->Poll);
        KeReleaseSpinLock(&hc->PollLock, pollIrql);
        hc->Counters.PollParks++;
        XhciControllerLockRelease(ext, oldIrql);
        goto epilogue;
    }

    if (role == XHCI_POLL_ROLE_WATCH) {
        hcdPollQemuLatch(hc, now);
        hc->Counters.WatchLooks++;
        asserted = hcdPollLook(hc);
        claims = ext->InterruptsClaimed;
#if defined(XHCI_FLAVOUR_QEMU)
        claims -= (ULONG)hc->QemuDeadClaims;
#endif
        KeAcquireSpinLock(&hc->PollLock, &pollIrql);
        dead = XhciPollWatchLook(&hc->Poll, ext->PollServiceGen, asserted,
                                 claims, now);
        pairs = hc->Poll.WatchPairs;
        KeReleaseSpinLock(&hc->PollLock, pollIrql);
        if (asserted) {
            hc->Counters.WatchAsserted++;
            if (pairs != 0) {
                hc->Counters.WatchStalePairs++;
            }
        }
        if (!dead || hc->PollValues.Mode != XHCI_POLL_MODE_AUTO ||
            ext->EventDelivery != XHCI_POLL_DELIVERY_LINE) {
            XhciControllerLockRelease(ext, oldIrql);
            goto epilogue;
        }
        hcdPollMove(hc, pairs);
    }

    /* The service pass. An owed retirement is tried at its start. */
    (VOID)hcdPollRetireLocked(hc, NULL);
    bounds = ext->DrainBoundHits;
    portEvents = hc->PortEvents;
    XhciControllerLockRelease(ext, oldIrql);

    drained = 0;
    (VOID)XhciEventDpcDrained(ext, FALSE, &drained);
    if (hc->PortEvents != portEvents) {
        HcdThreadWake(hc);
    }

    XhciControllerLockAcquire(ext, &oldIrql);
    hc->Counters.PollRuns++;
    if (drained != 0) {
        hc->Counters.PollRunsWithEvents++;
    }
    if (ext->EventsTotal != hc->PollLastEvents) {
        hc->PollLastEvents = ext->EventsTotal;
        hc->PollLastEventTime = now;
        hc->PollEventSeen = 1;
    }
    hcdPollBus(hc, &bus);
    bus.DrainedRecently = XhciPollRecent(&hc->PollEventSeen,
                                         hc->PollLastEventTime, now,
                                         hc->PollValues.IdleMs);
    bus.Bounded = (ext->DrainBoundHits != bounds &&
                   XhciEventRingPending(&ext->EventRing))
                      ? 1UL
                      : 0UL;
    decision = XhciPollDecidePeriod(&bus, hc->PollValues.ActiveMs,
                                    hc->PollValues.IdleMs, &state);
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    XhciPollObserve(&hc->PollObs, now);
    hc->Counters.PollIntervalMinMs = hc->PollObs.MinMs;
    hc->Counters.PollIntervalMaxMs = hc->PollObs.MaxMs;
    hc->Counters.PollIntervalsDiscarded = hc->PollObs.Discarded;
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
    XhciControllerLockRelease(ext, oldIrql);

epilogue:
    /* The only place the DPC gives the poller up (record 15 section 5.2):
     * after both locks are released, PollLock alone. The time is taken
     * again, so a PollOwed that fell due during the pass is served at
     * once (section 6.2) and the armed deadline is the one set. */
    now = hcdPollNow();
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    if (hc->Poll.Role == XHCI_POLL_ROLE_WATCH) {
        state = XHCI_POLL_BUS_WATCH;
    } else if (hc->Poll.ParkPending) {
        state = XHCI_POLL_BUS_ACTIVE;
    } else if (hc->Poll.State == XHCI_POLL_S_FIRING && hc->Poll.OwedValid &&
               XhciPollEarlier(hc->Poll.Owed,
                               now + decision * XHCI_POLL_TICKS_PER_MS)) {
        /* A promotion owed during the pass wins the arm: its state is
         * the active one (record 15 section 8). */
        state = XHCI_POLL_BUS_ACTIVE;
    }
    if (XhciPollEnd(&hc->Poll, decision, now, hc->PollValues.ActiveMs,
                    hc->PollValues.IdleMs, &ms, &displaced)) {
        hcdPollArm(hc, ms, state);
        if (displaced) {
            hc->Counters.PollArmsDisplaced++;
        }
    }
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
}
