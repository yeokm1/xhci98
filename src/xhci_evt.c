/*
 * xhci_evt.c - the interrupt path: the miniport ISR body and the event-ring
 * drain that usbport's DPC calls.
 *
 * Roadmap Phase 4 task 4, and it lands *before* anything enables a hardware
 * interrupt on purpose (docs/contributing/implementation-invariants.md, "Interrupt
 * Ordering"): usbport owns the interrupt object and calls EnableInterrupts the
 * moment StartController returns success, so the callbacks have to be real by
 * then. Task 5 sets USBCMD.RUN, task 6 sets IMAN.IE and USBCMD.INTE, and until
 * task 6 nothing in this file can be reached with an event to consume.
 *
 * The division of labour between the two halves is the whole design:
 *
 *   The ISR **carries nothing forward to the DPC**. It proves ownership from
 *   USBSTS.EINT, acknowledges EINT and then IMAN.IP, and returns TRUE. It does
 *   not look at the event ring, does not queue anything, and keeps no
 *   ISR-to-DPC bitmask - the Event Ring *is* the pending-work queue, and usbport
 *   queues the DPC off the TRUE (docs/usb-xhci-info/usbport-miniport-abi.md
 *   section 4, InterruptService). It is not literally *stateless*, which is what
 *   this line said until audit round 8: it bumps four diagnostic counters and
 *   latches `LastIsrStatus`, and the restore's lockless window is justified
 *   against the narrow property - no event-ring and no `ERDP` access - rather
 *   than against the wider word.
 *
 *   The DPC owns every event accumulated since its last run, not one
 *   interrupt's worth: a queued DPC object is coalesced, so a second interrupt
 *   arriving before the callback is dequeued adds no second callback (Oney
 *   p.187). It therefore drains until the Cycle Bit says empty rather than
 *   stopping after one event or one snapshot.
 *
 * The miniport allocates neither object. usbport calls IoConnectInterrupt and
 * owns the DPC; this file only supplies the two bodies.
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
/* For `XhciXferCodeInfo`: the restore's drain decides which stale events refuse
 * the restore by asking the completion-code table rather than by carrying a list
 * of codes of its own, which is what audit rounds 6, 7 and 8 each found short. */
#include "xhci_xfer.h"
#include "xhci_dbg.h"

/* XHCI_ISR_IMAN_READ_ATTEMPTS is in src/xhci_hw.h, with the contract it is part
 * of - the host vector for the literal fallback counts exactly that many reads. */

/* ------------------------------------------------------------------ */
/* ERDP publication                                                    */
/* ------------------------------------------------------------------ */

/*
 * Tell the controller how far software has consumed.
 *
 * `ehb` is 1 only on the write that follows an empty ring (or the bounded exit
 * below). Event Handler Busy is RW1C in bit 3, so an intermediate write puts a
 * **0** there - which leaves EHB set and keeps this interrupter suppressed for
 * the rest of the drain. Writing 1 mid-drain would clear it and let the
 * interrupter fire into a drain already in progress.
 *
 * Doing it at all mid-drain is not optional: the xHC decides the Event Ring is
 * full from the dequeue pointer software has advertised, so a stale ERDP
 * during a burst produces Event Ring Full (completion code 21) while the DPC
 * is busy consuming that very ring.
 *
 * IRQL: DISPATCH_LEVEL under the controller lock from the DPC and from
 * `XhciEnableInterrupts`, and PASSIVE_LEVEL without it from
 * `XhciEventDiscardStale`, which is the restore's own single-threaded window -
 * see that function for why the lock is neither held nor needed there. Audit
 * round 6 corrected this line, which still said "DPC only" after round 5 had
 * added the third caller.
 */
static VOID xhciPublishErdp(PXHCI_EXTENSION ext, ULONG ehb)
{
    XhciWrite64(ext, ext->HcInfo.RuntimeOffset + XHCI_RT_IR0 + XHCI_IR_ERDP,
                XhciEventRingErdpValue(&ext->EventRing, ehb));
}

/* ------------------------------------------------------------------ */
/* One event                                                           */
/* ------------------------------------------------------------------ */

