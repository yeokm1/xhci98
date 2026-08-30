/*
 * xhci_cmd.c - the asynchronous command engine.
 *
 * Roadmap Phase 4 task 7, and it lands before any slot code depends on it for
 * the reason the task is worded that way: every command this driver will ever
 * issue - Enable Slot, Address Device, Configure Endpoint, Reset Endpoint, Set
 * TR Dequeue Pointer - goes through this one path, and the parts that are easy
 * to get wrong (matching a completion, timing a command that cannot be
 * cancelled, recovering a stuck ring) are the same for all of them.
 *
 * Three properties decide the whole design, and none of them is a preference:
 *
 *   **Nothing may wait.** "Software shall be responsible for all command
 *   timeouts" (4.6, p.91), and the usbport callbacks that need commands run
 *   at DISPATCH_LEVEL under usbport's locks. So a command is issue-and-return:
 *   put the TRB on the ring, ring DB[0], arm a timer, leave. The Command
 *   Completion Event is serviced by the DPC.
 *
 *   **One command outstanding at a time.** The xHC "shall generate a Command
 *   Completion Event for every command", whose "Command TRB Pointer field ...
 *   shall point to the Command TRB that initiated the event" (4.6.1, p.92), so
 *   matching is a single comparison against the address the submit handed back.
 *   Serializing is also what makes command-ring-full unreachable on a 64-TRB
 *   ring, so the ring's own full path is a refusal that should never be taken.
 *
 *   **The timer cannot be cancelled.** UsbPortRequestAsyncCallback is the only
 *   deferred-work tool Option A sanctions (no private DPCs, no work items - see
 *   the deny list in scripts/import-gate/xhci98-imports.allow), it copies its
 *   context, and it exposes no cancellation (docs/usb-xhci-info/usbport-miniport-abi.md
 *   section 6). A stale callback is therefore normal input, not an error: every
 *   one carries the generation it was armed with, and claims the outstanding
 *   command only if that generation is still current. Generations are monotonic
 *   and never reused, so the comparison cannot alias.
 *
 * The interior lock is the fourth. Submit, completion and timeout run under
 * three different (or no) usbport locks, so this driver needs its own - and it
 * is the **driver's**, not the extension's, because usbport zeroes the extension
 * before every start and a lock re-created underneath an uncancellable callback
 * is a race no check can close. The same lock covers the failure transition and
 * every DISPATCH-level path that can issue controller MMIO, so failed is a
 * linearized state rather than a check followed by a race. It is innermost: no
 * usbport service is called while it is held, which is why timer arms and
 * controller-reset requests happen after it is dropped.
 *
 * And the ladder has an end. When it is reached the controller is *failed*, not
 * pending rescue: `ResetController` cannot reinitialize anything, because
 * usbport calls it at DISPATCH_LEVEL inside one of its own spin locks. Every
 * path here checks `ControllerFailed` for that reason - recording the state
 * without enforcing it would let an idle engine issue a fresh command onto a
 * controller nobody is going to fix.
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
/* For `XhciXferCodeInfo`: Table 6-90's fatal and slot-fatal instructions are
 * scoped to "any command or transfer", so the command engine reads the same
 * transcription the transfer paths and the restore's drain read (audit round
 * 9). */
#include "xhci_xfer.h"
#include "xhci_dbg.h"

/* Forward: armed by the submit, and by the timeout for its own second rung. */
static VOID NTAPI xhciCommandTimeout(PVOID miniPortExtension, PVOID context);

/*
 * **usbport zeroes the miniport extension before every StartController**, and
 * that fact is what this counter exists for.
 *
 * The first version of this engine assumed the opposite - that the extension
 * survives a stop/start pair - and advanced the generation across a start on
 * that basis. The mirror says otherwise in two places: USBPORT_StartDevice does
 * `RtlZeroMemory(FdoExtension->MiniPortExt, Packet->MiniPortExtensionSize)`
 * immediately before calling StartController (pnp.c), and the failed-resume
 * restart path zeroes both the extension and the common buffer before restarting
 * (power.c). So every start begins with CommandGeneration back at 0, and the
 * first command of a new start is handed the same generation as the first
 * command of the previous one - which an uncancelled watchdog from that previous
 * start will match, and abort a command that has been running for microseconds.
 *
 * Nothing usbport zeroes can carry the distinction, so it is kept here, in the
 * driver's own image. Every StartController takes the next value; the extension
 * records which one it got, and every timer context carries it. A callback from
 * an earlier start compares unequal and returns.
 *
 * Shared by every controller this driver serves, which is correct: the value is
 * a unique token rather than a per-controller count, and each extension holds
 * its own. Zero is never handed out, so a zeroed extension - one usbport has
 * just cleared and not yet started - matches no live context either.
 *
 * **Allocated with InterlockedIncrement**, because "shared by every controller"
 * means two of them can be started concurrently: PnP starts each device
 * independently, and nothing in the miniport ABI serializes StartController
 * across controllers. A plain `++` there can hand the same token to both, which
 * is the one thing this counter must never do. LONG rather than ULONG because
 * that is what the interlocked primitive takes; the value is only ever compared
 * for equality, so its signedness is immaterial.
 */
static LONG xhciStartEpoch;

/*
 * **The controller-state lock lives in the driver image, not in the miniport
 * extension**, and that is the second thing usbport's zeroing decides.
 *
 * The first version put a KSPIN_LOCK in the extension and re-created it on every
 * StartController. Checking an epoch before taking it narrows the window but
 * does not close it: a callback can pass the checks, be preempted, and resume
 * inside KeAcquireSpinLock on a lock word that usbport has since zeroed and a
 * restart has since re-initialized. A check is not synchronization.
 *
 * A lock that is created once, in DriverEntry, and never re-created has no such
 * window - there is nothing for a restart to do to it. Every field the engine
 * touches is then read *under* it, epoch and signatures included, so a stale
 * callback is excluded rather than merely detected.
 *
 * One lock for every controller this driver serves. That is a real
 * serialization, and it is the right trade: a machine has one or two xHCI
 * controllers, the engine allows one command at a time, and the longest
 * critical section is one bounded event-ring drain with no waiting in it.
 * Buying a per-controller lock back would mean re-creating one somewhere, which
 * is the hazard this exists to remove.
 *
 * IRQL: DISPATCH_LEVEL while held. Still innermost - no usbport service is
 * called under it.
 */
static KSPIN_LOCK xhciControllerLock;

/* IRQL: PASSIVE_LEVEL (DriverEntry only). */
VOID XhciControllerGlobalInit(VOID)
{
    KeInitializeSpinLock(&xhciControllerLock);
}

/*
 * Functions rather than an exposed lock word: every user has one spelling for
 * the lock order, and the host model sees every acquire and release through the
 * same hooks. IRQL: <= DISPATCH_LEVEL on entry, DISPATCH_LEVEL while held.
 */
VOID XhciControllerLockAcquire(PKIRQL oldIrql)
{
    KeAcquireSpinLock(&xhciControllerLock, oldIrql);
}

VOID XhciControllerLockRelease(KIRQL oldIrql)
{
    KeReleaseSpinLock(&xhciControllerLock, oldIrql);
}

/* IRQL: <= DISPATCH_LEVEL. */
ULONG XhciControllerUpdateFlags(PXHCI_EXTENSION ext,
                                ULONG clearMask,
                                ULONG setMask)
{
    KIRQL oldIrql;
    ULONG previous;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }

    XhciControllerLockAcquire(&oldIrql);
    previous = ext->Flags;
    ext->Flags = (previous & ~clearMask) | setMask;
    XhciControllerLockRelease(oldIrql);
    return previous;
}

/*
 * Task 11-V.7's log ring, on exactly the terms XhciControllerUpdateFlags above
 * uses: the ring is shared extension state, so every mutation of it is under
 * the one controller lock, and there are two spellings because a caller already
 * inside the lock cannot use the acquiring one.
 *
 * They live in this file rather than beside src/xhci_log.c because this is the
 * lock's home. src/xhci_log.c is pure core and must stay reachable from the
 * host suite with no lock, no DDK and no IRQL (design doc 03 section 2).
 *
 * See the contracts in src/xhci_hw.h.
 */
VOID XhciLogNoteLocked(PXHCI_EXTENSION ext, const char *label, ULONG value)
{
    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    XhciLogAppend(&ext->Log, label, value, 1);
}

VOID XhciLogNote(PXHCI_EXTENSION ext, const char *label, ULONG value)
{
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    XhciLogAppend(&ext->Log, label, value, 1);
    XhciControllerLockRelease(oldIrql);
}

