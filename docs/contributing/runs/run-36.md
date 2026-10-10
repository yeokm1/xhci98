# Phase 36 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 36". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 36.13 - the polling legs (record 15 section 11)

**The vehicle** (2026-10-10, development host A, QEMU 11.1.0, TCG; 98 SE,
ME and the plain Windows 2000 one processor, XP and later two, the Windows
2000 SMP guest two processors under MTTCG). Every guest ran from its own
overlay (`vm\t36pv\`) over the standard read-only base, with
`qemu-xhci,p2=4,p3=4` (four root ports, each with a USB 2.0 and a
SuperSpeed personality). Kit and raw texts: `out\p36-pollv\legs\`, one
directory per guest (`notes-*.txt` carry every reading; `RESULT` lines
are the verdicts).

**Two kinds of evidence, kept apart** (record 15 section 11):

- **Functional and regression legs** on the `release` package of
  `2.3.0.0` at `bfec7b0`: x86 `out\p36-int\pkg-release-bfec7b0`
  (`xhci98.sys` 213,846 bytes, SHA-256 `12f1368c...39ea`), amd64 built
  from the same commit (247,296 bytes, `ca8ecbcd...`). "The release
  before it" is the published `2.2.0.0` (`releases\2.2.0.0`), run on the
  same guest with the same devices (the PREV rows).
- **Fault-injection legs** on the `qemu` flavour of the same commit
  (x86 272,781 bytes `a2588bdb...`, amd64 506,880 bytes `f3404ec3...`):
  `XhciQemuNoInterrupt`, `XhciQemuPinUnreadable`, `XhciQemuDeadLine`,
  `XhciQemuInject` fault `05` for a forced recovery, and 36.4's
  `XhciQemuStartHold`.
- **The reread on the fixed build** (`824a6c7`, the two commits under
  "Findings" below), both flavours: the dead-line legs again, and a
  `release` smoke in modes 0 and 2.

**What was read.** The counter block's poll words (63 to 95, which this
build's `XHCISNAP` prints as numbered counters until 36.9 names them:
63 mode as read, 64 effective mode, 65 delivery, 66 reason, 67/68 the
pin's status and value, 69 to 72 the periods as read and in effect, 73
the poller's role, 74/75 the period and state last armed, 76 to 83 runs,
runs with events, peeks, peek hits, promotions, parks, kicks, arms
displaced, 84 to 86 the interval observed, 87 to 94 the watcher and the
move, 95 the values' read status), the log ring's `intr.*`, `poll.*` and
`qemu.inj.*` notes, and on NT the DebugView capture of the start notes and
the stop's counter dump (`XhciLogDebugView` 1).

### Release legs (functional and regression)

Device set of a full row: a HID mouse moving and clicking; a Bulk-Only
stick (dir, copy both ways, `fc /b`); a UAS disk at SuperSpeed; QEMU's
hub with a mouse and a stick behind it; the composite `usb-audio`
playing a 20 s tone, captured on the host and checked for gaps; hot-plug;
the controller disabled and enabled; a device removed and rescanned.

| Guest | Leg | Reading | Verdict |
|---|---|---|---|
| 98 SE | Mode 0 (INF default) | 63..66 = 0 0 1 1, role 1 (watch), 87 looks 3017 -> 14714, no stale pair, no move; full set but audio | PASS |
| 98 SE | Mode 1 | 1 1 1 5, no watcher; mouse, stick, hot-plug | PASS |
| 98 SE | Mode 2 | 2 2 2 8, role 2, period 8 ms (74), interval max 984 then 55 ms; full set but audio, Device Manager disable/enable and remove + refresh | PASS |
| 98 SE | Values and bounds | mode 7 -> read 7, effective 0; idle 0 -> 50; active 0 -> 1; idle 6000 -> 5000 and active 9999 -> 5000 (armed 5000, intervals 4999 to 5005 ms, a hot-plug seen inside 6 s); idle 200 / active 20 in effect (armed 20, then 200 idle); mode 3 -> 0 | PASS |
| 98 SE | Audio | "sound device is not installed" from Sound Recorder in every mode; no wedge | NOT MAKEABLE (image) |
| 98 SE | PREV (`2.2.0.0`) | the same set works | PASS |
| ME | Mode 0 | 0 0 1 1, role 1, looks 392 -> 914, no stale pair; mouse, stick, UAS, hub, hot-plug, disable/enable | PASS |
| ME | Mode 1 | 1 1 1 5, no watcher; mouse, stick, hot-plug | PASS |
| ME | Mode 2 | 2 2 2 8, period 8 ms, interval max 55 ms; full set but audio, uninstall + refresh | PASS |
| ME | Audio | the 98-family audio-load wedge at the `usb-audio` enumeration (36.41's vehicle wedge, not the bus's) | NOT MAKEABLE (vehicle) |
| ME | PREV | mouse, stick, hot-plug | PASS |
| 2000 | Mode 0 | 0 0 1 1, role 1, looks to 7233, 88 1 asserted look, no stale pair, no move; full set, audio 20.000 s no gap; DebugView start notes and stop dump | PASS |
| 2000 | Mode 1 | 1 1 1 5, no watcher; mouse, stick, hot-plug, audio no gap; DebugView | PASS |
| 2000 | Mode 2 | 2 2 2 8, period 8, intervals 10 to 50 ms; full set, audio no gap, remove + rescan; DebugView | PASS |
| 2000 | Values and bounds | as 98 SE: 7 -> 0; 0 -> 50 and 1; 6000/9999 -> 5000/5000 (intervals 5007), a hot-plug started inside 5 s; 200/20 armed 20 (intervals 20 ms); 3 -> 0 | PASS |
| 2000 | PREV | the same set, audio no gap | PASS |
| 2000 SMP, Verifier 1B | Parallelism | `check-smp-parallelism.ps1`: two vCPU threads, affinity holds | PASS |
| 2000 SMP | Mode 2 set | 2 2 2 8, role 2, period 8; mouse, four drives `fc` clean, hot-plug, disable/enable, remove + rescan; 18 devices started | PASS |
| 2000 SMP | Race: promotion against re-arm | 300 copies + `fc` and 32 x 32 MB copies + `fc`, pointer moving, about 10 min: all clean; promotions 83 -> 6081, arms displaced 0; no Verifier stop | PASS |
| 2000 SMP | Race: close against peeks and expiry | 5 disable/enable cycles under the storage storm (2 vetoed by Windows for open volumes, 3 closed) and 5 more under pointer motion alone: closed and reopened each time, all devices back; no bugcheck | PASS |
| 2000 SMP | Race: a bounded drain continued | two 32 MB copies at once + pointer motion: both clean; runs with events 126 -> 1455. No counter isolates a drain stopped at its bound | PASS (functional) |
| 2000 SMP | Mode 0 | 0 0 1 1, role 1, looks 6736, no stale pair, no move; mouse, `fc` clean | PASS |
| 2000 SMP | `verifier /query` at the end | level 1B, 482 of 482 allocations, none failed, no violation | PASS |
| XP | Mode 0 | 0 0 1 1, role 1, looks to 11295, no move; full set, audio one 45.5 ms gap at 0.145 s (the stream's start); DebugView | PASS |
| XP | Mode 1 | 1 1 1 5, no watcher; DebugView | PASS |
| XP | Mode 2 | 2 2 2 8, period 8 (1 when busy), intervals 15 to 62 ms; full set, audio no gap; DebugView | PASS |
| XP | Idle 200 / active 20 | in effect 200/20, armed 20, intervals 31 to 171 ms | PASS |
| XP | PREV | the same set, audio no gap | PASS |
| XP x64 | Modes 0, 1, 2 | 0 0 1 1 watch, no move / 1 1 1 5 / 2 2 2 8, period 8, intervals 15 to 62; full set, audio no gap | PASS |
| XP x64 | PREV | mouse, stick, hot-plug | PASS |
| Vista x86 | Modes 0, 1, 2 | watch 87 to 7918, no move / 1 1 1 5 / 2 2 2 8, period 8, intervals 5 to 62 | PASS |
| Vista x86 | PREV | mouse, stick, hot-plug, disable/enable | PASS |
| Vista x64 | Modes 0, 1, 2 | 0 0 1 1 watch, 87 406 -> 6717, no move / 1 1 1 5 / 2 2 2 8, period 8, intervals 5 to 62; full set, audio (Media Player) 20.02 s with two 20.7 ms gaps | PASS |
| Vista x64 | PREV | mouse, stick, hot-plug | PASS |
| 7 x86 | Modes 0, 1, 2 | 0 0 1 1 watch, no move / 1 1 1 5 / 2 2 2 8, period 8, intervals 1 to 62; full set, audio 19.995 s no gap in each mode | PASS |
| 7 x86 | PREV | the same set, 12 devices started | PASS |
| 7 x64 | Modes 0, 1, 2 | 0 0 1 1 watch, 87 to 5902, no move / 1 1 1 5 / 2 2 2 8, period 8, intervals 1 to 62; full set, audio no gap in each mode | PASS |
| 7 x64 | Idle 6000 / active 9999; idle 200 / active 20 | 5000/5000 (intervals 5007), a hot-plug seen in about 1.5 s; 200/20 armed 20, intervals 31 ms | PASS |
| 7 x64 | PREV | the same set, audio no gap | PASS |

**At this build there is no MSI** (36.15 is not merged), so on Vista and 7
modes 0 and 1 run the line, as on the other targets; the MSI rows of
record 15 section 11 ("with a message granted, modes 0, 1 and 3 read as
the message") are 36.16's.

**The controller's D3 and back without system standby**: no guest offers
one, so (record 15 section 11) the forced recovery below is read in its
place - park and kick where the poll DPC reads it (finding 3) - and
the disable and enable rows above read the close.

### Measurements: line against poll on the same guest

Copy rate: a 32 MB file from the stick to the system disk; idle load: the
guest's QEMU process, host CPU seconds over 60 s with the guest idle and
its devices attached (`cpu.ps1`). Both are TCG readings on a shared host
and spread widely from run to run (XP's copies read 2.4 to 15 s for the
same file); read them as "no step change", not as rates.

| Guest | Copy, mode 0 / 1 / 2 / PREV | Idle host CPU, mode 0 / 1 / 2 / PREV | Period armed (74), mode 2 |
|---|---|---|---|
| 98 SE | 18.5 / 20.2 / 22.1 / 25.6 s | 14.7 / 12.5 / 16.6 / 12.7 % | 8 ms |
| ME | 27.4 / 23.5 / 20.2 / 18.1 s | 3.7 / 6.5 / 7.7-10.0 / 17.0 % (PREV under a heavier host load) | 8 ms |
| 2000 | 0.83 / 0.83 / 0.83 / 0.82 s | 2.2-2.7 / 3.3 / 3.3-4.7 / 2.7 % | 8 ms |
| XP | 12.6 / 11.9 / 13.8 s / - | 1.8 / 2.0 / 3.6 % / - | 8 ms (1 when busy) |
| XP x64 | 0.35 / 0.38 / 0.25 / 0.20 s | 1.1 / 0.8 / 1.7 / 1.5 % | 8 ms |
| Vista x86 | 0.24 / 0.28 / 0.21 / 0.34 s | 4.6 / 3.5 / 5.8 / 5.6 % | 8 ms |
| Vista x64 | 0.53 / 0.43 / 0.34 / 0.35 s | 8.8-13.6 / 5.2 / 9.1-11.6 / 9.2 % | 8 ms |
| 7 x86 | 0.54 / 0.48 / 0.47 / 0.50 s | 4.7-6.5 / 5.0 / 5.4 / 6.0 % | 8 ms |
| 7 x64 | 0.41 / 0.34 / 0.32 / 0.37 s | 6.0 / 8.7 / 5.1 / 5.2 % | 8 ms |

The period each kernel gives for "as often as the timer will run" (active
1 ms) is 8 ms on every target here with a mouse attached (PERIODIC, the
interrupt endpoint's interval), 1 ms on the busy XP and Vista/7 readings,
and the idle 50 ms with nothing outstanding. Audio continuity: no gap in
any mode on 2000, XP x64, 7 x86 and 7 x64; XP mode 0 one 45.5 ms gap at
the start of the stream (line, not poll); Vista x64 (Media Player) two
20.7 ms gaps in mode 2, with the guest's clock running at a fraction of
real time from that playback on (a guest reboot cleared it; independent of
the driver - it stayed with the controller disabled).

### Fault-injection legs (`qemu` flavour, `bfec7b0`)

| Guest | Leg | Reading | Verdict |
|---|---|---|---|
| 98 SE | No interrupt, mode 0 | 0 0 2 4, pin 0, `qemu.inj.nointerrupt` 1, role 2; stick and mouse work | PASS |
| 98 SE | No interrupt, mode 1 | Code 10, as a controller with no interrupt always failed; the failed start leaves no extension for `XHCISNAP` to read delivery 0 reason 7 | PASS |
| 98 SE | Pin unreadable, modes 0 / 1 | 0 0 1 2, pin status 2, watcher / 1 1 1 6; devices work | PASS |
| 98 SE | Dead line N = 0, mode 0 | the move: 65/66 2/9, `intr.moved` 3, retired 1, masked 1, 90 1, 94 1; the stick enumerated, no `cmd.timeout` | PASS (notes, see finding 2) |
| 98 SE | Dead line N = 0, mode 1 | no move, but Enable Slot never completed: `cmd.timeout`, three recoveries, latched failed | **FAIL** (finding 1) |
| 98 SE | Forced recovery (fault 05), mode 2 | HCH, one recovery, devices back, the poller's runs go on; parks/kicks unchanged (finding 3) | PASS |
| 98 SE | 36.4 start hold 2000 ms, device present | `qemu.inj.starthold.released`, `enum.cmd.busy.end` 09000002, no `cmd.timeout`, 2 slots, stick `fc` clean | PASS |
| 98 SE | 36.4 start hold, no device | `qemu.inj.starthold.expired` 1; a stick hot-added later enumerated | PASS |
| ME | No interrupt, mode 0 | 0 0 2 4, `qemu.inj.nointerrupt` 1, pin 0, role 2; stick and mouse work | PASS |
| 2000 | No interrupt, mode 0 / mode 1 | 0 0 2 4 polling, devices work / Code 10, no extension to read | PASS |
| 2000 | Pin unreadable, modes 0 / 1 | reason 2 / 6, pin status 2, devices work | PASS |
| 2000 | Dead line N = 0, mode 0 | the move, 65/66 2/9, devices work; DebugView | PASS (notes, finding 2) |
| 2000 | Dead line N = 0, mode 1 | latched failed, as 98 SE | **FAIL** (finding 1) |
| 2000 | Dead line inside a command wait (`XhciTolerance` 0, N = 60000, a device plugged around the latch) | attempt 4 of 4 straddled it: Enable Slot before the latch, Address Device and the descriptor read after it, then `intr.moved` 3, the port Present, no `cmd.timeout`; 77 runs with events 60 | PASS |
| 2000 | Forced recovery, mode 2 | two recoveries, parks 1 -> 2, kicks 1 -> 2 -> 3; mouse back; a mounted stick came back at Code 31 until restarted (run-35's known Windows 2000 limitation, not polling's); then disable/enable reopens | PASS |
| XP | No interrupt, mode 0 / mode 1 | 0 0 2 4 (DebugView `qemu.inj.nointerrupt` 1) / Code 10 | PASS |
| XP | Pin unreadable, modes 0 / 1 | 0 0 1 2 / 1 1 1 6, pin status 2 | PASS |
| XP | Dead line N = 0, mode 0 | the move with DebugView: `intr.moved` 3, masked 1; stop dump `poll.runs.events` 0x34, `cmd.timedout` 0 | PASS (notes, finding 2) |
| XP | Dead line N = 0, mode 1 | latched failed | **FAIL** (finding 1) |
| XP | Forced recovery, mode 2 | parks 1, kicks 2, one recovery; devices back; disable/enable reopens with delivery 2 reason 8 | PASS |
| XP x64 | Dead line N = 0, mode 0 / mode 1 | the move / latched failed | PASS / **FAIL** (finding 1) |
| XP x64 | No interrupt, mode 0 | 0 0 2 4, stick `fc` clean | PASS |
| 7 x64 | No interrupt, modes 0 / 1 | 0 0 2 4 / the start fails | PASS |
| 7 x64 | Pin unreadable, modes 0 / 1 | reason 2 / 6 | PASS |
| 7 x64 | Dead line N = 0, mode 0 / mode 1 | the move / latched failed | PASS / **FAIL** (finding 1) |
| 7 x64 | Forced recovery, mode 2 | one recovery, devices back; parks/kicks unchanged (finding 3) | PASS |

### Findings

1. **Mode 1 under a dead line: a device present at the start never
   enumerated** (98 SE, 2000, XP, XP x64, 7 x64). 36.4's BUSY retry
   drained the start's No Op and resubmitted Enable Slot; `hcdCommand`
   then waited 5000 ms on its event for a completion nothing drained -
   the backstop runs on that same thread (issue 12 section 2) - so the
   wait expired into the controller's reset, three times, and the
   recovery window latched it failed. Record 15 section 11 expects mode 1
   to run on the backstop. **Fixed** (`5bca7bd`, `824a6c7`): while the
   controller thread waits for a completion of its own - a command's, an
   EP0 transfer's - it queues the drain itself between the wait's slices
   (`hcdThreadWaitDrain`), only when an event stands and the drain is
   admitted, at `XhciTolerance` 1 only; counted at the stop as
   `enum.wait.drains`. A deadline found passed is looked at once more
   before it counts as a timeout. One Codex loop, converged at its second
   round (no P1 or P2).
2. **Two notes the record names were missing in the `qemu` flavour**:
   `qemu.inj.deadline` was noted only for a delayed latch, and
   `qemu.inj.deadline.isr` did not exist. Fixed in `5bca7bd`: the first at
   the start for N = 0 too, the second at the move and in every stop's
   counter dump. The legs above read the move by the other notes and
   counters.
3. **Park and kick after a recovery depend on the timing.** The recovery
   itself runs at DISPATCH_LEVEL (`XhciRecoverController`), so a poll
   timer that does not expire in the short passive stretch between the
   revocation and the raise neither parks nor needs a kick. 2000 and XP
   read them; 98 SE (one processor) and 7 x64 did not, and their pollers
   kept running. Not a defect; the record's "the recovery alone reads
   park and kick" holds where an expiry falls in the window.
   **Settled 2026-10-10 (wave 3 matrix, M-2): the expectation was wrong,
   not the code.** The matrix's 2000 `qemu` P8 read the same (parks 0,
   kicks 1 unchanged, `PollServiceGen` 2 to 6, runs 3482 to 41577, the
   mouse back). The condition is not an expiry inside the raised
   stretch but the timer's DPC reading the service failed or revoked
   under the controller lock, anywhere from the health poll's
   `ControllerFailed` to the grant (one controller thread pass; a DPC
   pending on that processor can run at `KeLowerIrql`, before the
   grant). A DPC that takes the lock before or after that parks nothing.
   No timing of the window was measured, so why these recoveries parked
   nothing is a likely reason (a window short beside the period), not a
   reading. Record 15 sections 5.3 and 11 now say so: park and kick are
   read where they happen, and the leg's
   pass is the service restored (generation, `PollService` 1, runs
   advancing, devices working). The Bulk-Only stick at Code 31 after the
   same recovery is run-35's known Windows 2000 limitation (the release
   notes' "Known limitations"), not polling's.

### The reread on the fixed build (`824a6c7`)

Packages `out\p36-pollv\pkg-{qemu,release}-{x86,amd64}-824`: `qemu` x86
273,197 bytes `1178ac2a...`, amd64 507,392 bytes `c90f3986...`; `release`
x86 214,168 bytes `967676eb...`, amd64 247,808 bytes `79c0ec9e...`.

| Guest | Leg | Reading | Verdict |
|---|---|---|---|
| 2000 | Dead line N = 0, mode 1 | no move (1 1 1 5); the stick enumerated and copied `fc` clean, slowly (on the backstop); a hot-added mouse moves; no `cmd.timeout`, no recovery; stop dump `enum.wait.drains` 41, `qemu.inj.deadline.isr` 1332, `tol.backstop.drains` 1289; `qemu.inj.deadline` 1 at the start | PASS |
| 2000 | Dead line N = 0, mode 0 | the move with `qemu.inj.deadline` at the start and `qemu.inj.deadline.isr` 5 at the move; stop dump `enum.wait.drains` 3, `cmd.timedout` 0 | PASS |
| 2000 | Dead line, mode 0, `XhciTolerance` 0 | `enum.wait.drains` 0 (the thread does not drain at 0); the move carried it, no `cmd.timeout` | PASS |
| 2000 | Release, modes 0 and 2 | the full set, audio no gap in both, watcher / poll counters as at `bfec7b0` | PASS |
| 98 SE | Dead line N = 0, mode 1 | no move, `qemu.inj.deadline` 1 at the start, the stick enumerated with no `cmd.timeout` or recovery and copied `fc` clean, at backstop pace (3663 of 3673 transfers by the backstop; about 95 minutes for 4 MB); the mouse's driver installed and its transfers drained, but the 98 SE desktop stopped answering after the install, so no click is shown. The stop dump goes to DebugView only, which 98 SE lacks | PASS (storage); mouse not shown |
| 98 SE | Dead line N = 0, mode 0 | the move: `qemu.inj.deadline` 1 at the start, `intr.moved` 3, `qemu.inj.deadline.isr` 5 at the move, retired 1, masked 1, 65/66 2/9; no `cmd.timeout`; stick `fc` clean, mouse clicks | PASS |
| 98 SE | Release, mode 0 | 0 0 1 1, watcher looks 2212 -> 3981, no move; three drives `fc` clean, both mice, hot-plug, disable/enable | PASS |
| 98 SE | Release, mode 2 | 2 2 2 8, period 8, runs 17623 -> 56598; drives `fc` clean, mouse, disable/enable; a hot-plug re-enumerated (slots 6 -> 8) but the DOS box showed stale volumes (98 SE's drive cache) | PASS |
| XP | Dead line N = 0, modes 1 and 0 | | owed to the matrix |
| XP x64 | Dead line N = 0, mode 1 | | owed to the matrix |
| 7 x64 | Dead line N = 0, modes 1 and 0; release modes 0 and 2 | | owed to the matrix |

### Verdict

Every leg on `bfec7b0` passed except the dead line under mode 1 on all
five targets it ran on (finding 1). The fix, and the notes of finding 2,
read correctly on the fixed build on Windows 2000 and 98 SE (x86), with
`release` smokes there passing. **Owed to the matrix** (the leg agents
were stopped by a usage pause before reaching them): the fixed build's
dead-line legs on XP, XP x64 and Windows 7 x64 - so nothing of the fix has
been read on amd64 - and the `release` smoke of the fixed build on Windows
7 x64. Also unshown: on 98 SE under mode 1 and the dead line, a mouse's
click (the desktop stopped answering after the HID driver's install, with
the mouse's transfers still being drained at backstop pace). 36.13 stays
open until those are read.

Not makeable here and so not owed: audio on 98 SE (the image has no
working playback device) and on ME (the 98-family audio-load wedge);
reason 7 of a failed mode 1 start (no extension is left to read); the
controller's D3 without system standby (no guest offers one); and a
counter that isolates a drain stopped at its bound (none exists; the
race leg reads functionally).

## 36.41 - the Windows 98 SE audio-load wedge: the baseline before stage A

**Not the qualifying reading.** 36.41 asks for the wedge to be re-measured
on stage A's build, because stage A changes what QUERY_CAPABILITIES
answers. Stage A was not built when this was taken, so this is the
baseline on today's build, the tracing that the stage A reading will reuse,
and the answer to whether the wedge is the bus's. 36.41 stays open until
the stage A reading is taken.

**The vehicle** (2026-10-09, development host A, QEMU 11.1.0, TCG, one
`pentium3` processor, 256 MB, as the Phase 27 legs).
- **The build:** `2.3.0.0` at `ebe3ec9`, x86. `release` `xhci98.sys`
  198,952 bytes, SHA-256 `c74545af...41f0`; `qemu` 252,189 bytes,
  `471eb385...7a74`.
- **Two bases, both over `vm\t26\win98-gold.qcow2`** (Windows 98 SE under
  NUSB 3.3; its `AUTOEXEC.BAT` copies the transfer drive's `XHCI98.SYS`
  over the installed driver on every boot):
  - `vm\t36a\w98-audio-xhci-base.qcow2`: `qemu-xhci`, a `usb-mouse` at port 2
    as the keep-alive, and the `usb-audio` device's devnode at port 3,
    installed once (the bus splits the composite itself, so the wizard
    offered "USB Audio Device" directly, `WDMA_USB.INF`).
  - `vm\t36a\w98-audio-uhci-base.qcow2`, **the control with no `xhci98` at
    all**: no `qemu-xhci`; a `piix3-usb-uhci` driven by Windows 98 SE's own
    `uhcd.sys` (`USB.INF`, from the CD), the keep-alive mouse at port 1 and
    the audio device at port 2 ("USB Composite Device", then "USB Audio
    Device").
- **One repetition** (`out\p36-audio\kit\rep.ps1`): a fresh overlay of the
  base and a cold boot. The anchor t0 is QEMU's second `usb_set_addr` trace
  event, the keep-alive mouse addressed by Windows (the first is SeaBIOS's),
  about 6 s after launch; it is the moment the Phase 27 procedure marked
  with the trace's "keep-alive enumerated" line, and it is the same on both
  buses. At t0 plus the delay a `usb-audio` is hot-added from the monitor
  (`audiodev=none`). The guest is read at attach plus 60, 130 and 200 s:
  a screenshot, Ctrl+Esc, a screenshot, Esc. **A wedge** is the frame frozen
  from the second read to the third (about 70 s and two key presses, the
  taskbar clock included); live is the Start menu opening. Every verdict
  was recomputed from the saved screenshots (`kit\judge.ps1`,
  `out\p36-audio\final-verdicts.txt`). On a wedge twelve `info registers`
  samples are taken. The `qemu` 120 s cell and two of the UHCI 120 s
  repetitions are the kept runs below, repeated until one wedged and then
  left running for the checks. The first `qemu` 40 s repetition was read
  by hand at attach plus 9 minutes (the script stopped at its first read).

**The readings** (`out\p36-audio\reps.txt`; screenshots `shots\`, traces
`logs\`):

| Attach after t0 | `xhci98` `release` | `xhci98` `qemu` | UHCI control (no `xhci98`) |
|---|---|---|---|
| 40 s | 2 of 6 wedged | 3 of 7 | 3 of 6 |
| 120 s | 14 of 20 | 1 of 4 | 7 of 12 |
| 300 s | 7 of 13 | - | 4 of 12 |
| All | 23 of 39 | 4 of 11 | 14 of 30 |

**What they say.**
- **It is not this driver's.** Windows 98 SE's own UHCI stack, with
  `xhci98.sys` not loaded, wedges in the same way and at a rate within the
  spread of `xhci98`'s (14 of 30 against 27 of 50; no delay separates them
  beyond what these counts can tell apart).
- **The same state on both buses.** Twelve register samples per wedge put
  the processor in VMM's own code (`C00036xx`, `C000A4xx` to `C000A8xx`) on
  `xhci98` and on UHCI alike; two live idle guests sampled the same way sat
  mostly at `FF0270ED` (15 of 24 samples), which no wedge sample read.
  Nothing is spinning: no thread is runnable, a
  deadlock rather than a loop.
- **Ctrl+Alt+Del names the process.** On a kept `qemu`-flavour wedge and on
  a kept UHCI wedge the Close Program list read the same: Explorer, Systray,
  **"Msgsrv32 [Not responding]"** (`shots\xqk-4-cad3.png`,
  `shots\uk-2-cad2.png`). `MSGSRV32` is Windows 98's message server, the
  process that, by Windows 98's design (not read here), loads the
  multimedia drivers for a newly arrived sound device; End Task did not take
  it down. The wait is in that arrival path, above the
  USB stack, and not in anything a bus answers.
- **The bus keeps serving through the wedge.** On the kept `qemu`-flavour
  wedge (`logs\xqk-4-alive.txt`), with QEMU's xHCI trace events turned on
  for 15 s while the mouse was moved: 20 mouse reports completed, the
  driver acknowledged each event (IMAN and ERDP writes, 80) and rang the
  doorbell for the next transfer 20 times, so each report's IRP went up to
  `hidclass` and came back. The trace's transfer counters kept moving.
- **The bus did the same in live and wedged runs.** QEMU's trace after the
  attach reads `SET_CONFIGURATION`, `SET_INTERFACE` alternate 1, then
  alternate 0 in both; the `qemu` trace ends, as in Phase 27, at the
  function's SELECT_INTERFACE to alternate 0 and a QUERY_CAPABILITIES, each
  answered, with the same two transfers outstanding (submitted `0x12`, completed
  `0x10`) that a live run has at that point. A live run goes on to
  QUERY_DEVICE_RELATIONS and three more transfers; a wedged one asks the bus nothing more.
- **Waiting does not avoid it here.** Phase 27 read none of 20 at 120 s;
  this vehicle reads 14 of 20 on `xhci98` and 7 of 12 on UHCI at 120 s, and
  7 of 13 and 4 of 12 at 300 s (after eight 300 s repetitions each it read
  6 and 1, which the next ten did not bear out). The release notes' advice, plug in once the machine has
  settled, is not supported by these readings.

**For the stage A reading.** The same kit and bases: `kit\batch.ps1` with
the stage A package, `XhciSelectiveSuspend` 0 and 1, against the rows
above. A rate that differs from the UHCI control's would make stage A's
answers a suspect; one that matches leaves the limitation where it is.

## 98 SE hot-plug hangs

Task 36.44. During 36.17-36.19's legs (`out\p36-i17\legs\report.md`,
"98 SE hangs") Windows 98 SE stopped responding three times, each time
with the mouse pointer still moving. A fourth came in 36.43's legs
(`out\p36-comp\legs\w98`, `hang1.png` to `hang3.png`): the UI froze right
after usbview was closed. This section takes each one back to a
reproduction on the `qemu` flavour, against `2.2.0.0`, and against
Windows 98 SE's own USB stack with `xhci98` not loaded (36.41's method).

**Verdict: none of the four is the bus's.** Hang 1 reproduces exactly on
Windows 98 SE's own UHCI stack. Hang 2 is the audio-load wedge 36.41
measured (same CPU signature as its UHCI control; it does not come
without the audio device). Hang 3 is not a hang: Windows' own Power popup
ignores Esc, and the UI stays live. Hang 4 needs the USB audio device and
usbview together, and comes the same way on `2.2.0.0` and on Windows 98
SE's own UHCI stack. Every one also comes on `2.2.0.0`. No code changed,
so there was no review loop and no build beyond the `qemu` package below.

**The vehicle** (2026-10-10, development host A, QEMU 11.1.0, TCG, one
`pentium3` processor, 256 MB, `qemu-xhci,p2=8,p3=4`, as 36.17's legs).
- **The guest is the one that hung:** a copy of 36.17's 98 SE overlay
  (`vm\t36i17\w98.qcow2`, stock 98 SE with `xhci98` installed, no NUSB
  runtime, the 36.17 device set's devnodes present), sealed read-only as
  `vm\t36h\w98-i17copy.qcow2`. One boot added two lines to `AUTOEXEC.BAT`
  that copy `D:\XHCI98.SYS` and `D:\XHCIUAS.SYS` over the installed files,
  so the transfer drive picks the build (`vm\t36h\w98h-base.qcow2`); one
  more set `XHCISNAP -verbosity 3` (`w98h-base2.qcow2`). Every run is a
  fresh overlay of that.
- **The builds:** `2.3.0.0` at `bfec7b0`, x86 `qemu` flavour, `xhci98.sys`
  272,783 bytes, SHA-256 `6edae084...8b45`, `XHCISNAP` built from the same
  tree; and `2.2.0.0`'s `release-x86` from `releases\2.2.0.0`
  (194,459 bytes, `98f1fb1d...b88a`).
- **The UHCI control:** the same base booted once with `piix3-usb-uhci` and
  no `qemu-xhci`; Windows 98 SE installed its own `uhcd.sys` from the CD
  (`USB.INF`); sealed as `vm\t36h\w98h-uhci.qcow2`.
- **The kit:** `out\p36-w98hang\kit` (`hang1.ps1`, `hang2.ps1`,
  `i17keys.ps1`/`i17rest.ps1`, `power.ps1`, `eip.ps1`, `alive.ps1`); logs,
  QEMU traces (`usb_set_addr`, port attach/detach, `usb_xhci_slot_*`,
  `usb_xhci_port_*`), `XHCISNAP` dumps and register samples in
  `out\p36-w98hang\logs`, screenshots in `shots`.
- **A note on the clock.** Under TCG the 98 SE taskbar clock fell one to
  two minutes behind the host during the legs, and a minute turned late.
  A clock that "stayed" across one reading is therefore no evidence of a
  hang; every verdict below is taken from the Start menu, a click or a
  key acting, or not.

### Hang 1: a stick unplugged while its Add New Hardware wizard is open

**Steps** (`kit\hang1.ps1`, as 36.17's `leg3w98.ps1`): the desktop plus
two minutes; an MS-DOS prompt, `start /m D:\USBIF watch 1800`; a
`usb-storage` with a never-seen serial hot-added at port 7; 60 s (the
wizard "This wizard searches for new drivers for: USB Mass Storage Device"
is up); `device_del`; 40 s; the same serial added again at port 7; 120 s.

**What the bus does** (`h1a`, `h1b`, `2.3.0.0` `qemu`):
- The first plug is enumerated (reset, Enable Slot, Address Device,
  descriptors, PDO). The unplug disables the slot, the PDO moves to the
  gone list, and the root hub invalidates its bus relations
  (`IoInvalidateDeviceRelations`, `hcd_pdo.c`).
- **No `QUERY_DEVICE_RELATIONS` comes.** The debug log shows no PnP IRP at
  all after the unplug while the wizard stays open.
- The replug is seen (`port status change on port=0000000B`, the change
  acknowledged), but the port is not reset. `XHCISNAP` read with the
  wizard open (`logs\h1b-snap-held.txt`): port 11 `PORTSC 00000EE1`, CCS 1,
  PLS 7; the enumeration machine **Gone**, no slot; no device holds a slot
  ("0 device(s)"); counters SlotsEnabled 1, DevicesAddressed 1 - the first
  plug's. QEMU's `info usb`: "Device 0.0, Port 7".
- That is design record 13's rule, not something left undone: a port in
  Gone waits until the PDO it reported has been reported missing to PnP
  (`hcdPortQuiet`, `hcd_enum.c`), and only a relations answer does that.
  Nothing is half-done: there is no Enable Slot or Address Device in
  flight, no slot held, no command outstanding.
- **It completes once Windows asks.** `h1a`: Enter through the wizard
  (its search finds `C:\WINDOWS\INF\USBSTOR.INF` in the guest) sends the gone PDO `FILTER_RESOURCE_REQUIREMENTS`, `START`
  (refused: "START refused, device gone"), one `SELECT_CONFIGURATION`
  (refused at the next tick, DEVICE_GONE) and `STOP`; the wizard then asks
  to restart. Answering **No** brought the root hub's
  `QUERY_DEVICE_RELATIONS` at once: the port left Gone, the replugged stick
  was reset, given slot 1, addressed (DevicesAddressed 2), its PDO created
  and started, the gone PDO removed, and the stick configured. QEMU:
  "Device 0.1, Port 7". The UI stayed live.

**The hang** (`h1b`): 36.17's leg went on to type its next `rc.ps1`
command lines blind into whatever had the focus, which was the wizard
(`kit\i17keys.ps1` and `i17rest.ps1` replay them with the same timing).
They stepped the wizard through to "ready to install `USBSTOR.INF`", the
same `START`/`STOP` on the gone PDO followed, and the leg's later lines
went into the wizard's remaining pages. The end state is 36.17's
screenshot for screenshot: the USBIF window in front, the wizard behind it
on its Finish page, the Start menu not opening, a click on Finish doing
nothing, Ctrl+Alt+Del's Close Program listing USBIF, MS-DOS Prompt,
Explorer, Systray and Winoldap; QEMU idle (1-2 s of CPU per 10 s, EIP
mostly the idle `FF0270ED`); `info usb` "Device 0.0". No PnP IRP reached
the bus after the `STOP`.

**The controls:**
- **Windows 98 SE's own UHCI stack, no `xhci98`** (`u1a`, same steps, port
  2 of `piix3-usb-uhci`): the same wizard; the replug **also** stays
  "Device 0.0, Port 2" while the wizard is open (98 SE's own `usbhub` does
  not address it either); the same keystroke replay ends in **the same
  wedge**: Finish page, Start menu dead, the same Close Program list.
- **`2.2.0.0` `release`** (`r1a`): the same as `2.3.0.0` at every step -
  port 11 Gone with CCS set and no slot in `XHCISNAP`, "Device 0.0", and
  the same wedge after the replay.

**Verdict: not the bus's.** Windows 98 SE's configuration manager does not
re-enumerate a hub while the Add New Hardware wizard holds a devnode that
has gone, on either bus; the wedge follows from driving that wizard (and
its restart prompt) for a device that is no longer there, and it happens
the same way with no `xhci98` loaded. The bus's part - waiting in Gone for
the relations answer, then enumerating the replug when it comes - is the
designed behaviour and recovers as soon as Windows asks.

### Hang 2: eight devices hot-added a minute after the desktop

**Steps** (`kit\hang2.ps1`, 36.17's `w98devs.ps1`): cold boot; the
desktop plus about a minute; an MS-DOS prompt; the 36.17 set added 10 s
apart - SuperSpeed stick (port 1), mouse (2), keyboard (3), full-speed hub
(4) with a mouse behind it, high-speed stick (5), **`usb-audio` (6)**, the
tool stick (8); then `dir C:\ > COM1` and a watch every minute.

**Readings:**

| Run | Build | Set | Result |
|---|---|---|---|
| `h2a` | `2.3.0.0` `qemu` | all eight | screen frozen from the first adds, QEMU at 100 % CPU; the `dir` output reached COM1; **recovered on its own** within 15 minutes |
| `h2b` | `2.3.0.0` `qemu` | all eight | the same; still at 100 % after 28 minutes, then quit |
| `h2c` | `2.3.0.0` `qemu` | **no audio** | live throughout (1-3 s of CPU per 10 s) |
| `h2e` | `2.3.0.0` `qemu` | **no audio** | live |
| `h2f`, `h2g`, `qa1` | `2.3.0.0` `qemu` | **audio only** | `h2g` and `qa1` wedged (100 % CPU, the same EIPs), `h2f` live |
| `qf1`, `qf2` | `2.3.0.0` `qemu` | all eight | live |
| `r2a`, `r2b`, `rf1` | `2.2.0.0` `release` | all eight | live |
| `ra1`, `ra2`, `rb1`-`rb3` | `2.2.0.0` `release` | **audio only** | `ra1` and `rb1` wedged (100 % CPU), three live |

Totals: with the audio device, `2.3.0.0` wedged 4 of 7 and `2.2.0.0` 2 of 8;
without it, 0 of 2. The `qa`, `qf`, `ra`, `rb` and `rf` runs read the
verdict from QEMU's CPU over three minutes (`kit\batch2.ps1`,
`out\p36-w98hang\reps2.txt`). The numbers are small; 36.41's larger
sample on both buses is the rate to quote, and these show only that the
same wedge comes on `2.2.0.0` and does not come without the audio device.
In every wedge QEMU itself answered: the monitor took `info registers`,
screendumps and trace commands throughout, so none of these is the QEMU
process-level stop seen elsewhere in the run (no monitor answer, 0 % CPU).

**During the stall** (`h2b`):
- The debug log was silent from the last device's start onward: the bus
  had nothing outstanding to say.
- 25 `info registers` samples: EIP in the VMM - `C005340E`, `C000A881`,
  `C000A933`, `C0003621`, `C000A7BF`, `C000A6E7` - and twice in V86 code.
  **These are the addresses of 36.41's wedge samples, including its UHCI
  control with no `xhci98`** (`out\p36-audio\logs\uk-2-regs.txt`:
  `C000A881`, `C00036xx`, `C005340E`, `C0051551`). None is in `xhci98`.
- The bus kept serving (`kit\alive.ps1`): in 15 s with the mouse behind the
  hub moved, 20 doorbells and 20 successful 4-byte interrupt transfers on
  its slot, each resubmitted, and the driver's ERDP writes - the reports
  delivered and nobody above consuming them.
- Win32 did not run (`XHCISNAP` typed into the MS-DOS prompt never
  started), while the V86 prompt did (`dir`).

**Verdict: not the bus's; it is 36.41's audio-load wedge.** It needs the
audio device, its CPU signature is the one 36.41's control produced on
Windows 98 SE's own UHCI stack, and the bus serves throughout. The new
facts for 36.41: the wedge spins (100 % CPU rather than idle) and can end
on its own after minutes. 36.17's "the same set 30 s apart, two minutes
later, was fine" is the wedge's own rate, not a cure (36.41 found waiting
120 s does not avoid it).

### Hang 3: closing the root hub's Power properties

**Steps** (`kit\power.ps1`): Device Manager, Universal Serial Bus
controllers, the root hub, Properties, the Power tab, "Power properties",
then Esc. Run on a guest with the device set (`h3a`, no audio) and on the
stalled-then-recovered `h2a` (with audio).

**Readings:**
- The popup opened and listed the devices, as in 36.17 (the bus saw no
  request worth a log line: the list comes from the hub's node IOCTLs).
- **Esc did nothing**, two presses on `h3a` and three on `h2a`. The popup stayed - 36.17's "the
  dialog stayed".
- Alt+C, or a click on Close, closed it at once. The properties sheet,
  Device Manager's tree (arrow keys), Cancel and the Start menu all
  answered afterwards; the guest idled at 1 s of CPU per 10 s.
- **UHCI control** (`u3a`, 98 SE's own stack, "USB Root Hub"): the same
  Power popup ("2 port(s) available") ignores Esc the same way, and Alt+C
  closes it.
- **`2.2.0.0` `release`** (`r2a`, with the audio device): the same - Esc
  ignored, Alt+C closes it, the Start menu opens.

**Verdict: not a hang and not the bus's.** The popup is Windows' own
(`usbui.dll`, which the INF takes from the system) and does not act on
Esc, on any stack.
36.17's reading took the popup staying after Esc and a taskbar clock that
had not turned for a minute (see the note on the clock) for a frozen UI;
the IRQ1 count it saw rising was the keys arriving.

### Hang 4: usbview closed with a USB audio device present

**Steps** (`kit\uv.ps1`): a fresh overlay, the desktop plus 2.5 minutes,
36.17's device set added 20 s apart (mass storage, two mice, keyboard,
hub; with or without the `usb-audio`), 30 s, then up to four cycles of:
`start D:\UV.EXE` (the SweetLow usbview of 36.17), F5, the tree walked node
by node with the arrow keys as 36.43's `uvkeys.ps1` did (each node a
node-connection and descriptor request through the hub door), Alt+F4,
then Ctrl+Esc - live if the Start menu opens. A run whose audio device
wedged the guest on arrival (hang 2) stopped before usbview and is not
counted here.

**Readings** (cycles to the first freeze):

| Set | `2.3.0.0` `qemu` | `2.2.0.0` `release` | UHCI control (no `xhci98`) |
|---|---|---|---|
| no audio, usbview | 9 of 9 cycles live (`u4d`, `u5a`) | 9 of 9 live (`u4e`, `u5b`) | - |
| audio, usbview | 2 of 3 runs froze at cycle 2 (`va2`: "fatal exception 00 ... in VXD USBAUDIO(01) + 00002ED4"; `vq1`: frozen frame); `uq1` live | 2 of 4 runs froze at cycle 2 (`vc1`, `vd1`: frozen frame); `us1`, `vc2` live | **2 of 2 froze at cycle 2** (`uhb1`: "fatal exception 0E at 0028:FF045748"; `uha1`: black screen) |
| audio, no usbview (the control) | 16 of 16 cycles live (`na1`, `na2`, `nb1`, `nb2`) | 16 of 16 live (`nc1`, `nc3`, `nd1`, `nd3`) | - |

- The `2.3.0.0` debug log at `va2`'s fault: the audio function's
  streaming interface selected (alt 1), isochronous URBs published and 30
  packets answered, then the exception. Windows started playing (a system
  sound) and its `USBAUDIO.VXD` faulted at the very offset Phase 9 and
  Phase 10 recorded on QEMU's `usb-audio`, through a UHCI control too
  (lessons.md; batch 13-E found Windows 98 SE plays clean through `xhci98`
  on real silicon, so the fault is the QEMU vehicle's).
- The frozen frames on `2.2.0.0` and `vq1` were idle (12 samples each:
  `FF0270ED`, V86 code and VMM addresses, none in `xhci98`), the screen's top rows torn the way a mode switch to the text
  screen leaves them; the UHCI control showed the fault screens
  themselves.
- 36.43's freeze had the same ingredients: its set carried `usb-audio`
  (port 6 and behind the hub), and it froze right after usbview was closed.
  Its Close Program list (MS-DOS Prompt, Explorer, Systray) is a live
  kernel with a dead UI, as here.
- The hub door's `IRP_MJ_CREATE`, `CLEANUP` and `CLOSE` on `2.3.0.0` at
  `bfec7b0` complete at once with no work (`HcdDoorCreateClose`), and the
  freeze comes the same way with no `xhci98` at all. 36.45's change to that
  path (branch `p36-strdesc`) is not in either build read here.
- QEMU answered its monitor through every freeze (register samples and
  screendumps were read from each), so none is the QEMU process stop seen
  elsewhere in the run.

**Verdict: not the bus's.** It needs the audio device and usbview
together, and it comes on `2.2.0.0` and on Windows 98 SE's own UHCI stack
at the same point; without either ingredient nothing froze. It is Windows
98 SE's `USBAUDIO.VXD` on QEMU's `usb-audio` (the vehicle fault Phase 9
recorded), set off by playback starting during the usbview cycles - most
likely a system sound; what starts it was not traced.

### Owed

Nothing for the bus. For the release notes' known limitations (36.20's
rewording, not this task): on Windows 98 SE, leave the Add New Hardware
wizard of a device before unplugging it - a device unplugged under its
wizard, and anything plugged into that port, waits for the wizard, on
every USB stack.

## Matrix (wave 3)

The combined VM matrix of the owner's rule of 2026-10-10: one install per
guest, every Phase 36 check of that guest in the same boot where it could
be, the `qemu` flavour on its own overlay. Development host A, QEMU 11.1.0
(TCG), 2026-10-10 15:20-18:07. Build: integration wave 3, `2.3.0.0` at
`2d0de39` (`out\p36-int3`; x86 `xhci98.sys` `c6d6786a...`, `xhcimsi.sys`
`8dadfd42...`; amd64 `xhci98.sys` `2931f8dc...`; `qemu` x86 `cfdaeb21...`,
amd64 `a2db8bdc...`; `XHCISNAP.EXE` `6e126db3...`, all from the same
commit). Kit, checklist and evidence: `out\p36-matrix\` (kit `kit\`,
`checklist.md`, per-guest notes and texts in `legs\<guest>\`). Before the
guests: `XHCISNAP -selftest-counters` with `check-counter-names.ps1` -
all 162 words agree (18 reserved, not printed).

The run was cut by the session's end (owner, 2026-10-10): every check
below is either finished and recorded or marked "not run"; none is left
half-run. The not-run checks, and the final-build additions (stage B's
36.39, Driver Verifier on NT6 with D4 fixed, the composite-parent and hub
power under S3/S4, `XhciWideMap` default 0 with the `=1` registry legs,
36.26's 1 MB set), belong to the final matrix on the integration wave 4
build.

**Summary.** Passed on every guest it reached: the install and the INF's
values (`XhciSelectiveSuspend` and `XhciWideMap` absent), the counter
alignment (one named nonzero word per block: poll 65/66, MSI 96/97 on
Vista and 7, stage A's `SuspSwitchRead` on 2000, `CompositeParents` 1),
36.47's header and `-nosysinfo` (no computer or user name in any file),
36.43's composite parent (one parent per port with the function beneath
it, parent and function disable and enable, the audio playing after, a
clean removal) on all ten guests, 36.13's modes 0, 1 and 2, MSI on Vista
and 7 (x86 through `xhcimsi.sys`, amd64 direct), 36.45's strings with the
refresh hammer and unplugs (98 SE, ME, 2000, Vista x86 and x64), 36.46's
regression on 7 x64 and 98 SE but for one re-attach (below),
`XhciSelectiveSuspend` absent reading as before, and 36.26's 1 MB checks
on XP and XP x64 (Td512/Td513 at 2048, promotion on with `XhciWideMap`
absent and off with 0, VERIFY clean; 98 SE and ME "not run (Windows 98 or
ME)").

**Failures and findings** (evidence under `out\p36-matrix\legs\<guest>\`):
- 98 SE, 36.46 L5 (driver, to diagnose): a mouse attached behind QEMU's
  hub at port 4.1, the port whose tablet had been removed during the
  usbview refresh hammer, was never addressed (no slot, no hub-port note,
  `TopoBehindHubAddressed` unchanged; usbview shows the port empty).
  First attaches behind the same hub worked. `l5.txt`, `l5b.txt`,
  `shots\l5d.png`, `shots\ham3.png`. Route: the hub's port-change path
  (36.44/36.46 area). **Settled 2026-10-10: not the bus's** - a keyboard
  removed under its pending install held every port; see "## M-1" below.
  Owed again with every wizard finished before the next removal.
- 2000, `qemu` P8 (finding against record 15 section 11): a forced
  recovery (`XhciQemuInject` 01050000) under mode 2 recovered and the
  mouse worked, but `PollParks` stayed 0 and `PollKicks` did not move,
  as in 36.13's Q5 on 98 SE and 7 x64; and the Bulk-Only stick came back
  with Code 31 until `pnpctl restart` (as 36.13's Q5 on 2000). `p8pre.txt`,
  `p8.txt`, `debugcon-2.log` lines 1015-1120. Route: 36.13 (poll) and the
  recovery's re-enumeration. **Settled 2026-10-10 (M-2): the expectation
  was wrong, not the code** - see 36.13 finding 3 and record 15 sections
  5.3 and 11; the row below is re-scored PASS. The Code 31 is the known
  Windows 2000 limitation of the release notes (run-35).
- XP x64, 36.47 header (tool or expectation): no SystemBios lines - this
  XP x64 keeps the BIOS strings in `HARDWARE\DESCRIPTION\System\BIOS`, not
  in `SystemBiosVersion`/`SystemBiosDate`, and `XHCISNAP` reads only the
  latter there ("BIOS not in the registry"). Everything else as expected.
  `i3k6.txt`. Route: 36.47. **Fixed 2026-10-10 (M-3, `126a7e9`):**
  `XHCISNAP` falls back to that key when both values are missing; the
  XP x64 dump is owed again on the integrated build.
- XP x64, B11 (not a driver result): the per-device
  `MaximumTransferLength` was written under `Services\usbstor` - the
  checklist's wrong path; `xhciuas` reads `Control\usbstor` (2000 read it
  there: 65535, 2097120, 2097152 and absent gave 64 KiB, 2 MiB, 2 MiB and
  1 MiB). Owed again with the right path.
- 7 x64: once, `device_del` of a usb-mouse (no hub attached) froze the
  QEMU process (HMP refused, CPU frozen); the same removal on the next
  launch was clean. Not attributed (release build, no trace).
- 7 x64 and Vista x86: a controller disable was refused while a USB
  keyboard was attached (Windows' keyboard veto); P1 on 7 x64 was not
  reached for that reason.
- ME: the last shutdown, with usbview open under mode 1, did not power
  off (black screen, CPU in VMM); `shutdown-black.txt`. Unexplained.
- 98 SE: Update Driver left `XHCI98.TMP` beside `XHCI98.SYS`; ME asked for
  the restart and processed its WININIT rename (the ledger item) - both
  to watch in the install legs.
- Control (phase 1): the QEMU process deadlock when a usb-mouse is
  hot-added on root port 2 while QEMU's usb-hub with devices sits on root
  port 3 reproduces on `533e721` and on the published `2.2.0.0`, and not
  with the controller unbound - pre-existing, a QEMU defect triggered by
  the controller running; not 36.45 (`legs\xp\notes-control.txt`).
- Not makeable in QEMU: a true surprise removal of the controller
  (`device_del` is an orderly ACPI eject on `pc`); D4 (Verifier on 7)
  skipped as the known C9 0x220 of wave 3.

| Guest | Check | Package | Result | Observed |
|---|---|---|---|---|| 98 SE | I1 | rel | PASS (TMP noted) | Update Driver OK (usbd.sys/usbui.dll prompts as documented), no restart asked, BC "no differences"; XHCI98.TMP 257,623 left beside XHCI98.SYS (i1.png) |
| 98 SE | I2 | rel | PASS | key 0002 (reg-inst.txt): XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, XhciPollActiveMs 1, IrqRoute 1, Tolerance 1, IntervalCap 1, FastPollFsLs 0, FirstEnumWait 0x1388/Port ... |
| 98 SE | I3 | rel | PASS | i3.txt: verbosity read 2 APPLIED 2; intr.mode 0, intr.delivery 1, intr.reason 1, pin 1; IntrDelivery 1, IntrReason 1 (63/64 = 0 omitted), PollRole 1, WatchLooks 1040; psw.gate=00000000; tolerance counters 0, recovery ... |
| 98 SE | I4 | rel | PASS (poll block) | xhci98.sys c6d6786a... and XHCISNAP 6e126db3... both out\p36-int3 @ 2d0de39 (hashes.txt); i3.txt names IntrDelivery=1 / IntrReason=1 equal to intr.delivery=1 / intr.reason=1 notes; no unnamed "counter N"; 162 words (2... |
| 98 SE | K6 | rel | PASS | k6x.txt: "Windows 98 SE: platform 1, version 4.10, build 2222, A", "x86 (32-bit Windows)", make/model "not available on this OS", BIOSVersion/BIOSDate (SeaBIOS strings), CPU "Pentium(r) III Processor; 1 processor(s)",... |
| 98 SE | P2 (mode 0) | rel | PASS | i3.txt 63/64 0 (omitted), 65 1, 66 1, 73 1, 74 100; mouse moves + clicks Start; G: F.BAT copy + fc x2 + copy back fc clean (p2f.png) |
| 98 SE | T1 | rel | PASS (98 cap at 60 KiB as expected) | UAS H:\BIG.BIN -> C: fc clean, C: -> H:\B2.BIN fc clean; BOT G: C:\BIG.BIN -> G:\B3.BIN fc clean (t1a/t1b/t1d.png); t1.txt: command size 1048576 (asked 1), bulk URBs longest 61440, >64K 0; UAS record 1: commands 5868,... |
| 98 SE | G1 | rel | PASS (hub part below) | g1.txt (stick, mouse, UAS): 3 interfaces USB\VID_46F4&PID_0001, USB\VID_0627&PID_0001, USB\VID_46F4&PID_0003; k2if.txt with usb-audio: XHCI98\COMPOSITE&VID_46F4&PID_0002 carries the interface (parent), none on the roo... |
| 98 SE | G2 | rel | PASS (usbview "Super" see K4) | g1.txt usbif hubs: port 1 stick vid 46f4 _EX speed 3, V2 proto 4 flags 3; UAS port 3 _EX speed 3; HS mouse port 6 speed 2 |
| 98 SE | K2 | rel | PASS (audio playback NOT MAKEABLE without restart, retried after a reboot) | usb-audio a1 hot-added port 3 (12 Mb/s): DM by connection "xHCI98 USB Composite Device" with "USB Audio Device" (WDMA_USB.INF) beneath (k2dm.png, k2aa.png); USBIF interface on XHCI98\COMPOSITE&VID_46F4&PID_0002 (k2if.... |
| 98 SE | K5 (tablet/kbd/stick) | rel | PASS mouse/tablet/stick; kbd NOT MAKEABLE (98 SE needs kbdhid.vxd from the CD; no second wizard appeared - vehicle) | mouse port 2 clicks Start; stick G fc clean; tablet 4.1 absolute pointer moved to (320,117) via QMP abs (tab14.png); kbd 4.2: USB Human Interface Device installed (HIDDEV.INF) but the follow-on HID keyboard install di... |
| 98 SE | K4 36.45 L1-L5 | rel | PASS | UV32: stick iManufacturer "QEMU" iProduct "QEMU USB HARDDRIVE" iSerial "1-0000:00:03.0-1", bus speed Super (uv3/uv5.png); mouse "QEMU"/"QEMU USB Mouse"/"89126-..." (uv6); tablet "QEMU"/"QEMU USB Tablet"/"28754-...-4.1... |
| 98 SE | G2 (usbview) | rel | PASS | UV32 Connection Info "Device Bus Speed: Super" (uv5.png) |
| 98 SE | G1 (hub) | rel | NOT RUN (session end) | hub present in UV32 as External Hub XHCI98HUB6; usbif list not re-run with the hub attached |
| 98 SE | K5 36.46 L5 | rel | INVALID RUN (re-scored 2026-10-10, M-1: not the bus's - a keyboard removed under its pending install held every port; see "## M-1"; owed again with every wizard finished first; evidence l5.txt, l5b.txt, l5d.png, ham3.png) | m1 device_del (root port 2), re-plugged as m2 BEHIND THE HUB at 4.1 (root-port re-add avoided: QEMU wedge rule with the hub on port 4): QEMU lists m2 at 4.1 (12 Mb/s) but the driver never addressed it: l5.txt/l5b.txt ... |
| 98 SE | K5 36.46 L6 | rel | PASS | l5.txt: no cfg.ep.surplus/.vidpid notes; SelectEndpointsRequested 13 = EndpointsOpened 13; SelectEndpointsRefused absent (0) |
| 98 SE | S1 | rel | PASS | i3/t1/l5.txt: susp.switch.value=FFFFFFFF (absent), susp.switch=0, no other susp.* note; no suspend counter (114-141) nonzero in any snapshot over ~50 min with mouse/tablet attached (idle stretches > 2 min); devices wo... |
| 98 SE | P1 mode 2 | rel | NOT RUN (session end) | not reached on w98 |
| 98 SE | G3 FastPoll 0-3 + mode 4 | rel | NOT RUN (session end) |  |
| 98 SE | T2 64/2048/4096/0 via reboot | rel | NOT RUN (session end) |  |
| 98 SE | K1 portsw.vbs | rel | NOT RUN (session end) |  |
| 98 SE | T6/B6 at -Mem 64 (-Tag m) | rel | NOT RUN (session end) |  |
| 98 SE | K3 update from 2.2.0.0 with usb-audio (-Tag p) | prev | NOT RUN (session end) |  |
| 98 SE | E-4 mode-1 dead-line leg | qemu | NOT RUN (session end) |  |
| 98 SE | Q2 / P8 forced recovery parks/kicks | qemu | NOT RUN (session end) |  |
| ME | I2 | rel | PASS | reg-inst.txt as above; SelectiveSuspend absent; WideMap absent (driver default) |
| ME | I1 | rel | PASS (restart on ME = the WININIT rename, recorded) | Found New Hardware -> D:\XHCI98.INF; ME asked for a restart (wizard + boot pass): file lands as XHCI98.TMP with WININIT.INI [rename] until then (ledger behaviour, me1.png); after the restart XHCI98.SYS in place, no .T... |
| ME | I3 | rel | PASS | i3.txt: verbosity APPLIED 2; intr.mode 0, delivery 1, reason 1, pin 1; IntrDelivery 1, IntrReason 1, PollRole 1, WatchLooks 2962; psw.gate=00000000; tolerance counters 0 |
| ME | I4 | rel | PASS (poll block; composite block see K2) | both hashes from out\p36-int3 @ 2d0de39; IntrDelivery=1/IntrReason=1 named and equal to intr.delivery/intr.reason notes; no unnamed counter; 24 of 162 nonzero |
| ME | K6 | rel | PASS | k6x.txt "Windows ME: platform 1, version 4.90, build 3000", x86 (32-bit Windows), make/model "not available on this OS", BIOSVersion/BIOSDate, CPU Pentium(r) III 1 processor, RAM 255 MB; k6y.txt "not read: the system ... |
| ME | K2 | rel | PASS | usb-audio a1 port 3 (12): wizard "USB Audio Device" (WDMA_USB), DM by connection: xHCI98 USB Composite Device with USB Audio Device beneath (mk1.png); USBIF interface on XHCI98\COMPOSITE&VID_46F4&PID_0002 (k2if.txt), ... |
| ME | G1 | rel | PASS (hub part NOT RUN) | k2if.txt: 3 interfaces (stick, mouse, XHCI98\COMPOSITE parent); none on root hub; external hub not attached |
| ME | P2 (mode 0) | rel | PASS | i3.txt 65 1 66 1 (63/64 0); mouse clicks Start (mm2.png); G: F.BAT copy + fc x2 clean (mf2.png) |
| ME | B6 | rel | PASS | H:\BIG.BIN (UAS) -> C: fc clean; C: -> G:\B3.BIN (BOT) fc clean (mb6.png); b6.txt: "promotion off; the register-free check not run (Windows 98 or ME)", stage 3 big blocks built 0 refused 0, bulk URBs longest 61440 (>6... |
| ME | T1 | rel | PASS | as B6 (UAS read + BOT write/read; UAS write-back not run) ; command size 1048576, longest 61440 |
| ME | K4 36.45 L1-L5 | rel | PASS (tablet strings NOT RUN) | UV32: stick "QEMU"/"QEMU USB HARDDRIVE"/"1-0000:00:03.0-1" (muv2), UAS "QEMU"/"USB Attached SCSI HBA"/"27842-...-3" (muv3), mouse "QEMU"/"QEMU USB Mouse"/"89126-...-2" (muv4), hub External Hub XHCI98HUB6, iConfigurati... |
| ME | S1 | rel | PASS | i3/k2a/b6/l5.txt: susp.switch.value FFFFFFFF, susp.switch 0, no other susp note, no suspend counter nonzero over ~30 min with the mouse attached (idle stretches > 2 min) |
| ME | P1 mode 2 | rel | PASS | SETV 0000 XhciInterruptMode 2 + DM controller disable (Code 22, p1p2.png) / enable: p1a.txt 63 2 64 2 65 2 66 8, 73 2, 74 8, PollRuns 13005, 85 55; mouse clicks Start (p1m.png); G: F.BAT fc x2 clean (p1j.png); after a... |
| ME | P2 mode 1 | rel | PASS (2.2.0.0 side-by-side = P6 NOT RUN) | SETV mode 1 + DM disable/enable (p2p2.png Code 22 between): p2a.txt 63 1 64 1 65 1 66 5, no role/watcher, no cmd.timeout/ctrl.failed; G: F.BAT fc x2 clean (p2f.png); mouse clicks Start (p2m.png) |
| ME | K1 portsw.vbs | rel | NOT RUN (session end) | not reached |
| ME | Q1 (36.13 no-interrupt start mode 0 polls / mode 1 fails), P3, P4 | qemu | NOT RUN (session end) | not reached |
| ME | P6 PREV side by side | prev | NOT RUN (session end) | not reached |
| ME | G2 | rel | NOT RUN (session end) | USBIF hubs not run on ME; UV32 connection info not scrolled |
| ME | K5 (tablet/kbd) | rel | NOT RUN (session end) | only mouse + stick shown |
| 2000 SP4 | I1 | rel | PASS | Have Disk install OK, no prompt, no restart; Driver tab 2.2.0.0; bc.bat "FC: no differences encountered" (xhci98.sys c6d6786a...); xhcimsi.sys not installed (2000, expected); ls.bat all started problem 0 (shots i3, upd8) |
| 2000 SP4 | I2 | rel | PASS | key 0000: IntelPortSwitch 0, MaxTransferKB 0x400, InterruptMode 0, PollIdleMs 0x32, PollActiveMs 1, IntelIrqRoute 1, Tolerance 1, IntervalCap 1, FastPollFsLs 0, FirstEnumWaitMs 0x1388, FirstEnumPortMs 0x7d0, ImodInter... |
| 2000 SP4 | I3 | rel | PASS | s0.txt: intr.mode.read=0 intr.mode=0 intr.delivery=1 intr.reason=1 intr.pin=1; counters IntrDelivery 1 IntrReason 1 (63/64 zero, omitted); tolerance counters 0 nonzero; psw.gate=00000000; completion codes Success 246,... |
| 2000 SP4 | M5 | rel | PASS | s0.txt: delivery line, 63..66 0 0 1 1; no MSI counter 96-108 in nonzero list; MsiFilterMode 4294967295 (=FFFFFFFF, word 109); decoded "requirements filter none has run: not applicable (it runs on Vista and 7 only...)"... |
| 2000 SP4 | K6 | rel | PASS | k6-xy.txt: X "Windows 2000: platform 2, version 5.0, build 2195, Service Pack 4", "x86 (32-bit Windows)", make/model "not available on this OS", SystemBiosDate 04/01/14, CPU "x86 Family 6 Model 7 Stepping 3; 1 process... |
| 2000 SP4 | P1 | rel | PASS | p1.txt (after dc/ec + 60 s idle, cpu.ps1 60 s = 3.03 s / 5.1% of one host CPU): IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, PollRole 2, PollPeriodLast 8, PollIntervalMinMs 10, PollIntervalMaxMs 50, PollRuns... |
| 2000 SP4 | P2 | rel | PASS | mode 1 p2m1.txt: intr.mode.read 1 mode 1 delivery 1 reason 5 ("forced line"), tolerance 0 nonzero; mode 0 p2m0.txt: 0 0 1 1 ("automatic: the line interrupt is there"), PollRole 1 (watch); each after dc/ec: f.bat F: co... |
| 2000 SP4 | T1 | rel | PASS | UAS G: BIG.BIN->C: fc clean, C:->G:\B2.BIN fc clean (shot t1uas); BOT SS F: same both fc clean (shot t1bot); XFERTEST WRITE \.\PhysicalDrive4 1024 64 then VERIFY at 64/1024/2048 KiB: 105.3/154.8/168.5 MB/s, 0 sectors ... |
| 2000 SP4 | T3 | rel | PASS (2097152 accepted = roadmap's widened range; checklist clause stale - coordinator to confirm) | 65535: "command size 65536 bytes (the device's own usbstor MaximumTransferLength)", 256 cmds <=64K (t3a.txt); 2097120: 2097152 bytes (device's own), 8 cmds <=2M (t3b.txt); 2097152: 2097152 bytes (device's own), 8 cmds... |
| 2000 SP4 | G1 | rel | PASS | g1-usbif.txt: 6 interfaces = tablet 4.1, mouse 2, BOT stick 1, BOT stick 4.2, UAS 3, XHCI98\HUB port 4; none for XHCI98\ROOT_HUB |
| 2000 SP4 | G2 | rel | PASS | g1-usbif.txt "usbif hubs": root port 1 (BOT SS) _EX speed 2, V2 proto 4 flags 3 (operating at + capable of SuperSpeed); UAS port 3 speed 2 flags 3; HS mouse port 6 speed 2 flags 0; hub speed 1; V2 bad-argument probes ... |
| 2000 SP4 | K4 | rel | PASS | L1 device set: BOT SS stick port 1, mouse port 2, UAS port 3, hub port 4 + tablet 4.1 + BOT stick 4.2. UVMS.EXE (WDK 7.1 usbview) does NOT run on 2000 ("not a valid Win32 application" / "Access is denied") - used UV32... |
| 2000 SP4 | S1 | rel | PASS | rk1-a.txt: value absent; s1.txt (after ~110 s idle with HID mouse + BOT stick + UAS attached, cpu 3.0%): only the start's switch-read notes susp.switch.value=FFFFFFFF (absent) susp.switch=00000000 (off), no other susp... |
| 2000 SP4 | K2 | rel | PASS | a4 usb-audio port 4 (no wizard): k2-ls.txt XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-4 "xHCI98 USB Composite Device" started + USB\VID_46F4&PID_0002&MI_00 "USB Audio Device" started; TONE.WAV via sndrec32: aud... |
| 2000 SP4 | I4 | rel | PASS | hashes: xhci98.sys c6d6786aa4b8820928b2e69e82736f5b8c25bba2aa1e872b5ff8671035ce4d36 (built 15:11:30) and XHCISNAP.EXE 6e126db317c87cee459525dff4c89bdfbf753554d2c22aa5836d04dc61e8ab0c (built 15:01:24), both out\p36-int... |
| 2000 SP4 | K1 | rel | PASS (read only) | cscript //nologo E:\portsw.vbs: dialog "XhciIntelPortSwitch on each xhci98 controller: ...\0000  0 - off", Cancel -> "Nothing was changed.", exit=0 (shots k1a, k1b); 1/0 + restart toggle not repeated here (already PAS... |
| 2000 SP4 | K5 | rel | PASS | mouse port 2 (HS) moved throughout; tablet 4.1 enumerated, HID started, strings read (K4); usb-kbd port 4: "HID Keyboard Device" started problem=0 (k5-ls.txt), typed line echoed (shot k5b); BOT sticks ports 1 and 4.2 ... |
| 2000 SP4 | P8 | w2k | PASS (re-scored 2026-10-10, M-2: read FAIL against record 15 section 11's old "park expected", which was wrong - 81 = 0 and 82 unmoved are correct when no poll DPC lands in the failed/revoked window; service came back, PollServiceGen 2 to 6; stick Code 31 after recovery is the known 2000 limitation) | mode 2 (p8pre.txt: 2/8, PollRole 2, PollKicks 1, PollParks 0); debugcon-2.log: "qemu inject: fired=01050000", "controller halted itself, USBSTS=00000009", controller reset, init complete, "slots invalidated, dropping ... |
| 2000 SP4 | P5 | w2k | PASS (channel: DebugView, the "M86 S" start line; XHCISNAP cannot reach a failed controller) | setv XhciInterruptMode 1 + XhciQemuNoInterrupt 1, DebugView (DBGVIEW /f /l C:\dv5.log, XhciLogDebugView 1) then dc/ec: controller "not started HAS_PROBLEM problem=10", pnpctl enable "OK REBOOT-NEEDED" (shot p5-state).... |
| 2000 SMP + Verifier | check-smp-parallelism.ps1 | rel | PASS | 2 vCPUs, thread ids 35888/34164, affinity 0xFFFFFFFF (32 of 32), exit 0 (parallelism-check.txt) |
| 2000 SMP + Verifier | verifier /query at start | rel | PASS | 17:38: Level 0000001B, xhci98.sys loads 1 unloads 0 (the base's old driver), AllocationsFailed 0 (verifier-start.txt) |
| 2000 SMP + Verifier | I1 | rel | PASS | adv.bat > Driver (1.0.0.0, "xHCI USB 2.0 Host Controller (xhci98)") > Update Driver > list > Have Disk E:\ > xHCI98 USB 3.x eXtensible Host Controller: Driver Version 2.2.0.0, no prompt, no restart (shots upd1-upd5); ... |
| 2000 SMP + Verifier | I2 | rel | PASS | key 0002 (rk1-a.txt): IntelPortSwitch 0, MaxTransferKB 0x400, InterruptMode 0, PollIdleMs 0x32, PollActiveMs 1, IntelIrqRoute 1, Tolerance 1, IntervalCap 1, FastPollFsLs 0, ForceBulkOnly 0, AvgTrbEsit 0; XhciSelective... |
| 2000 SMP + Verifier | I3 | rel | PASS | s0.txt: intr.mode.read 0 mode 0 delivery 1 reason 1 pin 1; IntrDelivery 1 IntrReason 1; tolerance 0 nonzero; psw.gate=00000000; stick SS port 1 + mouse HS port 6 configured; Success 246 only |
| 2000 SMP + Verifier | P1 | rel | PASS | setv 0002 XhciInterruptMode 2 + dc/ec; f.bat F: (BOT SS port 1) copy + both fc no differences, pointer moved (shot p1); after 110 s idle (cpu.ps1 21.9 s / 19.9%): p1.txt IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrRe... |
| 2000 SMP + Verifier | S1 | rel | PASS | rk1-a.txt value absent; s0.txt/p1.txt only susp.switch.value=FFFFFFFF susp.switch=0 (start read), no Susp* counter nonzero after 110 s idle with HID mouse + BOT stick; devices worked after |
| 2000 SMP + Verifier | K2 | rel | PASS (partial: parent leg; playback NOT RUN) | a4 usb-audio port 4 (audiodev none on this launch: enumeration only, no TONE.WAV capture): k2-parent.txt XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:04.0-4 started + USB\VID_46F4&PID_0002&MI_00 "USB Audio Device" sta... |
| 2000 SMP + Verifier | I4 | rel | PASS (stage-A block not read on smp) | xhci98.sys c6d6786a... + XHCISNAP 6e126db3... both out\p36-int3 (package-rel.txt), bc clean; 63-95: IntrDelivery/IntrReason 1/1 (s0) and 2/8 (p1) = intr.* notes; 160-161: CompositeParents 1 with usb-audio (k2.txt); MS... |
| 2000 SMP + Verifier | M6 | rel | PASS | verifier-end.txt (17:54): Level 1B, xhci98.sys loads 4 unloads 3 (install + 3 dc/ec), AllocationsAttempted 161 = Succeeded 161 = SpecialPool 161, AllocationsFailed 0, FailedDeliberately 0; RaiseIrqls 7396, AcquireSpin... |
| 2000 SMP + Verifier | verifier /query at the end | rel | PASS | as M6 (verifier-end.txt) |
| 2000 SMP + Verifier | T5 + B4 | rel | NOT RUN (session end) | - |
| XP SP3 | I1 | rel | PASS | i.bat OK err 0x0, Logo x2, no restart; bc.bat "FC: no differences encountered"; ls: root hub, HID mouse, stick USBSTOR all problem=0 (shots i1) |
| XP SP3 | I2 | rel | PASS | key 0000: XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, XhciPollActiveMs 1, XhciImodInterval250ns 0xa0, XhciIntelIrqRoute 1; XhciSelectiveSuspend ABSENT; XhciWideMap ABSENT ... |
| XP SP3 | I3 | rel | PASS | XHCISNAP -verbosity 2 + dc/ec; i3.txt: intr.mode.read 0 intr.mode 0 intr.delivery 1 intr.reason 1 intr.pin 1 poll.idle.ms 32h active 1; psw.gate=00000000; tolerance counters 0 nonzero; completion codes Success 500 / S... |
| XP SP3 | I4 | rel | PASS | xhci98.sys c6d6786a..4d36 (257623) and XHCISNAP 6e126db3..ab0c both = out\p36-int3\hashes.txt (commit 2d0de39); i3.txt counter block named: IntrDelivery 1, IntrReason 1 (= intr.delivery/intr.reason notes), IntrPinStat... |
| XP SP3 | M5 | rel | PASS | i3.txt: IntrModeRead/IntrMode 0 (omitted as zero), IntrDelivery 1, IntrReason 1; no MSI word 96-108 nonzero; MsiFilterMode = FFFFFFFF; "requirements filter none has run: not applicable (... XP never send one)", routin... |
| XP SP3 | K6 | rel | PASS | k6-xy.txt: X "Windows XP: platform 2, version 5.1, build 2600, Service Pack 3", "x86 (32-bit Windows)", make/model "not available on this OS", SystemBiosVersion BOCHS - 1, SystemBiosDate 04/01/14, CPU Core2 Duo T7700 ... |
| XP SP3 | P1 | rel | PASS | p1.txt (after f.bat + 60 s idle): IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, PollRole 2, PollPeriodLast(74) 8, PollStateLast 2, PollIntervalMinMs(84) 15, PollIntervalMaxMs(85) 62, PollRuns 14920, Promotion... |
| XP SP3 | P2 | rel | PASS | mode 0: i3.txt 0 0 1 1, role 1 watcher, stick+mouse fine; mode 1 (setv + dc/ec): p2m1.txt IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5, no poller/watcher (role 0, looks 0); mouse moved + click focused cmd (cl... |
| XP SP3 | S1 | rel | PASS | s1.txt after 125 s idle (mode 0, mouse + stick): susp.switch.value=FFFFFFFF susp.switch=0, no suspend counter in the nonzero list (26 of 162), mouse port 6 PLS 0 (U0) and stick port 1 PLS 0 on the PORTSC table (shot s1t) |
| XP SP3 | T1 (copy part) | rel | PASS | BOT F: (port 1, 5000) and UAS G: (port 3, 5000, xhciuas via u.bat + Logo) both: BIG.BIN -> C: fc clean, C: -> BB.BIN fc clean (t1b, t1d); t1.txt command size 1048576 (bus answer), UAS send 21 streamed depth 15; copy U... |
| XP SP3 | T1 (XFERTEST part) | rel | PASS | uasraw u1 (port 3, PhysicalDrive3, 1 GiB disk xp-u1.raw): WRITE 1M x256 58.1 MB/s; VERIFY 1M 114.3 MB/s 0 bad; VERIFY 2M -skew 512 111.0 MB/s 0 bad; READ 64K 24.3 / 1M 278.2 / 2M 269.0 MB/s (shot b3a) |
| XP SP3 | B3 | rel | PASS | absent (b3a.txt): xfer.wide.value FFFFFFFF, "promotion on; the register-free check passed", promoted 1280, claimed 1280, big stream blocks built 6 refused 0, one-TD URBs 1280 largest 257 TRBs, TDs 256/257/512/513 = 10... |
| XP SP3 | G1 (root-port part) | rel | PASS | g1.txt usbif list: 4 interfaces = mouse USB\VID_0627&PID_0001, BOT USB\VID_46F4&PID_0001, UAS USB\VID_46F4&PID_0003, XHCI98\COMPOSITE&VID_46F4&PID_0002 (parent; no interface on the MI_00 function); none on XHCI98\ROOT... |
| XP SP3 | G2 | rel | PASS | g1.txt usbif hubs: port 1 BOT 46f4:0001 _EX speed 2, V2 proto 4 flags 3 (SuperSpeed op + capable); port 3 UAS _EX speed 2 V2 flags 3; mouse port 6 speed 2 flags 0; audio port 8 speed 1; V2 negative probes err 87/87/12... |
| XP SP3 | T8 | rel | PASS | run 1: sndrec32 TONE.WAV + BIG.BIN F:->C: fc clean + mouse moves: audio-5-t8.wav span 20.151 s, 1 gap 55.5 ms at 0.145 s (the start-of-play gap, as p36-pollv XP R1 45.5 ms at 0.145 s); run 2: + XFERTEST READ 1M x1000 ... |
| XP SP3 | K2 | rel | PASS | hot-add port 4: FNH "xHCI98 USB Composite Device" (Logo alt-i) then USB Audio Device installed silently; ls (k2a) XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-4 started + USB\VID_46F4&PID_0002&MI_00 USB Audio Dev... |
| XP SP3 | G1 (hub part) | rel | PASS | usb-hub h4 on port 4 (FNH "xHCI98 USB Hub" + Logo) with tablet 4.1 + kbd 4.2: g1b.txt list = 6 interfaces: tablet 4.1, kbd 4.2, mouse 2, BOT 1, UAS 3, XHCI98\HUB&VID_0409&PID_55AA (one on the hub), none on the root hu... |
| XP SP3 | K5 | rel | PASS | mouse port 2 HS (moves throughout), stick BOT SS fc clean, tablet 4.1 + kbd 4.2 behind the hub: ls k5a all started problem=0 (HID-compliant mouse x2, HID Keyboard Device); info mice: tablet current, mouse_move -> poin... |
| XP SP3 | K1 | rel | PASS | cscript //nologo E:\portsw.vbs: dialog "XhciIntelPortSwitch ... 0 - off" (k1a), typed 1 -> "now 1 on every xhci98 controller", "RESTART WINDOWS ..." (k1b); controller restart (dc/ec, not a Windows restart) -> reg 0x1 ... |
| XP SP3 | K4 | rel | NOT OWED | not re-run: checklist marks XP done (p36-strdesc); UVMS shows the device set incl. the composite parent (uv1) |
| XP SP3 | B7 | rel | NOT RUN (session end) | - |
| XP SP3 | T6 | rel | NOT RUN (session end) | - |
| XP SP3 | K3 | prev->rel | NOT RUN (session end) | - |
| XP SP3 | E-1 | qemu | NOT RUN (session end) | - |
| XP SP3 | E-2 | qemu | NOT RUN (session end) | - |
| XP SP3 | P5 | qemu | NOT RUN (session end) | - |
| XP SP3 | P9 | qemu | NOT RUN (session end) | - |
| XP x64 | I1 | rel | PASS | FNH cancelled, i.bat -> Logo alt-i -> "update ... OK (err 0x0)", root hub FNH alt-k/alt-l/alt-l + Logo alt-i + Finish; no restart; bc.bat "FC: no differences encountered"; root hub, HID mouse, BOT stick started proble... |
| XP x64 | I2 | rel | PASS | rk-r.txt key 0000 (.NTAMD64): XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, XhciPollActiveMs 1; XhciSelectiveSuspend ABSENT; XhciWideMap ABSENT |
| XP x64 | I3 | rel | PASS | i3k6.txt (first dump): intr.mode.read 0 mode 0 delivery 1 reason 1 pin 1; psw.gate=00000000; tolerance counters 0 nonzero; stick SS + mouse HS bound |
| XP x64 | I4 | rel | PASS | driver 2931f8dc..31f3 and XHCISNAP 6e126db3..ab0c both listed in out\p36-int3\hashes.txt (2d0de39); counter block by name: IntrDelivery 1, IntrReason 1 (= notes), PollRole 1, PollPeriodLast 100, WatchLooks 1209, IntrV... |
| XP x64 | M5 (XP x64 down-level) | rel | PASS | IntrModeRead/IntrMode 0, IntrDelivery 1, IntrReason 1; no MSI word 96-108 nonzero; MsiFilterMode FFFFFFFF |
| XP x64 | K6 | rel | FAIL (SystemBios lines absent; everything else as expected - tool/expectation finding, not the driver) | i3k6.txt X: "Windows XP x64: platform 2, version 5.2, build 3790, Service Pack 2", "64-bit Windows (x64); this x86 tool ran under WOW64", "make/model not available on this OS", CPU QEMU Virtual CPU 2.5+ 2 processors, ... |
| XP x64 | B1 | rel | PASS | b2048.txt: xfer.max.value 800 bytes 00200000, "used as set (2048)", command size 2097152; URBs <=2M 384; promoted 896, big blocks built 3 refused 0, largest 513 TRBs, TDs 256/257/512/513 = 512/0/128/256 (512 = the ali... |
| XP x64 | B2 | rel | PASS | 1024 (b1024.txt): xfer.max 400, command size 1048576, URBs <=1M 1536, VERIFY 1M 141.7 / 2M skew 152.2 MB/s 0 bad, READ 64K 182.2 / 1M 828.3 / 2M 1283.3; 2048 (b2048.txt): command size 2097152, URBs <=2M 384; 64 (b64.t... |
| XP x64 | B3 | rel | PASS | absent at 1024 (b1024.txt): xfer.wide.value FFFFFFFF check 1 enabled 1, "promotion on; the register-free check passed", promoted 1536, big blocks built 3 refused 0, TDs 256/257 = 1024/512, VERIFY 1M + 2M skew512 0 bad... |
| XP x64 | B11 | rel | FAIL (per-device MaximumTransferLength ignored; evidence b11.txt, b11b.txt, b11r.txt, shots b11*, rb3) | reg add HKLM\SYSTEM\CurrentControlSet\Services\usbstor\46F40003 /v MaximumTransferLength REG_DWORD 2097152 (XhciWideMap removed): after dc/ec (b11.txt) AND after a full guest reboot (b11r.txt): class-driver send "comm... |
| XP x64 | K2 | rel | PASS | usb-audio port 4: FNH "xHCI98 USB Composite Device" + Logo alt-i, USB Audio Device installed silently; XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-4 + USB\VID_46F4&PID_0002&MI_00 started (k2a); disable COMPOSITE... |
| XP x64 | G1 (root ports, no hub) | rel | PASS (hub part NOT RUN (session end)) | g1.txt: 3 interfaces = mouse, UAS 46F4:0003, XHCI98\COMPOSITE parent; none on MI_00 or the root hub. External-hub leg not run (time) |
| XP x64 | G2 | rel | PASS | g1.txt: UAS port 3 _EX speed 2, V2 proto 4 flags 3; mouse 2/flags 0; audio port 8 speed 1 (no BOT stick at that time) |
| XP x64 | P1 | rel | PASS | p1.txt (setv + dc/ec, VERIFY 1M x128 on UAS raw 0 bad (p1a), mouse moved (p1mv), ~50 s idle): IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, PollRole 2, PollPeriodLast 8, PollIntervalMinMs 15, PollIntervalMaxM... |
| XP x64 | P2 | rel | PASS | mode 0: i3k6.txt 0 0 1 1 role 1; mode 1 (setv + dc/ec): p2m1.txt IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5, poller none; UAS VERIFY 1M x128 0 bad (p2a), mouse moved (p2mv) |
| XP x64 | S1 | rel | PASS | s1.txt after 125 s idle (mode 0, mouse + UAS): susp.switch.value FFFFFFFF switch 0, no suspend counter in the 26 nonzero, both ports Bound, mouse moves after (s1mv) |
| XP x64 | K1 | rel | PASS | cscript //nologo E:\portsw.vbs 1 -> "now 1 on every xhci98 controller" + RESTART note; dc/ec -> reg 0x1 (k1a); portsw.vbs 0 -> "0 - off ... now 0", rc=0; dc/ec -> reg 0x0 (k1b). Controller restart used, not a Windows ... |
| XP x64 | K5 | rel | NOT RUN (session end) | mouse + BOT stick + UAS worked (I1, P1, P2); tablet/kbd behind a hub not attached |
| XP x64 | B13 | rel | NOT RUN (session end) | - |
| XP x64 | T1 (BIG.BIN copy) | rel | NOT RUN (session end) | XFERTEST raw reads/VERIFY clean at 64/1024/2048/4096 (B1-B3); file-copy leg not run |
| XP x64 | E-1 | qemu | NOT RUN (session end) | - |
| XP x64 | E-2 | qemu | NOT RUN (session end) | - |
| XP x64 | B8 | qemu | NOT RUN (session end) | - |
| XP x64 | B9 | qemu | NOT RUN (session end) | - |
| XP x64 | B10 | qemu | NOT RUN (session end) | - |
| XP x64 | B12 | qemu | NOT RUN (session end) | - |
| Vista x86 | I1 | rel | PASS | pnputil oem3.inf, no restart dialog on the pnputil route; before restart problem=1 + xhcimsi STOPPED 1077 (i1-prerestart.txt); after restart controller+root hub started problem=0, xhcimsi RUNNING, bc no differences bo... |
| Vista x86 | I2 | rel | PASS | key 0000: IntelPortSwitch 0, IntelIrqRoute 1, MaxTransferKB 0x400, InterruptMode 0, PollIdleMs 0x32, PollActiveMs 1, Imod 0xa0, Tolerance 1, IntervalCap 1, FastPollFsLs 0, FirstEnumWait 0x1388/0x7d0; XhciSelectiveSusp... |
| Vista x86 | I3 | rel | PASS | XHCISNAP -verbosity 2 (key 0000 0->2), dc/ec OK (Code 22 between, started problem=0 after; i3-de.txt); i3.txt: intr.mode.read 0 mode 0, intr.delivery 5 then 3 (MSI), reason 0A, msi.connected 1, psw.gate=00000000; coun... |
| Vista x86 | I4 | rel | PASS | xhci98.sys c6d6786a.. and XHCISNAP 6e126db3.. both out\p36-int3 (commit 2d0de39, hashes.txt; XHCISNAP built 15:01:23, sys 15:11:30); i3.txt 'tool xhcisnap 2.2.0.0 built Oct 10 2026 15:01:23', build flavour release; po... |
| Vista x86 | K6 | rel | PASS | k6-xy.txt: X: 'Windows Vista: platform 2, version 6.0, build 6002, Service Pack 2', 'bitness x86 (32-bit Windows)', SystemManufacturer QEMU, SystemProductName Standard PC (i440FX + PIIX, 1996), SystemVersion pc-i440fx... |
| Vista x86 | P1 | rel | PASS | setv XhciInterruptMode 2 + dc/ec (p1-de.txt); p1.txt: IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, PollRole 2, after 1 min idle PollPeriodLast(74) 8, PollIntervalMinMs(84) 5, PollIntervalMaxMs(85) 62, PollSt... |
| Vista x86 | P7 | rel | PASS (tone whole; one ~1-buffer 20.7 ms gap per play, as 7 x86's recorded 20.7 ms gaps) | mode 2 (63..66 2 2 2 8), usb-audio a1 root port 3 (12 Mb/s), TONE.WAV via WMPlayer.OCX (cscript C:\p.vbs): play 1 span 20.010 s, 1 gap 20.7 ms at 7.77 s (p7-audio.wav); play 2 ~20.01 s, 1 gap 20.7 ms (p7-audio2.wav; t... |
| Vista x86 | I4-composite | rel | PASS | p7.txt CompositeParents 1 (named) with a1 attached; l5.txt CompositeParents 2 CompositeRepresented 1 after the re-add/parent cycle |
| Vista x86 | K2 | rel | PASS | k2-ls.txt: XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-3 'xHCI98 USB Composite Device' started + USB\VID_46F4&PID_0002&MI_00 'Audio Device' started; mouse/stick plain USB\VID_..; UV32/UVMS tree port 7 'xHCI98 US... |
| Vista x86 | K4 | rel | PASS | L1 set: stick port 1 (Super), mouse port 2, audio port 3, hub port 4 + tablet 4.1. L2 UV32: stick QEMU / QEMU USB HARDDRIVE / 1-0000:00:03.0-1, iConfiguration 06 'Super speed config (usb 3.0)'; mouse QEMU / QEMU USB M... |
| Vista x86 | K5 | rel | PASS | mouse port 2 moved (mi3/mp1), tablet 4.1 'HID-compliant mouse' started + pointer moved in L3 (HMP pointer goes to the last-added tablet), usb-kbd 4.2 'HID Keyboard Device' started and HMP sendkey text arrived in cmd a... |
| Vista x86 | G1 | rel | PASS | g1-usbif.txt: 6 GUID_DEVINTERFACE_USB_DEVICE: tablet 4.1, kbd 4.2, mouse 2, stick 1, XHCI98\COMPOSITE (parent; none on MI_00), XHCI98\HUB; none on the root hub |
| Vista x86 | G2 | rel | PASS | usbif hubs: port 1 vid 46f4 _EX speed 2 / V2 proto 4 flags 3 (SuperSpeed operating); UV32 'Device Bus Speed: Super' (k4-stick32.png); UVMS (_EX) 'High'; V2 negative probes err 87/122 |
| Vista x86 | S1 | rel | PASS | value absent at I2 (read before any change); s1.txt after 2 min idle (mouse, tablet, kbd, hub, stick, audio attached): only susp.switch.value=FFFFFFFF susp.switch=0 (switch read: absent -> off), no suspend counter nam... |
| Vista x86 | P2 | rel | PASS | p2m1.txt: IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5, MsiFilterOutcome 10 MsiFilterStripped 1 (strip), no poll role; stick fc clean (p2m1-stick.txt), USB mouse moved (mp2m1). NOTE first dc VETOED (pnpctl di... |
| Vista x86 | H2 | qemu (msi86 build, raw files) | PASS (on the msi86 build; not repeated on p36-int3 - deadline) | verdict from out\p36-msi86\legs\vx86 raw files (msi86 build, xhcisnap built Oct 10 00:08, qemu flavour - NOT re-run on p36-int3): h2snap.txt counters 65=1 (line) 66=1 101=1 (absent) 102=2 103=C0000034 (not found) 107=... |
| Vista x86 | M2 | rel | NOT RUN (deadline) - partial | MSI launch mode 0 only partly read: i3.txt 65=3 MSI 66=10 96=2 97=1, mouse moved + stick fc clean; audio played under mode 2 (P7) and after the parent cycle; UAS driver install (u.bat) reached the unsigned-driver box ... |
| Vista x86 | M3 | rel | NOT RUN (deadline) - partial | dc/ec done 3x on MSI launch (i3-de, p1-de, p2m1-de2: back started, devices back); remove+rescan not run |
| Vista x86 | H3 | rel | NOT RUN (deadline) | not reached |
| Vista x86 | U1 | rel | NOT RUN (deadline) | not reached; note: the kit has no u1 flavour - p36-int3's xhcimsi.sys is FileVersion 2.2.0.0 like msi86's, only out\p36-msi86\pkg-release-x86-u1 (2.2.0.9, msi86 code) raises it |
| Vista x86 | T1 | rel | NOT RUN (deadline) | UAS hot-added port 4 (5000) but its driver install not finished; BOT 4 MB RAND.BIN fc clean many times, BIG.BIN not copied |
| Vista x86 | K1 | rel | NOT RUN (deadline) | not reached |
| Vista x64 | I1 | rel | PASS | F8 Disable DSE; Found New Hardware wizard 'Ask me again later'; elevated cmd (alt-i); pnputil -i -a E:\xhci98.inf -> unsigned box Install anyway -> oem3.inf, Program Compatibility Assistant 'requires a digitally signe... |
| Vista x64 | I2 | rel | PASS | key 0000 (i2-rk.txt): IntelPortSwitch 0, IntelIrqRoute 1, MaxTransferKB 0x400, InterruptMode 0, PollIdleMs 0x32, PollActiveMs 1, Imod 0xa0, Tolerance 1, IntervalCap 1; XhciSelectiveSuspend ABSENT; XhciWideMap ABSENT (... |
| Vista x64 | I3 | rel | PASS | i3.txt: intr.mode 0, intr.delivery 5 then 3 (MSI) reason 0A, msi.connected 1, msi.routines 1, msi86.state 0 (x64: no helper), psw.gate=00000000; no failure counters; stick s1 port 1 f.bat fc clean (i3-stick.txt), USB ... |
| Vista x64 | I4 | rel | PASS | amd64 xhci98.sys 2931f8dc.. (out\p36-int3\pkg-release-amd64, hashes.txt commit 2d0de39, built 15:22:09) + XHCISNAP 6e126db3 (out\p36-int3, built 15:01:23, x86 tool under WOW64); i3.txt counters named: IntrDelivery 3 /... |
| Vista x64 | K6 | rel | PASS | k6-xy.txt X: 'Windows Vista: platform 2, version 6.0, build 6002, Service Pack 2', 'bitness 64-bit Windows (x64); this x86 tool ran under WOW64', SystemManufacturer QEMU, SystemProductName Standard PC (i440FX + PIIX, ... |
| Vista x64 | I4-composite | rel | PASS | k2.txt CompositeParents 1 CompositeRepresented 1 (named) with a1 attached (after the parent cycle) |
| Vista x64 | K2 | rel | PASS | k2-ls.txt XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-3 started + USB\VID_46F4&PID_0002&MI_00 Audio Device started, mouse/stick plain USB\; TONE.WAV 20.000 s 0 gaps before (k2-audio1.wav); parent disable (Code 2... |
| Vista x64 | K4 | rel | PASS | L1: stick port 1 (Super), mouse 2, audio 3, hub 4 + tablet 4.1. L2 UV64: stick QEMU / QEMU USB HARDDRIVE / 1-0000:00:03.0-1, iConfiguration 06 'Super speed config (usb 3.0)'; mouse QEMU / QEMU USB Mouse / 89126-0000:0... |
| Vista x64 | G1 | rel | PASS | g1-usbif.txt (x86 USBIF under WOW64): 5 GUID_DEVINTERFACE_USB_DEVICE: tablet 4.1, mouse 2, stick 1, XHCI98\COMPOSITE parent (none on MI_00), XHCI98\HUB; none on the root hub |
| Vista x64 | G2 | rel | PASS | usbif hubs: port 1 46f4 _EX speed 2 / V2 proto 4 flags 3; UV64 'Device Bus Speed: Super' (k4-a.png) |
| Vista x64 | S1 | rel | PASS | absent at I2; s1.txt after 2 min idle (mouse, tablet, hub, stick, audio): only susp.switch.value=FFFFFFFF susp.switch=0, no suspend counter (114-141 zero), all started; USB mouse moved after idle (ms1 diff box) |
| Vista x64 | P1 | rel | PASS | setv mode 2 + dc/ec OK (p1-de.txt); p1.txt IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, PollRole 2, after ~1 min idle PollPeriodLast 8, PollIntervalMinMs 1, PollIntervalMaxMs 62, PollKicks 1, PollParks 0; st... |
| Vista x64 | M1/F1 (+P2 mode 1) | rel | PASS | setv mode 1 + dc/ec (f1-de.txt); f1.txt IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5 (forced line, not 11), MsiFilterClass 3 MsiFilterOutcome 10 (replaced) MsiFilterMode 1 MsiFilterStripped 1, MsiMessages 0 (... |
| Vista x64 | M1/F4 | rel | PASS | setv mode 0 + dc/ec (f4-de.txt); f4.txt intr.mode 0, intr.delivery 5 then 3 (MSI) reason 0A, IntrDelivery 3 IntrReason 10, MsiRoutines 1 MsiMessages 1 MsiFilterOutcome 4; stick fc clean (f4-stick.txt), mouse moved (mf4) |
| Vista x64 | K5 | rel | PASS | mouse port 2 moved (mi3b, ms1, mp1, mf1, mf4), tablet 4.1 'HID-compliant mouse' started, usb-kbd 4.2 'HID Keyboard Device' started (k5-ls.txt) and HMP sendkey text arrived after it was added (k5-kbd.png; HMP routes to... |
| Vista x64 | P2 | rel | PASS | mode 1 = 1 1 1 5 (f1.txt); mode 0 = MSI 0 0 3 10 on this MSI guest by design (i3.txt, f4.txt); mouse + stick each |
| Vista x64 | M2 | rel | NOT RUN (session end) - partial | MSI launch mode 0 only: mouse, stick, audio (TONE 20.000 s 0 gaps, k2-audio1.wav); MSI-X launch, mode 3, UAS not run |
| Vista x64 | M3 | rel | NOT RUN (session end) - partial | dc/ec x5 on MSI (all back started, delivery message at mode 0); remove + rescan not run |
| Vista x64 | M1/F0,F1r | rel | NOT RUN (session end) | this overlay ran the release package only; F1 and F4 read on it directly |
| Vista x64 | F3 | qemu | NOT RUN (session end) | not reached |
| Vista x64 | T1 | rel | NOT RUN (session end) | not reached (BOT RAND.BIN fc clean throughout) |
| Vista x64 | K1 | rel | NOT RUN (session end) | not reached |
| 7 x86 | I1 | rel | PASS | oem2.inf installed; problem 28 before restart (shots i4); after restart controller + root hub started problem=0 (v1); bc.bat: xhci98.sys + xhcimsi.sys no differences; one extra non-driver prompt at logon = Windows Act... |
| 7 x86 | I2 | rel | PASS | rk1.txt key 0000: XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, XhciPollActiveMs 1, XhciIntelIrqRoute 1, XhciTolerance 1, XhciIntervalCap 1, XhciAvgTrbEsit 0, XhciImodInterv... |
| 7 x86 | I3 | rel | PASS | i3a.txt: intr.mode.read 0 mode 0, intr.delivery 5 then (after connect) 3 MSI, reason 0A; counters IntrDelivery 3 IntrReason 10; no tol/failure counters (tolerance 0 nonzero); psw.gate=00000000; stick SS port 1 + mouse... |
| 7 x86 | I4 | rel | PASS | package-rel.txt: xhci98.sys c6d6786a.. (15:11:30) and XHCISNAP 6e126db3.. (15:01:24) both out\p36-int3 @2d0de39 (hashes.txt), header "tool xhcisnap built Oct 10 2026 15:01:23"; i3a.txt: IntrDelivery 3 / IntrReason 10 ... |
| 7 x86 | K6 | rel | PASS | k6xy.txt (X then Y): X "Windows 7: platform 2, version 6.1, build 7601, Service Pack 1", "bitness x86 (32-bit Windows)", SystemManufacturer QEMU / ProductName Standard PC (i440FX + PIIX, 1996) / SystemVersion pc-i440f... |
| 7 x86 | P1 | rel | PASS (note) | p1a/p1b/p1c.txt: IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8 (intr.* notes same), PollRole 2 SERVE, after idle minute PollPeriodLast(74) 8, PollIntervalMinMs(84) 15, PollIntervalMaxMs(85) 62, PollKicks 1; st... |
| 7 x86 | P2 | rel | PASS | mode 1 (p2m1.txt): 63..66 = 1 1 1 5, MsiFilterOutcome 10 replaced, MsiFilterStripped 1, MsiFilterMode 1, no LineMoves; stick fc clean x2 (p2-m1-stick.txt), mouse moved (p2m1r-b/-a). mode 0 (p2m0.txt): 63/64 0, intr.de... |
| 7 x86 | T1 | rel | PASS | t1-big.txt: UAS G:->C: fc clean, C:->G: fc clean, C:->BOT F: fc clean, F:->C: fc clean (32 MB each, ~3 s per copy); t1-xfer.txt raw READ UAS PhysicalDrive3 64K 141.5, 1M 801.4, 2M 1002.8 MB/s; BOT PhysicalDrive2 1M 27... |
| 7 x86 | M2 (MSI, mode 0) | rel | PASS | p2m0.txt/t1snap.txt: delivery 3 "MSI", reason 10, MsiCapability 3 "capability MSI", MsiMessages 1; mouse moved (p2m0-a); BOT fc clean (p2-m0-stick.txt); UAS BIG.BIN fc clean (t1-big.txt); aud-m2-msi-m0.wav: played 19.... |
| 7 x86 | M2 (MSI, mode 3) | rel | PASS | m2-msi-m3.txt: IntrModeRead 3, delivery 3 MSI, reason 10, MsiRoutines 2, MsiMessages 1, capability MSI, 108 = 4; m2-msi-m3-dev.txt BOT f.bat fc clean x2, UAS RAND + BIG fc clean; mouse moved (m2-msi-m3-m-b/-a, diff bo... |
| 7 x86 | M3 (MSI) | rel | PASS (Code 1 needs a restart) | dc/ec x4 during P1/P2/M2 (devices back each time, MSI on return: p2m0.txt). pnpctl remove VEN_1B36&DEV_000D + rescan (m3-msi-rescan-ls.txt): controller "not started problem=1" (Code 1 - the INF's reboot flag, as msi86... |
| 7 x86 | K2 | rel | PASS | k2-ls.txt XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-4 "xHCI98 USB Composite Device" + USB\VID_46F4&PID_0002&MI_00 Audio Device, mouse/stick/UAS plain USB\ devnodes; m3-msi.txt CompositeParents (160) = 1; paren... |
| 7 x86 | S1 | rel | PASS | s1.txt after 2 min idle: susp.switch.value FFFFFFFF, susp.switch 0, no susp.* activity notes, no suspend counter (114-141) printed (all zero); mouse moved right after (s1m-b/-a diff), stick/UAS/hub(not attached) as se... |
| 7 x86 | L3' | rel | NOT MAKEABLE | tried: HMP device_del xhci (machine pc/i440fx, ACPI PCI hotplug): Windows ejected it in order - controller + all children gone from pnpctl lists (l3b.png), info pci no 1B36 device, guest kept running, no bugcheck. Tha... |
| 7 x86 | M2 (MSI-X, modes 0 and 3) | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | M3 (MSI-X) | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | G1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | G2 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | G3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | T3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | T6 (-Mem 512, -Tag m) | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | T8 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | K1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | K4 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | K5 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | U2 (p202) | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | qemu M4 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | qemu P3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x86 | qemu P4 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | I1 | rel | PASS | unsigned box Install anyway -> PCA "requires a digitally signed driver" box (closed; known, msi86 R notes) -> controller + root hub started problem=0 without restart (i3.png); bc.bat no differences |
| 7 x64 | I2 | rel | PASS | rk1.txt key 0000: XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, XhciPollActiveMs 1 (+ IrqRoute 1, Tolerance 1, IntervalCap 1, AvgTrbEsit 0, Imod 0xa0, ForceBulkOnly 0, FastP... |
| 7 x64 | I3 | rel | PASS | i3a.txt intr.mode 0, intr.delivery 5 -> 3 MSI, reason 0A; no tolerance/failure counters; psw.gate=00000000; stick SS + mouse HS configured |
| 7 x64 | I4 | rel | PASS | package-rel.txt xhci98.sys 2931f8dc.. (15:22:09) + XHCISNAP 6e126db3.. (15:01:24) both out\p36-int3 @2d0de39; i3a.txt IntrDelivery 3 / IntrReason 10 = intr.delivery 3 / 0A; MsiRoutines 1 (amd64) = msi.routines 1; MsiM... |
| 7 x64 | K6 | rel | PASS | k6xy.txt X: "Windows 7: platform 2, version 6.1, build 7601, Service Pack 1", "bitness 64-bit Windows (x64); this x86 tool ran under WOW64", SystemManufacturer QEMU, ProductName Standard PC (i440FX + PIIX, 1996), Syst... |
| 7 x64 | K5 L1 | rel | PASS | launch 1: hot-added port 2, HID-compliant mouse started (k5-ls.txt), pointer moved (k5l1-b/-a diff), click sent |
| 7 x64 | K5 L2 | rel | PASS | port 3: HID-compliant mouse #2 started (k5-ls.txt), info mice "QEMU HID Tablet (absolute)" active, pointer driven to the corner (k5l2-c/-d diff box 0-682 x 0-518) |
| 7 x64 | K5 L3 | rel | PASS | port 4: "HID Keyboard Device" started problem=0 (k5-ls.txt); USB keystroke input not checkable (HMP sendkey goes to PS/2) |
| 7 x64 | K5 L4 | rel | PASS | port 1 SS: f.bat F: fc clean x2 (k5-stick.txt) |
| 7 x64 | K5 L5 | rel | PASS on retry (QEMU wedge once - see notes) | launch 1: HMP device_del m1 -> the QEMU PROCESS WEDGED (HMP refused, QMP no command replies, CPU time frozen; notes above, no hub attached - not the documented hub wedge); killed PID 61672. Retried on launch 2 (same o... |
| 7 x64 | K5 L6 | rel | PASS | k5l6.txt (launch 2, mouse+tablet+kbd+stick): no cfg.ep.surplus / .vidpid note, SelectEndpointsRequested 8 = EndpointsOpened 8, SelectEndpointsRefused not printed (0); all devnodes started problem=0; no bugcheck/Code 10 |
| 7 x64 | K2 | rel | PASS | launch 2, port 3: XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-3 + USB\VID_46F4&PID_0002&MI_00 Audio Device (k2-ls.txt), mouse/stick/kbd plain USB\ devnodes; parent disable -> problem 22, function gone (k2-dis.tx... |
| 7 x64 | S1 | rel | PASS | s1.txt (launch 2, after 2 min idle, mouse+stick+kbd): susp.switch.value FFFFFFFF, susp.switch 0, no other susp.* note, no suspend counter printed (114-141 zero); mouse moved right after (s1m diff) |
| 7 x64 | P1 | rel | NOT RUN (session end; disable veto noted) | NOT REACHED: XhciInterruptMode 2 written (p1set.png getv shows 2) but the controller restart did not happen - "pnpctl disable PCI\VEN_1B36&DEV_000D...: FAILED (err 0)" (disable vetoed; p1dcec.png) with mouse + BOT sti... |
| 7 x64 | P2 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | M1/F (F0 F1 F4 F1r) | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | F3 (qemu) | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | M2 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | M3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | E-3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | B1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | B2 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | B3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | B13 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | G1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | G2 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | G3 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | T1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | K1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | K4 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | qemu E-1 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | qemu E-2 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | qemu M4 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | qemu B8 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | qemu B9 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | qemu B10 | - | NOT RUN (session end) | not reached in the time box |
| 7 x64 | B5 | rel | SKIPPED (known D4, C9 0x220; wave 4) | coordinator: known D4 (C9 0x220 at boot on wave 3, fixed in wave 4) |

## M-1: the 98 SE re-attach behind the hub (matrix wave 3, 36.46 L5)

The wave 3 matrix's one Windows 98 SE failure (`## Matrix (wave 3)`,
"98 SE, 36.46 L5"): a mouse attached at QEMU hub port 4.1, after that
port's tablet had been removed during the usbview refresh hammer, was
never addressed. Defect M-1 asked whether the bus's hub port-change path
loses it.

