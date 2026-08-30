/*
 * xhci_rh.c - the root-hub callback family usbport.sys reaches, and the port
 * shadow behind it.
 *
 * Roadmap Phase 5 tasks 1 and 2. Under Option A the root hub PDO, its
 * descriptors and every hub-class request belong to usbport.sys; what this file
 * owns is the answer to four questions it asks - how many ports are there, what
 * is each one doing, do this to that port, and stop or start telling me about
 * changes - plus the one thing that arrives unasked, a Port Status Change
 * Event.
 *
 * **Why this is not src/xhci_port.c**, which the roadmap's "New files" table
 * named. Everything here reads or writes PORTSC, takes the driver's controller
 * lock, and lives on usbport's ABI; `xhci_port.c` is in `src/sources`' pure
 * list, which test/test_port.c compiles on the host with no DDK at all. Folding
 * these bodies in there would have ended that property for the file that most
 * needs it - the PORTSC write shapes are exactly the thing worth checking with
 * golden vectors. So the split follows the existing tiering: decisions and bit
 * layouts in the pure core, registers and locks here, and this file is compiled
 * into test_init instead (docs/contributing/design/03-host-unit-tests.md section 2).
 *
 * Three contracts run through every function, all binary-confirmed in
 * docs/usb-xhci-info/usbport-miniport-abi.md section 4 and none of them obvious:
 *
 *   1. A refusal is MP_STATUS_NOT_SUPPORTED. MP_STATUS_FAILURE is the one value
 *      usbport maps to RH_STATUS_NO_CHANGES, which leaves an endpoint-0 request
 *      queued rather than failing it - a hang, not an error.
 *   2. The two status queries succeed even when they have nothing to say. The
 *      status-change endpoint's scan treats any nonzero return as a hard error
 *      and abandons the whole scan, so one pessimistic refusal stalls the root
 *      hub's change pipe for every port on every future poll.
 *   3. The port index is this driver's to validate. usbport validates it on the
 *      class-command path only: the scan synthesizes 1..N itself, and Win2000's
 *      hub-directed feature path passes **0** to
 *      RH_ClearFeaturePortOvercurrentChange.
 *
 * Locking: the controller lock, per docs/contributing/design/05-locking-model.md
 * section 7. The status queries run under usbport's MiniportSpinLock and the
 * DPC that updates the same shadow runs under MiniportInterruptsSpinLock, which
 * is a different lock; the feature callbacks are documented as holding no
 * usbport lock, and whether a *caller* holds one was never established either
 * way. So the shadow is protected by this driver's own lock everywhere, and
 * nothing here calls a usbport service or waits.
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* Forward: armed by the two asynchronous port operations, and by the event path
 * for the resume a device starts by itself. */
static VOID NTAPI xhciRhPortTimeout(PVOID miniPortExtension, PVOID context);

/* Forward: the one way an armed operation ends without its own completion, used
 * by the health poll's age detector and by the port operations that preempt one.
 * Both must *report* what they ended, which is why they share it. */
#define XHCI_RH_RETIRE_AGED         0
#define XHCI_RH_RETIRE_PREEMPTED    1
static VOID xhciRhRetireOperation(PXHCI_EXTENSION ext,
                                  ULONG hubPort,
                                  XHCI_PORT_SHADOW *shadow,
                                  ULONG reason);

/* ------------------------------------------------------------------ */
/* Admission                                                           */
/* ------------------------------------------------------------------ */

/*
 * May this callback touch a register?
 *
 * The same three questions the ISR and the health poll ask, and for the same
 * reasons: HcInfoStatus is whether the register bases have ever been derived
 * (without it every accessor computes an offset from a zeroed structure and
 * reads some other device's mapping), INITIALIZED is whether a quiesce or a
 * re-initialization has taken the controller out of service, and
 * ControllerFailed is whether the recovery ladder has ended.
 *
 * The root hub survives a "no" rather than failing on it - the shadow is still
 * the last thing this driver knew, and reporting it is better than reporting a
 * hard error the scan cannot recover from. Only the operations that must reach
 * hardware refuse.
 *
 * IRQL: any. Callers hold the controller lock.
 */
static ULONG xhciRhAdmitted(PXHCI_EXTENSION ext)
{
    if (ext->HcInfoStatus != XHCI_HC_OK) {
        return 0;
    }
    if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0) {
        return 0;
    }
    /* Audit finding A4: the teardown's port-power pass writes PORTSC raw and
     * unlocked while INITIALIZED is still set, so this is the window in which a
     * concurrent callback would be the second writer. */
    if ((ext->Flags & XHCI_EXT_FLAG_RH_CLOSED) != 0) {
        return 0;
    }
    if (ext->ControllerFailed) {
        return 0;
    }
    return 1;
}

/*
 * **The one way this file writes PORTSC**, and the reason it is a function is a
 * rule that spans callbacks rather than living inside any of them.
 *
 * "After modifying PP, software shall read PP and confirm that it is reached its
 * target state **before modifying it again**, undefined behavior may occur if
 * this procedure is not followed" (Table 5-27, p.375) - and the delay is the
 * ordinary case rather than a corner: "the PP flag may be delayed in reflecting
 * this change, e.g. due to waiting for a port related state machine to complete
 * reset signaling" (footnote 91, p.375).
 *
 * Every value this family writes is built on `XhciPortscNeutral`, which carries
 * PP **exactly as it was read**. So while a `PP = 0` is in flight, any other
 * write to that port re-asserts the power it was removing - and the writer that
 * makes this reachable is not the interesting one. It is the ordinary
 * change-bit acknowledgement in xhciRhRefresh, which a plain status query or a
 * Port Status Change Event performs on whatever port they were asked about.
 *
 * So the confirmation is state on the port, not a check inside the callback that
 * took it on: a write is held back until a *read* of that port shows PP at the
 * value it was written. `portsc` is the caller's fresh reading, which is what
 * both settles the wait and composes the value.
 *
 * Returns 1 if the write was issued. IRQL: any. Caller holds the controller
 * lock.
 */
static ULONG xhciRhWritePortsc(PXHCI_EXTENSION ext,
                               ULONG xhciPort,
                               XHCI_PORT_SHADOW *shadow,
                               ULONG portsc,
                               ULONG value)
{
    if (!XhciPortShadowPpSettled(shadow, portsc)) {
        ext->RhPortPowerPending++;
        XHCI_DBG_VALUE_CHANGED("RH: holding a PORTSC write back - a Port Power "
                               "change is still in flight on port", xhciPort);
        return 0;
    }

    XhciWritePortsc(ext, xhciPort, value);
    return 1;
}

/*
 * The shadow of one hub port, or NULL when there is no such port.
 *
 * Every callback in this file goes through here rather than indexing
 * RootHub.Ports directly, because "hub port 3" is only a valid index once the
 * map has been built, and the map is rebuilt on every start - so a stale port
 * number from a previous topology has to answer "no such port" rather than an
 * entry describing a different connector.
 *
 * IRQL: any.
 */
static XHCI_PORT_SHADOW *xhciRhShadow(PXHCI_EXTENSION ext, ULONG hubPort)
{
    if (XhciRootHubPortOf(&ext->RootHub, hubPort) == 0) {
        return NULL;
    }
    return &ext->RootHub.Ports[hubPort - 1];
}

/*
 * Read one managed port's PORTSC, fold it into the shadow, and acknowledge the
 * change bits that reading it revealed.
 *
 * The acknowledgement is not optional and is not deferred: a PORTSC change bit
 * that nobody clears suppresses the controller's next Port Status Change Event
 * for that port, which on QEMU turned 24 hot-plug operations into 6 events
 * (docs/contributing/lessons.md, "hot-plug operations are not hot-plug events"). What the hub class calls the change is latched
 * in the shadow at the same instant, and *that* is what survives until the
 * matching clear-feature callback takes it down.
 *
 * **This is also where a reset finishes**, and it is here rather than in the
 * event path because all three latch sites can be the one that sees PRC: an
 * event arrives, but a status query racing it reads and acknowledges the same
 * bit first, and then the event's own refresh has nothing left to find. Whoever
 * observes it claims the armed generation, and there is exactly one of them
 * because the claim is a compare-and-disarm under the controller lock.
 *
 * **A resume deliberately does not finish here.** Its timer is a floor rather
 * than a deadline (see XHCI_PORT_OP_RESUME), so an event arriving mid-interval
 * updates the shadow and leaves the operation alone.
 *
 * Returns the hub-class change bits this refresh newly latched.
 *
 * IRQL: any (no wait). Caller holds the controller lock and has established
 * admission.
 */
static ULONG xhciRhRefresh(PXHCI_EXTENSION ext,
                           ULONG hubPort,
                           XHCI_PORT_SHADOW *shadow)
{
    ULONG port;
    ULONG portsc;
    ULONG ackBits;
    ULONG speedClass;
    ULONG latched;
    UCHAR prevSpeed;

    port = XhciRootHubPortOf(&ext->RootHub, hubPort);
    if (port == 0) {
        return 0;
    }

    portsc = XhciReadPortsc(ext, port);

    /*
     * **Every reading of a port is a chance to make the Port Power
     * confirmation, and this is where it is made.** It used to happen only
     * inside xhciRhWritePortsc, which a refresh calls only when there are change
     * bits to acknowledge - so on a *quiet* port the wait was never collected,
     * and the confirmation the specification asks for was coupled to whether
     * something unrelated happened to need writing. It is about PP, so it is
     * made from the read.
     */
    (VOID)XhciPortShadowPpSettled(shadow, portsc);

    /*
     * The speed decode needs the PSI table of the protocol group that owns the
     * port, which is the port map's job - and it answers XHCI_SPEED_UNKNOWN for
     * a PSIV the controller did not advertise rather than falling back to the
     * default IDs. An undecodable speed is reported as Full Speed by the shadow,
     * which is the answer every device can do.
     */
    speedClass = XHCI_SPEED_UNKNOWN;
    if (ext->PortMapStatus == XHCI_CAPS_OK && portsc != 0xFFFFFFFFUL) {
        (VOID)XhciPortSpeedClass(&ext->PortMap, port,
                                 XHCI_PORTSC_GET_SPEED(portsc), &speedClass);
    }

    prevSpeed = shadow->Speed;
    latched = XhciPortShadowUpdate(shadow, portsc, speedClass, &ackBits);

    /*
     * The decode is recorded here because after Phase 5 task 7 this is the only
     * place it survives: every connected port is reported to usbport as High
     * Speed, so RH_GetPortStatus's trace reads 0x0503 whatever arrived, and
     * nothing usbport is ever told distinguishes Full from Low from High. The
     * Phase 5 checkpoint asks for the decode per speed, so it has to be observed
     * on the way past.
     *
     * **The gate is semantic - "the first time this start decoded this speed
     * class" - and not a print budget, which is what makes it survive.** Every
     * trace macro caps at a per-site driver-image static that no start, stop or
     * resume resets, so any site that fires on ordinary bus activity is silent
     * by the time it is needed. A resume is ordinary bus activity here: HCRST
     * clears PP on every port, the ports are re-powered, the shadows are rebuilt
     * and every connected port is decoded again - and Win98 idle-suspends within
     * about half a second of a start, so that repeats indefinitely. Gating on
     * the transition alone fires every cycle; change-gating the value on top
     * does not help once more than one port is populated, because several
     * distinct values cycle round and each differs from the one before it.
     * Gating on the *set* bounds this at three prints for the life of a start
     * however many ports there are and however often the bus comes and goes,
     * which is why the plain XHCI_DBG_VALUE below needs no cap of its own -
     * and **that is what makes it safe with more than one controller**, where a
     * budgeted site is not. Every trace macro's state is per expansion, so it is
     * shared by every controller this image binds while the value it watches is
     * per controller; two controllers with different sets alternate at a
     * XHCI_DBG_VALUE_CHANGED site and exhaust it in seconds. A site with no
     * budget cannot be exhausted, and this one is still bounded in absolute
     * terms - three prints per controller per start.
     *
     * StartEpoch is in the value for the same reason: it is allocated from a
     * driver-image counter, so it names both which controller and which start
     * the decode belongs to, which a bare port number cannot.
     */
    if (shadow->Speed != prevSpeed) {
        ULONG seenBit;

        seenBit = 0;
        if (shadow->Speed == XHCI_SPEED_LOW) {
            seenBit = XHCI_RH_SEEN_LOW;
        } else if (shadow->Speed == XHCI_SPEED_FULL) {
            seenBit = XHCI_RH_SEEN_FULL;
        } else if (shadow->Speed == XHCI_SPEED_HIGH) {
            seenBit = XHCI_RH_SEEN_HIGH;
        }

        if (seenBit != 0 && (ext->RhSpeedsSeen & seenBit) == 0) {
            ext->RhSpeedsSeen |= seenBit;
            ext->RhFirstDecodes++;
            XHCI_DBG_VALUE("RH first decode of a speed "
                           "(epoch << 16 | hubPort << 8 | speed)",
                           (ext->StartEpoch << 16) | (hubPort << 8) |
                               (ULONG)shadow->Speed);
        }
    }

#ifdef XHCI_FIX_ACK_OWED
    /*
     * **Bench candidate W13 for Finding 3** (`run-13e.md` **Finding O**). The
     * shipping form of these six lines discards the write's answer, and that is
     * the defect: `xhciRhWritePortsc` refuses every write while a Port Power
     * change is in flight, `ackBits` is a stack local, and a refusal therefore
     * *drops* the acknowledgement. Nothing retries it - the health poll's only
     * refresh for this port is gated on `PpPending`, which `XhciPortShadowPpAge`
     * clears on give-up - so the change bit stays set, and per `lessons.md`
     * entry that suppresses the controller's next Port Status Change Event
     * for this port. The port is out of service until the driver restarts.
     *
     * So carry the debt instead of dropping it. **The retry is composed against
     * the fresh `portsc` of whichever refresh eventually succeeds**, never
     * against the reading that produced the refusal: a stale full register value
     * replayed into PORTSC would restart a reset or disable a port, which is the
     * hazard this file's header warns about.
     */
    ackBits |= shadow->PortscAckOwed;

    if (ackBits != 0) {
        if (xhciRhWritePortsc(ext, port, shadow, portsc,
                              XhciPortscClearChanges(portsc, ackBits))) {
            shadow->PortscAckOwed = 0;
        } else {
            shadow->PortscAckOwed = ackBits;
        }
    }
#else
    if (ackBits != 0) {
        (VOID)xhciRhWritePortsc(ext, port, shadow, portsc,
                                XhciPortscClearChanges(portsc, ackBits));
    }
#endif

    /*
     * The reset's finish line. PRC is "set when the Port Reset bit transitions
     * from '1' to '0' due to a Reset on this port" (Table 5-27, p.377), so it is
     * the completion rather than a hint of one - which is why claiming here
     * cannot be early, and why the watchdog below is a deadline and not a
     * second opinion. The hub-class C_PORT_RESET it implies was latched by the
     * update above; all that is left is to stop timing it.
     */
    if ((latched & XHCI_HUB_C_PORT_RESET) != 0 &&
        shadow->Armed == XHCI_PORT_OP_RESET) {
        (VOID)XhciPortShadowClaim(shadow, XHCI_PORT_OP_RESET,
                                  shadow->Generation);
        ext->RhResetsCompleted++;
        XHCI_DBG_VALUE_CHANGED("RH reset: completed on hub port", hubPort);
    }

    /*
     * **Task 11-V.9's second tier, and this function is where three of its
     * events are visible at once**: a connect, a disconnect and a completed
     * reset. It is the one place that sees a port's before and after, which is
     * also why the two announcements below are made from here.
     *
     * A connect and a disconnect are the *same* change bit and are told apart
     * by the current connect state, exactly as the teardown below tells them
     * apart - so they are one record with the state in it rather than two, and
     * a reader cannot see one without the other.
     *
     * The speed is `shadow->Speed`, which is this driver's decode and **not
     * what usbport is told**: every connected root port is reported to usbport
     * as High Speed (Phase 5 task 7), so a log that carried the reported speed
     * would say 0x0503 whatever was plugged in. This is the one channel on
     * which "a Full Speed device arrived on port 2" survives at all.
     *
     * One record per occurrence, which is the tier's whole budget claim: a port
     * change is a physical event, and a machine whose port is chattering is a
     * machine whose log should say so rather than one whose log hides it.
     */
    if ((latched & XHCI_HUB_C_PORT_CONNECTION) != 0) {
        XhciLogNoteLocked(ext,
                          ((portsc & XHCI_PORTSC_CCS) != 0) ? "port.connect"
                                                            : "port.disconnect",
                          (hubPort << 8) | (ULONG)shadow->Speed);
    }
    if ((latched & XHCI_HUB_C_PORT_RESET) != 0) {
        XhciLogNoteLocked(ext, "port.reset.done",
                          (hubPort << 8) | (ULONG)shadow->Speed);
    }
    if ((latched & XHCI_HUB_C_PORT_ENABLE) != 0 &&
        (portsc & XHCI_PORTSC_PED) == 0) {
        /* A port *disable* is the device-gone event this driver spent batch
         * 6-V finding out it must not ignore, so it is worth a record of its
         * own rather than being folded into the connect line. */
        XhciLogNoteLocked(ext, "port.disable", hubPort);
    }

    /*
     * The two announcements the device layer needs, and both are made from here
     * because this is the one function that sees a port's before and after
     * (Phase 6 tasks 6-B.2 and 6-B.5).
     *
     * A completed reset is the *only* thing that can associate the address-0
     * pipe with a root port: usbhub resets a port and then immediately creates a
     * device on it, and usbport's endpoint properties carry the transaction
     * translator rather than the root port. It is recorded whether or not this
     * driver was timing the reset, because a reset usbhub drove and a reset that
     * completed without our watchdog mean the same thing to enumeration.
     *
     * A connect change is the only teardown trigger there is - neither shipping
     * build calls `CloseEndpoint`, and an endpoint is deleted with no callback
     * at all (batch 6-0) - and it is a teardown in *both* directions, because a
     * change on a port that already has a device means that device has gone
     * whether or not something else has arrived.
     *
     * **The teardown goes first, and since task 7b-A.1.1 the order is
     * load-bearing rather than arbitrary.** That task makes the reset's decision
     * - whether it entitles an address-0 open to claim this port - depend on
     * whether a record on the port is still mid-enumeration. When both changes
     * latch together the device that record describes is gone, so the reset is
     * the *start* of a new enumeration and must be read against a port the
     * teardown has already cleared. Announcing the reset first would have it read
     * the outgoing device's record and decline to arm a claim the incoming one
     * needs. Nothing else depends on the order: the teardown does not touch the
     * enumeration hint, and the reset touches nothing else.
     */
    if ((latched & XHCI_HUB_C_PORT_CONNECTION) != 0) {
        XhciSlotPortConnectChanged(ext, hubPort);
    }
    if ((latched & XHCI_HUB_C_PORT_RESET) != 0) {
        XhciSlotPortReset(ext, hubPort);
    }

    return latched;
}