/*
 * The single handler every event type is dispatched through, and it is bounded
 * by construction: a switch with no loop, no wait and no call back into
 * usbport. That matters because the DPC runs at DISPATCH_LEVEL under usbport's
 * MiniportInterruptsSpinLock and the driver-image controller lock - a handler
 * that could block or spin would hold both while it did.
 *
 * Exactly one arm acts - Command Completion, since task 7 - and every other one
 * says which task or phase owns acting on it. That is not a placeholder: the
 * only two events a controller can currently produce are a Port Status Change
 * and the completion of the No Op self-test, and the Phase 4 checkpoint asks
 * precisely that both be *observed*. Counting each type and naming the port is
 * exactly what that checkpoint reads.
 *
 * The command arm may read CRCR and request a controller reset. The read stays
 * inside the stable controller lock.
 * The usbport reset service is deferred until the DPC releases that lock.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static ULONG xhciHandleEvent(PXHCI_EXTENSION ext, const XHCI_TRB *trb)
{
    ULONG type;

    type = XHCI_TRB_GET_TYPE(trb->Control);

    /*
     * Three outcomes, not two, because "advance past and ignore" is the right
     * action for two quite different findings and only the counters can tell
     * them apart afterwards (see the range comment in xhci.h).
     */
    if (type >= XHCI_TRB_TYPE_VENDOR_FIRST &&
        type <= XHCI_TRB_TYPE_VENDOR_LAST) {
        /* Legal here. "Software shall advance past and ignore Vendor Defined
         * TRBs encountered on an Event Ring" (4.11.6, p.211), which is what
         * returning without acting on it does. Not an anomaly: this driver has
         * no vendor knowledge, and the type ID would mean nothing without
         * qualifying it by PciVendorDevice anyway. */
        ext->EventsVendor++;
        XHCI_DBG_VALUE_CHANGED("event: vendor defined, ignored, type", type);
        return 0;
    }

    if (type < XHCI_EVENT_TYPE_FIRST || type > XHCI_EVENT_TYPE_LAST) {
        /* Reserved (40-47), or a command/transfer type that has no business on
         * an event ring. Counted rather than merely ignored: it means either
         * the controller or this driver's idea of where the event ring lives is
         * wrong. */
        ext->EventsUnknown++;
        XHCI_DBG_VALUE_CHANGED("event: type that cannot be on the event ring",
                               type);
        return 0;
    }

    ext->EventCounts[XHCI_EVENT_TYPE_INDEX(type)]++;

    switch (type) {
    case XHCI_TRB_TYPE_PORT_STATUS_CHANGE:
        /*
         * The event carries a port number and nothing else - "system software
         * shall read the PORTSC register of the port that generated the event to
         * determine the cause" (4.19.2) - so the work is a refresh of that one
         * port's shadow, which XhciRootHubPortEvent does under the lock this DPC
         * already holds (Phase 5 task 2).
         *
         * **The announcement is not here**, and neither is the timer arm a
         * device-initiated resume needs: both are usbport services, so they are
         * decided under this lock and performed by XhciRootHubDeferredWork after
         * the drain releases it (Phase 5 tasks 4 and 5). The announcement is the
         * one where that is a hard requirement rather than lock-order hygiene -
         * UsbPortInvalidateRootHub calls RH_DisableIrq back into this miniport,
         * which takes this same non-recursive lock.
         */
        ext->LastPortEventId = XHCI_TRB_GET_PORT_ID(trb->Param0);
        XhciRootHubPortEvent(ext, ext->LastPortEventId);
        /*
         * Per occurrence, not per distinct value: an unplug names the same port
         * its plug did, so the change-gated form witnessed only the plugs and
         * the checkpoint's "plugged/unplugged" clause was left resting on the
         * event counter (LESSONS).
         */
        XHCI_DBG_VALUE_LIMITED("event: port status change on port",
                               ext->LastPortEventId);
        break;

    case XHCI_TRB_TYPE_COMMAND_COMPLETION:
        /*
         * The one arm that acts. XhciCommandEvent (src/xhci_cmd.c) matches by
         * TRB pointer under the controller lock held by this DPC. usbport's
         * MiniportInterruptsSpinLock does not exclude submit or timeout.
         */
        XHCI_DBG_VALUE_CHANGED("event: command completion, code",
                               XHCI_TRB_GET_COMPLETION(trb->Status));
        return XhciCommandEvent(ext, trb);

    case XHCI_TRB_TYPE_TRANSFER_EVENT:
        /*
         * Phase 6 batch B. XhciSlotTransferEvent resolves the event's Slot ID
         * and Endpoint ID to a device record and hands it to that endpoint's
         * queue, all under the controller lock this DPC already holds; the
         * transfers it retires are threaded onto the extension's completion list
         * and answered by XhciSlotDeferredWork after the release below, because
         * UsbPortCompleteTransfer is a usbport service (design doc 05 section 7).
         */
        XHCI_DBG_VALUE_CHANGED("event: transfer, code",
                               XHCI_TRB_GET_COMPLETION(trb->Status));
        return XhciSlotTransferEvent(ext, trb);

    case XHCI_TRB_TYPE_HOST_CONTROLLER:
        /*
         * A controller-wide error - Event Ring Full is the one this driver can
         * plausibly cause. **It escalates from here**, rather than being left
         * for CheckController to notice, and the earlier note deferring that to
         * task 8 was wrong about what the two callbacks can see. CheckController
         * reads USBSTS, and neither Event Ring Full nor Event Lost sets HCE or
         * HSE: no poll of that register will ever see them. So a driver that
         * merely recorded the code here and waited for the poll would wait
         * forever, having already been told - by the controller, in the one
         * place it reports a controller-level fault - that events were dropped.
         * (**Not the only place Event Lost is reported**, which audit round 7
         * corrected elsewhere: a TD-related one also arrives as a Transfer Event
         * with code 32 and halts that endpoint, 4.10.1 p.173. Both routes
         * escalate, from here and from `xhciXferCodeInfo`'s `Fatal`.)
         *
         * Dropped events is what makes this fatal rather than a statistic. Every
         * completion this driver matches is matched by identity - a command by
         * its TRB pointer, later a transfer by its TD - so a lost event is not a
         * delayed one: nothing arrives afterwards that resolves it, the
         * one-outstanding-command engine stays PENDING, and its watchdog aborts
         * a ring whose real state nobody knows. docs/contributing/implementation-invariants.md,
         * "Fatal Errors" gives the answer for that state - stop submitting, fail
         * pending work, request UsbPortInvalidateController(RESET) - and this is
         * the site that detects it.
         *
         * Every completion code escalates, including the vendor-defined ranges.
         * A Host Controller Event is controller-level by construction (Table
         * 6-91), and this driver has no vendor knowledge with which to declare
         * one of them benign - the same reason XhciCommandSubmit refuses vendor
         * TRB types outright. Reading a code as "informational" on a controller
         * that has just reported a controller-level fault is the assumption with
         * the worse failure mode.
         *
         * **That is a deliberate deviation from a "shall", and audit round 7
         * asked for it to be named as one rather than left implicit.** Table
         * 6-90 says of codes 224-255: "If software does not recognize the code,
         * it shall interpret this range of vendor defined values as a Success
         * condition code" (p.470). This driver escalates them instead, so a
         * controller that reports vendor *information* through a Host Controller
         * Event gets an HCRST and a re-enumeration it did not need. The trade is
         * accepted because the cost is bounded and recoverable (one reset on a
         * controller nobody has observed doing this) while the converse - a
         * genuine controller fault read as Success - is a silent stall with no
         * second notice. Revisit it if a fleet machine is ever seen emitting one:
         * that is a measurement, and this is a policy without one.
         *
         * Capped like every other site, and the argument for exempting it -
         * "one of these is worth a line every time" - was wrong for the case
         * that produces them. An Event Ring Full cascade generates these in
         * bulk, so an uncapped line here is up to XHCI_DPC_MAX_EVENTS DbgPrint
         * calls and tens of thousands of port-0xE9 bytes in a single DPC,
         * holding usbport's MiniportInterruptsSpinLock at DISPATCH_LEVEL - the
         * Phase 3 trace-noise defect again, in the one path least able to
         * afford it. The cascade repeats one completion code, so on-change
         * prints it once; EventCounts and LastHostControllerCode carry the rest,
         * and they are readable from a release build besides.
         */
        ext->LastHostControllerCode = XHCI_TRB_GET_COMPLETION(trb->Status);
        ext->HostControllerEventResets++;
        XHCI_DBG_VALUE_CHANGED("event: HOST CONTROLLER EVENT, completion code",
                               ext->LastHostControllerCode);
        return 1;

    default:
        /*
         * Doorbell, Device Notification, MFINDEX Wrap: all legal, none of them
         * produced by anything this driver does. The per-type counter above is
         * the record; this is the trace.
         *
         * **Bandwidth Request is the one that does not need us to have asked**,
         * which audit round 7 raised: "System software may never issue a
         * Negotiate Bandwidth Command, however if the BNC flag is '1' an
         * unsolicited Bandwidth Request Event may be generated by hardware, e.g.
         * if the system software is running in a Virtual Machine and
         * communicating with an xHCI Virtual Function ... System software should
         * immediately honor an unsolicited Bandwidth Request Event and free
         * unused USB bandwidth" (4.6.9.1, p.133-134). This driver does not, and
         * cannot usefully: under Option A usbport owns bandwidth allocation and
         * the miniport has no configuration to lower. Ignoring it is therefore a
         * declared limitation rather than an oversight - the wording is "should",
         * the trigger is an SR-IOV virtual function, and neither target runs as
         * one. It is counted and traced like the rest; a nonzero per-type count
         * for 35 on a machine is the evidence that this ever mattered.
         */
        XHCI_DBG_VALUE_CHANGED("event: unhandled event type", type);
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* The ISR body                                                        */
/* ------------------------------------------------------------------ */

/*
 * usbport's connected ISR calls this at DIRQL, and only while its own
 * interrupt-enabled flags are set; it queues the miniport DPC exactly when
 * this returns TRUE (docs/usb-xhci-info/usbport-miniport-abi.md section 4).
 *
 * Ownership comes from USBSTS.EINT, "a logical OR of the IMAN register IP flag
 * '0' to '1' transitions" (5.4.2, p.365). On a shared PCI line - the normal
 * case on both target VMs, where an EHCI controller sits on the same IRQ - a
 * FALSE here is what lets the other device's ISR run.
 *
 * **EINT is acknowledged before IMAN.IP, and the order is the spec's, not a
 * preference**: "Software that uses EINT shall clear it prior to clearing any
 * IP flags. A race condition may occur if software clears the IP flags then
 * clears the EINT flag, and between the operations another IP '0' to '1'
 * transition occurs. In this case the new IP transition shall be lost" (5.4.2,
 * p.364). Clearing IP is separately mandatory rather than merely tidy: with pin
 * interrupts "IP shall be cleared to '0' by software" (4.17.5, p.270) and the
 * INTx line stays asserted while it is set, so a return without it is an
 * interrupt storm.
 *
 * There is no window in between for the acknowledgement to lose an event. The
 * xHC set EHB when it set IP, and it will not set IP again while EHB is set
 * (4.17.5, p.270) - only the DPC's final ERDP write clears that.
 *
 * IRQL: DIRQL.
 */
BOOLEAN XhciIsr(PXHCI_EXTENSION ext)
{
    ULONG usbsts;
    ULONG iman;
    ULONG attempt;

    /*
     * Leading signature only, and then the two questions the gates below are
     * really made of. Neither is defensive decoration: this runs at DIRQL
     * through a pointer usbport supplied.
     */
    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return FALSE;
    }

    /*
     * **Can this driver touch a register at all**, which is a different
     * question from whether it is admitted. HcInfo is what holds the real
     * register bases; without a decode behind it the two accessors below would
     * compute an offset from a zeroed structure and read some other device's
     * mapping. Nothing can relax this one - a controller whose window has never
     * been decoded is not one whose interrupt can be acknowledged either.
     *
     * XHCI_EXT_FLAG_INITIALIZED cannot answer it, because quiesce clears that
     * flag while HcInfo stays valid for the whole halt window; and
     * XhciInitController sets HcInfoStatus bad on entry, so a re-initialization
     * is excluded here for exactly as long as it is re-deriving the bases.
     */
    if (ext->HcInfoStatus != XHCI_HC_OK) {
        return FALSE;
    }

    /*
     * **Is this driver admitted**, and the answer is conditional on something
     * the gates themselves do not know. Declining is only free once this
     * controller can no longer be asserting the shared level-triggered line -
     * "prohibited from generating interrupts" (5.5.2.1, p.391) is a statement
     * about a controller whose enables are actually down. Both transitions that
     * publish a decline mask first for that reason, but a mask whose register
     * window answered all ones *wrote nothing*, and then a decline strands an
     * asserted INTx that nobody will acknowledge - an interrupt storm that
     * livelocks a UP Win98 box, which is the failure the mask order exists to
     * prevent, arriving through the gate instead.
     *
     * So the gates apply only while the enables are provably down. Otherwise
     * fall through and prove ownership from USBSTS, which costs a read on a
     * neighbour's interrupt in a state that lasts until the next mask succeeds.
     * If the window really is dead the read answers all ones and the claim is
     * declined below anyway, so this cannot claim an interrupt for a controller
     * that is gone.
     */
    if (((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0 ||
         ext->ControllerFailed) &&
        ext->InterruptDeliverySuppressed) {
        return FALSE;
    }

    /*
     * Every entry, claimed or not - and InterruptsClaimed below counts only the
     * ones that were ours. On a shared line the two answer different questions,
     * and the pair is what separates "our interrupt never routes here" from "it
     * routes but the controller has never had anything to report". Phase 3's
     * single counter meant entries; it still does.
     */
    ext->InterruptCount++;

    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    ext->LastIsrStatus = usbsts;

    /*
     * All-ones is a controller that has stopped decoding - surprise removal, or
     * a mapping torn down under us. Claiming it would say "handled" about a
     * device that is gone, on a line another device may be asserting.
     */
    if (usbsts == 0xFFFFFFFFUL) {
        return FALSE;
    }
    if ((usbsts & XHCI_USBSTS_EINT) == 0) {
        return FALSE;
    }

    /*
     * The bit mask, never the read value ORed back: USBSTS is RW1C throughout,
     * so writing back what was read acknowledges HSE, SRE and PCD that nothing
     * has looked at (docs/usb-xhci-info/xhci-data-structures.md section 3). CheckController
     * owns those.
     */
    XhciWriteOp(ext, XHCI_OP_USBSTS, XHCI_USBSTS_EINT);

    /*
     * IMAN is IP (RW1C) in bit 0, IE (RW) in bit 1, RsvdP above - so a
     * read-modify-write is the only way to clear IP without deciding IE, and
     * preserving RsvdP is what RsvdP asks for. The read-modify-write is
     * unavoidable; what task 9 removes is the *race* it used to carry.
     *
     * **IE is written as 0, not carried through.** This ISR runs at DIRQL and
     * cannot take the driver's DISPATCH-level controller lock, so it is the one
     * context that is not excluded from `XhciMaskInterrupts` - which
     * `DisableInterrupts`, quiesce, suspend and the terminal failure transition
     * all run under that lock. An ISR that carried IE through would re-publish
     * an enable those paths had just cleared, whenever the mask landed between
     * this read and this write. Forcing IE to 0 makes the ISR move that bit in
     * **the same direction they do**, so there is no value of the mask's state
     * that this write can undo: the two contexts no longer need to exclude each
     * other at all, which is the only shape of closure available to a lock the
     * ISR cannot hold. (`docs/contributing/design/05-locking-model.md`, "The DIRQL
     * exception".)
     *
     * **And it costs no delivery**, which is what makes it a fix rather than a
     * trade. The xHC set EHB when it set IP - EHB "shall be set to '1' when the
     * IP bit is set to '1'" (5.5.2.3.3, p.394) - and IP cannot be set again
     * while EHB is set: "when IMODC transitions to '0': if EHB = '0' and IPE =
     * '1', then IP shall be set to '1'" (4.17.5, p.270). So throughout the
     * window this write opens, no interrupt can be generated whatever IE holds.
     * The one thing that clears EHB is the DPC's final ERDP write, and the DPC
     * re-arms IE immediately after it, under the controller lock and only while
     * usbport still wants interrupts. IE clear is therefore this driver's
     * *normal* state between an interrupt and its DPC, not an error state.
     *
     * **The operand is validated first, like every other IMAN read-modify-write
     * in this driver** (`XhciMaskInterrupts`, `XhciUnmaskInterrupts` and
     * `XhciRearmInterrupter` all refuse all ones; the ISR was the one gap).
     * An undecoding window fed through the RsvdP-preserving form would publish
     * every reserved bit as 1. The retries cover a glitch that clears on the
     * next access and nothing longer - this runs at DIRQL and may not wait.
     */
    for (attempt = 0; attempt < XHCI_ISR_IMAN_READ_ATTEMPTS; attempt++) {
        iman = XhciReadIr0(ext, XHCI_IR_IMAN);
        if (iman != 0xFFFFFFFFUL) {
            XhciWriteIr0(ext, XHCI_IR_IMAN,
                         (iman & ~XHCI_IMAN_IE) | XHCI_IMAN_IP);
            ext->InterruptsClaimed++;
            return TRUE;
        }
    }

    /*
     * **The documented fail-safe, and it is an exception to the
     * RsvdP-preservation rule rather than an instance of it**
     * (docs/contributing/implementation-invariants.md, "Interrupt Ordering"). With no valid
     * operand there is no value from which a compliant RMW can be formed, and
     * both alternatives are worse: writing nothing leaves IP set behind an EHB
     * only the DPC clears, so a shared level-triggered INTx this controller is
     * asserting has no acknowledger at all - the livelock the mask order exists
     * to prevent - while deriving the write from all ones publishes RsvdP as 1
     * *and* asserts IE.
     *
     * The literal is monotone in the direction every masking path moves: IP
     * acknowledged, IE written 0, reserved written 0 - which is the value a
     * reset leaves and the only defensible guess. Counted, because "the
     * interrupter window stopped decoding inside an ISR" is a hardware
     * diagnosis, and CheckController traces the counter.
     */
    ext->IsrImanLiteralAcks++;
    XhciWriteIr0(ext, XHCI_IR_IMAN, XHCI_IMAN_IP);

    ext->InterruptsClaimed++;
    return TRUE;
}

/*
 * Receive every event the ring is already holding and drop all but the ones that
 * refuse the restore. Returns the number consumed, fatal ones included.
 *
 * "Without acting on any of it" is what this line said until audit round 8, and
 * it stopped being true in round 6: an event that fails the restore is the most
 * consequential thing this function can do with one.
 *
 * **This is the restore's half of save step 2**, and it exists because the
 * suspend does not discharge that step. 4.23.2 step 2 requires that "all
 * Command Completion Events associated with them have been received" (p.313);
 * `XhciQuiesceController` only *abandons* an outstanding command in software,
 * so a completion the xHC had already written can still be sitting on the event
 * ring when the controller comes back.
 *
 * **Leaving it there is not harmless, which is what the first version of the
 * save's comment claimed.** `xhciCommandCompleted` matches a completion by
 * physical address alone - `pointer == ext->CommandTrbPA` - and the restore
 * rebuilds the command ring from TRB zero, so the very next command the resume
 * issues can be handed the *same* address the abandoned one had. The stale event
 * would then match it and retire it, giving the slot layer another command's
 * completion code and another command's Slot ID. The address is the only
 * discriminator there is: `CommandGeneration` and `StartEpoch` exist, but they
 * arm the watchdog and are not carried on the event.
 *
 * So the events are received here instead - late, but received, which is the
 * word the step uses - and dropped. Three things make dropping them right:
 *
 *   Every event on the ring predates the suspend, because this runs before R/S
 *   is set and nothing has executed since the save.
 *
 *   Their owners have already been told. An abandoned command reached
 *   `XhciSlotCommandLost` during the quiesce, so a slot waiting on one has
 *   already been given its answer; delivering a second would be the duplicate,
 *   not the loss.
 *
 *   An ordinary Transfer Event is a transfer usbport has already withdrawn.
 *   **Audit round 7 found the argument that used to sit here to be false** - it
 *   said the save is declined while any endpoint queue is non-empty, so no
 *   transfer can be outstanding. The gate (`xhciSaveState`) counts *software*
 *   queue entries, and `XhciAbortTransfer` removes a transfer from that queue
 *   while the xHC may still own its TRBs: `AbortsBeforeStopped` exists to count
 *   exactly that window, and `XhciSlotCommandLost` clears `ActiveOp` for a Stop
 *   Endpoint that never answered without draining anything. So the gate can pass
 *   with hardware work still live, and a Transfer Event for it can reach this
 *   ring. Dropping *that* event is still right - its transfer was withdrawn by
 *   usbport and completed to it at abort time, so there is no owner left to
 *   tell - but the reason is the withdrawal, not an emptiness the gate does not
 *   establish.
 *
 *   A Port Status Change Event among them carries nothing a fresh read does not,
 *   because `XhciRootHubInit` re-seeds every PORTSC shadow from live registers
 *   immediately after this - step 9 of the same procedure, mandatory precisely
 *   because "the state of a Root Hub port is not covered by a Save or Restore
 *   operation" (p.315).
 *
 * **Those three cover Command Completion, Transfer and Port Status Change, and
 * audit round 6 found that they were being applied to every other type as
 * well.** A Host Controller Event is the one that breaks them: it is a
 * controller-level fault reported nowhere in `USBSTS` - neither Event Ring Full
 * nor Event Lost sets `HCE` or `HSE`, which is why the DPC escalates from its
 * own arm rather than leaving it to `CheckController` - and nothing in the save
 * gate excludes one from being on the ring. Dropping it would restart the
 * controller that raised it with the fault unrecorded.
 *
 * **`Event Lost` was the second one, and audit round 7 found it because round 6's
 * own prose was too strong.** That prose said Event Ring Full and Event Lost
 * report themselves *only* as a Host Controller Event. That is true of Event
 * Ring Full and false of Event Lost: "An Event Lost Error shall be generated for
 * the endpoint if the xHC is unable to generate all the Events defined by a TD.
 * An Event Lost Error shall halt the endpoint" (4.10.1, p.173), and Table 6-90
 * gives it as completion code 32 - so a TD-related overrun arrives as an
 * ordinary **Transfer Event** carrying code 32, on an endpoint the xHC has
 * halted. A drain that dropped it would restore over a halted endpoint and a
 * controller that has told us it lost events - the same restart-over-a-fault the
 * Host Controller Event arm exists to refuse.
 *
 * **Audit round 8 then found that counting to two was the defect, not the pair.**
 * Table 6-90 marks a third code fatal outright - "An Undefined Error shall be
 * treated as a fatal error by software" (33, p.469) - and gives the vendor error
 * range 192-223 that same reading for any code software does not recognise,
 * which is every one of them here. A fourth, Incompatible Device Error (22), is
 * fatal to the *slot* and names a Disable Slot as its recovery (p.468); a
 * successful restore would preserve exactly the slot that owes one, so refusing
 * is what discharges it. Two rounds had each hand-written the list and each been
 * found short by the next, so this arm no longer holds a list at all: it asks
 * `XhciXferCodeInfo`, the one place Table 6-90 is transcribed, and inherits
 * whatever that table calls fatal. The *type* test stays written out, because a
 * Host Controller Event's status field is not a completion code to consult.
 *
 * Fatal events are counted and the first one to decide the reading is reported
 * to the caller through `fatalEvent`, which fails the restore and takes the
 * resume down its reinitialisation path - HCRST and a fresh event ring, the
 * stronger of the two repairs and the one already written. Every other
 * completion code stays droppable for the reasons above, and vendor-defined and
 * out-of-range *types* stay droppable too: the spec's instruction for the first
 * is "advance past and ignore" (4.11.6, p.211), and this driver has never acted
 * on either.
 *
 * ERDP is published once at the end, with EHB left as it was: no interrupt can
 * be delivered here anyway, since the resume has not re-enabled them yet.
 *
 * **The four-lap bound is not the DPC's bound and the two are safe for
 * different reasons** - round 6 corrected the claim that they share one. The
 * DPC's exists so that a controller producing events faster than software
 * consumes them cannot own a CPU at DISPATCH_LEVEL; here the xHC is halted, no
 * doorbell has been rung and the CRCR write above fetches nothing until one is,
 * so there is no producer at all. What makes this bound unreachable rather than
 * merely generous is arithmetic: it is four times a ring that holds
 * `XHCI_EVENT_RING_TRBS` entries, so a ring nothing is refilling is empty long
 * before the count is spent, and the loop always exits on the Cycle Bit.
 *
 * IRQL: PASSIVE_LEVEL (restore only). **The controller lock is not held** -
 * `XhciResumeController` calls `xhciRestoreState` without it, which round 6
 * caught this comment claiming otherwise. What makes that sound is that nothing
 * else can reach the event ring or `ERDP` in this window: `INITIALIZED` is
 * clear, so every usbport callback that would take the lock declines, and the
 * DPC is not queued; the controller is halted, so it produces nothing.
 *
 * **The ISR is excluded by what it does, not by the mask** - audit round 7
 * corrected that. `XhciMaskInterrupts` reports delivery *proven* suppressed or
 * not, and its out-of-attempts exit leaves `InterruptDeliverySuppressed` at 0
 * with neither enable proven clear (`src/xhci_init.c`), so an ISR can still run
 * here - the suite proves one does, after a quiesce, with `INITIALIZED` clear.
 * It is harmless rather than excluded, and **audit round 8 narrowed the claim to
 * the part that carries the weight**: the ISR touches neither the event ring nor
 * `ERDP`, so it cannot race a walk of the one or a publication of the other.
 * Round 7 had written "stateless, touches `USBSTS` and `IMAN` only", which is
 * not true as stated - the ISR reads `HcInfoStatus`, `Flags`, `ControllerFailed`
 * and `InterruptDeliverySuppressed`, and writes `InterruptCount`,
 * `LastIsrStatus`, `InterruptsClaimed` and `IsrImanLiteralAcks` - and a future
 * edit measuring itself against the wider claim would find it already false and
 * learn nothing from that. The narrow property is the one to preserve: it is the
 * lockless-reinitialisation precondition of design doc 05 section 2 applied to a
 * path that reinitialises less, and section 5 names this the third `ERDP`
 * writer. (Those counters are why the window is safe rather than why it is
 * lockless: each is written only by the ISR, so no reader in the restore can see
 * a torn value it then acts on.)
 */
ULONG XhciEventDiscardStale(PXHCI_EXTENSION ext, PULONG fatalEvent)
{
    XHCI_TRB trb;
    XHCI_XFER_CODE info;
    ULONG discarded;
    ULONG fatal;
    ULONG kind;
    ULONG kindCode;
    ULONG type;
    ULONG code;

    if (fatalEvent != NULL) {
        *fatalEvent = XHCI_RESTORE_FATAL_NONE;
    }
    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }

    discarded = 0;
    fatal = 0;
    /*
     * The reported kind and the code that produced it move together, so that a
     * machine cannot read a kind from one event beside a code from another.
     * `fatalEvent` is written from these at the end rather than in the arms:
     * audit round 8 found the stored fields and the out-parameter obeying the
     * precedence rule separately, which is two implementations of one rule.
     */
    kind = XHCI_RESTORE_FATAL_NONE;
    kindCode = 0;
    while (discarded < XHCI_DPC_MAX_EVENTS &&
           XhciEventRingDequeue(&ext->EventRing, &trb) == XHCI_RING_OK) {
        discarded++;
        type = XHCI_TRB_GET_TYPE(trb.Control);
        code = XHCI_TRB_GET_COMPLETION(trb.Status);
        if (type == XHCI_TRB_TYPE_HOST_CONTROLLER) {
            ext->LastHostControllerCode = code;
            fatal++;
            /* Outranks a completion code already found: this one is the
             * controller's own statement of the failure. */
            kind = XHCI_RESTORE_FATAL_HOST_CONTROLLER;
            kindCode = code;
            XHCI_DBG_VALUE("restore: a HOST CONTROLLER EVENT was waiting on the "
                           "ring, completion code",
                           ext->LastHostControllerCode);
        } else if ((type == XHCI_TRB_TYPE_TRANSFER_EVENT ||
                    type == XHCI_TRB_TYPE_COMMAND_COMPLETION) &&
                   XhciXferCodeInfo(code, &info) == XHCI_XFER_OK &&
                   (info.Fatal != 0 || info.SlotFatal != 0)) {
            /*
             * **The table decides, not a list of codes here.** Rounds 6 and 7
             * each wrote one out by hand and each was found short by the next
             * round; round 8 found the pair short by two more (Undefined Error,
             * and the vendor error range that Table 6-90 says to read as it).
             *
             * A slot-fatal code counts too, and refusing is what discharges it:
             * Incompatible Device Error owes a Disable Slot (p.468), which a
             * successful restore would carry past by preserving the very slot
             * that owes it. The reinitialisation takes the slot with it.
             *
             * A Command Completion Event is included because Table 6-90's fatal
             * codes are not scoped to transfers - "This error may be returned by
             * any command or transfer" is the wording for one of them - and the
             * rebuild of the command ring answers a *stale retirement*, not a
             * fault the controller reported while it was executing.
             */
            fatal++;
            if (kind == XHCI_RESTORE_FATAL_NONE) {
                kind = XHCI_RESTORE_FATAL_COMPLETION_CODE;
                kindCode = code;
            }
            XHCI_DBG_VALUE("restore: an event carrying a fatal completion code "
                           "was waiting on the ring, code", code);
        }
    }

    if (kind != XHCI_RESTORE_FATAL_NONE) {
        ext->RestoreFatalKind = kind;
        ext->RestoreFatalCode = kindCode;
        if (fatalEvent != NULL) {
            *fatalEvent = kind;
        }
    }

    /*
     * The two counters partition what was consumed, rather than overlapping:
     * `RestoreEventsDiscarded` is documented in `xhci.h` as events deliberately
     * *not* acted on, and a fatal one is acted on - it refuses the restore. Audit
     * round 7 found a fatal event counted in both, which would have had a machine
     * report the same event as a fault and as harmless residue at once.
     */
    if (fatal != 0) {
        ext->RestoreEventsFatal += fatal;
    }
    if (discarded != 0) {
        xhciPublishErdp(ext, 0);
        ext->RestoreEventsDiscarded += discarded - fatal;
        /* `discarded - fatal`, not `discarded`: audit round 8 found this line
         * printing the total under a label that says dropped, so a ring holding
         * one fatal event and nothing else traced "1 dropped" beside a counter
         * that correctly read zero. The return value is still the total - it is
         * the loop's own measure of what was consumed - and its callers are
         * documented to read it that way. */
        XHCI_DBG_VALUE("restore: stale events received and dropped",
                       discarded - fatal);
    }

    return discarded;
}