**Verdict: not the bus's.** It is Hang 1's mechanism above, met from
another device: while Windows 98 SE's configuration manager holds an
install for a device that has gone, it sends no `QUERY_DEVICE_RELATIONS`
to any bus, so every port whose device left after that waits in Gone
(`hcdPortQuiet`, `hcd_enum.c`, design record 13's rule) and a device
plugged into it is not enumerated - until Windows asks, when the bus
catches up at once. Hang 1 showed it on the gone device's own port; it
holds every port. No code changed, so there was no review loop and no
build.

**The matrix leg's evidence** (`out\p36-matrix\legs\w98`, `2.3.0.0` at
`2d0de39`, `release`): the leg's `usb-kbd` at 4.2 had its first-stage
install (`HIDDEV.INF`) driven with the tablet, after which no keyboard
install came up and the pointer stayed an hourglass from 16:52 to 16:58
(`kb8.png`, `kb14.png`, `kb16.png`); the keyboard was then removed under
it. In both later snapshots (`l5.txt`, `l5b.txt`, 60 s apart) root port 6,
whose mouse had been removed before the re-attach, is still **Gone**: the
root hub got no relations query either, so the hold was not the hub's.

**The reproduction** (2026-10-10, development host A, QEMU, TCG, 98 SE
from `vm\t26\win98-gold.qcow2` on an own overlay, `qemu-xhci,p2=4,p3=4`;
`2.3.0.0` at `b1b20bf`, x86 `qemu` flavour, `xhci98.sys` SHA-256
`818b3888...b776`; kit, notes and debug log in `out\p36-m1\legs\w98`):
- **The matrix's sequence, nothing pending:** hub at root port 4, tablet
  at 4.1, usbview open on that port, F5 every 2 s with the mouse moving,
  the tablet removed at the eighth refresh; the hub's relations query
  (count 0) and the tablet PDO's `REMOVE` came at once; the root-port
  mouse removed (its relations query too); a mouse at 4.1 was reset,
  given a slot, addressed, its PDO created and bound. **Passes.** A
  keyboard at 4.2, its wizard finished and then removed, also got its
  query and `REMOVE` at once (no hourglass came this time).
