/*
 * xhci_pci.c - the only file in this driver that touches BAR0 or PCI config
 * space.
 *
 * Everything here is a two-line wrapper, and that is the design rather than an
 * accident: the pure core (xhci_ring.c, xhci_caps.c, xhci_port.c, xhci_mem.c)
 * decides *what* to read and write and is tested on the build host, and this
 * file is where those decisions become bus cycles
 * (docs/contributing/design/03-host-unit-tests.md section 2). Keeping the accessors in
 * one place is also what makes "no MMIO outside the wired-up lifecycle" a
 * property something can be said about, rather than a habit.
 *
 * READ_REGISTER_ULONG / WRITE_REGISTER_ULONG rather than volatile pointer
 * dereferences: they are the accessors AGENTS.md names, and on x86 the Win2K
 * DDK declares them as imports from NTOSKRNL.EXE - the same module and the
 * same two names the shipping NUSB usbehci.sys imports, which is the strongest
 * Win98 evidence available for any symbol this driver adds
 * (scripts/import-gate/xhci98-imports.allow).
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* ------------------------------------------------------------------ */
/* MMIO                                                                */
/* ------------------------------------------------------------------ */

/* IRQL: any. */
ULONG XhciRead32(PXHCI_EXTENSION ext, ULONG barOffset)
{
    return READ_REGISTER_ULONG((PULONG)(ext->ResourceBase + barOffset));
}

/* IRQL: any. */
VOID XhciWrite32(PXHCI_EXTENSION ext, ULONG barOffset, ULONG value)
{
    WRITE_REGISTER_ULONG((PULONG)(ext->ResourceBase + barOffset), value);
}

/*
 * Low DWORD first, high DWORD second, and the order is not this driver's
 * choice: "If a system is incapable of issuing Qword accesses, then writes to
 * the 64-bit address fields shall be performed using 2 Dword accesses; low
 * Dword-first, high-Dword second" (spec 5.1, p.337). A 32-bit kernel is
 * exactly the system that clause is about, and that clause is the whole
 * authority here. Linux does the same - its xhci_write_64 resolves to
 * lo_hi_writeq - but a reference implementation agreeing is corroboration,
 * not the reason.
 *
 * An earlier version of this function wrote the high half first, reasoning
 * that it avoided a momentary (stale high : new low). That reasoning was
 * backwards as well as non-conforming: the high half of these registers is
 * zero at reset and this driver never writes anything else into it, so it is
 * never the stale one. The half that goes stale is the low one, and a
 * controller that latches the pair when the *high* DWORD is written would
 * latch the old address and never see the new.
 *
 * The high DWORD is written even on an AC64 = 0 controller, where "the high
 * Dword of registers containing 64-bit address fields are unused" (same
 * section and page): writing a zero into an unused field is harmless, and one
 * code path is worth more here than saving a register write per start.
 *
 * IRQL: any.
 */
VOID XhciWrite64(PXHCI_EXTENSION ext, ULONG barOffset, ULONG low)
{
    XhciWrite32(ext, barOffset, low);
    XhciWrite32(ext, barOffset + 4UL, 0);
}

/* IRQL: any. */
ULONG XhciReadOp(PXHCI_EXTENSION ext, ULONG opOffset)
{
    return XhciRead32(ext, ext->HcInfo.OperationalOffset + opOffset);
}

/* IRQL: any. */
VOID XhciWriteOp(PXHCI_EXTENSION ext, ULONG opOffset, ULONG value)
{
    XhciWrite32(ext, ext->HcInfo.OperationalOffset + opOffset, value);
}

/* IRQL: any. */
ULONG XhciReadRt(PXHCI_EXTENSION ext, ULONG rtOffset)
{
    return XhciRead32(ext, ext->HcInfo.RuntimeOffset + rtOffset);
}

/* IRQL: any. */
VOID XhciWriteRt(PXHCI_EXTENSION ext, ULONG rtOffset, ULONG value)
{
    XhciWrite32(ext, ext->HcInfo.RuntimeOffset + rtOffset, value);
}

/* IRQL: any. */
ULONG XhciReadIr0(PXHCI_EXTENSION ext, ULONG irOffset)
{
    return XhciReadRt(ext, XHCI_RT_IR0 + irOffset);
}

/* IRQL: any. */
VOID XhciWriteIr0(PXHCI_EXTENSION ext, ULONG irOffset, ULONG value)
{
    XhciWriteRt(ext, XHCI_RT_IR0 + irOffset, value);
}

/*
 * PORTSC, by 1-based port number.
 *
 * The window was proved once, by XhciDeriveHcInfo, which refuses a controller
 * whose mapping cannot hold MaxPorts port register sets. What this bound adds
 * is protection against a *port number* that did not come from the port map -
 * MaxPorts is eight bits of device-supplied data and every loop over it is
 * arithmetic this driver wrote.
 *
 * IRQL: any.
 */
