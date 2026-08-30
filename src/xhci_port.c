/*
 * xhci_port.c - constructing PORTSC writes that have only the intended effect,
 * and deciding what a port's state means to the USB hub class.
 *
 * Pure computation on a value the caller already read: no MMIO, no DDK calls,
 * no IRQL dependencies, so it builds and runs on the host under
 * XHCI_HOST_TEST (docs/contributing/design/03-host-unit-tests.md). test/test_port.c is
 * the regression suite; the bit table is docs/usb-xhci-info/xhci-data-structures.md
 * section 3 and the rule is docs/usb-xhci-info/xhci-programming.md "Port Management".
 *
 * Why a whole file for four masks: PORTSC is the register where a correct-
 * looking read-modify-write is a bug. Writing back the PED bit that says
 * "enabled" *disables* the port; writing back a PR that reads 1 during a reset
 * restarts the reset; writing back a change bit silently discards a connect
 * this driver has not handled yet; writing with LWS set turns the preserved
 * PLS field into a link-state command. Every one of those is a port that goes
 * quiet rather than an error anyone sees, so no caller composes a PORTSC value
 * by hand - they all come from here.
 *
 * Phase 5 adds the other half of the same argument, one layer up. The
 * root-hub *callbacks* live in src/xhci_rh.c because they touch registers and
 * take a lock; what lives here is everything they decide **without** hardware:
 * which PORTSC a hub port is (task 2's explicit map), what a freshly read
 * PORTSC does to the shadow, and which hub-class bits that shadow reports
 * (task 2's "never expose raw xHCI bit positions as USB hub-class status
 * bits"). Splitting it that way is what lets the reporting rules be checked by
 * golden vectors rather than by plugging a device into a VM.
 *
 * C89 only. IRQL: every function is callable at any IRQL.
 */

#include "xhci.h"

ULONG XhciPortscNeutral(ULONG portsc)
{
    return portsc & ~XHCI_PORTSC_UNSAFE_MASK;
}

/*
 * Set exactly `bits`, changing nothing else. The caller passes the bit it
 * means to act on - PORTSC_PP to power the port, PORTSC_PR to start a reset -
 * and gets back a value whose every other write-sensitive bit is inert.
 */
ULONG XhciPortscWith(ULONG portsc, ULONG bits)
{
    return XhciPortscNeutral(portsc) | bits;
}

/*
 * Clear the named change bits and nothing else. RW1C means the write must
 * carry a 1 in each bit being cleared, which is exactly the shape that
 * accidentally clears the others: `changeBits` is masked so a caller that
 * passes the raw register value clears only what it asked for by name.
 */
ULONG XhciPortscClearChanges(ULONG portsc, ULONG changeBits)
{
    return XhciPortscNeutral(portsc) | (changeBits & XHCI_PORTSC_CHANGE_MASK);
}

/*
 * Write the Port Link State field. PLS is only armed when LWS is set in the
 * same write, which is why LWS is in the unsafe mask everywhere else: a
 * neutral write must never carry it, and this is the one place that adds it
 * back deliberately.
 */
ULONG XhciPortscSetLinkState(ULONG portsc, ULONG pls)
{
    ULONG value;

    value = XhciPortscNeutral(portsc) & ~XHCI_PORTSC_PLS_MASK;
    value |= (pls << XHCI_PORTSC_PLS_SHIFT) & XHCI_PORTSC_PLS_MASK;
    return value | XHCI_PORTSC_LWS;
}

/* ------------------------------------------------------------------ */
/* The five port operations, by name (roadmap Phase 5 task 3)          */
/* ------------------------------------------------------------------ */