/* ------------------------------------------------------------------ */
/* The DPC body                                                        */
/* ------------------------------------------------------------------ */

/*
 * Drain the event ring, then release the interrupter.
 *
 * `enableInterrupts` is usbport's own "interrupts should be enabled" state, not
 * a request from the hardware: when it is FALSE, usbport has disabled the
 * miniport's interrupts and this pass must not put them back.
 *
 * IRQL: DISPATCH_LEVEL, under usbport's MiniportInterruptsSpinLock and the
 * stable driver-image controller lock. The latter excludes submit, timeout,
 * interrupt enable/disable, and terminal failure while this bounded pass owns
 * the event and command-ring state.
 */
VOID XhciEventDpc(PXHCI_EXTENSION ext, BOOLEAN enableInterrupts)
{
    XHCI_TRB trb;
    ULONG drained;
    ULONG sincePublish;
    ULONG bounded;
    ULONG ringEmpty;
    ULONG resetRequested;
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    /*
     * Reset and lifecycle quiesce can both begin after this DPC was queued. The
     * admission decision is therefore made under their lock, not before it:
     * draining after either transition would publish ERDP and re-arm IMAN.IE
     * into a controller that is failed or heading toward D3.
     */
    XhciControllerLockAcquire(&oldIrql);
    if (ext->ControllerFailed) {
        ext->DpcsAfterFailure++;
        XhciControllerLockRelease(oldIrql);
        return;
    }
    if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0) {
        XhciControllerLockRelease(oldIrql);
        return;
    }

    ext->DpcCount++;

    drained = 0;
    sincePublish = 0;
    bounded = 0;
    ringEmpty = 0;
    resetRequested = 0;

    for (;;) {
        if (drained >= XHCI_DPC_MAX_EVENTS) {
            bounded = 1;
            break;
        }
        if (XhciEventRingDequeue(&ext->EventRing, &trb) != XHCI_RING_OK) {
            ringEmpty = 1;
            break;
        }

        resetRequested |= xhciHandleEvent(ext, &trb);

        drained++;
        ext->EventsTotal++;
        sincePublish++;
        if (sincePublish >= XHCI_ERDP_PUBLISH_EVERY) {
            xhciPublishErdp(ext, 0);
            sincePublish = 0;
        }
    }

    /*
     * **Task 9-0.2, and the gate is "the ring was observed empty".** That is a
     * different question from "did this pass stop at its bound", and the first
     * draft asked the second one - which is the defect the round-1 review found.
     *
     * The loop tests `drained >= XHCI_DPC_MAX_EVENTS` *before* it dequeues, so a
     * pass whose last consumed event happens to be the `XHCI_DPC_MAX_EVENTS`-th
     * leaves with `bounded` set having never asked the ring anything. If that
     * event was also the last one the controller had written, the ring is empty,
     * the settle is skipped - and nothing brings the DPC back: the interrupter's
     * IPE "shall be cleared to '0' ... if the Event Ring transitions to empty"
     * (4.17.5 p.270), so the re-assertion the bounded exit relies on never
     * happens. The deferred transfer then sits on the queue for the life of the
     * endpoint, never completing up to usbport, which never reposts - the dead
     * bulk IN endpoint of batch 8-V.2 reintroduced by its own fix.
     *
     * So the bounded exit takes the observation it skipped, with the same
     * non-destructive cycle-bit test the drain loop uses. `bounded` keeps its
     * meaning for `DrainBoundHits`; only the settle's gate changes.
     *
     * What the gate must guarantee is unchanged and is the whole content of the
     * fix: at the instant the ring reads empty every Transfer Event the xHC had
     * written has been consumed and matched, so a TD still holding a deferred
     * mid-TD short packet is one whose promised tail (4.10.1.1.2 p.175) was
     * never sent, and retiring it re-lets TRBs with no event outstanding against
     * them. A pass that stopped at its bound with events *still in the ring*
     * still does not qualify - the tail may be one of them - and those deferrals
     * wait for the pass that empties it, which a non-empty ring does guarantee.
     *
     * Before the ERDP publish rather than after it: EHB is still set here, so no
     * new interrupt can even be generated while the walk runs, which is the
     * cheapest available narrowing of the one window the fix does not close (a
     * tail the xHC writes after the ring read empty). It is not a closure and is
     * not described as one - see `XhciXferDrainSettled`.
     */
    if (bounded && !XhciEventRingPending(&ext->EventRing)) {
        ringEmpty = 1;
        ext->DrainBoundEmptyHits++;
    }
    if (ringEmpty) {
        XhciSlotDrainSettled(ext);
    }

    /*
     * The final write, and it is unconditional - including on a pass that found
     * nothing. EHB is what suppresses this interrupter, and the only thing that
     * clears it is this write; a DPC that skipped it because the ring was
     * already empty would silence the controller for good.
     *
     * On the bounded exit the ring is *usually* not empty - the peek above is
     * what distinguishes the case where it is - and clearing EHB is right
     * either way. The interrupter's internal IPE flag "shall be cleared to '0'
     * ... if the Event Ring transitions to empty" and is set by every event
     * inserted, and "when IMODC transitions to '0': if EHB = '0' and IPE = '1',
     * then IP shall be set to '1'" (4.17.5, p.270). So a non-empty ring with
     * EHB clear re-asserts by itself at the next moderation interval, and the
     * remaining events are collected by the DPC that follows. Leaving EHB set
     * instead would strand them permanently.
     */
    xhciPublishErdp(ext, 1);

    if (bounded) {
        ext->DrainBoundHits++;
        XHCI_DBG_VALUE_CHANGED("DPC: stopped at the drain bound after events",
                               drained);
    }

    /*
     * Re-arm last, after the ring is drained and EHB released, so the
     * interrupter cannot fire into a pass still in progress. Nothing is lost by
     * being last: IE and IP are independent, and "when this bit and the IP bit
     * are set, the Interrupter shall generate an interrupt when the Interrupter
     * Moderation Counter reaches '0'" (5.5.2.1, p.391) - an IP raised while IE
     * was clear is honoured as soon as IE is set.
     *
     * IP is written as 0 here: it is RW1C, so a 0 preserves an interrupt that
     * arrived since the ISR ran instead of acknowledging one nothing has seen.
     *
     * **Two conditions, not one, and the second is the lock's half.** The
     * BOOLEAN is usbport's own interrupt-enabled state, but usbport sampled it
     * before queuing this DPC and holds `MiniportInterruptsSpinLock` here, which
     * does not exclude the `MiniportSpinLock` its `DisableInterrupts` runs
     * under. So a disable can have masked both enables on another CPU since,
     * and a re-arm on that stale BOOLEAN would put IMAN.IE back on a controller
     * usbport has just silenced - the one write that could resurrect it, since
     * the ISR no longer carries IE through. XHCI_EXT_FLAG_INTERRUPTS is the same
     * fact recorded by this driver under *this* lock, which the disable's mask
     * also holds, so reading it here orders the two: whichever acquires first,
     * the other sees the finished state (`docs/contributing/design/05-locking-model.md`).
     *
     * **And the write itself is proved, not attempted.** Because the ISR now
     * clears IE, this is the only thing that puts interrupt delivery back on a
     * running controller, and usbport does not call EnableInterrupts again - so
     * a swallowed write or one transient all-ones read is permanent silence.
     * XhciRearmInterrupter applies the operand, read-back and bounded-retry
     * rules the mask and unmask paths already carry; a failure escalates below
     * with the lock dropped, exactly as a refused unmask does. It used to be a
     * bare read-modify-write here, whose all-ones read looked like "already
     * armed" - the worst available reading of that value.
     */
    if (enableInterrupts && (ext->Flags & XHCI_EXT_FLAG_INTERRUPTS) != 0) {
        if (!XhciRearmInterrupter(ext)) {
            ext->RearmEscalations++;
            resetRequested = 1;
        }
    }

    XhciControllerLockRelease(oldIrql);

    /*
     * The root hub's two deferred actions, and they are here because both are
     * usbport services: announcing a latched port change with
     * UsbPortInvalidateRootHub, and arming the timer a device-initiated resume
     * decided it needed inside the drain above (Phase 5 tasks 4 and 5). The
     * announcement in particular *cannot* be made above - the service calls
     * RH_DisableIrq straight back into this miniport, which takes the lock this
     * line has just released, and a KSPIN_LOCK is not recursive.
     *
     * Before the reset request rather than after it: a drain that both saw a
     * port change and decided the controller must be reset should still tell
     * usbport about the port, since the reset is containment rather than repair
     * and this is the last chance to say what was seen.
     */
    XhciRootHubDeferredWork(ext);
    /*
     * And the device layer's, for the same reason and with a longer list: the
     * transfers this drain retired owe UsbPortCompleteTransfer, an endpoint
     * whose command chain just completed owes UsbPortInvalidateEndpoint, and the
     * next command of a chain owes a submission - all three are things that must
     * not happen inside the lock this line has released.
     */
    XhciSlotDeferredWork(ext);

    if (resetRequested) {
        XhciRequestControllerReset(ext);
    }
}