ULONG XhciReadPortsc(PXHCI_EXTENSION ext, ULONG port)
{
    if (port == 0 || port > ext->HcInfo.MaxPorts) {
        XHCI_DBG_VALUE("PORTSC: refused read of port", port);
        return 0xFFFFFFFFUL;
    }
    return XhciRead32(ext, ext->HcInfo.PortscOffset +
                               (port - 1UL) * XHCI_OP_PORT_STRIDE);
}

/* IRQL: any. */
VOID XhciWritePortsc(PXHCI_EXTENSION ext, ULONG port, ULONG value)
{
    if (port == 0 || port > ext->HcInfo.MaxPorts) {
        XHCI_DBG_VALUE("PORTSC: refused write to port", port);
        return;
    }
    XhciWrite32(ext, ext->HcInfo.PortscOffset +
                         (port - 1UL) * XHCI_OP_PORT_STRIDE, value);
}

/*
 * One doorbell, by slot number - DB[0] is the Host Controller Command doorbell
 * and DB[n] is device slot n (spec 5.6).
 *
 * The window was proved once, by XhciDeriveHcInfo, which refuses a controller
 * whose mapping cannot hold DBOFF plus (MaxSlots + 1) DWORDs. This bound is the
 * same second line XhciWritePortsc carries, for the same reason: MaxSlots is
 * eight bits of device-supplied data and a slot number is arithmetic this driver
 * wrote.
 *
 * There is no read side. Doorbell registers read as zero, and "doorbell writes
 * may be posted" (docs/usb-xhci-info/xhci-data-structures.md section 5) - a read here would
 * answer nothing and flush the write for no reason.
 *
 * IRQL: any.
 */
VOID XhciWriteDoorbell(PXHCI_EXTENSION ext, ULONG slot, ULONG value)
{
    if (slot > ext->HcInfo.MaxSlots) {
        XHCI_DBG_VALUE("doorbell: refused write to slot", slot);
        return;
    }
    XhciWrite32(ext, ext->HcInfo.DoorbellOffset + slot * XHCI_DB_STRIDE, value);
}

/*
 * The capability walk's reader. It is handed a length and promises to stay
 * inside it, and test/test_caps.c checks that directly - so this bound is the
 * second line, for the case where the parser is right and something else
 * called it with a length the mapping does not have.
 *
 * All-ones is the honest answer to give on a refusal: it is what the bus
 * returns for an access nothing decodes, and the parser already treats it as
 * "no capability here" rather than as data.
 *
 * IRQL: any.
 */
ULONG XhciBarReader(PVOID context, ULONG byteOffset)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)context;
    if (ext == NULL) {
        return 0xFFFFFFFFUL;
    }
    if (ext->IoSpaceLength < 4UL ||
        byteOffset > (ext->IoSpaceLength - 4UL)) {
        XHCI_DBG_VALUE("BarReader: refused out-of-window offset", byteOffset);
        return 0xFFFFFFFFUL;
    }
    return XhciRead32(ext, byteOffset);
}

/* ------------------------------------------------------------------ */
/* PCI configuration space                                             */
/* ------------------------------------------------------------------ */

/* IRQL: PASSIVE_LEVEL. */
MPSTATUS XhciReadPciConfig(PXHCI_EXTENSION ext,
                           ULONG offset,
                           PVOID buffer,
                           ULONG length)
{
    if (XhciRegPacket.UsbPortReadWriteConfigSpace == NULL) {
        return MP_STATUS_FAILURE;
    }
    return XhciRegPacket.UsbPortReadWriteConfigSpace(ext, TRUE, buffer,
                                                     offset, length);
}

/* IRQL: PASSIVE_LEVEL. See the contract in src/xhci_hw.h - this exists for the
 * quiesce path's Bus Master Enable fallback and nothing else. */
MPSTATUS XhciWritePciConfig(PXHCI_EXTENSION ext,
                            ULONG offset,
                            PVOID buffer,
                            ULONG length)
{
    if (XhciRegPacket.UsbPortReadWriteConfigSpace == NULL) {
        return MP_STATUS_FAILURE;
    }
    return XhciRegPacket.UsbPortReadWriteConfigSpace(ext, FALSE, buffer,
                                                     offset, length);
}

/* ------------------------------------------------------------------ */
/* Bounded waits                                                       */
/* ------------------------------------------------------------------ */

/*
 * How long the busy-wait phase runs before switching to a real sleep, and in
 * what steps. 20 us x 500 = 10 ms of stalling, which covers a halt (the spec
 * allows 16 ms but hardware answers in microseconds) and every reset this
 * project has measured, without ever busy-waiting long enough to matter to a
 * PASSIVE-level worker thread. Beyond that the operation is already abnormal
 * and the caller is only waiting to be able to say so.
 */
#define XHCI_STALL_STEP_US      20UL
#define XHCI_STALL_STEPS        500UL
#define XHCI_SLEEP_STEP_MS      10UL

/*
 * One read's verdict: 1 when the wait is over, whichever way. An all-ones read
 * ends it as a refusal rather than a match, the same rule the ISR, the
 * interrupter rearm and the enable paths apply: a window that has stopped
 * decoding carries every bit set, so a wait whose wanted bits are all set
 * (HCHalted, port power) would otherwise take it for success. The caller
 * distinguishes the two by testing the value it already holds; *lastValue is
 * filled either way so the refusal record shows what was read.
 *
 * IRQL: any (pure).
 */