VOID XhciLogNoteAddress(PXHCI_EXTENSION ext, const char *label, ULONG value)
{
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    XhciLogAppendAddress(&ext->Log, label, value);
    XhciControllerLockRelease(oldIrql);
}

/*
 * What a path that runs under the controller lock decided its caller must do
 * once the lock is dropped. Deciding under the lock and acting outside it is not
 * tidiness: both actions are usbport services, and calling one while holding
 * this driver's lock would nest two lock hierarchies with no stated order
 * between them.
 */
#define XHCI_CMD_ACTION_NONE    0
#define XHCI_CMD_ACTION_ARM     1   /* CA is written; watch for CRR to clear  */
#define XHCI_CMD_ACTION_RESET   2   /* the ring cannot be recovered here      */

/* ------------------------------------------------------------------ */
/* Arming the watchdog                                                 */
/* ------------------------------------------------------------------ */

/*
 * Always called with the controller lock **released**.
 * UsbPortRequestAsyncCallback is usbport's, and it takes usbport's own timer
 * lock; calling it under this driver's lock would nest two lock hierarchies
 * with no stated order between
 * them, which is the shape of a deadlock rather than of a race.
 *
 * Nothing here fails the operation that armed it. A command that could not be
 * timed is worse off than one that could, but it is still a command that was
 * correctly issued, and the count is the record - there is no second timer
 * service to fall back to.
 *
 * IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciArmCommandTimer(PXHCI_EXTENSION ext,
                                ULONG milliseconds,
                                const XHCI_COMMAND_TIMEOUT *what)
{
    XHCI_COMMAND_TIMEOUT context;

    /*
     * The whole context is handed in by value, captured by the caller **under
     * controller lock**. Reading ext->StartEpoch here instead - which an earlier
     * version did - takes it after the lock has been dropped, so a restart
     * landing in that window would stamp the *new* epoch onto a command
     * generation belonging to the old one, and produce exactly the stale
     * callback the epoch exists to reject.
     *
     * **The host suite cannot see this one**, and it is labelled rather than
     * left looking covered: the difference is only observable when something
     * changes StartEpoch between the capture and this call, which needs a second
     * thread. Re-reading the epoch here fails zero checks (measured). It is a
     * review property, like the stale-ERDP half of task 6's flush.
     */
    context = *what;

    /*
     * The return value is deliberately discarded, and that is a statement about
     * the service rather than laziness: USBPORT_RequestAsyncCallback returns 0
     * on success **and** 0 when its pool allocation fails (usbport.c). There is
     * no value it could return that this driver could act on. What is checkable
     * - that the service exists at all - is checked by xhciCanArmTimer before
     * anything is enqueued, so reaching here with a NULL pointer is a bug in
     * this file rather than a condition.
     */
    if (XhciRegPacket.UsbPortRequestAsyncCallback == NULL) {
        ext->CommandTimerFailures++;
        XHCI_DBG_VALUE_CHANGED("command: no async timer service, generation",
                               context.Generation);
        return;
    }

    (VOID)XhciRegPacket.UsbPortRequestAsyncCallback(
        ext, milliseconds, &context, sizeof(context), xhciCommandTimeout);
}

/*
 * Can a command be timed at all? The only answerable half of that question:
 * whether usbport gave this driver the service. A pool failure *inside* the
 * service is invisible - it returns 0 either way - so "every command carries a
 * timeout" is an invariant this driver can honour but not verify, and the
 * residual belongs to CheckController noticing a command that has been pending
 * across many polls (roadmap Phase 4 task 8).
 *
 * Checked before the TRB is enqueued rather than after the doorbell, because the
 * two answers are very different: a refusal before the enqueue is a command that
 * was never issued, while a failure after it is a command that will sit on the
 * ring for the life of the driver with nothing to notice it.
 *
 * IRQL: any.
 */
static ULONG xhciCanArmTimer(VOID)
{
    return XhciAsyncTimerAvailable();
}

/* See the contract in src/xhci_hw.h. Shared with the root hub's asynchronous
 * port operations, which owe the same check before the same kind of write.
 * IRQL: any. */
ULONG XhciAsyncTimerAvailable(VOID)
{
    return (XhciRegPacket.UsbPortRequestAsyncCallback != NULL) ? 1UL : 0UL;
}

/*
 * Ask usbport to declare this controller broken. The HCRST the specification
 * calls for is not this driver's to perform under Option A, and - as the
 * ResetController callback's own note records - it is not something that
 * callback can perform either, because usbport dispatches it at DISPATCH_LEVEL
 * inside one of its own spin locks. What the request buys is the *transition*:
 * the miniport learns the ladder is over, masks its interrupt enables, and marks
 * the controller failed. Recovery is a stop/start.
 *
 * Always called with the controller lock **released**, for the same reason the
 * timer arm is. IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciRequestControllerReset(PXHCI_EXTENSION ext)
{
    if (XhciRegPacket.UsbPortInvalidateController == NULL) {
        return;
    }
    (VOID)XhciRegPacket.UsbPortInvalidateController(
        ext, USBPORT_INVALIDATE_CONTROLLER_RESET);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/*
 * The one place an outstanding command stops being outstanding. Every path that
 * ends one - normal completion, Command Aborted, Command Ring Stopped, the
 * abandon, and the engine's own (re)initialisation - goes through here, because
 * the traced build's self-test witness is armed for as long as its command is
 * outstanding and a path that cleared only the address would leave the token
 * set. A later command reusing that ring position would then consume it under
 * the self-test's name, which is the failure this token exists to prevent.
 * Adding the clear at each site as it is noticed is how the two abort paths
 * were missed once already.
 *
 * Callers with a diagnosis to record set LastCommandTrbPA and
 * CommandCompletionCode themselves, before calling: this says only that nothing
 * is outstanding any more.
 *
 * Called with the controller lock held (or before the engine is reachable, from
 * XhciCommandInit). IRQL: DISPATCH_LEVEL.
 */
static VOID xhciCommandEndOutstanding(PXHCI_EXTENSION ext)
{
    ext->CommandTrbPA = 0;
    ext->NoOpWitnessArmed = 0;
}

/*
 * Does this completion belong to the self-test, and is it the first time that
 * has been asked for this issuance? The decision is deliberately *not* inside
 * the trace macro: the macros are empty in a release build, so a policy
 * written there is untestable by construction - which is how three wrong
 * bounds in a row (an address on a reused ring, a per-start counter that
 * missed the resume arm) got as far as they did. Here it is ordinary code in
 * both flavours, the count is a release-build reading of the same checkpoint
 * fact, and the host suite can pin "once per issuance, start and resume
 * alike".
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
static BOOLEAN xhciCommandWitnessSelfTest(PXHCI_EXTENSION ext, ULONG pointer)
{
    if (ext->NoOpWitnessArmed == 0 || pointer != ext->NoOpTrbPA) {
        return FALSE;
    }

    ext->NoOpWitnessArmed = 0;
    ext->NoOpWitnessFired++;
    return TRUE;
}

/* IRQL: PASSIVE_LEVEL. See the contract in src/xhci_hw.h. */
VOID XhciCommandInit(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    LONG epoch;

    if (ext == NULL) {
        return;
    }

    /*
     * Under the driver's lock, which exists already and is never re-created -
     * so publishing this start's epoch *excludes* the stale callbacks of the
     * previous one rather than merely being detectable by them. A callback that
     * has not yet acquired the lock will read the new epoch when it does; one
     * that holds it finishes first, against the state it validated.
     */
    XhciControllerLockAcquire(&oldIrql);

    ext->CommandState = XHCI_CMD_STATE_IDLE;
    xhciCommandEndOutstanding(ext);
    ext->CommandType = 0;
    ext->CommandGeneration = 0;
    ext->ControllerFailed = 0;

    /*
     * The epoch, taken from the one counter usbport cannot zero. Everything else
     * in this extension - the generation included - has just been cleared by
     * usbport, so this is the only field that can tell a callback armed by the
     * previous start from one armed by this one. Zero is skipped so that a
     * zeroed-but-not-yet-started extension matches no context either.
     *
     * Published under the driver's lock, which is what makes it exclusion rather
     * than detection: every callback reads it holding the same lock, so there is
     * no interval in which one can have read the old value and not yet acted.
     *
     * The allocation itself is interlocked because the counter is shared with
     * every other controller this driver serves, and PnP can start two of them
     * at once - the lock protects this extension's fields, not that counter.
     */
    epoch = InterlockedIncrement(&xhciStartEpoch);
    if (epoch == 0) {
        /* Wrapped. Only reachable after 2^32 starts, and skipping the one value
         * a zeroed extension holds costs a branch. */
        epoch = InterlockedIncrement(&xhciStartEpoch);
    }
    ext->StartEpoch = (ULONG)epoch;

    XhciControllerLockRelease(oldIrql);
}