/* ------------------------------------------------------------------ */
/* usbport's interrupt callbacks                                       */
/* ------------------------------------------------------------------ */

/*
 * EnableInterrupts - usbport calls this immediately after StartController
 * returns success, and again after it restarts a controller whose resume
 * failed (ReactOS pnp.c:878, power.c:212). It is the moment this driver's
 * interrupts are allowed to reach the CPU for the first time.
 *
 * **It acknowledges nothing, and that is the finding of this task.** The
 * roadmap's wording for it was "acknowledges stale owned status, enables
 * IR[0].IMAN.IE, then sets USBCMD.INTE", and the first half of that would
 * deadlock the controller on the most ordinary machine there is - one with a
 * device plugged in at boot.
 *
 * Follow the state. StartController powers the ports while the controller is
 * already running, and a port with a device attached asserts PSCEG as HCH
 * transitions to '0', "generating a respective Port Status Change Event"
 * (4.19.4, p.296). So by the time this callback arrives there are usually
 * events on the ring already, and the interrupter has reacted to them with
 * interrupts still masked: EHB "shall be set to '1' when the IP bit is set to
 * '1'" (5.5.2.3.3, p.394), so IP = 1, EHB = 1, and USBSTS.EINT = 1.
 *
 * Now acknowledge that pair here, as the roadmap asks. IP goes to 0 with EHB
 * still set - and IP can only be set again from EHB = 0: "When IMODC
 * transitions to '0': if EHB = '0' and IPE = '1', then IP shall be set to '1'"
 * (4.17.5, p.270). EHB is cleared by exactly one thing in this driver, the
 * DPC's final ERDP write, and the DPC only ever runs off an ISR that returned
 * TRUE. No interrupt, no DPC; no DPC, no EHB; no EHB, no interrupt. The events
 * that were already on the ring stay there for the life of the driver and the
 * device never enumerates.
 *
 * Acknowledging only *half* the pair is worse rather than better. EINT is set
 * on IP '0' to '1' transitions, so clearing EINT while IP stays set means the
 * next assertion produces no new transition, the ISR reads EINT = 0, declines
 * its own controller's interrupt - and the INTx line "remains asserted until
 * the device driver clears the Interrupt Pending (IP) flag" (4.17.3, p.268).
 * That is a live-lock of the whole IRQ, not just of this device.
 *
 * What the callback does instead:
 *
 *   1. Release Event Handler Busy at the *unmoved* dequeue pointer. This
 *      consumes nothing - it re-publishes where software already was - and it
 *      is what lets a pending IP be delivered rather than suppressed. It runs
 *      first, while IE and INTE are both still clear, so no interrupt and
 *      therefore no DPC can be in flight to race the write.
 *   2. IMAN.IE, then USBCMD.INTE (XhciUnmaskInterrupts), with IP written as 0
 *      throughout because it is RW1C.
 *
 * Any interrupt the controller was already holding is then delivered normally:
 * "When an Interrupter is enabled it begins looking for two conditions: 1)
 * Interrupt Pending Enable (IPE = '1') and 2) the Event Handler not busy
 * (EHB = '0')" (4.17.2, p.265), and the signalling conditions are "this counter
 * is '0', the Event Ring is not empty, the IE and IP flags = '1', and EHB =
 * '0'" (5.5.2.2, p.392) - every one of which this ordering leaves true.
 *
 * The two enables are in the opposite order to the specification's own
 * initialization list, which sets INTE before IE (4.2, p.70). Neither order can
 * deliver an interrupt until both are set, so the list is a sequence rather
 * than a rule; this driver uses the order that is the exact mirror of the mask
 * (docs/contributing/implementation-invariants.md, "Interrupt Ordering"), because the mask's
 * order *is* load-bearing and one reversible pair is easier to keep right than
 * two unrelated ones.
 *
 * IRQL: DISPATCH_LEVEL (under usbport's MiniportSpinLock, unless the miniport
 * sets NOT_LOCK_INT - this one does not).
 */