/*
 * A port that is signalling resume and has nothing timing the end of it.
 *
 * This is the **device-initiated** half of 4.15.2.1 (p.256): a suspended port
 * that sees resume signalling from the device enters the Resume state by itself
 * and sets PLC, and "software shall start timing TDRSMDN from the notification
 * of the transition to the Resume state. After TDRSMDN is complete, software
 * shall write a '0' to the PLS field". Nothing else ends it - the xHC will not
 * leave the Resume state on its own - so a port left here is a device that
 * signalled a wake-up and was never answered.
 *
 * The predicate is the *link state*, not PLC, and that is deliberate. PLC is
 * acknowledged by whichever refresh observes it first, so a status query racing
 * the event consumes the notification and the event's own refresh would see
 * nothing to arm; the state, by contrast, persists until software ends it. So
 * the two callers here - the event path and the health poll's sweep - ask the
 * same question and the second is the safety net for the first.
 *
 * IRQL: any. Caller holds the controller lock. Sets ArmPending rather than
 * arming, because arming is a usbport service.
 */
static VOID xhciRhArmDeviceResume(PXHCI_EXTENSION ext,
                                  ULONG hubPort,
                                  XHCI_PORT_SHADOW *shadow)
{
    if (shadow->Armed != XHCI_PORT_OP_NONE) {
        return;
    }
    if ((shadow->Portsc & XHCI_PORTSC_CCS) == 0 ||
        XHCI_PORTSC_GET_PLS(shadow->Portsc) != XHCI_PLS_RESUME) {
        return;
    }
    /*
     * Refused rather than started when there is no timer, for the reason the
     * host-initiated resume refuses: an interval nothing ends is worse than one
     * that never began. Here the port is already signalling, so this driver
     * cannot make it worse - but arming a generation with no callback behind it
     * would leave the port permanently ineligible for any other operation.
     */
    if (!XhciAsyncTimerAvailable()) {
        ext->RhTimerFailures++;
        return;
    }

    if (XhciPortShadowArm(shadow, XHCI_PORT_OP_RESUME) == 0) {
        return;
    }
    shadow->ArmPending = 1;
    ext->RhPortsResumed++;
    XHCI_DBG_VALUE_CHANGED("RH resume: device-initiated on hub port", hubPort);
}

/* ------------------------------------------------------------------ */
/* Root-hub description                                                */
/* ------------------------------------------------------------------ */

/*
 * RH_GetRootHubData - how many ports, and what kind of hub.
 *
 * **NumberOfPorts must never be zero**, whatever state this driver is in.
 * usbport sizes the root hub descriptor's removable and power masks as
 * ((NumberOfPorts - 1) >> 3) + 1 with no guard in either shipping build, so a
 * zero unsigned-wraps into a request for about 1 GB of nonpaged pool
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 4, confirmed from both binaries
 * Phase 5 task 1). A driver that does not yet know its port count reports one
 * permanently disconnected port instead - the Phase 3 stub's answer, kept here
 * for the case where the map was never built.
 *
 * HubCharacteristics reports individual power switching when the controller has
 * port power switches, and individual over-current protection unconditionally:
 * PORTSC gives every port its own PP and its own OCA either way, and the
 * over-current an xHCI controller reports is per port by construction.
 *
 * PowerOnToPowerGood is in 2 ms units and is copied straight into the
 * descriptor's bPowerOnToPowerGood as a UCHAR, so xHCI's 20 ms rule is the
 * value 10.
 *
 * IRQL: DISPATCH_LEVEL.
 */
VOID XhciRhGetRootHubData(PXHCI_EXTENSION ext, PUSBPORT_ROOT_HUB_DATA data)
{
    KIRQL oldIrql;
    ULONG ports;
    ULONG characteristics;

    if (data == NULL) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    ports = (ext->RootHub.Status == XHCI_RH_OK) ? ext->RootHub.PortCount : 0;
    characteristics = XHCI_HUB_CHAR_OC_INDIVIDUAL;
    if (ext->HcInfoStatus == XHCI_HC_OK && ext->HcInfo.Ppc) {
        characteristics |= XHCI_HUB_CHAR_POWER_INDIVIDUAL;
    } else {
        characteristics |= XHCI_HUB_CHAR_POWER_GANGED;
    }
    XhciControllerLockRelease(oldIrql);

    if (ports == 0) {
        ports = 1;
        XHCI_DBG_TEXT("RH_GetRootHubData: no port map - reporting one "
                      "synthetic disconnected port rather than zero");
    }

    data->NumberOfPorts = ports;
    data->HubCharacteristics = (USHORT)characteristics;
    data->Padded1 = 0;
    data->PowerOnToPowerGood = XHCI_RH_POWER_ON_TO_POWER_GOOD;
    data->HubControlCurrent = 0;

    XHCI_DBG_VALUE_CHANGED("RH_GetRootHubData: managed ports reported", ports);
}

/*
 * RH_GetStatus - the root hub *device's* GET_STATUS, reached through the
 * standard command path rather than the class one. Self-powered, no remote
 * wakeup: the root hub is the machine.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
MPSTATUS XhciRhGetStatus(PXHCI_EXTENSION ext, PUSHORT status)
{
    (VOID)ext;

    if (status == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    *status = (USHORT)XHCI_HUB_SELF_POWERED;
    return MP_STATUS_SUCCESS;
}

/*
 * RH_GetHubStatus - the hub's own status and change, both constant zero.
 *
 * There is nothing for them to carry. Local power is the machine's supply, and
 * over-current is reported per port through PORTSC.OCA rather than for the hub
 * as a whole - which is also what makes the hub characteristics above say
 * "individual over-current protection".
 *
 * Succeeds unconditionally: a nonzero return here abandons the entire
 * status-change scan, and there is no state this could fail on.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
MPSTATUS XhciRhGetHubStatus(PXHCI_EXTENSION ext,
                            PUSBPORT_HUB_STATUS_AND_CHANGE status)
{
    if (status == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * **Incremented without the controller lock**, unlike `RhRefusals` below,
     * and the reason is worth stating exactly because an earlier version of
     * this comment stated it wrongly.
     *
     * What IS established: this driver has exactly **one** writer of this
     * counter - this line - and every other reference is a read (the trace site
     * in `xhci_dispatch.c` and the snapshot the tool serves). So it is written
     * unlocked from one place and never locked from another, which is the rule
     * `xhciRhRefuse`'s note asks for; what that note forbids is a counter with
     * *two* disciplines, not one without a lock.
     *
     * What is **NOT** established, and must not be written here as though it
     * were: whether the caller holds `MiniportSpinLock` at root-hub callback
     * entry. ReactOS takes it around the status-query callbacks, but whether
     * NUSB's usbport repeats that contract is **open question 9** in
     * `docs/usb-xhci-info/usbport-miniport-abi.md`, still open after three
     * attempts to close it, and that document's standing instruction is that
     * the miniport is written **not to depend on the answer**. So the safety
     * argument here is not serialisation. It is that a lost increment on a
     * diagnostic counter is a wrong number in a report and nothing else - this
     * value steers no decision in the driver.
     *
     * **If this counter ever gains a second writer, or is ever read to decide
     * something, take the lock** - neither is true today and the argument above
     * does not survive either.
     */
    ext->RhHubStatusQueries++;
    status->HubStatus = 0;
    status->HubChange = 0;
    return MP_STATUS_SUCCESS;
}

/*
 * RH_GetPortStatus - one port's hub-class status and change.
 *
 * It **reads and may write PORTSC**, which makes it the one status query in
 * this family that is not read-only, and the write is the change-bit
 * acknowledgement described at xhciRhRefresh. That is deliberate rather than
 * incidental: the alternative is to acknowledge only from the event path, and
 * the event path is exactly what a stuck change bit switches off.
 *
 * Every refusal answers zeros and MP_STATUS_SUCCESS, for the reason at the top
 * of this file. The single exception is a NULL buffer, where there is nothing
 * to report *into* - and usbport validates the buffer before the scan, so it is
 * unreachable from either shipping build.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
MPSTATUS XhciRhGetPortStatus(PXHCI_EXTENSION ext,
                             USHORT port,
                             PUSBPORT_PORT_STATUS_AND_CHANGE status)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;
    ULONG portStatus;
    ULONG portChange;

    if (status == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }

    status->PortStatus = 0;
    status->PortChange = 0;

    XhciControllerLockAcquire(&oldIrql);
    ext->RhPortStatusQueries++;

    shadow = xhciRhShadow(ext, (ULONG)port);
    if (shadow == NULL) {
        ext->RhInvalidPort++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH_GetPortStatus: no such managed port",
                               (ULONG)port);
        return MP_STATUS_SUCCESS;
    }

    /*
     * This refresh may latch a change, and it is the **one** latch site that
     * deliberately does not queue a root-hub invalidation - see
     * RootHubInvalidatesOwed. The other two (the start/resume seed and the Port
     * Status Change Event path) latch into a shadow nobody is currently looking
     * at, so the change has to be announced or it is lost. Here usbport is
     * asking at this instant and the change goes back in the answer below;
     * queuing one as well would ask it to poll for something it has just been
     * handed.
     */
    if (xhciRhAdmitted(ext)) {
        (VOID)xhciRhRefresh(ext, (ULONG)port, shadow);
    }
    XhciPortShadowReport(shadow, &portStatus, &portChange);
    XhciControllerLockRelease(oldIrql);

    status->PortStatus = (USHORT)portStatus;
    status->PortChange = (USHORT)portChange;

    /*
     * **The refresh above may have torn a device down**, and this is the one
     * refresh site that does not reach XhciRootHubDeferredWork - deliberately,
     * because the change it latched goes back in the answer rather than into an
     * announcement (see the note above). That argument is about the root hub's
     * own work and does not extend to the device layer's: a connect change
     * observed here completes queued transfers and owes a Disable Slot, neither
     * of which can happen under the lock.
     *
     * The rule this leaves behind, and it is the one to check when a refresh
     * site is added: **anything that can call xhciRhRefresh outside the event
     * DPC owes XhciSlotDeferredWork.** The suite caught this site missing it.
     */
    XhciSlotDeferredWork(ext);

    XHCI_DBG_VALUE_CHANGED("RH_GetPortStatus: status and change",
                           portStatus | (portChange << 16));
    return MP_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Port operations                                                     */
