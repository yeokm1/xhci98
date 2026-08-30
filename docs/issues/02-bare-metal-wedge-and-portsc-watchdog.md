# Issue 2 - The bare-metal wedge that QEMU never showed, the PORTSC watchdog that "fixed" it, and what was actually wrong

Status: root cause found by batch 13-R, repaired in `0.0.0.5`.

Targets affected: real hardware only. Reproduced on two Intel xHCI
generations (ThinkPad E460, Skylake 2016; ThinkPad P14s Gen 1, Comet Lake
2020), never in any QEMU guest.

The short version: five hot-plugs at one root port (a High-Speed drive, then
a Low-Speed mouse or a Full-Speed audio device, then the drive again) wedged
the whole controller until a cold boot. The first thing that recovered it was
a watchdog that swept PORTSC on a timer and acknowledged the change bits it
found. That fitted a hazard the project had measured much earlier and written
into its own invariants, and it was a mitigation for the wrong disease.

The real cause was a recovery path in the driver whose recovery step had no
owner: `ResetController` marked the controller failed, masked its interrupts
and waited for a stop/start that nothing in either operating system ever
sends. The watchdog looked like a cure only because it ran in the window
before that latch closed. The instrument built to read the register that
settled it is [issue 1](01-windows-98-log-capture.md)'s log channel.

The full bench record is [run-13e.md](../contributing/runs/run-13e.md)
(Findings 3, O, Q, R, S, T, U, V) and the repair design is
[design record 07](../contributing/design/07-controller-recovery-in-place.md).

---

## 1. The problem as it appeared

Batch 13-E, Windows 98 SE + NUSB 3.3 on the E460, `xhci98.sys` `0.0.0.4`.
After some device churn the machine reached a state where nothing enumerated
anywhere, not behind the hub and not on a root port that bypasses the hub.
Leaf devices vanished on their own, and a hub physically unplugged still
showed in Device Manager after a Refresh. That last symptom became Finding
3's signature. Only a cold boot recovered it.

It was reduced to a recipe (lessons.md):

| Sequence at one root port | Plug events | Result |
|---|---|---|
| one High-Speed drive, plugged and unplugged repeatedly | 30 | clean |
| two different High-Speed drives, alternating | 16 | clean |
| High-Speed -> Low-Speed mouse -> High-Speed | 5 | wedge |
| High-Speed -> Full-Speed audio -> High-Speed | 5 | wedge |

A pause between the unplug and the next plug did not prevent it, so it was
not a race. The P14s Gen 1 reproduced it with the same binary and the same
five plugs. Two different Intel xHCI generations, one binary, same five
plugs: silicon was excluded, and this was a driver defect.

## 2. Why QEMU had never shown it

Three independent reasons, all recorded. Together they define what a QEMU
pass is evidence of.

1. The trigger does not fire there. The chain (section 5) starts with a Stop
   Endpoint command against a periodic (interrupt) endpoint failing to
   complete. On QEMU the stop always succeeds, so the failing link never
   fires. Three pulls of a High-Speed interrupt device arm three Stop
   Endpoints and all complete. QEMU says the code is fine without ever
   reproducing the fault.
2. QEMU's emulated devices never fail. Roughly 19,000 transfer events across
   four substantial runs produced only `CC_SUCCESS`, `CC_SHORT_PACKET` and
   self-issued `CC_STOPPED`. Every error path this driver has ever exercised
   got there through `usb-host` passthrough of real hardware.
3. The structural equivalent already passed there. Phase 10's device matrix
   alternates High-Speed and Full-Speed devices six times in one boot, which
   is structurally what wedges the E460 on the first alternation, and those
   rows pass. QEMU's model tolerates what real silicon halts on, which makes
   it useless as a pass/fail oracle for this class of defect.

[failure-diagnosis.md](../contributing/failure-diagnosis.md) generalises it:
QEMU has no BIOS ownership contention, no EHCI pairing and forgiving timing.
Passing in QEMU proves logic, not hardware compatibility.

## 3. The wrong theory, and why it was so convincing

Back in Phase 4, in QEMU, the project had measured a real xHCI property: a
Port Status Change Event follows a change bit transitioning, so a change bit
nobody acknowledges suppresses the controller's next event for that port.
Phase 4 did not acknowledge anything yet, and the trace showed that shape:
`CSC` on ports 6, 7 and 8 and then silence. It became an invariant
([implementation-invariants.md](../contributing/implementation-invariants.md):
"There is one way to read a port, and everything uses it", meaning a reader
must acknowledge and fold what it observed), and the sentence was quoted in
`src/xhci_rh.c` twenty lines from where the candidate code went.

