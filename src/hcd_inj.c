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
 *   CLEAR         every answer and the lost-interrupt window off
 *
 * A port the guest names in bits 15:8 must be a managed USB 2.0 root port
 * with a device enabled on it; 0 takes the first such. A command that
 * cannot be carried out is refused with a note, and its sequence is spent.
 *
 * IRQL: each function says; the thread's functions run at PASSIVE_LEVEL
 * with no lock held. InjLock is a leaf, taken at <= DISPATCH_LEVEL.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

#if defined(XHCI_FLAVOUR_QEMU)

#define HCD_VALUE_QEMU_INJECT L"XhciQemuInject"

/* The trigger is read once per ten tolerance ticks, about a second. */
#define HCD_INJ_READ_TICKS 10UL

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
    ULONG value;
    ULONG found;

    hcdInjClear(hc);
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
        done = hcdInjHardware(hc, fault, XHCI_INJ_GET_PORT(value));
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
    case XHCI_INJ_TAKE_UNBUILT:
        XHCI_DBG_VALUE("qemu inject: fault not built yet", value);
        XhciLogNote(ext, "qemu.inj.unbuilt", value);
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