/* ------------------------------------------------------------------ */

#define XHCI_RH_OP_POWER_ON     0
#define XHCI_RH_OP_POWER_OFF    1
#define XHCI_RH_OP_DISABLE      2
#define XHCI_RH_OP_SUSPEND      3

/*
 * Has a disable or power-off actually taken this port out of service?
 *
 * Each operation is confirmed in its own target bit, which is the rule the
 * preemption block arrived at and which applies here for the same reason: `PED`
 * "does not change until the port state actually changes. There may be a delay
 * in disabling or enabling a port due to other host controller or bus events"
 * (Table 5-27, p.372), and `PP` may lag while a reset finishes signalling
 * (footnote 91, p.375). An all-ones read answers no - a window that has stopped
 * decoding says nothing about the port, and treating it as confirmation would
 * be the "all-ones is not a status report" mistake this driver has made before.
 *
 * IRQL: <= DISPATCH_LEVEL, controller lock held.
 */
static ULONG xhciRhPortIsDisowned(PXHCI_EXTENSION ext, ULONG xhciPort,
                                  ULONG wantsPp)
{
    ULONG portsc;

    portsc = XhciReadPortsc(ext, xhciPort);
    if (portsc == 0xFFFFFFFFUL) {
        return 0;
    }
    if (wantsPp) {
        return ((portsc & XHCI_PORTSC_PP) == 0) ? 1UL : 0UL;
    }
    return ((portsc & XHCI_PORTSC_PED) == 0) ? 1UL : 0UL;
}

/*
 * The shape every feature callback shares: validate the port, take the lock,
 * check admission, read PORTSC, write one composed value, release.
 *
 * `operation` is one of the XHCI_RH_OP_* below and decides only which builder
 * runs; keeping them in one function is what keeps "clearing power" and
 * "disabling the port" from being written as the same expression twice, which
 * is the mistake src/xhci_port.c's named builders exist to prevent one layer
 * down.
 *
 * An all-ones PORTSC read is refused rather than written back through: it is a
 * window that has stopped decoding, and composing a write from it would send a
 * neutral value to a register that is not there while reporting success.
 *
 * IRQL: DISPATCH_LEVEL. Calls usbport services only through
 * XhciRootHubDeferredWork, after releasing the controller lock.
 */
static MPSTATUS xhciRhPortOperation(PXHCI_EXTENSION ext,
                                    USHORT port,
                                    ULONG operation)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;
    ULONG xhciPort;
    ULONG portsc;
    ULONG value;
    ULONG preempt;

    preempt = 0;
    XhciControllerLockAcquire(&oldIrql);

    shadow = xhciRhShadow(ext, (ULONG)port);
    xhciPort = XhciRootHubPortOf(&ext->RootHub, (ULONG)port);
    if (shadow == NULL || xhciPort == 0) {
        ext->RhInvalidPort++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH port operation: no such managed port",
                               (ULONG)port);
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (!xhciRhAdmitted(ext)) {
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH port operation: controller not in service, "
                               "operation", operation);
        return MP_STATUS_NOT_SUPPORTED;
    }

    portsc = XhciReadPortsc(ext, xhciPort);
    if (portsc == 0xFFFFFFFFUL) {
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH port operation: PORTSC reads all ones on "
                               "port", xhciPort);
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * **A Port Power change this driver issued has not landed yet.** The
     * specification's rule is "before modifying it again" (Table 5-27, p.375),
     * so this is a refusal rather than a write held back: answering success for
     * an operation that was not performed is the one thing worse than declining
     * it. `XhciPortShadowPpSettled` clears the wait when this reading shows the
     * change arrived, so the ordinary case costs a comparison.
     */
    if (!XhciPortShadowPpSettled(shadow, portsc)) {
        ext->RhPortPowerPending++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH: refusing - a Port Power change is still in "
                               "flight on hub port", (ULONG)port);
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * **What this port is already doing, before deciding what to do to it.**
     *
     * The armed state exists to exclude any *other* operation on that port, and
     * the first version of this family enforced that only between reset and
     * resume - so a power-off or a disable could land in the middle of one. That
     * is not a theoretical race: `RH_ClearFeaturePortPower` takes VBus away
     * while the controller is driving a bus reset or 20 ms of resume signalling,
     * and `RH_ClearFeaturePortEnable` writes PED into a port mid-reset.
     *
     * The answer is not the same for all four, because they do not mean the same
     * thing to a port with an operation in flight:
     *
     *   **Power off and disable proceed rather than being refused.** They are
     *   the caller taking the port out of service, and refusing them would be
     *   worse than the disruption they cause: powering a port down is precisely
     *   usbhub's recovery for a reset that is not finishing, so declining it
     *   would block the recovery on the strength of the thing being recovered
     *   from. What happens to the armed operation is xhciRhRetireOperation's,
     *   and it is **not the same for the two operations** - see the argument
     *   there. A resume ends here; a reset does not, because this driver cannot
     *   end one and must not say it has.
     *
     *   **Suspend refuses.** Its own precondition happens to refuse in both
     *   reachable cases already - a port mid-reset has PED = 0, a port
     *   mid-resume has PLS = 15 - and that is exactly why this is written down:
     *   the exclusion should not rest on an accident of another rule.
     *
     *   **Power on does neither.** A port cannot be mid-reset without PP, so
     *   there is nothing to conflict with; the neutral write carries no PR and
     *   no LWS, so it disturbs neither operation.
     */
    if (shadow->Armed != XHCI_PORT_OP_NONE) {
        if (operation == XHCI_RH_OP_SUSPEND) {
            ext->RhPortsBusy++;
            ext->RhRefusals++;
            XhciControllerLockRelease(oldIrql);
            XHCI_DBG_VALUE_CHANGED("RH_SetFeaturePortSuspend: an operation is "
                                   "already armed on hub port", (ULONG)port);
            return MP_STATUS_NOT_SUPPORTED;
        }
        if (operation == XHCI_RH_OP_POWER_OFF ||
            operation == XHCI_RH_OP_DISABLE) {
            /*
             * Decided here and performed **after the write below**. The write is
             * what terminates the operation in hardware, and the retire is what
             * reports that it ended - so doing it in this order is the
             * difference between a report and a prediction.
             *
             * **A disable cannot end a reset**, and needs no special case to
             * say so: PED is RW1C and "shall automatically be cleared to '0'
             * when PR is set to '1'" (Table 5-27, p.372), so on a resetting port
             * it already reads 0, a written 1 clears nothing, and the port never
             * reaches the Powered-off state the confirmation below looks for. A
             * *resume* is preempted by either write: there PED is 1, so the
             * disable really does take effect.
             */
            preempt = 1;
        }
    }

    /*
     * The one operation with a precondition of its own. "Software should only
     * set the PLS field to '3' when the port is in the Enabled state" and
     * "software should not attempt to suspend a port unless the port reports
     * that it is in the enabled (PED = '1', PLS < '3') state" (4.15.1, p.255).
     * Suspending a disabled port, or one already in U3 or Resume, is a write
     * with no defined outcome - refused rather than issued and reported as a
     * success.
     */
    if (operation == XHCI_RH_OP_SUSPEND &&
        ((portsc & XHCI_PORTSC_PED) == 0 ||
         XHCI_PORTSC_GET_PLS(portsc) >= XHCI_PLS_U3)) {
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH_SetFeaturePortSuspend: port is not enabled "
                               "and in a link state below U3, PORTSC", portsc);
        return MP_STATUS_NOT_SUPPORTED;
    }

    switch (operation) {
    case XHCI_RH_OP_POWER_ON:
        value = XhciPortscPower(portsc, 1);
        ext->RhPortsPowered++;
        break;
    case XHCI_RH_OP_POWER_OFF:
        value = XhciPortscPower(portsc, 0);
        ext->RhPortsUnpowered++;
        break;
    case XHCI_RH_OP_DISABLE:
        value = XhciPortscDisable(portsc);
        ext->RhPortsDisabled++;
        break;
    default:
        value = XhciPortscSuspend(portsc);
        ext->RhPortsSuspended++;
        break;
    }

    (VOID)xhciRhWritePortsc(ext, xhciPort, shadow, portsc, value);
    if (operation == XHCI_RH_OP_POWER_ON || operation == XHCI_RH_OP_POWER_OFF) {
        /* The confirmation this write now owes, which outlives this callback. */
        XhciPortShadowPpArm(shadow,
                            (operation == XHCI_RH_OP_POWER_ON) ? 1UL : 0UL);
    }

    /*
     * **Taking a port out of service takes its device with it** (task 6-B.5's
     * second trigger, added after the batch 6-V Win98 run). usbhub abandons a
     * device by disabling its port with the device still connected - a cancelled
     * driver install does exactly this - and no connect change follows, so the
     * connect-change trigger never fires and the record survives holding a USB
     * address usbport has already recycled.
     *
     * Both write shapes count. A power-off removes VBus, which is a stronger
     * statement than a disable and produces no change bits either (the exclusion
     * quoted above), so a device left recorded across one is in the same
     * position.
     *
     * **And it is gated on the port confirming the write, which the first draft
     * was not.** That draft tore down immediately after the write, on the same
     * "reporting before it would be a prediction" reasoning the block above
     * applies to change bits - and got it backwards, because a teardown is not a
     * report. It completes the device's queued transfers, and
     * `UsbPortCompleteTransfer` hands their mapped buffers back to usbport while
     * the TRBs naming them may still be executable on a port that has not gone
     * down yet. `PP` in particular "may be delayed in reflecting this change,
     * e.g. due to waiting for a port related state machine to complete reset
     * signaling" (footnote 91 to Table 5-27, p.375) - so the window is the
     * documented one, not a theoretical one. It is the same
     * DMA-into-reclaimed-memory hazard `XhciSlotInvalidateAll`'s evidence rule
     * exists for, reintroduced in the file that explains it.
     *
     * Each operation is confirmed in **its own** target bit, the rule the
     * `settled` block below arrived at: `PP` for a power-off, `PED` for a
     * disable. A port that has not landed yet keeps the obligation on its
     * shadow and the health poll collects it, exactly as `PpPending` is
     * collected - which is why this needs no new sweep.
     */
    if (operation == XHCI_RH_OP_DISABLE || operation == XHCI_RH_OP_POWER_OFF) {
        ULONG disownWantsPp;

        /*
         * **The software half runs unconditionally**, and a Win98 run is why.
         * Gating it on the hardware left the address map stale on every path
         * where the write did not land - declined by the "confirm before
         * modifying again" holdback, refused because the controller was
         * suspended, or simply never confirmed - which is the whole defect this
         * trigger exists for, still open.
         */
        XhciSlotPortDisowned(ext, (ULONG)port);

        disownWantsPp = (operation == XHCI_RH_OP_POWER_OFF) ? 1UL : 0UL;
        if (xhciRhPortIsDisowned(ext, xhciPort, disownWantsPp)) {
            /* Clearing a debt this port may already have been carrying, not
             * just recording that this one needs none. A power-off whose `PP`
             * never confirmed leaves the obligation set; a later disable that
             * confirms `PED` immediately would otherwise leave it standing
             * unbounded against a record the teardown below has just taken -
             * and skew the disowned/disabled counter pair the comment above
             * says to read together. The teardown is idempotent, so one
             * settlement discharges both. */
            shadow->DisownPending = 0;
            shadow->DisownWantsPp = 0;
            XhciSlotPortDisabled(ext, (ULONG)port);
        } else {
            shadow->DisownPending = 1;
            shadow->DisownWantsPp = (UCHAR)disownWantsPp;
        }
        /*
         * Which branch ran, because the first target run of this code could not
         * be diagnosed from its own log: the teardown did not happen and the
         * three candidate reasons were indistinguishable. Bit 0 is "the port
         * confirmed at once", bit 1 "the confirmation is owed".
         */
        XHCI_DBG_VALUE_CHANGED("RH: port disown, hub port << 8 | outcome",
                               ((ULONG)port << 8) |
                                   (shadow->DisownPending ? 2UL : 1UL));
    }

    /*
     * **Now, and only on what the port says afterwards.**
     *
     * What this write ends is an operation the controller will say nothing
     * about: "this flag [PRC] shall not be set to '1' if the reset processing
     * was forced to terminate due to software clearing PP or PED to '0'" (Table
     * 5-27, p.377), and the same note appears on CSC, PEC, PLC and WRC - a
     * software-initiated power-off produces **no change bits at all**. So no
     * event is coming and no refresh will ever observe the end: this driver is
     * the only thing that can report it.
     *
     * **But issuing the write is not the same as the port having stopped**, and
     * the specification names this exact case as the reason: "a port
     * implementation shall initiate a Port Power change immediately when PP is
     * written, however the PP flag may be delayed in reflecting this change,
     * e.g. due to waiting for a port related state machine to complete reset
     * signaling" (footnote 91 to Table 5-27, p.375), with "after modifying PP,
     * software shall read PP and confirm that it is reached its target state ...
     * undefined behavior may occur if this procedure is not followed" (p.375).
     * A reset in progress is precisely the state that delays it. Reporting
     * C_PORT_RESET on the strength of the write alone would say the sequence had
     * completed while the port was still signalling - the same defect an earlier
     * round fixed at the top of this function, moved one step later.
     *
     * So the reset is retired only when the port is *observed* to have reached
     * the Powered-off state; otherwise it keeps its armed generation and its own
     * watchdog, which is what that deadline is for. The read costs one access on
     * a path that already writes, and it is asking a different question from the
     * "no read back" rule below - that one is about whether an asynchronous
     * effect has landed, and is safe to skip because the change bits report it
     * later. Here there will be no change bit, which is why this one has to look.
     *
     * **The bit it looks at is PP, not PR, and that distinction is the whole
     * check.** "This flag is '0' if PP is '0'" (PR, Table 5-27, p.372) - so
     * once power is off, PR reads 0 whether or not the port ever left the
     * Resetting state, and a PR-based test confirms the thing it is standing on.
     * It is also wrong in the other direction: in the lag window the reset can
     * complete *normally* (PR 1->0, PRC set) while PP is still catching up, and
     * a PR test would call that a preemption - retiring the operation, reporting
     * it, and leaving a real PRC pending for the next refresh to mis-attribute
     * to whatever is armed by then. PP is what the specification says to confirm
     * ("software shall read PP and confirm that it is reached its target state",
     * p.375) and it is the state that makes the termination true.
     */
    if (preempt) {
        ULONG after;
        ULONG settled;
        ULONG endedReset;
        ULONG latched;

        after = XhciReadPortsc(ext, xhciPort);

        /*
         * **Two questions, and they are not the same one** - which is the
         * mistake a review round had just been fixed for, repeated one level
         * down when a single flag was asked to answer both.
         *
         * `settled` is *may this path compose another write from what the port
         * now reads*. The acknowledgement below is a neutral write, and a
         * neutral value carries `PP` exactly as it was read - so performing one
         * while a `PP = 0` is still in flight would re-assert the power this
         * call just removed, and is "modifying it again" before confirming,
         * which p.375 says may produce undefined behaviour. A disable does not
         * have that problem: it changes `PED`, which the neutral value clears
         * rather than preserves.
         *
         * `endedReset` is *did this operation end the reset* - which only a
         * power-off can do, and only once the port has actually reached the
         * Powered-off state. A disable is `settled` and ends nothing: PED "shall
         * automatically be cleared to '0' when PR is set to '1'" (p.372), so on
         * a resetting port a written 1 clears a bit that already reads 0.
         */
        settled = 0;
        if (after != 0xFFFFFFFFUL) {
            /*
             * Each operation is confirmed in **its own** target bit, which is
             * the rule a review round arrived at for the power-off and which
             * applies just as much to the disable: PED "does not change until
             * the port state actually changes. There may be a delay in disabling
             * or enabling a port due to other host controller or bus events"
             * (Table 5-27, p.372).
             */
            if (operation == XHCI_RH_OP_POWER_OFF) {
                settled = ((after & XHCI_PORTSC_PP) == 0) ? 1UL : 0UL;
            } else {
                settled = ((after & XHCI_PORTSC_PED) == 0) ? 1UL : 0UL;
            }
        }
        /* Only a power-off ends a reset - a disable writes a PED that already
         * reads 0 on a resetting port, so it clears nothing. */
        endedReset = (operation == XHCI_RH_OP_POWER_OFF && settled) ? 1UL : 0UL;

        if (settled) {
            /*
             * **Fold the port in before concluding anything about it**, and the
             * reason is a change bit that is already there rather than one this
             * write produced. A reset that completed in the moments *before*
             * this call left `PRC` set and nothing has drained it - `PRC` is
             * RW1CS, so it sits there until software clears it, and the
             * power-off's own write carries no change bit and cannot. Retiring
             * without acknowledging it would report this reset as preempted and
             * leave a genuine completion behind, which the next refresh would
             * attribute to whatever is armed by then: usbhub re-powers, starts a
             * second reset, and that reset is completed microseconds later by
             * the first one's leftover.
             *
             * Doing it through the ordinary refresh also gets the *diagnosis*
             * right for free. If the `PRC` is real the refresh latches
             * C_PORT_RESET and claims the armed generation itself - a
             * completion, counted as one - and there is nothing left to preempt.
             */
            latched = xhciRhRefresh(ext, (ULONG)port, shadow);
            if (latched != 0) {
                ext->RootHubInvalidatesOwed++;
            }
        }

        if (!settled) {
            /*
             * **The interrupting write has not taken effect, so it has ended
             * nothing** - and that is as true of a resume as of a reset. An
             * earlier version tested only the reset here and retired a resume
             * regardless, which disarmed the one thing that was going to write
             * the terminating U0 while the port was very possibly still
             * signalling: an orphan, with nothing left to end it. Whatever is
             * armed stays armed, and its own timer finishes the port.
             */
            ext->RhPreemptsUnconfirmed++;
            XHCI_DBG_VALUE_CHANGED("RH port operation: it has not taken effect "
                                   "yet, PORTSC", after);
        } else if (shadow->Armed == XHCI_PORT_OP_RESET && !endedReset) {
            /* Still resetting, and this operation is not what ends one. It keeps
             * its armed generation and the watchdog behind it. */
            ext->RhResetsUnconfirmed++;
            XHCI_DBG_VALUE_CHANGED("RH port operation: the reset is still "
                                   "running after it, PORTSC", after);
        } else if (shadow->Armed != XHCI_PORT_OP_NONE) {
            xhciRhRetireOperation(ext, (ULONG)port, shadow,
                                  XHCI_RH_RETIRE_PREEMPTED);
        }
    }

    /*
     * The write is not read back here, and the two reasons are different from
     * each other. A port operation's effect is asynchronous by construction -
     * PP "may be delayed in reflecting this change" (footnote 91 to Table 5-27,
     * p.375), a suspend takes effect when the link reaches U3, a resume when it
     * reaches U0 - so a read taken now would report the old state on a
     * conforming controller. And the hub class already has the mechanism for
     * telling the caller when it landed: the next RH_GetPortStatus refreshes the
     * shadow from hardware, and the change bits that arrive with it are what
     * usbhub is waiting for.
     */
    XhciControllerLockRelease(oldIrql);

    /*
     * Unconditionally, rather than only on the path that preempted something -
     * one rule for the whole family is easier to keep true than "the site that
     * happened to need it remembers". It costs one uncontended acquire when
     * there is nothing owed, and it is what gets a preempted reset's
     * C_PORT_RESET announced now instead of at the next health poll.
     */
    XhciRootHubDeferredWork(ext);
    return MP_STATUS_SUCCESS;
}

