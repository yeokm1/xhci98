# 07 - Recovering a failed controller in place

Phase 13, batch 13-R, task 13-R.1. This record comes out of the defect batch
13-E found on a ThinkPad E460 and read off the machine rather than inferred:
`docs/contributing/runs/run-13e.md`, Finding S.

The roadmap asked for this record by name. The choice it records, recover in
place rather than do not latch at all, is a design decision with a hardware
contract and an IRQL contract behind it, and neither of those fits in a commit
message.

## 1. The defect, in one paragraph

`xhciResetController` is the miniport's `ResetController` slot. It runs at
DISPATCH_LEVEL inside one of usbport's spin locks, so it cannot reinitialize
anything: the initialization sequence sleeps twice, and `UsbPortWait` is
`KeDelayExecutionThread`. What it did instead was mask the two interrupt enables,
latch `ControllerFailed`, and say in its own trace text what it was waiting for:
"a miniport cannot reinitialize from a DPC holding usbport's lock; recovery is a
stop/start".

Nothing anywhere performed that stop/start. After the latch, `xhciRhAdmitted`'s
fourth clause refuses every root-hub path that would touch hardware, so a replug
raises `CSC` in silicon that nothing is listening to. On the machine that
reproduces it, the port sat powered, connected, in Polling, with a standing
change bit, until a cold boot. A failure path whose recovery step has no owner
is not a recovery path.

The blast radius is wider than the callback. `XhciRequestControllerReset` has
five callers: the command timeout in `src/xhci_cmd.c`, three event-drain paths
in `src/xhci_evt.c` and one in `src/xhci_dispatch.c`. The dead end was reachable
from any of them, and the repair converts "permanently dead until reboot" into
"a stall that recovers" for all five at once.

## 2. Why "wait for a stop/start" was not a design

It was never checked, and when it was checked it was false. The batch 13-R
census (static, both shipping `usbport.sys` builds, recorded in
`docs/usb-xhci-info/usbport-miniport-abi.md` in the two subsections after the
`UsbPortInvalidateController(RESET)` box) established three things:

1. `StartController` has exactly three call sites per build and `StopController`
   exactly three. Every direct caller chain out of them terminates at the
   `IRP_MJ_PNP` or `IRP_MJ_POWER` handler (plus, on the Win2000/XP build only, an
   HCD IOCTL that asks for a power transition). Nothing reaches them on a
   timer, from a watchdog, or from a failed transfer.
2. The reset DPC's own body calls the slot, releases the lock, drops a busy
   reference and returns. It arms nothing.
3. `CheckController` is a `VOID` slot and usbport never reads `EAX` after the
   call, so usbport collects no health verdict from a miniport and can conclude
   nothing from one.

So on a machine nobody suspends and nobody disables in Device Manager, "recovery
is a stop/start" means "recovery is the next boot".

The same pass answered the other end of the ladder, and it matters here: usbport
never asks for a `ResetController` either. The only producer of
`UsbPortInvalidateController(RESET)` is a miniport; usbport's one internal call
site of that routine passes `SURPRISE_REMOVE`. The whole loop is this driver
calling itself back, with usbport contributing a DPC and a spin lock. That is the
one piece of good news in the defect: no third party has to change behaviour
for the latch to stop being terminal.

## 3. The two candidate designs, and why one of them was chosen

### (a) Do not latch at all, degrade instead

Keep interrupts live, keep the root hub admitted, and mark only the command
engine out of service. Rejected as insufficient on the evidence, for two
reasons.

It does not recover the observed fault. In the wedged dump the device record for
the port was `XHCI_DEV_STATE_GONE` with `XHCI_DEV_OP_DISABLE_SLOT` still owed,
and a Disable Slot waits for every endpoint's Stop Endpoint, the command that
would not complete. A replug then arrives at a port a dead record still claims,
and `EnumResetSuppressed 1` says the reset was suppressed. A driver that kept
listening would have heard the connect and still not enumerated it.