static ULONG xhciWaitSettled(ULONG value, ULONG mask, ULONG want,
                             ULONG *lastValue)
{
    if (value != 0xFFFFFFFFUL && (value & mask) != want) {
        return 0;
    }
    if (lastValue != NULL) {
        *lastValue = value;
    }
    return 1;
}

/* IRQL: PASSIVE_LEVEL, or DISPATCH_LEVEL with `ext->InitBelowPassive` set
 * (task 13-R.1) - see the contract in xhci_hw.h and the branch below. */
ULONG XhciWaitForBits(PXHCI_EXTENSION ext,
                      ULONG barOffset,
                      ULONG mask,
                      ULONG want,
                      ULONG timeoutMs,
                      ULONG *lastValue)
{
    ULONG value;
    ULONG i;
    ULONG stalledMs;
    ULONG sleeps;

    value = XhciRead32(ext, barOffset);
    if (xhciWaitSettled(value, mask, want, lastValue)) {
        return value != 0xFFFFFFFFUL;
    }

    for (i = 0; i < XHCI_STALL_STEPS; i++) {
        KeStallExecutionProcessor(XHCI_STALL_STEP_US);
        value = XhciRead32(ext, barOffset);
        if (xhciWaitSettled(value, mask, want, lastValue)) {
            return value != 0xFFFFFFFFUL;
        }
    }

    stalledMs = (XHCI_STALL_STEP_US * XHCI_STALL_STEPS) / 1000UL;
    /*
     * **Task 13-R.1: the sleep phase is skipped outright below PASSIVE_LEVEL.**
     * The in-place recovery re-runs this whole sequence from
     * `UsbPortRequestAsyncCallback`'s DPC, where `UsbPortWait` -
     * `KeDelayExecutionThread` - is a hang on Win98 and a Driver Verifier
     * bugcheck on Win2000. What is *not* done here is extending the stall to
     * cover the timeout instead: that would busy-wait a DISPATCH-level DPC for
     * the better part of a second on hardware that has already failed. The
     * stall phase is 10 ms, which covers every halt and every reset this
     * project has measured, and a controller that needs longer than that gets
     * its extra time from the recovery being retried at the next health poll
     * rather than from a spin here.
     */
    if (timeoutMs <= stalledMs || ext == NULL || ext->InitBelowPassive ||
        XhciRegPacket.UsbPortWait == NULL) {
        if (lastValue != NULL) {
            *lastValue = value;
        }
        return 0;
    }

    /*
     * Round up, so a timeout is never cut short by integer division. The sleep
     * itself is only approximately XHCI_SLEEP_STEP_MS - KeDelayExecutionThread
     * rounds up to the system tick - which makes the real timeout longer than
     * asked for and never shorter. That is the right direction for a bound
     * whose whole job is to avoid declaring healthy hardware dead.
     */
    sleeps = ((timeoutMs - stalledMs) + (XHCI_SLEEP_STEP_MS - 1UL)) /
             XHCI_SLEEP_STEP_MS;
    for (i = 0; i < sleeps; i++) {
        XhciRegPacket.UsbPortWait(ext, XHCI_SLEEP_STEP_MS);
        value = XhciRead32(ext, barOffset);
        if (xhciWaitSettled(value, mask, want, lastValue)) {
            return value != 0xFFFFFFFFUL;
        }
    }

    if (lastValue != NULL) {
        *lastValue = value;
    }
    return 0;
}

/*
 * A fixed delay, for the one wait in this driver that has no register to poll.
 *
 * UsbPortWait's granularity is the system tick, so this sleeps for at least the
 * interval asked for and usually longer - the right direction for a settle
 * delay. The stall fallback is deliberate rather than defensive: without the
 * service there is no way to sleep, and a caller that skipped the delay would
 * go on to touch a port whose power is not yet stable.
 *
 * IRQL: PASSIVE_LEVEL, or DISPATCH_LEVEL with `ext->InitBelowPassive` set
 * (task 13-R.1), where the stall below is not merely the fallback but the only
 * legal form of this wait.
 */
VOID XhciDelayMs(PXHCI_EXTENSION ext, ULONG milliseconds)
{
    ULONG i;
    ULONG steps;

    /* Task 13-R.1: below PASSIVE_LEVEL the stall is the only legal form of this
     * wait, so the fallback path is taken deliberately rather than for want of
     * the service. It is 20 ms once per recovery attempt (the port-power
     * settle), on a controller that is out of service either way. */
    if (XhciRegPacket.UsbPortWait != NULL &&
        (ext == NULL || !ext->InitBelowPassive)) {
        XhciRegPacket.UsbPortWait(ext, milliseconds);
        return;
    }

    steps = (milliseconds * 1000UL) / XHCI_STALL_STEP_US;
    for (i = 0; i < steps; i++) {
        KeStallExecutionProcessor(XHCI_STALL_STEP_US);
    }
}