VOID XhciEnableInterrupts(PXHCI_EXTENSION ext)
{
    ULONG erdp;
    ULONG unmasked;
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    /*
     * usbport can enable interrupts after failure or while lifecycle quiesce is
     * closing the controller. Decide under the lock shared with both transitions;
     * putting IMAN.IE and USBCMD.INTE back after either mask would hand a dead or
     * D3-bound controller a live interrupt line. The INTERRUPTS bookkeeping RMW
     * belongs under the same lock: even a refused enable remains usbport's
     * requested state for resume, but it cannot resurrect INITIALIZED from a
     * value read before quiesce cleared it.
     */
    XhciControllerLockAcquire(&oldIrql);
    ext->Flags |= XHCI_EXT_FLAG_INTERRUPTS;
    if (ext->ControllerFailed ||
        (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0) {
        XhciControllerLockRelease(oldIrql);
        return;
    }
    /*
     * A refused unmask is escalated, not absorbed, and the escalation happens
     * below with the lock dropped - XhciRequestControllerReset is a usbport
     * service, and none may be called while this lock is held.
     */

    /*
     * Read only to record it. The release below is unconditional, for the same
     * reason the DPC's final ERDP write is: a write that clears an already
     * clear RW1C bit costs one register access, and the failure mode of
     * skipping it is a controller that never interrupts again.
     */
    erdp = XhciReadIr0(ext, XHCI_IR_ERDP);
    if (erdp != 0xFFFFFFFFUL && (erdp & XHCI_ERDP_EHB) != 0) {
        ext->EnablesWithEventsPending++;
        XHCI_DBG_VALUE_CHANGED("EnableInterrupts: the controller was already "
                               "holding events, pending",
                               XhciEventRingPending(&ext->EventRing));
    }
    xhciPublishErdp(ext, 1);

    unmasked = XhciUnmaskInterrupts(ext);
    XhciControllerLockRelease(oldIrql);

    /*
     * The controller was started and usbport believes interrupts are on, but
     * both enables are still down and nothing else will come back to set them:
     * this callback returns void and usbport does not repeat it after a
     * successful resume.
     *
     * **What this buys is containment, not repair.** The invalidation queues a
     * DPC that calls this miniport's own ResetController at DISPATCH inside a
     * usbport lock, after which usbport does nothing
     * (docs/usb-xhci-info/usbport-miniport-abi.md), and that callback marks
     * the controller terminally failed. So the trade is a silently
     * non-interrupting controller for a visibly failed one with its command
     * engine stopped - worth making, because the silent form is
     * indistinguishable from working hardware until a transfer hangs.
     * Restoring service needs a stop/start, which no miniport can initiate.
     */
    if (!unmasked) {
        ext->UnmaskEscalations++;
        XHCI_DBG_TEXT("EnableInterrupts: enables could not be restored - "
                      "requesting a controller reset");
        XhciRequestControllerReset(ext);
    }
}

/*
 * DisableInterrupts - the mask, plus the bookkeeping usbport's state implies.
 *
 * A body rather than three lines inside the callback wrapper, and the reason is
 * coverage rather than symmetry: `XhciMaskInterrupts` normally writes two
 * registers, so everything that decides *whether* to call it - the
 * signature check and the XHCI_EXT_FLAG_INITIALIZED gate - is the part worth
 * testing, and while it lived in src/xhci_dispatch.c the host suite could not
 * reach it. A disable that masked after a suspend would be writing MMIO to a
 * controller that may be in D3, and Win98 drives this callback on every
 * shutdown.
 *
 * Nothing is acknowledged: IP and EINT survive, so an interrupt the controller
 * is already holding is delivered when usbport enables again (XhciMaskInterrupts
 * writes IP as 0 for exactly that reason).
 *
 * IRQL: DISPATCH_LEVEL, under usbport's MiniportSpinLock.
 */
VOID XhciDisableInterrupts(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG suppressed;
    ULONG everRequested;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    ext->InterruptDisables++;
    XhciControllerLockAcquire(&oldIrql);
    /*
     * Sampled *before* the clear below, and it says **usbport ever asked this
     * driver for interrupts during this start** - not that any hardware enable
     * was ever raised. `XhciEnableInterrupts` sets this bit under the lock
     * ahead of its `ControllerFailed`/`INITIALIZED` gates, deliberately, so a
     * refused enable still records usbport's requested state for resume.
     *
     * That request is nevertheless the right predicate for the escalation
     * below, and a stronger one than "the enables actually went up" would be.
     * What this callback is closing is usbport's ISR wrapper: it calls
     * `InterruptService` only while its own interrupt-enabled flags are set,
     * and this is what clears them. If usbport never asked, there was never a
     * wrapper calling us and there is nothing whose loss can strand an asserted
     * line - and on a zeroed extension `InterruptDeliverySuppressed` reads 0
     * for want of a mask rather than for want of a proof, so consulting it
     * there would escalate over a start that touched nothing.
     *
     * (The comment here read "whether this driver ever put the enables up",
     * which overstated what the bit records; corrected by the second-reader
     * second-reader review. The behaviour was already the intended one.)
     */
    everRequested = (ext->Flags & XHCI_EXT_FLAG_INTERRUPTS) != 0;
    ext->Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
    if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0) {
        XhciMaskInterrupts(ext);
    }
    /*
     * Read outside the INITIALIZED branch, because the branch that does *not*
     * mask is the ordinary one: the measured Win98 shutdown is
     * Suspend -> DisableInterrupts -> Stop, and the suspend already cleared
     * INITIALIZED. Reporting success unconditionally there threw away the
     * suspend-time mask's verdict, so a mask that had exhausted its retries
     * reached this point looking like a proven suppression - the one state
     * whose escalation this function exists to make. The field is the record of
     * the last mask attempt whichever path made it, and it is read under the
     * lock either way.
     */
    suppressed = !everRequested || ext->InterruptDeliverySuppressed;
    XhciControllerLockRelease(oldIrql);

    /*
     * **This is the one mask whose failure the ISR cannot cover for.**
     * Everywhere else, an unproven mask leaves XhciIsr admitted precisely so it
     * can still acknowledge an asserted line. Here usbport is about to stop
     * calling it: its ISR wrapper "only calls [InterruptService] while its
     * interrupt-enabled flags are set" (docs/usb-xhci-info/usbport-miniport-abi.md), and this
     * callback is what clears them. So if INTE and IE are still up, a
     * level-triggered INTx this controller is asserting can no longer reach any
     * acknowledgement at all - the shared-line livelock, with the one escape
     * hatch closed by the caller rather than by this driver.
     *
     * The retries inside the mask are what can actually fix that, and they run
     * before this point. The escalation below cannot: it ends at this
     * miniport's own ResetController, which marks the controller failed
     * (docs/contributing/implementation-invariants.md, "Interrupt Ordering"). It is here so
     * the state is recorded rather than silent, which on this path is the whole
     * of what a miniport can still do.
     */
    if (!suppressed) {
        ext->MaskEscalations++;
        XHCI_DBG_TEXT("DisableInterrupts: delivery is not proven suppressed and "
                      "usbport is about to stop calling the ISR");
        XhciRequestControllerReset(ext);
    }
}