/*
 * Each of these is one line over the primitives above, and the reason they
 * exist as functions is that the one-liners are not interchangeable and look as
 * though they are.
 *
 * `XhciPortscPower(v, 0)` and `XhciPortscDisable(v)` differ by one bit and by
 * everything else: PP is an ordinary RW bit, so taking power away means writing
 * a **0** there, while PED is RW1C, so disabling the port means writing a
 * **1**. A call site that reasoned "clearing is clearing" would either leave a
 * port powered or disable one it meant to unpower, and neither reports an
 * error. Same for reset and suspend: PR is RW1S, so a reset is a written 1 and
 * there is no "write 0 to stop"; a suspend is not a bit at all but a PLS field
 * write that only takes effect with LWS in the same word.
 *
 * The one property they all share is the neutral base: none of them carries a
 * change bit, so no operation can silently swallow a connect that has not been
 * reported yet.
 */

/* PP is RW: on means writing a 1, off means writing a 0. */
ULONG XhciPortscPower(ULONG portsc, ULONG on)
{
    if (on) {
        return XhciPortscWith(portsc, XHCI_PORTSC_PP);
    }
    return XhciPortscNeutral(portsc) & ~XHCI_PORTSC_PP;
}

/* PR is RW1S: "when software writes a '1' to this flag, the bus reset sequence
 * ... is started" (Table 5-27, p.373). Warm reset (WPR) is SuperSpeed-only and
 * is deliberately not offered here. */
ULONG XhciPortscReset(ULONG portsc)
{
    return XhciPortscWith(portsc, XHCI_PORTSC_PR);
}

/*
 * PED is RW1C, and this is the one place in the driver that deliberately writes
 * it: "software cannot enable a port by writing a '1' to this flag. A port may
 * be disabled by software writing a '1' to this flag" (Table 5-27, p.372).
 * There is no matching enable, which is why RH_SetFeaturePortEnable is refused
 * rather than implemented.
 */
ULONG XhciPortscDisable(ULONG portsc)
{
    return XhciPortscWith(portsc, XHCI_PORTSC_PED);
}

/*
 * Suspend and resume are PLS field writes, so they are the shapes that
 * deliberately carry LWS - "the Port Link State Write Strobe (LWS) bit shall be
 * set to '1' to write the PLS field" (4.15.1, p.255).
 *
 * A suspend is one write: "system software places individual ports into suspend
 * mode by writing a '3' into the appropriate PORTSC register Port Link State
 * (PLS) field" (4.15.1, p.254). The port reports the transition with PLC when
 * the suspend signalling has completed, which may be as long as 10 ms later.
 */
ULONG XhciPortscSuspend(ULONG portsc)
{
    return XhciPortscSetLinkState(portsc, XHCI_PLS_U3);
}

/*
 * **A resume is two writes with a timed gap between them, and on a USB 2.0 port
 * neither of them is U0-to-start.** This is the one place in the port family
 * where the USB 3.x answer and the USB 2.0 answer are different *values*, and
 * where using the wrong one looks like a resume that reports success and does
 * nothing.
 *
 * "For a USB2 protocol port, software shall write a '15' (Resume) to the PLS
 * field to initiate resume signaling. The port shall transition to the Resume
 * substate and the xHC shall transmit the resume signaling within 1 ms
 * (TURSM). Software shall ensure that resume is signaled for at least 20 ms
 * (TDRSMDN). Software shall start timing TDRSMDN from the write of '15'
 * (Resume) to PLS. After TDRSMDN is complete, software shall write a '0' (U0)
 * to the PLS field" (4.15.2.2, p.257). The USB 3.x port in the same list writes
 * '0' to *initiate* (step 1a) - which is exactly the value a USB 2.0 port uses
 * to *terminate*, and this driver serves only USB 2.0 ports.
 *
 * The device-initiated case ends the same way and starts by itself: the port
 * enters Resume on its own and sets PLC, and "software shall start timing
 * TDRSMDN from the notification of the transition to the Resume state. After
 * TDRSMDN is complete, software shall write a '0' to the PLS field" (4.15.2.1,
 * p.256). So `Done` is owed on both paths, and only the first write differs.
 *
 * They are two functions rather than one with a flag because the gap between
 * them is 20 ms of bus signalling that no callback in this driver may wait for
 * (`XHCI_PORT_RESUME_SIGNAL_MS`) - so they are always issued from two different
 * contexts, and a single builder would suggest otherwise.
 */
