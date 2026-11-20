/*
 * hcd_inj.c - 35-T.9's fault injection, the driver half (xhci_inj.h has the
 * pure half, the trigger's encoding and the reserved codes). Design record
 * 17 section 5.
 *
 * QEMU flavour only: src/sources defines XHCI_FLAVOUR_QEMU for that build
 * alone, and the release image carries none of this file, its
 * value name or its hooks (scripts\check-flavour-marker.ps1 names the
 * flavour; the "qemu.inj" strings are absent from the shipping image).
 * It adds no import: the registry read is hcd_ctl.c's, the rest is the
 * spin lock, interlocked and register calls every flavour already makes.
 *
 * The faults built here are the ones whose driver behaviour is in the tree:
 *
 *   LOST_IRQ      the ISR acknowledges as ever and does not queue the drain,
 *                 for `arg` interrupts (255: until CLEAR); 35-T.1's backstop
 *                 is what delivers the event that waits in the ring
 *   PED           a real write of PED 1 to a USB 2.0 root port, PEC answered
 *                 set in its PORTSC reads until the driver acknowledges it
 *                 and PED answered clear - QEMU ignores the write - until
 *                 the driver resets the port or the device leaves, and a
 *                 Port Status Change Event for the port handed to the
 *                 drain's own handler (35-T.5)
 *   OC            the port's PORTSC reads answer PP clear and OCA and OCC set;
 *   OC_RELEASE    OCA answered clear from then on, and the driver's PP write
 *                 ends the emulation (35-T.5)
 *   HCH           a real write clearing Run/Stop (35-T.6)
 *   DEAD          the health poll's USBSTS read (every XhciTolerance value,
 *                 2.1.1.0's handling at 0) and the containment step's (at 1)
 *                 answer all-ones for `arg` passes (0: until CLEAR), the Bus
 *                 Master Enable proof real (35-T.6)
 *   DEAD_NOPROOF  the same, the proof's read-back answering set
 *   CLEAR         every answer and the lost-interrupt window off, and no
 *                 further endpoint fault: one in flight runs to its end
 *
 * The endpoint faults (35-T.2 to 35-T.4), `arg` injections each (0 one, 255
 * until CLEAR), on an interrupt-IN TD the controller is NAKing - record 17
 * section 5's target, a QEMU HID tablet or mouse with no input - of a
 * published device at root port bits 15:8 (0 any):
 *
 *   TRANSACTION   a real Stop Endpoint, its Stopped event swallowed, the
 *                 dequeue confirmed to be the TD's first TRB (put back with
 *                 a real Set TR Dequeue when QEMU fetched it ahead with
 *                 nothing moved; abandoned and counted otherwise), then a
 *                 Transaction Error for the TD; context reads answer Halted
 *                 and doorbells are dropped until the driver's Reset
 *                 Endpoint, answered Success without being sent - the real
 *                 endpoint is Stopped already - and the driver's doorbell
 *                 restarts the TD for real (35-T.2)
 *   RESET_EP_FAIL the same, the Reset Endpoint answered Context State Error:
 *                 hcdCfgFault's real path, which for a device still present
 *                 requests the controller recovery, charged to the recovery
 *                 window - persistent, its terminal, the controller latched
 *                 failed
 *   RACE_RING     the same, the Reset Endpoint held while the layer rings
 *                 the endpoint, as a submission would, resuming it - only
 *                 the soft retry's own reset of the TD it still holds; any
 *                 other reset is answered plainly, nothing rung
 *   RACE_SECOND   the same, then another real stop, the dequeue confirmed
 *                 again, Halted answered again and a second Transaction
 *                 Error delivered, all before the held Reset completes
 *   REFUSED_CODE  Bandwidth Overrun for the TD, no stop (35-T.3)
 *   EP_NOT_ENABLED code 12, pointer and length 0
 *   HALT_HALTED   a real stop as above, then a Stall with pointer 0; reads
 *                 answer Halted (35-T.4)
 *   HALT_ERROR    a Stall naming a TRB off the ring; reads answer Error
 *   HALT_STALE    a Stall with pointer 0 and no answer: the endpoint reads
 *                 Stopped, and once the driver has read it the layer rings
 *                 it
 *   EP0_THREAD    at the thread's next EP0 doorbell for a published device,
 *                 the doorbell withheld - the transfer waits as on a device
 *                 that never answers - and a refused code delivered for it
 *   EP0_PREPDO    the same for a device whose PDO does not exist yet
 *
 * Task 50.7's ENUM_DESC (20h): the next `arg` 8-byte device-descriptor
 * reads of an enumeration at a root port answered with bMaxPacketSize0 0
 * (HcdInjEnumDesc), so the port fails its enumeration for good.
 *
 * Task 36.22's bulk faults (BULK_TIMEOUT, _STOP, _ERROR, _SHORT): the
 * doorbells of one bulk endpoint (`arg` its address, 0 the first bulk IN)
 * of a published device withheld, so the next TD sits unstarted - QEMU
 * finishes a bulk TD inside the doorbell write - and then nothing (the
 * driver's own stop ends it), a real stop the driver sees, TRANSACTION's
 * path on it, or a stop, a Set TR Dequeue past it and a Short Packet on
 * its last TRB (hcdInjArmBulk, hcdInjBulkFire).
 *
 * A port the guest names in bits 15:8 must be a managed USB 2.0 root port
 * with a device enabled on it for PED and OC, and the root port a device is
 * under for the endpoint faults; 0 takes the first such. A command that
 * cannot be carried out is refused with a note, and its sequence is spent.
 *
 * IRQL: each function says; the thread's functions run at PASSIVE_LEVEL
 * with no lock held. InjLock is a leaf, taken at <= DISPATCH_LEVEL.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_dbg.h"

#if defined(XHCI_FLAVOUR_QEMU)

#define HCD_VALUE_QEMU_INJECT L"XhciQemuInject"
/* Task 36.4: milliseconds after a start for which the ISR's drains and the
 * backstop's are held back, so the No Op self-test's completion stands
 * undrained and the command engine busy into the first enumeration, as the
 * E7250's lost interrupt left it (issue 12, the boot-time no-slot
 * refusal), on a line that
 * works. Read at each start, held to HCD_QEMU_START_HOLD_MAX: a nominal
 * margin inside the No Op's own watchdog (XHCI_COMMAND_TIMEOUT_MS, 5000),
 * not a guarantee - the watchdog runs from the No Op's submit, the hold
 * from a later point of the start, and it ends at a thread pass - which
 * the leg confirms; absent or 0 holds nothing. */
#define HCD_VALUE_QEMU_START_HOLD L"XhciQemuStartHold"
#define HCD_QEMU_START_HOLD_MAX   3000UL

/* The trigger's first admission, ten tolerance ticks (about a second)
 * after the start's latch, the gate the controller thread's own periodic
 * read had (task 50.38). */
#define HCD_INJ_READ_TICKS 10UL

/* Task 50.38: the trigger reader's period, from one read's start to the
 * next, about a second as the controller thread's own read was, and its
 * period while a rendezvous holds, the hold's own 100 ms turn. */
#define HCD_INJ_POLL_MS      1000UL
#define HCD_INJ_POLL_FAST_MS 100UL

/* An endpoint fault's phase (InjPhase). */
#define HCD_INJ_PHASE_IDLE      0UL  /* ready to inject while one is left  */
#define HCD_INJ_PHASE_ANSWERING 1UL  /* until the answers end              */
#define HCD_INJ_PHASE_STALE     2UL  /* until the stale halt is read       */
#define HCD_INJ_PHASE_GONE      3UL  /* until the device leaves            */
#define HCD_INJ_PHASE_BULK_WAIT 4UL  /* 36.22: a bulk doorbell withheld,
                                      * until a TD sits behind it          */
#define HCD_INJ_PHASE_BULK_HELD 5UL  /* BULK_TIMEOUT: until the driver's
                                      * own stop ends the withholding      */
#define HCD_INJ_PHASE_BULK_CLEAR 6UL /* CLEAR met a withholding: the next
                                      * pass gives the doorbells back      */
#define HCD_INJ_PHASE_BULK_LOSE 7UL  /* BULK_TIMEOUT: until a TD's
                                      * completion is lost                 */

/* How long the thread waits for the drain to take an injected event, and
 * for the driver's answer to a stale halt or a refused code (ticks, at
 * least 45 ms each: a few seconds) before the next injection. */
#define HCD_INJ_DELIVER_MS      1000UL
#define HCD_INJ_SETTLE_TICKS    50UL

/* Task 50.30 step 3 (record 17 section 4.15.16 E): fault 2D's exercise,
 * at the end of this file. */
static VOID hcdInjQsStart(PHCD_CONTROLLER hc);
static ULONG hcdInjQsArm(PHCD_CONTROLLER hc, ULONG value);

/* ----------------------------------------------------------------------- */
/* The hooks the kept files and hcd_ctl.c call                              */
/* ----------------------------------------------------------------------- */

/* IRQL: DIRQL (hcdIsr). Lock-free: the thread arms and disarms the window
 * (hcdInjIrqArm), the ISR counts only while it is armed, so a disarmed
 * window drops nothing however long it stays so. An interrupt claimed as
 * the thread re-arms may count against the new window - one fewer dropped,
 * a test aid's slack, never a drop with nothing armed. */
ULONG HcdInjIsrDrop(PHCD_CONTROLLER hc)
{
    ULONG taken;

    if (!hc->InjIrqArmed) {
        return 0;
    }
    taken = (ULONG)InterlockedIncrement((PLONG)&hc->InjIrqTaken);
    if (!XhciInjIrqDrop(1, (ULONG)hc->InjIrqForever, taken,
                        (ULONG)hc->InjIrqBudget)) {
        return 0;
    }
    (VOID)InterlockedIncrement((PLONG)&hc->InjIrqDropped);
    return 1;
}

/* Task 36.4: whether XhciQemuStartHold still holds this start's drains
 * back. Reads only. IRQL: any. */
ULONG HcdInjStartHeld(PHCD_CONTROLLER hc)
{
    ULONG end;

    end = (ULONG)hc->InjStartHoldTicks;
    return (end != 0 && HcdTolNow(hc) < end) ? 1UL : 0UL;
}

/* The hold has done its work once the bus's command path met the busy
 * engine: ended before that path's first drain, so the resubmitted
 * command's own completion interrupt is drained as ever. IRQL:
 * <= DISPATCH_LEVEL. */
VOID HcdInjStartRelease(PHCD_CONTROLLER hc)
{
    if (InterlockedExchange((PLONG)&hc->InjStartHoldTicks, 0) != 0) {
        XhciLogNote(&hc->Hc, "qemu.inj.starthold.released", HcdTolNow(hc));
    }
}

/* Task 49.29 (record 13 section 11.11.12): the staging hold's containment,
 * DEAD_NOPROOF until CLEAR as the trigger arms it, so the health poll's
 * next pass reads all ones and the containment finds no proof. Refused,
 * as hcdInjFire refuses any fault aimed at a failed controller, when the
 * controller has failed already - read without its lock, which a caller
 * holding StageLock may not take; a test aid's read, and the containment
 * then comes on its own. InjLock, a leaf, is all it takes, so a caller may
 * hold the controller lock and StageLock (no log note here: XhciLogNote
 * takes the controller lock). Returns 1 when armed. IRQL:
 * <= DISPATCH_LEVEL. */
ULONG HcdInjContainNoProof(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (hc->Hc.ControllerFailed) {
        XHCI_DBG_TEXT("qemu inject: the staging hold's DEAD_NOPROOF refused,"
                      " the controller has failed");
        return 0;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjArmDead(&hc->InjRegs, 0, 1);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XHCI_DBG_TEXT("qemu inject: the staging hold arms DEAD_NOPROOF");
    return 1;
}

/* IRQL: any. Above DISPATCH_LEVEL nothing is answered: InjLock cannot be
 * taken there, and no PORTSC read of this driver's runs there. */
ULONG HcdInjPortscRead(PXHCI_EXTENSION ext, ULONG port, ULONG value)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG answer;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL) {
        return value;
    }
    hc = HcdControllerFromExt(ext);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    answer = XhciInjPortscRead(&hc->InjRegs, port, value);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return answer;
}

/* The value the hardware is given. IRQL: any, as the read. */
ULONG HcdInjPortscWrite(PXHCI_EXTENSION ext, ULONG port, ULONG value)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG written;

    if (KeGetCurrentIrql() > DISPATCH_LEVEL) {
        return value;
    }
    hc = HcdControllerFromExt(ext);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    written = XhciInjPortscWrite(&hc->InjRegs, port, value);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return written;
}

/* The containment step's USBSTS read (hcdContain, under the controller
 * lock; XhciTolerance 1 only), answered from the pass's snapshot
 * (HcdInjPoll). IRQL: DISPATCH_LEVEL. */
ULONG HcdInjUsbsts(PHCD_CONTROLLER hc, ULONG usbsts)
{
    KIRQL oldIrql;
    ULONG answer;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    answer = XhciInjUsbsts(&hc->InjRegs, usbsts);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return answer;
}

/* The health poll's USBSTS read (XhciControllerHealthPoll, under the
 * controller lock), which 2.1.1.0's handling and XhciTolerance 0 rest on:
 * answered at every tolerance value from the pass's snapshot.
 * IRQL: DISPATCH_LEVEL. */
ULONG HcdInjHealthUsbsts(PXHCI_EXTENSION ext, ULONG usbsts)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG answer;

    hc = HcdControllerFromExt(ext);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    answer = XhciInjUsbsts(&hc->InjRegs, usbsts);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return answer;
}

/* ----------------------------------------------------------------------- */
/* The endpoint faults' hooks                                               */
/* ----------------------------------------------------------------------- */

/* The drain's look at each event off the ring (xhci_evt.c): the Stopped
 * event of the layer's own Stop Endpoint is the layer's, and the driver
 * never sees it. IRQL: DISPATCH_LEVEL, controller lock held. Watch is set
 * by the thread before its Stop goes out, so an unlocked look that misses
 * it is a look at an event that cannot be the watched one. */
static ULONG hcdInjBulkLoseEvent(PHCD_CONTROLLER hc, const XHCI_TRB *ev);
static VOID hcdInjQueueDrain(PHCD_CONTROLLER hc);

ULONG HcdInjEventSwallow(PXHCI_EXTENSION ext, const XHCI_TRB *ev)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG swallowed;

    hc = HcdControllerFromExt(ext);
    /* Task 50.30 step 2: the completion of the layer's own No Op is the
     * layer's, never the engine's; after outcome 4's completion the
     * Stopped event is owed at the next TRB. */
    if ((hc->InjHang.NoOpPA != 0 || hc->InjHang.StopOwedPA != 0) &&
        XHCI_TRB_GET_TYPE(ev->Control) == XHCI_TRB_TYPE_COMMAND_COMPLETION) {
        ULONG ate;
        ULONG owe;

        ate = 0;
        owe = 0;
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        if (hc->InjHang.NoOpPA != 0 &&
            (ev->Param0 & ~0xFUL) == hc->InjHang.NoOpPA) {
            hc->InjHang.NoOpPA = 0;
            ate = 1;
        } else if (hc->InjHang.StopOwedPA != 0 &&
                   (ev->Param0 & ~0xFUL) == hc->InjHang.StopOwedPA &&
                   hc->InjHangEvN == 0) {
            XhciInjCommandEvent(&hc->InjHangEv[0],
                                hc->InjHang.StopOwedPA + XHCI_TRB_BYTES,
                                XHCI_CC_COMMAND_RING_STOPPED);
            hc->InjHangEvN = 1;
            hc->InjHangEvAt = 0;
            hc->InjHang.StopOwedPA = 0;
            owe = 1;
        }
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        if (ate) {
            XhciLogNoteLocked(ext, "qemu.inj.noop.retired", 1);
            return 1;
        }
        if (owe) {
            hcdInjQueueDrain(hc);
        }
    }
    /* 36.22's BULK_TIMEOUT: a completion the driver never sees. */
    if (hc->InjBulkLose && hcdInjBulkLoseEvent(hc, ev)) {
        return 1;
    }
    if (!hc->InjEp.Watch) {
        return 0;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    swallowed = XhciInjEpSwallow(&hc->InjEp, ev);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (swallowed) {
        XhciLogNoteLocked(ext, "qemu.inj.stopped",
                          XHCI_TRB_GET_COMPLETION(ev->Status));
    }
    return swallowed;
}

/* The injected Transfer Event, taken by the drain at the top of its pass
 * and handed to the handler a ring event meets. IRQL: DISPATCH_LEVEL,
 * controller lock held. */