Not masking is a real risk on real hardware. The enables are masked so a wedged
controller cannot storm a shared level-triggered INTx line that the driver has
decided it will no longer service. That risk is unchanged by the repair, and
trading it away to buy a half-recovery is a bad trade.

### (b) Recover in place (chosen)

Perform the reinitialization the latch names, from a context this driver reaches
on its own. HCRST returns every slot and every port to its default state, so the
devices on the bus disconnect and usbport enumerates them again. That is what a
hub being unplugged and replugged does to usbport, and it needs no cooperation
from it.

## 4. The shape, and every part of it is forced

```
  ResetController (DISPATCH, usbport's reset-DPC spin lock held)
      mask the enables, latch ControllerFailed, set RecoveryRequested
             |
             v
  CheckController (DISPATCH, usbport's MiniportSpinLock held, every 500 ms)
      xhciArmRecovery: one UsbPortRequestAsyncCallback, stamped with StartEpoch
             |
             v
  xhciRecoveryCallback (DISPATCH, NO usbport lock)
      XhciRecoverController:
          XhciControllerBeginQuiesce   retire the command engine and the
                                       root-hub timers, drop INITIALIZED
          XhciSlotInvalidateAll        drop every device, completing the
                                       transfers usbport is holding
          XhciInitController(NULL)     HCRST and the whole sequence, with
                                       ext->InitBelowPassive set
          XhciEnableInterrupts         if usbport had asked for interrupts
          XhciRootHubDeferredWork      announce what the fresh seed found
```

Why the request and the arming are in different places.
`UsbPortRequestAsyncCallback` reaches usbport's own timer machinery, and
`ResetController` runs inside usbport's reset-DPC spin lock. Arming there would
nest two of usbport's locks with no stated order between them, which is the shape
of a deadlock rather than of a race. `CheckController` is where this driver
already calls usbport services, and it is the only periodic context that survives
the latch: on the wedged E460, `HealthPolls` climbed past 8,485 while
`InterruptCount` sat frozen at 182.

Why the async callback rather than the poll itself. The poll holds
`MiniportSpinLock`; the async-callback DPC holds no usbport lock at all
(`docs/usb-xhci-info/usbport-miniport-abi.md` section 7, confirmed in the NUSB
binary at `0002785E`). It is the sanctioned deferred-work tool, and the only one.
`IoAllocateWorkItem`/`IoQueueWorkItem` are on the import gate's deny list as
absent on Win98, and there is no PASSIVE-level worker available to this driver
between `StartController` and the shutdown.

Why the quiesce is not optional. `XhciInitController` reprograms the hardware;
it does not retire this driver's own command engine, because on every other path
something already has. `StartController` calls `XhciCommandInit` on an extension
usbport has just zeroed, and the resume arrives through a suspend whose quiesce
ran it. The recovery arrives through neither, and the state it arrives with is
the wedged one: a command outstanding on a ring that would not stop. Without the
quiesce the reinitialization's own No Op self-test is answered `XHCI_CMD_BUSY`
by an engine holding a command HCRST has just abolished, and the whole sequence
refuses at its last step. The host suite found this, not review.

## 5. `InitBelowPassive`: the IRQL contract, stated as a flag

The recovery runs at DISPATCH_LEVEL. Everything the initialization sequence
reaches has to be legal there, and three things were not. Rather than fork the
sequence, one word in the extension is set for its duration and these sites read
it:

| Site | What it does while set | Why |
|---|---|---|
| `XhciWaitForBits` | skips the sleep phase entirely; the wait is the existing 10 ms stall and nothing more | `UsbPortWait` is `KeDelayExecutionThread`. Not extended to busy-wait the full timeout: that would spin a DPC for the better part of a second on hardware that has already failed. A bit that does not settle inside the stall makes the attempt refuse, and the extra time comes from the next attempt rather than from a spin. |
| `XhciDelayMs` | stalls instead of sleeping | Same reason. It is 20 ms once per attempt, the port-power settle, which the specification states as a duration rather than as a condition. |
| `XhciInitController` | skips the three PCI configuration-space reads (identification, the INTx gate, the bus-master gate) | `UsbPortReadWriteConfigSpace` goes out to the bus driver; this project's contract for it is PASSIVE_LEVEL. |
| `xhciTryClearBusMaster` | declines outright | Same service. The proof it would obtain is unavailable, and on this path unnecessary (see below). |
| `XhciFailClosedDma` | counts (`DmaFailClosedDeferred`) instead of bugchecking | The bugcheck's premise is a reclamation and this path has none. It is justified by usbport being about to take the common buffer back while an xHC that may still be mastering points at it. On the recovery path nothing is handing anything back, so the block stays this driver's. Taking a machine down here would turn a stall into a crash, the opposite of the task. |

What skipping the configuration-space reads costs. Those three are questions
about a device rather than about a controller's current state, and every one of
them was answered on this same PCI function by the start that succeeded; the
recovery is reached only from a controller this driver started. Nothing between
then and now re-enumerated the bus. A PnP restart would have gone through
`StopController`/`StartController` and taken this path with `InitBelowPassive`
clear.

## 6. Where the latch is released, and why there

`XhciInitController` clears `ControllerFailed` immediately after HCRST
completes, beside `XhciSlotInit`. Both alternatives are wrong for the same
reason: the latch is a statement about hardware in an unknown state, and this is
the line at which that stops being true.

- At the top of the sequence it would clear on a run that then refused at a
  preflight gate, reporting a failed controller as merely uninitialized.
- At the bottom it would be too late for the two steps that need it:
  `XhciRootHubInit` passes `xhciRhAdmitted`, whose fourth clause is this word, and
  the No Op self-test passes `XhciCommandSubmit`, whose first gate is this word.

`XHCI_EXT_FLAG_INITIALIZED` is still down across that window, so nothing is
admitted in between whatever the word says.

A refusal after that point re-latches, in `XhciRecoverController`. A sequence
that got past HCRST and then failed at the run, the root hub or the self-test has
left a controller that is not in service, and leaving the word clear would say the
opposite to every admission gate in the driver. Concretely, the health poll's
arming predicate reads that word, so without the re-latch a failed attempt would
never be retried.

This also fixes the resume path, which had the same gap by the same route:
`XhciCommandInit` is the only other place that clears the word and it is called
from `StartController`, so a controller that failed and was then suspended and
resumed came back reinitialized and still latched.

## 7. Bounds, and what stays terminal

`XHCI_RECOVERY_MAX_ATTEMPTS` is 3, spaced by the health poll (~500 ms apart)
because each attempt drives the controller through a halt and an HCRST. A
controller that will not come back after that stays latched, and that is the
honest terminal state.

The cap bounds consecutive failures. The arming predicate reads
`RecoveryFailuresConsecutive`: a refusal increments it, a success clears it.
`RecoveryAttempts` stays the lifetime reading, because a counter that resets
cannot answer "how often has this machine needed one".

That distinction was learned on the E460 (`run-13e.md`, Finding T). The first
version's cap read `RecoveryAttempts`, the lifetime total. On the bench the
recovery worked three times out of three, and the fourth incident, an ordinary
new fault retrying nothing, found the budget already spent by those three
successes and left the port dead. `ResetControllerCalls 4` against
`RecoveryAttempts 3` with `RecoveryFailures 0` is the whole defect in three
numbers. A budget that is spent by success is not a safety bound, it is an
expiry date. No host vector caught it because every one of them made the
recovery fail; the case that breaks it is the recovery working more often than
the cap allows.