ULONG XhciPortscResumeSignal(ULONG portsc)
{
    return XhciPortscSetLinkState(portsc, XHCI_PLS_RESUME);
}

ULONG XhciPortscResumeDone(ULONG portsc)
{
    return XhciPortscSetLinkState(portsc, XHCI_PLS_U0);
}

/* ------------------------------------------------------------------ */
/* The logical-port map (roadmap Phase 5 task 2)                       */
/* ------------------------------------------------------------------ */

ULONG XhciRootHubBuild(const XHCI_PORT_MAP *map, PXHCI_ROOT_HUB rh)
{
    ULONG port;
    ULONG hubPort;
    ULONG i;

    if (map == NULL || rh == NULL) {
        return XHCI_RH_BAD_PARAM;
    }

    /*
     * Cleared in full, including the shadows. A rebuild happens on a restart or
     * a resume, both of which have reset the controller since the previous
     * reading was taken - so carrying a shadow across one would report a connect
     * state from before the reset, which is the shape of bug that looks like a
     * device that "was already there".
     *
     * **The one field that is carried across is the generation**, and it has to
     * be. A rebuild happens on a resume as well as on a start, and a resume does
     * *not* get a new start epoch - usbport zeroes the extension before
     * StartController, not before ResumeController, so `StartEpoch` is unchanged
     * across a suspend/resume pair. If the generations restarted at 0 here, the
     * first operation armed after the resume would be handed generation 1 with
     * the same epoch and the same hub port as one armed just before the suspend,
     * and an uncancellable timer from before would claim it. Win98 idle-suspends
     * within about half a second of a start, so that window is the ordinary case
     * there rather than a corner. Advanced rather than merely preserved, so a
     * timer armed against the pre-rebuild topology is stale even if nothing was
     * armed at the moment of the rebuild.
     */
    rh->PortCount = 0;
    rh->Status = XHCI_RH_NO_PORTS;
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        rh->HubToPort[i] = 0;
        rh->PortToHub[i] = 0;
        rh->Ports[i].Portsc = 0;
        rh->Ports[i].Changes = 0;
#ifdef XHCI_FIX_ACK_OWED
        /* A rebuild has thrown away the reading the debt was composed from, and
         * an HCRST clears every change bit in the hardware anyway - so there is
         * nothing left to acknowledge and a surviving mask would be written
         * against a port this driver has not read yet. */
        rh->Ports[i].PortscAckOwed = 0;
#endif
        rh->Ports[i].Speed = XHCI_SPEED_UNKNOWN;
        rh->Ports[i].Armed = XHCI_PORT_OP_NONE;
        rh->Ports[i].ArmPending = 0;
        rh->Ports[i].AgeArmed = 0;
        rh->Ports[i].AgeStamp = 0;
        rh->Ports[i].PpPending = 0;
        rh->Ports[i].PpWanted = 0;
        rh->Ports[i].PpArmed = 0;
        rh->Ports[i].PpStamp = 0;
        /*
         * **The disown debt is per-tenancy, not per-shadow.** It names a device
         * record the *previous* run held on this port, and the rebuild has just
         * thrown away the reading it would be confirmed against. Left standing,
         * the first post-resume health poll would confirm it against the new
         * run's PORTSC - a re-powered unconnected or mid-reset port reads
         * PED = 0 - and fire `XhciSlotPortDisabled` at whatever record the new
         * run holds there, which on the restore-success path is a preserved
         * live device. Cleared alongside the other pending-operation state for
         * the same reason `Armed` and `PpPending` are.
         */
        rh->Ports[i].DisownPending = 0;
        rh->Ports[i].DisownWantsPp = 0;
        rh->Ports[i].Generation++;
        if (rh->Ports[i].Generation == 0) {
            rh->Ports[i].Generation = 1;
        }
    }
    rh->Reserved[0] = 0;
    rh->Reserved[1] = 0;

    hubPort = 0;
    for (port = 1; port <= map->PortCount && port <= XHCI_MAX_ROOT_PORTS;
         port++) {
        if (!XhciPortIsManaged(map, port)) {
            continue;
        }
        hubPort++;
        rh->HubToPort[hubPort - 1] = (UCHAR)port;
        rh->PortToHub[port - 1] = (UCHAR)hubPort;
    }

    rh->PortCount = hubPort;
    /*
     * Zero managed ports is refused here rather than reported. usbport sizes the
     * root hub descriptor's removable and power masks as
     * ((NumberOfPorts - 1) >> 3) + 1 with no guard in either shipping build, so
     * NumberOfPorts = 0 unsigned-wraps into a ~1 GB nonpaged allocation
     * (docs/usb-xhci-info/usbport-miniport-abi.md section 4, confirmed). The init
     * sequence already refuses such a controller at the port-map step; this is
     * the second gate on the same fact, because the cost of reaching usbport
     * with a zero is not a failed start.
     */
    rh->Status = (hubPort > 0) ? XHCI_RH_OK : XHCI_RH_NO_PORTS;
    return rh->Status;
}