/* Called with the controller lock held. IRQL: DISPATCH_LEVEL. */
static VOID xhciCommandInvalidateLocked(PXHCI_EXTENSION ext)
{
    if (ext->CommandState != XHCI_CMD_STATE_IDLE) {
        ext->CommandsAbandoned++;
        XHCI_DBG_VALUE_CHANGED("command: abandoned outstanding TRB",
                               ext->CommandTrbPA);
        /* Task 11-V.9's third tier. Change-gated by the test above: an engine
         * already idle abandons nothing, so this fires once per abandonment
         * rather than once per invalidation. The owning device is in the record
         * because a command abandoned mid-enumeration is what a device that
         * never appears looks like from the controller's side. */
        XhciLogNoteLocked(ext, "cmd.abandoned",
                          (ext->CommandOwnerOp << 16) | ext->CommandOwner);
    }
    ext->CommandState = XHCI_CMD_STATE_IDLE;
    xhciCommandEndOutstanding(ext);
    /* Unconditional, and it has to be: the owner is recorded *before* the
     * submit, so a device can own the engine while CommandState is still IDLE -
     * exactly the window a quiesce arriving mid-pump lands in. */
    XhciSlotCommandLost(ext);
    ext->CommandGeneration++;
}

/* IRQL: <= DISPATCH_LEVEL. No wait is performed while the lock is held. */
VOID XhciControllerBeginQuiesce(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    xhciCommandInvalidateLocked(ext);
    /*
     * The port half of the same retirement (Phase 5 task 6). Every armed reset
     * and resume generation is advanced here, so a timer that fires after this
     * point - and every one of them will, since none can be cancelled - finds a
     * generation that has moved on and returns before touching PORTSC on a
     * controller that is halting or heading for D3. It is in this transition
     * rather than in the two lifecycle callbacks because *both* of them, and the
     * failed-start teardown, reach the quiesce and only the quiesce.
     */
    XhciRootHubRetireOperations(ext);
    /* A DIRQL ISR may decline only after this controller cannot still be the
     * source of the shared level-triggered interrupt. */
    XhciMaskInterrupts(ext);
    ext->Flags &= ~XHCI_EXT_FLAG_INITIALIZED;
    XhciControllerLockRelease(oldIrql);
}

/*
 * Task 13-R.3.5's sizing rule, checked rather than asserted in prose: the
 * backstop must sit beyond the watchdog ladder's own legitimate worst case, or
 * it is a second competing timeout instead of a detector of a ladder that never
 * ran. Both sides are now in the same unit, which is the whole point - the
 * poll-count form could not be checked at all, because one side was a count of
 * somebody else's timer ticks.
 */
XHCI_C_ASSERT(command_age_clears_the_watchdog_ladder,
              XHCI_COMMAND_AGE_MS > XHCI_COMMAND_TIMEOUT_MS +
                                        (XHCI_COMMAND_ABORT_WAITS + 1UL) *
                                            XHCI_COMMAND_ABORT_MS);