/* IRQL: DISPATCH_LEVEL. */
MPSTATUS XhciRhSetFeaturePortPower(PXHCI_EXTENSION ext, USHORT port)
{
    return xhciRhPortOperation(ext, port, XHCI_RH_OP_POWER_ON);
}

/* IRQL: DISPATCH_LEVEL. */
MPSTATUS XhciRhClearFeaturePortPower(PXHCI_EXTENSION ext, USHORT port)
{
    return xhciRhPortOperation(ext, port, XHCI_RH_OP_POWER_OFF);
}

/*
 * SET_FEATURE(PORT_ENABLE) has no xHCI implementation, and this is a refusal
 * rather than a polite success: "software cannot enable a port by writing a '1'
 * to this flag" (Table 5-27, p.372). A port becomes enabled when a reset
 * completes and at no other time, so a driver that answered success here would
 * be telling usbport a port was enabled that is not.
 *
 * IRQL: DISPATCH_LEVEL.
 */
MPSTATUS XhciRhSetFeaturePortEnable(PXHCI_EXTENSION ext, USHORT port)
{
    KIRQL oldIrql;

    /*
     * The lock is taken for a counter, which looks like overkill and is not:
     * `RhRefusals` is written under it by every other refusal in this file, and
     * a counter written under a lock from one context and without it from
     * another is a counter with no rule at all. Cheaper to hold the rule than
     * to remember which of the twelve slots is the exception.
     */
    XhciControllerLockAcquire(&oldIrql);
    ext->RhRefusals++;
    XhciControllerLockRelease(oldIrql);

    XHCI_DBG_VALUE_CHANGED("RH_SetFeaturePortEnable: xHCI has no software "
                           "enable - refusing, port", (ULONG)port);
    return MP_STATUS_NOT_SUPPORTED;
}

/* IRQL: DISPATCH_LEVEL. */
MPSTATUS XhciRhClearFeaturePortEnable(PXHCI_EXTENSION ext, USHORT port)
{
    return xhciRhPortOperation(ext, port, XHCI_RH_OP_DISABLE);
}

/* IRQL: DISPATCH_LEVEL. */
MPSTATUS XhciRhSetFeaturePortSuspend(PXHCI_EXTENSION ext, USHORT port)
{
    return xhciRhPortOperation(ext, port, XHCI_RH_OP_SUSPEND);
}

/* ------------------------------------------------------------------ */
/* The two asynchronous port operations (roadmap Phase 5 task 4)       */
/* ------------------------------------------------------------------ */

/*
 * Reset and resume are the two port operations that cannot be done by writing a
 * register and returning, and they share one mechanism: a per-port generation,
 * an armed state that excludes any other operation on that port, an
 * uncancellable timer carrying the generation, and stale generations returning
 * without touching MMIO.
 *
 * **What they do not share is the completion rule**, and that is the whole
 * hazard of building them together - see XHCI_PORT_OP_RESET / _RESUME in
 * xhci.h. Reset's timer is a deadline behind a real finish line (PRC); resume's
 * timer *is* the finish line, and an event that claimed it would end resume
 * signalling below T(DRSMDN) with both sides reporting success.
 *
 * The common half is here; the rules diverge in xhciRhRefresh (which claims a
 * reset and never a resume) and in xhciRhPortTimeout (which recovers a reset and
 * completes a resume).
 */

/*
 * Start one asynchronous operation on a port.
 *
 * Returns MP_STATUS_SUCCESS having written `value` to PORTSC and armed a
 * generation the caller must then get a timer onto - which is
 * XhciRootHubDeferredWork's job, once the lock is released.
 *
 * The two refusals worth naming. **No timer service means no operation**, for
 * the reason the command engine refuses an untimed command: a reset nothing
 * finishes and a resume nothing terminates are both worse than a refusal, and
 * the check is made before the write rather than after it. And **a port with an
 * operation already armed refuses** rather than replacing it, because the two
 * timers would then both believe they own the port - usbport should never
 * produce that, which is why it is counted separately from an ordinary refusal.
 *
 * IRQL: DISPATCH_LEVEL. Takes and releases the controller lock; no wait, and
 * usbport services only through XhciRootHubDeferredWork, after the release.
 */
static MPSTATUS xhciRhStartOperation(PXHCI_EXTENSION ext,
                                     USHORT port,
                                     ULONG operation)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;
    ULONG xhciPort;
    ULONG portsc;
    ULONG pls;
    ULONG value;
    ULONG write;
    MPSTATUS status;

    if (!XhciAsyncTimerAvailable()) {
        XhciControllerLockAcquire(&oldIrql);
        ext->RhTimerFailures++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH async operation: no timer service to time "
                               "it with - refusing, operation", operation);
        return MP_STATUS_NOT_SUPPORTED;
    }

    status = MP_STATUS_NOT_SUPPORTED;
    XhciControllerLockAcquire(&oldIrql);

    shadow = xhciRhShadow(ext, (ULONG)port);
    xhciPort = XhciRootHubPortOf(&ext->RootHub, (ULONG)port);
    if (shadow == NULL || xhciPort == 0) {
        ext->RhInvalidPort++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH async operation: no such managed port",
                               (ULONG)port);
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (!xhciRhAdmitted(ext)) {
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH async operation: controller not in service, "
                               "operation", operation);
        return MP_STATUS_NOT_SUPPORTED;
    }

    portsc = XhciReadPortsc(ext, xhciPort);
    if (portsc == 0xFFFFFFFFUL) {
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH async operation: PORTSC reads all ones on "
                               "port", xhciPort);
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * **A Port Power change this driver issued has not landed yet.** The
     * specification's rule is "before modifying it again" (Table 5-27, p.375),
     * so this is a refusal rather than a write held back: answering success for
     * an operation that was not performed is the one thing worse than declining
     * it. `XhciPortShadowPpSettled` clears the wait when this reading shows the
     * change arrived, so the ordinary case costs a comparison.
     */
    if (!XhciPortShadowPpSettled(shadow, portsc)) {
        ext->RhPortPowerPending++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH: refusing - a Port Power change is still in "
                               "flight on hub port", (ULONG)port);
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * **The busy test comes before anything operation-specific**, because an
     * armed port must refuse every answer and not only the ones that would have
     * written. The first version asked it last, through XhciPortShadowArm, and a
     * resume requested on a port already mid-reset took the "this port is not
     * suspended" exit and reported **success** - true of the link state, and a
     * lie about the request, since the port is in the middle of an operation
     * that will change it. XhciPortShadowArm keeps its own test as the
     * primitive's invariant; this one is the callback's policy.
     */
    if (shadow->Armed != XHCI_PORT_OP_NONE) {
        ext->RhPortsBusy++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH async operation: an operation is already "
                               "armed on hub port", (ULONG)port);
        return MP_STATUS_NOT_SUPPORTED;
    }

    write = 1;
    value = 0;
    if (operation == XHCI_PORT_OP_RESET) {
        value = XhciPortscReset(portsc);
    } else {
        pls = XHCI_PORTSC_GET_PLS(portsc);
        if (pls != XHCI_PLS_U3 && pls != XHCI_PLS_RESUME) {
            /*
             * Nothing to resume. Unlike SET_FEATURE(PORT_ENABLE) this is a
             * success rather than a refusal, and the difference is what the
             * caller is told: there the driver would be claiming a state the
             * port is not in, here the port is already in the state the request
             * asks for. Answering NOT_SUPPORTED would make usbhub treat an
             * already-running port as a failure.
             */
            XhciControllerLockRelease(oldIrql);
            XHCI_DBG_VALUE_CHANGED("RH_ClearFeaturePortSuspend: port is not "
                                   "suspended, PORTSC", portsc);
            return MP_STATUS_SUCCESS;
        }
        if (pls == XHCI_PLS_RESUME) {
            /*
             * The device is already signalling and the interval has already
             * started - this request is late to it rather than the start of it.
             * Re-writing '15' would restart signalling the port is in the middle
             * of; timing T(DRSMDN) from here instead only makes the interval
             * longer, and long is the safe direction.
             */
            write = 0;
        } else {
            value = XhciPortscResumeSignal(portsc);
        }
    }

    if (XhciPortShadowArm(shadow, operation) == 0) {
        /* Unreachable behind the test above, and kept because the primitive's
         * invariant is the primitive's: this is the answer if a future caller
         * reaches it by another route. */
        ext->RhPortsBusy++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (write) {
        (VOID)xhciRhWritePortsc(ext, xhciPort, shadow, portsc, value);
    }
    shadow->ArmPending = 1;
    if (operation == XHCI_PORT_OP_RESET) {
        ext->RhPortsReset++;
        /*
         * Task 11-V.9's second tier: the reset's *begin*, whose completion is
         * recorded in xhciRhRefresh. Both halves are worth records rather than
         * one, and that is the whole reason a reset appears in this tier: a
         * begin with no completion is a port that never came back, which is
         * a different fault from a device that enumerated and failed later and
         * looks identical in every counter.
         */
        XhciLogNoteLocked(ext, "port.reset.begin", (ULONG)port);
    } else {
        ext->RhPortsResumed++;
    }
    status = MP_STATUS_SUCCESS;

    XhciControllerLockRelease(oldIrql);

    /* The timer, and any announcement this or another context has queued. Both
     * are usbport services, so neither could happen above. */
    XhciRootHubDeferredWork(ext);
    return status;
}