ULONG XhciRootHubPortOf(const XHCI_ROOT_HUB *rh, ULONG hubPort)
{
    if (rh == NULL || rh->Status != XHCI_RH_OK) {
        return 0;
    }
    if (hubPort == 0 || hubPort > rh->PortCount) {
        return 0;
    }
    return rh->HubToPort[hubPort - 1];
}

ULONG XhciRootHubHubPortOf(const XHCI_ROOT_HUB *rh, ULONG port)
{
    if (rh == NULL || rh->Status != XHCI_RH_OK) {
        return 0;
    }
    if (port == 0 || port > XHCI_MAX_ROOT_PORTS) {
        return 0;
    }
    return rh->PortToHub[port - 1];
}

/* ------------------------------------------------------------------ */
/* The shadow                                                          */
/* ------------------------------------------------------------------ */

/*
 * Did this port just finish resuming?
 *
 * xHCI has no "resume complete" bit: PLC says the link state changed and the
 * meaning is in *which* change. C_PORT_SUSPEND is the hub class's "the resume
 * you asked for has finished", so it is owed exactly when a port that was in U3
 * or in Resume signalling is no longer in either. Deriving it needs the
 * previous link state, which is why the shadow keeps the whole previous PORTSC
 * rather than a handful of decoded booleans.
 */
static ULONG xhciPortLeftSuspend(ULONG previous, ULONG portsc)
{
    ULONG was;
    ULONG now;

    was = XHCI_PORTSC_GET_PLS(previous);
    now = XHCI_PORTSC_GET_PLS(portsc);

    if (was != XHCI_PLS_U3 && was != XHCI_PLS_RESUME) {
        return 0;
    }
    if (now == XHCI_PLS_U3 || now == XHCI_PLS_RESUME) {
        return 0;
    }

    /*
     * **And the port has to still be one a device could have resumed on.**
     *
     * C_PORT_SUSPEND is the hub class's "the resume you asked for has finished",
     * so a reading that *looks* like leaving Resume must not be reported as one
     * unless the port could have done it. The case is not the one an earlier
     * version of this comment claimed - it argued that a disable sets PLC
     * because PLC's row excludes only "software setting PP to '0'", which reads
     * the exclusion as if it defined the list. It does not: "this flag is set to
     * '1' due to the following PLS transitions" (Table 5-27, p.378) enumerates
     * them, and for a USB2 port a disable is not among them.
     *
     * What is real is a **stale PLC**. It is RW1CS, so a legitimate one - the
     * U3-to-Resume that announces a device's wakeup - stays set until software
     * writes a 1, and the acknowledgement can be deferred (a Port Power change
     * in flight holds every PORTSC write on that port). If the port is out of
     * service by the time the next reading is taken, PLS no longer says Resume:
     * "this field is undefined if PP = '0'" (p.374), and PED and PR likewise
     * read '0' there (p.372). Previous state Resume, current state not Resume,
     * PLC still set - and without this condition that is reported to usbhub as a
     * device that woke up, on a port whose power was taken away under it.
     *
     * So the question is whether the port could have completed a resume at all,
     * and a port that is not enabled with something still attached could not.
     */
    if ((portsc & XHCI_PORTSC_PED) == 0 || (portsc & XHCI_PORTSC_CCS) == 0) {
        return 0;
    }
    return 1;
}

