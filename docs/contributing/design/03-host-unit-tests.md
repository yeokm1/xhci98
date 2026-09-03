# Host-Side Unit Tests for the xHCI Core - Design

Design doc 03. Applies from Phase 3/4 onward (the moment `src/` exists); this
doc pins the design so the source files are structured to be testable.

Status: built. Twelve suites and 13,554 checks at the time of writing, run by
`test\run-host-tests.cmd` on the Windows build host. A count written here goes
stale silently, so take the current number from the runner's own output.
Sections 3 and 5 below mark what exists and what is still owed. The suite runs before the
DDK builds in `scripts\build-driver.cmd`, and again in
`scripts\package\make-package.ps1` before install media is staged.

## 1. Why (the reboot-loop economics)

The most error-prone work in this driver is bit packing: TRB encoding,
cycle-bit management, ring wrap, context field placement, interval math. A
mistake there costs a full deploy cycle to observe (build, copy into the VM,
reboot the guest, reproduce, read a log), and it costs that twice, since every
checkpoint runs on both target VMs. Minutes per iteration, and the failure is
often the "silent death" mode `docs/contributing/failure-diagnosis.md` exists
for.

Every one of those functions is pure computation on bytes. Compiled on the
host with plain `assert`-style checks, the same mistakes surface in seconds,
before the driver is ever deployed. For ring/cycle logic especially, a host
test can exercise thousands of wrap iterations, which no amount of VM testing
does on purpose.

The QEMU trace oracle (`docs/contributing/build-and-test.md`, "QEMU xHCI trace events")
covers the next layer up (did the emulated controller accept what we
enqueued); host tests cover the layer below (are the bytes right before any
controller sees them). Together they leave only genuinely environmental bugs
for the slow VM loop.

## 2. Source-layout rule that makes this possible

Keep the hardware-logic core free of DDK dependencies:

- Pure functions in `src/xhci_mem.c`, `src/xhci_ring.c`, `src/xhci_caps.c` and
  `src/xhci_port.c`, and later the TRB-encode parts of `src/xhci_xfer.c` and
  the context builders in `src/xhci_slot.c`, operate on caller-provided memory
  and return values. They do not touch MMIO, IRQL, spinlocks, or usbport
  services. `src/sources` says so, because the rule is only worth anything
  while it holds for every file in that list.
- Register I/O stays in the callers (`xhci_pci.c`, `xhci_init.c`): encode
  first, then one call-site that writes the doorbell/register. (The "write the
  first TRB's cycle bit last" rule lives in the encode layer as an explicit
  final step the caller triggers, so it is testable too.)
- Where pure code has to read registers (the extended-capability walk is a
  linked list inside BAR0) it takes a reader function pointer (`XHCI_READ32`)
  rather than a mapped pointer. The driver passes a `READ_REGISTER_ULONG`
  wrapper; the tests pass an array. That keeps the walk in the pure core
  instead of duplicating its bounds logic in `xhci_init.c`.
- Types come from `src/xhci.h`, which under a host-test define
  (`#ifdef XHCI_HOST_TEST`) takes `ULONG`/`USHORT`/`UCHAR`/`BOOLEAN` from a
  tiny local typedef block instead of `ntddk.h`. C89 throughout, same as the
  driver, so MSVC 6.0's plain `cl.exe` can build and run the tests on the same
  machine that builds the driver (no separate toolchain).

This is a constraint on how `src/` is written, not extra machinery: if a
function needs a DDK header to compile, it does not belong in the pure core.

### The second tier: the sequence, against a synthetic controller

One suite is not over pure code. `test_init` compiles the driver's MMIO-facing
files (`src/xhci_init.c`, `src/xhci_evt.c`, `src/xhci_cmd.c`, `src/xhci_pci.c`
and, since the callback surface itself needed covering, `src/xhci_dispatch.c`)
and redirects their DDK primitives (`READ_REGISTER_ULONG`,
`WRITE_REGISTER_ULONG`, `KeStallExecutionProcessor`, and the three spin-lock
entry points) at hooks a test provides, declared in `src/xhci_compat.h` under
`XHCI_HOST_TEST` alongside the type block.

That does not weaken the rule above; it answers a different question. The pure
core is tested on what bytes it produces. The sequence has almost no bytes of
its own. What it has is an order, and a set of refusals that are only worth
anything if they happen before the controller has been touched. Both are
invisible to a suite that inspects finished state, and both are things this
project got wrong once each:

- An early `XhciWrite64` wrote the high DWORD before the low one, which spec
  5.1 (p.337) forbids in as many words.
- An early init sequence ran the port classification after the ownership
  handoff, halt and reset, so a controller whose topology this driver cannot
  serve was claimed and killed before being declined.

A model that logs every register access in sequence turns both into checks.
The boundary to keep in mind is that the model is this project's reading of
the spec, not a controller: it can only confirm the driver does what this
project believes it should, so it replaces no VM or QEMU-trace obligation.

The model has a second half for the same reason. The event ring is
common-buffer memory rather than MMIO, so the register hooks never see it;
driving the ISR and the DPC needs a writer as well, one that keeps its own
enqueue index and producer cycle as the controller does. That writer is also
what makes the DPC's drain bound reachable: it refills the ring on each ERDP
publication, which is a controller producing events while software consumes
them. Neither the bound nor "the pass after it collects what it left" is
expressible without it.

The model also has PORTSC, and the interesting part is what it has to get
right rather than what it injects. PP comes back asserted from HCRST (4.19.4,
p.295), which is what makes "the managed ports needed no write" the ordinary
outcome and the USB 3.x deassertions the only writes a healthy start performs.
A model that started ports unpowered would have made the driver look correct
while it silently left SuperSpeed ports live. The three port-power states it
can come out of reset in (all powered, none powered, USB 3.x only) are each a
legal controller: the second is the delay 4.19.4 permits until MaxSlotsEn is
written, and the third is the only one where both the assert and deassert
passes write, so it is the only one that can show their order.

Phase 5 found the shape of gap this model is most likely to have, and it is
worth naming because nothing in the suite could have failed on it. The model's
boot-attached-device knob posted a Port Status Change Event and never set
`CCS`/`CSC` in the PORTSC that event points at. That is not a controller: the
event carries a port number and nothing else, so every driver-side reading of
what changed comes from the register. It stayed invisible for as long as no
code read the register the event named; the moment one did, the first vector
written against it failed. The transferable form: when the model gets a new
fact, ask which register that fact is also visible in. A model that reports an
event without its register state makes a driver that reads the register look
broken and one that guesses look correct.

The rest of PORTSC followed: the bus reset a written `PR` starts, the `PED` a
written 1 clears, the link state only a write carrying `LWS` may change, and
the reset a power-off or a disable terminates silently, because "this flag
[PRC] shall not be set to '1' if the reset processing was forced to terminate
due to software clearing PP or PED to '0'" (Table 5-27, p.377). That last one
is the same lesson as the boot-attached knob in its sharpest form: a review
round reasoned that the termination reports itself, wrote a rule on it, and
wrote the model to match, so the suite went green with vectors specifically
about the follow-through, all of them agreeing with a fiction. When a model
gains a behaviour rather than a layout, the source has to be the spec text and
the citation belongs in the model.

Two further choices are worth stating. The reset completes in the register and
posts no event, as a connect does, so a vector can express "the reset finished
and the event has not been drained yet"; and `portResetHangs` is the input the
watchdog needs, because a deadline cannot be tested against a controller that
always meets it. The model also models one service rather than a register:
`UsbPortInvalidateRootHub` calls `RH_DisableIrq` straight back into the
miniport, and modelling that re-entry is what turns "never announce under the
driver's lock" from a comment into a double acquire the suite's never-reset
lock counter catches.

### The no-touch checklist, and why it is a checklist

Several driver entry points have paths that must touch no hardware at all: a
guard rejecting a NULL or unsignatured extension, or an early return before
the register bases are known. That property cannot be checked by looking at
the write log: it sees no reads, and no write whose address was rejected
before being recorded. It is checked by `hw_access_snapshot()` before the call
and `check_touched_nothing(&before, where)` after, which compare MMIO reads,
MMIO writes and configuration-space accesses across it. `flush_and_check()`
fuses the two for `XhciFlushInterrupts`, because a helper that only measures
is one a call site can use while forgetting to assert.

The rows are the registered callbacks, not the internal bodies. `test_init`
compiles `src/xhci_dispatch.c` (with `DriverEntry` excluded under
`XHCI_HOST_TEST`; `XhciFillPacketForTest` is the seam) and drives the pointers
through `XhciRegPacket`, because usbport reaches this driver only through
those pointers. Calling `XhciSuspendController` directly tests a guard the
real wrapper has already filtered, and cannot reach `xhciExtensionValid` at
all, which is where the trailing signature is checked. Both halves were
measurably untested before the table existed: removing all four
leading-signature checks from `src/xhci_evt.c` left the suite green, and the
trailing check had never been executed by any test.

The table exists because "did I get them all" was answered from memory several
times and was wrong each time, each round finding one more site. Adding a
callback means adding a row.

The row set is derived, not recalled. The procedure is one command,
`grep -n xhciExtensionValid src/xhci_dispatch.c`, plus `xhciStartController`,
which validates its own two arguments instead because usbport hands it a
zeroed extension. The count is checkable in a second rather than remembered,
and no count is written here on purpose: the last one recorded had drifted
badly as later phases registered more callbacks. Run the grep.

The registration packet is not the only surface. A function pointer this
driver hands to `UsbPortRequestAsyncCallback` is a callback usbport invokes
too, on a timer DPC, holding neither miniport lock, and it is not in
`src/xhci_dispatch.c`, so the grep above does not find it. The derivation
therefore has a second half: `grep -n UsbPortRequestAsyncCallback src/*.c`
names every such pointer, and each gets a row. They are driven the same way
the registered callbacks are, through the pointer the model recorded when the
driver armed the timer, with the model's copy of the context, because the
real service copies and the driver arms from a stack local.

There is a third half, for the same reason the first two exist. The transfer
contract probe (task 6-V.1) brackets its own arguments with
`xhciProbeExtensionUsable`, which checks both signatures because the probe's
state sits immediately before the trailing one, and it is reached from two
callbacks that have no `xhciExtensionValid` of their own, `CloseEndpoint` and
`GetEndpointState`, so neither grep above finds those sites. The third command
is `grep -n XhciProbe src/xhci_dispatch.c`, and its rows are the probe's
single row below. That row also carries a property no other row does: the
wiring itself is a claim, because the probe changes nothing the driver does,
so a call site that silently stopped probing would fail nothing else in the
suite. It failed nothing when measured, too; see the row.