/*
 * SET_FEATURE(PORT_RESET).
 *
 * "When software writes a '1' to this flag, the bus reset sequence as defined in
 * the USB2 specification is started" (Table 5-27, p.373) - and the sequence is
 * the controller's from that point on. This callback runs at DISPATCH_LEVEL and
 * may not wait for it, so it writes PR, arms a generation, and returns; the
 * completion arrives as PRC in a Port Status Change Event, and the timer behind
 * it exists to diagnose the case where it never does.
 *
 * There is no "write 0 to stop": PR is RW1S. That is what makes the deadline a
 * diagnosis rather than an abort - the watchdog cannot take a reset back, it can
 * only report that the port did not come out of one.
 *
 * IRQL: DISPATCH_LEVEL.
 */
MPSTATUS XhciRhSetFeaturePortReset(PXHCI_EXTENSION ext, USHORT port)
{
    return xhciRhStartOperation(ext, port, XHCI_PORT_OP_RESET);
}

/*
 * CLEAR_FEATURE(PORT_SUSPEND) - a resume request, and on a USB 2.0 port it is
 * two writes with a mandatory gap rather than one register write.
 *
 * A first version of this callback wrote PLS = U0 and returned success, on a
 * reading of 4.15.2 that turned out to be the *USB 3.x* branch of that section.
 * The USB 2.0 branch is explicit and different: "software shall write a '15'
 * (Resume) to the PLS field to initiate resume signaling ... Software shall
 * ensure that resume is signaled for at least 20 ms (TDRSMDN). Software shall
 * start timing TDRSMDN from the write of '15' (Resume) to PLS. After TDRSMDN is
 * complete, software shall write a '0' (U0) to the PLS field" (4.15.2.2,
 * p.257), where the USB 3.x port in the same list writes '0' to *initiate*
 * (step 1a). This driver serves only USB 2.0 ports, so U0-to-start was the one
 * value that could not be right - and it would have reported success while
 * leaving the port in U3 and the device asleep.
 *
 * So the first write goes out here and the terminating one comes from the timer,
 * whose interval is a **floor**: XHCI_PORT_RESUME_TIMER_MS rounds T(DRSMDN) up
 * by a clock tick, because ending resume signalling early is a protocol
 * violation and ending it late is legal.
 *
 * IRQL: DISPATCH_LEVEL.
 */
MPSTATUS XhciRhClearFeaturePortSuspend(PXHCI_EXTENSION ext, USHORT port)
{
    return xhciRhStartOperation(ext, port, XHCI_PORT_OP_RESUME);
}

/*
 * The watchdog for a reset and the terminator for a resume - one callback,
 * because they are one mechanism, and two branches, because the rules differ.
 *
 * usbport invokes this from its async timer DPC holding neither miniport lock
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 6), so everything it reads or writes is
 * under the controller lock and every decision is made from the copied
 * generation rather than from anything it could re-read. The timer cannot be
 * cancelled - every armed callback *will* fire - so arriving with nothing to do
 * is ordinary input rather than an error: the operation completed, a stop or a
 * suspend retired it, or the extension has been zeroed and restarted since.
 *
 * IRQL: DISPATCH_LEVEL, no usbport lock held.
 */
static VOID NTAPI xhciRhPortTimeout(PVOID miniPortExtension, PVOID context)
{
    PXHCI_EXTENSION ext;
    PXHCI_PORT_TIMEOUT timeout;
    XHCI_PORT_SHADOW *shadow;
    KIRQL oldIrql;
    ULONG xhciPort;
    ULONG portsc;
    ULONG latched;

    ext = (PXHCI_EXTENSION)miniPortExtension;
    timeout = (PXHCI_PORT_TIMEOUT)context;

    if (ext == NULL || timeout == NULL) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);

    /*
     * The full bracket and the epoch, exactly as the command watchdog validates
     * them and for the same reason: usbport zeroes the whole miniport extension
     * before every StartController, so a per-port generation restarts from 0 on
     * each one and cannot by itself tell one start's callbacks from the next's.
     */
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        ext->TrailingSignature != XHCI_EXTENSION_TRAILING ||
        timeout->Epoch == 0 || timeout->Epoch != ext->StartEpoch) {
        XhciControllerLockRelease(oldIrql);
        return;
    }

    shadow = xhciRhShadow(ext, timeout->HubPort);
    xhciPort = XhciRootHubPortOf(&ext->RootHub, timeout->HubPort);
    if (shadow == NULL || xhciPort == 0) {
        ext->RhStaleTimers++;
        XhciControllerLockRelease(oldIrql);
        return;
    }

    if (timeout->Operation == XHCI_PORT_OP_RESET) {
        /*
         * **Claim before reading, and the order is not arbitrary.** A first
         * version read the port first, on the argument that the deadline
         * expiring is not evidence the reset failed - the event may simply not
         * have been drained yet - and that argument is right about the *reading*
         * and wrong about where it belongs. A stale callback taking that reading
         * is a callback touching MMIO on a controller it has no claim to, and
         * the refresh it performs claims whatever *is* armed on the port: in the
         * host suite a timer left over from before a suspend arrived after the
         * resume and completed a reset that a fresh caller had just started
         * (measured). So: prove ownership from the copied generation first,
         * touch nothing if it has moved, and take the reading afterwards.
         */
        if (!XhciPortShadowClaim(shadow, XHCI_PORT_OP_RESET,
                                 timeout->Generation)) {
            ext->RhStaleTimers++;
            XhciControllerLockRelease(oldIrql);
            return;
        }

        /*
         * Now the reading, which is a diagnosis and can also be a late
         * completion: the PRC may have arrived and simply not been drained. The
         * port is already disarmed by the claim above, so the refresh's own
         * reset-claim finds nothing and this decides which of the two happened.
         */
        latched = 0;
        if (xhciRhAdmitted(ext)) {
            latched = xhciRhRefresh(ext, timeout->HubPort, shadow);
        }

        if ((latched & XHCI_HUB_C_PORT_RESET) != 0) {
            ext->RhResetsCompleted++;
            XHCI_DBG_VALUE_CHANGED("RH reset: PRC found by the watchdog rather "
                                   "than by the event, hub port",
                                   timeout->HubPort);
        } else {
            /*
             * The reset really did not complete. **C_PORT_RESET is reported
             * anyway**, and that is the honest answer rather than a convenient
             * one: usbhub is waiting for the reset to finish and reads the port
             * status beside the change, where PED = 0 says the device did not
             * enable. Reporting nothing would leave it waiting on its own much
             * longer timeout, and reporting a port that never came back is what
             * lets it retry or give up on this port instead of on the hub.
             *
             * PR is not written back: it is RW1S with no "write 0 to stop"
             * (Table 5-27, p.372), so there is nothing this driver can do to a
             * reset that has not ended except say so.
             */
            ext->RhResetTimeouts++;
            XhciPortShadowLatchChange(shadow, XHCI_HUB_C_PORT_RESET);
            XHCI_DBG_VALUE("RH reset: no PRC within the deadline, PORTSC",
                           shadow->Portsc);
        }

        ext->RootHubInvalidatesOwed++;
        XhciControllerLockRelease(oldIrql);
        XhciRootHubDeferredWork(ext);
        return;
    }

    /*
     * The resume, where this callback *is* the completion. T(DRSMDN) has
     * elapsed, so the terminating write is owed - "after TDRSMDN is complete,
     * software shall write a '0' (U0) to the PLS field" (4.15.2.2, p.257).
     */
    /*
     * **Validated without claiming**, because this callback may have to give the
     * port back: if the terminating write cannot go out yet, the resume is not
     * over and disarming it would leave the port signalling with nothing left to
     * end it. Claiming happens below, once the write has actually been issued.
     * The comparison is the one XhciPortShadowClaim makes, minus the disarm.
     */
    if (shadow->Armed != XHCI_PORT_OP_RESUME ||
        shadow->Generation != timeout->Generation ||
        timeout->Generation == 0) {
        ext->RhStaleTimers++;
        XhciControllerLockRelease(oldIrql);
        return;
    }

    if (!xhciRhAdmitted(ext)) {
        (VOID)XhciPortShadowClaim(shadow, XHCI_PORT_OP_RESUME,
                                  timeout->Generation);
        /* A stop or a suspend landed inside the interval. The retire has already
         * moved the generation, so this is normally unreachable; it is here
         * because "the claim succeeded" and "a register may be touched" are two
         * questions and only one of them has been asked. */
        ext->RhResumesAbandoned++;
        XhciControllerLockRelease(oldIrql);
        return;
    }

    portsc = XhciReadPortsc(ext, xhciPort);
    if (portsc == 0xFFFFFFFFUL || (portsc & XHCI_PORTSC_CCS) == 0) {
        /*
         * The device left mid-interval - a plausible way for a resume to end,
         * since a user unplugging a sleeping device is exactly what produces
         * one. Driving PLS to U0 on a port with nothing attached would be
         * writing a link state onto a link that no longer exists, so the
         * operation is abandoned rather than completed. The disconnect itself
         * arrives as CSC through the ordinary event path and needs nothing from
         * here.
         */
        (VOID)XhciPortShadowClaim(shadow, XHCI_PORT_OP_RESUME,
                                  timeout->Generation);
        ext->RhResumesAbandoned++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH resume: port went away mid-interval, PORTSC",
                               portsc);
        return;
    }

    /*
     * **A write that was held back is not a completion.** The Port Power
     * confirmation can defer this one - a power-off issued during the interval
     * leaves it outstanding - and an earlier version counted the resume complete
     * regardless of whether the write went out, then disarmed the port: the
     * signalling was never terminated and the counter said it had been. Since
     * T(DRSMDN) is a floor with no ceiling, waiting is free and correct, so the
     * operation stays armed and is re-timed.
     */
    if (!xhciRhWritePortsc(ext, xhciPort, shadow, portsc,
                           XhciPortscResumeDone(portsc))) {
        shadow->ArmPending = 1;
        ext->RhResumeRetries++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH resume: terminating write deferred - "
                               "re-timing hub port", timeout->HubPort);
        XhciRootHubDeferredWork(ext);
        return;
    }

    (VOID)XhciPortShadowClaim(shadow, XHCI_PORT_OP_RESUME,
                              timeout->Generation);
    ext->RhResumesCompleted++;
    XhciControllerLockRelease(oldIrql);

    /*
     * No announcement is queued here. The U3-to-U0 transition this write asks
     * for reports itself through PLC when the link reaches U0, and the refresh
     * that observes it derives C_PORT_SUSPEND - the hub class's "the resume you
     * asked for has finished" - from the previous link state. Latching it here
     * instead would report the resume complete at the instant it was requested.
     */
    XHCI_DBG_VALUE_CHANGED("RH resume: T(DRSMDN) elapsed, driving hub port to "
                           "U0", timeout->HubPort);
}

/*
 * The five change-clearing callbacks.
 *
 * They take down a **latched** bit and touch no register, which is the whole
 * point of the split between the shadow's `Changes` and PORTSC's RW1C bits: the
 * hardware bit was acknowledged the instant it was observed, so that the
 * controller keeps reporting the port, and the hub class's change survived here
 * until its owner asked for it to be cleared. Clearing hardware again from this
 * path would clear a *later* change that has arrived since.
 *
 * Port 0 is tolerated rather than refused, because Win2000 alone reaches
 * RH_ClearFeaturePortOvercurrentChange with `Port = 0` from its hub-directed
 * feature path (docs/usb-xhci-info/usbport-miniport-abi.md section 4). There is no port-zero
 * change to clear, so the answer is success having done nothing - a refusal
 * would put an MPSTATUS 6 into a path Win2000 takes on ordinary hub traffic.
 *
 * IRQL: DISPATCH_LEVEL.
 */
MPSTATUS XhciRhClearFeaturePortChange(PXHCI_EXTENSION ext,
                                      USHORT port,
                                      ULONG changeBit)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;
    ULONG cleared;

    if (port == 0) {
        XHCI_DBG_VALUE_CHANGED("RH clear change: hub-directed port 0, nothing "
                               "to clear, bit", changeBit);
        return MP_STATUS_SUCCESS;
    }

    XhciControllerLockAcquire(&oldIrql);
    shadow = xhciRhShadow(ext, (ULONG)port);
    if (shadow == NULL) {
        ext->RhInvalidPort++;
        ext->RhRefusals++;
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_CHANGED("RH clear change: no such managed port",
                               (ULONG)port);
        return MP_STATUS_NOT_SUPPORTED;
    }

    cleared = XhciPortShadowClearChange(shadow, changeBit);
    if (cleared) {
        ext->RhChangesCleared++;
    }
    XhciControllerLockRelease(oldIrql);

    /*
     * Success whether or not the bit was set. usbhub clears changes it has just
     * been told about and changes it is tidying up after, and "that one was
     * already clear" is not a failure of this operation.
     */
    return MP_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* The notification gate                                               */
/* ------------------------------------------------------------------ */

/*
 * RH_DisableIrq / RH_EnableIrq - a pure software gate, and never IMAN.IE.
 *
 * The name says interrupt and the lifecycle says otherwise. usbport calls the
 * disable from USBPORT_InvalidateRootHub and the enable from exactly one place:
 * the status-change scan's no-changes exit. Success, error, and the scan's own
 * early return all bypass it, so **a close is not guaranteed a matching open**
 * (all binary-confirmed, docs/usb-xhci-info/usbport-miniport-abi.md section 4). Anything held
 * back waiting for an enable that never comes is held back forever.
 *
 * Two consequences shape the implementation. It touches no register: IMAN.IE
 * gates the whole interrupter, so masking it here would silence transfer
 * completions along with port changes - and this driver's ISR/DPC design gives
 * that bit exactly one writer per direction already
 * (docs/contributing/design/05-locking-model.md section 4). And it holds no state that
 * only an enable could release: a port change that arrives while the gate is
 * closed is still latched in the shadow, where the next status query finds it.
 * What the gate suppresses is the *announcement* - Phase 5 task 5's
 * UsbPortInvalidateRootHub call - and nothing else.
 *
 * IRQL: DISPATCH_LEVEL.
 */
