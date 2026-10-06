/*
 * hcd_inj.c - 35-T.9's fault injection, the driver half (xhci_inj.h has the
 * pure half, the trigger's encoding and the reserved codes). Design record
 * 17 section 5.
 *
 * QEMU flavour only: src/sources defines XHCI_FLAVOUR_QEMU for that build
 * alone, and the debug and release images carry none of this file, its
 * value name or its hooks (scripts\check-flavour-marker.ps1 names the
 * flavour; the "qemu.inj" strings are absent from both shipping images).
 * It adds no import: the registry read is hcd_ctl.c's, the rest is the
 * spin lock, interlocked and register calls every flavour already makes.
 *
 * The faults built here are the ones whose driver behaviour is in the tree:
 *
 *   LOST_IRQ      the ISR acknowledges as ever and does not queue the drain,
 *                 for `arg` interrupts (255: until CLEAR); 35-T.1's backstop
 *                 is what delivers the event that waits in the ring
 *   PED           a real write of PED 1 to a USB 2.0 root port, PEC answered
 *                 set in its PORTSC reads until the driver acknowledges it,
 *                 and a Port Status Change Event for the port handed to the
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

/* The trigger is read once per ten tolerance ticks, about a second. */
#define HCD_INJ_READ_TICKS 10UL

/* An endpoint fault's phase (InjPhase). */
#define HCD_INJ_PHASE_IDLE      0UL  /* ready to inject while one is left  */
#define HCD_INJ_PHASE_ANSWERING 1UL  /* until the answers end              */
#define HCD_INJ_PHASE_STALE     2UL  /* until the stale halt is read       */
#define HCD_INJ_PHASE_GONE      3UL  /* until the device leaves            */

/* How long the thread waits for the drain to take an injected event, and
 * for the driver's answer to a stale halt or a refused code (ticks, at
 * least 45 ms each: a few seconds) before the next injection. */
#define HCD_INJ_DELIVER_MS      1000UL
#define HCD_INJ_SETTLE_TICKS    50UL

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
ULONG HcdInjEventSwallow(PXHCI_EXTENSION ext, const XHCI_TRB *ev)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG swallowed;

    hc = HcdControllerFromExt(ext);
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
    (VOID)KeSetEvent(&HcdControllerFromExt(ext)->InjEvDone, IO_NO_INCREMENT,
                     FALSE);
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

    if (hc->InjEp.Slot == 0) {
        return state;
    }
    KeAcquireSpinLock(&hc->InjLock, &oldIrql);
    answer = XhciInjEpState(&hc->InjEp, slot, dci, state);
    KeReleaseSpinLock(&hc->InjLock, oldIrql);
    return answer;
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
    XhciLogNote(&hc->Hc, "qemu.inj.latched", found ? value : 0xFFFFFFFFUL);
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
         * PORTSC) - which is why PEC is answered here. */
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
            XhciRingTrbPA(ring, XhciRingNextIndex(ring, head->LastIndex)),
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
    KIRQL oldIrql;
    ULONG verdict;
    ULONG answer;
    ULONG icc;
    ULONG icDrop;
    ULONG icAdd;
    ULONG survives;
    ULONG type;

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
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
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
VOID HcdInjService(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG now;
    ULONG same;
    ULONG marked;

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
    default:
        break;
    }
    hc->InjPhase = HCD_INJ_PHASE_IDLE;
    if (hc->InjFault == 0) {
        return;
    }
    if (hc->InjLeft == 0) {
        XhciLogNote(&hc->Hc, "qemu.inj.done", hc->InjFault);
        hc->InjFault = 0;
        return;
    }
    hcdInjPipeFire(hc);
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
}

/* One command the trigger fired. IRQL: PASSIVE_LEVEL. */
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
    case XHCI_INJ_DEAD:
    case XHCI_INJ_DEAD_NOPROOF:
        KeAcquireSpinLock(&hc->InjLock, &oldIrql);
        XhciInjArmDead(&hc->InjRegs, arg, fault == XHCI_INJ_DEAD_NOPROOF);
        KeReleaseSpinLock(&hc->InjLock, oldIrql);
        break;
    default:
        if (XhciInjKind(fault) != XHCI_INJ_KIND_OTHER) {
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

/*
 * Each controller-thread pass, first in hcdPoll: the trigger read about once
 * a second, a fresh command carried out, and the interrupts the window has
 * dropped since the last pass noted. IRQL: PASSIVE_LEVEL, no lock held.
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

    now = HcdTolNow(hc);
    if (now - hc->InjReadAt < HCD_INJ_READ_TICKS && now >= hc->InjReadAt) {
        return;
    }
    hc->InjReadAt = now;
    value = 0;
    found = NT_SUCCESS(HcdCtlQemuReadDword(hc, HCD_VALUE_QEMU_INJECT, &value))
                ? 1UL
                : 0UL;
    switch (XhciInjTake(&hc->InjTrigger, found, value)) {
    case XHCI_INJ_TAKE_FIRE:
        hcdInjFire(hc, value);
        break;
    case XHCI_INJ_TAKE_UNKNOWN:
        XHCI_DBG_VALUE("qemu inject: no such fault", value);
        XhciLogNote(ext, "qemu.inj.unknown", value);
        break;
    default:
        break;
    }
}

#else /* debug and release: injection is not in the image */

typedef int hcdInjNotInImage;

#endif