- **One install held:** a tablet at 4.3 with its Add New Hardware wizard
  left open and the tablet removed under it; then the 4.1 mouse removed
  and another attached at 4.1. The debug log shows the two teardowns and
  nothing else - no relations query, no `REMOVE` - and QEMU lists the new
  mouse as "Device 0.0, Port 4.1" (never addressed): **M-1's signature.**
- **Released:** Esc on the wizard brought, at once, the gone tablet PDO's
  `REMOVE`, the hub's relations query (count 0), the 4.1 port's reset,
  Enable Slot, Address Device and PDO, a second query (count 1), and the
  mouse started, configured and bound with no wizard.

Hang 1's controls above (Windows 98 SE's own UHCI stack, and `2.2.0.0`)
show the configuration manager holding the same way with no `xhci98`
loaded. Addressing the device early would change nothing a user sees: a
PDO the bus created could not reach Windows until the same query came.

**For the final combined matrix:** 98 SE 36.46 L5 again, with every Add
New Hardware wizard finished before the next device is removed (on 98 SE
the `usb-kbd` leg after L5, or left out: its second-stage install did not
come up in the matrix). The release notes' known limitation now says the
hold covers every port, not only the device's own.

## Matrix (early warning, p36-int5 0b7c395)

**Superseded by the full matrix on the stamped `2.2.0.3` build.** This
was an early-warning pass only (owner, 2026-10-10): the high-risk rows
nobody had read yet, run on the integration build before stage A's tail
and the VIA fix were merged. Every row it dropped, and every row below,
is read again on the stamped build.