/* IRQL: DISPATCH_LEVEL. See the contract in src/xhci_hw.h. */
ULONG XhciControllerHealthPoll(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG usbsts;
    ULONG escalate;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }

    escalate = 0;
    XhciControllerLockAcquire(&oldIrql);

    /*
     * The same two questions the ISR asks, in the same order and for the same
     * reasons. Can a register be touched at all - HcInfo is what holds the
     * bases, and XhciInitController sets HcInfoStatus bad while it re-derives
     * them. Then: is this controller still admitted, which a quiesce clears
     * while HcInfo stays valid for the whole halt window.
     *
     * A failed controller is excluded outright. Its enables are already masked
     * and its engine already stopped; a second reset request would ask usbport
     * to queue a DPC into a callback that has nothing left to do.
     */
    if (ext->HcInfoStatus != XHCI_HC_OK || ext->ControllerFailed ||
        (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0) {
        XhciControllerLockRelease(oldIrql);
        return 0;
    }

    ext->HealthPolls++;
    /*
     * Task 9-A.1, review round 3. Resampling the frame axis here is what bounds
     * the gap between two readings of MFINDEX's eleven-bit Frame Index to the
     * poll's period, which is far under the 2,048-frame lap at which
     * `XhciFrameIdNow`'s reconstruction of the high bits would silently lose one.
     * Placed in the poll rather than left to usbport's own frame calls because
     * "usbport calls it often" is an observation about two binaries, not a bound.
     */
    XhciFrameSample(ext);
    /*
     * Task 13-R.3.5, and it has to be before every detector below: this is the
     * clock they are all measured on. Beside XhciFrameSample rather than inside
     * it because the two axes answer different questions and must not share an
     * admission gate - see XhciPollClockAdvance in src/xhci_init.c.
     */
    XhciPollClockAdvance(ext);
    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    ext->LastCheckStatus = usbsts;

    if (usbsts == 0xFFFFFFFFUL) {
        /*
         * Not a fatal-error report. All ones is a window that has stopped
         * decoding, and HCE and HSE are simply two of the thirty-two bits it is
         * answering with - the same operand rule the interrupt masks were taught
         * over five review rounds. Escalating here would request a reset on the
         * strength of a read that proves nothing, and the reset is containment
         * rather than repair: it would mark a possibly-healthy controller
         * terminally failed on the evidence of one bad config of the bus.
         */
        ext->HealthPollsDead++;
        XhciControllerLockRelease(oldIrql);
        return 0;
    }

    /*
     * HCE (RO) is "internal xHC error; the controller ceases all activity", HSE
     * (RW1C) is a system-level error after which "both the xHC and software must
     * assume system integrity is compromised" - docs/usb-xhci-info/xhci-programming.md,
     * "Fatal Controller Errors (HCE, HSE)". Either one is terminal for this
     * controller, so this is the poll docs/contributing/implementation-invariants.md, "Fatal
     * Errors" has required on every invocation since before the callback existed.
     *
     * Neither bit is acknowledged. HSE is RW1C and clearing it would destroy the
     * one durable record of why the controller was failed, on a path that has
     * already decided not to retry in place; HCE cannot be cleared by software
     * at all. The transition is what escalates - ControllerFatal latches - so a
     * bit that stays set for the life of the driver does not ask usbport to
     * queue a reset every 500 ms.
     */
    if ((usbsts & (XHCI_USBSTS_HCE | XHCI_USBSTS_HSE)) != 0) {
        if (!ext->ControllerFatal) {
            ext->ControllerFatal = 1;
            ext->FatalStatusDetected++;
            escalate = 1;
            XHCI_DBG_VALUE("check: FATAL controller status, USBSTS", usbsts);
        }
    }

    /*
     * The command that lost its watchdog.
     *
     * UsbPortRequestAsyncCallback "returns 0 on success **and** 0 when its pool
     * allocation fails" (src/xhci_cmd.c, xhciArmCommandTimer), so a command can
     * be enqueued, doorbelled, and left with nothing scheduled to time it. The
     * engine allows one outstanding command, so that state is not a slow command
     * - it is an engine that answers XHCI_CMD_BUSY to everything, for the life of
     * the driver, with no path that ever notices.
     *
     * This poll is the independent age measure that closes it, and **task
     * 13-R.3.5 is why it is measured in milliseconds on PollClockMs rather than
     * counted in polls**. The poll-count form argued its own error direction
     * safe - "usbport's timer is nominally 500 ms, so a slower poll only makes
     * this fire later, never sooner - and firing sooner is the only way it could
     * break a command that was going to complete" - and named only one of the
     * two directions. **The E460 polls this miniport at 36-80 ms**, so 64 polls
     * was 2.3-5.1 s rather than 32 s - at or under XHCI_COMMAND_TIMEOUT_MS, the
     * 5 s watchdog this was sized to sit 12 s behind. Firing sooner is exactly
     * what it did: `CommandTimeoutArrivals` 633 of `CommandsIssued` 635 with
     * `CommandsTimedOut` 0, because every watchdog arrived and found a
     * generation this detector had already moved on
     * (docs/contributing/runs/run-13e.md, Findings U and **V**; V is the one
     * that measured the period rather than inferring it).
     *
     * The threshold clears the whole watchdog ladder by construction: 5,000 ms
     * for the command, then XHCI_COMMAND_ABORT_WAITS + 1 intervals of 5,000 ms
     * for the abort, is 20 s of legitimate lateness against 32 s here. So this
     * cannot pre-empt a ladder that is working; it can only catch one that was
     * never armed - which is now a statement about time rather than about
     * somebody else's timer, and is asserted below.
     *
     * The generation is what the stamp belongs to, not the state, because two
     * commands can be issued between two polls and the second must not inherit
     * the first's age.
     */
    if (ext->CommandState == XHCI_CMD_STATE_IDLE) {
        ext->CommandAgeStamp = ext->PollClockMs;
        ext->CommandAgeEscalated = 0;
        ext->CommandAgeGeneration = ext->CommandGeneration;
    } else {
        if (ext->CommandAgeGeneration != ext->CommandGeneration) {
            ext->CommandAgeGeneration = ext->CommandGeneration;
            ext->CommandAgeStamp = ext->PollClockMs;
            ext->CommandAgeEscalated = 0;
        }

        /*
         * Exactly on the crossing, so one over-age command produces one action.
         * The escalation queues a DPC; more polls can run before it lands, and
         * re-requesting on each of them would be asking usbport to re-enter a
         * callback that has already been told.
         *
         * **A clock cannot deliver that with the equality test the poll count
         * used**, because it advances by whatever the period happened to be and
         * may step straight over the threshold. Rung 1 gets it by re-stamping -
         * which is also how the second interval is measured from the abort - and
         * rung 2 by an explicit latch, cleared wherever the stamp is re-taken.
         *
         * **Task 13-R.2: this enters the recovery ladder at rung 1, and until
         * batch 13-R it entered at the end of it.** The old form asked for a
         * controller reset outright, and the wedged E460 is what that reads
         * like from the counters: `CommandAgeResets 1` with `CommandsTimedOut`,
         * `CommandAbortWaits` and `CommandAbortsNotWritten` all **0** - so
         * nothing had ever written CA, and a single Stop Endpoint that would not
         * complete took the whole controller out of service without the
         * specification's own remedy being tried once
         * (docs/contributing/runs/run-13e.md, Finding S).
         *
         * That was wrong on its own terms rather than only in hindsight. This
         * detector's entire premise, argued three paragraphs up, is that
         * reaching it means the *watchdog was never armed* - and the answer to a
         * ladder that never ran is to run it, not to skip to what it would have
         * ended in. "If software doesn't see CRR negated in a timely manner ...
         * then it should assume that there are larger problems with the xHC and
         * assert HCRST" (4.6.1.2, p.94) is the escalation *after* an abort, and
         * an abort is what had never been attempted.
         *
         * So the two rungs are separated by the same interval, measured by
         * re-stamping: an over-age PENDING command is aborted, and only an
         * over-age ABORTING one - a ring that has been told to stop and has not
         * - reaches the reset. The generation is deliberately left alone, so a
         * *different* command arriving in between still re-stamps through the
         * branch above.
         *
         * The margin argument that keeps this from pre-empting a working ladder
         * is unchanged and now covers both rungs: 32 s to the abort against a
         * legitimate worst case of 20 s for the whole watchdog ladder, and 64 s
         * to the reset.
         */
        if ((ext->PollClockMs - ext->CommandAgeStamp) >= XHCI_COMMAND_AGE_MS) {
            if (ext->CommandState == XHCI_CMD_STATE_PENDING) {
                ULONG crcr;

                crcr = 0;
                ext->CommandAgeAborts++;
                ext->CommandState = XHCI_CMD_STATE_ABORTING;
                if (!XhciWriteCrcrAbort(ext, &crcr)) {
                    ext->CommandAbortsNotWritten++;
                }
                ext->CommandAgeStamp = ext->PollClockMs;
                ext->CommandAgeEscalated = 0;
                XHCI_DBG_VALUE("check: command outstanding past every watchdog "
                               "interval - aborting, TRB", ext->CommandTrbPA);
                /* Task 11-V.9's third tier, beside `cmd.timeout`: this is the
                 * same rung reached by the other route, and a ring that shows
                 * one and not the other is a watchdog that never armed. */
                XhciLogNoteLocked(ext, "cmd.age.abort",
                                  (ext->CommandOwnerOp << 16) |
                                      ext->CommandOwner);
            } else if (!ext->CommandAgeEscalated) {
                ext->CommandAgeEscalated = 1;
                ext->CommandAgeResets++;
                escalate = 1;
                XHCI_DBG_VALUE("check: aborted command outstanding past every "
                               "watchdog interval - requesting controller "
                               "reset, TRB", ext->CommandTrbPA);
            }
        }
    }

#ifdef XHCI_FIX_EVT_REARM
    /*
     * **EXPERIMENTAL, bench candidate W8 for Finding 3.** Built only under the
     * define; no shipping flavour carries it.
     *
     * **The upstream half of "usbport was never told".** W7 forces the root-hub
     * *announcement* gate, and is inert if no change was ever latched to
     * announce. This covers the case where nothing is latched because **no event
     * arrives at all**: EHB left set, so the xHC will not raise IP again
     * (4.17.5 p.270), or IMAN.IE lost. Either silences the interrupter, and a
     * silenced interrupter delivers no Port Status Change Events - so a connect
     * is never seen, never latched, never announced, and nothing appears in
     * Device Manager. Machine-wide, because one interrupter serves the whole
     * controller.
     *
     * It also explains why the health poll keeps running while everything else
     * stops: usbport drives this on its own timer, not on our interrupts.
     *
     * So re-publish the dequeue pointer with **EHB clear** and re-arm the
     * interrupter, every poll, unconditionally. `XhciRearmInterrupter` carries
     * the operand validation, read-back and bounded retry a raw write would
     * skip.
     *
     * **Why clearing EHB here is not the mid-drain hazard `xhciPublishErdp`
     * warns about**: that hazard is an intermediate write *during* a drain,
     * which would let the interrupter fire into a drain already in progress.
     * This runs under the controller lock, and the DPC drains to completion
     * under that same lock, so no drain is in flight here.
     *
     * **A RECOVERY candidate, like W7**: if delivery was stalled, it restarts
     * and the machine comes back on its own, with no cold boot.
     */
    XhciWrite64(ext, ext->HcInfo.RuntimeOffset + XHCI_RT_IR0 + XHCI_IR_ERDP,
                XhciEventRingErdpValue(&ext->EventRing, 1));
    (VOID)XhciRearmInterrupter(ext);
#endif

    XhciControllerLockRelease(oldIrql);

#ifdef XHCI_FIX_PORT_POLL
    /*
     * Bench candidate W10 for Finding 3, and **after the release for the same
     * reason W7 is**: its announce path re-enters this miniport's lock.
     */
    XhciRhPortPollSweep(ext);
#endif

#ifdef XHCI_FIX_RH_GATE
    /*
     * Bench candidate W7 for Finding 3. **After the release, and that placement
     * is the whole of its safety**: the watchdog's announce path calls
     * `UsbPortInvalidateRootHub`, which calls `RH_DisableIrq` straight back into
     * this miniport and takes this same non-recursive lock.
     *
     * The health poll is the right heartbeat for it because it is the one thing
     * usbport keeps calling when the root hub has gone quiet - which is exactly
     * the state the watchdog exists to break.
     */
    XhciRhGateWatchdog(ext);
#endif

    return escalate;
}

/* ------------------------------------------------------------------ */
/* Submit                                                             */
/* ------------------------------------------------------------------ */