So after a candidate that re-opened the root-hub interrupt gate (`W7GATE`)
turned out to be inert, the obvious next move was to poll PORTSC and
acknowledge whatever had silenced the port. Candidate `W10ALL` swept every
managed port on every usbport health poll through `xhciRhRefresh`, the one
correct reader, since it folds into the shadow and RW1C-acknowledges every
change bit it sees. The commit that added it said plainly that this was a
polled fallback and not a repair.

It recovered the machine, the first thing in the investigation to fix
anything. It was then bisected into `W11POLL` (same sweep, ~0.5 s cadence)
and `W15SLOW` (cadence divided by `XHCI_RH_SWEEP_SLOW_POLLS = 16`, ~8 s, slow
enough that a stopwatch could tell "the sweep found it" from "an event
announced it").

Both routines are still in the tree: `XhciRhPortPollSweep` under
`XHCI_FIX_PORT_POLL` and `XhciRhGateWatchdog` under `XHCI_FIX_RH_GATE` in
`src/xhci_rh.c`, called from the health poll in `src/xhci_cmd.c`. No shipping
flavour sets either define. They never shipped.

## 4. How it was troubleshot: three refuted findings and then an instrument

The same evening produced Finding O: a dropped acknowledgement in
`xhciRhWritePortsc` when a Port Power change is in flight (`ackBits` is a
stack local, so the debt is lost). A real latent defect, and its repair
(`W13ACK`, a `PortscAckOwed` shadow field) did not fix the wedge. Finding O
was refuted by its own repair. The commit that recorded it also put its
finger on the methodological problem: W11POLL's polled sweep does two
independent things. It acknowledges change bits, and it bypasses event
delivery entirely by reading PORTSC on a timer. A working poll is therefore
equally consistent with "the port was stranded" and "no events are being
delivered at all".

By 20:10 the tally was five bench boots, five remedies, no readings. Nobody
had ever read PORTSC in the failed state. Finding Q (21:40) was the first
electrical observation, and it was wrong too: under `W15SLOW` the port sat
dead for three minutes across ~22 sweep crossings, and the mouse's sensor LED
did not light, which was read as "the port has no VBus". The LED, it turned
out, indicates enumeration, not VBus.

What Finding Q did establish was that everything that could answer the
question (the counters, the note ring, the raw PORTSC array) was sitting in
RAM on a machine with no sink (see issue 1). The note at the time: the next
task is an instrument and not another remedy. The PassThru snapshot reader
was derived from usbport's binary and built that night. Its one design rule
that matters here: it reads PORTSC through `XhciReadPortsc` and does not
acknowledge and does not fold. That is the one sanctioned exception to the
invariant above, because an instrument that acknowledged what it came to
measure would destroy the evidence on the one boot that mattered.

## 5. The root cause

Finding R was the first direct PORTSC read in the failed state, on a shipping
binary with no `XHCI_FIX_*` at all:

```text
Port 3 reads 00020AE1: PP=1, CCS=1, speed=Low, PED=0, PLS=7 (Polling), CSC=1.
Every other port reads exactly its healthy value.
```

The port is powered, the device is connected, and a change bit is standing
that nothing acknowledged. The port is not broken; it is waiting for a reset
nobody will issue. Finding Q refuted. But a stranded `CSC` on one port does
not explain a machine-wide wedge, and the same dump had the discriminator:
`InterruptCount 182` and `InterruptsClaimed 182` frozen, against
`HealthPolls 8485` still climbing. The interrupter was dark, machine-wide.

Finding S read the driver's own note ring off the wedged machine, 89 records,
no wrap, and the tail says everything
(`run-13e-evidence/wedge2-notering.txt`):

```text
port.connect=00000301          the mouse arrives at port 3
ep.open=00050303               one endpoint - the interrupt endpoint
ep.open.rate=00200004          with an interval: it is periodic
port.disconnect=00000300       the mouse is unplugged
ctrl.failed.here=00000001      <-- and the ring stops. Forever.
```

The chain, from `run-13e.md`:

1. A periodic endpoint's device is unplugged; teardown owes a Stop Endpoint.
2. That command does not complete, and on this machine the command-age
   backstop fires (see 6.2 for why it fired so early).
3. The backstop asks for a controller reset, the end of the escalation
   ladder, rather than the specification's first remedy, `CRCR.CA`.
4. `xhciResetController` masks the interrupter, sets `ControllerFailed`, logs
   `ctrl.failed.here`, and its own comment says "recovery is a stop/start".
5. That stop/start never comes. Nothing in the driver, and nothing in either
   shipping `usbport.sys`, ever sends one.
6. `xhciRhAdmitted`, the predicate every root-hub path passes through, has
   four clauses, and `ControllerFailed` is the fourth. Every port operation
   is now refused.
7. The replug raises `CSC` in silicon; no event is delivered because the
   interrupter is masked, nothing acknowledges it, nothing resets the port.
   `PORTSC 00020AE1`.

And why the sweep had looked like a cure: the polled sweep is gated by
`xhciRhAdmitted` too. Stage one of the failure is a stranded change bit on a
live controller, which a fast enough sweep still fixes. Stage two is
`ControllerFailed`, after which nothing in the root hub can reach PORTSC at
all. `W11POLL` recovered because it acted before the latch; `W15SLOW` found
nothing in three minutes because after the latch it never read the register.

The earlier invariant was true. It just wasn't the fault.

## 6. How it was solved

### 6.1 Recovery in place (batch 13-R, `0.0.0.5`)

`xhciResetController` now raises `RecoveryRequested`, and the usbport health
poll, the only periodic context that survives the latch (which is what
`HealthPolls 8485` against `InterruptCount 182` measured), arms one recovery
callback. `XhciRecoverController` re-runs the init sequence from `HCRST` at
DISPATCH level. Making `XhciInitController` callable there needed three
changes (bounded waits, PCI config reads, DMA fail-closed); everything else
had merely inherited a PASSIVE tag from its only caller. The age detector now
aborts the command (`CRCR.CA`) at the crossing instead of resetting the
controller. Verified three for three (Finding T), then 33 for 33 (Finding U).
Design record 07 has the full argument.

### 6.2 The threshold that made it fire every boot (Finding V)

The command-age backstop counted usbport `CheckController` polls, on the
reasoning that the miniport has no clock. Its comment argued the error
direction was safe because "usbport's timer is nominally 500 ms, so a host
that polls more slowly makes this fire later, never sooner." The E460 polls
faster: `PollClockMs` measured 36-80 ms (54.9 ms mean) against 465 ms in the
Windows 98 VM. So a threshold meant to sit 32 s behind the 5,000 ms command
watchdog actually stood at 2.3-5.1 s and pre-empted it on every boot, which
is why `CommandsTimedOut` read 0 across hundreds of commands. Task 13-R.3.5
turned five poll-counted thresholds into millisecond budgets on a clock the
driver owns (`XhciFrameNumber` is MFINDEX-derived and import-free; the
objection that had blocked using it was false).

The release notes carried this as a limitation until it was fixed, and the
entry went at the `1.0.0.0` cut because the release does not have the fault.
What it described: a deadline inside the driver decided the command had hung,
marked the
controller failed, masked its own interrupts, and waited for a stop/start
that nothing in either operating system ever sends. Every later path to the
hardware was refused, and the replug raised a change bit in silicon that
nothing was listening to.

### 6.3 What still times PORTSC change bits in shipping code

Not the sweep. The one shipping watchdog on port state is the reset watchdog
(`RhResetTimeouts`; callback in `src/xhci_rh.c`, deadline in
`src/xhci_hw.h`). If `PRC` has not arrived by the deadline it reports
`C_PORT_RESET` anyway, because usbhub reads the port status beside it. That
is a per-reset timer with an owner, not a periodic sweep.

Two latent PORTSC defects the investigation exposed are recorded as real and
not this fault: the `XhciRootHubPortEvent` early return that consumes an
event without acknowledging it, and the `xhciRhWritePortsc` dropped-ack on
the Port-Power path (Finding O).

## 7. Lessons the record kept

From lessons.md:

- A failure path whose recovery step has no owner is not a recovery path.
  The sentence read like a design because it named a real mechanism and
  pointed at the right layer; it was in fact a `TODO` that had lost its
  marker.
- A backstop that skips to the end of the ladder is not a backstop.
  `CRCR.CA`, the spec's own first remedy, three lines away, was never
  written.
- A threshold counted in polls is a threshold in somebody else's units.
- Checking one clause of a compound predicate does not clear the predicate.
  A review traced one of `xhciRhAdmitted`'s four clauses, was right about it,
  generalised, and discarded the correct hypothesis.
- An instrument pays for itself the first time it disagrees with an
  inference. Five bench boots of remedies produced three wrong attributions;
  one PORTSC read refuted the standing headline in a minute. Observation
  outranks remedy.
- The driver spent the investigation describing `ResetController` as usbport
  escalating. usbport never asked for one. The driver asks.

## Sources

`docs/contributing/runs/run-13e.md` (Finding 3 and Findings O, Q, R, S, T, U,
V; stage P8 P14s replication; stage P9);
`docs/contributing/runs/run-13e-evidence/`; `docs/contributing/lessons.md`'s
hot-plug-events entry;
`docs/contributing/design/07-controller-recovery-in-place.md`;
`docs/contributing/implementation-invariants.md` (PORTSC reader rule and its
observation-mode exception).