/*
 * Both counters are incremented **under the lock**, for the reason stated at
 * XhciRhSetFeaturePortEnable: `XhciControllerUpdateFlags` brackets only the
 * flag, so an increment beside it is unlocked, and on SMP (target 2d) two gate
 * calls can then lose one. A counter written under a lock from one context and
 * without it from another has no rule at all - and a *diagnostic* counter that
 * silently undercounts is worse than none, because the reading is what a future
 * "the gate never opened" investigation would trust.
 */
VOID XhciRhDisableIrq(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;

    (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_RH_IRQ, 0);
    XhciControllerLockAcquire(&oldIrql);
    ext->RhIrqGateCloses++;
    XhciControllerLockRelease(oldIrql);
}

/* IRQL: DISPATCH_LEVEL. */
VOID XhciRhEnableIrq(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;

    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_RH_IRQ);
    XhciControllerLockAcquire(&oldIrql);
    ext->RhIrqGateOpens++;
    XhciControllerLockRelease(oldIrql);
}

/*
 * RH_ChirpRootPort - success without bus action.
 *
 * It is an EHCI high-speed detection handshake: that controller has to reset a
 * port to find out whether the attached device is high speed, and hand the port
 * to a companion controller if it is not. xHCI has no companion controllers and
 * no such ambiguity - the port reports its speed in PORTSC after a reset - so
 * there is nothing to chirp and the correct answer is to say so quietly.
 *
 * The return is discarded by both builds anyway. What matters about this
 * callback is that it is reached at all: usbport withholds packet slot 0x12C
 * below interface Version 200, and on NUSB the call site does **not** re-check
 * the version and does not null-check the slot, so a sub-200 registration is a
 * null call during root-hub startup rather than a skipped feature. This driver
 * registers at 200.
 *
 * IRQL: DISPATCH_LEVEL.
 */
MPSTATUS XhciRhChirpRootPort(PXHCI_EXTENSION ext, USHORT port)
{
    /*
     * Unlocked, on the same argument as `RhHubStatusQueries` - one writer, a
     * diagnostic value that steers nothing - and see that note for what would
     * end the argument. **Not** on any claim about the caller's lock, which is
     * weaker here than it is even there: ReactOS's own contract table covers
     * the root-hub *status-query* callbacks, and its `RH_ChirpRootPort` call
     * site takes no `MiniportSpinLock` at all - it brackets the call with
     * `InterlockedIncrement`/`Decrement` on its own `ChirpRootPortLock`
     * (`external/reactos/usbport/roothub.c`). So this callback is not known to
     * be serialised by anything.
     */
    ext->RhChirps++;
    XHCI_DBG_VALUE_CHANGED("RH_ChirpRootPort: no xHCI equivalent, port",
                           (ULONG)port);
    return MP_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Deferred work: the timers and the announcement (task 4 and task 5)  */
/* ------------------------------------------------------------------ */

/*
 * Arm one port timer. Always called with the controller lock **released**.
 *
 * Nothing here can report failure to the operation that armed it, so the failure
 * that matters is handled by the caller rather than by a return: a port armed
 * with no callback behind it would be permanently ineligible for any other
 * operation, which is a worse state than not having tried.
 *
 * IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciRhArmTimer(PXHCI_EXTENSION ext, const XHCI_PORT_TIMEOUT *what)
{
    XHCI_PORT_TIMEOUT context;
    ULONG milliseconds;

    /* By value, captured by the caller under the controller lock - the same
     * rule the command engine's arm follows, and for the same reason: reading
     * the epoch or the generation here would take it after the lock was
     * dropped. */
    context = *what;

    milliseconds = (context.Operation == XHCI_PORT_OP_RESUME)
                       ? XHCI_PORT_RESUME_TIMER_MS
                       : XHCI_PORT_RESET_TIMEOUT_MS;

    if (XhciRegPacket.UsbPortRequestAsyncCallback == NULL) {
        return 0;
    }

    /*
     * The return value is discarded because there is nothing in it:
     * USBPORT_RequestAsyncCallback answers 0 on success **and** 0 when its pool
     * allocation fails (docs/usb-xhci-info/usbport-miniport-abi.md section 6). What is
     * checkable - that the service exists - was checked before the write that
     * needs timing.
     */
    (VOID)XhciRegPacket.UsbPortRequestAsyncCallback(
        ext, milliseconds, &context, sizeof(context), xhciRhPortTimeout);
    return 1;
}

/*
 * Take the next port owing a timer arm, if any, and describe it.
 *
 * One port per acquisition rather than a list built under the lock, because the
 * arm itself is a usbport service and must happen outside: taking them one at a
 * time is what keeps the critical section to a scan and a flag clear.
 *
 * IRQL: <= DISPATCH_LEVEL. Takes the controller lock.
 */
static ULONG xhciRhTakePendingArm(PXHCI_EXTENSION ext, XHCI_PORT_TIMEOUT *armed)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;
    ULONG hubPort;
    ULONG found;

    found = 0;
    XhciControllerLockAcquire(&oldIrql);

    for (hubPort = 1; hubPort <= ext->RootHub.PortCount &&
                      hubPort <= XHCI_MAX_ROOT_PORTS; hubPort++) {
        shadow = &ext->RootHub.Ports[hubPort - 1];
        if (shadow->ArmPending == 0) {
            continue;
        }
        shadow->ArmPending = 0;
        if (shadow->Armed == XHCI_PORT_OP_NONE) {
            /* Retired between the decision and here - a stop or a suspend
             * landing in that window. The generation has already moved, so a
             * timer armed now would be stale before it fired. */
            continue;
        }
        armed->Epoch = ext->StartEpoch;
        armed->Generation = shadow->Generation;
        armed->HubPort = hubPort;
        armed->Operation = shadow->Armed;
        found = 1;
        break;
    }

    XhciControllerLockRelease(oldIrql);
    return found;
}

/* Give up on a port whose timer could not be armed. Advances the generation, so
 * the port is usable again and any callback that did somehow get through is
 * stale. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciRhDisarmPort(PXHCI_EXTENSION ext, ULONG hubPort)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;

    XhciControllerLockAcquire(&oldIrql);
    shadow = xhciRhShadow(ext, hubPort);
    if (shadow != NULL) {
        (VOID)XhciPortShadowDisarm(shadow);
    }
    ext->RhTimerFailures++;
    XhciControllerLockRelease(oldIrql);

    XHCI_DBG_VALUE("RH async operation: no timer could be armed for hub port",
                   hubPort);
}

/*
 * Tell usbport that something on the root hub has changed (roadmap Phase 5
 * task 5).
 *
 * **One call drains every owed change**, because the invalidation makes usbport
 * re-poll *all* ports - it ends in a status-change scan that walks 1..N calling
 * RH_GetPortStatus - so a second call would ask for a pass the first already
 * covers. `RootHubInvalidatesOwed` is therefore a "somebody has news" flag with
 * a count attached for diagnosis rather than a queue of announcements.
 *
 * **XHCI_EXT_FLAG_RH_IRQ decides whether the call happens at all**, and
 * suppressing it is not the same as losing it. usbport closes that gate *by*
 * taking a notification - `USBPORT_InvalidateRootHub` calls `RH_DisableIrq`
 * itself - so a closed gate means a scan of every port is already outstanding,
 * and that scan reads this shadow. The one enable site is the scan's own
 * no-changes exit, i.e. the moment usbport becomes interested again
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 4).
 *
 * Nothing is held back waiting for an enable that may never arrive: a change is
 * latched in the shadow either way, and every status query reports it.
 *
 * IRQL: <= DISPATCH_LEVEL. **Call with the controller lock released** - and here
 * that rule is not lock-order caution but a self-deadlock: the service calls
 * `RH_DisableIrq` straight back into this miniport, which takes the same
 * non-recursive spin lock.
 */
static VOID xhciRhAnnounce(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG announce;

    announce = 0;
    XhciControllerLockAcquire(&oldIrql);
    if (ext->RootHubInvalidatesOwed != 0) {
        if ((ext->Flags & XHCI_EXT_FLAG_RH_IRQ) != 0) {
            ext->RootHubInvalidatesOwed = 0;
            ext->RootHubInvalidates++;
            announce = 1;
        } else {
            ext->RootHubInvalidatesGated++;
        }
    }
    XhciControllerLockRelease(oldIrql);

    if (!announce) {
        return;
    }
    if (XhciRegPacket.UsbPortInvalidateRootHub == NULL) {
        return;
    }

    XHCI_DBG_TEXT("root hub: announcing a port change to usbport");
    (VOID)XhciRegPacket.UsbPortInvalidateRootHub(ext);
}

#ifdef XHCI_FIX_PORT_POLL
/*
 * **EXPERIMENTAL, bench candidate W10 for Finding 3.** Built only under the
 * define; no shipping flavour carries it.
 *
 * **THE HAZARD IS THIS PROJECT'S OWN, ESTABLISHED EARLY AND STATED TWENTY
 * LINES FROM HERE**: a PORTSC change bit "nobody clears **suppresses the
 * controller's next Port Status Change Event for that port**"
 * (`lessons.md`, "hot-plug *operations* are not hot-plug *events*"). A port whose change bit is left set therefore stops
 * announcing anything, for good - and this file already records having been
 * bitten once by "the one PORTSC reader in this file that did not fold what it
 * read into the shadow".
 *
 * **That is the E460's and the P14s's signature exactly.** A device is plugged
 * in, the controller raises no event because the previous change was never
 * acknowledged, the driver never learns of the arrival, nothing is latched,
 * nothing is announced, and **nothing appears anywhere in Device Manager** - not
 * even a failed devnode. It also explains why W7's gate watchdog was inert: with
 * no event, there was never anything owed for it to force out.
 *
 * **The sweep.** Every managed port, every health poll, through `xhciRhRefresh`
 * - which is the one correct way to read PORTSC here, because it folds what it
 * reads into the shadow and *acknowledges* the change bits it observes. So this
 * both **notices** the missed arrival and **unsticks** the port for future
 * events, which a bare read of PORTSC would not.
 *
 * **A RECOVERY candidate, and the loudest of the set**: if this is the fault the
 * device appears within about half a second of being plugged in, with no cold
 * boot and no ten-second wait.
 *
 * **It is a polled fallback, not a repair.** It does not find whoever leaves the
 * change bit set; it survives them. If it works, that search is the next one,
 * and `RhPolledLatches` below is what says how often it was needed.
 *
 * IRQL: <= DISPATCH_LEVEL. **Call with the controller lock RELEASED** - it takes
 * the lock itself for the sweep and must not hold it across the announce, whose
 * `UsbPortInvalidateRootHub` calls `RH_DisableIrq` straight back into this
 * miniport and onto the same non-recursive lock.
 */
VOID XhciRhPortPollSweep(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG hubPort;
    ULONG ports;
    ULONG owed;
    XHCI_PORT_SHADOW *shadow;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    owed = 0;
    XhciControllerLockAcquire(&oldIrql);
#ifdef XHCI_FIX_PORT_POLL_SLOW
    /*
     * **Bench candidate W15** (`run-13e.md` P9): the whole candidate is this
     * divider. The sweep's action is unchanged; only its cadence moves, from
     * every health poll (~0.5 s) to every XHCI_RH_SWEEP_SLOW_POLLS-th (~8 s) -
     * which makes "the sweep found it" and "an event announced it" distinguish
     * themselves by a wall-clock latency no operator can misread. See the
     * declaration comment in src/xhci_hw.h for the arithmetic.
     */
    ext->RhSweepPollsSeen++;
    if (ext->RhSweepPollsSeen < XHCI_RH_SWEEP_SLOW_POLLS) {
        XhciControllerLockRelease(oldIrql);
        return;
    }
    ext->RhSweepPollsSeen = 0;
#endif
    ports = ext->RootHub.PortCount;
    for (hubPort = 1; hubPort <= ports; hubPort++) {
        shadow = xhciRhShadow(ext, hubPort);
        if (shadow == NULL) {
            continue;
        }
        if (xhciRhRefresh(ext, hubPort, shadow) != 0) {
            ext->RootHubInvalidatesOwed++;
            ext->RhPolledLatches++;
            owed = 1;
        }
    }
    XhciControllerLockRelease(oldIrql);

    if (owed) {
        XHCI_DBG_TEXT("root hub: a polled sweep found a change no event "
                      "announced");
        xhciRhAnnounce(ext);
    }
}
#endif

#ifdef XHCI_FIX_RH_GATE
/*
 * **EXPERIMENTAL, bench candidate W7 for Finding 3.** Built only under the
 * define; no shipping flavour carries it.
 *
 * **THE HAZARD IS THIS FILE'S OWN, DOCUMENTED ABOVE AND NEVER GUARDED.**
 * `RH_DisableIrq` is called by usbport from `USBPORT_InvalidateRootHub`, and the
 * single enable site is the status-change scan's *no-changes* exit - "success,
 * error, and the scan's own early return all bypass it, so a close is not
 * guaranteed a matching open. Anything held back waiting for an enable that
 * never comes is held back forever."
 *
 * The reason that is *supposed* to be safe is that a closed gate implies a scan
 * of every port is already outstanding, and that scan reads the shadow through
 * `RH_GetPortStatus`. **The case it does not cover is a change latched after
 * that scan has already finished**: the gate is shut, no scan is outstanding,
 * `RootHubInvalidatesOwed` stays set, `xhciRhAnnounce` counts
 * `RootHubInvalidatesGated` and returns, and usbport is never told there is
 * anything to look at. One gate serves the whole root hub, so every port is
 * affected at once and nothing but a restart clears it.
 *
 * That is the E460's signature exactly, including the part that survived every
 * earlier theory: **a connect produces nothing anywhere in Device Manager** -
 * not even a failed or unknown devnode - because enumeration is never asked for,
 * while disconnects processed *before* the gate stuck went through normally.
 *
 * **The watchdog.** A shut gate with an announcement owed is legitimate and
 * common for a moment. It is never legitimate for seconds. So count consecutive
 * health polls in that state, and past the threshold force the gate open and
 * announce. `XHCI_RH_GATE_STUCK_POLLS` at 20 is about ten seconds at usbport's
 * nominal 500 ms, which no ordinary scan turnaround approaches.
 *
 * **A RECOVERY candidate, which is what makes it readable with no counter
 * channel**: if this is the fault, the machine comes back **by itself, without a
 * cold boot**, about ten seconds after the failed plug - and the device
 * enumerates. A success is unmistakable from across the room.
 *
 * IRQL: <= DISPATCH_LEVEL. **Call with the controller lock RELEASED** - the
 * announce path calls `UsbPortInvalidateRootHub`, which calls `RH_DisableIrq`
 * straight back into this miniport and takes the same non-recursive lock.
 */