/*
 * The submit, with the one thing a caller may ask for beyond the command
 * itself: `armWitness` arms the self-test completion witness for *this*
 * issuance, inside the same lock hold that publishes the TRB address and rings
 * the doorbell. It is a parameter rather than a flag on the extension because
 * an arm and the address it is armed against must be published together, and it
 * is not keyed on the TRB type because a No Op is also the cheapest command a
 * test or a future recovery path can issue - the witness belongs to the
 * self-test, not to a TRB type.
 *
 * IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciCommandSubmitEx(PXHCI_EXTENSION ext,
                                 const XHCI_TRB *command,
                                 ULONG *trbPA,
                                 ULONG armWitness)
{
    XHCI_COMMAND_TIMEOUT armed;
    KIRQL oldIrql;
    ULONG status;
    ULONG issuedPA;
    ULONG type;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        command == NULL) {
        return XHCI_CMD_BAD_PARAM;
    }

    /*
     * The ring layer checks capacity and Chain shape but has no opinion about
     * TRB *type* - it is the same code the transfer rings use. So the command
     * ring's own admission rule is here: Table 6-91 gives 9-23 to commands, and
     * publishing anything else - a transfer TRB, an event type, a Link TRB the
     * ring layer owns placing itself - would have the controller execute a TRB
     * type it is not allowed to find there.
     *
     * Vendor Defined types (48-63) are legal on a command ring and are still
     * refused, because this driver issues no vendor command and a type ID means
     * nothing unqualified by PCI Vendor/Device (Table 6-91 note). A phase that
     * needs one relaxes this deliberately rather than by having left it open.
     */
    type = XHCI_TRB_GET_TYPE(command->Control);
    if (type < XHCI_TRB_TYPE_COMMAND_FIRST ||
        type > XHCI_TRB_TYPE_COMMAND_LAST) {
        XHCI_DBG_VALUE_CHANGED("command: refused a TRB type the command ring "
                               "cannot carry", type);
        return XHCI_CMD_BAD_TRB;
    }

    /*
     * Before the enqueue, not after the doorbell. "Software shall be responsible
     * for all command timeouts" (4.6, p.91), and a command that goes out
     * untimed sits on the ring forever - so if there is no way to time it, it is
     * not issued at all.
     */
    if (!xhciCanArmTimer()) {
        ext->CommandTimerFailures++;
        XHCI_DBG_VALUE_CHANGED("command: refused - no async timer service to "
                               "time it with, type", type);
        return XHCI_CMD_NO_TIMER;
    }

    issuedPA = 0;
    armed.Epoch = 0;
    armed.Generation = 0;
    armed.Phase = XHCI_CMD_PHASE_COMMAND;
    armed.Attempt = 0;

    XhciControllerLockAcquire(&oldIrql);

    if (ext->ControllerFailed) {
        /*
         * The recovery ladder ended on this controller. Nothing may go out - not
         * even from an idle engine, which is the state a stop leaves behind and
         * the one a "the state happens to be ABORTING" gate would have let
         * through.
         */
        status = XHCI_CMD_FAILED;
    } else if ((ext->Flags &
                (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) !=
               (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) {
        /*
         * Not merely defensive. A command ring only runs while the controller
         * does - CRR "is set to '1' if the Run/Stop (R/S) bit is '1' and the
         * Host Controller Doorbell register is written" (Table 5-24, p.368) -
         * so a doorbell rung at any other time is a write into a register set
         * whose pointers may belong to a previous start.
         */
        status = XHCI_CMD_NOT_READY;
    } else if (ext->CommandState != XHCI_CMD_STATE_IDLE) {
        /*
         * BUSY covers both a command in flight and an abort in progress, and
         * the second is the one with teeth: "if the Command doorbell is rung
         * before CRR = '0', (i.e. the ring is not fully stopped), then the
         * behavior is undefined, e.g. the Command Ring may not restart"
         * (Table 5-24 note, p.368).
         */
        status = XHCI_CMD_BUSY;
    } else {
        status = XhciRingEnqueue(&ext->CommandRing, command, &issuedPA);
        if (status != XHCI_RING_OK) {
            status = (status == XHCI_RING_FULL) ? XHCI_CMD_RING_FULL
                                                : XHCI_CMD_BAD_TRB;
        } else {
            armed.Generation = ext->CommandGeneration + 1UL;
            if (armed.Generation == 0) {
                /* 0 means "nothing was ever issued", so it is never handed out
                 * - a wrapped counter must not make a stale callback current. */
                armed.Generation = 1;
            }
            ext->CommandGeneration = armed.Generation;
            /* Captured here, with the generation, and not re-read after the
             * lock is dropped - see xhciArmCommandTimer. */
            armed.Epoch = ext->StartEpoch;

            ext->CommandState = XHCI_CMD_STATE_PENDING;
            ext->CommandTrbPA = issuedPA;
            ext->CommandType = type;
            ext->CommandsIssued++;

            /*
             * **The self-test witness is armed here, with the address, and not
             * by the caller after the lock is dropped.** On SMP Win2000 a No Op
             * completes in microseconds: the event DPC on the other CPU can
             * take this lock the instant it is released, read
             * `NoOpWitnessArmed` as 0, leave the genuine completion unwitnessed
             * - and then the caller's store would arm the token against a
             * completion that has already happened, which is exactly the stale
             * state `xhciCommandEndOutstanding`'s comment says every path must
             * prevent. Armed with the same lock hold that publishes
             * `CommandTrbPA`, the way `armed.Epoch` is captured, there is no
             * such window.
             */
            if (armWitness) {
                ext->NoOpTrbPA = issuedPA;
                ext->NoOpWitnessArmed = 1;
            }

            /*
             * The doorbell is inside the lock, and deliberately: it is what
             * makes "the TRB is published and the controller has been told" one
             * step as far as the timeout path is concerned. A CA written
             * between the enqueue and the doorbell would abort a ring that had
             * not been started for this command.
             *
             * Ordering against the TRB's own stores is the accessor's: "all
             * ring/TRB memory writes must hit memory before the doorbell"
             * (docs/usb-xhci-info/xhci-data-structures.md section 8), and WRITE_REGISTER_ULONG
             * serializes on x86. XhciRingEnqueue has already published the Cycle
             * Bit last.
             */
            XhciWriteDoorbell(ext, XHCI_DB_COMMAND, 0);
            status = XHCI_CMD_OK;

            /* Reported from inside the lock, so a caller cannot see the address
             * land after a DPC on another CPU has already completed and cleared
             * the command it names. */
            if (trbPA != NULL) {
                *trbPA = issuedPA;
            }
        }
    }

    XhciControllerLockRelease(oldIrql);

    if (status != XHCI_CMD_OK) {
        XHCI_DBG_VALUE_CHANGED("command: submit refused, status", status);
        return status;
    }

    xhciArmCommandTimer(ext, XHCI_COMMAND_TIMEOUT_MS, &armed);
    return XHCI_CMD_OK;
}

/* IRQL: <= DISPATCH_LEVEL. */
ULONG XhciCommandSubmit(PXHCI_EXTENSION ext,
                        const XHCI_TRB *command,
                        ULONG *trbPA)
{
    return xhciCommandSubmitEx(ext, command, trbPA, 0);
}

/* IRQL: <= DISPATCH_LEVEL: the in-place recovery reaches it from a DPC through
 * XhciInitController, and nothing here waits. */
ULONG XhciCommandNoOpSelfTest(PXHCI_EXTENSION ext)
{
    XHCI_TRB trb;
    ULONG status;

    /* Zeroed first so a refused submit - which arms nothing and stores no
     * address - leaves neither the token nor a previous issuance's address
     * standing for some later command that lands on the same ring position.
     * The successful case's address and its witness are both published by
     * `XhciCommandSubmit` inside the lock hold that rings the doorbell. */
    ext->NoOpTrbPA = 0;
    XhciTrbNoOpCommand(&trb);

    status = xhciCommandSubmitEx(ext, &trb, NULL, 1);
    ext->NoOpStatus = status;

    /* Once per start, so unbounded is the right macro here and nowhere else in
     * this file. */
    XHCI_DBG_VALUE("No Op command issued at TRB", ext->NoOpTrbPA);
    XHCI_DBG_VALUE("No Op submit status", status);
    return status;
}

/* ------------------------------------------------------------------ */
/* Completion                                                          */
/* ------------------------------------------------------------------ */

/*
 * Retire the outstanding command's TD, or say why it could not be.
 *
 * A command is always a single-TRB TD, so the classification is never a
 * judgement call about position - but it is still run rather than assumed,
 * because it is also what rejects a completion code Table 6-90 does not give to
 * a Command Completion Event at all. When it does reject one, the dequeue
 * pointer still has to move: this command's TRB is behind the enqueue pointer
 * and leaving it there permanently shrinks the ring's free count. Flushing to
 * the enqueue position is the correct answer for a ring with one outstanding
 * TD and is exactly what XhciRingSetDequeue's "discard all pending work" case
 * is for.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciRetireCommand(PXHCI_EXTENSION ext, ULONG pointer, ULONG code)
{
    XHCI_TD_COMPLETION completion;

    if (XhciRingClassifyEvent(&ext->CommandRing, pointer, code,
                              &completion) == XHCI_RING_OK &&
        completion.CanRetire &&
        XhciRingRetireTd(&ext->CommandRing, &completion) == XHCI_RING_OK) {
        return;
    }

    ext->CommandsBadCompletion++;
    XHCI_DBG_VALUE_CHANGED("command: unclassifiable completion code", code);
    (VOID)XhciRingSetDequeue(&ext->CommandRing,
                             XhciRingTrbPA(&ext->CommandRing,
                                           ext->CommandRing.Enqueue));
}

/*
 * An ordinary Command Completion Event: this command has finished, whatever its
 * completion code says about how.
 *
 * Returns 1 when the event was **matched** to the outstanding command TRB and 0
 * when it names nothing this engine is waiting for.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
static ULONG xhciCommandCompleted(PXHCI_EXTENSION ext,
                                  ULONG pointer,
                                  ULONG code,
                                  ULONG control)
{
    if (ext->CommandTrbPA == 0 || pointer != ext->CommandTrbPA) {
        /*
         * Expected input rather than an error in one case - an event arriving
         * after this driver has already given the command up - and a real
         * finding in every other. The counter is the only thing that can tell
         * them apart afterwards, which is why it is separate from every other
         * command counter.
         *
         * **The return value is what the severity arm in `XhciCommandEvent`
         * reads**, and audit round 10 asked for it: an event this engine could
         * not match is an event whose Slot ID this driver has no reason to trust
         * as naming a device it still owns.
         */
        ext->CommandsUnmatched++;
        XHCI_DBG_VALUE_CHANGED("command: event names no outstanding TRB",
                               pointer);
        return 0;
    }

    xhciRetireCommand(ext, pointer, code);

    /*
     * The matching command completion is a positive observation the Phase 4
     * checkpoint names directly; without it a match is only inferable from the
     * absence of the unmatched line above, which an event that never arrived
     * would produce as well.
     *
     * The self-test gets its own unbounded site because the checkpoint witness
     * has to survive a *repeated* start. There is one No Op per start and its
     * address is the same every time on a given machine, so a change-gated
     * trace prints the first start and goes silent for every later one - which
     * on Win98 means every resume, since the image stays loaded and the statics
     * with it. Measured: Win98's resume reinitialisation reported
     * nothing here, while Win2000 appeared to work only because disable/enable
     * reloads the image and resets the counters. A witness that a repeat
     * silences cannot distinguish "matched again" from "never completed".
     *
     * The bound is a token armed by the submit, not a property of the value:
     * the TRB address is a position on a reused ring rather than a self-test
     * identity, and a "first completion of the start" counter would miss every
     * resume, since usbport zeroes this extension before StartController but a
     * ResumeController reinitialisation - which issues its own self-test -
     * inherits the counters. One line per issuance, start and resume alike.
     */
    if (xhciCommandWitnessSelfTest(ext, pointer)) {
        XHCI_DBG_VALUE("No Op self-test completion matched TRB", pointer);
        XHCI_DBG_VALUE("No Op self-test completion code", code);
    } else {
        XHCI_DBG_VALUE_CHANGED("command: completion matched outstanding TRB",
                               pointer);
    }

    ext->LastCommandTrbPA = pointer;
    ext->CommandCompletionCode = code;
    ext->CommandSlotId = XHCI_TRB_GET_SLOT_ID(control);
    xhciCommandEndOutstanding(ext);
    ext->CommandsCompleted++;

    /*
     * The device layer's half, and it is handed the *decoded* completion rather
     * than the event, because by here the engine has already done the one thing
     * only it can: proved this event names the command that is outstanding. What
     * the slot layer still has to decide - whether the outstanding command was
     * one of *its* - it decides from ext->CommandType, which this function has
     * deliberately not cleared (src/xhci_slot.c, XhciSlotCommandEvent).
     */
    XhciSlotCommandEvent(ext, code, control);

    if (ext->CommandState == XHCI_CMD_STATE_PENDING) {
        ext->CommandState = XHCI_CMD_STATE_IDLE;
    }
    /*
     * If the state is ABORTING the command completed in the window between this
     * driver writing CA and the xHC acting on it - which the specification says
     * is the *normal* outcome: "Typically when software asserts the Command
     * Abort (CA) flag, the Command Ring will normally stop after the completion
     * of a command ... Only if a command is 'blocked' will it be aborted"
     * (4.6.1.2 implementation note, p.94). The command is resolved either way,
     * but the state stays ABORTING until the Command Ring Stopped event, because
     * that is the event that says the ring may be rung again.
     */
    return 1;
}