/*
 * FlushInterrupts - and this one deliberately touches no register at all.
 *
 * **Its call site is known**, contrary to the ReactOS mirror, which has none.
 * All three shipping builds call it from their device-power completion routine
 * (`PREQUEST_POWER_COMPLETE`, `ret 14h`), on the successful `PowerDeviceD0`
 * path, with the single argument `FdoExt->MiniPortExt`, immediately before the
 * `TakePortControl` slot and before resume processing. Extracts are in
 * `tools/{nusb,win2ksp4,winxpsp3}-extracted/usbport-flushinterrupts-disasm.txt`
 * and the derivation is in docs/usb-xhci-info/usbport-miniport-abi.md.
 *
 * Two facts from that disassembly decide the whole implementation:
 *
 *   **The routine acquires no spin lock.** Neither MiniportSpinLock nor
 *   MiniportInterruptsSpinLock is held, so on SMP this callback can run
 *   concurrently with XhciEventDpc on another CPU.
 *
 *   **It runs before the resume**, so on the ordinary path the miniport is
 *   still suspended: XhciSuspendController has already halted the controller,
 *   masked both enables and dropped XHCI_EXT_FLAG_INITIALIZED.
 *
 * The natural implementation - acknowledge USBSTS.EINT and IMAN.IP - is not
 * available, and this task's own finding is why. Acknowledging that pair
 * without also releasing Event Handler Busy strands every event on the ring
 * permanently (4.17.5, p.270), so the acknowledgement and the ERDP write are
 * inseparable. But ERDP is the *drain's* register: XhciEventDpc owns the
 * software dequeue pointer, publishes it as it goes, and relies on its
 * intermediate writes carrying EHB = 0 so that nothing un-suppresses the
 * interrupter mid-pass. An unsynchronized writer would publish a dequeue
 * pointer the DPC has already moved past and clear EHB in the middle of a
 * drain.
 *
 * **The lock that would prevent that now exists, and this callback still does
 * not act - the reason has changed, and is recorded rather than left standing.**
 * Task 7's controller lock covers the whole bounded drain including every ERDP
 * publication, so a second writer taking it could not land mid-drain and would
 * read a consistent software dequeue pointer; task 9's rule is exactly that
 * (docs/contributing/design/05-locking-model.md, "Every ERDP writer is
 * serialized" - the section was titled "ERDP has one writer" until audit round 7
 * found three under the heading). What remains
 * true is that there is nothing here to do - at the site the disassembly found,
 * the pending state an acknowledgement would clear is cleared moments later by
 * the resume's HCRST, as the paragraph below sets out. A write with no effect
 * does not earn a second writer on the driver's most delivery-critical
 * register; but a later phase that needs one now has a mechanism rather than a
 * prohibition.
 *
 * So: acknowledge nothing, publish nothing, count the call. Nothing is lost by
 * that at the site the disassembly found, but the reason is not that there is
 * nothing pending - XhciMaskInterrupts writes IP as 0 precisely so that a
 * pending interrupt *survives* a disable, and both IP and EINT do survive the
 * suspend. What the mask takes away is delivery: IE = 0 leaves the Interrupter
 * "prohibited from generating interrupts" (5.5.2.1, p.391) and INTE = 0 is the
 * means of "enabling or disabling the host system interrupts generated by
 * Interrupters" (5.4.1, p.360), so the INTx line is not asserted however long
 * IP stays set. The
 * pending state is then cleared not by anything a flush could do but by the
 * resume, which reinitializes through HCRST: "after initial power-on or HCRST
 * ... all of the Operational and Runtime Registers shall be at their default
 * values" (4.23.1, p.312), and the defaults of IMAN and ERDP put IP, IE and EHB
 * at 0 while USBSTS's puts EINT there. That happens before the enables go back
 * on, so the interval in which a flush could have contributed anything is one
 * where nothing can be delivered anyway.
 *
 * If a later phase does need this callback to act, the fix is to serialize ERDP
 * against the DPC under the controller lock - not to mask interrupts around it,
 * which does not exclude a DPC already running on another CPU. Note what that
 * costs: this is the one callback usbport calls holding neither of its own
 * locks, so it would be acquiring the controller lock from an arbitrary thread
 * at <= DISPATCH_LEVEL, which is legal but makes it the only such caller.
 *
 * IRQL: <= DISPATCH_LEVEL, arbitrary thread, no usbport lock held.
 */
VOID XhciFlushInterrupts(PXHCI_EXTENSION ext)
{
    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    /*
     * The whole body. It is counted rather than ignored because the count is
     * the runtime confirmation of the disassembly above: a release build
     * that reaches D0 and shows InterruptFlushes still at zero would mean the
     * call site was read wrong.
     */
    ext->InterruptFlushes++;
}