#define XHCI_RH_GATE_STUCK_POLLS 20

VOID XhciRhGateWatchdog(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG stuck;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    stuck = 0;
    XhciControllerLockAcquire(&oldIrql);
    if (ext->RootHubInvalidatesOwed != 0 &&
        (ext->Flags & XHCI_EXT_FLAG_RH_IRQ) == 0) {
        ext->RhGateStuckPolls++;
        if (ext->RhGateStuckPolls >= XHCI_RH_GATE_STUCK_POLLS) {
            ext->RhGateStuckPolls = 0;
            stuck = 1;
        }
    } else {
        ext->RhGateStuckPolls = 0;
    }
    XhciControllerLockRelease(oldIrql);

    if (!stuck) {
        return;
    }

    /*
     * Force the gate and re-drive the announcement. `XhciControllerUpdateFlags`
     * takes the lock itself, and `xhciRhAnnounce` must be reached without it.
     */
    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_RH_IRQ);
    XHCI_DBG_TEXT("root hub: notification gate forced open by the watchdog");
    xhciRhAnnounce(ext);
}
#endif

/* See the contract in src/xhci_hw.h. IRQL: <= DISPATCH_LEVEL, controller lock
 * released. */
VOID XhciRootHubDeferredWork(PXHCI_EXTENSION ext)
{
    XHCI_PORT_TIMEOUT armed;
    ULONG guard;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    /*
     * Bounded by the number of ports rather than by "until none are pending":
     * an arm site running on another CPU can set a flag while this loop is
     * draining, and a loop with no bound would then be at the mercy of how often
     * that happens. Anything left is drained by the next call, and there is
     * always a next call - the health poll makes one every interval.
     */
    for (guard = 0; guard < XHCI_MAX_ROOT_PORTS; guard++) {
        if (!xhciRhTakePendingArm(ext, &armed)) {
            break;
        }
        if (!xhciRhArmTimer(ext, &armed)) {
            xhciRhDisarmPort(ext, armed.HubPort);
        }
    }

    xhciRhAnnounce(ext);

    /*
     * **And the device layer's drain, because every root-hub path that can
     * decide something for it ends here.** A refresh is what observes a connect
     * change, and a connect change tears a device down (task 6-B.5): queued
     * transfers to complete, a Disable Slot to issue. All of it is decided under
     * the lock and none of it can be done there, and the refresh sites -
     * `RH_GetPortStatus`, the port event, the health poll's sweep - reach this
     * function and nothing else in common.
     *
     * The suite found it missing rather than review: a disconnect marked the
     * record gone, owed a Disable Slot, and then nothing issued it, because the
     * teardown's one caller was a *root-hub* callback. Idempotent, so the event
     * DPC calling both explicitly costs nothing.
     */
    XhciSlotDeferredWork(ext);
}

/*
 * End an armed operation that is not going to end by itself, and **report it**.
 *
 * Two callers, and one mechanism rather than two copies, because the thing they
 * share is the part that is easy to get wrong: an operation that stops without
 * anybody being told is worse than one that fails, since usbhub is *waiting* on
 * the report and will wait on its own much longer timeout instead.
 *
 *   XHCI_RH_RETIRE_AGED - the health poll found an operation that has outlived
 *   every timer that could legitimately still be coming, which means no callback
 *   was ever scheduled for it (UsbPortRequestAsyncCallback cannot say so). Only a
 *   callback disarms a port, so without this the port refuses every later reset
 *   for the life of the driver.
 *
 *   XHCI_RH_RETIRE_PREEMPTED - usbport took the port out from under the
 *   operation with a power-off or a disable.
 *
 * **The completion rule is per operation, and the reasons differ**, which is
 * stated rather than implied - sharing a mechanism does not mean sharing a
 * completion rule, and that is the lesson task 4 was built on. The reset's row
 * took three review rounds to get right and is worth reading in that order:
 *
 *   A **reset that aged out** reports C_PORT_RESET. It ended - nothing was ever
 *   going to time it - it did not succeed, and usbhub reads the port status
 *   beside the change, where PED = 0 says so.
 *
 *   A **preempted reset** reports C_PORT_RESET too, and this row took three
 *   review rounds because the two obvious answers are both wrong. Reporting
 *   nothing orphans it: usbhub is blocked on that change and waits out its own
 *   much longer timeout. Reporting it from *here*, before the caller has issued
 *   its write, announces "the reset sequence is complete" while PORTSC still has
 *   PR set - a false statement about the hardware. **So the caller issues its
 *   write first and calls this afterwards**, which is a precondition of this
 *   function rather than a habit of one call site: the report then follows the
 *   act that ended the reset.
 *
 *   **And it is this driver's to make, because the hardware will not.** "Note
 *   that this flag [PRC] shall not be set to '1' if the reset processing was
 *   forced to terminate due to software clearing PP or PED to '0'" (Table 5-27,
 *   p.377). So the interrupting write really does terminate the reset - PR is
 *   '0' if PP is '0' (p.372) - and does so **silently**: no PRC, therefore no
 *   Port Status Change Event, therefore no observation will ever arrive. An
 *   earlier round left the reset armed to wait for exactly that observation,
 *   which the specification forbids the controller from producing. The same note
 *   appears on CSC, PEC, PLC and WRC: **a software-initiated power-off produces
 *   no change bits at all.**
 *
 *   A **resume** owes its terminating write only when it aged out: a port left
 *   signalling resume with nothing to end it is a port driving the bus, and
 *   issuing U0 late is legal where issuing it early is not. Under preemption it
 *   is **abandoned**, because the operation that preempted it *is* ending the
 *   signalling - power-off removes VBus and a disable puts the link in Disabled -
 *   so writing U0 first would compose a link-state write for a port the caller is
 *   about to take away. No hub-class report is owed either: C_PORT_SUSPEND means
 *   a *completed* resume, and this one did not complete.
 *
 * Called with the controller lock held; the announcement it queues is drained by
 * the caller once the lock is released. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciRhRetireOperation(PXHCI_EXTENSION ext,
                                  ULONG hubPort,
                                  XHCI_PORT_SHADOW *shadow,
                                  ULONG reason)
{
    ULONG operation;
    ULONG xhciPort;
    ULONG portsc;

    operation = shadow->Armed;
    if (operation == XHCI_PORT_OP_NONE) {
        return;
    }
    xhciPort = XhciRootHubPortOf(&ext->RootHub, hubPort);

    (VOID)XhciPortShadowDisarm(shadow);
    if (reason == XHCI_RH_RETIRE_AGED) {
        ext->RhAgeRetires++;
        XHCI_DBG_VALUE("RH: an armed port operation outlived every watchdog "
                       "interval - no timer was ever scheduled for hub port",
                       hubPort);
    } else {
        ext->RhOperationsPreempted++;
        XHCI_DBG_VALUE_CHANGED("RH: preempting the operation armed on hub port",
                               hubPort);
    }

    if (operation == XHCI_PORT_OP_RESET) {
        /* A preempted reset did not run out of time - it was ended by the write
         * that took its port out of service - so it is counted as a preemption
         * above and not here. The report is the same either way. */
        if (reason == XHCI_RH_RETIRE_AGED) {
            ext->RhResetTimeouts++;
        }
        XhciPortShadowLatchChange(shadow, XHCI_HUB_C_PORT_RESET);
        ext->RootHubInvalidatesOwed++;
        return;
    }

    if (reason == XHCI_RH_RETIRE_PREEMPTED || xhciPort == 0) {
        ext->RhResumesAbandoned++;
        return;
    }
    portsc = XhciReadPortsc(ext, xhciPort);
    if (portsc == 0xFFFFFFFFUL || (portsc & XHCI_PORTSC_CCS) == 0 ||
        XHCI_PORTSC_GET_PLS(portsc) != XHCI_PLS_RESUME) {
        ext->RhResumesAbandoned++;
        return;
    }
    /*
     * Same rule as the timer path: a write that was held back is not a
     * completion. Here the operation is being retired either way - that is what
     * the age detector is for - so it is counted as **abandoned**, and the port
     * is left for the sweep to re-arm once the Port Power confirmation clears,
     * which it is separately bounded to do.
     */
    if (!xhciRhWritePortsc(ext, xhciPort, shadow, portsc,
                           XhciPortscResumeDone(portsc))) {
        ext->RhResumesAbandoned++;
        return;
    }
    ext->RhResumesCompleted++;
    /*
     * The shadow is deliberately not refreshed from that write, exactly as the
     * timer path does not: the U3-to-U0 transition reports itself through PLC
     * when the link reaches U0, and the refresh that observes it is what derives
     * C_PORT_SUSPEND from the previous link state. Folding a reading in here
     * would report the resume finished at the instant it was asked for.
     */
}

/*
 * Task 13-R.3.5's sizing rule, checked rather than asserted in prose: the age
 * detector must sit beyond the longest wait a port operation is legitimately
 * allowed, or it retires timers that were going to fire. As a poll count that
 * relationship could not be checked at all - one side was a count of somebody
 * else's timer ticks - and on the E460 it was false by a factor of thirty.
 */
XHCI_C_ASSERT(port_age_clears_the_reset_deadline,
              XHCI_PORT_AGE_MS > XHCI_PORT_RESET_TIMEOUT_MS);

/*
 * The health poll's root-hub half (roadmap Phase 5 task 6), called from
 * CheckController.
 *
 * It exists for one case, and the case is real rather than defensive: a port
 * that entered the Resume state by itself announces the transition with PLC, and
 * whichever refresh observes PLC acknowledges it - so a status query racing the
 * Port Status Change Event consumes the notification, and the event path then
 * finds nothing to arm. The link state persists where the change bit does not,
 * so a sweep over the shadows finds such a port and arms the terminating write
 * that nothing else would.
 *
 * A sweep rather than an event because there is nothing to hang it on: the
 * notification has already been consumed. The cost is a walk over at most 255
 * bytes of shadow per poll, under a lock this poll already takes for USBSTS.
 *
 * IRQL: DISPATCH_LEVEL, under usbport's MiniportSpinLock (CheckController).
 * Takes and releases the controller lock; calls usbport services only
 * through XhciRootHubDeferredWork, after releasing it.
 */