ULONG XhciPortShadowUpdate(XHCI_PORT_SHADOW *shadow,
                           ULONG portsc,
                           ULONG speedClass,
                           ULONG *ackBits)
{
    ULONG latched;
    ULONG previous;

    if (ackBits != NULL) {
        *ackBits = 0;
    }
    if (shadow == NULL) {
        return 0;
    }

    /*
     * An all-ones read is a window that has stopped decoding, not a port with
     * every change bit set. Taking it at face value would latch five changes,
     * report a connected, enabled, powered, over-current, resetting port, and
     * ask the caller to write 1 to every RW1C bit of a register that is not
     * there. The shadow keeps what it last knew instead.
     */
    if (portsc == 0xFFFFFFFFUL) {
        return 0;
    }

    previous = shadow->Portsc;
    latched = 0;

    if ((portsc & XHCI_PORTSC_CSC) != 0) {
        latched |= XHCI_HUB_C_PORT_CONNECTION;
    }
    if ((portsc & XHCI_PORTSC_PEC) != 0) {
        latched |= XHCI_HUB_C_PORT_ENABLE;
    }
    if ((portsc & XHCI_PORTSC_OCC) != 0) {
        latched |= XHCI_HUB_C_PORT_OVER_CURRENT;
    }
    if ((portsc & XHCI_PORTSC_PRC) != 0) {
        latched |= XHCI_HUB_C_PORT_RESET;
    }
    if ((portsc & XHCI_PORTSC_PLC) != 0 &&
        xhciPortLeftSuspend(previous, portsc)) {
        latched |= XHCI_HUB_C_PORT_SUSPEND;
    }

    /*
     * **Every** change bit that was set is acknowledged, not only the ones that
     * became a hub-class change. WRC and CEC are SuperSpeed-only and mean
     * nothing to a USB 2.0 port's report, but an unacknowledged change bit
     * suppresses the controller's next Port Status Change Event for the port
     * just the same, and a port that stops reporting is the failure this whole
     * shadow exists to avoid.
     */
    if (ackBits != NULL) {
        *ackBits = portsc & XHCI_PORTSC_CHANGE_MASK;
    }

    shadow->Portsc = portsc;
    shadow->Changes = (UCHAR)((shadow->Changes | latched) &
                              XHCI_HUB_C_PORT_MASK);

    /*
     * Speed is only meaningful while something is attached, and only really
     * settled after a reset - the Port Speed field "is only relevant when the
     * port is in the Enabled state" (Table 5-27, p.374). An unconnected port
     * forgets it rather than reporting the speed of the device that left.
     */
    if ((portsc & XHCI_PORTSC_CCS) != 0) {
        shadow->Speed = (UCHAR)speedClass;
    } else {
        shadow->Speed = XHCI_SPEED_UNKNOWN;
    }

    return latched;
}