/*
 * Command Aborted (code 25) - the abort caught a command actually executing.
 *
 * This does **not** put the ring back in service. The abort generates a second
 * event: "Generate a Command Completion Event with the Completion Code set to
 * Command Ring Stopped and the Command TRB Pointer set to the current value of
 * the Command Ring Dequeue Pointer" (4.6.1.2, p.93), and that one is what ends
 * the abort. The ring layer agrees independently - it classifies 24 and 25 as
 * stopped codes, which never retire a TD, because software rather than the
 * event chooses where execution resumes.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciCommandAborted(PXHCI_EXTENSION ext, ULONG pointer)
{
    ext->CommandsAborted++;

    if (ext->CommandTrbPA == 0 || pointer != ext->CommandTrbPA) {
        ext->CommandsUnmatched++;
        XHCI_DBG_VALUE_CHANGED("command: abort event names no outstanding TRB",
                               pointer);
        return;
    }

    ext->LastCommandTrbPA = pointer;
    ext->CommandCompletionCode = XHCI_CC_COMMAND_ABORTED;
    xhciCommandEndOutstanding(ext);
    /* Whatever this command was going to do for a device, it is not going to:
     * an aborted command's effect is unknown, which is not a state a slot can
     * be left waiting in. */
    XhciSlotCommandLost(ext);

    /*
     * Reaching here with the state still PENDING means something asserted CA
     * that was not this driver's timeout - there is no other way for the xHC to
     * produce this code. Either way a Command Ring Stopped event is now on its
     * way and the doorbell must stay silent until it arrives.
     */
    ext->CommandState = XHCI_CMD_STATE_ABORTING;
}

/*
 * Command Ring Stopped (code 24) - the ring has stopped and software owns it:
 * "While the Command Ring is stopped, ownership of all Command Descriptors on
 * the ring is passed to software" (4.6.1.1, p.93).
 *
 * Its Command TRB Pointer is **not** a completed command. It is "the current
 * value of the Command Ring Dequeue Pointer" (same page), which after an abort
 * has already been advanced past the aborted command - "Advance the Command Ring
 * Dequeue Pointer to point to the next Command TRB" (4.6.1.2, p.93). So the
 * aborted TRB is not re-executed by a restart, and this driver's job is only to
 * make its own dequeue pointer agree with the one the controller just reported.
 * Nothing else is needed on the normal path: "Command Ring execution shall
 * restart at the current Dequeue Pointer value, i.e. the TRB following the last
 * command executed (or aborted)" (4.6.1.1, p.93), and the next submit's
 * doorbell is the restart.
 *
 * **When the reported position is one this ring cannot hold, the answer is to
 * escalate rather than to reposition**, and that is a conclusion rather than
 * caution. The obvious repair - rewrite CRCR, which is legal exactly here
 * because "The Command Ring Pointer field may only be modified by software while
 * the Command Ring is stopped" (4.6.1, p.92) - cannot express what it would
 * need to. The Command Ring Pointer is bits 63:6: "The Command Ring is 64 byte
 * aligned, so the low order 6 bits of the Command Ring Pointer shall always be
 * '0'" (Table 5-24 note, p.368), so only every *fourth* TRB of a 16-byte-TRB
 * ring is an addressable restart position, and this driver's enqueue pointer is
 * generally not one of them. Rewriting to the ring's base instead - which is
 * 64-byte aligned by construction - would restart the controller over TRBs left
 * from earlier laps whose Cycle Bits may still read as produced, i.e. re-execute
 * commands that have already run. So a pointer this ring cannot adopt means the
 * software and hardware ideas of the command ring have diverged, and the only
 * honest recovery is the one that rebuilds both: usbport's controller reset.
 *
 * The state deliberately stays ABORTING in that case, which is what keeps the
 * doorbell silent until the reset arrives.
 *
 * **This event is the only thing that ends an abort**, and that is a decision
 * the review round after task 7 reversed. The abort watchdog used to conclude
 * locally that the ring had stopped - it can read CRR - and put the engine back
 * in service without waiting for the event. That opened a window nothing could
 * close honestly: a Command Ring Stopped arriving afterwards, with a new command
 * already outstanding, is indistinguishable from a controller that has stopped a
 * ring this driver believes is running. An attempt to distinguish them by
 * counting the events an abort still owes fails on the specification's own
 * wording, because the Command Aborted "may not be found on the Event Ring"
 * (4.6.1.2, p.94) - so the count can never be reconciled, and a stale
 * expectation would swallow a later genuine disagreement.
 *
 * With the local recovery gone the ambiguity is gone with it: while an abort is
 * outstanding the state is ABORTING and this event resolves it, and in any other
 * state a stopped ring is a statement this driver cannot reconcile with what it
 * believes, so it escalates without touching the current command. The watchdog's
 * CRR read now only decides how long to keep waiting.
 *
 * Called with the controller lock held; returns the action its caller takes
 * after dropping it. IRQL: DISPATCH_LEVEL.
 */