Development host A, QEMU 11.1.0 (TCG), 2026-10-10 22:50 to 2026-10-11.
Build: `p36-int5` at `0b7c395`, built unchanged on branch `p36-mtx2`
(x86 all and amd64 all, every gate, host suites, `XHCISNAP` selftest
19 cases with all 162 counter names agreeing). Hashes (`out\p36-mtx2\hashes.txt`):
`release` x86 `xhci98.sys` `6865c333...`, `xhcimsi.sys` `161d0104...`,
`xhciuas.sys` `41299702...`; `release` amd64 `xhci98.sys` `4dc2b799...`,
`xhciuas.sys` `2e0ddd57...`; `qemu` x86 `xhci98.sys` `1bd66412...`,
amd64 `310f2f75...`; `XHCISNAP.EXE` `b789222a...`, from the same tree.
Kit, checklist and evidence: `out\p36-mtx2\` (kit `kit\`, `checklist.md`,
per-guest notes, texts and shots in `legs\<guest>\`).

Rows kept: stage B SB1-SB7 (36.39); Driver Verifier V1 on Windows 7
x64, Vista x86 and Vista x64, and the Windows 2000 SMP Driver Verifier
leg; CP1, the composite-parent and hub FDO power under S3/S4 (the
comppwr work); the `XhciWideMap=1` registry leg with the promotion
counter. Each guest also read the install and the counter alignment
(I1-I4) that the rows rest on.

**Reading the CP1 trace.** The line "hcd: parent FDO system state,
device state asked" logs `(system state << 8) | device state`, once per
parent FDO (composite parent and hub). The checklist's "4 on sleep, 1
on wake" names the device state asked: D3 = 4, D0 = 1. So `00000404`
(S3) or `00000504` (S4) on sleep, and `00000101` on wake, once per
parent, is the expected reading. ME's `00000100` on wake is a parent
already in D0 with no device state asked, which is 9x's same-state
suppression by design. The leg agents first scored the line count (2
and 2, not 4 and 1) as a failure. Those rows are PASS (orchestrator
ruling).

**Summary.** These rows passed on every guest that reached them:
- the install and INF values, with `XhciSelectiveSuspend` and `XhciWideMap` absent;
- the counter alignment;
- `XhciWideMap` default off ("not set (the default); promotion off");
- stage B on 98 SE, ME, 2000, 2000 SMP + Verifier, XP and 7 x86/x64:
  - switch 0 reads as before;
  - switch 1 gives DNCTRL `0x0002`, one `susp.ss.cap` per configuration, and the QEMU GetStatus STALL counted;
  - the test client's D2/D0 (U3 held, U0 back) works, under mode 2 as well;
  - removals and controller disable/enable while suspended do not hang;
  - Function Wake and SuperSpeed-hub counters read 0, as QEMU allows;
- Driver Verifier on Vista x86 (with `xhcimsi.sys`), Vista x64, 7 x64 and 2000 SMP (0x1B): no violation, `AllocationsFailed` 0;
- S4 with a composite device and a hub (with a device behind it) on 2000 SMP, XP, XP x64, Vista x86/x64 and 7 x86/x64, plus ME standby and hibernate;
- `XhciWideMap=1` on XP, XP x64 and 7 x64: promoted 1024 or 256, the register-free check passed, VERIFY clean at 1 MiB and 2 MiB.

**Failures and findings** (evidence under `out\p36-mtx2\legs\<guest>\`):
- **Windows 7 audio, regression lead and pre-existing defect.** On 7
  x86 and 7 x64 a 20 s `TONE.WAV` played in about 63 s or 106 s.
  - Packet counts: about 6,300 `IsoPacketsAnswered` per play instead of about 20,100. On a quiet host it was 0 of 6 at normal speed.
  - The cause the trace shows: a mid-stream isochronous Ring Underrun stalls the endpoint for about 41 s, until `usbaudio` aborts and resets the pipe (host-timestamped `qemu` trace `w7x64\tr3q-ts.log`).
  - Control: integration wave 3 (`2d0de39`, `release` `2931f8dc...`) on the same guest and procedure played 6 of 6 at normal speed. But wave 3's `qemu` build also stalled once in two plays, so the defect predates this build and later merges made it much more frequent.
  - Vista x86 and x64 on this build played at normal speed.
  - Evidence: `w7x64\aud1.txt`, `ctl-mtx2-times.txt`, `ctl-int3-times.txt`, `tr3q-ts.log`; `w7x86\cp1.txt`.
  - Routed to worker p36-aud. Attribution: driver.
- **98 SE CP1 standby (pre-existing, not a regression of this build).**
  After `system_wakeup` USB is dead and the desktop does not come back.
  The trace stops after "slots invalidated, dropping every device" and
  "hub leaving". With `usb-audio`, a later fatal exception 0E follows
  at `BFF9DFFF`. This is the open 36.28 part 2 standby defect. Evidence:
  `w98\debugcon-6.log`, `debugcon-7.log`, `wake-eips.txt`,
  `shots\w75-w80.png`. Routed to worker p36-sby.
- **2000 S4, Bulk-Only stick Code 31 after the resume (a known-limitation
  extension, not a new defect).**
  - The resume takes the reinit branch because QEMU's controller saves no state (no FSC). The driver then drops and re-creates every device, and the stick's new PDO comes up with the same instance id while the old storage stack is still open.
  - Windows 2000 shows Code 31 until a device restart. This is the same family as the release notes' Windows 2000 Code 31 after a recovery.
  - Composite audio, hub, tablet and mouse came back fine.
  - Evidence: `w2k\res-ls.txt`, `q-res-ls.txt`, `debugcon-4.log` lines 72-94.
- **2000 SMP + Verifier, warm restart under `XhciInterruptMode` 2
  (attribution open).**
  - A guest Restart under mode 2 boots very slowly: about 10.5 min with switch 0, against about 5 min under mode 0, and more than 14 min with switch 1 under host load.
  - It first read as a hang at "Starting up". There was no bugcheck, and `AllocationsFailed` stayed 0.
  - A cold launch of the same overlay boots normally.
  - Evidence: `smp\hang1-regs.txt` to `hang4-regs.txt`, `wr-ls.txt`, `wr-end.txt`, shots `wr6`-`wr8`.
  - To re-measure on a quiet host in the stamped matrix.
- **SB4 mid-D2 removal: read on 2000 SMP only.** The SMP guest caught
  one removal 0.3 s into a D2: `susp.seq=00405140` (outcome GONE),
  `SuspGone` 1, no hang (`smp\sb4c.txt`). On XP and 7 x86, four tries
  each found the D2 already finished: QEMU enters U3 faster than a
  `device_del` lands, so this is NOT MAKEABLE there. `SuspGone` counts
  only a sequence that ends gone, by design.
- Not makeable: standby on 2000 SMP (the Cirrus display driver vetoes
  it; hibernate was read instead); S3 on Vista and 7 (VGAPNP; hibernate
  only). Vista/7 Device Manager power data were not read.

| Guest | Check | Package | Result | Observed |
|---|---|---|---|---|
| 98 SE | I1-I4 | rel | PASS | BC no differences; `XHCI98.TMP` left beside `XHCI98.SYS` (known); INF values, `XhciSelectiveSuspend` and `XhciWideMap` absent; delivery/reason 1/1, `psw.gate` 0; 24 of 162 counters nonzero, all named; stage 2 "promotion off" (`i3.txt`) |
| 98 SE | SB1 | rel | PASS | `susp.switch` 0, "DNCTRL not read", enables/readback 0, 114-159 all 0, no `susp.ss.cap`; stick fc clean (`i3.txt`) |
| 98 SE | CP1 Stand by | rel | FAIL (pre-existing 36.28 part 2 defect; routed to p36-sby) | standby entered; after `system_wakeup` the desktop never returned (CPU looping in VMM, `wake-eips.txt`, `w75-w80.png`); audio playback before sleep not makeable (`USBAUDIO` fatal exception 00, `w60.png`) |
| 98 SE | CP1 trace | qemu | FAIL (same defect) | with audio + hub: `00000404` x2 on sleep, `00000101` x2 on wake (correct), then exception 0E at Ctrl+Alt+Del; hub/mouse only: one sleep line, the resume stops after "slots invalidated" / "hub leaving" (`debugcon-6.log`, `debugcon-7.log`) |
| ME | I1-I4 | rel | PASS | restart for the WININIT rename (known), no `.TMP`; BC clean; values as 98 SE; delivery/reason 1/1; 24 of 162 named; "promotion off" (`i3.txt`) |
| ME | SB1 | rel | PASS | as 98 SE; stage B record slot 1 ACTIVE Ss 0 (`i3.txt`) |
| ME | CP1 Stand by | rel | PASS | sleep and wake; `TONE.WAV` 20 s without gaps before and after; hub stick fc clean, hub mouse clicks Start; `CompositeParents` 1 (`h1.txt`) |
| ME | CP1 Hibernate | rel | PASS | hibernate and resume on the same overlay; `TONE.WAV` 20 s, hub stick and root stick fc clean, hub mouse works (`m78.png`) |
| ME | CP1 trace | qemu | PASS (ruling: D-state reading) | sleep `00000404` x2; wake `00000101` and `00000100` (parent already D0, 9x same-state suppression); hub mouse after (`debugcon-6.log`, `q27.png`) |
| 2000 SP4 | I1-I4 | rel | PASS | Have Disk install, no prompt, bc clean; values (`rk1.txt`), both switches absent; delivery/reason 1/1, `psw.gate` 0 (`s0.txt`); `SuspSwitchRead` named on switch 1 (`i4s1.txt`); `CompositeParents` 1 (`cp1pre.txt`) |
| 2000 SP4 | SB1 | rel | PASS | `-verbosity 3`: "DNCTRL not read", 114-159 zero, no `susp.ss.cap`; fc clean (`sb1.txt`, `sb1-fc.txt`) |
| 2000 SP4 | CP1 S4 | rel | PASS, stick Code 31 (known-limitation extension) | hibernate and resume, no hang; composite + audio function, hub, tablet 4.1 and mouse started; `TONE.WAV` full 20 s (`audio-2.wav`); BOT stick Code 31 until a device restart (`res-ls.txt`, `res-restart.txt`) |
| 2000 SP4 | CP1 trace + stick | qemu | PASS (ruling); Code 31 reproduced | sleep `00000504` x2, wake `00000101` x2 (`cp1-trace.txt`); stick re-created with the same instance id under the open stack (`debugcon-4.log` 72-94, `q-res-ls.txt`) |
| 2000 SMP + Verifier | parallelism, verifier start | rel | PASS | 2 vCPUs on distinct threads (`parallelism-check.txt`); Level 1B (`verifier-start.txt`) |
| 2000 SMP + Verifier | I1-I4, SB1 | rel | PASS | Have Disk over the base driver, bc clean (`inst1.txt`); values (`rk1.txt`); delivery/reason 1/1 (`s0.txt`); switch 0 reads as before; switch 1: `SuspSwitchRead` 1, DNCTRL `0x0002`, `susp.ss.cap=01010001` (`sw1-ec.txt`) |
| 2000 SMP + Verifier | SB3 | rel | PASS | WW `0xC00000BB`; D2: `susp.seq=00102110`, PLS 3 in readings 12 s apart (`sb3a.txt`, `sb3b.txt`); D0: `00120250`, PLS 0 (`sb3c.txt`) |
| 2000 SMP + Verifier | SB4 | rel | PASS | ACTIVE and SUSPENDED removals clean; mid-D2 caught once: `00405140` GONE, `SuspGone` 1, no hang (`sb4a.txt`, `sb4b.txt`, `sb4c.txt`) |
| 2000 SMP + Verifier | SB5, SB6, SB7 | rel | PASS | dc/ec while SUSPENDED: back, D2/D0 again (`sb5.txt`); mode 2: delivery 2 reason 8, PLS 3 held, D0 back (`sb6a-c.txt`); Function Wake / SS hub counters 0 |
| 2000 SMP + Verifier | CP1 S3 | rel | NOT MAKEABLE | the Cirrus display driver vetoes standby (`sby2`) |
| 2000 SMP + Verifier | CP1 S4 | rel | PASS | hibernate and resume; composite, hub, tablet, mouse started (`hb-ls.txt`); `TONE.WAV` full 20 s after; tablet reports flow; no Verifier stop |
| 2000 SMP + Verifier | warm restart, mode 2 | rel | OPEN (very slow boot) | mode 2 + switch 1: > 14 min at "Starting up"; mode 2 alone 10.5 min, mode 0 5 min; cold launch normal (`hang*-regs.txt`, `wr-ls.txt`) |
| 2000 SMP + Verifier | verifier end | rel | PASS | 136 allocations, all special pool, `AllocationsFailed` 0, no violation, no bugcheck (`verifier-end.txt`, `wr-end.txt`) |
| XP SP3 | I1-I4, SB1 | rel | PASS | i.bat install, bc clean (`i1.txt`); values (`rk-r.txt`); delivery/reason 1/1; 26 of 162 named; "promotion off"; stage B "not read", fc clean (`i3.txt`, `sb1f.txt`) |
| XP SP3 | WideMap=1 | rel | PASS | default: promoted 0, stage-1 chunks (`b3w0.txt`); `XhciWideMap` 1: check passed, promoted 1024, big blocks 3, Td512/513 0/0 at 1 MiB, VERIFY 1M and 2M `-skew 512` 0 bad |
| XP SP3 | SB2 | rel | PASS | DNCTRL `0x0002`; `susp.ss.cap` per SuperSpeed configuration (UAS, BOT); `SsSuspCapFailed` 2; Ss 1; fc and UAS VERIFY clean (`sb2.txt`) |
| XP SP3 | SB3 | rel | PASS | client on 46F4:0001; WW `0xC00000BB`; D2 `00102110`, PLS 3 at two readings 74 ticks apart; D0 `00120250`, PLS 0 (`sb3a-c.txt`) |
| XP SP3 | SB4 | rel | PASS (ACTIVE, SUSPENDED); mid-D2 NOT MAKEABLE | clean removals; four mid-D2 tries all found D2 finished (`sb4a-f.txt`) |
| XP SP3 | SB5, SB6, SB7 | rel | PASS | dc/ec while SUSPENDED, back (`sb5a.txt`, `sb5b.txt`); mode 2 D2/D0 (`sb6.txt`); bench-only counters 0 |
| XP SP3 | CP1 S4 | rel (Cirrus, -Tag h) | PASS | hibernate and resume; 8 devnodes started, `CompositeParents` 2; mouse and hub tablet move; `TONE.WAV` full 20 s after (`cp1post.txt`) |
| XP SP3 | CP1 trace | qemu | PASS (ruling) | `00000504` x2 on sleep (`debugcon-4.log`), `00000101` x2 on wake (`debugcon-5.log`); all started (`qpost.txt`) |
| XP x64 | I1-I4 | rel | PASS | install, bc clean (`i1.txt`); values (`rk-r.txt`); delivery/reason 1/1; extension 132944 bytes matches; 26 of 162 named; "promotion off" (`i3.txt`) |
| XP x64 | WideMap=1 | rel | PASS | default promoted 0 (`b3w0.txt`); at 1: check passed, promoted 1024, big blocks 3, VERIFY 1M / 2M `-skew 512` / 2M 0 bad |
| XP x64 | CP1 S4 | rel | PASS | hibernate offered with standard VGA; resume, all started, fc clean, `CompositeParents` 2; tablet and mouse move; `TONE.WAV` full 20 s (`audio-2.wav`) |
| Vista x86 | I1-I4 | rel | PASS | pnputil, restart asked as documented, bc clean (`i1-post.txt`); values; MSI delivery 3 reason 0A; `MsiRoutines` 2, `MsiMessages` 1; "promotion off" (`i3.txt`) |
| Vista x86 | CP1 S4 | rel | PASS | hibernate only offered; resume, all started, `CompositeParents` 1 to 2, MSI kept, no failure counter |
| Vista x86 | V1 | rel | PASS | Verifier on `xhci98.sys` + `xhcimsi.sys` with audio + hub (mouse behind) at boot: desktop, MSI, fc clean, mouse, dc/ec, no bugcheck; `AllocationsFailed` 0 (`v-after.txt`) |
| Vista x86 | CP1 trace | qemu | PASS | D3 asked on sleep and D0 on wake per parent FDO (`q-sleep-trace.txt`); all started after (`q-ls-after.txt`) |
| Vista x64 | I1-I4 | rel | PASS | pnputil under F8, no restart, bc clean; MSI delivery 3; `MsiRoutines` 1, `MsiMessages` 1 (`i3.txt`) |
| Vista x64 | CP1 S4 | rel | PASS | hibernate; resume, all started, `CompositeParents` 1 to 2, MSI kept |
| Vista x64 | V1 | rel | PASS | Verifier on `xhci98.sys` with audio + hub at boot: desktop, MSI, fc clean, dc/ec, no bugcheck; `AllocationsFailed` 0 (`v-after.txt`) |
| Vista x64 | CP1 trace | qemu | PASS | D3/D0 per parent FDO; all started, hub mouse moves (`q-ls-after.txt`) |
| 7 x86 | I1-I4, SB1 | rel | PASS | pnputil, Code 28 until the restart (known), bc clean both files; MSI delivery 3, `MsiRoutines` 2; switch 0 as before (`i1-post.txt`, `i3.txt`) |
| 7 x86 | CP1 S4 | rel | PASS (audio speed: see AUD-W7) | hibernate; resume, devices and hub tablet work, MSI kept (`cp1.txt`) |
| 7 x86 | SB3-SB7 | rel | PASS (SB4 mid-D2 NOT MAKEABLE) | D2 `00102110` PLS 3 held 26 s, D0 `00120250` (`sb3c`); clean removals, dc/ec while D2 (`sb5-after.txt`), mode 2 (`sb6a-c`); bench-only counters 0 |
| 7 x86 | CP1 trace | qemu | PASS (ruling) | `00000504` x2, `00000101` x2; all started, fc clean (`qcp1-post.txt`) |
| 7 x86 | AUD-W7 | rel | FAIL (routed to p36-aud) | `TONE.WAV` 20 s took 63 s; 3 plays `IsoPacketsAnswered` 14546, Ring Underrun 11 (`cp1.txt`) |
| 7 x64 | I1-I4, SB1, SB2 | rel | PASS | install under F8, bc clean; MSI delivery 3, `MsiRoutines` 1; switch 0 as before; switch 1: DNCTRL `0x0002`, one `susp.ss.cap`, `SsSuspCapFailed` 1, Ss 1, `SuspSwitchRead` named (`sb2.txt`) |
| 7 x64 | WideMap=1 | rel | PASS | check passed, promoted 256, wide entries 8; VERIFY clean at 1 MiB and 2 MiB (`wm-*.txt`) |
| 7 x64 | CP1 S4 | rel | PASS | hibernate; all started, fc clean, hub tablet moves; `TONE.WAV` 20.6 s right after |
| 7 x64 | V1 | rel | PASS | Verifier /standard on `xhci98.sys` with audio + hub at boot: desktop, MSI, fc clean, dc/ec, no bugcheck, 206119 allocations, 0 failed (`v1-after.txt`) |
| 7 x64 | CP1 trace | qemu | PASS (ruling) | `00000504` x2, `00000101` x2 (`debugcon-4.log` 74-75); all started (`qcp1-post.txt`) |
| 7 x64 | AUD-W7 | rel | FAIL (routed to p36-aud) | 20 s tone in 106 s, 8290 packets (`aud1.txt`); quiet host 0 of 6 normal (`ctl-mtx2-times.txt`) |
| 7 x64 | AUD-CTL (wave 3 `2d0de39`) | rel / qemu | normal on rel, 1 slow of 2 on qemu | `release` 6 of 6 at about 20 s, about 20100 packets each (`ctl-int3-times.txt`); `qemu` trace of a slow play: Ring Underrun, then a 41 s stall (`tr3q-ts.log`) |

**Owed to the stamped run** (the full matrix): every row this pass
dropped, which is all of the checklist's rows except those above. These
include:
- `XhciWideMap` default 0 against 1 on every guest, and 36.26 set 5b;
- B11 with the `Control\usbstor` path (M-4) and the XP x64 `XHCISNAP` dump (M-3);
- 98 SE 36.46 L5 with every wizard finished;
- wave 3's not-run rows;
- Verifier V1 on 7 x86;
- the Vista/7 Device Manager power data.

It also owes stage A's 36.35 rows, 36.28 part 2, the 36.41 re-measure,
and the VIA fix's rows. On top of those: the 98 SE standby and Windows 7
audio rows again after their fixes, and the 2000 SMP mode-2 warm restart
on a quiet host.