| Registered callback | Wrapper guard | Body's own no-touch paths | Vectors |
|---|---|---|---|
| `InterruptService` | none, by choice (see below) | NULL/signature, `INITIALIZED` clear | uninitialized, NULL, suspended, bad leading signature, registered bad leading and trailing; forwarding: the registered ownership vector asserts the call read `USBSTS` ; decline gates: they apply only while `InterruptDeliverySuppressed` is set - after a quiesce or a terminal failure whose mask wrote nothing the ISR claims and acknowledges instead, and an undecoded controller (`HcInfoStatus` not OK) still declines without touching a register; and the proof itself is a read back, so a mask whose operands were derivable but whose writes the window swallowed publishes no proof - note it proves suppression, either enable clear, not both |
| `InterruptDpc` | full bracket | NULL/signature, `INITIALIZED` clear | uninitialized, NULL, suspended, bad leading signature; registered NULL, bad leading and trailing; forwarding: registered live, events consumed and `ERDP` republished with EHB released |
| `EnableInterrupts` | full bracket | NULL/signature, `INITIALIZED` clear | uninitialized, NULL, bad leading signature; registered NULL, bad leading and trailing; forwarding: registered live, `IMAN.IE` and `USBCMD.INTE` set with EHB released first; the `INTERRUPTS` Flags transition is observed between one controller-lock acquire/release pair; an all-ones `USBCMD` or `IMAN` refuses both unmask writes and counts one `InterruptUnmaskFailures`; and with `mmioReadsBeforeDead` armed at the operand count, the `USBCMD` write still carries the seeded RsvdP - proving the validated read is the one that supplied it |
| `DisableInterrupts` | full bracket | NULL/signature, `INITIALIZED` clear (`XhciDisableInterrupts`) | registered: bad leading, bad trailing, NULL, suspended, live; body: bad leading signature; forwarding: the registered live pair, `INTE` before `IE` on the hardware, with the `INTERRUPTS` Flags transition observed inside one controller-lock section; and the asymmetric mask refusals - a dead `IMAN` still lets the validated `INTE` clear through, a dead `USBCMD` still lets the `IE` clear through, a wholly dead window writes nothing, each counting one `InterruptMaskFailures` ; the registered live vector also covers a swallowed-write disable - delivery unproven, counted, escalated, and the controller lock released before the service call - because usbport stops calling the ISR after this callback returns |
| `FlushInterrupts` | full bracket | every path - it never touches hardware | live, mid-drain, suspended, NULL, bad leading signature; registered bad leading and trailing; forwarding: registered live, `InterruptFlushes` advances (the counter is in the body on purpose) |
| `InterruptNextSOF` | full bracket | every path - it never touches hardware, and never refuses on controller state either | registered live, NULL, bad leading and trailing, each asserting no touch and no count; forwarding: registered live, `InterruptNextSofRequests` advances (the counter is in the body on purpose, since there is no hardware effect to witness); plus a halted vector, because usbport asks for it without consulting the miniport and a body that declined there would make the counter measure this driver instead of usbport |
| `SuspendController` | full bracket | NULL, `HcInfoStatus` not OK | refused controller, NULL, registered bad leading and trailing; forwarding: registered live, `SUSPENDED` set, R/S clear, `HCHalted` set |
| `ResumeController` | full bracket, and returns `MP_STATUS_FAILURE` | NULL, `SUSPENDED` clear, `HcInfoStatus` not OK | not-suspended, refused controller, NULL, registered bad leading and trailing; forwarding: registered live, `ResumeReinits` advances and the controller is running again |
| `StopController` | full bracket | `XhciStopController`: NULL, none of `RUNNING`, `HW_RUNNING` or `INITIALIZED`; then the port pass's own two gates, which stay `RUNNING`-only plus a live `USBSTS.HCH` - a controller the run step merely found executing is one the quiesce must stop and the port pass must claim nothing on | registered live, NULL, bad leading and bad trailing; body second quiesce and refused controller; the registered live vector observes the final lifecycle-Flags RMW inside one controller-lock section; forwarding: `test_teardown_port_power` asserts the registered stop unpowers the ports, that every `PORTSC` write precedes the R/S clear, that no `HCRST` is written, and that a suspend does none of it |
| `StartController` | NULL extension/resources; initializes both bracket words | the whole preflight, steps 1-6 | registered success and both NULL refusals; body preflight refusals, each asserting no write of either kind |
| `Get32BitFrameNumber` | full bracket, and answers `0` | the guard - it reads MFINDEX only while the controller is admitted and running | registered NULL, bad leading and trailing, each answering `0`, touching nothing and advancing nothing; forwarding: since batch 6-B the three model-clock vectors the placeholder's row promised - the same MFINDEX twice answers the same number, eight microframes later answers one more (the vector that kills `return 0;`), and crossing the eleven-bit wrap advances by the real difference rather than going backwards. Plus `test_frame_number_stalls`, which is the property no MFINDEX vector can express: a halted controller, and one whose window has stopped decoding, must still advance the number, because usbport's post-open wait is uncapped and Win98 idle-suspends within half a second of every start |
| `CheckController` | full bracket | the guard, plus the health poll's own admission gate (`HcInfoStatus`, `INITIALIZED`, `ControllerFailed`) | registered NULL, bad leading and trailing, all asserting no touch; forwarding: registered live asserting read, but wrote nothing - plus `test_health_poll` for the behaviour (HCE and HSE escalate once each, an all-ones `USBSTS` is not a fatal-bit report, a quiesced or failed controller is not polled, and an outstanding command past `XHCI_COMMAND_AGE_MS` escalates once per crossing). Driven against a model clock: `poll_clock_prime` / `poll_after_ms` advance MFINDEX and poll, because the budget is a duration and not a call count; the vector that says so asserts that 32,000 polls costing no time abort nothing. `test_poll_clock` covers the clock itself: the per-poll advance, the intended undercount beyond one 2,048-frame MFINDEX lap, the stall that adds nothing and counts itself, and that `Get32BitFrameNumber`'s halted increments do not move it - the confusion the second axis exists to prevent |
| the command timeout (`UsbPortRequestAsyncCallback`, `src/xhci_cmd.c`) | NULL pointers only, then the full bracket under the driver's lock - it is not a packet slot, and it is not the DIRQL path either | NULL extension, NULL context, bad leading signature, bad trailing signature, an epoch from a previous start, a generation that is no longer current, `INITIALIZED` clear, and `ControllerFailed` set | each of those asserting no touch, driven through the pointer the model recorded, including one delivered between usbport's zeroing and the restart's first act; forwarding: a live one that asserts CA and arms the abort watchdog, and a second that escalates through `UsbPortInvalidateController` |
| `RH_GetRootHubData` | full bracket, and fills the caller's structure anyway | the guard - it writes no register in any state | registered NULL, bad leading and trailing, each asserting no touch and `NumberOfPorts == 1`, because usbport builds a descriptor out of that structure whether or not the callback filled it and zero is a ~1 GB nonpaged request; forwarding: registered live reports the managed port count (4 of 8), `PowerOnToPowerGood` 10, and individual over-current - plus a second controller with `HCCPARAMS1.PPC` set, which is the only thing that moves the power-switching bit |
| `RH_GetStatus`, `RH_GetHubStatus` | full bracket, and succeed with zeros | the guard | registered NULL, bad leading and trailing, each asserting `MP_STATUS_SUCCESS` with zeros - a nonzero return here abandons the whole status-change scan, so the refusal answer is the thing under test; NULL output buffer is the one nonzero either ever answers; forwarding: registered live answers self-powered, and `RhHubStatusQueries` advances |
| `RH_GetPortStatus` | full bracket, and succeeds with zeros | the guard, and a port index outside the managed range | registered NULL, bad leading and trailing, all asserting no touch and success with zeros; an out-of-range and a zero port index likewise succeed having counted `RhInvalidPort`; forwarding: registered live on a quiet port `check_read_but_wrote_nothing`, on a connected port reports connection/power/High Speed with `C_PORT_CONNECTION`, acknowledges `CSC` in hardware, and keeps reporting the latched change across repeated polls until the clear-feature callback takes it down |
| the twelve feature slots (`RH_Set/ClearFeature*`) | full bracket each | the guard, an unmanaged port, a controller not in service, and a port with an operation already armed | registered NULL, bad leading and trailing across the whole family asserting `MP_STATUS_NOT_SUPPORTED` and no touch; a USB 3.x port and a suspended controller refuse having touched nothing; forwarding: one golden written value per operation read out of the write log - `PP` as a written 1 and as a written 0, `PED` as a written 1, `PR` as a written 1 with no `LWS`, and `PLS = U3` with `LWS` on a port made enabled first (the spec's own precondition, refused when it does not hold) - plus `SetFeaturePortEnable`, which is a refusal rather than a gap because xHCI has no software enable, and a resume asked of a port that is not suspended, which succeeds having written nothing (the end state already holds) |
| `RH_SetFeaturePortReset` / `RH_ClearFeaturePortSuspend` (the asynchronous pair) | full bracket each | as above, plus no async timer service | `test_root_hub_reset` and `test_root_hub_resume`. Shared: the context carries epoch, hub port, generation and operation; the timer is armed outside the lock; a second operation on an armed port refuses as busy; and with `UsbPortRequestAsyncCallback` NULL the operation is refused before the write. Reset: the deadline interval, PRC through the event path completing and disarming it, the status query then reporting `C_PORT_RESET` with an enabled port at the decoded speed, a hung reset whose watchdog reports `C_PORT_RESET` anyway beside a port that did not enable, and `PR` never written back (RW1S has no stop). Resume: `PLS = 15` with `LWS` and never U0-to-start, the interval strictly longer than T(DRSMDN), an event mid-interval that does not complete it (the vector a first draft of the roadmap entry would have failed), the timer alone writing U0, `C_PORT_SUSPEND` appearing only after the refresh that follows it, a port unplugged mid-interval abandoning with no write, the device-initiated arm from the event path, and the health poll's sweep arming one whose PLC a status query had already consumed |
| the four synchronous operations against an armed port | as above | - | `test_root_hub_conflicts`: a power-off and a disable each preempt the operation in flight and proceed, its timer then touching nothing, and the port accepts a fresh operation at once; a suspend refuses, with the register forced to satisfy the suspend precondition first so the refusal cannot be that rule answering by accident. For an interrupted reset: the write ends it and the controller says nothing (`PR` clear, `PRC` not set - the spec forbids one here), so the driver reports `C_PORT_RESET` itself, counted as a preemption rather than a timeout, and announced by the callback. Plus the ordering, which no register log can show and which the model's latch-at-write probe exists for: nothing was latched at the instant the write went out, so the report is of something done rather than intended. A preempted resume is abandoned with no link-state write of the driver's own |
| the age detector (`XhciRootHubPoll`) | full bracket via `CheckController` | an unadmitted controller is not swept | `test_root_hub_conflicts`: an operation whose timer is never fired survives `XHCI_PORT_AGE_MS - 1` milliseconds and is retired on the crossing, a reset then reporting `C_PORT_RESET` and a resume getting its terminating U0 write; the port accepts operations again; and the operation armed since is not retired by the next poll. Plus: two thousand polls that cost no time retire nothing. Counted as sixteen polls this was 0.6-1.3 s on the E460, which polls at 36-80 ms (`run-13e.md`, Finding V), leaving 1.2-2.6x over the 500 ms deadline it exists to sit behind where 16x was intended. `test_port.c` carries the helper's own vectors, the clock wrap among them |
| the port timeout (`UsbPortRequestAsyncCallback`, `src/xhci_rh.c`) | NULL pointers only, then the full bracket under the driver's lock | NULL extension, NULL context, bad signatures, an epoch from a previous start, a generation that has moved, a hub port that no longer exists, and a controller not admitted | fired through the pointer the model recorded, with the model's copy: after a completion, after a suspend, and after a resume that rebuilt the shadow - each asserting no touch. That last one is why the claim precedes the read: a first version refreshed before claiming, and the refresh completed a reset a fresh caller had just started (measured) |
| the five change clears | full bracket each | the guard, and `Port = 0` | as above; forwarding: the connect clear takes the latched bit down and touches no register (the hardware bit went down when it was observed), `RhChangesCleared` advances, and `Port = 0` on the overcurrent clear succeeds having done nothing - Win2000 reaches it that way on ordinary hub traffic |
| `RH_DisableIrq` / `RH_EnableIrq` | full bracket | every path - a software gate that never touches a register | registered NULL, bad leading and trailing asserting no touch and no count; forwarding: the live pair moves `XHCI_EXT_FLAG_RH_IRQ` and counts each edge while `IMAN.IE` is asserted to be unchanged, and a change arriving while the gate is closed is still found by the next status query |
| `RH_ChirpRootPort` | full bracket | every path - success without bus action | registered NULL, bad leading and trailing refusing; forwarding: registered live succeeds, touches no register, and `RhChirps` advances |
| `ResetController` | full bracket | NULL, bad bracket | registered NULL and bad-trailing asserting the body did not run; forwarding: it masks the enables, marks the controller failed, and does not sleep (checked against the model's wait counter - the only vector available for an IRQL this suite cannot model). Then the enforcement: from an idle engine a submit is refused, an uncancellable watchdog and a queued DPC both touch nothing, `EnableInterrupts` puts nothing back, and the quiesce still halts |
| `QueryEndpointRequirements` (batch 6-B) | full bracket, and fills the caller's structure anyway | the guard - it touches no register and no state in any path | a bad bracket still leaves a usable pair, because usbport does not check whether the callback filled it and a zero `MaxTransferSize` reaches the transfer splitter's division before the open that would have refused |
| `OpenEndpoint` / `ReopenEndpoint` (batch 6-B) | full bracket, and `MP_STATUS_NOT_SUPPORTED` | the guard, anything that is not the default control endpoint, and every association the port shadow will not support | `test_slot_enumeration` and `test_slot_open_refusals`: the chain from a completed reset to a Slot ID taken only from the matching completion; an address-0 open with no enumerating port, with two candidates and no reset to choose between them, with a stale reset hint beside a live port, at a speed this driver cannot address, at an address no record holds, and with every record in use - the last counted apart, because a controller at its slot limit and a lost port association have opposite causes. Plus the reopen: the corrected MPS0 recorded but not adopted until Evaluate Context says so, the ring's enqueue, dequeue and cycle unchanged, and an illegal `bMaxPacketSize0` refused |
| `SetEndpointState` (batch 6-B) | full bracket | every state but `REMOVE`, which is recorded and ignored | `test_slot_endpoint_remove`: ACTIVE and PAUSED leave the binding alone; a REMOVE drops the binding and not the device - it arrives for a reopen as well as a delete, so treating it as a teardown would destroy a slot mid-enumeration; a REMOVE with work still queued completes it and counts `RemovesWithWork`; and a submit afterwards is refused for retry rather than completed against a record whose binding has gone. `test_slot_ep0_remove_superseded_handle` (issue 4, task 19.7): the two-handle restore XP's hub performs - the port reset again, EP0 reopened at address 0 through a second extension that resolves to the same record, SET_ADDRESS through the new handle still in flight - and then the REMOVE of the first extension, which closes that extension alone and counts `Ep0RemovesSuperseded`: the binding, the pending SET_ADDRESS (completed once, with success, when the command answers) and a submit through the live handle all survive, and the REMOVE of the handle the record is bound to still takes the existing path; `test_slot_ep0_remove_superseded_handle_late` is the same restore in the `p194` order, the old handle removed after the new one is addressed with a read queued, which must issue no stop and refuse nothing |
| `SubmitTransfer` / `AbortTransfer` (batch 6-B) | full bracket, and `MP_STATUS_NOT_SUPPORTED` | the guard, an unadmitted controller, and every device state that is not ready | `test_slot_transfers` and `test_slot_set_address_refusals`. The distinction under test is which refusal is used: a nonzero return leaves the transfer queued for retry (a chain still running, an MPS0 correction still outstanding), and failing a transfer means accepting it and completing it with an error (a failed or departed device, an address that is 0, past 127, or one another record holds). Plus the SET_ADDRESS interception itself - nothing placed on the ring, the transfer completed exactly once and only when the command answers - the doorbell rung at the right slot naming DCI 1, an event for an unopened slot, for an endpoint the device does not have and with a pointer above 4 GB all counted and dropped, and the labelled defensive re-entry vector - the model calls `SubmitTransfer` from inside `UsbPortCompleteTransfer`, which is a modelled vector rather than an observed contract: the re-entry was refuted from both binaries (`UsbPortCompleteTransfer` re-enters no miniport slot; `docs/contributing/design/05-locking-model.md` section 7 carries the derivation), so what this row asserts is that the guard holds if it ever happened, not that it does |
| the device command hooks (`XhciSlotCommandEvent` / `XhciSlotCommandLost`) | NULL and an owner of 0 | a completion whose TRB type is not the owner's | `test_slot_command_ownership`: a No Op completing while a record owns the engine advances nothing, the same completion with the right type does; a Slot ID of 0, past `MaxSlotsEn`, or one another record holds each fails the record without adopting it; a lost command fails the record rather than leaving it waiting, through the wiring (a quiesce with a command outstanding, using the suspend rather than the stop because the stop also releases every record and would mask a missing notification); and the age detector fires exactly on its crossing |
| what a lifecycle path may claim (batch 6-B) | - | a release asserted without evidence the controller let go | `test_lifecycle_release_evidence` and `test_restore_preconditions`, and the property under test is a claim rather than a write. Releasing a device record makes its Slot ID and its common-buffer blocks reusable, so a stop whose quiesce could prove neither a halt nor a lost bus master, and a resume whose reinitialisation then refused in the preflight, must abandon the record in place - Slot ID kept, DCBAA entry untouched, counted apart - and must not complete its queued transfers either, because a completion hands their mapped buffers back to usbport while the TRBs that name them are still on a ring the controller can execute. The other end is a vector too: `XhciSlotInit` runs after HCRST and drains them, so the transfer an abandonment held back is answered exactly once, on the far side of the reset. Plus the three preconditions a restore must not skip and did: bus mastering put back (a restored controller without it runs and delivers nothing), `CNR` clear before any operational write, and "is this still the same controller". Each refuses into the reinitialisation, which is the honest answer: the state may be intact and this driver cannot use it |
| the transfer-contract probe (batch 6-V, `src/xhci_probe.c`) | its own bracket, `xhciProbeExtensionUsable`, both signatures | every path - it performs no MMIO and calls no usbport service in any state | `test_probe_transfer_shapes`, `test_probe_gates`, `test_probe_endpoint_contract` and `test_probe_surface_wiring`. Three distinct subjects. The classification, asserted as pairs - ascending and disordered, tiling and gapped, mapped and unmapped - because a probe that only set a bit on the anomaly reports the same empty result whether the anomaly never happened or the probe never ran; plus a bit and the counter beside it asserted separately, which a mutation is why (dropping the `GAPPED` bit while leaving `ProbeSgGapped` alone failed zero checks). The gates, which are semantic rather than budgeted and therefore the kind that can silently stop firing: the same transfer five times announces once, a new property announces again, the two gates are independent, the key set saturates and counts what it dropped, and the element-dump budget is spent per start rather than per driver load. The wiring, and it is a row of its own for the reason the header says - deleting `XhciProbeTransfer` from the registered `SubmitTransfer` failed zero checks until `test_probe_surface_wiring` existed, because every other vector called the probe directly. Its site list is derived by grep, not recalled. Plus `check_touched_nothing` across both entry points |
| Save/Restore State (batch 6-B) | - | a controller that is not halted, a save with work outstanding, a restore with nothing saved | `test_save_restore` drives four controllers: the measured QEMU shape (CRS sets SRE - the default, because it is what both target VMs do and what every resume vector written before this batch was implicitly describing), a controller that keeps nothing, the conforming one where the device survives the power transition with its slot, and one that restores and then will not run. Plus the save declined while an endpoint still has work - which is what makes the one step of the spec's procedure this driver cannot perform sound - and the operation that never finishes, counted as a timeout rather than as either verdict |

`Get32BitFrameNumber` is the row that mattered most. It is the only registered
callback outside the lifecycle set that mutates the extension, no test called
it at all for a long time, and replacing its whole body with `return 0;`
passed the entire suite. Its own comment says usbport "waits for the number to
advance before confirming some transitions. A constant would therefore be
worse than useless", so the suite was blind to the failure the code
documents, on a callback usbport drives on every URB frame query and every
endpoint state change. The command engine's uncancellable timeout depends on
the same property.

The contract to pin is advancement with time, not with calls. The callback
returns `MFINDEX >> 3` with software rollover extension, and that may
legitimately answer the same value to several calls inside one 1 ms frame. A
vector asserting "successive calls strictly increase" would reject a correct
MFINDEX implementation, or push whoever writes it into adding a spurious
increment on top of the register to keep the suite green. So the model has an
MFINDEX a test advances explicitly (a counter rather than a stored register,
so it wraps at fourteen bits as the hardware's does) and the value checks are
these three:

- the same `MFINDEX` read twice -> the same frame number, which must be legal;
- `MFINDEX` advanced by eight microframes -> the frame number rises by one;
- `MFINDEX` wrapped -> the software-extended value stays monotonic across it.

Only the middle one kills `return 0;`, so it is the one that may not be
dropped. A constant answers the same value to two reads of the same `MFINDEX`,
satisfying the first; and a constant is trivially non-decreasing, satisfying
the third. Those two exist to pin what a correct implementation is allowed to
do, but a permissive vector cannot also be the proof that the callback
advances at all. That proof is the +8 microframe step.

One thing had to be unlearned on the way: the first draft of the wrap vector
asserted that a full 2,048-frame wrap advances the number by 2,048. That is
not knowable from an eleven-bit counter sampled at its period, since it is
indistinguishable from one that has not moved, so the vector crosses the wrap
by thirteen frames instead. The useful way to find that out was to watch it
fail.

A fourth vector belongs beside them that is not about the register at all.
`test_frame_number_stalls` covers what task 6-B.1 exists for: a halted
controller, and one whose window has stopped decoding, must still advance the
number. usbport's post-open wait is uncapped and Win98 idle-suspends within
about half a second of every start, so a reader that froze would hang the
enumerating thread at PASSIVE_LEVEL for good, and the failure would present
as a device that never enumerates rather than as a frame counter. The vector
also pins the re-sync: the first read after a stall re-establishes the delta
base rather than counting a difference against a counter that restarted at
zero.

`CheckController` was for a while the row that could not be mutation-checked,
and the reason it could not turned out to be the finding. The old body was
`XHCI_DBG_VALUE_CHANGED` several times over, which in a release build expands
to nothing (macro and argument both), and every suite here compiles the
release build, so the dereference the bracket protects existed only in the
debug build and deleting the guard outright left all 850 `test_init` checks
passing (measured).

Nobody asked why a health poll had no live effect to
assert. The answer was that it was not polling, while
`docs/contributing/implementation-invariants.md`, "Fatal Errors" had required
`USBSTS.HCE` and `HSE` on every invocation since before the callback was
written. A row labelled "un-mutatable" is a claim about the test; it can also
be a symptom of the code, and here it was.

The row now proves behaviour, and it needed a second assertion shape to do it.
The refusal vectors (NULL, bad leading, bad trailing) keep the full no-touch
delta. The live vector cannot: the callback reads `USBSTS` by design. So it
uses `check_read_but_wrote_nothing` (it must have read, and must not have
written), because the "did read" half is what makes a poll that silently
stopped polling visible, and a write-only assertion would have let the whole
health poll be deleted with the suite green. That is the general form of the
rule: ask what the code is allowed to do, and give the row the strongest
assertion that permits that and nothing more.

### Guards are only half of a row: the wrapper must be proven to forward

Every vector above was originally a refusal: NULL, bad leading signature, bad
trailing signature, assert that nothing happened. That pins
`xhciExtensionValid` and nothing else, because a wrapper that validates
correctly and then simply returns is indistinguishable from a correct one.

Measured before the forwarding vectors existed: deleting all six forwarding
calls from `src/xhci_dispatch.c` failed 3 checks, and all three belonged to
`DisableInterrupts`, the one row whose vectors happened to have been written
against a live controller. `XhciResumeController` reduced to
`return MP_STATUS_SUCCESS;` passed the whole suite, which is a driver reporting
a resume when nothing reinitialized the controller: no run, no port power,
`XHCI_EXT_FLAG_SUSPENDED` still set, and usbport's recovery for a failed resume
(stop and restart, ReactOS `power.c:192-212`) never taken, because it was told
there was nothing to recover from.

An `InterruptDpc` wrapper doing the same drains no events, so the ring fills
and EHB is never released, which is the deadlock the DPC design exists to
avoid.

So each guarded row also carries a live vector asserting an effect only the
body can have produced. Each of the six fails on its own (5, 4, 5, 5, 5 and 1
check respectively; all six together, 19).

The trap when writing one is to assert on state the wrapper itself sets.
`ext->InterruptEnables++` and `ext->Flags |= XHCI_EXT_FLAG_INTERRUPTS` both
live in `xhciEnableInterrupts` above its body call, so an assertion on either
is the same proxy mistake one level down. `XhciFlushInterrupts` is the
opposite case: its counter is in the body because the count is the whole
behaviour, which makes `InterruptFlushes` a legitimate forwarding witness
there and only there.

`InterruptService` is the documented exception, and the test pins it rather
than asserting against it: `xhciInterruptService` forwards straight to
`XhciIsr` without `xhciExtensionValid`, because the full bracket test is a
second dereference at the far end of the extension and the DIRQL path does not
pay for it on every interrupt of a shared line. So the leading signature is
all that guards it, and a bad trailing word does not stop it reading `USBSTS`.
That asymmetry reads as an oversight until you know it is a choice, so it has
a check of its own.

The full-bracket proof is compositional rather than assumed. Every guarded
callback gets a bad-trailing vector, proving that its wrapper calls
`xhciExtensionValid`; registered bad-leading vectors exercise the other branch
of that shared function. Removing the leading branch fails 15 checks. Start is
the other exception: usbport hands it a zeroed extension, so it cannot
validate a bracket that it owns initializing.

The rule for which assertion a site gets, because the answer is not "always
the delta": ask what the code under test is allowed to do.
`XhciInitController`'s preflight refusals read capability registers and
configuration space by design, and `XhciIsr` reads `USBSTS` to decide
ownership; those five sites assert `writeCount == 0` and claim "wrote
nothing", where a read delta would be simply wrong. Everything whose contract
is "touch nothing" takes the delta.

Two supporting counters make the guarantee whole. Each hook increments its
total at the very top, before address validation, so an attempted
out-of-window or misaligned access still counts as an access. And
`mmioAccessesOutsideTotal` is not reset by `hc_build()`; it is asserted once
at the end of `main()`, which is what catches an out-of-window access in a
test whose own per-test counter was zeroed at the top of that very test.

## 3. What gets tested on the host

"Suite" names the runner that owns the area; a dash means the area's code does
not exist yet and the row is an obligation on the phase named.

| Area | Suite | Checks |
|---|---|---|
| Struct layout | all | Compile-time size asserts (TRB = 16, ERST entry = 16, registration packet = expected size) - the C89 negative-array-size trick from `docs/usb-xhci-info/win98-wdm.md` |
| Registration/support structs | `test_packet` | Every `USBPORT_REGISTRATION_PACKET` field offset re-transcribed by hand from `docs/usb-xhci-info/usbport-miniport-abi.md` section 3, plus the extension-signature bracketing |
| Common-buffer carver | `test_membuf` | Declared size, region map, Table 6-1 alignment and no-cross-boundary rules per region, refusal paths, and what usbport actually allocates |
| TRB encoding | `test_ring`, `test_xfer` | Golden DWORD vectors for the No Op Command and Link TRBs, and every DW2/DW3 field macro. Expected DWORDs transcribed by hand from `docs/usb-xhci-info/xhci-data-structures.md` (which is spec-verified) - never generated by the code under test. Setup/Data/Status and Normal chains arrived with Phase 6 batch A and are `test_xfer`'s; the four device-lifecycle command TRBs arrived with batch 6-B |
| Cycle/ring logic | `test_ring` | Enqueue through >= 3 full wraps of a small ring, asserting cycle bit and Link-TRB handling each lap; event-ring dequeue simulation with a scripted "hardware" writer, including the empty-ring (cycle mismatch) stop condition and stale previous-lap TRBs; ring-full detection (Enqueue+1 == Dequeue rule, `docs/contributing/implementation-invariants.md` "Ring Full"); ERDP construction with EHB set and left alone |
| Completion classification | `test_ring` | Completion code x event position x ring kind against `XhciRingClassifyEvent`'s two independent flags. Covers the cases the spec separates by a single sentence each: a mid-TD Short Packet and a mid-TD Missed Service defer rather than retire (p.175, p.188); a tail halting error sets `CanRetire` and `NeedsRecovery` (p.214 + p.177); the same error code answers oppositely on an isoch ring, which never halts (p.177, p.184); command-only, transfer-only and isoch-only codes are refused on the wrong ring; vendor information 224-255 means Success; a failed command needs no recovery at all; and the sweep that reclaims a deferred TD when a controller drops its tail event (p.187) |
| Dequeue cycle state | `test_ring` | `XhciRingDequeueCycle` across four full laps, checked at every position against the Cycle Bit the ring actually holds there; the wrapped window where the two pointers are on different laps; and the flush-to-enqueue case, where the ring's own bit is stale and the producer cycle is the right answer (Table 6-67 p.455) |
| Context strides | `test_membuf` | Device- and Input-Context byte offsets at both strides (CSZ = 0 and CSZ = 1), including the Input Context's one-index shift and the DCI 1..31 bounds |
| PORTSC write masking | `test_port` | The neutral-value builder never carries PED/PR/WPR/LWS/RW1C/RsvdZ bits through a read-modify-write, and each builder changes only what it names (`docs/usb-xhci-info/xhci-programming.md` "Port Management"). Since Phase 5 task 3 also a golden value per operation - power on, power off, reset, disable, suspend, and both halves of a resume - because they are not interchangeable and look as though they are: PP off is a written 0 and PED (disable) is a written 1, reset is RW1S with no inverse, the PLS writes are the only ones that carry LWS, and a USB 2.0 resume is `PLS = Resume` then (20 ms later) `PLS = U0`, where the value that starts a USB 3.x resume is the value that ends a USB 2.0 one |
| The root-hub map and shadow | `test_port` | Phase 5 task 2's pure half. The logical-port map against a controller whose USB 3.x group comes first, which is the shape every arithmetic shortcut fails on, plus the reverse map answering "not mine" for an unmanaged port and a zero-managed-port topology being refused rather than reported. The shadow: a change acknowledged in hardware and latched for the hub class, the latch surviving repeated polling and only the matching clear taking it down, the SuperSpeed-only change bits latching nothing and being acknowledged anyway, an all-ones PORTSC latching nothing at all, `C_PORT_SUSPEND` as a completed resume derived from the previous link state, and every status bit translated to its hub-class position rather than passed through |
| Capability-register derivation | `test_caps` | `XhciDeriveHcInfo`: the three register-block bases the driver dereferences (operational at CAPLENGTH, runtime at RTSOFF, doorbells at DBOFF) against the mapped length. All-ones on each of the six registers separately, because an all-ones HCSPARAMS1 decodes to 255 ports and 255 slots and the field extraction cannot tell that from a dead bus; the exact boundary on either side of each window check; the two wrap cases an `offset + need <= mapped` bound would accept (`0xFFFFFFE0` is what an undecoded RTSOFF becomes after its RsvdZ mask); and that a refused derivation leaves the caller's structure untouched |
| Extended-caps parser | `test_caps` | Synthetic capability chains (USB2-only, USB2+USB3 paired, orphan USB3, a legal chain with nothing to manage, terminated/degenerate NEXT pointers, all-ones reads, truncated capabilities) -> correct port classification (`docs/usb-xhci-info/xhci-programming.md` "Port Topology Classification"), plus PSI speed decoding against a reordered table and the reader never leaving the declared window. Since Phase 4 task 3 it also replays both bare-metal fleet controllers from their XHCIQUAL reports (open question 2 below records what those reports do and do not carry) |
| Interrupt path | `test_init` | The ISR and DPC bodies (`src/xhci_evt.c`) against the same synthetic controller, whose model gained an event-ring producer to drive them. The ISR: claim only on USBSTS.EINT, acknowledge EINT before IMAN.IP (5.4.2 p.363), write IE as 0 in that read-modify-write (an earlier version carried IE through, which let it undo a mask it cannot be excluded from; the vector pins the reserved bits coming back out set while IE goes down out of the same word), leave every other RW1C status bit for `CheckController`, consume no event, and write nothing at all when unclaimed, uninitialized, or facing an all-ones USBSTS. Its all-ones IMAN case is separate and has its own vector: USBSTS readable so the interrupt is still proven ours while the interrupter window is dead, then the full retry budget (`XHCI_ISR_IMAN_READ_ATTEMPTS`) spent, one `IsrImanLiteralAcks` increment, and a bare literal `XHCI_IMAN_IP` write - the documented exception to the RsvdP rule. Without it the ISR writes `0xFFFFFFFD`, which is what the vector fails on. The DPC: per-type event counting, with a vendor-defined type (48-63, legal per Table 6-91 and "advance past and ignore" per 4.11.6 p.211) separated from a type that cannot be on an event ring at all, checked at both edges of the vendor range; ERDP published every 32 events with EHB = 0 and only the final write carrying 1, that final write taken even on an empty ring, a drain across the segment's end with the consumer cycle toggling, the bound reached against a model that refills while software drains and the next pass collecting exactly what it left, and the re-arm honouring usbport's `enableInterrupts` flag, running after EHB is released, and writing IP as 0 |
| Init sequence and its order | `test_init` | `XhciInitController` against a synthetic controller (section 2's second tier). The preflight - resources, the INTx gate, the capability decode, the port classification - refuses each of its cases having performed no MMIO write and no PCI configuration write at all; the first write of a successful start is the ownership claim; each 64-bit register takes its low-offset write before its `+4` (spec 5.1 p.337); ERSTSZ and ERDP precede ERSTBA (4.9.4); `CONFIG` precedes DCBAAP; exactly one write sets `USBCMD.RUN` and it is the bit alone, after ERSTBA; no write sets `USBCMD.INTE` or `IMAN.IE`; `USBSTS` is only ever written as RW1C bits; and a controller that changes identity across the reset is refused by the capability re-derivation or by the post-reset port-map re-parse according to which half moved - including a changed Supported Protocol range, a changed slot type, and a single changed PSI DWORD with everything else identical |
| Running, port power, and stopping | `test_init` | Task 5's half of the sequence. Every PORTSC write happens after the R/S write (5.4.8 p.370) and carries no PED/PR/LWS/change bit; every USBCMD write preserves the RsvdP fields the model seeds (5.1.1 p.339); managed ports end powered and USB 3.x ports end unpowered, which on a controller that came out of HCRST with PP asserted everywhere means the deassertions are the only port writes there are; a controller that comes back with the two halves the other way round is the vector that shows assertions precede deassertions (4.19.7 p.303); the 20 ms power-stable delay is taken when and only when a port really transitioned (5.4.8 p.371); a port whose PP ignores writes is counted and does not fail the start. Refusals: R/S is never written to a controller that is not halted (5.4.1 p.359), and a controller that takes R/S but never clears HCHalted fails the start and has the half-start undone. The quiesce clears `USBCMD.INTE` before `IMAN.IE`, halts, leaves IP pending, never issues HCRST (5.4.1 p.360), is idempotent, and touches nothing at all on a controller this driver never programmed. "Never started" is the wrong test and was the wrong word here: a run step that reads a valid `USBSTS` with HCHalted clear has observed the xHC executing, and by then DCBAAP, CRCR, ERSTBA and ERDP are all programmed and usbport reclaims the common buffer the moment the start is reported failed - so that case publishes `XHCI_EXT_FLAG_HW_RUNNING`, the quiesce halts it (or clears BME, or fails closed), and its vector asserts the controller really ends halted. The untouched case is the preflight refusal, which wrote nothing at all |
| Port power that lags, and the VBus it can drop | `test_init` | The model's PP can be made to trail a write by N reads in either direction (footnote 91 to Table 5-27, p.375). A lagging controller must not have its ports counted as failures, the wait must stay bounded when a port never arrives, and - the property that ordering the writes does not give - the model counts every time a USB 3.x port is deasserted while its USB 2.0 companion is not yet reporting power, which is a connector whose VBus really drops |
| Proving DMA has stopped | `test_init` | The two cases where halting is unavailable, against a modelled PCI Command register: a controller that will not halt, and an MMIO window that has stopped decoding while config space still answers. Both must fall back to clearing Bus Master Enable and confirming it by re-reading - a Command register that takes the write and keeps BME set is its own vector, since nothing reports an error there. When neither proof is available the quiesce returns failure, counts it, and leaves whichever running flag admitted it up so a retry still has something to try; a later start restores the BME it took. The admitting flag is `XHCI_EXT_FLAG_RUNNING` when this driver wrote R/S and `XHCI_EXT_FLAG_HW_RUNNING` when the run step found the controller executing - a case with its own vector, because the quiesce must stop such a controller (its pointer registers are already programmed and usbport reclaims the buffer on a failed start) while `xhciUnpowerPorts` must still claim nothing |
| Suspend and resume | `test_init` | The suspend clears R/S and the controller really halts; it masks `USBCMD.INTE` before `IMAN.IE` itself, including when there is nothing to halt, because a suspend that leaves IE set while dropping the flag the ISR tests produces an unclaimable interrupt; while suspended an ISR or DPC touches no register. The resume reinitializes: a controller whose every operational register came back at its default still resumes, with DCBAAP, CRCR, ERSTBA, CONFIG and port power all reprogrammed - which is what makes CSS/CRS deferrable rather than merely deferred. It restores the interrupt enables when usbport had them on and leaves them masked when it did not, reports failure with the step recorded when the reinitialization cannot succeed, counts a suspend that could neither halt nor drop bus mastering, and does nothing at all on a controller that was never programmed |
| Bus mastering: the gate | `test_init` | Asked of the hardware on every start, not of the driver's record of what it did. BME clear with `BusMasterCleared` unset - a power transition, or firmware that never enabled it - refuses at `XHCI_INIT_STEP_BUS_MASTER` with the Command register in `InitStatus`, and does so as a preflight refusal: no MMIO write, no configuration write, the Command register left exactly as found, because repairing somebody else's BME is not this driver's to do. An all-ones Command register refuses, and so does an unreadable one - with `InitStatus` set to `0xFFFFFFFF` so a release build can tell a failed service from a register that read as all-ones. The INTx gate's opposite rule is pinned in the same suite by a knob that fails only the Interrupt Pin read, so the two are checked to differ on evidence rather than by accident: that gate has `USBPORT_RESOURCES_INTERRUPT` as a second witness, and bus mastering has none. The ordinary bus-mastering controller passes with no configuration write at all |
| Bus mastering: the restore | `test_init` | The mirror of the quiesce's fallback, and a refusal rather than best-effort: a write that fails, and a write that returns success while BME stays clear, each fail the start at `XHCI_INIT_STEP_BUS_MASTER_RESTORE`, leave `BusMasterCleared` set, and - the property that matters - never write R/S. An unreadable Command register refuses one step earlier, at the gate, which already knows the bit is wrong and that it cannot be read to put right |
| usbport's interrupt callbacks | `test_init` | Task 6, against a model whose interrupter now raises `IP`/`EINT`/`EHB` for an event that finds the event handler idle, treats EHB as the RW1C bit it is rather than part of the ERDP pointer field, resets all three at HCRST, and can report a device that was already attached when the controller began running - posted at the R/S write, which is the only faithful position for it and is before `EnableInterrupts`. The enable releases Event Handler Busy at the unmoved dequeue pointer first, while both enables are still clear, then sets `IMAN.IE` and `USBCMD.INTE`; it acknowledges neither `EINT` nor `IP`, consumes no event, and is a no-op on a NULL or uninitialized extension. The disable writes `USBCMD.INTE` before `IMAN.IE`, acknowledges nothing and does not touch ERDP, and the pair round-trips. The flush touches no hardware in any state, and every state is measured the same way - `flush_and_check` brackets the call with MMIO read, MMIO write and configuration-space counts and asserts all three deltas in one step, so a call site cannot take the measurement and skip the assertion. The mechanism underneath it (`hw_access_snapshot` / `check_touched_nothing`) is shared by every "this call must touch no hardware" vector in the suite - the ISR and DPC guards, uninitialized and suspended; `EnableInterrupts`; the idempotent quiesce and the quiesce and suspend of a refused controller. The distinction that decides which assertion a site gets is what the code under test is allowed to do: a preflight refusal and the ISR's ownership check both read by design, so those keep `writeCount` and claim "wrote nothing". The rest had the same weakness for the same reason: they asserted on the write log, so a read through a zeroed `HcInfo` - the hazard the ISR's guard comment names in as many words - was invisible, and the out-of-window sentinel could not help because a zeroed `HcInfo` yields offset 0, which is inside the window. That uniformity is the property: while the measurement lived only in the mid-drain vector, an implementation that touched hardware only while `INITIALIZED` was clear passed the whole file, and that is the state its real caller finds. The states are live with an interrupt pending, suspended with `INITIALIZED` down (the D0 power completion's own view), a NULL extension, and called from inside a drain: the model delivers it from an intermediate ERDP publication, so the DPC is between events, its dequeue pointer has already moved and Event Handler Busy is still set. That last case is the interleaving the lockless call site permits and the only one where a stray `ERDP` write does damage; a flush called before or after a DPC meets a ring at rest. The pass must still drain every event and end on its own final ERDP write. "Touched nothing" is measured as MMIO read, MMIO write and configuration-space deltas taken either side of the call itself, not looked for in the pass's write log: a log scan can only find accesses the test thought to look for, and a flush that read `ERDP` and republished it with EHB = 0 - a stale pointer with no release - satisfied every log-based assertion while doing the one thing the callback must not do (measured: it passed the whole suite). Reads count on both buses, since a read leaves no trace in any log. The MMIO counters advance at the very top of each hook, before address validation, so an attempted out-of-window or misaligned access is still an access - the bounded write log rejects a bad address before recording it, which is a second thing a log cannot see. The pass's own "exactly one write releases Event Handler Busy" check stays, but as a statement about the DPC's discipline rather than about the flush. A single-threaded host still cannot make the flush read a stale dequeue pointer - it sees the state the DPC just updated - so that half remains a review property. The call is counted every time, since the counter is the runtime confirmation that the call site was read correctly. The resume restores the enables through the enable path, so the interrupter its own reinitialization left busy is released too, and a flush arriving between the suspend and the resume changes none of it |
| The registered callback surface | `test_init` | The pointers `xhciFillPacket` hands usbport, driven through `XhciRegPacket` rather than through the bodies underneath - the only vectors that execute the `xhciExtensionValid` wrappers at all, and the only ones that can reach its trailing signature check. Two halves, and the checklist above is the row set: each guarded callback refuses a NULL, a bad leading signature and a bad trailing signature having touched nothing, and performs an effect only its body can produce when handed a live extension. Plus `Get32BitFrameNumber`, which nothing called at all until a constant `return 0;` was measured passing the whole suite; its live vector pins the placeholder counter's exact values rather than "successive calls increase", because the eventual `MFINDEX >> 3` may answer the same value twice within a frame |
| The asynchronous command engine | `test_init` | Task 7, against a model that grew a command-ring consumer - its own dequeue pointer and consumer cycle state, executing on a doorbell only while the controller runs, following Link TRBs, and able to hang, abort, skip the Command Aborted event, report a stopped pointer the ring cannot hold, or refuse to negate CRR - plus usbport's async timer service, which copies its context because the real one does and the driver arms from a stack local. The No Op self-test end to end: one doorbell of value 0 after the R/S write, a watchdog armed at 5 s with the whole context copied and the interior lock released, then the completion matched by the TRB pointer the submit reported. One command at a time, with `BUSY` for a second one and no doorbell rung; `NOT_READY` on a stopped controller. Matching refuses a zero pointer, a nonzero high DWORD and a duplicate, none of which may move the ring; a failing completion code still retires (a command ring has no endpoint to halt) and an unclassifiable one still moves the dequeue pointer, because leaving a TRB behind the enqueue pointer shrinks the free count permanently. The ladder: CA written as the bit alone with no high-DWORD write, the abort's own watchdog, the two events, the doorbell staying silent for the whole `ABORTING` state, the documented variant where Command Aborted never arrives, a stopped pointer the ring cannot hold escalating instead of repositioning, and a CRR that never negates reaching `UsbPortInvalidateController(RESET)`. Stale callbacks: after a stop, while uninitialized, and - the one a review would not have found - a watchdog belonging to a command that already completed, arriving while the next one is in flight |
| Surviving a restart | `test_init` | The model performs usbport's own `RtlZeroMemory` over the miniport extension before every start, because usbport does - and `run_init()` drives the registered `StartController` rather than the body underneath it, so the per-start initialization that only the wrapper performs actually runs. Both were review findings: entering underneath the wrapper meant every init vector ran against an engine whose start epoch had never been issued, and a model that carried state across a restart would have hidden the hazard entirely. The vector starts a controller, keeps its watchdog's copied context, stops, starts again, and fires the old callback: it must touch no register, take no lock, and leave the new start's command outstanding. The assertion that matters is that the two starts' generations are equal: that is the reason a generation kept in the extension cannot separate them, and it is what would fail if the epoch were ever folded back into it |
| The miniport's interior lock | `test_init` | Not what it protects, which a single-threaded host cannot show, but where it is taken. `KeInitializeSpinLock`/`KeAcquireSpinLock`/`KeReleaseSpinLock` are redirected at hooks (`src/xhci_compat.h`) that count a nested acquire and a release-while-unheld - on the target the first is an instant self-deadlock at DISPATCH_LEVEL and the second lowers an IRQL nobody raised - and that record every usbport service called while the depth is nonzero. Each of the three has a never-reset twin asserted once at the end of `main()`, the same net `mmioAccessesOutsideTotal` is. Creation is pinned separately, by the registered `StartController` vector: the lock is created there and nowhere else, because a path that re-initializes it could be re-initializing one another CPU's timeout callback is holding |
| Multi-TD groups | `test_ring` | Phase 6 batch A. A control transfer is two or three TDs published as one store (spec 6.4.1.2 p.429), so `XhciRingEnqueueTdGroup` is checked for: each TD separately recoverable from the Chain flags on the ring afterwards; one event on the group's last TRB retiring all of them; a declared TD boundary the Chain flags do not mark refused; a partition that does not sum to the TRB count refused; and the Link TRB's Chain flag at a crossing taking the preceding TRB's value - set mid-TD, clear between two TDs of one group, which is the distinction "are there more TRBs to write" gets right only while a group is a single TD |
| Multi-TRB length sums | `test_ring` | `XhciRingSumTrbLengths`, the spec's own arithmetic for how many bytes a TD moved (p.175): the sum through each position, the Link TRB stepped over rather than added, only the length field of DW2 and not TD Size or Interrupter Target, a backwards range refused rather than answered the long way round, and any end that is not outstanding refused - a sum across a retired TRB is a previous lap's leftovers |
| Contexts | `test_ctx` | Phase 6 batch B, and where Low Speed lives: the checkpoint's LS leg was struck because no QEMU peripheral declares it and since Phase 5 task 7 every connected port is reported as High Speed, so LS survives only in the slot context and in EP0's Max Packet Size. Golden Slot Contexts for FS, LS and HS on named root ports; the hub tier fields Phase 7b will set, encoded now so a shift typo in a field nothing yet uses is caught here; EP0 at all four legal packet sizes with DCS in bit 0 of the dequeue word; and the two Input Control Context shapes (A0 + A1 for Address Device, A1 alone for the MPS0 correction). Every field that would be masked by a builder trusting its caller is a refusal with a poison-pattern check that nothing at all was written - and the "exactly eight DWORDs whatever the stride" property is checked at both context sizes through the offset accessors, because writing `ContextSize / 4` words overwrites the next context at CSZ = 0 and clears reserved bytes at CSZ = 1, and a single build shows only one of the two |
| Control TD construction | `test_xfer` | Phase 6 batch A, golden control words per stage. The no-data, IN-data and OUT-data shapes with their TRT, DIR and Status-DIR values taken from Table 4-7 by hand; the Setup TRB's immediate data packed from the 8 SETUP bytes; exactly one IOC in the whole transfer, on the Status Stage TRB (p.430); ISP on every IN data TRB including the last; Chain set within the Data Stage TD and clear at both TD boundaries; and the TD Size formula (4.11.2.4) worked through by hand for a three-fragment TD and at its 31-packet saturation |
| 64 KB splitting | `test_xfer` | SG fragments that start mid-page and cross 64 KB physical boundaries produce TRB chains where no TRB buffer spans a boundary (spec 6.4.1 note), including the three-boundary case, a fragment that ends exactly on a boundary and is not split, the last usable block at `0xFFFF0000` where the distance-to-boundary subtraction wraps, and one byte past it refused |
| The scatter/gather walk | `test_xfer` | The whole list consumed in `SgOffset` order rather than array order - the defensive answer to the one thing task 6-V.1 measures - plus every way a list can fail to tile `[0, TransferBufferLength)`: a gap, duplicate offsets, an element past the end, an element that overruns, a zero-length element, an empty list for a nonzero transfer, and a nonzero physical high DWORD, which usbport does not mask and this driver therefore checks |
| Transfer completion | `test_xfer` | Which event ends a transfer, and with what length. The ordinary single-event success; a short packet on the data stage deferring to the Status Stage event and fixing the length there; the multi-TRB short packet, whose answer is the TRB sum minus the residual and not `requested - residual` (the wrong formula's number is named in the check); an error completing the transfer wherever it lands and asking for recovery; codes 26-28 as cancellation whose length field is not a residual; the sweep that completes transfers a retire reclaimed without an event of their own; a group spanning the wrap; and every rejection - ED = 1, a foreign Slot ID or DCI, a zero or off-ring pointer, an unassigned code, an unowned index - leaving the queue exactly as it was |
| Completion mapping | `test_xfer` | Every xHCI completion code 0-255 through `XhciXferCodeInfo`, with the USBD_STATUS values hand-typed a second time from the Windows 2000 DDK's `usbdi.h`; plus a cross-check that the set it accepts is exactly the set `XhciRingClassifyEvent` accepts for an endpoint ring, so the two decoders cannot drift |
| Context builders | - (Phase 6 batch B) | Slot/Endpoint context DWORDs for known inputs, at both context strides; Input Control Context A/D flags for Address Device / Configure Endpoint / Evaluate Context shapes |
| Interval math | `test_ctx` (Phase 7a) | usbport `Period` -> xHCI Interval per speed. Not raw `bInterval`: the miniport never sees it, usbport having already applied the `docs/usb-xhci-info/xhci-data-structures.md` conversion, so testing against that table would pin a conversion this code must not perform. `Period` is a power of two 1..32 counting microframes on High Speed and frames on Full/Low Speed (`docs/usb-xhci-info/usbport-miniport-abi.md` section 5), giving Intervals 0-5 / 3-8 / 6-8 (LS is floored at 8 upstream); out-of-contract values are refused, not repaired |
| SET_ADDRESS interception | - (Phase 6 batch B) | The EP0 setup-packet classifier flags exactly bmRequestType 0x00 / bRequest 0x05 and nothing else; address-map lookups stay correct when usbport and xHC addresses coincide |
| The hub topology graph | `test_topo` (Phase 7b task 7b-A.1) | `src/xhci_topo.c` in isolation. Setup packets are hand-built from the values the batch 7b-V0 QEMU trace measured on the wire rather than from the hub-class specification, which is the difference between matching every hub and matching none: `GET_DESCRIPTOR(Hub)` at `wValue = 0x0000`, the port selectors 4 (reset) and 8 (power), `GET_STATUS(port)` at `0xA3`. Route String nibbles are hand-computed numbers typed out in the vector, never recomputed through the shift the code uses - the `test_ctx` rule - and the five-tier ceiling refuses rather than truncates. Plus: a self-consistent hub descriptor folds while a malformed one is counted and dropped; the descriptor type byte is recorded rather than required, so a wrong constant cannot silently refuse every hub; a connect bit going 1 -> 0 is a disconnect and 0 -> 0 is not; a claim is spent by the ask; a pruned hub takes its subtree and any claim naming it; and the claim identity (`Claims + ClaimsUnarmed == calls made`) is a never-reset net with its own "the net saw something" twin, which is task 7b-A.1.0's mechanism in a third place |
| Mixed-traffic teardown | `test_init` (task 8-A.3) | Control + interrupt + bulk IN + bulk OUT outstanding at once, then unplugged: four completions, every busy endpoint stopped before the Disable Slot, all three pooled rings back only afterwards, and a second device that gets all three again - the check that would fail on a leak visible only on the second device. Task 8-A.2's ten-code matrix runs beside it on a bulk endpoint with a transfer queued behind the failure: completed exactly once with the mapped `USBD_STATUS`, nothing placed on a ring the xHC is not executing, the survivor's TRBs kept |
| The mid-TD deferral's settle | `test_init`, `test_xfer` (task 9-0.2) | Three vectors the mutation sweep showed were missing, each of which had failed zero checks: `test_slot_bulk_short_packet_bounded_pass_does_not_settle` (the model's `refillOnErdp` controller, with the event ring primed to `XHCI_ERDP_PUBLISH_EVERY` first, since the refill fires only on a publication); `test_slot_bulk_short_packet_deferral_lost_to_teardown` (the fold's gate and its net are the same test, so a missed fold forces the walk that detects it); `test_settle_refuses_when_the_ring_moved` (the chain corrupted between the arm and the settle - its `!CanRetire` neighbour is not independently reachable and is recorded as consistency rather than claimed) |
| Isochronous submission | `test_iso`, `test_init` (task 9-A.1) | Two mutations worth knowing the weight of: reading the Frame ID window as a magnitude comparison instead of a distance fails 9 checks (right for half of every second, wrong for the other half), and giving an isochronous endpoint an ordinary endpoint ring fails 10 - the ring kind is behaviour, not a label, because it decides which completion codes the classifier accepts and whether an error reads as a halt on a pipe that never halts |
| The topology graph's wiring | `test_init` | Whether anything reaches the graph, which is a different question from whether the graph works and is the one a mutation kept answering "no" for the probe. Every call goes through `XhciRegPacket`: a `GET_DESCRIPTOR(Hub)` submitted and completed with bytes at the SG list's `MappedSystemVa`, so the reply path is exercised end to end; a port reset arming a claim that an address-0 open then spends; a refused transfer that must not be snooped, because usbport resubmits it and a reset counted twice reports a serialization violation that never happened; a failed transfer whose buffer must not be folded, in the one shape that reaches the branch (bytes moved, then an error - a plain error reports zero bytes and is refused by the length test instead); and the three ways a device's node leaves - a connect-change teardown, a disown, and an abandoned record that keeps its Slot ID because nothing proved the xHC let go but gives up its node anyway, since a node is bookkeeping keyed on an address usbport is free to reuse |

Golden vectors are data, so each VM- or hardware-found ring/TRB bug gets a
regression vector added in the same change that fixes it. The host suite
accumulates the project's bug history.

## 4. What stays out

- MMIO access, barriers, doorbell ordering: QEMU trace events verify these.
- IRQL discipline, spinlocks, usbport callback interplay: Phase 3 spike
  logging and the VM.
- Timing, interrupt delivery, BIOS handoff: QEMU/real hardware only
  (`docs/contributing/design/01-hardware-qualification-tool.md`).
- Anything requiring the registration packet ABI at runtime: the binary
  validation procedure in `docs/usb-xhci-info/usbport-miniport-interface.md`
  owns that.

## 5. Shape of the harness (as built)

- `test/` directory beside `src/`, not referenced by the DDK `sources` file,
  so the driver build stays untouched. The dependency runs the other way:
  each suite compiles the `src/` file it covers.
- One runner per pure-core module, plain C89, no framework: a `CHECK(cond)` /
  `CHECK_EQ(got, want)` pair that prints file/line and counts failures;
  process exit code = failure count.

  | Runner | Covers |
  |---|---|
  | `test_membuf` | `src/xhci_mem.c` - the carve and the context strides |
  | `test_packet` | `src/xhci_usbport.h` - the miniport ABI declaration |
  | `test_ring` | `src/xhci_ring.c` - TRBs, rings, cycle, event drain |
  | `test_caps` | `src/xhci_caps.c` - capability walk, ports, speeds |
  | `test_port` | `src/xhci_port.c` - PORTSC write construction, the logical-port map, and the port shadow. Links `src/xhci_caps.c` too since Phase 5: the map is derived from the port classification, so it asks `XhciPortIsManaged` which ports it may present rather than re-deciding it |
  | `test_init` | `src/xhci_init.c` + `src/xhci_evt.c` + `src/xhci_cmd.c` + `src/xhci_rh.c` + `src/xhci_pci.c` + `src/xhci_dispatch.c` + `src/xhci_slot.c` + `src/xhci_probe.c` - the init sequence, the run/port-power/quiesce paths, the interrupt path, the command engine, the root-hub callback family, the slot/endpoint lifecycle, and the registered callback surface, with their access order, against a synthetic controller. `DriverEntry` is excluded under `XHCI_HOST_TEST`, so the file contributes its wrappers and the packet without its DDK dependencies. It also links `src/xhci_port.c`, because the port-power step and the root-hub callbacks compose their PORTSC values there rather than by hand |
  | `test_xfer` | `src/xhci_xfer.c` - the TD builder, the pending queue, and the Transfer Event classifier including the multi-TRB length sum |
  | `test_iso` | `src/xhci_xfer.c`'s isochronous half (task 9-A.1) - the Isoch TRB's own fields including TBC and TLBPC, the Valid Frame Window across both of its wraps, and the per-packet completion write-back into usbport's parameter block. Its own runner rather than more `test_xfer` cases because almost nothing the bulk path does is right here: an isoch endpoint never halts, a Missed Service Error is the pipe resynchronising, two completion codes name no TD at all, and the completion carries a status per packet |
  | `test_ctx` | `src/xhci_ctx.c` - Slot and Endpoint Context encodings at both strides, against the transcribed doc |
  | `test_topo` | `src/xhci_topo.c` - the snooped hub graph, Route String construction, and the TT triple |
  | `test_log` | `src/xhci_log.c` - task 11-V.7's optional log ring, on its own because it is pure by construction and has to stay that way: the ring, its wrap and which byte it loses, the record cap, the FIFO chunked drain and the flush verdict are all here, and the file I/O is not. Two accounting nets in task 7b-A.1.0's shape (`Appends + Suppressed == calls made`, and bytes conserved across drains, drops and what the ring still holds), each with a never-reset twin. The half it does not cover is named rather than implied - `ZwCreateFile`/`ZwWriteFile`, the `KeGetCurrentIrql` guard and the registry read live in `src/xhci_dispatch.c` and are driven through `test_init`'s stand-ins |
  | `test_desc` | `src/xhci_desc.c` + `src/xhci_ctx.c` - the configuration-descriptor snoop (task 9-A.2): which EP0 setup packets carry one, the descriptor walk and its refusals, the isochronous `bInterval` table with its alternate-setting conflicts, and the Table 6-12 conversion the recovered value feeds. It links the context encoder because the walk recovers a `bInterval` and `XhciBuildEndpointParams` is what turns it into the number the hardware sees, so a suite that stopped at the table would have tested a lookup and left the arithmetic to a VM run. The property the driver's caller depends on and cannot check for itself is here too: the fold reads usbport's mapping through a 32-byte window, so the same descriptor fed at every chunk size from 1 upwards must produce the same table |

- Build+run is one `cl.exe /W3 /Za` command per runner on the Windows host,
  using the same MSVC 6.0 the driver build uses. `/Za` is not decoration: it is
  the C89 dialect gate (no `//`, no mid-block declarations) that the DDK build
  would otherwise be the first to apply, an hour later.
- `test\run-host-tests.cmd` runs every suite even after one fails (a second
  failure costs milliseconds here and another build cycle anywhere else) and
  distinguishes a blocked launch (exit 2) from a failing one. On a host with
  Smart App Control, a freshly linked unsigned exe is blocked roughly one
  launch in three with a nonzero exit that is otherwise indistinguishable from
  a failed assertion (`docs/contributing/lessons.md`, "Smart App Control").

  The discriminator is the reputation block's own message in the captured
  output. A binary that starts and then dies also prints no
  `checks, N failures` line, and that case is classified FAILED, not blocked;
  otherwise a deterministically crashing vector reads as "inconclusive, just
  run this again" on every run. Anything that classifies a run by what it did
  not print needs the same question asked of it: what else produces that
  silence?
- Run before every deploy to either VM; a failing vector is cheaper than a
  guest reboot, and far cheaper than a Win2000 bugcheck-and-recover cycle.
  This is mechanical rather than a convention: `scripts\build-driver.cmd` runs
  the suite before the DDK builds, and `scripts\package\make-package.ps1` runs
  it again, with the import gate, before staging install media.

### Two properties of the model itself that a vector must be written against

The model stamps a command's completion code when the command is submitted,
not when its event is processed. `hwCmdCompletionCode` is read at the moment
the driver enqueues the command, so a vector that sets it after the call which
issues the command is testing the previous value, and one that sets it after
the call and before `deliver_events()` is testing nothing it thinks it is. Set
it before the call that issues the command, and restore it before the
completion is processed if only that one command is meant to see it; several
batch 7a-B vectors set it between two commands for that reason. This has
produced silently vacuous vectors more than once.

The model does not enforce command preconditions the hardware does. It does
not track xHC slot state and does not implement Address Device's own
algorithm; it posts the globally selected completion code unconditionally. So
a vector can drive an illegal sequence to Success and stay green for ever,
which is how the retained-slot re-enumeration defect (roadmap, batch 7a-B)
survived review: BSR = 1 is valid only from the Enabled slot
state, the driver queued it against an Addressed or Configured one, and no
vector could notice. A green vector over a command sequence is evidence about
this driver, not about the sequence's legality; that has to come from the
spec.

The driver side of that example is fixed: the re-enumeration reads the Output
Slot Context's Slot State and issues Reset Device where that is the legal
transition. The model's gap is not fixed. It still posts Success for any
command against any slot state, so the three-way branch is no better witnessed
by the suite than the old unconditional one was. What witnesses it is the
transcription in `docs/usb-xhci-info/xhci-data-structures.md`, "Which Slot
State each command requires", and Phase 13's bare-metal run. A model that
tracked Slot State and answered Context State Error would close this, and it
does not exist.

Some mutations are closable only by constructing states the model does not
produce on its own, and that is a reason to add a knob rather than to declare
the mutation unreachable. Batch 6-V closed two that way: an all-ones PORTSC
read counting as a port-disable confirmation needed the dead-window knob, and
a power-off confirmed in `PED` instead of `PP` needed a port that is powered
but not enabled. Normally the two bits fall together, so the wrong bit is
invisible until they are pulled apart.

Never reuse one `XHCI_TRANSFER` record across submissions in a vector. Once a
deferral keeps a transfer queued the record links to itself and the suite
hangs outright (a 17-minute spin before the cause was obvious). That is a
fixture property, not a driver one: usbport allocates a distinct transfer
extension per transfer and never re-offers one the miniport still holds
(batch 7a-0). It is why every loop in the affected vectors settles or takes
the tail before posting again.

### Three model seams that make the vectors over them narrower than they read

The two properties above are things a vector must be written against. These
three are places the model cannot produce the input a real controller would,
so a green vector there is a claim about a smaller thing than its name
suggests.

A forced Stopped Transfer Event's pointer is manufactured from the driver's
own ring record. The model reports `ring->Dequeue` and chooses the completion
code from `Dequeue == Enqueue` (`test/test_init.c`), so the integration stop
path never sees a mid-TD stop naming an interior TRB, which is the case 4.6.10
p.128's "a Set TR Dequeue destroys the stopped TD's partial progress" rule
exists for. The pure latch is exercised with interior pointers in
`test/test_xfer.c`, so what is blind is the seam between the two, not the
arithmetic on either side.

The TT-pair identity net intercepts only the open paths. It is written as
"every endpoint-properties block the probe folded", but
`xhciProbeFoldProperties` is also reached from `QueryEndpointRequirements`, so
a Query-path fold in a test with no subsequent open escapes the net entirely.
Same shape as the row-set lesson elsewhere in this document: a net is only as
complete as the list of call sites routed into it, and "every" in a net's
description is a claim to check against `grep`, not a property of the net.

Output Endpoint Context state never transitions on its own. `slot_set_ep_state`
is the model's only writer, so every EP-state-dependent branch (the
Context-State-Error recovery that reads EP State because 4.6.9 p.123 says to,
the stop-that-changed-nothing test) runs against hand-placed, spec-anchored
values. The values are right; what is structurally unobservable is a
transition's timing, which puts EP-state races in the same category as the
orderings whose only observer would be a second thread, below.

### What the host suite cannot check

Memory write order. The rule that each TRB's Cycle Bit reaches memory after
its payload rests on the `volatile` accesses in `src/xhci_ring.c` and on the
caller ringing the doorbell only afterwards. Those are direct stores into a
buffer; there is no accessor to hook, and a host test sees only the finished
bytes. That property remains a review obligation and a QEMU-trace one
(`docs/contributing/build-and-test.md`, "QEMU xHCI trace events").

Trace volume, and anything else that only exists in a traced build. Every
suite here compiles with `DBG` undefined, which is the release path, where
every `XHCI_DBG_*` macro expands to nothing. So "this site is capped" is
invisible to the host suite even though the site is right there in the file
under test. It matters because the cost is not the log: an uncapped line in
the DPC is `DbgPrint` and tens of thousands of port-0xE9 bytes spent at
DISPATCH_LEVEL under usbport's `MiniportInterruptsSpinLock` (one shipped once,
in the Host Controller Event arm, and a review caught it). The rule that
replaces a test: a trace site reachable per event, per URB, or per timer tick
uses `XHCI_DBG_VALUE_CHANGED`, never `XHCI_DBG_VALUE`.

Which counters are printed, and whether their value is quiet when the bus is.
The same `DBG`-undefined compile makes both invisible, and Phase 5 task 7
turned them into a checkpoint risk rather than a logging preference. Every
trace macro, `_LIMITED` and `_CHANGED` alike, is bounded by a per-site
driver-image static that no start, stop or resume resets, so a site whose
value moves for reasons unrelated to the question will be silent by the time
it is needed.

Two rejected shapes are the instructive part: a running count,
which moved on every resume; and a tally of what is attached now, which moves
too, because a resume really does take the bus down and bring it back. Win98
idle-suspends within about half a second of a start, so on a multi-port bus
both shapes are exhausted within seconds. What works is a value that is
monotone within a start (a set of "speeds seen") whose gate is semantic rather
than budgetary.

The deepest version of the same thing: every trace macro's state is per
expansion, therefore per driver image, while the values these sites watch are
per controller. On a machine with two xHCI controllers both bind this one
image and reach the same expansion with their own extension's value; if the
two differ, successive calls alternate, every sample counts as "changed", and
the site spends its budget in the time it takes to make 32 calls, about eight
seconds for a 500 ms callback. Monotone per controller is not quiet at the
site. This is why the speed evidence has no periodic print at all: it is
carried by a site gated on a monotone set with no budget, which cannot be
exhausted however many controllers exist, and which carries `StartEpoch` so a
line names its controller and start.

Known and not fixed: every `XHCI_DBG_VALUE_CHANGED` in `xhciCheckController`
has this property, including the `port status change events` line that
carries Phase 4's plug/unplug clause. It costs nothing on the
single-controller test VMs, so no checkpoint observed so far is affected. It
is recorded so a multi-controller bare-metal run is not debugged from the
wrong end.

Rules that replace the missing tests, all review obligations on every bounded
site. A bounded site owes a durable witness. A counter in the
`CheckController` block is the usual one and `src/xhci_dbg.h` names it, but it
is not the strongest: it inherits the per-image/per-controller flaw above, so
it is a witness for the single-controller case only. A site whose gate is
semantic and which therefore needs no budget (the first-decode line) needs no
separate witness at all, so the speed evidence has no `CheckController`
counter of its own. Three more rules follow:

- The witness must not move when the thing being diagnosed has not changed.
  Ask what else moves it.
- A value that is quiet per controller is not quiet at the site.
- If the gate is what you are relying on, count the firings: a set is
  idempotent under OR and looks identical whether the gate works or not.

The last two are testable even though the printing is not.
`test_root_hub_reported_speed` runs ten multi-port suspend/resume cycles and
asserts the set is unchanged (making it a snapshot fails 4 checks) and asserts
`RhFirstDecodes == 3`, which catches a lost gate at 33 firings against a
budget of 32.

Orderings whose only observer would be a second thread. Two examples from the
command engine: capturing the start epoch under `CommandLock` rather than
re-reading it in the timer-arm helper, and initializing the command engine
before `StartController` publishes the extension's signatures. Both are
correct and both fail zero checks when reverted, because a single-threaded
host cannot put anything between the capture and the use. They are labelled
at their sites rather than left looking covered, the same treatment
`CheckController`'s bracket gets above, and for the same reason: a line the
suite cannot distinguish from its absence is a line a later edit can delete
for free unless something says so.

Mutual exclusion itself, but not the code shape around the lock. The harness
is single-threaded, so no vector can put two contexts inside a critical
section and no vector can prove the lock is the right one; that stays a review
property plus the Phase 2d SMP VM. What the spin-lock hooks do make visible is
the shape: every acquire has exactly one release, the lock is never nested,
the claim happens inside it, and no usbport service and no bounded wait
happens while it is held.

That last one is checked for every service rather
than per-service: every service stub and wait hook (`UsbPortWait`, which is
`KeDelayExecutionThread` on the target and so a hang under a DISPATCH-level
spin lock, `UsbPortReadWriteConfigSpace`, `KeStallExecutionProcessor` and the
rest) reports through one function whose never-reset total is asserted once at
the end of the run, so a stub added later is covered by construction rather
than by whoever adds it remembering. Same family as the no-touch checklist above: give a property
several call sites share one mechanism, and route the sites into it.

A vector's negative half is free unless its positive half is asserted. Phase
5's mutation checks found this three times in one day, and each time the
check read correctly and passed for a reason other than the one it named. "An
event arrives mid-resume and the operation survives it" posted an event that
latched nothing, so the mutation it was written against (applying reset's
claim rule to whatever is armed) failed zero checks.

"The next operation does
not inherit the previous one's age" asserted only that the first poll answers
"not yet", which an inherited count does too, being already past the limit
and therefore never crossing at all: the worst available failure, silently
switching the detector off. And "the retired port is not re-armed by the same
sweep" ran against a shadow that still described the port as it was before
the suspend, because a port operation writes and returns without reading
back, so the state that makes the sweep's ordering matter was never reached.

The rule: assert that the thing you are claiming is survivable actually
happened, and where the subject is a count or a state machine, walk the whole
cycle rather than its first step.

A callback's IRQL and the locks its caller holds. `test_init` invokes every
registered callback synchronously from `main()`, so it models neither the DPC
usbport dispatches some of them from nor the spin lock usbport holds across
the call. A `ResetController` that reinitialized the controller (a sleeping
operation) against an assumed PASSIVE_LEVEL once shipped with the whole suite
green: the model has no IRQL and `hc_wait` simply counts milliseconds.

The rule that replaces a test is the one that caught it: derive the calling
context from the shipping binaries before writing the body, the way the
`FlushInterrupts` call site was derived, and record it in
`docs/usb-xhci-info/usbport-miniport-abi.md`. Where the answer is
"DISPATCH_LEVEL under somebody else's lock", the vector that can be written is
a negative one: assert the callback did not sleep, by checking the model's
wait counter across it.

MMIO register write order is not in that category, though it looks as if it
should be. The `XhciWrite64` defect (high DWORD of DCBAAP/CRCR/ERDP/ERSTBA
written before the low one, which spec 5.1 p.337 forbids) is catchable on the
host because every MMIO access in this driver goes through two functions in
`src/xhci_pci.c`, so redirecting those at a model makes the whole access
sequence observable. `test_init` does that, and reverting `XhciWrite64` to
high-first fails four checks, one per register.

The distinction that survives is about what is being ordered. `test_init` sees
the sequence of accesses the driver's source performs; it says nothing about
what the bus, the write-combining rules, or a controller's latching behaviour
do with them. So the QEMU register trace is still owed. It is a confirmation
on real hardware behaviour rather than the only line of defence against a
source-level mistake.

### The model rebuilds itself under you

These produced green assertions that were measuring nothing, and were found by
an audit rather than by the suite. They are properties of the harness, not of
any one vector.

- `slot_enumerate` starts from `enable_start`, which calls `hc_build`, which
  resets every model global, the save/restore shape back to the measured QEMU
  one included. So a vector that selects a knob and then enumerates a device
  is exercising the default, not the knob.
  `test_save_declines_while_a_stop_is_unproven` did that once and spent a
  whole block measuring the reinitialisation path while claiming to measure
  the restore.
- A declined save means the resume reinitialises, and the reinitialisation
  drops every device. A record inspected after one is zeroed rather than
  settled, so `record->Quiesce.Flags == 0` passes because the object was
  reset, not because a chain completed. Ask what would make an assertion pass
  if the object under it had been destroyed; if the answer is "it still
  passes", the assertion is not measuring the property.
- The model answers a command when its doorbell is rung, not when the event is
  drained. So an injected `hwCmdCompletionCode` is already on the ring by the
  time the submitting call returns, and it must be cleared before the drain if
  that drain is where the code under test issues commands of its own.
  Otherwise the fix's own recovery commands are answered with the injected
  code and the vector measures an injection artifact.

## 6. Open questions

1. Compiling the pure core with a modern compiler at maximum warnings as a
   lint pass: decided against. No build host in this project has a second C
   compiler, and adding one would be a new host prerequisite for a marginal
   gain over `/W3 /Za`, which already catches the dialect errors that matter
   on this toolchain. The core stays written so that it would work (C89, local
   typedefs, no host headers beyond `stdio.h` in the runners), so this is one
   added `:suite` line if a host ever grows one.
2. Replaying real controllers' topologies from Phase 0 DOS tool reports as
   test vectors: done. `test_replay_e460` and `test_replay_p14s` in
   `test/test_caps.c`. The parser reads through a caller-supplied
   `XHCI_READ32`, so a replay vector is a reader over captured data and needed
   no change to the code under test. Both recorded topologies (18 ports; USB
   2.0 on 1-12, USB 3.x on 13-18) reproduce the port map XHCIQUAL, an
   independently written classifier running on the silicon, printed.

   What it does not yet cover, and why. XHCIQUAL v0.9 prints a decoded fact
   sheet, not the raw capability DWORDs, so a vector can carry a capability's
   shape (major/minor revision, port offset, port count, slot type, PSI count)
   but not its bytes. Three consequences:

   - The capability registers are reconstructed from decoded values, so they
     check acceptance and the scratchpad split field's round trip rather than
     field positions.
   - xECP, DBOFF, RTSOFF and the BAR length were never recorded and are
     synthetic.
   - The PSI DWORDs are not captured at all, so the counts are real but the
     entries are placeholders that nothing asserts.

   Turning the speed decode
   into a replay too needs a xhciqual change (dump the raw xECP DWORDs
   alongside the decoded sheet) followed by another bare-metal run on each
   machine. Worth doing at the next bench session; not worth a session of its
   own.
3. Four surviving mutations from batch 7a-A's review, each naming a vector
   that does not exist: allocating a device record from a GONE one; accepting
   Context State Error as proof that a Disable Slot released the slot (partly
   covered from the other direction: a Context State Error from a Stop
   Endpoint is no longer taken as proof of anything, because the Output
   Endpoint Context's EP State is read, as 4.6.9 p.123 says to); excluding
   GONE from REMOVE processing; and adding completion code 18 to the Configure
   Endpoint refusal family (recorded in batch 7a-B).