static ULONG xhciCommandRingStopped(PXHCI_EXTENSION ext, ULONG pointer)
{
    ext->CommandRingStops++;

    if (ext->CommandState != XHCI_CMD_STATE_ABORTING) {
        /* A stopped ring with no abort of this driver's outstanding - with a
         * command running or without one. The two ideas of the ring disagree,
         * and nothing here can reconcile them without discarding whatever is
         * live. */
        ext->CommandRingDiverged++;
        ext->CommandResetRequests++;
        XHCI_DBG_VALUE_CHANGED("command: stopped event with no abort "
                               "outstanding - the ring has diverged, TRB",
                               pointer);
        return XHCI_CMD_ACTION_RESET;
    }

    if (ext->CommandTrbPA != 0) {
        /* The abort found the ring between commands, so the Command Aborted
         * event never came: "a Command Completion Event with the Completion Code
         * set to Command Aborted may not be found on the Event Ring after an
         * abort operation" (4.6.1.2, p.94). The command is over regardless. */
        ext->LastCommandTrbPA = ext->CommandTrbPA;
        ext->CommandCompletionCode = XHCI_CC_COMMAND_RING_STOPPED;
        xhciCommandEndOutstanding(ext);
        XhciSlotCommandLost(ext);
    }

    /*
     * A ring stopped with its dequeue pointer on the Link TRB is a ring stopped
     * at index 0: the xHC reports where it would fetch next, and after the
     * command at `Trbs - 2` that is the link, which software's enqueue has
     * already crossed. The ring layer refuses the link as a position execution
     * can start at, so the mapping is made here, where it is known to be the
     * Command Ring Stopped meaning of that address.
     */
    if (pointer == XhciRingTrbPA(&ext->CommandRing,
                                 ext->CommandRing.Trbs - 1)) {
        pointer = XhciRingTrbPA(&ext->CommandRing, 0);
    }

    if (XhciRingSetDequeue(&ext->CommandRing, pointer) != XHCI_RING_OK) {
        ext->CommandRingDiverged++;
        ext->CommandResetRequests++;
        XHCI_DBG_VALUE_CHANGED("command: stopped at a position this ring cannot "
                               "hold - requesting controller reset, TRB",
                               pointer);
        return XHCI_CMD_ACTION_RESET;
    }

    ext->CommandState = XHCI_CMD_STATE_IDLE;
    return XHCI_CMD_ACTION_NONE;
}

/* IRQL: DISPATCH_LEVEL, under usbport's MiniportInterruptsSpinLock. */
ULONG XhciCommandEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event)
{
    ULONG code;
    ULONG pointer;
    ULONG action;
    ULONG matched;

    if (ext == NULL || event == NULL) {
        return 0;
    }

    code = XHCI_TRB_GET_COMPLETION(event->Status);

    /*
     * The Command TRB Pointer is a 64-bit field whose low four bits are RsvdZ.
     * The high DWORD is structurally zero for this driver - every ring it owns
     * lives below 4 GB - but it is checked rather than discarded, which is the
     * rule Phase 3 task 10 arrived at for usbport's own scatter/gather list: a
     * value that is always zero costs one comparison to confirm and is a real
     * finding when it is not.
     */
    if (event->Param1 != 0) {
        ext->CommandsUnmatched++;
        XHCI_DBG_VALUE_CHANGED("command: event pointer above 4 GB, high dword",
                               event->Param1);
        return 0;
    }

    /*
     * Bits 3:0 are RsvdZ in a field the *hardware* writes, so a conforming
     * controller leaves them clear. They are masked rather than made a refusal -
     * "Reserved" means software must not depend on them, and refusing a real
     * completion over a bit the specification says is not there would turn a
     * cosmetic controller quirk into a wedged command ring. But a nonzero value
     * is still a finding, and counting it is the difference between knowing that
     * and silently aliasing the pointer down to a TRB the controller did not
     * name.
     */
    if ((event->Param0 & 0x0FUL) != 0) {
        ext->CommandsReservedBitsSet++;
        XHCI_DBG_VALUE_CHANGED("command: event pointer has RsvdZ bits set",
                               event->Param0);
    }
    pointer = event->Param0 & ~0x0FUL;
    action = XHCI_CMD_ACTION_NONE;
    matched = 0;

    /*
     * The caller holds the controller-state lock across the whole drain. That
     * is what makes its ControllerFailed decision stable through this function
     * and through the ERDP publication/re-arm that follow it. Taking the lock
     * here as well would self-deadlock.
     */
    if (code == XHCI_CC_COMMAND_RING_STOPPED) {
        action = xhciCommandRingStopped(ext, pointer);
    } else if (code == XHCI_CC_COMMAND_ABORTED) {
        xhciCommandAborted(ext, pointer);
    } else {
        matched = xhciCommandCompleted(ext, pointer, code, event->Control);
    }

    /*
     * **Table 6-90's severity, on the event family audit round 9 found bypassing
     * it.** Round 8 taught the transfer paths and the restore's drain to read the
     * table's fatal and slot-fatal instructions; the live command path was left
     * handing every code to the slot state machine, which reduces it to
     * success/non-success. So a Configure Endpoint answered with Incompatible
     * Device Error left the slot in service, and an Undefined Error or an
     * unrecognised vendor error on a command produced no invalidation at all -
     * while the *same code on the same ring* refused a restore.
     *
     * The table is consulted rather than a list written out here, for the reason
     * rounds 6, 7 and 8 each established the hard way: `XhciXferCodeInfo` is the
     * one place Table 6-90 is transcribed, and every hand-written copy of it in
     * this repository has been found short by the next round. It answers
     * `XHCI_XFER_BAD_PARAM` for the two command-ring codes handled above, so
     * neither of those can reach an arm here.
     *
     * **After the ordinary retirement, not instead of it**, which is the rule the
     * transfer paths already follow: the command is matched, retired and reported
     * to its owner by the machinery that owns it, and only then does the severity
     * act. Escalating first would leave a command outstanding for ever on an
     * engine that allows one at a time.
     *
     * **The two severities are gated differently, and audit round 10 is why.**
     * Round 9 ran both arms on any Command Completion Event, matched or not. A
     * *controller-level* fault is a statement about the controller and carries
     * the same weight whichever command it names - the escalation is bounded,
     * and refusing to act on an unmatched one would discard the only notice of an
     * Undefined Error. A *slot-fatal* code is a statement about one device, and
     * acting on it destroys that device: the only thing tying the event's Slot ID
     * to a record this driver still owns is the match, and an event the engine
     * cannot match is one whose Slot ID may have been recycled since. Not acting
     * costs little, because a slot the controller cannot use produces the same
     * code on the next *transfer* to it, where the path is unconditional.
     */
    {
        XHCI_XFER_CODE info;

        if (XhciXferCodeInfo(code, &info) == XHCI_XFER_OK) {
            if (info.SlotFatal != 0 && matched) {
                XhciSlotCommandSlotFatal(ext, code, event->Control);
            }
            if (info.Fatal != 0) {
                /*
                 * The same escalation the transfer path takes.
                 * `CommandResetRequests` stays the total this engine has asked
                 * for, so nothing that read it reads something else now, and
                 * `CommandsFatal` says how much of that total the controller
                 * *reported* rather than this driver *inferred* from a ring
                 * position it could not adopt. Audit round 10 found the two
                 * sharing one reading, which is the counter contract this
                 * repository has enforced since Phase 4 task 8: two causes
                 * needing opposite investigations may not answer as one number.
                 */
                ext->CommandsFatal++;
                ext->CommandResetRequests++;
                XHCI_DBG_VALUE_CHANGED("command: a completion code Table 6-90 "
                                       "calls fatal - requesting controller "
                                       "reset, code", code);
                action = XHCI_CMD_ACTION_RESET;
            }
        }
    }

    /*
     * Return the service action instead of taking it here. The DPC still holds
     * the controller-state lock, and UsbPortInvalidateController must be called
     * only after that lock has been released.
     */
    return (action == XHCI_CMD_ACTION_RESET) ? 1UL : 0UL;
}

/* ------------------------------------------------------------------ */
/* The timeout, and the recovery ladder it drives                      */
/* ------------------------------------------------------------------ */