VOID XhciPortShadowReport(const XHCI_PORT_SHADOW *shadow,
                          ULONG *portStatus,
                          ULONG *portChange)
{
    ULONG status;
    ULONG portsc;

    if (portStatus != NULL) {
        *portStatus = 0;
    }
    if (portChange != NULL) {
        *portChange = 0;
    }
    if (shadow == NULL) {
        return;
    }

    portsc = shadow->Portsc;
    status = 0;

    if ((portsc & XHCI_PORTSC_CCS) != 0) {
        status |= XHCI_HUB_PORT_CONNECTION;
    }
    if ((portsc & XHCI_PORTSC_PED) != 0) {
        status |= XHCI_HUB_PORT_ENABLE;
    }
    if ((portsc & XHCI_PORTSC_OCA) != 0) {
        status |= XHCI_HUB_PORT_OVER_CURRENT;
    }
    if ((portsc & XHCI_PORTSC_PR) != 0) {
        status |= XHCI_HUB_PORT_RESET;
    }
    if ((portsc & XHCI_PORTSC_PP) != 0) {
        status |= XHCI_HUB_PORT_POWER;
    }
    /*
     * Suspend is a link state, not a bit. U3 is the suspended state; Resume is
     * reported as still suspended, because the hub class says the port leaves
     * suspend when C_PORT_SUSPEND is set and that is what a completed resume
     * produces (xhciPortLeftSuspend above).
     */
    if (XHCI_PORTSC_GET_PLS(portsc) == XHCI_PLS_U3 ||
        XHCI_PORTSC_GET_PLS(portsc) == XHCI_PLS_RESUME) {
        status |= XHCI_HUB_PORT_SUSPEND;
    }

    /*
     * Every connected managed root port is reported to usbport as High Speed,
     * whatever `shadow->Speed` decoded - Phase 5 task 7, and the one place this
     * driver deliberately tells usbport something untrue.
     *
     * usbport applies the EHCI model, in which a root port can only ever have a
     * High Speed device enabled because Full and Low Speed devices are released
     * to a companion controller. Told otherwise, USBPORT_CreateDevice looks for
     * the transaction translator that model guarantees, and USBPORT_GetTt's
     * `TtCount <= 1` branch CONTAINING_RECORDs an empty TtList into 0xFFFFFFEC
     * rather than returning NULL - which survives USBPORT_OpenPipe's null check
     * and bugchecks both targets. The empty-list guard exists only on the
     * multi-TT branch; neither shipping build has ReactOS's early returns. The
     * instruction-level derivation is in docs/usb-xhci-info/usbport-miniport-abi.md section 8,
     * "The transaction-translator lookup".
     *
     * The lie is confined to this reporting layer. `shadow->Speed` keeps the
     * decoded class, and it - never usbport's DeviceSpeed - is what the slot and
     * endpoint contexts must be programmed from, along with EP0's max packet
     * size and the interval, since usbport will now derive all three on High
     * Speed rules (docs/contributing/implementation-invariants.md, "Root Hub Reporting").
     *
     * Gated on a connection because the speed bits mean nothing without one, and
     * that is exactly the window in which a real speed would have been reported.
     * XHCI_HUB_PORT_LOW_SPEED is now never set at all.
     */
    if ((portsc & XHCI_PORTSC_CCS) != 0) {
        status |= XHCI_HUB_PORT_HIGH_SPEED;
    }

    if (portStatus != NULL) {
        *portStatus = status;
    }
    if (portChange != NULL) {
        *portChange = (ULONG)shadow->Changes & XHCI_HUB_C_PORT_MASK;
    }
}

ULONG XhciPortShadowClearChange(XHCI_PORT_SHADOW *shadow, ULONG changeBit)
{
    ULONG had;

    if (shadow == NULL || changeBit == 0 ||
        (changeBit & ~XHCI_HUB_C_PORT_MASK) != 0) {
        return 0;
    }

    had = ((ULONG)shadow->Changes & changeBit) != 0 ? 1UL : 0UL;
    shadow->Changes = (UCHAR)((ULONG)shadow->Changes & ~changeBit);
    return had;
}

VOID XhciPortShadowLatchChange(XHCI_PORT_SHADOW *shadow, ULONG changeBits)
{
    if (shadow == NULL) {
        return;
    }
    shadow->Changes = (UCHAR)(((ULONG)shadow->Changes | changeBits) &
                              XHCI_HUB_C_PORT_MASK);
}