ULONG HcdInjEventTake(PXHCI_EXTENSION ext, XHCI_TRB *ev)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG taken;

    hc = HcdControllerFromExt(ext);
    if (!hc->InjEp.EvPending && hc->InjHangEvAt < hc->InjHangEvN) {
        /* Task 50.30 step 2: one of the outcome's events, a pass each. */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        taken = 0;
        if (hc->InjHangEvAt < hc->InjHangEvN && hc->InjHangEvAt < 2) {
            *ev = hc->InjHangEv[hc->InjHangEvAt];
            hc->InjHangEvAt++;
            if (hc->InjHangEvAt >= hc->InjHangEvN) {
                hc->InjHangEvN = 0;
                hc->InjHangEvAt = 0;
            }
            taken = 1;
        }
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        return taken;
    }
    if (!hc->InjEp.EvPending) {
        return 0;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    taken = hc->InjEp.EvPending;
    if (taken) {
        ev->Param0 = hc->InjEp.Ev.Param0;
        ev->Param1 = hc->InjEp.Ev.Param1;
        ev->Status = hc->InjEp.Ev.Status;
        ev->Control = hc->InjEp.Ev.Control;
        hc->InjEp.EvPending = 0;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return taken;
}

/* The injected event has been handled: a thread waiting on it goes on.
 * IRQL: DISPATCH_LEVEL. */
VOID HcdInjEventDone(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;

    hc = HcdControllerFromExt(ext);
    (VOID)KeSetEvent(&hc->InjEvDone, IO_NO_INCREMENT, FALSE);
    /* Task 50.30 step 2: the outcome's next event, at the next pass. */
    if (hc->InjHangEvN != 0) {
        hcdInjQueueDrain(hc);
    }
}

/* A doorbell for the answered endpoint is dropped: a halted endpoint
 * ignores it, and the real one, Stopped, would otherwise restart. IRQL:
 * any; above DISPATCH_LEVEL nothing is dropped (no doorbell of this driver
 * is rung there). */
ULONG HcdInjDoorbell(PXHCI_EXTENSION ext, ULONG slot, ULONG value)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG dropped;

    hc = HcdControllerFromExt(ext);
    if (!hc->InjEp.Block || KeGetCurrentIrql() > DISPATCH_LEVEL) {
        return 0;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    dropped = XhciInjEpDoorbell(&hc->InjEp, slot, value);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return dropped;
}

/* An Endpoint Context state read (hcd_enum.c, hcd_cfg.c, hcd_hub.c).
 * IRQL: <= DISPATCH_LEVEL, the controller lock held or not. */
ULONG HcdInjEpState(PHCD_CONTROLLER hc, ULONG slot, ULONG dci, ULONG state)
{
    KIRQL oldIrql;
    ULONG answer;

    if (hc->InjStopRaceReads != 0) {
        /* Issue 26: fault 1A's one Running read after its Stop. */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        if (hc->InjStopRaceReads != 0 && hc->InjStopRaceSlot == slot &&
            hc->InjStopRaceDci == dci) {
            hc->InjStopRaceReads = 0;
            KeReleaseSpinLock(&hc->InjLock, oldIrql);
            return XHCI_EP_STATE_RUNNING;
        }
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
    }
    if (hc->InjEp.Slot == 0) {
        return state;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    answer = XhciInjEpState(&hc->InjEp, slot, dci, state);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return answer;
}

/* 36.54: whether the layer withholds this endpoint's doorbells (its own
 * stop, a bulk fault's watch or an answered halt), which holds the
 * endpoint as a pause would for the QEMU stream resync (hcd_cfg.c,
 * HcdCfgQemuStreamResync; record 17 section 4.12.4). IRQL: PASSIVE_LEVEL,
 * no lock held. */
ULONG HcdInjWithholds(PHCD_CONTROLLER hc, ULONG slot, ULONG dci)
{
    KIRQL oldIrql;
    ULONG held;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    held = (hc->InjEp.Block && hc->InjEp.Slot != 0 &&
            hc->InjEp.Slot == slot && hc->InjEp.Dci == dci) ? 1UL : 0UL;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return held;
}

/* The drain queued through the ISR's admission, as the backstop queues it
 * (hcd_ctl.c): counted before the insertion, rolled back on a refusal,
 * nothing once the DPC is closed. IRQL: <= DISPATCH_LEVEL. */
static VOID hcdInjQueueDrain(PHCD_CONTROLLER hc)
{
    (VOID)InterlockedIncrement((PLONG)&hc->DpcsInFlight);
    if (hc->DpcClosed || !KeInsertQueueDpc(&hc->IsrDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement((PLONG)&hc->DpcsInFlight);
    }
}

/*
 * The thread's EP0 doorbell for its own control transfer (hcd_enum.c,
 * hcdThreadControlQuiet), rung here unless an armed EP0 fault fits the
 * device: then the doorbell is withheld - the controller never sees the TD,
 * as with a device that never answers - and a refused code for the TD is
 * delivered by the drain, whose cycle mark ends the thread's wait. The
 * cycle's Disable Slot takes the TD back. IRQL: DISPATCH_LEVEL, controller
 * lock held.
 */
VOID HcdInjEp0Doorbell(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG fire;
    ULONG first;

    fire = 0;
    if (hc->InjHang.Fault == XHCI_INJ_EP0_HANG ||
        hc->InjHang.Fault == XHCI_INJ_DEVICE_HANG) {
        /* Task 50.30 step 2: a device that never answers - no event, the
         * thread's deadline meets the silence. */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        fire = XhciInjHangEp0(&hc->InjHang, dev->Port, dev->Route);
        if (fire) {
            hc->InjHangEp0Slot = dev->SlotId;
            /* Step 3 (record 17 section 4.15.16 C): bit 7's latch, and
             * outcome 7's mark deadline. */
            XhciInjHangLatch(&hc->InjHang, dev->SlotId, HcdTolNow(hc));
        }
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        if (fire) {
            XhciLogNoteLocked(&hc->Hc, "qemu.inj.hang",
                              (hc->InjHang.Fault << 24) | (dev->Port << 16) |
                                  (dev->SlotId << 8));
            return;
        }
    }
    if (hc->InjEp.Ep0Want != 0) {
        first = dev->Ep0Xfer.FirstIndex;
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        fire = XhciInjEp0Fires(&hc->InjEp, dev->Port, dev->Pdo != NULL);
        if (fire) {
            XhciInjTransferEvent(
                &hc->InjEp.Ev, XhciRingTrbPA(&dev->Ep0, first),
                XHCI_TRB_GET_LENGTH(dev->Ep0.Base[first].Status),
                XHCI_INJ_REFUSED_CC, dev->SlotId, 1);
            hc->InjEp.EvPending = 1;
        }
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
    }
    if (!fire) {
        XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);
        return;
    }
    hcdInjQueueDrain(hc);
    XHCI_DBG_VALUE("qemu inject: EP0 doorbell withheld, slot", dev->SlotId);
    XhciLogNoteLocked(&hc->Hc, "qemu.inj.ep0", dev->SlotId);
}

/* The containment proof's read-back (hcdContainProve). IRQL: PASSIVE_LEVEL. */
USHORT HcdInjPciCommand(PHCD_CONTROLLER hc, USHORT command)
{
    KIRQL oldIrql;
    ULONG answer;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    answer = XhciInjPciCommand(&hc->InjRegs, command);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return (USHORT)answer;
}

/* ----------------------------------------------------------------------- */
/* The thread's half                                                        */
/* ----------------------------------------------------------------------- */

/* The lost-interrupt window: disarmed first, so the ISR stops counting
 * before the count is reset under it; armed last. `budget` 0 leaves it
 * disarmed. IRQL: PASSIVE_LEVEL (the thread). */
static VOID hcdInjIrqArm(PHCD_CONTROLLER hc, ULONG budget, ULONG forever)
{
    (VOID)InterlockedExchange((PLONG)&hc->InjIrqArmed, 0);
    (VOID)InterlockedExchange((PLONG)&hc->InjIrqTaken, 0);
    (VOID)InterlockedExchange((PLONG)&hc->InjIrqBudget, (LONG)budget);
    (VOID)InterlockedExchange((PLONG)&hc->InjIrqForever, forever ? 1 : 0);
    if (budget != 0 || forever) {
        (VOID)InterlockedExchange((PLONG)&hc->InjIrqArmed, 1);
    }
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdInjClear(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjRegsClear(&hc->InjRegs);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    hcdInjIrqArm(hc, 0, 0);
    hc->InjClearHalt = 0;
    hc->InjSetInterface = 0;
    hc->InjSelectConfig = 0;
    hc->InjRecycleCfg = 0;      /* task 50.20: faults 34 and 35 */
    hc->InjRecycleDci = 0;
    hc->InjRecycleSkip = 0;
    hc->InjRvPoint = 0;
    hc->InjRvSeq = 0;
    hc->InjEnumDesc = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjStopRace = 0;
    hc->InjStopRaceReads = 0;
    hc->InjResetArmed = 0;
    hc->InjResetHolding = 0;
    hc->InjStaleDeq = 0;
    hc->InjStaleSlot = 0;
    hc->InjRestartRace = 0;
    hc->InjRepairRace = 0;
    XhciInjHangEnd(&hc->InjHang);       /* task 50.30: faults 24 to 2B */
    hc->InjHangEvN = 0;
    hc->InjHangEvAt = 0;
    hc->InjHangEp0Slot = 0;
    /* Task 50.31: faults 30 to 35. */
    hc->InjRetryFail = 0;
    hc->InjRetryOnes = 0;
    hc->InjRetryPortsc = 0;
    hc->InjRetryPortscPort = 0;
    hc->InjRetryHoldTicks = 0;
    hc->InjRetryHolding = 0;
    hc->InjRecoverEarly = 0;
    hc->InjSweepBreak = 0;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
}

/*
 * Task 50.31 (design record 38 section 17): faults 30 to 35, the retry's
 * hooks. Each is a count or an arm under InjLock, a leaf taken inside the
 * controller lock where the caller holds it (the commit's two hooks); none
 * takes another lock while it holds InjLock. The arming, from the
 * injection poll: `arg` 0 is one, 255 until CLEAR where a count is
 * unbounded. IRQL: PASSIVE_LEVEL, the controller thread's injection poll;
 * no lock held on entry.
 */
static VOID hcdInjRetryArm(PHCD_CONTROLLER hc, ULONG fault, ULONG port,
                           ULONG arg)
{
    KIRQL oldIrql;
    ULONG count;

    count = (arg == 0) ? 1UL
            : (arg == XHCI_INJ_ARG_FOREVER) ? 0xFFFFFFFFUL
                                            : arg;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    switch (fault) {
    case XHCI_INJ_RETRY_FAIL:
        hc->InjRetryFail = count;
        break;
    case XHCI_INJ_RETRY_ONES:
        hc->InjRetryOnes = count;
        break;
    case XHCI_INJ_RETRY_PORTSC_ONES:
        hc->InjRetryPortsc = 1;
        hc->InjRetryPortscPort = port;
        break;
    case XHCI_INJ_RETRY_HOLD:
        hc->InjRetryHoldTicks = (arg == 0) ? XHCI_INJ_RETRY_HOLD_TICKS : arg;
        hc->InjRetryHolding = 0;
        break;
    case XHCI_INJ_RECOVER_EARLY:
        hc->InjRecoverEarly = count;
        break;
    default:
        hc->InjSweepBreak = count;
        break;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciLogNote(&hc->Hc, "qemu.inj.retry.armed", (fault << 16) | arg);
}

/* 30: 1 while runs are left, an attempt about to begin refused (hcdRecover,
 * before XhciRecoverController). IRQL: <= DISPATCH_LEVEL. */
ULONG HcdInjRetryFailNow(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG refuse;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    refuse = (hc->InjRetryFail != 0) ? 1UL : 0UL;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return refuse;
}

/* 30: a run of failures spent (terminal 2): one run used. IRQL: <=
 * DISPATCH_LEVEL. */
VOID HcdInjRetryRunEnd(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjRetryFail != 0 && hc->InjRetryFail != 0xFFFFFFFFUL) {
        hc->InjRetryFail--;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
}

/* 31: the commit's USBSTS read. Controller lock held; IRQL
 * DISPATCH_LEVEL. */
ULONG HcdInjRetryOnes(PHCD_CONTROLLER hc, ULONG usbsts)
{
    KIRQL oldIrql;
    ULONG answer;

    answer = usbsts;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjRetryOnes != 0) {
        if (hc->InjRetryOnes != 0xFFFFFFFFUL) {
            hc->InjRetryOnes--;
        }
        answer = 0xFFFFFFFFUL;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (answer != usbsts) {
        XhciLogNoteLocked(&hc->Hc, "qemu.inj.retry.ones", usbsts);
    }
    return answer;
}

/* 32: a sweep's PORTSC read. Controller lock held; IRQL DISPATCH_LEVEL. */
ULONG HcdInjRetryPortsc(PHCD_CONTROLLER hc, ULONG port, ULONG portsc)
{
    KIRQL oldIrql;
    ULONG answer;

    answer = portsc;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjRetryPortsc &&
        (hc->InjRetryPortscPort == 0 || hc->InjRetryPortscPort == port)) {
        answer = 0xFFFFFFFFUL;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return answer;
}

/* 33: 1 while the next armed try's commit is held back, the hold begun at
 * the first look and lasting its ticks on the tolerance clock. Controller
 * lock held; IRQL DISPATCH_LEVEL. */
ULONG HcdInjRetryHeld(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG now;
    ULONG held;
    ULONG began;

    now = HcdTolNow(hc);
    held = 0;
    began = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjRetryHoldTicks != 0) {
        if (!hc->InjRetryHolding) {
            hc->InjRetryHolding = 1;
            hc->InjRetryHoldStart = now;
            began = 1;
        }
        if (XhciTolElapsed(now, hc->InjRetryHoldStart,
                           hc->InjRetryHoldTicks)) {
            hc->InjRetryHoldTicks = 0;
            hc->InjRetryHolding = 0;
        } else {
            held = 1;
        }
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (began) {
        XhciLogNoteLocked(&hc->Hc, "qemu.inj.retry.hold", now);
    }
    return held;
}

/* 34: whether this attempt's capability decode answers all ones, taken by
 * hcdRecover for each attempt's first call. IRQL: PASSIVE_LEVEL, the
 * controller thread; no lock held. */
ULONG HcdInjRecoverEarlyTake(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG take;

    take = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjRecoverEarly != 0) {
        if (hc->InjRecoverEarly != 0xFFFFFFFFUL) {
            hc->InjRecoverEarly--;
        }
        take = 1;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (take) {
        XhciLogNote(&hc->Hc, "qemu.inj.retry.early", 1);
    }
    return take;
}

/* 34: the decode's first read (xhciInitDecode), all ones while the attempt
 * hcdRecover marked runs. IRQL: any. */
ULONG HcdInjDecodeAnswer(PXHCI_EXTENSION ext, ULONG value)
{
    return HcdControllerFromExt(ext)->InjDecodeOnes ? 0xFFFFFFFFUL : value;
}

/* 35: between a sweep's first and second port holds, the leave generation
 * advanced in a hold of its own, nothing else written. IRQL:
 * PASSIVE_LEVEL, the controller thread; neither lock held on entry. */
VOID HcdInjSweepBreak(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG take;

    take = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjSweepBreak != 0) {
        if (hc->InjSweepBreak != 0xFFFFFFFFUL) {
            hc->InjSweepBreak--;
        }
        take = 1;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (!take) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Hc.Retry.LeaveGen++;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XhciLogNote(&hc->Hc, "qemu.inj.retry.sweep", 1);
}

/*
 * Task 49.35: fault 1B, the slow reset (xhci_inj.h). The HCRST write in
 * xhciReset starts a hold when the fault is armed; while it runs, the reads
 * the reset waits make answer HCRST (or CNR) set. Time is the system
 * time's low part, good for holds under seven minutes; a hold for ever
 * never ends. IRQL: any (KeQuerySystemTime and InjLock are).
 */
/*
 * Task 50.30 step 2 (record 17 section 4.15.9): the command ring given up
 * or rebuilt - every invalidation (xhciCommandInvalidateLocked), the
 * start's and a recovery's ring setup (xhciProgramCommandRing), the
 * restore (xhciRestoreState) and the HCRST (HcdInjResetWritten). The
 * layer's addresses on the ring and the outcome's events not yet delivered
 * go with it (XhciInjHangRingGone), so its No Op marker never meets a
 * later command at the same address and no queued Command Ring Stopped or
 * Command Aborted reaches a new ring generation; a 0x26 armed by a
 * device's behaviour stays armed. hcd_svc.h. IRQL: <= DISPATCH_LEVEL, the
 * controller lock held or not.
 */
VOID HcdInjCmdRingReset(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    hc = HcdControllerFromExt(ext);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjHangRingGone(&hc->InjHang);
    hc->InjHangEvN = 0;
    hc->InjHangEvAt = 0;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
}

VOID HcdInjResetWritten(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER now;
    KIRQL oldIrql;
    ULONG hold;

    hc = HcdControllerFromExt(ext);
    if (hc->InjHang.Fault != 0 || hc->InjHang.NoOpPA != 0) {
        /* Task 50.30 step 2: the ring is rebuilt; a 0x26 armed by a
         * device's behaviour stays armed across the recovery. */
        HcdInjCmdRingReset(ext);
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjHangEp0Slot = 0;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
    }
    if (!hc->InjResetArmed) {
        return;
    }
    KeQuerySystemTime(&now);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hold = 0;
    if (hc->InjResetArmed) {
        hc->InjResetHolding = 1;
        hc->InjResetStamp = now.LowPart;
        hold = hc->InjResetHoldMs;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (hold != 0) {
        XHCI_DBG_VALUE("qemu inject: HCRST held, ms", hold);
    }
}

ULONG HcdInjResetAnswer(PXHCI_EXTENSION ext, ULONG opOffset, ULONG value)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER now;
    KIRQL oldIrql;
    ULONG held;
    ULONG cnr;

    hc = HcdControllerFromExt(ext);
    if (!hc->InjResetHolding) {
        return value;
    }
    KeQuerySystemTime(&now);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    held = 0;
    cnr = hc->InjResetCnr;
    if (hc->InjResetHolding) {
        held = XhciInjResetHeld(hc->InjResetHoldMs,
                                (now.LowPart - hc->InjResetStamp) / 10000UL);
        if (!held) {
            hc->InjResetHolding = 0;
        }
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return XhciInjResetAnswer(opOffset, value, held, cnr);
}

/*
 * Task 36.12: fault 13. The next CLEAR_FEATURE(ENDPOINT_HALT) the thread
 * sends a device (hcd_cfg.c, hcdCfgDeviceClearHalt: a RESET_PIPE, a
 * SYNC_CLEAR_STALL, a streams open or close) is answered with the armed
 * outcome and never sent, once; the device's halt and toggle stay as they
 * were, which is what a refused clear leaves. Returns 1 with *outcome
 * (HCD_CTL_*) when it answered. IRQL: PASSIVE_LEVEL, the thread.
 */
ULONG HcdInjClearHalt(PHCD_CONTROLLER hc, ULONG slot, ULONG dci,
                      PULONG outcome)
{
    if (hc->InjClearHalt == 0) {
        return 0;
    }
    *outcome = hc->InjClearHalt - 1;
    hc->InjClearHalt = 0;
    XHCI_DBG_VALUE("qemu inject: device clear answered, slot/dci/outcome",
                   (slot << 24) | (dci << 16) | *outcome);
    XhciLogNote(&hc->Hc, "qemu.inj.clearhalt",
                (slot << 24) | (dci << 16) | *outcome);
    return 1;
}

/*
 * Task 38.10: fault 18. The next SET_INTERFACE a SELECT_INTERFACE sends
 * (hcd_cfg.c, hcdCfgSetInterfaceOne) is answered and never sent, once: a
 * STALL, or not established - the device left at its setting either way,
 * which is what a refused request leaves. QEMU's devices answer every
 * SET_INTERFACE their descriptors allow, so this is the only way a guest
 * reaches design record 20 section 5.3's step 4 branches. Returns 1 with
 * *stalled when it answered. IRQL: PASSIVE_LEVEL, the thread.
 */
ULONG HcdInjSetInterface(PHCD_CONTROLLER hc, ULONG slot, PULONG stalled)
{
    if (hc->InjSetInterface == 0) {
        return 0;
    }
    *stalled = (hc->InjSetInterface == 1) ? 1UL : 0UL;
    hc->InjSetInterface = 0;
    XHCI_DBG_VALUE("qemu inject: SET_INTERFACE answered, slot/stalled",
                   (slot << 24) | *stalled);
    XhciLogNote(&hc->Hc, "qemu.inj.setif", (slot << 24) | *stalled);
    return 1;
}

/*
 * Task 50.7: fault 20. 1 when the 8-byte device-descriptor read an
 * enumeration at root port `port` just made is to be answered with
 * bMaxPacketSize0 0 (hcd_enum.c, hcdPerform's GET_DEVICE), one read of the
 * armed count spent. QEMU's devices answer every descriptor read, so this
 * is the one way a guest reaches a root port whose enumeration failed for
 * good with no cycle, recovery or hold. IRQL: PASSIVE_LEVEL, the thread.
 */
ULONG HcdInjEnumDesc(PHCD_CONTROLLER hc, ULONG port)
{
    if (hc->InjEnumDesc == 0 ||
        (hc->InjEnumDescPort != 0 && hc->InjEnumDescPort != port)) {
        return 0;
    }
    if (hc->InjEnumDesc != 0xFFFFFFFFUL) {
        hc->InjEnumDesc--;
    }
    XHCI_DBG_VALUE("qemu inject: device descriptor's bMaxPacketSize0 0, "
                   "port", port);
    XhciLogNote(&hc->Hc, "qemu.inj.enumdesc", port);
    return 1;
}

/*
 * Task 38.10: fault 19. The next Configure Endpoint of an interface switch
 * - step 3's (`rollback` 0; a function's unconfigure or release shares
 * it) or the rollback's (1), as armed - is answered
 * and never issued, once: refused with Resource Error, so the controller
 * changed nothing, as xHCI 4.6.6 has a refusal leave it; or with no
 * completion (code 0) and the controller reset requested, as hcdCommand
 * answers a command that never completes - the HCRST then the proof that
 * releases what the switch held. Returns 1 with *code when it answered.
 * IRQL: PASSIVE_LEVEL, the thread.
 */
/* Task 50.20 (record 17 section 4.12.10), fault 34: RESET_PIPE's recycle
 * Configure answered and not issued, at the selected attempt and DCI.
 * Thread only. */
ULONG HcdInjRecycleConfigure(PHCD_CONTROLLER hc, ULONG slot, ULONG dci,
                             PULONG code)
{
    ULONG arg;

    if (hc->InjRecycleCfg == 0 ||
        (hc->InjRecycleDci != 0 && hc->InjRecycleDci != dci)) {
        return 0;
    }
    if (hc->InjRecycleSkip != 0) {
        hc->InjRecycleSkip--;
        return 0;
    }
    arg = hc->InjRecycleCfg - 1;
    hc->InjRecycleCfg = 0;
    if ((arg & XHCI_INJ_RC_NOCOMPLETE) != 0) {
        *code = 0;
        HcdSvcRequestResetWhy(&hc->Hc, XHCI_WHY_INJECTED, slot, 0,
                              XHCI_INJ_RECYCLE_CFG);
    } else {
        *code = XHCI_CC_RESOURCE_ERROR;
    }
    XhciLogNote(&hc->Hc, "qemu.inj.recycle",
                (slot << 24) | ((dci & 0xFFUL) << 16) | ((arg & 0xFUL) << 8) |
                    (*code & 0xFFUL));
    return 1;
}

/* Task 50.38: the oldest reading in the trigger reader's queue taken, if
 * there is one, into *value. Returns whether one was taken. IRQL:
 * PASSIVE_LEVEL, the controller thread. */
static ULONG hcdInjQueueTake(PHCD_CONTROLLER hc, PULONG value)
{
    KIRQL oldIrql;
    ULONG taken;

    taken = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjPollCount != 0) {
        *value = hc->InjPollQ[hc->InjPollHead];
        hc->InjPollHead = (hc->InjPollHead + 1) % HCD_INJ_POLL_DEPTH;
        hc->InjPollCount--;
        taken = 1;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return taken;
}

/* Task 50.38: the release of the rendezvous armed with sequence `seq` (23
 * with arg 0 and another sequence) sought in the reader's queue, oldest
 * first; found, it and every reading before it are removed - those passed
 * over, as this loop's own reads passed over the values before the
 * release - and it is returned in *value. Returns whether it was found.
 * IRQL: PASSIVE_LEVEL, the controller thread. */
static ULONG hcdInjQueueRelease(PHCD_CONTROLLER hc, ULONG seq, PULONG value)
{
    KIRQL oldIrql;
    ULONG found;
    ULONG v;
    ULONG i;

    found = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    for (i = 0; i < hc->InjPollCount; i++) {
        v = hc->InjPollQ[(hc->InjPollHead + i) % HCD_INJ_POLL_DEPTH];
        if (XHCI_INJ_GET_SEQ(v) != seq &&
            XHCI_INJ_GET_FAULT(v) == XHCI_INJ_RENDEZVOUS &&
            XHCI_INJ_GET_ARG(v) == 0) {
            hc->InjPollHead = (hc->InjPollHead + i + 1) % HCD_INJ_POLL_DEPTH;
            hc->InjPollCount -= i + 1;
            *value = v;
            found = 1;
            break;
        }
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return found;
}

/* Task 50.20, fault 35: hold at `point` while it is armed, noting that it
 * was reached, until the release (23 again, arg 0) or 600 turns of 100 ms.
 * The thread looks for the release in the trigger reader's queue at each
 * turn, since its own pass cannot run while it holds, and the reader reads
 * every 100 ms meanwhile (task 50.38; until then this loop read the
 * machine-wide key itself, and each turn also held that read's time).
 * Readings that are not the release stay queued for the pass after a hold
 * that ends without one. PASSIVE_LEVEL, the thread, no lock held. */
VOID HcdInjRendezvous(PHCD_CONTROLLER hc, ULONG point)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG value;
    ULONG standing;
    ULONG latched;
    ULONG i;

    if (hc->InjRvPoint == 0 || hc->InjRvPoint != point) {
        return;
    }
    if (point == XHCI_INJ_RV_RECOVERY) {
        /* Record 17 section 4.15.16 C (revision 34): only a route raised
         * from here on is latched for this hold. */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hc->InjRvLeave = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    hc->InjRvReached++;
    XhciLogNote(&hc->Hc, "qemu.inj.rv.reached", (point << 16) | hc->InjRvReached);
    /* At once on the debugcon line too: the ring is not flushed while the
     * thread holds here, and a leg times its next step on this line. */
    XHCI_DBG_VALUE("qemu inject: rendezvous reached, point/count",
                   (point << 16) | hc->InjRvReached);
    (VOID)InterlockedExchange((PLONG)&hc->InjPollFast, 1);
    (VOID)KeSetEvent(&hc->InjPollKick, IO_NO_INCREMENT, FALSE);
    for (i = 0; i < 600; i++) {
        /* Task 50.30 step 3 (record 17 section 4.15.16 C): the recovery's
         * entry lets the thread go at once when a route's bit is raised -
         * a power transition, a removal or a stop - so the recovery meets
         * it; since revision 34 also when one was raised and lowered since
         * the last poll (HcdEnumLeaveRaise's latch). */
        if (point == XHCI_INJ_RV_RECOVERY) {
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            standing = hc->CmdWaitLeave;
            latched = hc->InjRvLeave;
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            if (XhciInjRvRouteLets(point, standing, latched)) {
                XhciLogNote(&hc->Hc, "qemu.inj.rv.route", standing | latched);
                break;
            }
        }
        /* A release written before this first read counts as well: the
         * baseline is the arming command's sequence, not this read's. */
        if (hcdInjQueueRelease(hc, hc->InjRvSeq, &value)) {
            /* Consumed here, so the pass does not fire it again. */
            (VOID)XhciInjTake(&hc->InjTrigger, 1, value);
            break;
        }
        due.QuadPart = -1000000;               /* 100 ms */
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    (VOID)InterlockedExchange((PLONG)&hc->InjPollFast, 0);
    hc->InjRvPoint = 0;
    XhciLogNote(&hc->Hc, "qemu.inj.rv.released", (point << 16) | i);
}

ULONG HcdInjSelectConfigure(PHCD_CONTROLLER hc, ULONG rollback, PULONG code)
{
    ULONG arg;

    if (hc->InjSelectConfig == 0) {
        return 0;
    }
    arg = hc->InjSelectConfig - 1;
    if ((arg & 1UL) != (rollback ? 1UL : 0UL)) {
        return 0;
    }
    hc->InjSelectConfig = 0;
    if (arg >= 2) {
        *code = 0;
        HcdSvcRequestResetWhy(&hc->Hc, XHCI_WHY_INJECTED, 0, 0,
                              XHCI_INJ_SELECT_CONFIG);
    } else {
        *code = XHCI_CC_RESOURCE_ERROR;
    }
    XHCI_DBG_VALUE("qemu inject: Configure Endpoint answered, rollback/code",
                   (rollback << 8) | *code);
    XhciLogNote(&hc->Hc, "qemu.inj.selcfg", (rollback << 8) | *code);
    return 1;
}

/*
 * Task 49.39, fault 1D's target: the cancel about to send its stream's Set
 * TR Dequeue names the stream and its taken-off head TD's first TRB
 * (`firstIndex` on the pipe's ring), or with dev NULL ends the window. Only
 * while 1B has sends left. IRQL: PASSIVE_LEVEL (the thread).
 */
VOID HcdInjStaleArm(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, PHCD_PIPE pipe,
                    ULONG firstIndex)
{
    KIRQL oldIrql;
    KIRQL ringIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (dev == NULL || pipe == NULL || hc->InjStaleDeq == 0) {
        hc->InjStaleSlot = 0;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        return;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciControllerLockAcquire(&hc->Hc, &ringIrql);
    hc->InjStalePa = XhciRingTrbPA(pipe->Ring, firstIndex);
    hc->InjStaleDcs = pipe->Ring->Base[firstIndex].Control & XHCI_TRB_CYCLE;
    XhciControllerLockRelease(&hc->Hc, ringIrql);
    hc->InjStaleSlot = dev->SlotId;
    hc->InjStaleDci = pipe->Dci;
    hc->InjStaleStream = pipe->StreamId;
}

/*
 * Task 49.39, fault 1E: before a stream's Set TR Dequeue, a doorbell for
 * another stream of the endpoint with a TD queued, so QEMU may run the
 * endpoint between the driver's read of it and the command. IRQL:
 * PASSIVE_LEVEL (the thread), controller lock not held.
 */
static VOID hcdInjRaceRing(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe, PULONG left, const char *note,
                           ULONG anyStream)
{
    PHCD_PIPE ep;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG id;
    ULONG rung;

    if (*left == 0) {
        return;
    }
    ep = (pipe->Parent != NULL) ? pipe->Parent : pipe;
    rung = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (id = 1; ep->Streams != NULL && id <= ep->Streams->Count; id++) {
        p = ep->Streams->Pipe[id];
        if (p != NULL && p != pipe && p->Queue->Count != 0 && !dev->Gone &&
            !HCD_DEV_FROZEN(dev)) {
            XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                              XhciStreamDoorbell(p->Dci, p->StreamId));
            rung = p->StreamId;
            break;
        }
    }
    /* Fault 1F: with no other stream holding a TD, another stream is rung
     * all the same - its ring empty, so nothing runs on it, but the doorbell
     * takes the endpoint Running (xHCI 4.6.10), which is the window this
     * fault is for. */
    for (id = 1; anyStream && rung == 0 && ep->Streams != NULL &&
                 id <= ep->Streams->Count && !dev->Gone &&
                 !HCD_DEV_FROZEN(dev); id++) {
        p = ep->Streams->Pipe[id];
        if (p != NULL && p != pipe) {
            XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                              XhciStreamDoorbell(p->Dci, p->StreamId));
            rung = p->StreamId;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (rung == 0) {
        return;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (*left != 0) {
        (*left)--;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciLogNote(&hc->Hc, note,
                (dev->SlotId << 16) | (pipe->Dci << 8) | (rung & 0xFFUL));
}

VOID HcdInjRestartRace(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       PHCD_PIPE pipe)
{
    hcdInjRaceRing(hc, dev, pipe, &hc->InjRestartRace, "qemu.inj.restart", 0);
}

/*
 * Task 49.39, fault 1F: the same doorbell, rung where c1 is about to
 * resend after a difference (record 17 4.12.8), so the endpoint may run
 * between the difference and the resend. IRQL: PASSIVE_LEVEL (the thread),
 * controller lock not held.
 */
VOID HcdInjRepairRace(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                      PHCD_PIPE pipe)
{
    hcdInjRaceRing(hc, dev, pipe, &hc->InjRepairRace, "qemu.inj.repairrace",
                   1);
}

/*
 * Every start begins with nothing injected, and the value a script left in
 * the key is latched, not fired. IRQL: PASSIVE_LEVEL (hcdStartBody, before
 * the interrupt is connected and the thread runs).
 */
VOID HcdInjStart(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG value;
    ULONG found;

    hcdInjClear(hc);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEpClear(&hc->InjEp);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    hcdInjQsStart(hc);
    hc->InjFault = 0;
    hc->InjLeft = 0;
    hc->InjPort = 0;
    hc->InjPhase = HCD_INJ_PHASE_IDLE;
    hc->InjSince = 0;
    hc->InjWaitNoted = 0;
    hc->InjDev = NULL;
    hc->InjSlot = 0;
    hc->InjDci = 0;
    hc->InjOwnCmd = 0;
    hc->InjIrqNoted = hc->InjIrqDropped;
    value = 0;
    found = NT_SUCCESS(HcdCtlQemuReadDword(hc, HCD_VALUE_QEMU_INJECT, &value))
                ? 1UL
                : 0UL;
    XhciInjTriggerStart(&hc->InjTrigger, found, value);
    hc->InjReadAt = HcdTolNow(hc);
    /* Task 50.38: the reader's state for this lifetime, before any thread
     * of it exists (hcdThreadStart makes the reader after the controller
     * thread), so no reading of an earlier lifetime is ever taken. */
    hc->InjPollHead = 0;
    hc->InjPollCount = 0;
    hc->InjPollLost = 0;
    hc->InjPollLostNoted = 0;
    hc->InjPollLast = 0;
    hc->InjPollLastValid = 0;
    hc->InjPollOpen = 0;
    hc->InjPollFast = 0;
    hc->InjPollStop = 0;
    hc->InjPollRunning = 0;
    hc->InjPollObject = NULL;
    KeInitializeEvent(&hc->InjPollKick, SynchronizationEvent, FALSE);
    KeInitializeEvent(&hc->InjPollExited, NotificationEvent, FALSE);
    XhciLogNote(&hc->Hc, "qemu.inj.latched", found ? value : 0xFFFFFFFFUL);
    value = 0;
    if (!NT_SUCCESS(HcdCtlQemuReadDword(hc, HCD_VALUE_QEMU_START_HOLD,
                                        &value))) {
        value = 0;
    }
    if (value > HCD_QEMU_START_HOLD_MAX) {
        value = HCD_QEMU_START_HOLD_MAX;
    }
    /* An end on the tolerance clock, never 0 while a hold is asked for. */
    (VOID)InterlockedExchange(
        (PLONG)&hc->InjStartHoldTicks,
        (value == 0) ? 0L
                     : (LONG)(HcdTolNow(hc) +
                              (value + XHCI_TOL_TICK_MS - 1UL) /
                                  XHCI_TOL_TICK_MS));
    XhciLogNote(&hc->Hc, "qemu.inj.starthold", value);
}

/*
 * The hardware faults go out only to a controller that runs: under the
 * power gate, in D0, started, initialized and not failed - what the
 * driver's own steps would need to meet them. IRQL: PASSIVE_LEVEL, the
 * power gate held.
 */
static ULONG hcdInjRunning(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;

    ext = &hc->Hc;
    return (ext->HcInfoStatus == XHCI_HC_OK &&
            (ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
            (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0 &&
            (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
            !ext->ControllerFailed &&
            hc->Common.DevicePower == PowerDeviceD0 &&
            !hc->SuspendedInD0) ? 1UL : 0UL;
}

/* The first root port that fits (XhciInjPortFits), or 0. IRQL:
 * PASSIVE_LEVEL, the power gate held. */
static ULONG hcdInjPickPort(PHCD_CONTROLLER hc, ULONG wanted)
{
    PXHCI_EXTENSION ext;
    ULONG port;

    ext = &hc->Hc;
    for (port = 1; port <= ext->HcInfo.MaxPorts; port++) {
        if (XhciInjPortFits(&ext->PortMap, port, wanted,
                            XhciReadPortsc(ext, port))) {
            return port;
        }
    }
    return 0;
}

/* A Port Status Change Event for the port, as the drain hands one over
 * (xhci_evt.c): the controller lock held, the port marked, the thread
 * woken. IRQL: PASSIVE_LEVEL. */
static VOID hcdInjPortEvent(PHCD_CONTROLLER hc, ULONG port)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    XhciRootHubPortEvent(&hc->Hc, port);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* PED, OC and HCH: the hardware faults. Returns the port acted on (1 for
 * HCH), 0 for a refusal. IRQL: PASSIVE_LEVEL. */
static ULONG hcdInjHardware(PHCD_CONTROLLER hc, ULONG fault, ULONG wanted)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG port;
    ULONG portsc;
    ULONG usbcmd;

    ext = &hc->Hc;
    port = 0;
    HcdPowerGateEnter(hc);
    if (!hcdInjRunning(hc)) {
        HcdPowerGateLeave(hc);
        return 0;
    }
    switch (fault) {
    case XHCI_INJ_PED:
        port = hcdInjPickPort(hc, wanted);
        if (port == 0) {
            break;
        }
        portsc = XhciReadPortsc(ext, port);
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjArmPec(&hc->InjRegs, port);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        /* PED is RW1C: writing 1 disables the port, the one way software
         * can, and the controller sets no PEC for it (xhci-data-structures.md,
         * PORTSC) - which is why PEC is answered here. QEMU ignores the
         * write altogether (hcd-xhci.c, xhci_port_write), so PED is
         * answered clear too; on hardware the write is the fault. */
        XhciWritePortsc(ext, port, XhciPortscDisable(portsc));
        hcdInjPortEvent(hc, port);
        break;
    case XHCI_INJ_OC:
        port = hcdInjPickPort(hc, wanted);
        if (port == 0) {
            break;
        }
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjArmOc(&hc->InjRegs, port);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        hcdInjPortEvent(hc, port);
        break;
    case XHCI_INJ_HCH:
        /* Under the controller lock, as every USBCMD read-modify-write is
         * on a running controller; the health poll finds HCH. */
        XhciControllerLockAcquire(ext, &oldIrql);
        usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
        if (usbcmd != 0xFFFFFFFFUL) {
            XhciWriteOp(ext, XHCI_OP_USBCMD, usbcmd & ~XHCI_USBCMD_RS);
            port = 1;
        }
        XhciControllerLockRelease(ext, oldIrql);
        break;
    default:
        break;
    }
    HcdPowerGateLeave(hc);
    return port;
}

/* ----------------------------------------------------------------------- */
/* The endpoint faults: the thread's half                                   */
/* ----------------------------------------------------------------------- */

/* The layer's own command, which its command hook lets through.
 * IRQL: PASSIVE_LEVEL (the thread), powered. */
static ULONG hcdInjSend(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                        PULONG control)
{
    ULONG code;

    hc->InjOwnCmd = 1;
    code = HcdThreadCommand(hc, trb, control);
    hc->InjOwnCmd = 0;
    return code;
}

static ULONG hcdInjOwnCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb)
{
    ULONG control;

    return hcdInjSend(hc, trb, &control);
}

/* An injected event handed to the drain and waited for. Returns 1 when the
 * drain took it; one it never took is withdrawn. IRQL: PASSIVE_LEVEL. */
static ULONG hcdInjDeliver(PHCD_CONTROLLER hc, const XHCI_TRB *ev)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG posted;
    ULONG withdrawn;

    KeClearEvent(&hc->InjEvDone);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    posted = !hc->InjEp.EvPending;
    if (posted) {
        hc->InjEp.Ev.Param0 = ev->Param0;
        hc->InjEp.Ev.Param1 = ev->Param1;
        hc->InjEp.Ev.Status = ev->Status;
        hc->InjEp.Ev.Control = ev->Control;
        hc->InjEp.EvPending = 1;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (!posted) {
        return 0;
    }
    hcdInjQueueDrain(hc);
    HcdRelativeMs(&due, HCD_INJ_DELIVER_MS);
    (VOID)KeWaitForSingleObject(&hc->InjEvDone, Executive, KernelMode, FALSE,
                                &due);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    withdrawn = hc->InjEp.EvPending;
    hc->InjEp.EvPending = 0;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return withdrawn ? 0UL : 1UL;
}

/* The target endpoint rung, as a submission or the driver's own recovery
 * rings it, while its device is still the one the slot names. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdInjRing(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev = hc->SlotDevice[hc->InjSlot];
    if (dev != NULL && dev == hc->InjDev && !dev->Gone &&
        !HCD_DEV_FROZEN(dev) &&
        dev->Pipes[hc->InjDci] != NULL && !dev->Pipes[hc->InjDci]->Closed) {
        XhciWriteDoorbell(&hc->Hc, hc->InjSlot, hc->InjDci);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* An attempt given up: the answers end, the count and the note, and the
 * endpoint rung so the TD the layer stopped runs again. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdInjAbandon(PHCD_CONTROLLER hc, ULONG why)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEpEnd(&hc->InjEp);
    hc->InjEp.Abandoned++;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XHCI_DBG_VALUE("qemu inject: abandoned, slot/dci/why",
                   (hc->InjSlot << 16) | (hc->InjDci << 8) | why);
    XhciLogNote(&hc->Hc, "qemu.inj.abandon",
                (hc->InjSlot << 16) | (hc->InjDci << 8) | why);
    hcdInjRing(hc);
}

/* The context's TR Dequeue Pointer, DCS masked off. Controller lock held. */
static ULONG hcdInjCtxDequeue(PHCD_CONTROLLER hc, ULONG slot, ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, slot, dci, &offset) !=
        XHCI_LAYOUT_OK) {
        return 0;
    }
    return XhciCommonAt(&hc->Hc, offset)[2] & XHCI_EP_DEQUEUE_MASK;
}

/*
 * The layer's Stop Endpoint on the target, its Stopped event swallowed by
 * the drain, and the TD named by `token` confirmed at the controller's
 * dequeue (XhciInjStopVerdict) - put back with a Set TR Dequeue when QEMU
 * fetched it ahead with nothing moved. Returns 1 with the TD's first TRB
 * and its length, the doorbells still dropped; otherwise the attempt is
 * abandoned. IRQL: PASSIVE_LEVEL, powered.
 */
static ULONG hcdInjStop(PHCD_CONTROLLER hc, ULONG token, PULONG firstPA,
                        PULONG firstLen)
{
    PHCD_PIPE pipe;
    PXHCI_TRANSFER head;
    PXHCI_RING ring;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG code;
    ULONG seen;
    ULONG seenPA;
    ULONG seenCode;
    ULONG seenResidual;
    ULONG verdict;
    ULONG same;
    ULONG first;
    ULONG len;
    ULONG dcs;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEpWatch(&hc->InjEp, hc->InjSlot, hc->InjDci);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    code = 0;
    if (XhciTrbStopEndpoint(&trb, hc->InjSlot, hc->InjDci, 0) ==
        XHCI_RING_OK) {
        code = hcdInjOwnCommand(hc, &trb);
    }
    /* The Stopped event precedes the command's completion on the event
     * ring (xHCI 4.6.9), so it has been drained by now if there was one. */
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjEp.Watch = 0;
    seen = hc->InjEp.StopSeen;
    seenPA = hc->InjEp.StopPA;
    seenCode = hc->InjEp.StopCode;
    seenResidual = hc->InjEp.StopResidual;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (code != XHCI_CC_SUCCESS) {
        hcdInjAbandon(hc, 0x80UL | (code & 0x7FUL));
        return 0;
    }

    verdict = XHCI_INJ_STOP_BROKEN;
    same = 0;
    first = 0;
    len = 0;
    dcs = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe = (hc->SlotDevice[hc->InjSlot] == hc->InjDev && !hc->InjDev->Gone)
               ? hc->InjDev->Pipes[hc->InjDci]
               : NULL;
    head = (pipe != NULL) ? pipe->Queue->Head : NULL;
    if (head != NULL) {
        ring = pipe->Ring;
        same = (head->Token == token) ? 1UL : 0UL;
        first = XhciRingTrbPA(ring, head->FirstIndex);
        len = XHCI_TRB_GET_LENGTH(ring->Base[head->FirstIndex].Status);
        dcs = ring->Base[head->FirstIndex].Control & XHCI_TRB_CYCLE;
        verdict = XhciInjStopVerdict(
            hcdInjCtxDequeue(hc, hc->InjSlot, hc->InjDci), first,
            XhciInjAfterPA(ring->BasePA, head->LastIndex),
            head->TrbCount == 1, len, seen, seenPA, seenCode, seenResidual);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);

    if (verdict == XHCI_INJ_STOP_REPOINT) {
        verdict = XHCI_INJ_STOP_BROKEN;
        if (XhciTrbSetTrDequeue(&trb, hc->InjSlot, hc->InjDci, first, dcs) ==
                XHCI_RING_OK &&
            hcdInjOwnCommand(hc, &trb) == XHCI_CC_SUCCESS) {
            KeAcquireSpinLock(&hc->InjLock, &oldIrql);
            hc->InjEp.Repointed++;
            KeReleaseSpinLock(&hc->InjLock, oldIrql);
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            if (hcdInjCtxDequeue(hc, hc->InjSlot, hc->InjDci) == first) {
                verdict = XHCI_INJ_STOP_OK;
            }
            XhciControllerLockRelease(&hc->Hc, oldIrql);
        }
    }
    if (verdict != XHCI_INJ_STOP_OK || !same) {
        /* A TD moved on, or one this layer cannot put back. */
        hcdInjAbandon(hc, (verdict << 1) | same);
        return 0;
    }
    *firstPA = first;
    *firstLen = len;
    return 1;
}

/* The answer to a held Reset Endpoint (RACE_RING): it has taken effect -
 * the answers end, the real endpoint is Stopped - and the endpoint is rung
 * as a submission arriving meanwhile would ring it, resuming the TD before
 * the thread does. IRQL: PASSIVE_LEVEL. */
static VOID hcdInjHeldRing(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEpEnd(&hc->InjEp);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    hcdInjRing(hc);
    XhciLogNote(&hc->Hc, "qemu.inj.held.ring",
                (hc->InjSlot << 8) | hc->InjDci);
}

/* RACE_SECOND: the held Reset Endpoint has taken effect and a ring resumed
 * the TD; another real stop, the TD confirmed at the dequeue again, Halted
 * answered again and a second Transaction Error delivered - a newer
 * generation - all before the held completion is returned. The answers
 * then stand for the next Reset Endpoint. IRQL: PASSIVE_LEVEL, powered. */
static VOID hcdInjHeldSecond(PHCD_CONTROLLER hc)
{
    PHCD_PIPE pipe;
    XHCI_TRB ev;
    KIRQL oldIrql;
    ULONG token;
    ULONG first;
    ULONG len;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjEp.State = 0;
    hc->InjEp.Block = 0;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    hcdInjRing(hc);
    token = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe = (hc->SlotDevice[hc->InjSlot] == hc->InjDev && !hc->InjDev->Gone)
               ? hc->InjDev->Pipes[hc->InjDci]
               : NULL;
    if (pipe != NULL && pipe->Queue->Head != NULL) {
        token = pipe->Queue->Head->Token;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (pipe == NULL) {
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjEpEnd(&hc->InjEp);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        return;
    }
    if (!hcdInjStop(hc, token, &first, &len)) {
        return;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjEp.State = XHCI_EP_STATE_HALTED;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciInjTransferEvent(&ev, first, len, XHCI_CC_USB_TRANSACTION_ERROR,
                         hc->InjSlot, hc->InjDci);
    if (!hcdInjDeliver(hc, &ev)) {
        hcdInjAbandon(hc, 0x40UL);
        return;
    }
    XhciLogNote(&hc->Hc, "qemu.inj.held.second",
                (hc->InjSlot << 8) | hc->InjDci);
}

/*
 * Whether a Reset Endpoint for the target is the soft retry's own, for the
 * TD it still holds (XhciInjCommand's `retrySurvives`): the pipe not paused
 * - every operation that resets a halted endpoint for itself, an abort, a
 * cancel or a client's reset, pauses it - and RetryWanted naming the TD
 * the queue still has at its head with its deferred outcome. Read under
 * the controller lock the queue is kept under. IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdInjRetrySurvives(PHCD_CONTROLLER hc, ULONG slot, ULONG dci)
{
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG token;
    ULONG survives;

    survives = 0;
    if (slot == 0 || slot > XHCI_MAX_SLOTS || dci < 2 || dci > 31) {
        return 0;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev = hc->SlotDevice[slot];
    pipe = (dev != NULL && dev == hc->InjDev && !dev->Gone)
               ? dev->Pipes[dci]
               : NULL;
    if (pipe != NULL && !pipe->Paused && !pipe->Closed &&
        XhciXferRetryPending(pipe->Queue, &token, NULL) &&
        XhciXferRetryHeadIs(pipe->Queue, token)) {
        survives = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return survives;
}

/*
 * Every command the thread issues (hcd_enum.c, hcdCommand), before it is
 * sent: the layer's own pass, and so does everything that does not meet
 * the answered endpoint (XhciInjCommand). A Reset Endpoint or Set TR
 * Dequeue the answers cover is completed here without reaching the
 * controller - `*code`, and a Command Completion's DW3 in `*control` -
 * and a held one after the race it holds for, which runs only for the
 * soft retry's own reset (hcdInjRetrySurvives). A command that ends the
 * answers only by succeeding (a Disable Slot, a Configure Endpoint that
 * drops or adds the endpoint, its Address Device or Reset Device, a Set TR
 * Dequeue leaving Error) is sent from here, once, and its completion
 * returned; the answers end on its Success. Returns 1 when this function
 * produced the completion. IRQL: PASSIVE_LEVEL (the thread), powered.
 */
ULONG HcdInjCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb, PULONG control,
                    PULONG code)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;
    ULONG verdict;
    ULONG answer;
    ULONG icc;
    ULONG icDrop;
    ULONG icAdd;
    ULONG survives;
    ULONG type;
    ULONG ended;
    ULONG slot;
    ULONG dci;
    ULONG race;

    /* Task 49.39: fault 1D. The cancel's own stream Set TR Dequeue, while
     * armed, goes out for real with the cancelled head TD's first TRB and
     * its cycle; QEMU takes it and writes the Stream Context, which the
     * driver's read-back then meets. */
    if (!hc->InjOwnCmd && hc->InjStaleDeq != 0 && hc->InjStaleSlot != 0 &&
        XHCI_TRB_GET_TYPE(trb->Control) == XHCI_TRB_TYPE_SET_TR_DEQUEUE &&
        XHCI_TRB_GET_SLOT_ID(trb->Control) == hc->InjStaleSlot &&
        XHCI_TRB_GET_EP_ID(trb->Control) == hc->InjStaleDci &&
        ((trb->Status >> 16) & 0xFFFFUL) == hc->InjStaleStream) {
        XHCI_TRB stale;

        stale = *trb;
        stale.Param0 = hc->InjStalePa | (trb->Param0 & 0x0EUL) |
                       (hc->InjStaleDcs & 1UL);
        stale.Param1 = 0;
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        if (hc->InjStaleDeq != 0) {
            hc->InjStaleDeq--;
        }
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        XhciLogNote(&hc->Hc, "qemu.inj.staledeq",
                    (hc->InjStaleSlot << 16) | (hc->InjStaleDci << 8) |
                        (hc->InjStaleStream & 0xFFUL));
        *code = hcdInjSend(hc, &stale, control);
        return 1;
    }
    /* Issue 26: fault 1A, before the endpoint faults' own pass. The Stop
     * is sent for real, so the endpoint is truly Stopped; the driver is
     * told Context State Error, and its next read of the context is told
     * Running. */
    if (!hc->InjOwnCmd && hc->InjStopRace != 0 &&
        XHCI_TRB_GET_TYPE(trb->Control) == XHCI_TRB_TYPE_STOP_EP) {
        slot = XHCI_TRB_GET_SLOT_ID(trb->Control);
        dci = XHCI_TRB_GET_EP_ID(trb->Control);
        race = 0;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        dev = (slot >= 1 && slot <= XHCI_MAX_SLOTS) ? hc->SlotDevice[slot]
                                                    : NULL;
        if (dev != NULL && !dev->Gone && dci >= 2 && dci < 32 &&
            dev->Pipes[dci] != NULL && dev->Pipes[dci]->Streams != NULL) {
            race = 1;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (race) {
            answer = hcdInjSend(hc, trb, control);
            if (answer != XHCI_CC_SUCCESS) {
                /* Not proven Stopped: the real outcome, no race. */
                *code = answer;
                return 1;
            }
            KeAcquireSpinLock(&hc->InjLock, &oldIrql);
            if (hc->InjStopRace != 0) {
                hc->InjStopRace--;
            }
            hc->InjStopRaceSlot = slot;
            hc->InjStopRaceDci = dci;
            hc->InjStopRaceReads = 1;
            KeReleaseSpinLock(&hc->InjLock, oldIrql);
            XhciLogNote(&hc->Hc, "qemu.inj.stoprace", (slot << 8) | dci);
            *code = XHCI_CC_CONTEXT_STATE_ERROR;
            return 1;
        }
    }
    if (hc->InjOwnCmd || hc->InjEp.Slot == 0) {
        return 0;
    }
    type = XHCI_TRB_GET_TYPE(trb->Control);
    icDrop = 0;
    icAdd = 0;
    if (type == XHCI_TRB_TYPE_CONFIGURE_EP &&
        XhciInputControlContextOffset(&hc->Hc.Layout, &icc) ==
            XHCI_LAYOUT_OK) {
        /* The Input Control Context the command is about to be issued
         * with: Drop flags in DW0, Add flags in DW1. */
        icDrop = XhciCommonAt(&hc->Hc, icc)[0];
        icAdd = XhciCommonAt(&hc->Hc, icc)[1];
    }
    survives = (type == XHCI_TRB_TYPE_RESET_EP)
                   ? hcdInjRetrySurvives(hc, XHCI_TRB_GET_SLOT_ID(trb->Control),
                                         XHCI_TRB_GET_EP_ID(trb->Control))
                   : 0UL;
    answer = XHCI_CC_SUCCESS;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    verdict = XhciInjCommand(&hc->InjEp, trb, icDrop, icAdd, survives,
                             &answer);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    switch (verdict) {
    case XHCI_INJ_CMD_SEND_END:
        answer = hcdInjSend(hc, trb, control);
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjCommandSent(&hc->InjEp, XHCI_TRB_GET_SLOT_ID(trb->Control),
                           answer);
        ended = (hc->InjEp.Slot == 0) ? 1UL : 0UL;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        if (ended) {
            /* 36.22: the losing ends with the command that ended the
             * fault, before the recovery that sent it goes on. */
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            hc->InjBulkLose = 0;
            XhciControllerLockRelease(&hc->Hc, oldIrql);
        }
        *code = answer;
        return 1;
    case XHCI_INJ_CMD_ANSWER:
        break;
    case XHCI_INJ_CMD_RING:
        hcdInjHeldRing(hc);
        answer = XHCI_CC_SUCCESS;
        break;
    case XHCI_INJ_CMD_SECOND:
        hcdInjHeldSecond(hc);
        answer = XHCI_CC_SUCCESS;
        break;
    default:
        return 0;
    }
    *code = answer;
    *control = XHCI_TRB_SLOT_ID(XHCI_TRB_GET_SLOT_ID(trb->Control)) |
               XHCI_TRB_TYPE(XHCI_TRB_TYPE_COMMAND_COMPLETION);
    XhciLogNote(&hc->Hc, "qemu.inj.answer",
                (XHCI_TRB_GET_TYPE(trb->Control) << 24) |
                    (XHCI_TRB_GET_SLOT_ID(trb->Control) << 16) |
                    (XHCI_TRB_GET_EP_ID(trb->Control) << 8) | answer);
    return 1;
}

/* The first pipe that fits (XhciInjPipeFits), taken as the target, and its
 * head TD's token; 0 when none does. IRQL: PASSIVE_LEVEL. */
static ULONG hcdInjPick(PHCD_CONTROLLER hc, PULONG token)
{
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG slot;
    ULONG dci;
    ULONG found;

    found = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (slot = 1; slot <= XHCI_MAX_SLOTS && !found; slot++) {
        dev = hc->SlotDevice[slot];
        if (dev == NULL) {
            continue;
        }
        for (dci = 2; dci < 32 && !found; dci++) {
            pipe = dev->Pipes[dci];
            if (pipe == NULL ||
                !XhciInjPipeFits(
                    hc->InjPort, dev->Port, dev->Pdo != NULL,
                    dev->Hub != NULL, dev->Gone, pipe->TransferType,
                    pipe->EndpointAddress,
                    pipe->Streams != NULL || pipe->StreamId != 0,
                    pipe->Paused || pipe->Closed || pipe->Halted ||
                        pipe->DrainPending || pipe->CancelPending,
                    pipe->Queue->Head != NULL, pipe->Queue->RetryWanted)) {
                continue;
            }
            hc->InjDev = dev;
            hc->InjSlot = slot;
            hc->InjDci = dci;
            *token = pipe->Queue->Head->Token;
            found = 1;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return found;
}

/*
 * One injection of the armed pipe fault, on the first pipe that fits; none
 * fitting is noted once and tried again next pass. A refused code needs no
 * stop - the cycle's Disable Slot takes the TD back - and waits for the
 * device to leave; a retry or a halt stops the endpoint first. IRQL:
 * PASSIVE_LEVEL, powered.
 */
static VOID hcdInjPipeFire(PHCD_CONTROLLER hc)
{
    PHCD_PIPE pipe;
    PXHCI_RING ring;
    XHCI_TRB ev;
    KIRQL oldIrql;
    ULONG fault;
    ULONG kind;
    ULONG token;
    ULONG first;
    ULONG len;
    ULONG state;

    fault = hc->InjFault;
    if (!hcdInjPick(hc, &token)) {
        if (!hc->InjWaitNoted) {
            hc->InjWaitNoted = 1;
            XhciLogNote(&hc->Hc, "qemu.inj.waiting", fault);
        }
        return;
    }
    hc->InjWaitNoted = 0;
    (VOID)XhciInjSpend(&hc->InjLeft);
    kind = XhciInjKind(fault);
    XHCI_DBG_VALUE("qemu inject: fault/slot/dci",
                   (fault << 16) | (hc->InjSlot << 8) | hc->InjDci);
    XhciLogNote(&hc->Hc, "qemu.inj.ep",
                (fault << 16) | (hc->InjSlot << 8) | hc->InjDci);

    if (kind == XHCI_INJ_KIND_REFUSED) {
        if (fault == XHCI_INJ_EP_NOT_ENABLED) {
            /* As a doorbell for a Disabled endpoint raises it (xHCI 4.7):
             * pointer, length and ED 0. */
            XhciInjTransferEvent(&ev, 0, 0, XHCI_CC_EP_NOT_ENABLED,
                                 hc->InjSlot, hc->InjDci);
        } else {
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            pipe = hc->InjDev->Pipes[hc->InjDci];
            ring = pipe->Ring;
            first = 0;
            len = 0;
            if (pipe->Queue->Head != NULL) {
                first = XhciRingTrbPA(ring, pipe->Queue->Head->FirstIndex);
                len = XHCI_TRB_GET_LENGTH(
                    ring->Base[pipe->Queue->Head->FirstIndex].Status);
            }
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            XhciInjTransferEvent(&ev, first, len, XHCI_INJ_REFUSED_CC,
                                 hc->InjSlot, hc->InjDci);
        }
        if (!hcdInjDeliver(hc, &ev)) {
            XhciLogNote(&hc->Hc, "qemu.inj.undelivered", fault);
        }
        hc->InjPhase = HCD_INJ_PHASE_GONE;
        hc->InjSince = HcdTolNow(hc);
        return;
    }

    if (!hcdInjStop(hc, token, &first, &len)) {
        return;
    }
    state = XhciInjHaltStateOf(fault);
    if (kind == XHCI_INJ_KIND_RETRY) {
        XhciInjTransferEvent(&ev, first, len, XHCI_CC_USB_TRANSACTION_ERROR,
                             hc->InjSlot, hc->InjDci);
    } else {
        /* A halt naming no TD: pointer 0, or for the Error variant a TRB
         * of the event ring, which no transfer ring holds. */
        XhciInjTransferEvent(
            &ev, fault == XHCI_INJ_HALT_ERROR ? hc->Hc.EventRing.BasePA : 0UL,
            0, XHCI_CC_STALL, hc->InjSlot, hc->InjDci);
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (state != 0) {
        hc->InjEp.State = state;
        hc->InjEp.Hold = XhciInjHoldOf(fault);
    } else {
        /* HALT_STALE: the real Stopped endpoint, the TD back at its
         * dequeue; a submission's ring may restart it from here. */
        XhciInjEpEnd(&hc->InjEp);
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (!hcdInjDeliver(hc, &ev)) {
        hcdInjAbandon(hc, 0x40UL);
        return;
    }
    hc->InjPhase = (fault == XHCI_INJ_HALT_STALE) ? HCD_INJ_PHASE_STALE
                                                  : HCD_INJ_PHASE_ANSWERING;
    hc->InjSince = HcdTolNow(hc);
}

/*
 * The endpoint faults' step of each controller-thread pass (HcdEnumService,
 * before the soft retry's service, so a diverted error is decided in the
 * pass that delivered it): the answers ended when the target's device left;
 * the injection in flight followed to its end - the driver's Reset
 * Endpoint or Disable Slot for an answered endpoint, the read of a stale
 * halt (then the ring that restarts it), the departure a refused code
 * brings - and then the next injection while one is left. IRQL:
 * PASSIVE_LEVEL, the power gate held, powered, the controller not halted.
 */
static ULONG hcdInjBulkFire(PHCD_CONTROLLER hc);
static VOID hcdInjBulkRelease(PHCD_CONTROLLER hc);

VOID HcdInjService(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG now;
    ULONG same;
    ULONG marked;
    ULONG phase;
    ULONG lost;

    if (hc->InjFault == 0 && hc->InjPhase == HCD_INJ_PHASE_IDLE &&
        hc->InjEp.Slot == 0) {
        return;
    }
    now = HcdTolNow(hc);
    same = 0;
    marked = 0;
    if (hc->InjPhase != HCD_INJ_PHASE_IDLE || hc->InjEp.Slot != 0) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        same = (hc->InjSlot != 0 && hc->SlotDevice[hc->InjSlot] == hc->InjDev &&
                hc->InjDev != NULL && !hc->InjDev->Gone) ? 1UL : 0UL;
        marked = same && XhciTolMarkPending(&hc->InjDev->CycleMark);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    if (hc->InjEp.Slot != 0 && !same) {
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjEpEnd(&hc->InjEp);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
    }
    switch (hc->InjPhase) {
    case HCD_INJ_PHASE_ANSWERING:
        if (hc->InjEp.Slot != 0) {
            return;
        }
        XhciLogNote(&hc->Hc, "qemu.inj.ended", hc->InjFault);
        break;
    case HCD_INJ_PHASE_STALE:
        if (same && marked && now - hc->InjSince < HCD_INJ_SETTLE_TICKS) {
            return;
        }
        hcdInjRing(hc);
        XhciLogNote(&hc->Hc, "qemu.inj.stale.ring", same);
        break;
    case HCD_INJ_PHASE_GONE:
        if (same && now - hc->InjSince < HCD_INJ_SETTLE_TICKS) {
            return;
        }
        break;
    case HCD_INJ_PHASE_BULK_WAIT:
        /* 36.22: the withholding ended before any TD met it - its
         * device left, or the driver stopped the endpoint - or the fault
         * is carried out now on the TD it holds. */
        if (hc->InjEp.Slot == 0) {
            XhciLogNote(&hc->Hc, "qemu.inj.bulk.ended", hc->InjFault);
            break;
        }
        phase = hcdInjBulkFire(hc);
        if (phase != HCD_INJ_PHASE_IDLE) {
            hc->InjPhase = phase;
            hc->InjSince = now;
            return;
        }
        break;
    case HCD_INJ_PHASE_BULK_LOSE:
        /* BULK_TIMEOUT armed: ended unmet - its device left, or the
         * driver's own stop, reset or dequeue move came first - or met,
         * a TD's completion lost, then held until the driver recovers. */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        lost = hc->InjBulkLost;
        if (hc->InjEp.Slot == 0) {
            hc->InjBulkLose = 0;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (hc->InjEp.Slot == 0) {
            XhciLogNote(&hc->Hc, "qemu.inj.bulk.ended", hc->InjFault);
            break;
        }
        if (!lost) {
            return;
        }
        (VOID)XhciInjSpend(&hc->InjLeft);
        /* A TRB's physical address: level 4 alone. */
        XhciLogNoteAddress(&hc->Hc, "qemu.inj.bulk.lost",
                           hc->InjBulkLostFirst);
        hc->InjPhase = HCD_INJ_PHASE_BULK_HELD;
        hc->InjSince = now;
        return;
    case HCD_INJ_PHASE_BULK_CLEAR:
        if (hc->InjEp.Slot != 0 && hc->InjEp.Bulk && !hc->InjEp.Block) {
            /* Nothing withheld: the losing ends; a completion already
             * lost stays the driver's to recover. */
            KeAcquireSpinLock(&hc->InjLock, &oldIrql);
            XhciInjEpEnd(&hc->InjEp);
            KeReleaseSpinLock(&hc->InjLock, oldIrql);
        } else if (hc->InjEp.Slot != 0 && hc->InjEp.Bulk &&
                   hc->InjEp.State == 0) {
            hcdInjBulkRelease(hc);
        }
        XhciLogNote(&hc->Hc, "qemu.inj.bulk.cleared", hc->InjBulkStream);
        break;
    case HCD_INJ_PHASE_BULK_HELD:
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        lost = hc->InjBulkLose;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (hc->InjEp.Slot != 0 && lost) {
            return;
        }
        /* Ended: the driver's own command, the device's departure, or the
         * lost TD leaving the queue (hcdInjBulkLoseEvent). */
        if (hc->InjEp.Slot != 0) {
            KeAcquireSpinLock(&hc->InjLock, &oldIrql);
            XhciInjEpEnd(&hc->InjEp);
            KeReleaseSpinLock(&hc->InjLock, oldIrql);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hc->InjBulkLose = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        XhciLogNote(&hc->Hc, "qemu.inj.bulk.released",
                    (hc->InjBulkLostEvents << 16) | (hc->InjSlot << 8) |
                        hc->InjDci);
        break;
    default:
        break;
    }
    hc->InjPhase = HCD_INJ_PHASE_IDLE;
    if (hc->InjFault == 0) {
        return;
    }
    if (hc->InjLeft == 0 || XhciInjKind(hc->InjFault) == XHCI_INJ_KIND_BULK) {
        XhciLogNote(&hc->Hc, "qemu.inj.done", hc->InjFault);
        hc->InjFault = 0;
        return;
    }
    hcdInjPipeFire(hc);
}

/* ----------------------------------------------------------------------- */
/* The bulk faults (task 36.22)                                            */
/* ----------------------------------------------------------------------- */

/* The target endpoint's pipe while its device is still the one the slot
 * names, else NULL. Controller lock held. */
static PHCD_PIPE hcdInjBulkPipe(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;

    dev = hc->SlotDevice[hc->InjSlot];
    if (dev == NULL || dev != hc->InjDev || dev->Gone) {
        return NULL;
    }
    return dev->Pipes[hc->InjDci];
}

/* The ring a stream (0: the endpoint itself) runs on, its pipe in *p.
 * Controller lock held. */
static PXHCI_RING hcdInjBulkRing(PHCD_PIPE pipe, ULONG stream,
                                 PHCD_PIPE *p)
{
    *p = NULL;
    if (pipe == NULL) {
        return NULL;
    }
    if (stream == 0) {
        if (pipe->Streams != NULL) {
            return NULL;
        }
        *p = pipe;
        return pipe->Ring;
    }
    if (pipe->Streams == NULL || stream > pipe->Streams->Count ||
        pipe->Streams->Pipe[stream] == NULL) {
        return NULL;
    }
    *p = pipe->Streams->Pipe[stream];
    return pipe->Streams->Ring[stream];
}

/* Every TD the withholding held back started: the endpoint rung, or each
 * of its streams with one queued, by hcdCfgStreamKick's checks.
 * Controller lock taken. IRQL: PASSIVE_LEVEL, powered. */
static VOID hcdInjBulkKick(PHCD_CONTROLLER hc)
{
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG id;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe = hcdInjBulkPipe(hc);
    if (pipe != NULL && !pipe->Closed && !HCD_DEV_FROZEN(pipe->Device)) {
        if (pipe->Streams == NULL) {
            if (pipe->Queue->Count != 0 && !pipe->Paused) {
                XhciWriteDoorbell(&hc->Hc, hc->InjSlot, hc->InjDci);
            }
        } else {
            for (id = 1; id <= pipe->Streams->Count; id++) {
                p = pipe->Streams->Pipe[id];
                if (p != NULL && p->Queue->Count != 0 && !p->Paused &&
                    !p->Closed) {
                    XhciWriteDoorbell(&hc->Hc, hc->InjSlot,
                                      XhciStreamDoorbell(hc->InjDci, id));
                }
            }
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* The withholding ended by the layer itself, and every TD it held
 * started. IRQL: PASSIVE_LEVEL, powered. */
static VOID hcdInjBulkRelease(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEpEnd(&hc->InjEp);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    hcdInjBulkKick(hc);
}

/* hcdInjAbandon for a bulk fault: counted and noted the same, and every
 * stream rung, not stream 0 alone. IRQL: PASSIVE_LEVEL, powered. */
static VOID hcdInjBulkAbandon(PHCD_CONTROLLER hc, ULONG why)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjEp.Abandoned++;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciLogNote(&hc->Hc, "qemu.inj.abandon",
                (hc->InjSlot << 16) | (hc->InjDci << 8) | why);
    hcdInjBulkRelease(hc);
}

/* 36.54: the attempt given up after the QEMU stream resync could not finish
 * (record 17 section 4.12.4): counted and noted as an abandon (0x44), the
 * withholding ended - and nothing rung, since a stream may still be
 * unrepaired; the controller reset the resync requested invalidates the
 * endpoint. IRQL: PASSIVE_LEVEL, powered. */
static VOID hcdInjBulkGiveUp(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjEp.Abandoned++;
    XhciInjEpEnd(&hc->InjEp);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciLogNote(&hc->Hc, "qemu.inj.abandon",
                (hc->InjSlot << 16) | (hc->InjDci << 8) | 0x44UL);
}

/* `index` within the TD [first, last] of one ring, the TD wrapping past
 * the Link when last precedes first; the Link itself is never a TD's. */
static ULONG hcdInjInTd(const XHCI_RING *ring, ULONG index, ULONG first,
                        ULONG last)
{
    if (index + 1UL == ring->Trbs) {
        return 0;
    }
    if (first <= last) {
        return index >= first && index <= last;
    }
    return index >= first || index <= last;
}

/* The transfer with `token` still on the queue, or NULL. Controller lock
 * held. */
static PXHCI_TRANSFER hcdInjTdOf(PHCD_PIPE p, ULONG token)
{
    PXHCI_TRANSFER t;

    for (t = p->Queue->Head; t != NULL; t = t->Next) {
        if (t->Token == token) {
            return t;
        }
    }
    return NULL;
}

/*
 * BULK_TIMEOUT's hook in the drain, with the controller lock held. Armed,
 * the first Transfer Event of the target endpoint whose pointer - its high
 * word 0 - lies on the endpoint's ring, or one stream's, inside the TD at
 * that ring's head is lost, and that TD (by its ring and token) becomes
 * the lost one. From then every Transfer Event on that one ring is lost
 * too, so neither a short's tail nor a successor's completion reaches the
 * engine, which would otherwise sweep the lost TD as failed: the TD stays
 * outstanding, with the controller past it, until the driver's own stop, reset
 * or dequeue move ends the losing as it is sent (HcdInjCommand) or the TD
 * leaves the queue - then nothing more is lost. Events of other rings pass.
 * Returns 1 to swallow the event. IRQL: DISPATCH_LEVEL.
 */
static ULONG hcdInjBulkLoseEvent(PHCD_CONTROLLER hc, const XHCI_TRB *ev)
{
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    PXHCI_RING ring;
    PXHCI_TRANSFER head;
    ULONG index;
    ULONG id;
    ULONG lo;
    ULONG hi;

    if (XHCI_TRB_GET_TYPE(ev->Control) != XHCI_TRB_TYPE_TRANSFER_EVENT ||
        XHCI_TRB_GET_SLOT_ID(ev->Control) != hc->InjSlot ||
        XHCI_TRB_GET_EP_ID(ev->Control) != hc->InjDci ||
        XHCI_EVENT_IS_EVENT_DATA(ev->Control) || ev->Param1 != 0) {
        return 0;
    }
    pipe = hcdInjBulkPipe(hc);
    if (pipe == NULL) {
        hc->InjBulkLose = 0;
        return 0;
    }
    if (hc->InjBulkLost) {
        ring = hcdInjBulkRing(pipe, hc->InjBulkStream, &p);
        if (ring == NULL || hcdInjTdOf(p, hc->InjBulkLostToken) == NULL) {
            /* The lost TD has left the queue: the fault is over. */
            hc->InjBulkLose = 0;
            return 0;
        }
        if (XhciRingIndexFromPA(ring, ev->Param0, &index) != XHCI_RING_OK) {
            return 0;
        }
        hc->InjBulkLostEvents++;
        return 1;
    }
    lo = (pipe->Streams != NULL) ? 1UL : 0UL;
    hi = (pipe->Streams != NULL) ? pipe->Streams->Count : 0UL;
    for (id = lo; id <= hi; id++) {
        ring = hcdInjBulkRing(pipe, id, &p);
        if (ring == NULL || p->Queue->Head == NULL ||
            XhciRingIndexFromPA(ring, ev->Param0, &index) != XHCI_RING_OK) {
            continue;
        }
        head = p->Queue->Head;
        if (!hcdInjInTd(ring, index, head->FirstIndex, head->LastIndex)) {
            return 0;
        }
        hc->InjBulkLost = 1;
        hc->InjBulkLostToken = head->Token;
        hc->InjBulkLostFirst = XhciRingTrbPA(ring, head->FirstIndex);
        hc->InjBulkStream = id;
        hc->InjBulkLostEvents = 1;
        return 1;
    }
    return 0;
}

/*
 * A bulk fault armed: the first bulk endpoint of a published device at the
 * port (0 any) whose address is `address` (0: the first bulk IN) has its
 * doorbells withheld from now. Refused when another fault is armed or in
 * flight or no endpoint fits. Returns 1 when armed. IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdInjArmBulk(PHCD_CONTROLLER hc, ULONG fault, ULONG port,
                           ULONG address)
{
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    KIRQL injIrql;
    ULONG slot;
    ULONG dci;
    ULONG found;

    found = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    KeAcquireSpinLock(&hc->InjLock, &injIrql);
    if (hc->InjFault != 0 || hc->InjPhase != HCD_INJ_PHASE_IDLE ||
        hc->InjEp.Slot != 0 || hc->InjEp.Ep0Want != 0) {
        KeReleaseSpinLock(&hc->InjLock, injIrql);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return 0;
    }
    for (slot = 1; slot <= XHCI_MAX_SLOTS && !found; slot++) {
        dev = hc->SlotDevice[slot];
        if (dev == NULL || dev->Gone || dev->Pdo == NULL || dev->Hub != NULL ||
            (port != 0 && dev->Port != port)) {
            continue;
        }
        for (dci = 2; dci < 32 && !found; dci++) {
            pipe = dev->Pipes[dci];
            if (pipe == NULL || pipe->Closed ||
                pipe->TransferType != XHCI_PIPE_XFER_BULK) {
                continue;
            }
            if (address != 0 ? pipe->EndpointAddress != address
                             : (pipe->EndpointAddress & 0x80UL) == 0) {
                continue;
            }
            hc->InjDev = dev;
            hc->InjSlot = slot;
            hc->InjDci = dci;
            hc->InjEp.Slot = slot;
            hc->InjEp.Dci = dci;
            hc->InjEp.State = 0;
            hc->InjEp.Bulk = 1;
            hc->InjFault = fault;
            hc->InjLeft = 1;
            hc->InjBulkStream = 0;
            if (fault == XHCI_INJ_BULK_TIMEOUT) {
                /* No doorbell withheld: QEMU runs the TD and its
                 * completion is lost on the way to the driver. */
                hc->InjEp.Block = 0;
                hc->InjBulkLose = 1;
                hc->InjBulkLost = 0;
                hc->InjBulkLostEvents = 0;
                hc->InjPhase = HCD_INJ_PHASE_BULK_LOSE;
            } else {
                hc->InjEp.Block = 1;
                hc->InjPhase = HCD_INJ_PHASE_BULK_WAIT;
            }
            found = 1;
        }
    }
    KeReleaseSpinLock(&hc->InjLock, injIrql);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (found) {
        hc->InjSince = HcdTolNow(hc);
        XhciLogNote(&hc->Hc, "qemu.inj.bulk.armed",
                    (fault << 16) | (hc->InjSlot << 8) | hc->InjDci);
    }
    return found;
}

/* The TD the withheld doorbell left on a ring - the endpoint's, or the
 * lowest stream's that has one: its token, stream, last TRB and that
 * TRB's length, and the ring position after the TD with the cycle that
 * position carries or will carry (one Link crossing separates it from the
 * producer when it lies past the enqueue: XhciRingDequeueCycle's rule, at
 * that index). 0 when no TD waits yet. Controller lock taken. IRQL:
 * PASSIVE_LEVEL. */
static ULONG hcdInjBulkHead(PHCD_CONTROLLER hc, PULONG token, PULONG stream,
                            PULONG lastPA, PULONG lastLen, PULONG afterPA,
                            PULONG afterCycle)
{
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    PXHCI_RING ring;
    PXHCI_TRANSFER head;
    KIRQL oldIrql;
    ULONG id;
    ULONG first;
    ULONG last;
    ULONG after;
    ULONG found;

    found = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe = hcdInjBulkPipe(hc);
    first = 0;
    last = (pipe != NULL && pipe->Streams != NULL) ? pipe->Streams->Count : 0;
    if (pipe != NULL && pipe->Streams != NULL) {
        first = 1;
    }
    for (id = first; pipe != NULL && id <= last && !found; id++) {
        ring = hcdInjBulkRing(pipe, id, &p);
        if (ring == NULL || p->Queue->Head == NULL) {
            continue;
        }
        head = p->Queue->Head;
        *token = head->Token;
        *stream = id;
        *lastPA = XhciRingTrbPA(ring, head->LastIndex);
        *lastLen = XHCI_TRB_GET_LENGTH(ring->Base[head->LastIndex].Status);
        after = XhciRingNextIndex(ring, head->LastIndex);
        *afterPA = XhciRingTrbPA(ring, after);
        *afterCycle = (after > ring->Enqueue) ? (ring->Cycle ^ 1UL)
                                              : ring->Cycle;
        found = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return found;
}

/* A dequeue move on one stream (0: the endpoint), the layer's own command.
 * Returns its completion code. IRQL: PASSIVE_LEVEL, powered. */
static ULONG hcdInjBulkSetDequeue(PHCD_CONTROLLER hc, ULONG stream, ULONG pa,
                                  ULONG cycle)
{
    XHCI_TRB trb;
    PHCD_PIPE ep;
    KIRQL oldIrql;
    ULONG contract;
    ULONG built;

    /* Task 50.20 (record 17 section 4.12.10): never on an endpoint held to
     * the contract, whose streams get no Set TR Dequeue from anyone; the
     * attempt fails, and its caller abandons the injection. */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    ep = (hc->InjDev != NULL && hc->SlotDevice[hc->InjSlot] == hc->InjDev &&
          !hc->InjDev->Gone)
             ? hc->InjDev->Pipes[hc->InjDci]
             : NULL;
    contract = (ep != NULL && ep->StreamContract) ? 1UL : 0UL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (contract) {
        XhciLogNote(&hc->Hc, "qemu.inj.contract",
                    (hc->InjSlot << 16) | (hc->InjDci << 8) | stream);
        return XHCI_CC_INVALID;
    }
    if (stream == 0) {
        built = XhciTrbSetTrDequeue(&trb, hc->InjSlot, hc->InjDci, pa,
                                    cycle) == XHCI_RING_OK;
    } else {
        built = XhciStreamSetDequeueTrb(&trb, hc->InjSlot, hc->InjDci,
                                        stream, pa, cycle,
                                        XHCI_STREAM_SCT_PRIMARY_TR) ==
                XHCI_STREAM_OK;
    }
    return built ? hcdInjOwnCommand(hc, &trb) : XHCI_CC_INVALID;
}

/*
 * hcdInjStop for a bulk TD on the endpoint or one of its streams: the
 * layer's Stop Endpoint with its Stopped event swallowed, and the TD named
 * by `token` confirmed at the dequeue - the Endpoint Context's, or the
 * stream's Stream Context's - and put back with a dequeue move when the
 * controller fetched it ahead with nothing moved. Returns 1 with the TD's
 * first TRB and length and the swallowed event's code, pointer and
 * residual (*seen 0 when none came); otherwise the attempt is abandoned.
 * The doorbells stay withheld. IRQL: PASSIVE_LEVEL, powered.
 */
static ULONG hcdInjBulkStop(PHCD_CONTROLLER hc, ULONG stream, ULONG token,
                            PULONG firstPA, PULONG firstLen,
                            PULONG firstCycle, PULONG seen,
                            PULONG seenCode, PULONG seenPA,
                            PULONG seenResidual)
{
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    PXHCI_RING ring;
    PXHCI_TRANSFER head;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG code;
    ULONG verdict;
    ULONG same;
    ULONG first;
    ULONG len;
    ULONG dcs;
    ULONG deq;
    ULONG resync;

    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEpWatch(&hc->InjEp, hc->InjSlot, hc->InjDci);
    hc->InjEp.Bulk = 1;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    code = 0;
    if (XhciTrbStopEndpoint(&trb, hc->InjSlot, hc->InjDci, 0) ==
        XHCI_RING_OK) {
        code = hcdInjOwnCommand(hc, &trb);
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjEp.Watch = 0;
    *seen = hc->InjEp.StopSeen;
    *seenPA = hc->InjEp.StopPA;
    *seenCode = hc->InjEp.StopCode;
    *seenResidual = hc->InjEp.StopResidual;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (code != XHCI_CC_SUCCESS) {
        hcdInjBulkAbandon(hc, 0x80UL | (code & 0x7FUL));
        return 0;
    }

    verdict = XHCI_INJ_STOP_BROKEN;
    same = 0;
    first = 0;
    len = 0;
    dcs = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe = hcdInjBulkPipe(hc);
    ring = hcdInjBulkRing(pipe, stream, &p);
    head = (ring != NULL) ? p->Queue->Head : NULL;
    if (head != NULL) {
        same = (head->Token == token) ? 1UL : 0UL;
        first = XhciRingTrbPA(ring, head->FirstIndex);
        len = XHCI_TRB_GET_LENGTH(ring->Base[head->FirstIndex].Status);
        dcs = ring->Base[head->FirstIndex].Control & XHCI_TRB_CYCLE;
        deq = (stream == 0)
                  ? hcdInjCtxDequeue(hc, hc->InjSlot, hc->InjDci)
                  : (((volatile ULONG *)((PUCHAR)pipe->Streams->Va +
                                         stream * 16UL))[0] &
                     XHCI_EP_DEQUEUE_MASK);
        verdict = XhciInjStopVerdict(
            deq, first, XhciInjAfterPA(ring->BasePA, head->LastIndex),
            head->TrbCount == 1, len, *seen, *seenPA, *seenCode,
            *seenResidual);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);

    if (verdict == XHCI_INJ_STOP_REPOINT) {
        verdict = XHCI_INJ_STOP_BROKEN;
        if (hcdInjBulkSetDequeue(hc, stream, first, dcs) == XHCI_CC_SUCCESS) {
            KeAcquireSpinLock(&hc->InjLock, &oldIrql);
            hc->InjEp.Repointed++;
            KeReleaseSpinLock(&hc->InjLock, oldIrql);
            verdict = XHCI_INJ_STOP_OK;
        }
    }
    /* 36.54: the stream resync QEMU's xHCI owes after this stop (record 17
     * section 4.12.4), which the command path leaves to the layer's own
     * stop so the verdict above read the Stream Context as QEMU left it;
     * before anything is rung or delivered, the doorbells still withheld.
     * One that had to stop the endpoint again - QEMU restarted it on its
     * own, and may have moved the TD the verdict found untouched - or gave
     * up ends the attempt: abandoned (0x43) after a re-stop, given up with
     * nothing rung (0x44) when the resync failed and requested the
     * controller reset. */
    resync = HcdCfgQemuStreamResync(hc, hc->InjSlot, hc->InjDci);
    if (resync == HCD_RESYNC_FAILED) {
        hcdInjBulkGiveUp(hc);
        return 0;
    }
    if (resync != HCD_RESYNC_CLEAN) {
        hcdInjBulkAbandon(hc, 0x43UL);
        return 0;
    }
    if (verdict != XHCI_INJ_STOP_OK || !same) {
        /* Already fetched or moved on - QEMU's device wakeup can start a
         * stream behind a withheld doorbell: refused, not reported as
         * held. */
        hcdInjBulkAbandon(hc, (verdict << 1) | same);
        return 0;
    }
    *firstPA = first;
    *firstLen = len;
    *firstCycle = dcs ? 1UL : 0UL;
    return 1;
}


/*
 * BULK_WAIT's step: once a TD waits behind the withheld doorbell, the
 * armed fault is carried out on it. Returns the phase to go on in.
 * IRQL: PASSIVE_LEVEL, powered.
 */
static ULONG hcdInjBulkFire(PHCD_CONTROLLER hc)
{
    XHCI_TRB ev;
    KIRQL oldIrql;
    ULONG token;
    ULONG stream;
    ULONG lastPA;
    ULONG lastLen;
    ULONG afterPA;
    ULONG afterCycle;
    ULONG first;
    ULONG len;
    ULONG cycle;
    ULONG seen;
    ULONG seenCode;
    ULONG seenPA;
    ULONG seenResidual;
    ULONG fault;
    ULONG tries;
    ULONG delivered;

    fault = hc->InjFault;
    if (!hcdInjBulkHead(hc, &token, &stream, &lastPA, &lastLen, &afterPA,
                        &afterCycle)) {
        return HCD_INJ_PHASE_BULK_WAIT;
    }
    hc->InjBulkStream = stream;
    (VOID)XhciInjSpend(&hc->InjLeft);
    XhciLogNote(&hc->Hc, "qemu.inj.bulk",
                (fault << 24) | (stream << 16) | (hc->InjSlot << 8) |
                    hc->InjDci);
    if (fault == XHCI_INJ_BULK_SHORT && lastLen < 2) {
        hcdInjBulkAbandon(hc, 0x41UL);
        return HCD_INJ_PHASE_IDLE;
    }
    if (!hcdInjBulkStop(hc, stream, token, &first, &len, &cycle, &seen,
                        &seenCode, &seenPA, &seenResidual)) {
        return HCD_INJ_PHASE_IDLE;
    }
    UNREFERENCED_PARAMETER(cycle);
    switch (fault) {
    case XHCI_INJ_BULK_STOP:
        /* The stop the controller reported for the TD, handed to the driver as
         * it came; QEMU reports none for a TD it never fetched, so then the
         * event a stop at a TD's start gives: its first TRB, nothing moved. */
        if (seen && seenPA == first) {
            XhciInjTransferEvent(&ev, seenPA, seenResidual, seenCode,
                                 hc->InjSlot, hc->InjDci);
        } else {
            XhciInjTransferEvent(&ev, first, len, XHCI_CC_STOPPED,
                                 hc->InjSlot, hc->InjDci);
        }
        XhciLogNote(&hc->Hc, "qemu.inj.bulk.stopped",
                    (seen << 8) | (seen ? seenCode : 0UL));
        if (!hcdInjDeliver(hc, &ev)) {
            XhciLogNote(&hc->Hc, "qemu.inj.undelivered", fault);
        }
        hcdInjBulkRelease(hc);
        return HCD_INJ_PHASE_IDLE;
    case XHCI_INJ_BULK_ERROR:
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjEp.State = XHCI_EP_STATE_HALTED;
        hc->InjEp.Hold = XHCI_INJ_HOLD_NONE;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        XhciInjTransferEvent(&ev, first, len, XHCI_CC_USB_TRANSACTION_ERROR,
                             hc->InjSlot, hc->InjDci);
        if (!hcdInjDeliver(hc, &ev)) {
            hcdInjBulkAbandon(hc, 0x40UL);
            return HCD_INJ_PHASE_IDLE;
        }
        return HCD_INJ_PHASE_ANSWERING;
    case XHCI_INJ_BULK_SHORT:
        /* The controller past the TD before the driver retires it, so the
         * next doorbell starts the next TD and never this one again. */
        if (hcdInjBulkSetDequeue(hc, stream, afterPA, afterCycle) !=
            XHCI_CC_SUCCESS) {
            hcdInjBulkAbandon(hc, 0x42UL);
            return HCD_INJ_PHASE_IDLE;
        }
        /* On the TD's last TRB, which carries its IOC: one event ends it,
         * with no trailing event to follow (xHCI 4.10.1.1). The dequeue is
         * past the TD already, so the event must reach the driver: tried
         * again. */
        XhciInjTransferEvent(&ev, lastPA, lastLen / 2, XHCI_CC_SHORT_PACKET,
                             hc->InjSlot, hc->InjDci);
        delivered = 0;
        for (tries = 0; tries < 3 && !delivered; tries++) {
            delivered = hcdInjDeliver(hc, &ev);
        }
        if (!delivered) {
            /* Never rewound: a successor may have run meanwhile. The TD is
             * then a lost completion, as code 14's, for the driver's own
             * timeout. */
            XhciLogNote(&hc->Hc, "qemu.inj.undelivered", fault);
        }
        hcdInjBulkRelease(hc);
        return HCD_INJ_PHASE_IDLE;
    default:
        hcdInjBulkRelease(hc);
        return HCD_INJ_PHASE_IDLE;
    }
}

/* An endpoint fault armed: refused while another is armed or in flight, so
 * the guest ends one (CLEAR) before it aims the next. Returns 1 when
 * armed. IRQL: PASSIVE_LEVEL. */
static ULONG hcdInjArmEndpoint(PHCD_CONTROLLER hc, ULONG fault, ULONG port,
                               ULONG arg)
{
    KIRQL oldIrql;
    ULONG armed;

    armed = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjFault == 0 && hc->InjPhase == HCD_INJ_PHASE_IDLE &&
        hc->InjEp.Slot == 0 && hc->InjEp.Ep0Want == 0) {
        armed = 1;
        if (XhciInjKind(fault) == XHCI_INJ_KIND_EP0) {
            XhciInjEp0Arm(&hc->InjEp, fault, port, XhciInjLostCount(arg));
        } else {
            hc->InjFault = fault;
            hc->InjLeft = XhciInjLostCount(arg);
            hc->InjPort = port;
            hc->InjWaitNoted = 0;
        }
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return armed;
}

/* CLEAR's half for the endpoint faults: nothing further is injected, and
 * an injected event no drain has taken is withdrawn; an injection in
 * flight runs to its end, so no endpoint is left Stopped under a driver
 * that believes it halted. IRQL: PASSIVE_LEVEL. */
static VOID hcdInjDisarmEndpoint(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    hc->InjFault = 0;
    hc->InjLeft = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjEp0Arm(&hc->InjEp, 0, 0, 0);
    hc->InjEp.EvPending = 0;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    /* 36.22: a bulk doorbell still withheld with nothing emulated is
     * given back by the next thread pass, under the power gate with the
     * controller running (HcdInjService), never from here. */
    if (hc->InjPhase == HCD_INJ_PHASE_BULK_WAIT ||
        hc->InjPhase == HCD_INJ_PHASE_BULK_HELD ||
        hc->InjPhase == HCD_INJ_PHASE_BULK_LOSE) {
        hc->InjPhase = HCD_INJ_PHASE_BULK_CLEAR;
    }
    /* Nothing more is lost from now. */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->InjBulkLose = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* One command the trigger fired. IRQL: PASSIVE_LEVEL. */
/* Task 50.20 (record 17 section 4.12.10): fault 33's argument applied -
 * bit 0 this controller counted as listed, as 8086:1E31 is, so that
 * XhciStreamDequeueAvoid decides the flag as it would there (0 off, 1 and 2
 * on); bit 1 the QEMU resync off - and counted as the start latch counts
 * it. */
static VOID hcdInjStreamApply(PHCD_CONTROLLER hc, ULONG arg)
{
    PXHCI_QUIRK_STATE st;
    KIRQL oldIrql;

    if ((arg & XHCI_INJ_SU_FLAG) != 0) {
        st = &hc->Hc.Quirk;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        XhciQuirkSetEffective(st, XHCI_QK_DEQ_AVOID, 1,
                              XhciQuirkEffective(st->Mode[XHCI_QK_DEQ_AVOID],
                                                 1),
                              0);
        HcdQuirkPublishLocked(hc);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        hc->StreamSetDeqUntrusted = XhciQuirkOn(st, XHCI_QK_DEQ_AVOID);
    }
    if ((arg & XHCI_INJ_SU_RESYNC_OFF) != 0) {
        hc->QemuResyncOff = 1;
    }
    hc->Counters.StreamDeqUntrusted = hc->StreamSetDeqUntrusted;
    XhciLogNote(&hc->Hc, "qemu.inj.streamflag",
                (hc->StreamSetDeqUntrusted << 1) | hc->QemuResyncOff);
}

/* Fault 33 as the key holds it at the controller's start (the start's
 * value of every other fault is latched and never fires): the controller's
 * driver key, then on 2000 to 10 the machine-wide key. PASSIVE_LEVEL. */
VOID HcdInjStreamStart(PHCD_CONTROLLER hc)
{
    ULONG value;
    ULONG found;

    value = 0;
    found = NT_SUCCESS(HcdCtlQemuReadDword(hc, HCD_VALUE_QEMU_INJECT, &value))
                ? 1UL
                : 0UL;
    if ((!found || XHCI_INJ_GET_FAULT(value) != XHCI_INJ_STREAM_UNTRUSTED) &&
        IoIsWdmVersionAvailable(1, 0x10)) {
        value = 0;
        found = NT_SUCCESS(HcdCtlQemuReadMachineDword(HCD_VALUE_QEMU_INJECT,
                                                      &value))
                    ? 1UL
                    : 0UL;
    }
    if (found && XHCI_INJ_GET_FAULT(value) == XHCI_INJ_STREAM_UNTRUSTED) {
        hcdInjStreamApply(hc, XHCI_INJ_GET_ARG(value));
    }
}

/*
 * Task 50.34 (design record 16 section 21.10): fault 2C's argument, read as
 * HcdInjStreamStart reads fault 21 - the controller's key, then the
 * machine-wide key - and called only by a start that reaches the port
 * switchover's step (hcd_ctl.c), so no other controller or start reads it.
 * 0 when the value is absent or another fault. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdInjPswStep(PHCD_CONTROLLER hc)
{
    ULONG value;
    ULONG found;

    value = 0;
    found = NT_SUCCESS(HcdCtlQemuReadDword(hc, HCD_VALUE_QEMU_INJECT, &value))
                ? 1UL
                : 0UL;
    if ((!found || XHCI_INJ_GET_FAULT(value) != XHCI_INJ_PSW_STEP) &&
        IoIsWdmVersionAvailable(1, 0x10)) {
        value = 0;
        found = NT_SUCCESS(HcdCtlQemuReadMachineDword(HCD_VALUE_QEMU_INJECT,
                                                      &value))
                    ? 1UL
                    : 0UL;
    }
    if (found && XHCI_INJ_GET_FAULT(value) == XHCI_INJ_PSW_STEP) {
        XhciLogNote(&hc->Hc, "qemu.inj.psw", XHCI_INJ_GET_ARG(value));
        return XHCI_INJ_GET_ARG(value);
    }
    return 0;
}

static VOID hcdInjFire(PHCD_CONTROLLER hc, ULONG value)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG fault;
    ULONG arg;
    ULONG count;
    ULONG done;
    ULONG failed;

    ext = &hc->Hc;
    fault = XHCI_INJ_GET_FAULT(value);
    arg = XHCI_INJ_GET_ARG(value);
    done = 1;
    /* A fault aimed at a failed controller is refused: nothing would
     * meet it, and an all-ones answer would stand until CLEAR. CLEAR and
     * OC_RELEASE end faults and are always taken. */
    if (XhciInjNeedsLive(fault)) {
        XhciControllerLockAcquire(ext, &oldIrql);
        failed = ext->ControllerFailed ? 1UL : 0UL;
        XhciControllerLockRelease(ext, oldIrql);
        if (failed) {
            fault = XHCI_INJ_NONE;
            done = 0;
        }
    }
    switch (fault) {
    case XHCI_INJ_NONE:
        break;
    case XHCI_INJ_CLEAR:
        hcdInjClear(hc);
        hcdInjDisarmEndpoint(hc);
        break;
    case XHCI_INJ_LOST_IRQ:
        count = XhciInjLostCount(arg);
        if (count == 0xFFFFFFFFUL) {
            hcdInjIrqArm(hc, 0, 1);
        } else {
            hcdInjIrqArm(hc, count, 0);
        }
        break;
    case XHCI_INJ_OC_RELEASE:
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        done = hc->InjRegs.OcActive;
        XhciInjReleaseOc(&hc->InjRegs);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_CLEAR_HALT:
        /* arg 0 STALL, 1 another failure, 2 never sent. */
        if (arg <= 2) {
            hc->InjClearHalt = (arg == 0   ? HCD_CTL_STALLED
                                : arg == 1 ? HCD_CTL_FAILED
                                           : HCD_CTL_NOT_SENT) + 1;
        } else {
            done = 0;
        }
        break;
    case XHCI_INJ_SET_INTERFACE:
        /* arg 0 STALL, 1 not established. */
        if (arg <= 1) {
            hc->InjSetInterface = arg + 1;
        } else {
            done = 0;
        }
        break;
    case XHCI_INJ_STOP_RACE:
        /* Issue 26: arg Stops (0 one, 255 held to 255). */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjStopRace = (arg == 0) ? 1UL : arg;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_STALE_DEQUEUE:
        /* Task 49.39: arg sends (0 one). */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjStaleDeq = (arg == 0) ? 1UL : arg;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_RESTART_RACE:
        /* Task 49.39: arg doorbells (0 one). */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjRestartRace = (arg == 0) ? 1UL : arg;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_REPAIR_RACE:
        /* Task 49.39: arg doorbells at c1's resend (0 one). */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjRepairRace = (arg == 0) ? 1UL : arg;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_STREAM_UNTRUSTED:
        /* Task 50.20: read at the controller's start only (record 17
         * section 4.12.10, HcdInjStreamStart); fired later it changes
         * nothing and is noted. */
        XhciLogNote(&hc->Hc, "qemu.inj.streamflag.ignored", arg);
        break;
    case XHCI_INJ_PSW_STEP:
        /* Task 50.34: read by a start that reaches the port switchover's
         * step only (HcdInjPswStep). */
        XhciLogNote(&hc->Hc, "qemu.inj.psw.ignored", arg);
        break;
    case XHCI_INJ_RECYCLE_CFG:
        /* Task 50.20: the recycle's Configure, at attempt arg 3:0. */
        hc->InjRecycleCfg = arg + 1;
        hc->InjRecycleDci = XHCI_INJ_GET_PORT(value);
        hc->InjRecycleSkip = arg & 0xFUL;
        if (hc->InjRecycleSkip != 0) {
            hc->InjRecycleSkip--;
        }
        break;
    case XHCI_INJ_RENDEZVOUS:
        /* Task 50.20: arm a point; arg 0 disarms. The arming command's
         * sequence is kept: any later 23/0 is the release. */
        hc->InjRvPoint = arg;
        hc->InjRvReached = 0;
        hc->InjRvSeq = XHCI_INJ_GET_SEQ(value);
        break;
    case XHCI_INJ_ENUM_DESC:
        /* Task 50.7: arg reads (0 one, 255 until CLEAR) at the root port in
         * bits 15:8 (0 any). */
        hc->InjEnumDesc = (arg == 0) ? 1UL
                          : (arg == XHCI_INJ_ARG_FOREVER) ? 0xFFFFFFFFUL
                                                          : arg;
        hc->InjEnumDescPort = XHCI_INJ_GET_PORT(value);
        break;
    case XHCI_INJ_SELECT_CONFIG:
        /* arg 0/1 refused at step 3 / the rollback, 2/3 no completion. */
        if (arg <= 3) {
            hc->InjSelectConfig = arg + 1;
        } else {
            done = 0;
        }
        break;
    case XHCI_INJ_DEAD:
    case XHCI_INJ_DEAD_NOPROOF:
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjArmDead(&hc->InjRegs, arg, fault == XHCI_INJ_DEAD_NOPROOF);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_RETRY_FAIL:
    case XHCI_INJ_RETRY_ONES:
    case XHCI_INJ_RETRY_PORTSC_ONES:
    case XHCI_INJ_RETRY_HOLD:
    case XHCI_INJ_RECOVER_EARLY:
    case XHCI_INJ_RETRY_SWEEP_BREAK:
        /* Task 50.31 (design record 38 section 17); taken on a failed
         * controller too (XhciInjNeedsLive). */
        hcdInjRetryArm(hc, fault, XHCI_INJ_GET_PORT(value), arg);
        break;
    case XHCI_INJ_RESET_SLOW:
        /* Task 49.35: arg x 10 ms (255 for ever), port 0 HCRST, 1 CNR. */
        if (arg == 0 || XHCI_INJ_GET_PORT(value) > 1) {
            done = 0;
            break;
        }
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        hc->InjResetHoldMs = XhciInjResetHoldMs(arg);
        hc->InjResetCnr = XHCI_INJ_GET_PORT(value);
        hc->InjResetHolding = 0;
        hc->InjResetArmed = 1;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    case XHCI_INJ_ADDRESS_HANG:
    case XHCI_INJ_EP0_HANG:
    case XHCI_INJ_DEVICE_HANG:
    case XHCI_INJ_CFGEP_HANG:
    case XHCI_INJ_RESETEP_HANG:
    case XHCI_INJ_SETDEQ_HANG:
    case XHCI_INJ_EVAL_HANG:
    case XHCI_INJ_RESETDEV_HANG:
        /* Task 50.30 step 2 (record 17 section 4.15.9). */
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjHangArm(&hc->InjHang, fault, XHCI_INJ_GET_PORT(value), arg);
        hc->InjHangEvN = 0;
        hc->InjHangEvAt = 0;
        hc->InjHangEp0Slot = 0;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        if (hc->InjHang.Fault == 0) {
            /* 0x26 behind a hub: refused (xhci_inj.h). */
            XhciLogNote(&hc->Hc, "qemu.inj.hang.refused", value);
            break;
        }
        XhciLogNote(&hc->Hc, "qemu.inj.hang.armed",
                    (fault << 24) | (XHCI_INJ_GET_PORT(value) << 16) |
                        (hc->InjHang.ByBehaviour << 8) | arg);
        break;
    case XHCI_INJ_QUERY_STOP:
        /* Task 50.30 step 3 (record 17 section 4.15.16 E): armed or
         * refused there, with its own note. */
        done = hcdInjQsArm(hc, value);
        break;
    case XHCI_INJ_HC_EVENT:
        /* Task 49.35: one Host Controller Event, code arg, through the
         * drain - the escalation a release build met unrecorded. */
        {
            XHCI_TRB ev;

            XhciInjHostControllerEvent(&ev, arg);
            done = hcdInjDeliver(hc, &ev);
        }
        break;
    default:
        if (XhciInjKind(fault) == XHCI_INJ_KIND_BULK) {
            done = hcdInjArmBulk(hc, fault, XHCI_INJ_GET_PORT(value), arg);
        } else if (XhciInjKind(fault) != XHCI_INJ_KIND_OTHER) {
            done = hcdInjArmEndpoint(hc, fault, XHCI_INJ_GET_PORT(value),
                                     arg);
        } else {
            done = hcdInjHardware(hc, fault, XHCI_INJ_GET_PORT(value));
        }
        break;
    }
    if (done) {
        XHCI_DBG_VALUE("qemu inject: fired", value);
        XhciLogNote(ext, "qemu.inj.fire", value);
        if (fault == XHCI_INJ_PED || fault == XHCI_INJ_OC) {
            XhciLogNote(ext, "qemu.inj.port", done);
        }
    } else {
        XHCI_DBG_VALUE("qemu inject: refused", value);
        XhciLogNote(ext, "qemu.inj.refused", value);
    }
}

/* One reading of the trigger taken: XhciInjTake's sequence rule, then the
 * command carried out or the unknown code noted. IRQL: PASSIVE_LEVEL, the
 * controller thread, no lock held. */
static VOID hcdInjTakeValue(PHCD_CONTROLLER hc, ULONG value)
{
    switch (XhciInjTake(&hc->InjTrigger, 1, value)) {
    case XHCI_INJ_TAKE_FIRE:
        hcdInjFire(hc, value);
        break;
    case XHCI_INJ_TAKE_UNKNOWN:
        XHCI_DBG_VALUE("qemu inject: no such fault", value);
        XhciLogNote(&hc->Hc, "qemu.inj.unknown", value);
        break;
    default:
        break;
    }
}

/*
 * Each controller-thread pass, first in hcdPoll: the trigger reader's
 * readings taken (task 50.38; until then the thread read the key itself
 * about once a second here), a fresh command carried out, and the
 * interrupts the window has dropped since the last pass noted. Nothing is
 * taken before the first admission, 10 tolerance ticks after the start's
 * latch, the gate the thread's own first read had; readings made earlier
 * wait in the queue. IRQL: PASSIVE_LEVEL, no lock held.
 */
VOID HcdInjPoll(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG now;
    ULONG value;
    ULONG found;
    LONG dropped;
    KIRQL oldIrql;

    ext = &hc->Hc;
    /* Task 36.4: a hold that ran out with no command meeting the busy
     * engine has swallowed an interrupt whose acknowledgement cleared
     * IMAN.IE, and nothing else may drain it (the backstop is off at
     * XhciTolerance 0): one drain of its own, under the power gate and the
     * drain's admission, apart from the busy retry's count. */
    if (hc->InjStartHoldTicks != 0 && !HcdInjStartHeld(hc) &&
        InterlockedExchange((PLONG)&hc->InjStartHoldTicks, 0) != 0) {
        HcdPowerGateEnter(hc);
        found = HcdDrainPending(hc);
        HcdPowerGateLeave(hc);
        XhciLogNote(ext, "qemu.inj.starthold.expired", found);
    }
    /* The all-ones snapshot for this pass, before the health poll and the
     * containment step read it: spent here whether or not either reads,
     * so a finite fault ends on a failed controller too. */
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    (VOID)XhciInjDeadPass(&hc->InjRegs);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);

    dropped = hc->InjIrqDropped;
    if (dropped != hc->InjIrqNoted) {
        hc->InjIrqNoted = dropped;
        XHCI_DBG_VALUE("qemu inject: interrupts lost so far", (ULONG)dropped);
        XhciLogNote(ext, "qemu.inj.irq.lost", (ULONG)dropped);
    }
    /* A spent window is disarmed, so the ISR's count never runs on with
     * nothing armed (XhciInjIrqDrop is unsigned besides). */
    if (XhciInjIrqSpent((ULONG)hc->InjIrqArmed, (ULONG)hc->InjIrqForever,
                        (ULONG)hc->InjIrqTaken, (ULONG)hc->InjIrqBudget)) {
        hcdInjIrqArm(hc, 0, 0);
    }

    if (!hc->InjPollOpen) {
        now = HcdTolNow(hc);
        if (now - hc->InjReadAt < HCD_INJ_READ_TICKS &&
            now >= hc->InjReadAt) {
            return;
        }
        hc->InjPollOpen = 1;
    }
    /* Oldest first, at most what was queued at this look plus what the
     * reader adds meanwhile, up to the queue's depth. */
    for (found = 0; found < HCD_INJ_POLL_DEPTH; found++) {
        value = 0;
        if (!hcdInjQueueTake(hc, &value)) {
            break;
        }
        hcdInjTakeValue(hc, value);
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    dropped = (LONG)hc->InjPollLost;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if ((ULONG)dropped != hc->InjPollLostNoted) {
        hc->InjPollLostNoted = (ULONG)dropped;
        XHCI_DBG_VALUE("qemu inject: readings dropped so far",
                       (ULONG)dropped);
        XhciLogNote(ext, "qemu.inj.reader.lost", (ULONG)dropped);
    }
}

/* ----------------------------------------------------------------------- */
/* Task 50.38: the trigger reader (design record 17 section 5)               */
/* ----------------------------------------------------------------------- */

/* The low part of the system time, in 100 ns units; a difference of two
 * is good for some seven minutes, far more than any period here. IRQL:
 * any. */
static ULONG hcdInjReaderNow(VOID)
{
    LARGE_INTEGER now;

    KeQuerySystemTime(&now);
    return now.LowPart;
}

/* The reader's end: the event set last and the thread gone; on 98 SE and
 * ME, where the stop joins the event alone, set from outside the image
 * (record 15 section 4.5.22). Never returns. IRQL: PASSIVE_LEVEL, the
 * reader. */
static VOID hcdInjReaderExit(PHCD_CONTROLLER hc)
{
#if defined(_M_IX86)
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        HcdThreadEnd9x(HCD_END9X_READER, &hc->InjPollExited, NULL);
    }
#endif
    (VOID)KeSetEvent(&hc->InjPollExited, IO_NO_INCREMENT, FALSE);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * The trigger reader: XhciQemuInject read about once a second (every
 * 100 ms while a rendezvous holds) off the controller thread, which a read
 * that did not return held on 98 SE with a cancel waiting on it (50.38's
 * kind A, s5v07 and s5v20). Each period runs from one read's start to the
 * next, whatever the controller thread is doing. A value read that differs
 * from the one queued last is queued (the oldest dropped and counted when
 * the queue is full) and the controller thread woken; the thread takes
 * every queued reading in order at its next pass. The keys are the controller
 * thread's own until then: on 2000 to 10 the machine-wide key alone (no
 * IoOpenDeviceRegistryKey from a thread of the driver; task 49.24), on 98
 * SE and ME both. IRQL: PASSIVE_LEVEL (a system thread), no lock held
 * across the read.
 */
static VOID NTAPI hcdInjReader(PVOID context)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    NTSTATUS status;
    KIRQL oldIrql;
    ULONG started;
    ULONG elapsed;
    ULONG period;
    ULONG value;
    ULONG found;

    hc = (PHCD_CONTROLLER)context;
    started = hcdInjReaderNow();
    for (;;) {
        period = (hc->InjPollFast != 0) ? HCD_INJ_POLL_FAST_MS
                                        : HCD_INJ_POLL_MS;
        elapsed = (hcdInjReaderNow() - started) / 10000UL;
        HcdRelativeMs(&due, (elapsed < period) ? period - elapsed : 0UL);
        /* The kick (a rendezvous's entry, the stop) ends the wait early;
         * the time-out is the deadline. */
        status = KeWaitForSingleObject(&hc->InjPollKick, Executive,
                                       KernelMode, FALSE, &due);
        if (hc->InjPollStop) {
            break;
        }
        if (status != STATUS_SUCCESS && status != STATUS_TIMEOUT) {
            /* Windows 98 can end the wait at once, unsatisfied
             * (HcdWaitEvent): 1 ms yielded, the same deadline again. */
            HcdRelativeMs(&due, 1);
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
            continue;
        }
        started = hcdInjReaderNow();
        value = 0;
        if (IoIsWdmVersionAvailable(1, 0x10)) {
            found = NT_SUCCESS(HcdCtlQemuReadMachineDword(
                        HCD_VALUE_QEMU_INJECT, &value))
                        ? 1UL
                        : 0UL;
        } else {
            found = NT_SUCCESS(HcdCtlQemuReadDword(hc, HCD_VALUE_QEMU_INJECT,
                                                   &value))
                        ? 1UL
                        : 0UL;
        }
        if (!found ||
            (hc->InjPollLastValid && value == hc->InjPollLast)) {
            continue;
        }
        hc->InjPollLast = value;
        hc->InjPollLastValid = 1;
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        if (hc->InjPollCount == HCD_INJ_POLL_DEPTH) {
            hc->InjPollHead = (hc->InjPollHead + 1) % HCD_INJ_POLL_DEPTH;
            hc->InjPollCount--;
            hc->InjPollLost++;
        }
        hc->InjPollQ[(hc->InjPollHead + hc->InjPollCount) %
                     HCD_INJ_POLL_DEPTH] = value;
        hc->InjPollCount++;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        HcdThreadWake(hc);
    }
    hcdInjReaderExit(hc);
}

/* hcd.h. The reader made, from hcdThreadStart once the controller thread
 * runs (HcdInjStart has set its state); on 2000 to 10 its object had, the
 * reference retried until had (record 15 section 4.5.21). A creation that
 * fails is noted and the lifetime goes on with no trigger read. IRQL:
 * PASSIVE_LEVEL, the start. */
VOID HcdInjReaderStart(PHCD_CONTROLLER hc)
{
    HANDLE handle;
    NTSTATUS status;
    ULONG failed;

    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL,
                                  NULL, hcdInjReader, hc);
    if (!NT_SUCCESS(status)) {
        XHCI_DBG_VALUE("qemu inject: no trigger reader, status",
                       (ULONG)status);
        XhciLogNote(&hc->Hc, "qemu.inj.reader.none", (ULONG)status);
        return;
    }
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        failed = HcdThreadReference(hc, handle, &hc->InjPollObject,
                                    "qemu.inj.reader.ref.fail",
                                    L"XhciQemuThreadRefReader");
        if (failed != 0) {
            XhciLogNote(&hc->Hc, "qemu.inj.reader.ref.retry", failed);
        }
    } else {
        (VOID)ZwClose(handle);
    }
    hc->InjPollRunning = 1;
}

/* hcd.h. The reader told to leave and joined, from hcdThreadStop after the
 * controller thread's join: on 2000 to 10 by its object, on 98 SE and ME by
 * the event it sets from outside the image. A read in progress is waited
 * for. IRQL: PASSIVE_LEVEL, the stop. */
VOID HcdInjReaderStop(PHCD_CONTROLLER hc)
{
    if (!hc->InjPollRunning) {
        return;
    }
    (VOID)InterlockedExchange((PLONG)&hc->InjPollStop, 1);
    (VOID)KeSetEvent(&hc->InjPollKick, IO_NO_INCREMENT, FALSE);
    if (hc->InjPollObject != NULL) {
        (VOID)KeWaitForSingleObject(hc->InjPollObject, Executive, KernelMode,
                                    FALSE, NULL);
        ObDereferenceObject(hc->InjPollObject);
        hc->InjPollObject = NULL;
    } else {
        (VOID)HcdWaitEvent(&hc->InjPollExited, 0);
    }
    XHCI_DBG_TEXT("qemu inject: trigger reader has exited");
    hc->InjPollRunning = 0;
}

/* ----------------------------------------------------------------------- */
/* Task 50.30 step 2: a device or a command that stops answering            */
/* (design record 17 section 4.15.9; xhci_inj.h XHCI_INJ_HANG)              */
/* ----------------------------------------------------------------------- */

/* The place of the device a command or a transfer names: its root port and
 * route, 0 and 0 when no record holds the slot. Controller lock held. */
static VOID hcdInjHangPlace(PHCD_CONTROLLER hc, ULONG slot, PULONG port,
                            PULONG route)
{
    PHCD_USB_DEVICE dev;

    *port = 0;
    *route = 0;
    if (slot == 0 || slot > XHCI_MAX_SLOTS) {
        return;
    }
    dev = hc->SlotDevice[slot];
    if (dev != NULL) {
        *port = dev->Port;
        *route = dev->Route;
    }
}

/* hcd.h. A command that fits is marked for the submit about to be made
 * (its type and slot); nothing is spent here. The doorbell of that very
 * command, once the engine has placed it, spends the count and withholds
 * (HcdInjCmdDoorbell); HcdInjHangSubmitted ends the mark whatever the
 * submit answered, so a refused or busy submission leaves nothing for a
 * later command's doorbell to take. IRQL: PASSIVE_LEVEL (the thread,
 * before the submit). */
VOID HcdInjHangCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb)
{
    KIRQL oldIrql;
    KIRQL lockIrql;
    ULONG port;
    ULONG route;
    ULONG type;
    ULONG slot;

    if (hc->InjHang.Fault == 0 || hc->InjOwnCmd) {
        return;
    }
    type = XHCI_TRB_GET_TYPE(trb->Control);
    slot = XHCI_TRB_GET_SLOT_ID(trb->Control);
    XhciControllerLockAcquire(&hc->Hc, &lockIrql);
    hcdInjHangPlace(hc, slot, &port, &route);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (XhciInjHangCmdFits(&hc->InjHang, type, port, route)) {
        hc->InjHang.HoldNext = 1;
        hc->InjHang.HoldType = type;
        hc->InjHang.HoldSlot = slot;
        hc->InjHang.HoldRec = 0;
    } else if (XhciInjHangRecoveryFits(&hc->InjHang, type, slot,
                                       XHCI_TRB_GET_EP_ID(trb->Control))) {
        /* Step 3 (record 17 section 4.15.16 C): the latched slot's
         * recovery command, matched by its slot rather than its place,
         * since the device's record may already be going. */
        hc->InjHang.HoldNext = 1;
        hc->InjHang.HoldType = type;
        hc->InjHang.HoldSlot = slot;
        hc->InjHang.HoldRec = 1;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciControllerLockRelease(&hc->Hc, lockIrql);
}

/* hcd.h. The submit has answered (placed, refused or busy): the mark ends.
 * IRQL: PASSIVE_LEVEL (the thread, after the submit). */
VOID HcdInjHangSubmitted(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (!hc->InjHang.HoldNext) {
        return;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    hc->InjHang.HoldNext = 0;
    hc->InjHang.HoldType = 0;
    hc->InjHang.HoldSlot = 0;
    hc->InjHang.HoldRec = 0;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
}

/* hcd_svc.h. The command doorbell: withheld only when the command the
 * engine has just made outstanding is the marked one (its type and slot,
 * from the submit that placed it), and only then is the count spent.
 * IRQL: DISPATCH_LEVEL, controller lock held (the submit's). */
ULONG HcdInjCmdDoorbell(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG held;
    ULONG rec;
    ULONG type;
    ULONG port;
    ULONG route;
    ULONG slot;
    ULONG trig;
    ULONG window;

    hc = HcdControllerFromExt(ext);
    if (!hc->InjHang.HoldNext) {
        return 0;
    }
    held = 0;
    rec = 0;
    type = 0;
    port = 0;
    slot = 0;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    if (hc->InjHang.HoldNext && ext->CommandTrbPA != 0 &&
        ext->CommandType == hc->InjHang.HoldType &&
        XHCI_TRB_GET_SLOT_ID(ext->CommandControl) == hc->InjHang.HoldSlot) {
        slot = hc->InjHang.HoldSlot;
        type = hc->InjHang.HoldType;
        if (hc->InjHang.HoldRec) {
            /* Step 3: the recovery command, consumed only here, at the
             * doorbell of the command the engine has placed. */
            if (XhciInjHangRecoveryTake(
                    &hc->InjHang, type, slot,
                    XHCI_TRB_GET_EP_ID(ext->CommandControl))) {
                held = 1;
                rec = 1;
                hc->InjHang.HeldPA = ext->CommandTrbPA;
            }
        } else {
            hcdInjHangPlace(hc, slot, &port, &route);
            if (XhciInjHangCmd(&hc->InjHang, type, port, route)) {
                held = 1;
                hc->InjHang.HeldPA = ext->CommandTrbPA;
                XhciInjHangLatch(&hc->InjHang, slot, HcdTolNow(hc));
            }
        }
        hc->InjHang.HoldNext = 0;
        hc->InjHang.HoldType = 0;
        hc->InjHang.HoldSlot = 0;
        hc->InjHang.HoldRec = 0;
    }
    /* Record 17 section 4.15.16 E: fault 2D's arming taken by this
     * withholding, in this hold of both locks; with bit 7 the route's bit
     * raised here, so the thread's next look leaves the wait. */
    trig = 0;
    window = 0;
    if (held && XhciInjQsTrigger(&hc->InjQs, slot, ext->CommandTrbPA,
                                 HcdTolNow(hc))) {
        trig = 1;
        if ((hc->InjQs.Arg & XHCI_INJ_QS_WINDOW) != 0 &&
            hc->IsolationApplies) {
            hc->CmdWaitLeave |= HCD_LEAVE_QTEST;
            window = 1;
        }
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (rec) {
        XHCI_DBG_VALUE("qemu inject: recovery command withheld, slot/type",
                       (slot << 8) | type);
        XhciLogNoteLocked(ext, "qemu.inj.hang.recovery",
                          (hc->InjHang.Fault << 24) | (slot << 16) |
                              (type << 8) | hc->InjHang.Outcome);
    } else if (held) {
        XhciLogNoteLocked(ext, "qemu.inj.hang",
                          (hc->InjHang.Fault << 24) | (port << 16) |
                              (slot << 8) | hc->InjHang.Outcome);
    }
    if (trig) {
        KeClearEvent(&hc->InjQsIdle);
        if (window) {
            (VOID)KeSetEvent(&hc->CmdDoneEvent, IO_NO_INCREMENT, FALSE);
        }
        XHCI_DBG_VALUE("qemu inject: query-stop exercise taken, slot/window",
                       (slot << 8) | window);
        XhciLogNoteLocked(ext, "qemu.inj.qstop.trigger",
                          (slot << 16) | (type << 8) | hc->InjQs.Arg);
    }
    return held;
}

/* hcd_svc.h. IRQL: DISPATCH_LEVEL, controller lock held (the watchdog's
 * rung 1, CA just written). */
VOID HcdInjCmdAbortWritten(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    XHCI_TRB ev[2];
    KIRQL oldIrql;
    ULONG count;
    ULONG rewrite;
    ULONG ring;
    ULONG held;
    ULONG index;
    ULONG badPA;

    hc = HcdControllerFromExt(ext);
    if (hc->InjHang.HeldPA == 0) {
        return;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    held = hc->InjHang.HeldPA;
    if (held != ext->CommandTrbPA) {
        /* Not the command it withheld: nothing to act out. */
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        return;
    }
    badPA = XhciRingTrbPA(&ext->CommandRing, ext->CommandRing.Trbs - 1) +
            2UL * XHCI_TRB_BYTES;
    count = XhciInjHangEvents(XhciInjHangActOutcome(hc->InjHang.Outcome),
                              held,
                              held + XHCI_TRB_BYTES, badPA, ev, &rewrite,
                              &ring);
    hc->InjHang.HeldPA = 0;
    if (rewrite &&
        XhciRingIndexFromPA(&ext->CommandRing, held, &index) == XHCI_RING_OK &&
        XhciRingNoOpAtType(&ext->CommandRing, index,
                           XHCI_TRB_TYPE_NOOP_COMMAND) == XHCI_RING_OK) {
        /* The layer's own No Op, never the engine's marker: its
         * completion is swallowed here (HcdInjEventSwallow). */
        hc->InjHang.NoOpPA = held;
    }
    if (ring) {
        hc->InjHang.StopOwedPA = held;
    }
    hc->InjHangEvN = count;
    hc->InjHangEvAt = 0;
    if (count > 0) {
        hc->InjHangEv[0] = ev[0];
    }
    if (count > 1) {
        hc->InjHangEv[1] = ev[1];
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciLogNoteLocked(ext, "qemu.inj.outcome",
                      (hc->InjHang.Outcome << 8) | count);
    if (ring) {
        XhciWriteDoorbell(ext, XHCI_DB_COMMAND, 0);
    }
    if (count > 0) {
        hcdInjQueueDrain(hc);
    }
}

/* hcd.h. IRQL: PASSIVE_LEVEL (the thread's late path). */
VOID HcdInjHangLate(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG armed;
    ULONG withheld;

    if (hc->InjHang.Fault != XHCI_INJ_DEVICE_HANG) {
        return;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    withheld = (hc->InjHangEp0Slot == dev->SlotId) ? 1UL : 0UL;
    armed = XhciInjHangLate(&hc->InjHang, dev->Port, dev->Route, withheld);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (armed) {
        XhciLogNote(&hc->Hc, "qemu.inj.armed",
                    (dev->Port << 16) | (dev->SlotId << 8));
    }
}

/*
 * hcd.h. Step 3 (design record 17 section 4.15.16 C): outcome 7's mark
 * deadline, looked at by the controller thread's own EP0 wait for `dev`
 * at each look, before that look takes the controller lock. Armed for this
 * device's slot and passed, it is disarmed under the injection lock (one
 * shot); then, that lock let go, the cycle is marked through the one entry
 * point every mark takes (HcdTolCycleMark, reason REFUSED_CODE, DCI 1)
 * under the controller lock, only while the record is still the slot's.
 * The same wait's look resolves the mark. No callback exists, so nothing
 * outlives the wait to be drained. IRQL: PASSIVE_LEVEL, the controller
 * thread, no lock held.
 */
VOID HcdInjWaitLook(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG slot;
    ULONG due;
    ULONG marked;

    if (hc->InjHang.MarkSlot == 0) {
        return;
    }
    slot = dev->SlotId;
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    due = XhciInjHangMarkDue(&hc->InjHang, slot, HcdTolNow(hc));
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (!due) {
        return;
    }
    marked = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (slot != 0 && slot <= XHCI_MAX_SLOTS && hc->SlotDevice[slot] == dev) {
        HcdTolCycleMark(hc, dev, XHCI_TOL_CYCLE_REFUSED_CODE, 1);
        marked = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XHCI_DBG_VALUE("qemu inject: mark deadline in the wait, slot", slot);
    XhciLogNote(&hc->Hc, "qemu.inj.hang.mark", (slot << 8) | marked);
}

/* hcd.h. IRQL: PASSIVE_LEVEL (the thread's port inspection). */
VOID HcdInjHangPortGone(PHCD_CONTROLLER hc, ULONG port)
{
    KIRQL oldIrql;
    ULONG ended;

    if (hc->InjHang.Fault != XHCI_INJ_DEVICE_HANG) {
        return;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    ended = XhciInjHangGone(&hc->InjHang, port);
    if (ended) {
        hc->InjHangEp0Slot = 0;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (ended) {
        XhciLogNote(&hc->Hc, "qemu.inj.disarm", port);
    }
}

/* ----------------------------------------------------------------------- */
/* Task 50.30 step 3: fault 2D, the query-stop under a debt, a driver-path  */
/* exercise (design record 17 section 4.15.16 E; xhci_inj.h XHCI_INJ_QSTOP) */
/* ----------------------------------------------------------------------- */

/* The routes whose bit standing keeps the query from being committed. */
#define HCD_INJ_QS_ROUTES                                                    \
    (HCD_LEAVE_STOP | HCD_LEAVE_REMOVE | HCD_LEAVE_POWER | HCD_LEAVE_DIRECT | \
     HCD_LEAVE_SHUT)

/* How an exercise ended (qemu.inj.qstop.end, bits 15:8). */
#define HCD_INJ_QS_END_CANCEL   1UL  /* the thread's cancel, or none owed */
#define HCD_INJ_QS_END_HANDOFF  2UL  /* a terminal request's hand-off     */
#define HCD_INJ_QS_END_NODEBT   3UL  /* bit 7 and no debt formed          */
#define HCD_INJ_QS_END_ABORTED  4UL  /* the thread, before any call       */
#define HCD_INJ_QS_END_BRACKET  5UL  /* a bracket, before any call        */

/* A start, before the thread runs: IDLE, not closed, the route's bit of
 * any earlier lifetime gone; Busy as it is. IRQL: PASSIVE_LEVEL. */
static VOID hcdInjQsStart(PHCD_CONTROLLER hc)
{
    KIRQL lockIrql;
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &lockIrql);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjQsStart(&hc->InjQs);
    hc->CmdWaitLeave &= ~HCD_LEAVE_QTEST;
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    (VOID)KeSetEvent(&hc->InjQsIdle, IO_NO_INCREMENT, FALSE);
    XhciControllerLockRelease(&hc->Hc, lockIrql);
}

/* Fault 2D fired: armed, or refused with its reason (noted). Returns 1 when
 * armed. IRQL: PASSIVE_LEVEL (the thread's injection poll). */
static ULONG hcdInjQsArm(PHCD_CONTROLLER hc, ULONG value)
{
    PXHCI_EXTENSION ext;
    KIRQL lockIrql;
    KIRQL oldIrql;
    ULONG reason;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &lockIrql);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    reason = XhciInjQsArmRefusal(
        &hc->InjQs,
        (hc->ControllerStarted && (ext->Flags & XHCI_EXT_FLAG_STARTED) != 0)
            ? 1UL
            : 0UL,
        (hc->Msg4System == XHCI_MSG4_SYS_NT5 && hc->Msg4Info != NULL) ? 1UL
                                                                       : 0UL,
        hc->IsolationApplies ? 1UL : 0UL);
    if (reason == XHCI_INJ_QS_R_OK) {
        XhciInjQsArm(&hc->InjQs, XHCI_INJ_GET_ARG(value));
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (reason == XHCI_INJ_QS_R_OK) {
        KeClearEvent(&hc->InjQsIdle);
    }
    XhciControllerLockRelease(ext, lockIrql);
    if (reason != XHCI_INJ_QS_R_OK) {
        XHCI_DBG_VALUE("qemu inject: query-stop exercise refused, reason",
                       reason);
        XhciLogNote(ext, "qemu.inj.qstop.refused", reason);
        return 0;
    }
    XhciLogNote(ext, "qemu.inj.qstop.armed", XHCI_INJ_GET_ARG(value));
    return 1;
}

/* An exercise ended: noted with how, and on the debugcon line at once, in
 * the hold of the controller lock that makes it IDLE, so no bracket can
 * read IDLE and write a note before this one. IRQL: DISPATCH_LEVEL, the
 * controller lock held, InjLock not held. */
static VOID hcdInjQsEndNoteLocked(PHCD_CONTROLLER hc, ULONG how,
                                  ULONG released)
{
    XHCI_DBG_VALUE("qemu inject: query-stop exercise ended, how/released",
                   (how << 8) | released);
    XhciLogNoteLocked(&hc->Hc, "qemu.inj.qstop.end", (how << 8) | released);
}

/*
 * hcd.h. The thread's step: at most one driver call a turn, each committed
 * in one hold of the controller lock and the injection lock inside it
 * (XhciInjQsStep), made with both let go, and recorded in one hold again.
 * The query and the cancel are the driver's own (HcdMsg4QueryStop,
 * HcdMsg4CancelStop), called with no IRP: no PnP state is touched, nothing
 * is forwarded or completed. An end with no call - bit 7 and no debt, a
 * terminal request's hand-off - is decided, its route's bit cleared, its
 * notes written and InjQsIdle set in one hold of the controller lock, and
 * the cancel's end likewise in the hold that records it: a bracket reads
 * the state under that lock, so whether it waited or found IDLE, its notes
 * follow the exercise's last. IRQL: PASSIVE_LEVEL, the controller thread,
 * no lock and no gate held.
 */
VOID HcdInjExercise(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    NTSTATUS status;
    KIRQL lockIrql;
    KIRQL oldIrql;
    ULONG action;
    ULONG ready;
    ULONG debt;
    ULONG released;
    ULONG leftNote;

    if (hc->InjQs.State != XHCI_INJ_QS_TRIGGERED &&
        hc->InjQs.State != XHCI_INJ_QS_QUERIED) {
        return;
    }
    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &lockIrql);
    ready = (hc->ControllerStarted &&
             (ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
             hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0 &&
             (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
             (hc->CmdWaitLeave & HCD_INJ_QS_ROUTES) == 0)
                ? 1UL
                : 0UL;
    leftNote = ((hc->Left.Owed & 0xFFUL) << 24) |
               ((hc->Left.Slot & 0xFFUL) << 16);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    debt = XhciInjQsDebtFits(&hc->InjQs,
                             hc->Left.Owed != XHCI_LEFT_CLEAR ? 1UL : 0UL,
                             hc->Left.Slot, hc->Left.PA);
    /* Before the step, which a hand-off clears it in. */
    released = hc->InjQs.Released;
    action = XhciInjQsStep(&hc->InjQs, HcdTolNow(hc), ready, debt);
    leftNote |= (hc->InjQs.HeldSlot & 0xFFUL) << 8;
    if (action == XHCI_INJ_QS_NODEBT || action == XHCI_INJ_QS_HANDOFF ||
        action == XHCI_INJ_QS_ENDED) {
        hc->CmdWaitLeave &= ~HCD_LEAVE_QTEST;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    switch (action) {
    case XHCI_INJ_QS_NODEBT:
        /* The withheld command's watchdog won, or the debt names another
         * command: an unsuccessful arrangement, never a query under a
         * debt. */
        XhciLogNoteLocked(ext, "qemu.inj.qstop.nodebt", leftNote);
        hcdInjQsEndNoteLocked(hc, HCD_INJ_QS_END_NODEBT, 0);
        (VOID)KeSetEvent(&hc->InjQsIdle, IO_NO_INCREMENT, FALSE);
        break;
    case XHCI_INJ_QS_HANDOFF:
        /* A stop or a removal takes the released query over as a real
         * query that succeeded leaves it: no reconnection. */
        hcdInjQsEndNoteLocked(hc, HCD_INJ_QS_END_HANDOFF, released);
        (VOID)KeSetEvent(&hc->InjQsIdle, IO_NO_INCREMENT, FALSE);
        break;
    case XHCI_INJ_QS_ENDED:
        hcdInjQsEndNoteLocked(hc, HCD_INJ_QS_END_ABORTED, 0);
        (VOID)KeSetEvent(&hc->InjQsIdle, IO_NO_INCREMENT, FALSE);
        break;
    default:
        break;
    }
    XhciControllerLockRelease(ext, lockIrql);

    switch (action) {
    case XHCI_INJ_QS_QUERY:
        status = HcdMsg4QueryStop(hc);
        released = (NT_SUCCESS(status) && hc->Msg4QueryReleased) ? 1UL : 0UL;
        XHCI_DBG_VALUE("qemu inject: query-stop made, status",
                       (ULONG)status);
        XhciLogNote(ext, "qemu.inj.qstop", (ULONG)status);
        XhciControllerLockAcquire(ext, &lockIrql);
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjQsQueried(&hc->InjQs, released, HcdTolNow(hc));
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        XhciControllerLockRelease(ext, lockIrql);
        return;
    case XHCI_INJ_QS_CANCEL:
        /* A refused query has reconnected and cleared its own bit
         * already: no cancel is owed, and none is made. */
        if (released) {
            HcdMsg4CancelStop(hc);
            XHCI_DBG_VALUE("qemu inject: query-stop cancelled, lost",
                           hc->Msg4Lost);
            XhciLogNote(ext, "qemu.inj.qcancel", hc->Msg4Lost);
        }
        XhciControllerLockAcquire(ext, &lockIrql);
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjQsEnded(&hc->InjQs);
        hc->CmdWaitLeave &= ~HCD_LEAVE_QTEST;
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        hcdInjQsEndNoteLocked(hc, HCD_INJ_QS_END_CANCEL, released);
        (VOID)KeSetEvent(&hc->InjQsIdle, IO_NO_INCREMENT, FALSE);
        XhciControllerLockRelease(ext, lockIrql);
        return;
    default:
        return;
    }
}

/*
 * hcd.h. A real request's bracket, at its dispatch's entry: Busy raised,
 * Abort set, and for a terminal request Closed and Terminal, in one hold
 * of both locks. An exercise armed or triggered is ended here, with no
 * call; one in a call or queried is waited for - the thread woken, which
 * finishes the call it committed and at its next turn ends the exercise,
 * by the cancel for a request that is not terminal and by the hand-off
 * for one that is. Nothing the thread needs is held while waiting. Every
 * bracket notes qemu.inj.qstop.real (the kind, the state it found, its
 * answer) once its wait is over, and one that waited
 * qemu.inj.qstop.waited (ms). IRQL: PASSIVE_LEVEL, no lock, no door gate
 * and no power gate held.
 */
VOID HcdInjRealEnter(PHCD_CONTROLLER hc, ULONG kind, ULONG terminal)
{
    PXHCI_EXTENSION ext;
    LARGE_INTEGER before;
    LARGE_INTEGER after;
    KIRQL lockIrql;
    KIRQL oldIrql;
    ULONG found;
    ULONG answer;
    ULONG waited;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &lockIrql);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    found = hc->InjQs.State;
    answer = XhciInjQsBracket(&hc->InjQs, terminal ? 1UL : 0UL);
    if (answer == XHCI_INJ_QS_BR_ENDED) {
        hc->CmdWaitLeave &= ~HCD_LEAVE_QTEST;
    }
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    if (answer == XHCI_INJ_QS_BR_ENDED) {
        hcdInjQsEndNoteLocked(hc, HCD_INJ_QS_END_BRACKET, 0);
        (VOID)KeSetEvent(&hc->InjQsIdle, IO_NO_INCREMENT, FALSE);
    }
    XhciControllerLockRelease(ext, lockIrql);
    if (answer == XHCI_INJ_QS_BR_WAIT) {
        KeQuerySystemTime(&before);
        HcdThreadWake(hc);
        (VOID)HcdWaitEvent(&hc->InjQsIdle, 0);
        KeQuerySystemTime(&after);
        waited = (after.LowPart - before.LowPart) / 10000UL;
        XhciLogNote(ext, "qemu.inj.qstop.waited", waited);
    }
    if (answer != XHCI_INJ_QS_BR_NOTHING) {
        XHCI_DBG_VALUE("qemu inject: query-stop bracket, kind/found/answer",
                       ((kind & 0xFFUL) << 16) | ((found & 0xFFUL) << 8) |
                           answer);
    }
    XhciLogNote(ext, "qemu.inj.qstop.real",
                ((kind & 0xFFUL) << 16) | ((found & 0xFFUL) << 8) | answer);
}

/* hcd.h. Where the driver's own handling of a bracketed request ends.
 * IRQL: <= DISPATCH_LEVEL, no lock held. */
VOID HcdInjRealLeave(PHCD_CONTROLLER hc)
{
    KIRQL lockIrql;
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &lockIrql);
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    XhciInjQsLeave(&hc->InjQs);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    XhciControllerLockRelease(&hc->Hc, lockIrql);
}

#else /* release: injection is not in the image */

typedef int hcdInjNotInImage;

#endif