/*
 * The command watchdog, and the abort watchdog after it. usbport invokes this
 * from its async timer DPC "without MiniportSpinLock or MiniportInterruptsSpinLock"
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 7), so it can run concurrently with the
 * DPC that services completions, on another CPU. Everything it reads or writes
 * of the command state is therefore under the controller lock, and every
 * decision is made from the copied generation rather than from anything it
 * could re-read.
 *
 * Four ways to arrive with nothing to do, and all four are ordinary:
 *
 *   The command completed before the timer expired - the overwhelmingly common
 *   case, since the timer is never cancelled.
 *   The controller was stopped, suspended or reinitialized in between, which
 *   XhciControllerBeginQuiesce records by advancing the generation.
 *   A second start reused the extension - usbport zeroed it in between, so the
 *   *epoch* is what answers this one and the generation cannot.
 *   The abort this callback was watching resolved by another route.
 *
 * There is a fifth, which is not "nothing to do" but "nothing may be done": the
 * controller has been declared failed, and the ladder is over.
 *
 * All of them are comparisons, and all of them return **before touching a
 * register** - which is what "post-stop callbacks do not touch MMIO" means when
 * the controller may be in D3 by now.
 *
 * IRQL: DISPATCH_LEVEL, no usbport lock held.
 */
static VOID NTAPI xhciCommandTimeout(PVOID miniPortExtension, PVOID context)
{
    PXHCI_EXTENSION ext;
    PXHCI_COMMAND_TIMEOUT timeout;
    XHCI_COMMAND_TIMEOUT armed;
    KIRQL oldIrql;
    ULONG action;
    ULONG crcr;

    ext = (PXHCI_EXTENSION)miniPortExtension;
    timeout = (PXHCI_COMMAND_TIMEOUT)context;

    /*
     * Only the two pointers are checked before the lock, because they are the
     * only things that can be judged without reading the extension. **Everything
     * else is validated underneath it**, which is the change the second review
     * round asked for: an epoch tested outside the lock is a check, not
     * synchronization, and a callback that passed such a check could still be
     * preempted and resume inside KeAcquireSpinLock. That mattered while the lock
     * lived in the extension and a restart re-created it; the lock is now the
     * driver's and is never re-created, so the honest form is to take it first
     * and decide everything after.
     */
    if (ext == NULL || timeout == NULL) {
        return;
    }

    action = XHCI_CMD_ACTION_NONE;
    crcr = 0;
    armed.Epoch = 0;
    armed.Generation = 0;
    armed.Phase = XHCI_CMD_PHASE_ABORT;
    armed.Attempt = 0;

    XhciControllerLockAcquire(&oldIrql);

    /*
     * The full bracket, like the registered callbacks get. This one is not on
     * the DIRQL path, so the argument that lets XhciIsr check only the leading
     * word does not apply, and the trailing word is what catches an extension
     * smaller than MiniPortExtensionSize asked for. A zero epoch is a zeroed
     * extension usbport has not yet started, which no context ever carries.
     */
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        ext->TrailingSignature != XHCI_EXTENSION_TRAILING ||
        timeout->Epoch == 0 || timeout->Epoch != ext->StartEpoch) {
        /* Not this driver's extension, or not this start's callback. Counting it
         * would be a write into somebody else's structure. */
        XhciControllerLockRelease(oldIrql);
        return;
    }

    /*
     * **Counted here and nowhere else: after the bracket, before every
     * decision.** Finding T needed to know whether a watchdog that produced no
     * verdict had *arrived* at all, and every other counter in this function is
     * written after a branch has already decided what the callback was for. It
     * cannot go above the bracket - that would be a write into a structure this
     * driver has not established is its own.
     */
    ext->CommandTimeoutArrivals++;

    if (ext->ControllerFailed) {
        /* The ladder ended. Nothing may write CRCR or ask for a second reset. */
        ext->CommandsAfterFailure++;
    } else if (timeout->Generation != ext->CommandGeneration ||
        (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) == 0) {
        ext->CommandStaleCallbacks++;
    } else if (timeout->Phase == XHCI_CMD_PHASE_COMMAND &&
               ext->CommandState == XHCI_CMD_STATE_PENDING) {
        /*
         * Rung 1: abort - and the write is now gated on the read it needs anyway.
         *
         * The old form wrote CA as a bare literal on the argument that a read
         * could preserve nothing: RCS, CS and CA all "always return '0'" and the
         * pointer field does too (Table 5-24, p.367). That enumeration is
         * accurate and it is incomplete - **bits 5:4 are RsvdP**, they are the
         * only bits of this register a read can carry anything in, and "software
         * shall preserve the value read for writes to bits" (5.1.1, p.338). So
         * the operand comes from a read, through the same helper the two
         * pointer-writing sites use.
         *
         * The same read then answers a second question the literal form could
         * not ask. CA "is ignored by the xHC if Command Ring Running (CRR) =
         * '0'" (p.367), so on a stopped ring the write achieves nothing - except
         * that the *pointer* half is not ignored there: "If the CRCR is written
         * while the Command Ring is stopped (CRR = '0'), the value of this field
         * shall be used to fetch the first Command TRB the next time the Host
         * Controller Doorbell register is written" (p.368). A CA write composed
         * with a zero pointer, on a ring that is already stopped, therefore
         * repoints the command ring at physical address 0. So the write is made
         * only while CRR is set, which is exactly when it does anything; a
         * stopped ring falls through to the same ABORT phase with nothing
         * written, and rung 2's own CRR read then bounds it.
         */
        ext->CommandsTimedOut++;
        ext->CommandState = XHCI_CMD_STATE_ABORTING;
        if (!XhciWriteCrcrAbort(ext, &crcr)) {
            ext->CommandAbortsNotWritten++;
            XHCI_DBG_VALUE_CHANGED("command: abort not written, CRCR", crcr);
        }
        armed.Epoch = ext->StartEpoch;
        armed.Generation = ext->CommandGeneration;
        action = XHCI_CMD_ACTION_ARM;
        XHCI_DBG_VALUE_CHANGED("command: timed out, aborting TRB",
                               ext->CommandTrbPA);
        /* Task 11-V.9's third tier, and the one the recovery ladder hangs off:
         * a command that did not complete is the start of every escalation this
         * driver can make, up to and including a controller reset. */
        XhciLogNoteLocked(ext, "cmd.timeout",
                          (ext->CommandOwnerOp << 16) | ext->CommandOwner);
    } else if (timeout->Phase == XHCI_CMD_PHASE_ABORT &&
               ext->CommandState == XHCI_CMD_STATE_ABORTING) {
        /*
         * Rung 2 or 3, and the *only* thing decided here is how much longer to
         * wait. The engine does not return to service on this path: only the
         * Command Ring Stopped event ends an abort, for the reason argued at
         * xhciCommandRingStopped - recovering locally on a CRR read opens a
         * window in which a later stopped event cannot be told from a real
         * disagreement, and the specification's "may not be found on the Event
         * Ring" (4.6.1.2, p.94) means that window can never be closed by
         * counting.
         *
         * So: CRR still set is the escalation the specification names outright -
         * "If software doesn't see CRR negated in a timely manner (e.g. longer
         * than 5 seconds), then it should assume that there are larger problems
         * with the xHC and assert HCRST" (4.6.1.2, p.94). CRR clear means the
         * ring did stop and the event is merely late, so wait one more interval
         * - bounded, because a controller that stops without ever posting the
         * event is as broken as one that will not stop.
         */
        crcr = XhciReadOp(ext, XHCI_OP_CRCR);
        if ((crcr & XHCI_CRCR_CRR) != 0 ||
            timeout->Attempt >= XHCI_COMMAND_ABORT_WAITS) {
            ext->CommandResetRequests++;
            action = XHCI_CMD_ACTION_RESET;
            XHCI_DBG_VALUE_CHANGED("command: abort did not complete, CRCR",
                                   crcr);
        } else {
            ext->CommandAbortWaits++;
            armed.Epoch = ext->StartEpoch;
            armed.Generation = ext->CommandGeneration;
            armed.Attempt = timeout->Attempt + 1UL;
            action = XHCI_CMD_ACTION_ARM;
        }
    } else {
        ext->CommandStaleCallbacks++;
    }

    XhciControllerLockRelease(oldIrql);

    if (action == XHCI_CMD_ACTION_ARM) {
        xhciArmCommandTimer(ext, XHCI_COMMAND_ABORT_MS, &armed);
    } else if (action == XHCI_CMD_ACTION_RESET) {
        XHCI_DBG_VALUE_CHANGED("command: ring will not stop - requesting "
                               "controller reset, CRCR", crcr);
        XhciRequestControllerReset(ext);
    }
}