An attempt that declines before it starts spends budget too.
`XhciRecoverController`'s first guard is `HcInfoStatus != XHCI_HC_OK`: nothing
was ever decoded, so there is no register to reprogram. `XhciInitController`
sets `HcInfoStatus = XHCI_HC_BAD_PARAM` at its top and only restores it at the
capability decode, so an attempt that refused above that line (a dead MMIO
window, realistic in a genuine wedge) leaves the status bad for every later
callback.

The callback re-requests whenever `RecoveryFailuresConsecutive` is
under the cap, so if that guard counted nothing the callbacks would decline
uncounted and re-request once per health poll, for ever. It counts as a
consecutive failure, with `RecoveryLastStep = XHCI_INIT_STEP_NONE` (no step of
the sequence ran) and `RecoveryLastStatus` carrying the `HcInfoStatus` that
stopped it. A bound that is only decremented on the path that does work is not
a bound.

The difference the repair makes is not that failure became impossible. Failure
became measured: `RecoveryAttempts`, `RecoveryFailures` and
`RecoveryLastStep`/`RecoveryLastStatus` are readable from a release build, so
"the controller would not come back, and it refused at step N" is a finding
rather than a silence.

## 8. The known window, recorded rather than closed

The recovery runs from a DPC holding no usbport lock, so on SMP Windows 2000
usbport can call `SubmitTransfer` on another CPU while it is running.
`XHCI_EXT_FLAG_INITIALIZED` is down for the whole sequence except its last steps,
and every device record has been invalidated, so a transfer landing in that window
is refused rather than served against a slot the xHC no longer has. This is the
same window the resume path's reinitialization already has. What is different is
that the resume runs from a power IRP, where usbport is not delivering transfers,
so this is a narrowing of that argument rather than an appeal to it. It is
written here so the next reader does not have to rediscover the difference.

### 8.1 A suspend in the latch-to-callback window: closed

Section 6's last paragraph covers a controller that was suspended and then
resumed while latched. The interleaving is a separate case: an idle suspend
arriving between the latch and the callback that acts on it. That is not a
corner. Windows 98's usbport idle-suspends this controller within about half a
second of the last transfer, and a wedge is a bus with nothing moving on it.

`StopController` and `SuspendController` clear neither `ControllerFailed` nor
`RecoveryRequested`, and neither of the callback's two stale checks covers a
suspend: the epoch answers a completed restart, `ControllerFailed` answers a
healthy controller. Without a third check the recovery would restart, power the
ports off, and re-enable interrupts on a controller usbport believes is asleep.

Both `xhciArmRecovery` and `xhciRecoveryCallback` decline while
`XHCI_EXT_FLAG_SUSPENDED` is set, the callback counting `RecoveryStaleCallbacks`.

Declining also puts the request back, and that is not bookkeeping. The arming
consumed `RecoveryRequested`. The other two stale clauses decline because there
is nothing left to do; this one declines because it cannot act now.