/* ------------------------------------------------------------------ */
/* The armed operations (roadmap Phase 5 task 4)                       */
/* ------------------------------------------------------------------ */

/*
 * Arming, claiming and disarming are here rather than beside the callbacks
 * because they are the part with no hardware in it - and because reset and
 * resume must share exactly one copy of them. Building the generation dance
 * twice is how the second copy gets a subtly different version of it; what the
 * two operations do *not* share is the completion rule, which lives at the call
 * sites in src/xhci_rh.c and is different for each.
 */

ULONG XhciPortShadowArm(XHCI_PORT_SHADOW *shadow, ULONG operation)
{
    ULONG generation;

    if (shadow == NULL || operation == XHCI_PORT_OP_NONE) {
        return 0;
    }
    /*
     * One operation per port at a time, and the exclusion is the point rather
     * than caution: a resume issued onto a port that is mid-reset would write
     * PLS into a register whose PR the controller is still acting on, and the
     * two timers would then both believe they own the port.
     */
    if (shadow->Armed != XHCI_PORT_OP_NONE) {
        return 0;
    }

    generation = shadow->Generation + 1UL;
    if (generation == 0) {
        /* 0 means "nothing was ever armed", so it is never handed out - a
         * wrapped counter must not make a stale callback current. */
        generation = 1;
    }
    shadow->Generation = generation;
    shadow->Armed = (UCHAR)operation;
    /*
     * Disarmed **here**, which is what lets the age be a stamp taken by the
     * first poll rather than a stamp plus a copy of the generation it belongs
     * to. The command engine needed both because its age sits in the extension
     * beside a single command and two commands can be issued between two polls;
     * this one lives inside the operation's own record and is disarmed by the
     * act of arming, so it can only ever describe the operation in flight -
     * including when the port is re-armed with no poll in between, which is the
     * case that would otherwise inherit a stamp from the last operation.
     */
    shadow->AgeArmed = 0;
    return generation;
}

/* ------------------------------------------------------------------ */
/* The Port Power confirmation the specification requires              */
/* ------------------------------------------------------------------ */

/*
 * "After modifying PP, software shall read PP and confirm that it is reached its
 * target state before modifying it again, undefined behavior may occur if this
 * procedure is not followed" (Table 5-27, p.375).
 *
 * The obligation spans callbacks, so it is state on the port rather than a check
 * inside whichever function issued the write. Every root-hub PORTSC write
 * composes a *neutral* value and a neutral value carries PP as it reads - so
 * while a `PP = 0` is in flight, an ordinary status query's change-bit
 * acknowledgement would write PP back as 1 and cancel it.
 */

VOID XhciPortShadowPpArm(XHCI_PORT_SHADOW *shadow, ULONG wanted)
{
    if (shadow == NULL) {
        return;
    }
    shadow->PpPending = 1;
    shadow->PpWanted = (UCHAR)(wanted ? 1 : 0);
    shadow->PpArmed = 0;
}

ULONG XhciPortShadowPpSettled(XHCI_PORT_SHADOW *shadow, ULONG portsc)
{
    ULONG now;

    if (shadow == NULL) {
        return 0;
    }
    if (shadow->PpPending == 0) {
        return 1;
    }
    /*
     * An all-ones read is not a confirmation of anything - it is a window that
     * has stopped decoding, and taking its PP bit at face value would confirm
     * "powered" for every port on a controller that is gone.
     */
    if (portsc == 0xFFFFFFFFUL) {
        return 0;
    }

    now = ((portsc & XHCI_PORTSC_PP) != 0) ? 1UL : 0UL;
    if (now != (ULONG)shadow->PpWanted) {
        return 0;
    }

    shadow->PpPending = 0;
    shadow->PpArmed = 0;
    return 1;
}