VOID XhciRootHubPoll(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    XHCI_PORT_SHADOW *shadow;
    ULONG hubPort;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    if (xhciRhAdmitted(ext)) {
        for (hubPort = 1; hubPort <= ext->RootHub.PortCount &&
                          hubPort <= XHCI_MAX_ROOT_PORTS; hubPort++) {
            shadow = &ext->RootHub.Ports[hubPort - 1];

            /*
             * **The Port Power confirmation, which nothing else is guaranteed
             * to collect.** A status query or a port event settles it as a side
             * effect of reading, but a port nobody asks about would hold the
             * wait for ever - and while it is held every operation on that port
             * is refused, which turns a slow power change into a dead port. So
             * the poll reads it, and bounds it: a PP that never arrives is a
             * broken port, and one write made without the confirmation is a
             * better answer than refusing that port for the life of the driver.
             */
#ifdef XHCI_FIX_ACK_OWED
            /*
             * **Bench candidate W13's second half, and without it the first half
             * only narrows the window.** The gate below is `PpPending`, and
             * `XhciPortShadowPpAge` clears that on give-up - so a debt still
             * owed at that moment would lose the one thing that re-reads this
             * port. An owed acknowledgement is therefore its own reason to
             * refresh, and it stays one until the write is accepted.
             */
            if (shadow->PpPending != 0 || shadow->PortscAckOwed != 0) {
#else
            if (shadow->PpPending != 0) {
#endif
                /*
                 * **Through the refresh, not a bare read of PP.** The first
                 * version read PORTSC here and looked at one bit, which broke
                 * this driver's own rule about what reading that register
                 * obliges you to do: a change bit is acknowledged the instant it
                 * is observed, because one nobody clears suppresses the
                 * controller's next Port Status Change Event for that port
                 * (docs/contributing/lessons.md, "hot-plug operations are not hot-plug events"). A poll that saw a connect and
                 * dropped it on the floor was the one PORTSC reader in this file
                 * that did not fold what it read into the shadow.
                 *
                 * The refresh settles the confirmation from the same reading,
                 * and its acknowledgement is still held back until that
                 * confirmation is made - so the ordering rule survives and the
                 * change is at least *latched*, where the next status query
                 * finds it.
                 */
                if (xhciRhRefresh(ext, hubPort, shadow) != 0) {
                    ext->RootHubInvalidatesOwed++;
                }
                if (shadow->PpPending != 0 &&
                    XhciPortShadowPpAge(shadow, ext->PollClockMs,
                                        XHCI_PORT_AGE_MS)) {
                    ext->RhPortPowerStuck++;
                    XHCI_DBG_VALUE("RH: giving up waiting for Port Power on "
                                   "hub port", hubPort);
                }
            }

            /*
             * **The device teardown a disable or power-off could not earn at
             * the time**, settled here for the same reason the Port Power
             * confirmation above is: nothing else is guaranteed to collect it,
             * and until it is collected the record keeps a USB address usbport
             * has already recycled - which is the whole defect the port-disable
             * trigger exists for.
             *
             * Unbounded on purpose, unlike the Port Power wait beside it. That
             * one is bounded because holding it refuses every operation on the
             * port, so giving up is better than a dead port; this one gates a
             * teardown whose *early* execution is the DMA hazard, so there is
             * nothing safe to give up to. A port whose PED or PP never clears
             * has not taken its device out of service, and the record is correct
             * to survive.
             */
            if (shadow->DisownPending != 0) {
                ULONG disownPort;

                disownPort = XhciRootHubPortOf(&ext->RootHub, hubPort);
                if (disownPort != 0 &&
                    xhciRhPortIsDisowned(ext, disownPort,
                                         (ULONG)shadow->DisownWantsPp)) {
                    shadow->DisownPending = 0;
                    XhciSlotPortDisabled(ext, hubPort);
                }
            }

            /*
             * **The arm sweep first, then the age**, and the order is the
             * difference between rescuing a port and thrashing it. Both are
             * no-ops for a port with an operation armed, so on that port only
             * the age runs; but a port the age *retires* has just been issued
             * its terminating U0 write, and the shadow still holds the reading
             * from before it - so arming afterwards would start a fresh resume
             * on a port that was told to end one microseconds earlier, on the
             * strength of a value known to be stale. This way the link is given
             * until the next poll to report the transition, which is what the
             * PLC event does anyway.
             */
            xhciRhArmDeviceResume(ext, hubPort, shadow);
            if (XhciPortShadowAge(shadow, ext->PollClockMs,
                                  XHCI_PORT_AGE_MS)) {
                xhciRhRetireOperation(ext, hubPort, shadow,
                                      XHCI_RH_RETIRE_AGED);
            }
        }

    }
    XhciControllerLockRelease(oldIrql);

    XhciRootHubDeferredWork(ext);
}

/* See the contract in src/xhci_hw.h. Called with the controller lock held.
 * IRQL: <= DISPATCH_LEVEL. */
VOID XhciRootHubRetireOperations(PXHCI_EXTENSION ext)
{
    ULONG hubPort;
    ULONG retired;

    if (ext == NULL) {
        return;
    }

    retired = 0;
    for (hubPort = 1; hubPort <= ext->RootHub.PortCount &&
                      hubPort <= XHCI_MAX_ROOT_PORTS; hubPort++) {
        retired += XhciPortShadowDisarm(&ext->RootHub.Ports[hubPort - 1]);
    }
    ext->RhOperationsRetired += retired;

    /*
     * And the announcement, which has nobody left to announce to. A stop or a
     * suspend is followed either by nothing at all or by a start or resume whose
     * seed re-reads every port and re-queues what it finds, so carrying a count
     * across the transition would announce the *previous* controller's news to
     * whatever came back.
     */
    ext->RootHubInvalidatesOwed = 0;

    if (retired != 0) {
        XHCI_DBG_VALUE("root hub: port operations retired by the quiesce",
                       retired);
    }
}

/* ------------------------------------------------------------------ */
/* Construction and the event path                                     */
/* ------------------------------------------------------------------ */

/*
 * Coming back to D0 does not resume the ports (roadmap Phase 5 task 6).
 *
 * "Any Root Hub port that is in the Resume or U3 state when the xHC is
 * transitioned to the D0 power state shall require software to drive the port to
 * the U0 state. The xHC shall not automatically transition a root hub port from
 * the Resume or U3 state to the U0 state" (4.15, p.254). So a controller that
 * has been reprogrammed and whose shadow has been rebuilt has still left every
 * suspended port suspended, and this is the pass that ends that.
 *
 * **It is synchronous, and that is a departure from task 4's machinery rather
 * than an oversight.** The timer exists because a DISPATCH_LEVEL callback may
 * not wait 20 ms; this runs at PASSIVE_LEVEL inside the init sequence, where a
 * wait is exactly what the rest of the sequence already does - the port-power
 * settle is the same shape. Doing it with the timer instead would mean arming
 * callbacks during a start whose extension is still being filled in, to save a
 * delay this path is allowed to take. The writes are the same two builders and
 * the interval is the same T(DRSMDN); only the mechanism that spans it differs.
 *
 * The delay is paid **once for all such ports**, like the port-power settle:
 * every port's resume signalling starts before the wait begins, so the intervals
 * overlap and each one is still at least T(DRSMDN) long.
 *
 * On today's driver this pass is expected to find nothing, and the counter is
 * how that expectation is checked rather than assumed: a resume here is a full
 * reinitialization, and HCRST puts every port register back to its default, so
 * no port survives in U3. It becomes load-bearing the moment Phase 6's CSS/CRS
 * save-and-restore replaces that reinitialization - which is precisely when
 * nobody will be thinking about link states.
 *
 * IRQL: PASSIVE_LEVEL (it waits).
 */
static VOID xhciRhDriveSuspendedPortsToU0(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG hubPort;
    ULONG xhciPort;
    ULONG portsc;
    ULONG pls;
    ULONG signalled;

    signalled = 0;

    XhciControllerLockAcquire(&oldIrql);
    for (hubPort = 1; hubPort <= ext->RootHub.PortCount; hubPort++) {
        xhciPort = XhciRootHubPortOf(&ext->RootHub, hubPort);
        if (xhciPort == 0) {
            continue;
        }
        portsc = ext->RootHub.Ports[hubPort - 1].Portsc;
        if ((portsc & XHCI_PORTSC_CCS) == 0) {
            continue;
        }
        pls = XHCI_PORTSC_GET_PLS(portsc);
        if (pls != XHCI_PLS_U3 && pls != XHCI_PLS_RESUME) {
            continue;
        }
        /* A port already in Resume is mid-signalling: it owes the terminating
         * write and not a second start, exactly as the callback path decides.
         * A write the Port Power confirmation held back started nothing, so it
         * is not counted either: only ports actually signalling owe the wait
         * and the terminating write below. */
        if (pls == XHCI_PLS_U3 &&
            !xhciRhWritePortsc(ext, xhciPort,
                               &ext->RootHub.Ports[hubPort - 1], portsc,
                               XhciPortscResumeSignal(portsc))) {
            continue;
        }
        signalled++;
    }
    XhciControllerLockRelease(oldIrql);

    if (signalled == 0) {
        /*
         * **The expected reading, and it has to be printed here or it is an
         * absent line** (roadmap task 6-B.6). `RhPortsDriventoU0` is predicted
         * to stay at zero on both paths - a resume that reinitialises passes
         * through HCRST, which defaults every port link state, and a resume that
         * *restores* does not bring port state back either ("The state of a Root
         * Hub port is not covered by a Save or Restore operation", p.315). A
         * counter whose expected value is zero and whose only trace site is past
         * an early return proves nothing when it is right: the pass and a
         * function that was never called look identical.
         */
        XHCI_DBG_VALUE("root hub: no port needed driving out of U3, total",
                       ext->RhPortsDriventoU0);
        return;
    }

    XhciDelayMs(ext, XHCI_PORT_RESUME_SIGNAL_MS);

    XhciControllerLockAcquire(&oldIrql);
    for (hubPort = 1; hubPort <= ext->RootHub.PortCount; hubPort++) {
        xhciPort = XhciRootHubPortOf(&ext->RootHub, hubPort);
        if (xhciPort == 0) {
            continue;
        }
        /* Re-read rather than reusing the seed's word: 20 ms have passed, the
         * port has moved through Resume, and the terminating write must be
         * composed from what the register says now. */
        portsc = XhciReadPortsc(ext, xhciPort);
        if (portsc == 0xFFFFFFFFUL || (portsc & XHCI_PORTSC_CCS) == 0) {
            continue;
        }
        /* U0 with LWS is the terminating half of Resume -> U0 (4.15.2.2) and
         * nothing else: a port still in U3 after the interval was never
         * signalled (its start was held back, or it re-entered U3), so it gets
         * no write here and is left for the callback path, as the retire path
         * decides too. */
        if (XHCI_PORTSC_GET_PLS(portsc) != XHCI_PLS_RESUME) {
            continue;
        }
        if (!xhciRhWritePortsc(ext, xhciPort,
                               &ext->RootHub.Ports[hubPort - 1], portsc,
                               XhciPortscResumeDone(portsc))) {
            continue;
        }
        ext->RhPortsDriventoU0++;
    }
    XhciControllerLockRelease(oldIrql);

    XHCI_DBG_VALUE("root hub: ports driven out of U3 after the D0 transition",
                   ext->RhPortsDriventoU0);
}

/*
 * Build the root hub and take the first reading of every port it contains.
 *
 * Called from the init sequence, after the ports have been powered: the shadow
 * records PP, and a seed taken before the port-power step would report every
 * managed port as unpowered until something else refreshed it.
 *
 * The seed **latches** whatever change bits it finds, and that is deliberate. A
 * device attached before the driver started leaves CSC set on its port, and
 * that connect is the only announcement the controller will ever make about it
 * - a start that acknowledged the bit without latching the change would leave a
 * boot-attached device connected, unreported and unenumerated.
 *
 * IRQL: PASSIVE_LEVEL (init only). Takes the controller lock like every other
 * writer of this state, even though the init sequence runs with every other
 * context already refusing: the cost is one uncontended acquire per start, and
 * the alternative is a second rule about when this state may be touched
 * unlocked.
 */
ULONG XhciRootHubInit(PXHCI_EXTENSION ext, ULONG afterRestore)
{
    KIRQL oldIrql;
    ULONG status;
    ULONG hubPort;
    ULONG connected;
    ULONG latched;
    ULONG owed;

    if (ext->PortMapStatus != XHCI_CAPS_OK) {
        ext->RootHub.Status = XHCI_RH_BAD_PARAM;
        return XHCI_RH_BAD_PARAM;
    }

    XhciControllerLockAcquire(&oldIrql);

    status = XhciRootHubBuild(&ext->PortMap, &ext->RootHub);
    if (status != XHCI_RH_OK) {
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE("root hub: refusing, build status", status);
        return status;
    }

    connected = 0;
    latched = 0;
    owed = 0;
    for (hubPort = 1; hubPort <= ext->RootHub.PortCount; hubPort++) {
        ULONG portLatched;

        portLatched = xhciRhRefresh(ext, hubPort,
                                    &ext->RootHub.Ports[hubPort - 1]);
        if (portLatched != 0) {
            /*
             * **Queued, exactly as the event path queues.** This reading is the
             * only one that will ever see these changes: it acknowledged the
             * PORTSC bits a moment ago, so the controller will not announce
             * them again, and it is not itself a report to anybody - unlike
             * RH_GetPortStatus, which latches and reports in the same call and
             * therefore owes no invalidation.
             *
             * On a *start* the omission would probably have been survivable,
             * since usbhub scans every port once when it loads. On a **resume**
             * it is not: the root hub already exists, nothing rescans it, and a
             * device plugged in while the controller was suspended would be
             * latched here, acknowledged in hardware, and never mentioned to
             * usbport again - a device that is simply missing until something
             * else happens to that port.
             */
            ext->RootHubInvalidatesOwed++;
            owed++;
            latched |= portLatched;
        }
        if ((ext->RootHub.Ports[hubPort - 1].Portsc & XHCI_PORTSC_CCS) != 0) {
            connected++;
        }
    }

    XhciControllerLockRelease(oldIrql);

    /*
     * **Not after a successful restore** (audit finding A5). The pass drives
     * every connected port out of U3, and its own justification is that no port
     * *can* be in U3 by the time it runs: a resume that reinitialises passes
     * through HCRST, which defaults every port link state. That argument is
     * exactly what a successful CSS/CRS restore removes - there is no HCRST on
     * that path, so the port registers survive - and what survives includes the
     * ports **usbhub deliberately suspended** through
     * `RH_SetFeaturePortSuspend`. Resuming those wakes a device nobody asked to
     * wake and latches a `C_PORT_SUSPEND` completion nobody requested, which
     * usbhub then reads as a resume it did not initiate.
     *
     * There is no vehicle for this today - QEMU fails every restore, so the
     * branch needs bare-metal FSC >= 1 - which is why it is closed here before a
     * machine makes it live rather than after.
     *
     * A port left mid-Resume by a device-initiated remote wake is not lost with
     * it: the shadow, the armed operation and the port's own generation all
     * survive the suspend (usbport zeroes the extension only before a
     * *StartController*), so the ordinary resume-completion path still owns it.
     */
    if (afterRestore) {
        ext->RhU3PassSkippedAfterRestore++;
        XHCI_DBG_TEXT("root hub: restore succeeded, so U3 ports are left as the "
                      "suspend left them");
    } else {
        xhciRhDriveSuspendedPortsToU0(ext);
    }

    XHCI_DBG_VALUE("root hub: managed ports", ext->RootHub.PortCount);
    XHCI_DBG_VALUE("root hub: ports connected at start", connected);
    XHCI_DBG_VALUE("root hub: changes latched by the first reading", latched);
    XHCI_DBG_VALUE("root hub: ports owing an invalidation", owed);
    return XHCI_RH_OK;
}

/*
 * One Port Status Change Event.
 *
 * "A Port Status Change Event is generated ... to inform system software that a
 * change has occurred on a Root Hub port" and the event carries only the port
 * number - the change itself has to be read from PORTSC (4.19.2). So this
 * refreshes that one port and lets the shadow decide what changed.
 *
 * **The caller holds the controller lock**: this runs inside the event DPC's
 * bounded drain, which owns that lock for the whole pass. Nothing here acquires
 * it, waits, or calls a usbport service.
 *
 * An event naming a port this driver does not manage is counted and dropped.
 * That is not a defect report - a USB 3.x port this driver deliberately left
 * unpowered can still report a change, and it has no shadow entry because it
 * has no hub port.
 *
 * **The announcement is decided here and made by the caller.**
 * UsbPortInvalidateRootHub is a usbport service and calls RH_DisableIrq straight
 * back into this miniport, so making it under this lock would self-deadlock on a
 * non-recursive spin lock. What happens here is `RootHubInvalidatesOwed++`; the
 * DPC calls XhciRootHubDeferredWork once its drain has released the lock.
 *
 * IRQL: DISPATCH_LEVEL.
 */
VOID XhciRootHubPortEvent(PXHCI_EXTENSION ext, ULONG portId)
{
    ULONG hubPort;
    ULONG latched;

    hubPort = XhciRootHubHubPortOf(&ext->RootHub, portId);
    if (hubPort == 0) {
        ext->PortEventsUnmapped++;
        XHCI_DBG_VALUE_CHANGED("port event: not a managed port", portId);
        return;
    }

    ext->PortEventsMapped++;
    if (!xhciRhAdmitted(ext)) {
        return;
    }

    latched = xhciRhRefresh(ext, hubPort, &ext->RootHub.Ports[hubPort - 1]);
    if (latched != 0) {
        ext->PortEventChanges++;
        ext->RootHubInvalidatesOwed++;
        XHCI_DBG_VALUE_LIMITED("port event: hub-class changes latched",
                               latched);
    }

    /*
     * The device-initiated resume's arming site, and it has no callback to hang
     * off - a device that wants to wake the machine signals resume on a
     * suspended port, the port enters the Resume state by itself, and usbport is
     * told nothing until this driver reports the completed resume. So the
     * notification 4.15.2.1 (p.256) says to time from *is* this event.
     *
     * It only decides; the arm is a usbport service, so it goes through
     * ArmPending and XhciRootHubDeferredWork, which the DPC calls after
     * releasing the lock this function runs under.
     */
    xhciRhArmDeviceResume(ext, hubPort, &ext->RootHub.Ports[hubPort - 1]);
}