Without restoring the request, the latch would be left with no request and no
armed callback, and the resume does not always clear the latch. A
reinitialising resume does (section 6's last paragraph read forwards). A
resume that succeeds through the `xhciRestoreState` path returns without
reinitialising, so `ControllerFailed` survives it.

That combination is a permanently stranded
controller, reachable on any host whose xHC implements CSS/CRS conformingly,
which is every real one this driver is aimed at and neither target VM: QEMU
implements CRS as "set SRE" and always falls through to the reinitialisation. A
host vector on the conforming controller covers it; the QEMU-shaped default
cannot see it.

The residual is SMP-only and is narrowed rather than closed. A suspend arriving
during `XhciRecoverController`, after the flag was read under the lock and while
the sequence is running, is the same window section 8 describes for
`SubmitTransfer`. On Windows 98 there is no preemption, so the suspend cannot
begin until the DPC returns; on SMP Windows 2000 it can, and the two would be
writing the same registers. Two things bound what that costs.

First, a recovery that got this far has already succeeded, so
`XhciInitController` cleared the latch and the resume that follows is an
ordinary one: it reinitialises or restores over a controller in a known state,
and either way ends with one writer rather than two. (This is a different window
from the declined-callback one above. There is no handback here, because the
callback never declined.)

Second, the one step that would outlive the suspend is refused: the recovery's
`XhciEnableInterrupts` reads `SUSPENDED` as well as `INTERRUPTS`, so an
interrupter is never armed on a controller usbport believes is asleep. What
remains is the halt, the HCRST and the port power, all of which a suspend is
about to perform or tolerate anyway. A future task that widens either path
should close the rest with the controller lock rather than by re-reading the
flag.

Who puts the enables back afterwards depends on whether the latch is still
standing. `XhciEnableInterrupts` refuses while `ControllerFailed` is set
(`src/xhci_evt.c`), recording usbport's request and touching no register,
because handing a live interrupt line to a controller this driver has declared
dead is the thing the latch exists to prevent. So:

- Latch clear: an ordinary suspend, and the case after a recovery that
  succeeded. Both resume branches re-enable on their own path; on the restoring
  one that is `XhciResumeController`'s own call, made once `xhciRestoreState`
  has succeeded (the helper itself touches no enable).
- Latch still set: the declined-callback window above. The reinitialising
  resume clears the latch and re-enables. The restoring one is refused, and the
  enables come back with the next recovery, the one the handed-back request
  arms. The two halves of that repair hold each other up: without the handback
  the latch never clears, and without the latch clearing the interrupts never
  come back either.

Both are asserted by host vectors on the conforming controller, which is the
only shape that reaches this branch and is neither target VM.

## 9. What this does not do

- It does not fix what caused the observed command to hang. That is task
  13-R.2, which is separable and narrower: the health poll's age detector
  enters the recovery ladder at rung 1 (write `CRCR.CA`) instead of skipping to
  its end, so a Stop Endpoint that will not complete is aborted before anything
  asks for a controller reset.
- It does not make the latch unreachable. It makes it survivable.
- It has been read off the machine that reproduces the fault (E460,
  `run-13e.md` Finding T) and it worked three times out of three, with the
  whole bus re-enumerating after each one. That boot also found the attempt-cap
  defect described in section 7.

Two later boots (Findings U and V in `docs/contributing/runs/run-13e.md`)
settled what the hang itself was, and the conclusions bound what this record
may be read as saying:

- The 5 s command watchdog was never missing. It was being pre-empted, and by
  this driver. `CommandTimeoutArrivals` was 633 of `CommandsIssued` 635 in
  Finding U, equal at every interim dump: every watchdog arrives, and usbport
  drops none of them. What arrived first was the health poll's own age
  backstop. `XHCI_COMMAND_AGE_POLLS` was 64 polls, sized against a nominal
  500 ms period this machine does not have. Finding V measured the E460's poll
  at 36-80 ms, so the backstop stood at 2.3-5.1 s against
  `XHCI_COMMAND_TIMEOUT_MS` = 5,000, at or under the watchdog it was sized to
  sit 12 s behind. That is the whole of `CommandsTimedOut 0` across 76, 635 and
  123 commands on three boots. Task 13-R.3.5 repaired it by making every budget
  a duration in milliseconds on `PollClockMs`.
- An earlier reading that "the xHC does not answer a Command Abort in this
  state" was measured in the wrong window and does not stand as a claim about
  the hardware. The abort rung fired 4-for-4 in Finding T and 33-for-33 in
  Finding U, which is what aborting a command that was going to finish looks
  like. On the repaired build (Finding V) the escalation ladder read 0 across
  the board: 123 commands issued, 123 completed, nothing to abort at all.
- Finding S's headline, a Stop Endpoint that never completes, is refuted, and
  Phase 14 must not restate it in any form. What survives of Finding S is the
  latch, the missing owner and the batch 13-R census, which is what this whole
  record is built on and is untouched.