/*
 * **Both ages below were counts of health polls until task 13-R.3.5.** Sixteen
 * polls was meant to be eight seconds; on the E460, which polls this miniport
 * at 36-80 ms, it was 0.6-1.3 s - so the margin over the 500 ms deadline these
 * exist to sit behind was 1.2-2.6x rather than 16x, close enough that a reset
 * finishing late could be retired while its own timer was still legitimately
 * running. See the sizing rule above XHCI_COMMAND_AGE_MS in src/xhci_hw.h.
 *
 * `now` is a `XHCI_EXTENSION.PollClockMs` reading and `limitMs` a duration, and
 * the elapsed test is an unsigned difference so it is correct across the clock's
 * own 32-bit wrap. Two properties of the poll-count form are deliberately kept:
 *
 *   - **the stamp belongs to one operation by construction**, taken on the first
 *     poll that sees the wait outstanding and thrown away by the arm, rather
 *     than carried beside a copy of the generation it describes;
 *   - **the answer arrives exactly once per wait.** The poll count got that from
 *     an equality test, which a clock cannot make - it advances by whatever the
 *     period happened to be and steps over a threshold rather than landing on
 *     it. So each of these disarms itself as it answers.
 *
 * A `limitMs` of 0 fires on the poll after the stamp is taken rather than never,
 * which is the same direction the old limit clamp chose and for the same
 * reason: an age that arrives early is a diagnosis, one that never arrives is a
 * hang. There is no upper clamp to make - the old one existed because a UCHAR
 * could not count past 255.
 */

ULONG XhciPortShadowPpAge(XHCI_PORT_SHADOW *shadow, ULONG now, ULONG limitMs)
{
    if (shadow == NULL || shadow->PpPending == 0) {
        return 0;
    }
    if (!shadow->PpArmed) {
        shadow->PpArmed = 1;
        shadow->PpStamp = now;
        return 0;
    }
    if ((now - shadow->PpStamp) < limitMs) {
        return 0;
    }

    shadow->PpPending = 0;
    shadow->PpArmed = 0;
    return 1;
}

ULONG XhciPortShadowAge(XHCI_PORT_SHADOW *shadow, ULONG now, ULONG limitMs)
{
    if (shadow == NULL) {
        return 0;
    }
    if (shadow->Armed == XHCI_PORT_OP_NONE) {
        shadow->AgeArmed = 0;
        return 0;
    }
    if (!shadow->AgeArmed) {
        shadow->AgeArmed = 1;
        shadow->AgeStamp = now;
        return 0;
    }
    if ((now - shadow->AgeStamp) < limitMs) {
        return 0;
    }
    /* Disarmed here rather than left to the caller, because unlike the Port
     * Power wait above this one does not take the operation down itself - the
     * caller's retirement does - and without it every later poll would answer
     * on the same crossing. */
    shadow->AgeArmed = 0;
    return 1;
}

ULONG XhciPortShadowClaim(XHCI_PORT_SHADOW *shadow,
                          ULONG operation,
                          ULONG generation)
{
    if (shadow == NULL || generation == 0) {
        return 0;
    }
    if (shadow->Armed != operation || shadow->Generation != generation) {
        return 0;
    }

    shadow->Armed = XHCI_PORT_OP_NONE;
    shadow->ArmPending = 0;
    return 1;
}

ULONG XhciPortShadowDisarm(XHCI_PORT_SHADOW *shadow)
{
    ULONG was;

    if (shadow == NULL) {
        return 0;
    }

    was = (shadow->Armed != XHCI_PORT_OP_NONE) ? 1UL : 0UL;
    shadow->Armed = XHCI_PORT_OP_NONE;
    shadow->ArmPending = 0;
    /*
     * **The generation moves even when nothing was armed.** Disarming is what a
     * stop, a suspend and a reinitialization use to make every outstanding timer
     * stale, and a timer armed a moment ago against a port that has since been
     * claimed and re-armed would otherwise match again. Advancing unconditionally
     * costs one increment and removes the case analysis.
     */
    shadow->Generation++;
    if (shadow->Generation == 0) {
        shadow->Generation = 1;
    }
    return was;
}
