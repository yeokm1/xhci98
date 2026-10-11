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

### 36.41's qualifying re-measure on stage A's build (2026-10-10)

Development host A, QEMU 11.1.0, the same kit (copied to
`out\p36-susp\audio`) and bases; the `release` x86 package of branch
`p36-susp` at `4282007` (`xhci98.sys` 240,391 bytes, SHA-256
`b462112b...`), selective suspend's stage A built in. `XhciSelectiveSuspend`
absent (0) on the base `w98-audio-xhci-base`; set to 1 on an overlay of it
made a base of its own (`vm\t36sau\w98-audio-xhci-ss1-base.qcow2`), so the
usb-audio device's QUERY_CAPABILITIES carries stage A's answers (D1, D2,
wake from D2) there. Three batches in parallel slots; verdicts as the
baseline's (`out\p36-susp\audio\reps.txt`, `reps-run1.txt`; two
repetitions failed in the kit, an overlay race and an admission timeout,
and are not counted).

| Attach after t0 | switch 0 | switch 1 | baseline `xhci98` | UHCI control |
|---|---|---|---|---|
| 120 s | 6 of 9 wedged | 5 of 9 | 14 of 20 | 7 of 12 |
| 300 s | 3 of 3 | 2 of 2 | 7 of 13 | 4 of 12 |

**Verdict.** The same: stage A's capabilities change nothing a count this
size can see (7 of 11 at switch 1 against 9 of 12 at 0, 21 of 33 for the
baseline and 11 of 24 for the UHCI control with no `xhci98` at all). The
wedge stays Windows 98 SE's own, a documented limitation; waiting longer
before attaching does not avoid it on this vehicle, so the release notes
no longer advise it.

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

## 36.35 - stage A's completion pool: the 98 SE shutdown hang (2026-10-10)

**The defect.** With `XhciSelectiveSuspend` 1 and the test client bound
in D0, Windows 98 SE hung at "Windows is shutting down" (`8d5baa1`, both
flavours; with no client device bound the shutdown took 7 s). Traced
with temporary lines (`out\p36-susp\legs\w98\tmp-shutdown-traces.patch`,
not committed): the PDO's S5 SET_POWER, the client's D3, the Dx pended,
the suspend sequence, the Dx claimed and handed off, the enum thread
leaving the gate - then `PsCreateSystemThread` in the hand-off's thread
creation (`HcdSuspDoneLaunch`), and nothing after it. The client
completes its system IRP only in its D3 callback, so the shutdown
waited for ever. It was the wake-wedge fix's per-IRP completion thread,
created at completion time.

**The fix** (record 14 section 3.12; `a8ffc73`, `8201e2d`, `8b9a272`,
`14166b6`): the claimed IRPs are completed by a pool of parked workers
that the PnP start provisions (two, and a grower thread, while the switch
is on; one not had turns the switch off for that start). No completion
and no enum pass waits on a thread's creation; the grower adds a worker
only after an IRP has waited 500 ms with none parked. Codex thread
`01a12620`: round 1 four P1s (growth on the enum thread, a ceiling that
deadlocks a deep chain, the drain racing a power-path hand-off, the 9x
flag join), round 2 one P1 (the drain's power-gate barrier rests on an
event wait 9x may leave unsatisfied) and one P3, round 3 clean. Two
residuals are stated in the record, not closed: a chain deeper than the
spares during a 9x transition needs a creation the transition may block,
and the 9x flag join proves the context's last use, not the image's exit.

**Readings.** Development host A, QEMU 11.1.0, the `release` x86 package
of `14166b6` (`xhci98.sys` 268,183 bytes, SHA-256 `8951b850...`), the
test client (`XHCIPM.SYS`), `XhciSelectiveSuspend` 1; raw texts in
`out\p36-susp\legs\w98\notes-W3.txt` and `legs\w7x86\`.

| Guest | Leg | Reading | Verdict |
|---|---|---|---|
| 98 SE | the start | `susp.done.pool` 2, `susp.switch` 1 | as designed |
| 98 SE | S3 suspend | WAIT_WAKE held, D2: `susp.seq` `00102110`, `SuspSuspended` 1, `SuspArms` 1 | pass |
| 98 SE | S4 remote wake | the WAIT_WAKE completed `0` and its callback ran, the client's D0, reads 6 to 14; `00120350`, `SuspWakeEvents` 1, `SuspDisarms` 1 | pass |
| 98 SE | S5 host resume | D2 then D0: `00102110`, `00120250`, `SuspResumesBegun` 1, reads 14 to 20 | pass |
| 98 SE | shutdown, the client in D0, reads running | off in 7 s (was a hang) | pass |
| 98 SE | standby, the client in D0 | S3 reached (no hang on entry); at `system_wakeup` the guest reset | entry pass; resume is 36.28 part 2's |
| 98 SE | standby, no USB device (control) | S3 and back to the desktop; a console program then run stopped with fatal exception 0E at `BFF9DFFF`, the address 36.28 part 1 read after a 98 SE standby on `ebe3ec9` | the open post-standby defect, not this fix |
| 7 x86 | the start | `susp.done.pool` 2 | as designed |
| 7 x86 | S3, S4, S5 | `00202110`, then `00220350` (wake: WAIT_WAKE `0`, callback, D0, reads 6 to 14), then `00202110`, `00220250` (reads 14 to 20) | pass |
| 7 x86 | shutdown, the client in D0 | off in 18 s | pass |
| 7 x86 | hidusb's own idle (brief C1) | a `usb-mouse` with `SelectiveSuspendEnabled` 01 in its Device Parameters, replugged, four minutes untouched: no idle IRP reached the bus | Windows 7's hidusb does not idle QEMU's mouse on that value alone |

**Owed.** 98 SE standby's resume (36.28 part 2, the open post-standby
defect, now readable with the client bound since the entry no longer
hangs); the rest of 36.35's legs in the combined matrix; the bench
(36.36).

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
  - Settled 2026-10-11: QEMU's SDL display stalling QEMU's main loop, not the driver - see "## Windows 7 slow audio in the early-warning matrix" below; these rows are harness readings, rerun with -display none in the stamped matrix.
- **98 SE CP1 standby (pre-existing, not a regression of this build).**
  After `system_wakeup` USB is dead and the desktop does not come back.
  The trace stops after "slots invalidated, dropping every device" and
  "hub leaving". With `usb-audio`, a later fatal exception 0E follows
  at `BFF9DFFF`. This is the open 36.28 part 2 standby defect. Evidence:
  `w98\debugcon-6.log`, `debugcon-7.log`, `wake-eips.txt`,
  `shots\w75-w80.png`. Settled 2026-10-11: not the bus's - see "## 36.28 part 2" below.
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
| 98 SE | CP1 Stand by | rel | FAIL (pre-existing; settled 2026-10-11 as Windows 98 SE under QEMU, not the bus - "## 36.28 part 2") | standby entered; after `system_wakeup` the desktop never returned (CPU looping in VMM, `wake-eips.txt`, `w75-w80.png`); audio playback before sleep not makeable (`USBAUDIO` fatal exception 00, `w60.png`) |
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
| 7 x86 | AUD-W7 | rel | VOID - harness (QEMU SDL display stalls; settled 2026-10-11, see the Windows 7 slow audio section) | `TONE.WAV` 20 s took 63 s; 3 plays `IsoPacketsAnswered` 14546, Ring Underrun 11 (`cp1.txt`) |
| 7 x64 | I1-I4, SB1, SB2 | rel | PASS | install under F8, bc clean; MSI delivery 3, `MsiRoutines` 1; switch 0 as before; switch 1: DNCTRL `0x0002`, one `susp.ss.cap`, `SsSuspCapFailed` 1, Ss 1, `SuspSwitchRead` named (`sb2.txt`) |
| 7 x64 | WideMap=1 | rel | PASS | check passed, promoted 256, wide entries 8; VERIFY clean at 1 MiB and 2 MiB (`wm-*.txt`) |
| 7 x64 | CP1 S4 | rel | PASS | hibernate; all started, fc clean, hub tablet moves; `TONE.WAV` 20.6 s right after |
| 7 x64 | V1 | rel | PASS | Verifier /standard on `xhci98.sys` with audio + hub at boot: desktop, MSI, fc clean, dc/ec, no bugcheck, 206119 allocations, 0 failed (`v1-after.txt`) |
| 7 x64 | CP1 trace | qemu | PASS (ruling) | `00000504` x2, `00000101` x2 (`debugcon-4.log` 74-75); all started (`qcp1-post.txt`) |
| 7 x64 | AUD-W7 | rel | VOID - harness (QEMU SDL display stalls; settled 2026-10-11, see the Windows 7 slow audio section) | 20 s tone in 106 s, 8290 packets (`aud1.txt`); quiet host 0 of 6 normal (`ctl-mtx2-times.txt`) |
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

## 36.28 part 2 - 98 SE standby resume

2026-10-11, development host A, QEMU 11.1.0 (`-machine pc,smm=off`, TCG,
one Pentium III, 256 MB, `-device VGA`), Windows 98 SE from the matrix's
`win98-gold` lineage (a copy of the early-warning qemu overlay, never the
base). Build: branch `p36-sby` at `p36-int5` `12e4b03`, `qemu` flavour,
with temporary traces on every power IRP of the controller FDO, the
root-hub FDO and PDO, the hub FDOs and the device PDOs, on the D0 route
and the system IRP's request, and on every internal IOCTL a device PDO
received after its first D-IRP. The traces were never committed. Kit and
evidence (not tracked): `out\p36-sby\kit` (`sb.sh` standby/wake helper),
`out\p36-sby\legs\w98`, `out\p36-sby\legs\w98u`.

**What the controller does on a 98 SE resume.** Every resume took
`hcdD0Finish` inline: the D0's completion routine ran at PASSIVE_LEVEL
(`irql` 0) and the work item was never used; the D0 then went through
`XhciResumeController`'s reinitialization (QEMU has no Force Save
Context), dropped every device and completed. The system IRP's
`PoRequestPowerIrp` callback ran at PASSIVE_LEVEL too. The direct
transition never ran. Two orders were seen: S0 first and its requested D0
after (a mouse alone, a hub and a mouse), or a D0 sent alone with no S0
before it (whenever a USB stick was attached, and in the early-warning
matrix's runs 6 and 7, which had one).

| Run | Devices | What happened after `system_wakeup` | Reading |
|---|---|---|---|
| xHCI 1 | a mouse on a root port | S0, D0, reinitialization, the mouse re-enumerated; the pointer moves | resume passes |
| xHCI 1 (cont.) | the same | an MS-DOS prompt, then XHCISNAP: fatal exception 0E at `016F:BFF9DFFF` | the post-standby fault reproduced |
| xHCI 7 | a hub, a mouse behind it | S0, D0, the hub FDO's S0 and its requested D0 (`asked` `00000101`), the hub and the mouse re-enumerated; the pointer moves through the hub | resume passes |
| xHCI 5, 6 | a SuperSpeed stick (with or without a hub and mouse) | D0 first; the controller, the root-hub FDO and the stick's PDO each completed theirs (`dev PDO power completed=00000000`); then nothing reached the bus again - no S0 to any object of this driver, no internal IOCTL on the stick's PDO, no request held; the No Op self-test still completed, so the controller and its thread were live; black screen, VMM idle EIPs `C00036xx`, `C000A6E7`/`A881`, `C0051551` | Windows stops after the stick's D0 |
| xHCI 9 | the stick, with the bus's re-enumeration held until S0 (temporary experiment) | the same stop after the stick's D0, with no device dropped and nothing reported missing | the stop is not caused by the bus dropping the stick |
| none 4, 14 | no USB controller at all | standby and back to the desktop; an MS-DOS prompt then reads "There is not enough memory available to display the program"; XHCISNAP in it: fatal exception 0E at `0167:BFF87EDE`; Ctrl+Alt+Del then: 0E at `0167:BFF9DB34` | the 0E in KERNEL32 needs no USB stack |
| UHCI 1, 2 | 98 SE's own `uhcd.sys` (NUSB 3.3 above it): a stick; then a mouse alone | both: black screen after the wake, the CPU spinning at `C002F1B0` | 98 SE's own controller driver does not resume on this vehicle either |
| EHCI 12, 13 | NUSB 3.3's EHCI stack: a stick; then a mouse alone | the stick: black screen after the wake, spinning at `C002F1FD`; the mouse: standby refused ("a device driver or program won't allow it") | the stick's stop is not this HCD's |

(Run numbers are the kit's launch counters; `none 4` was a fresh boot.
Two vehicle artefacts were seen on the way: a Windows restarted from
MS-DOS mode came back from standby to a black screen with no USB
controller at all, and one `system_wakeup` reset the guest before any
driver code ran - stage A's 36.35 table read the same reset.)

**Verdict: not this driver.** The fatal exception 0E in KERNEL32
(`BFF9DFFF` and its neighbours) follows a Windows 98 SE standby on QEMU
with no USB host controller in the machine; the stop after a USB stick's
D0 happens with this driver idle and owing nothing, unchanged when the
bus defers its re-enumeration, and the same stick stops a resume under
NUSB's EHCI stack; 98 SE's own UHCI driver does not resume at all. With
only HID devices and hubs on the bus, the resume completes and the
devices work through this driver. No code change. ME-1 (ME standby or
hibernate with a copy running) is not touched by this reading.

**Release-notes wording (draft, for 36.20):** "Stand by on Windows 98
SE has been read only in QEMU virtual machines, where Windows 98 SE
itself does not resume reliably: a console program or Ctrl+Alt+Del after
the wake can stop with a fatal exception 0E in KERNEL32 even with no USB
controller in the machine, and with a USB disk attached the resume stops
before the desktop returns under this driver and under NUSB's EHCI
driver alike. With only USB mice, keyboards and hubs attached, the
resume completed and the devices worked. On real hardware Stand by is
untested; remove USB disks before Stand by." (The release notes and
README now carry the refined wording from the SweetLow control below.)

**SweetLow's stack control (2026-10-11, branch `p36-swl`).** The same
vehicle and procedure (development host A, QEMU 11.1.0, `-display none`,
power throttling opted out), on 98 SE with NUSB removed and SweetLow's
XP-SP2-derived USB 2.0 stack installed (`vm\t26\win98-sweetlow-base`).
`A` = SweetLow's EHCI on QEMU's `usb-ehci`, no xHCI. Because SweetLow's
stack alone has no `usbd.sys`, it was extracted from the 98 SE CD (root hub
Code 2 before that). `B` = the stamped 2.2.0.3 `release` on the same
system, as the final matrix installed it. `N` = the same release on the
NUSB 3.3 gold system (installed file byte-identical). Mass storage is the
same class driver on all three: SweetLow's package has no `usbstor`, and
NUSB 3.3's `USBSTOR.SYS` (21,040 B) stays behind. Evidence:
`out\p36-swl\legs\notes.txt` (RESULT lines), `legs\sla`, `legs\slx`,
`legs\nsx` (shots, EIP samples, traces).

The stick result turns on whether the BIOS saw the stick. SeaBIOS
presents a boot-attached stick as an INT 13h disk, and Windows 98 SE
then runs that drive in MS-DOS compatibility mode ("Drive E is using
MS-DOS compatibility mode file system" on the Performance tab). With such
a drive, this driver's resume stopped every time: 4 of 4 on the SweetLow
system (one on the `qemu` flavour), including a run where the stick was removed during standby, and
1 on NUSB. With a stick the BIOS never saw (hot-plugged, or added after
POST while the Windows logo showed), it resumed 6 of 6 (one on the
`qemu` flavour) and the stick
stayed attached. A `qemu`-flavour 2.2.0.3 trace shows the difference is
above the bus. In both cases the controller's D0 arrives alone, the
reinitialization runs, every device is dropped and the No Op self-test
completes. In the passing case Windows then sends S0 and the root hub's
QUERY_DEVICE_RELATIONS, and the stick is re-enumerated and configured.
In the stopping case Windows sends nothing more: no S0, no relations
query, and no transfer is outstanding. SweetLow's EHCI resumes with a
BIOS-mapped stick, but it does not re-present the stick (the tray icon is
gone and the drive's window has closed). So this driver returning the
device is the one visible difference. It is not a defect of the bus: the
stop needs neither the stick's return nor any request to the stick, and
NUSB's EHCI stopped the same way with a boot-attached stick (EHCI 12 above).
The `KERNEL32` 0E appears on every system after any wake: any program
started (Explorer included), MS-DOS prompts and Ctrl+Alt+Del.

| Run | Devices | What happened after `system_wakeup` | Reading |
|---|---|---|---|
| SweetLow EHCI (A1) | a stick hot-plugged at the desktop | desktop back, Start menu; the stick not re-presented; My Computer, then Ctrl+Alt+Del: 0E at `016F:BFF9DFFF` | resume passes, stick lost (SweetLow's stack) |
| SweetLow EHCI (A1b) | a boot-attached stick, drive E in MS-DOS compatibility mode | desktop back, Start menu; stick not re-presented | resume passes, stick lost |
| SweetLow EHCI (A2) | a mouse alone | standby refused 2 of 2 ("a device driver or program won't allow it"); with the mouse then removed, standby entry hung (EIPs `FF05xxxx`) | not makeable on SweetLow's EHCI |
| SweetLow EHCI (A3) | a hub, a mouse behind it | QEMU's hub is Full Speed only and cannot attach to an EHCI port; with ICH9 UHCI companions the hub ran on 98 SE's `uhcd`: hang at `C002F1B0` | not makeable; the companion is the UHCI hang above |
| xHCI 2.2.0.3, SweetLow system (B1) | a stick hot-plugged, or added after POST | 3 of 3: desktop back, the stick stays; a new program, then Ctrl+Alt+Del: 0E at `BFF9DFFF` | resume passes |
| xHCI 2.2.0.3, SweetLow system (B1b) | a boot-attached stick (compatibility mode), once removed during standby | 4 of 4: the pre-standby frame stays, VMM EIPs as xHCI 5/6; traced: the bus idle after D0, no S0 | Windows stops above the bus |
| xHCI 2.2.0.3, SweetLow system (B2) | a mouse alone | desktop back, the pointer moves; MS-DOS prompt "not enough memory"; Ctrl+Alt+Del: 0E | resume passes |
| xHCI 2.2.0.3, NUSB system (N) | a stick added after POST / hot-plugged; then boot-attached (compatibility mode) | 2 of 2 pass; boot-attached: stop (one more boot-attached run passed, but its compatibility mode was not confirmed) | as on SweetLow's system |

**Verdict unchanged: not this driver.** The USB-disk stop is Windows 98
SE stopping after a resume while it holds a BIOS-mapped
compatibility-mode drive. A disk plugged in after Windows started
resumes. The release-notes limitation and the README row now name the
"attached when the computer started" case. Remove USB disks before Stand
by stays the advice.

**Owed.** 36.28's part 2 on the bench (E460, P14s Gen 1: standby with a
stick); the stage A, pended-D0 and `XhciInterruptMode` 2 resumes of
part 2 on guests that resume (ME standby, the NT hibernates) in the
combined matrix.

## Windows 7 slow audio in the early-warning matrix: QEMU's display, not the driver (2026-10-11)

**The report.** The early-warning matrix on `0b7c395` (agent E, AUD-W7,
AUD-CTL-1, CP1) timed `TONE.WAV` (20 s) over QEMU's `usb-audio` on Windows 7
x86 and x64 at about 63 s, sometimes 106 s, with fewer packets answered than
the ~20,100 of a normal play; Vista on the same build played normally, and a
6-play control of the wave 3 build (`2d0de39`) on a fresh overlay was 6/6
normal, so the slowness was booked as a regression between `2d0de39` and
`b1b20bf`. A qemu-flavour trace of a slow play (agent E, `tr3q-ts.log`)
showed a mid-stream Ring Underrun, ~41 s with no driver line and no event,
then usbaudio's ABORT_PIPE and SYNC_RESET_PIPE_AND_CLEAR_STALL and a normal
replay.

**Not a regression.** Six plays per build on one 7 x64 overlay (branch
`p36-aud`, development host A, QEMU 11.1.0, amd64 `release` builds of the
merge points): `2d0de39` (int3) 2 slow of 6 (50.0 s and 63.1 s, ~22,000
packets each, the replay shape), `fc990da` 0/6, `d7d19ae` 0/6, `53cfb21` 1/6
(48.7 s), `b1b20bf` 0/6 twice, the head `12e4b03` (qemu flavour) 0/10. The
wave 3 build is not immune, and no merge flips the rate.

**The mechanism.** A host-side probe of QEMU's main loop (an HMP `info
status` round trip every 50 ms) finds it blocked for 160 to 490 ms about once
a second on a Windows 7 guest whose screen is changing (a blinking cmd
cursor is enough): 8 to 18 stalls per 20 s, none while the guest is
paused, none on a still desktop, and the same with the xHCI controller
disabled in the guest - so neither the driver nor the guest's USB traffic
causes them. With `-display none` the probe reads 0 stalls over every run
(max 1 ms). The stall stops the whole device model: a timestamped
`usb_xhci_*` trace of a slow play has no doorbell, no kick and no event of
any device for 0.2 to 0.5 s, about 20 times in a 27 s play. QEMU schedules a
Start Isoch ASAP TD one Interval after the last one it ran
(`xhci_calc_iso_kick`, `external\qemu\hcd-xhci.c`), with no catch-up after
a late timer, so every stall is added to the stream's length. Win7's
usbaudio sends ASAP URBs (SIA); and QEMU's controller reports CFC = 0
(HCCPARAMS1 `0x00080001`), under which software may set a Frame ID only in
a data flow's first TD and shall set SIA in the rest (xHCI 1.2c 4.11.2.5,
p.199-200), so the driver cannot pace the stream by Frame IDs here. The
63 s replay shape is a stall long enough for usbaudio to give up on the
stream and reset the pipe; its 41 s length was not reproduced.

**A/B on one build.** Head `release` (`a71a2bd3`, `out\p36-int5`), 7 x64,
the same overlay, outputs and guest clock: with the SDL display 26.2 /
27.1 / 27.7 / 27.5 / 26.5 / 27.0 s (6/6 slow, all ~20,100 packets); with
`-display none` 20.4 / 20.2 / 20.2 / 20.2 / 20.2 / 20.2 s. The head qemu
flavour the same way (SDL 24.1 to 28.0 s; none 20.2 to 20.8 s). 7 x86, head
`release` x86 (`46329030`), fresh overlay, `-display none`: 21.2 / 20.3 /
20.2 / 20.2 / 20.2 / 20.2 s. Moving the overlay delta, wav, debugcon and
serial files out of the OneDrive tree, `-audiodev none` and a guest clock
inside the activation grace period changed nothing on their own.

**The driver's underrun path.** A Ring Underrun leaves an isoch endpoint
Running and off the schedule until software rings its doorbell (xHCI 1.2c
4.10.3.1, p.185). The bus rings the doorbell on every TD it publishes
(`HcdIoMapped`, `hcd_io.c`) and on an underrun that leaves work queued
(`hcdIsoEvent`, `hcd_dev.c`). Forced with the guest's vCPU threads
suspended for 150 ms eight times in one play (`-display none`, head qemu
flavour): eight underruns, each followed within milliseconds by usbaudio's
abort and reset and a resumed stream; the play took 21.5 s, the next two
20.2 s. One 400 ms suspension on the `release` build of `0b7c395`: the
same recovery, 21.5 s.

**Consequence.** No driver change. Audio timing legs (and any other leg that
times isochronous streams) run with `-display none` - screenshots still come
through the monitor's `screendump` - or are read as host-dependent. The
early-warning AUD-W7 / AUD-CTL rows are harness readings, not a defect of
`0b7c395`. Vista's normal plays on the same host were not examined.
Raw texts: `out\p36-aud\legs\w7x64\*-times.txt`, `legs\w7x86\x86-h5r-times.txt`,
traces `legs\w7x64\qtrace-{2,3,4}.log` and the local temp directory's
`qtrace-8.log`; harness `out\p36-aud\kit` (`probe.ps1`, `stall.ps1`,
`stallplay.ps1`, `launch.ps1 -Display`).

## 36.53 - XhciInterruptMode 1 the line only, 3 a message only (2026-10-11)

Branch `p36-imode` at `4e11974` (2.3.0.0 merged, with 36.51's INFs), the
`release` and `qemu` packages of that tree, QEMU `qemu-xhci` with its
default INTx, MSI and MSI-X, development host A. The reason of a refused
start was read from the `qemu` flavour's `M86 S` record (debugcon) and,
on XP, also from DebugView on `release`.

| Guest | Mode | Flavour | Result |
|---|---|---|---|
| 7 x64 | 0 | release | MSI-X (delivery 4), reason 10; mouse, stick copy fc-clean: PASS |
| 7 x64 | 3 | release | MSI-X, reason 15, filter outcome 12 (the line choices stripped); mouse, stick: PASS |
| 7 x64 | 1 | release | the line, reason 5, filter outcome 10; mouse: PASS |
| 7 x64 | 1, `XhciMsi86InjNoLine` 1 (only a message offered) | qemu | Code 10; `M86 S` grant 2, outcome 6, delivery 0, reason 14, status `C0000182`: PASS |
| 7 x64 | 3, `XhciQemuMsgConnectFail` 1 | qemu | Code 10; reason 17: PASS |
| 7 x64 | 3, `MSISupported` 0 and a restart | qemu | Code 10; grant 1, outcome 8, reason 16: PASS |
| 7 x86, helper bound | 0 | release | MSI-X, reason 10, routines from `xhcimsi.sys`; mouse, stick: PASS |
| 7 x86, helper disabled | 0 | release | helper ABSENT (`C0000034`); the line, reason 1; mouse, stick: PASS |
| 7 x86, helper disabled | 3 | release, qemu | Code 10 on both; `M86 S` grant 2, bind 1, outcome 12, reason 17: PASS |
| Vista x86, helper bound | 0 / 3 / 1 / 2 | release | MSI-X reason 10 / MSI-X reason 15, outcome 12 / line reason 5, outcome 10 / poll reason 8; mouse (and stick under 0 and 3): PASS |
| Vista x86 | 1, `XhciMsi86InjNoLine` 1 | qemu | Code 10; reason 14 (the trace validator accepted the capture): PASS |
| Vista x86 | 3, `XhciQemuMsgConnectFail` 1 | qemu | Code 10; outcome 12, reason 17: PASS |
| XP | 0 | release | the line, reason 1; mouse, stick: PASS |
| XP | 3 | release, qemu | Code 10 on both; DebugView `intr.reason` and `start.refused.delivery` 18; `M86 S` reason 18, status `C0000182`: PASS |
| XP | 1 / 2 / 0 again | qemu | line reason 5 / poll reason 8 / line reason 1: PASS |
| 98 SE | 0 | release | the line, reason 1; mouse, stick: PASS |
| 98 SE | 3 | qemu | Device Manager: "This device is either not present, not working properly, or does not have all the drivers installed. (Code 10.)"; `M86 S` reason 18: PASS |
| 98 SE | 1 / 0 again | qemu / release | line reason 5 / line reason 1: PASS |

Every refused start returned `STATUS_DEVICE_CONFIGURATION_ERROR`
(`C0000182`). Not read: a controller that really offers no line (QEMU's
always has one; `XhciMsi86InjNoLine` made the list message-only), MSI
rather than MSI-X under mode 3 (`msi=on,msix=off`), 64-bit Vista, ME,
2000, XP x64, and `XhciLogDebugView` on 98 SE. `check-msi86-trace.ps1`
reads an XP or 98 SE capture's `S` record and then refuses the capture
for having no requirements list, since those systems never send one.
Raw texts: `out\p36-imode\legs\<guest>\notes.txt` and the files it names.

## 36.50 - the x64 test signature: the rehearsal (2026-10-11)

Design record 19 (owner, 2026-10-11): the published x64 `xhci98.sys` and
`xhciuas.sys` carry an embedded Authenticode test signature by the
project's own certificate (thumbprint
`A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF`; SHA-1 file digest, no
timestamp, no catalog). The signed `2.2.0.3` re-cut was cancelled; the
first signed cut is `2.3.0.0`. Before it, a rehearsal on the same code:
branch `p36-tsign` at `e8b1b09` (the `2.3.0.0` code before its stamp,
version still `2.2.0.3`), amd64 `release` built with every gate, and the
owner ran `make-package.ps1 -Flavor release -Arch amd64 -TestSignPfx ...`
himself into `out\p36-tsign\rehearsal\pkg-release-amd64`.

**The signature** (`out\p36-tsign\rehearsal\verify.txt`): both binaries
signed by that thumbprint, an Authenticode PE-image signature with SHA-1 as
both digests, the vouched digest equal to the image's, no timestamp; each
exactly the build plus the signature (`xhci98.sys` 310,272 to 311,768
bytes, `xhciuas.sys` 36,352 to 37,848); `SignTool verify /pa /v` shows the
chain and "File is not timestamped", then the expected untrusted-root
failure. The INFs, `txtsetup.oem` and `portsw.vbs` are byte-identical to an
unsigned package of the same build. SignTool left key containers behind
with the owner's key too; the owner deleted them (record 19 section 5).

**The legs** (QEMU on development host A, the signed `release` package,
`out\p36-tsign\legs\<guest>\notes.txt`):

| Guest | Row | Reading | Verdict |
|---|---|---|---|
| Vista x64 | test-signing on, no F8 | `bcdedit /set testsigning on`, ordinary starts: install after "Windows can't verify the publisher of this driver software" (Install anyway), no Program Compatibility Assistant box; controller started, mouse, stick copy + `fc`, `XHCISNAP` release build; UAS disk on `xhciuas`; a second restart the same | PASS |
| Vista x64 | test-signing off, no F8 | Code 39: "Windows cannot load the device driver for this hardware. The driver may be corrupted or missing. (Code 39)"; `xhci98`, `xhciuas` STOPPED, exit 1077; Code Integrity event 3004 for `xhci98.sys`; no notification | PASS |
| Vista x64 | watermark | "Test Mode" in all four corners while activation is in grace; with it expired the not-genuine watermark replaces it (the mode still on) | read |
| Windows 7 x64 | test-signing on, no F8 | as Vista: same prompt, no PCA box, devices and UAS working, a second restart the same; watermark "Test Mode / Windows 7 / Build 7601" bottom right | PASS |
| Windows 7 x64 | test-signing off, no F8 | Code 52: "Windows cannot verify the digital signature for the drivers required for this device. ... (Code 52)"; `xhci98`, `xhciuas` STOPPED, exit 1077 | PASS |
| XP x64 | install, restart | the usual Logo-testing box, Continue Anyway; controller, stick, mouse working across a restart, as unsigned | PASS |

On all three the Driver tab's Digital Signer reads "Not digitally signed",
in both modes: expected for a package with no catalog. Harness: the Vista
x64 and Windows 7 x64 bases carry `advancedoptions`, removed so the starts
were ordinary. Not read: real hardware; a Windows 7 with the SHA-2 update.
36.50 is ticked after the `2.3.0.0` cut's own signature check.

## Matrix (final, stamped 2.2.0.3)

The full combined VM matrix, run on the stamped `2.2.0.3` TEST build: every checklist row on every guest, on
the packages the owner will bench. It is also the install-from-zip evidence. It supersedes the early-warning
pass above for every row it read again. Development host A, QEMU 11.1.0 (TCG), 2026-10-11 06:15 to 17:16
(per-guest times in the summary table). Coordinator branch `p36-final`. Kit, checklist, brief and evidence:
`out\p36-final\` (`kit\`, `checklist.md` with this run's section 12, `brief-legs.txt`, `assign.txt`, per-guest
`legs\<guest>\notes.txt` with every RESULT line, texts, traces and shots; `suspect.txt` is the suspected-failure
log with each follow-up).

**The packages.**
- `release` and `debug`: installed FROM `out\xhci98-2.2.0.3.zip` (1,031,267 bytes, SHA-256
  `a6d91ec43cf91ea7007922e3b67140c64670e296bf5519fa279d020069affaa8`), unpacked to `out\p36-final\zip`. Every
  binary matched `out\p36-stamp\hashes.txt`.
  - `release` x86: `xhci98.sys` `427b9d98...`, `xhcimsi.sys` `abb1319c...`, `xhciuas.sys` `2d60ce46...`.
  - `release` x64: `xhci98.sys` `fe1cf7b3...`, `xhciuas.sys` `7f3bfd95...`.
  - `debug` x86 `xhci98.sys` `b108c28f...`; x64 `38269385...`.
  - `XHCISNAP.EXE` `79fe8faa...` (all flavours); `XHCIQUAL.EXE` `d5ab9fd6...`.
  - Version 2.2.0.3, DriverVer 10/10/2026. The source is `2.3.0.0` at `20f08ee`, with the driver binaries
    built at `0d57571`.
- `qemu` flavour (fault injection and traces only, never in the zip): `out\p36-stamp\pkg-qemu-x86` `xhci98.sys`
  `db0c4847...` and `pkg-qemu-amd64` `35845597...`.
- The zip's INFs write no `XhciWideMap` line, so every guest read it absent: the driver default, promotion off.

**The harness.**
- The kit is `out\p36-final\kit`, a copy of `out\p36-aud\kit` with the following settings:
  - locks `<guest>-fin`, QEMU name `p36fin`, HMP 57100+idx, QMP 57130+idx, overlays `vm\t36fin`;
  - flavours `rel`/`dbg` (the zip) and `qemu`;
  - `-Rtc base=2026-09-20T12:00:00` on Windows 7 (activation);
  - `-Display none` for every audio or timed isochronous leg.
- Seven leg agents ran in parallel under the global admission lock: 98 SE; ME with the two clean 98 SE bases;
  2000, 2000 SMP and the clean 2000; XP and XP x64; Vista x86/x64; 7 x86/x64; the XHCIQUAL DOS matrix plus the
  36.41 re-measure. Up to 16 of the run's guests ran at once; other workers' guests shared the host from about
  10:50.
- **Host power throttling (harness).** The host left Modern Standby at 08:03:34 (Kernel-Power 507). From then
  on, Windows power-throttled the windowless QEMU processes, and the guests' clocks ran at about 1/7 of real
  time.
  - Fixed at about 08:33: every QEMU process was opted out with `SetProcessInformation(ProcessPowerThrottling,
    ControlMask 0x5, StateMask 0)` (`kit\nothrottle.ps1`), and the launcher now does this on every launch.
  - Every timed row read in that window was read again (ME T8, XP T8, the Vista plays).
  - Lesson: keep the host awake during a VM matrix, or opt QEMU out of power throttling.

**Summary per guest** (rows = RESULT lines, including attribution and follow-up rows; "other" = INFO,
superseded, bisect, NOT HIT or RECORDED rows):

| Guest | Date/time (2026-10-11) | Rows | PASS | FAIL | NOT MAKEABLE | NOT RUN | other |
|---|---|---|---|---|---|---|---|
| 98 SE | 06:27-15:40 | 63 | 57 | 4 | 1 | 0 | 1 |
| ME | 06:28-14:18 | 67 | 45 | 2 | 1 | 1 | 18 |
| 98 SE NUSB clean | 06:28-08:47 | 11 | 10 | 0 | 1 | 0 | 0 |
| 98 SE SweetLow clean | 06:28-08:53 | 11 | 11 | 0 | 0 | 0 | 0 |
| 2000 SP4 | 06:28-15:32 | 37 | 32 | 2 | 2 | 0 | 1 |
| 2000 SMP + Verifier | 06:28-15:32 | 30 | 28 | 0 | 1 | 1 | 0 |
| 2000 clean | 06:28-07:18 | 13 | 13 | 0 | 0 | 0 | 0 |
| XP SP3 | 06:27-15:55 | 55 | 54 | 1 | 0 | 0 | 0 |
| XP x64 | 06:27-13:56 | 50 | 38 | 4 | 3 | 1 | 4 |
| Vista x86 | 06:28-17:16 | 45 | 43 | 2 | 0 | 0 | 0 |
| Vista x64 | 06:28-16:32 | 43 | 38 | 4 | 0 | 0 | 1 |
| 7 x86 | 06:27-15:40 | 48 | 45 | 1 | 0 | 0 | 2 |
| 7 x64 | 06:27-14:06 | 53 | 46 | 0 | 4 | 0 | 3 |
| XHCIQUAL (DOS) | 06:15-06:33 | 2 | 2 | 0 | 0 | 0 | 0 |
| 98 SE 36.41 | 06:25-07:37 | 8 | 8 | 0 | 0 | 0 | 0 |

**What passed, by area.** Every guest that reached the area passed these.
- **Install and readings.**
  - Install from the zip: the 10 install legs of section 9, plus I1 on every regression guest.
  - The INF values with `XhciSelectiveSuspend` absent; the Driver tab reads 2.2.0.3.
  - bc against the zip's binaries.
  - Counter alignment I4 on every guest and flavour.
  - The 36.47 header on every guest (K6/K6x). XP x64 now prints the `System\BIOS` strings (M-3 fixed). No
    computer name, user, serial or UUID appears in any file.
- **Polling and interrupts.**
  - 36.13 polling: P1 mode 2, P2 modes 0 and 1, the qemu dead-line legs E-1/E-2/P3/P4 on NT, P5 and P9 through
    DebugView, P8/Q2 forced recovery (M-2: the park is not required).
  - MSI and MSI-X on Vista and 7 (M1-M4, F, H, U1/U2): see the MSI-X note below.
- **Devices and tools.**
  - 36.17-36.19 (G1-G3).
  - 36.43 composite parent (K2) and K3, the update from 2.2.0.0: values preserved, the composite re-detected
    once, nothing after a reboot.
  - 36.45 strings with the usbview hammer (K4), 36.46 (K5).
  - 36.42 `portsw.vbs` (K1).
  - Issue 18 regression I18, on 98 SE, XP and 7 x64: a Full Speed device behind a usb-hub hot-added after the
    desktop. hub.speed.fallback reads 1, and 0 with the switch off.
- **Transfer sizes.**
  - 36.22/36.24/36.26: T1-T8 and B1-B3, B5-B7, B11 (XP x64 with `Control\usbstor`: 2 MiB commands, source 5),
    B13.
  - 98 SE and ME: nothing above 64 KiB, "not run (Windows 98 or ME)" as designed.
- **Selective suspend.**
  - Stage B: SB1-SB7. SB4's mid-D2 window cannot be hit in QEMU.
  - Stage A 36.35 rest:
    - 98 SE: shutdown with the XHCIPM client in D0 completes in 4.5 s (was the hang); S3-S5, A3-A6.
    - ME: A0-A3, client STOP/START x2, shutdown with the client D0 in 7 s. ME's WAIT_WAKE completion and
      callback both ran.
    - 2000 and 2000 SMP + Verifier: B0-B5, shutdown with a read pending.
    - XP: S4/S5 reread with reads resuming after the wake, C3-C5.
    - Vista x86 and 7 x86: C0/C1.
  - 36.28 part 2: ME D1/D2/D4, XP D5, 7 x86 D6.
- **Power and Verifier.**
  - CP1 composite-parent and hub power under S4 on 2000, 2000 SMP, XP, XP x64, Vista x86/x64 and 7 x86/x64,
    and under ME standby and hibernate.
  - The qemu traces read `00000404`/`00000504` on sleep and `00000101` on wake, one line per parent FDO.
  - Driver Verifier V1 on 7 x86 (with `xhcimsi.sys`), 7 x64 and Vista x86/x64; B5 on 7 x64; the 2000 SMP
    Verifier (0x1B) leg throughout. No Verifier stop or bugcheck anywhere, and AllocationsFailed 0.
- **Debug package (D1)** on 98 SE, XP and 7 x64: installs, works, no assert.
- **L5r.** 98 SE 36.46 L5 rerun with every wizard finished first: the re-attach behind the hub binds (M-1's
  reading confirmed), and L6 passes.
- **Audio.** Audio on NT with `-Display none` plays a 20 s tone in 20.2-22.0 s (E-3 7 x64 20.3-20.8 s; P7/T8
  7 x86 20.3-22.0 s).
- **XHCIQUAL 36.48 QEMU matrix (QM): 50 of 50.** The three `-smbios` header and quick cases name the make,
  model, board and BIOS, and none of the 13 identifiers (serial, UUID, SKU, asset, part). `--no-sysinfo` prints
  "System: not read - skipped by --no-sysinfo" and nothing of the machine. Evidence:
  `legs\xhciqual\matrix.txt`, `smbios\`.

**36.41 re-measure (W41)** on this build, method of "36.41's qualifying re-measure", `-display none`, the zip's
`release` x86 driver (verified in each base by size, version and hash).
- The stage A bases predate 36.43's composite parent, so attaching the audio device raised Add New Hardware
  there. New bases with the parent installed from the zip were made: `vm\t36fin\aud\w98-audio-fin-base0/1`.

| Attach after t0 | switch 0 | switch 1 | stage A switch 0 | stage A switch 1 |
|---|---|---|---|---|
| 120 s | 3 of 6 wedged | 2 of 6 | 6 of 9 | 5 of 9 |
| 300 s | 2 of 2 | 2 of 2 | 3 of 3 | 2 of 2 |

- Two 120 s repetitions the judge called MIXED (the Start menu opened at every read, but the tray clock did not
  change) are counted live. Counted as wedges, the 120 s cells read 4 of 6 and 3 of 6.
- The wedge state is 36.41's: VMM code, no bugcheck. The verdict is unchanged: Windows 98 SE's own, and the
  switch makes no visible difference.
- On 98 SE every re-attach of an already-installed usb-audio wedged the guest ("Msgsrv32 [Not responding]",
  5 of 5), while first installs did not (3 of 3). Audio present at boot faulted in `USBAUDIO(01)+2ED4` (36.44
  verdict 4). That is why 98 SE's T8, the audio part of CP1 and the playback of K3/section 9 are NOT MAKEABLE.
- Evidence: `legs\w41\notes.txt`, `reps.txt`, `final-verdicts.txt`, `out\p36-final\audio\`.

**Failures and findings, with attribution** (each was raised to the orchestrator as it came in):
- **UAS stream fault recovery: FAIL, sent to task 36.54** (owner: fix before 2.3.0.0, worker `p36-ste`).
  - What happens: a fault the qemu flavour injects on a UAS data-in stream TD (`XhciQemuInject` bulk codes 14
    timeout, 15 stop, 16 error, 17 short) leaves the device taking no further command until it is removed or the
    controller restarts. Its class timeouts climb, or never fire at all (B9(b) at 2048).
  - Where:
    - XP x64: 16 and 17, 2 of 2, and B9(b) = 15 at 2048 with WideMap 0 and 1;
    - 2000: 16 and 17;
    - XP: 14 and 15;
    - Vista x64: 14-17; Vista x86: 14 and 17;
    - 7 x86: 15;
    - 98 SE: 16, 1 of 2 (the failing boot had a controller disable/enable and the UAS first install before
      the injection).
    - 7 x64 recovered from all four.
  - Not a regression: the pre-stage-2 build `90c625c` (36.22 stage 1) reproduces it.
  - The BOT stick recovers from the same fault through the HCD's soft retry, from which stream pipes are
    excluded by design (record 17).
  - The QEMU trace of a run that did recover shows the reset path working: Stop Endpoint, Set TR Dequeue for
    the stream, port reset (`usb_uas_reset`), re-address, then doorbells with the stream ID.
  - The stuck runs rest on the injection model: a Transaction Error for a TD QEMU never started, while usb-uas
    still holds that tag's data phase. No stuck run was traced: tracing slowed QEMU, and that run recovered.
  - Evidence: `legs\xp64\q3-16*.txt`, `q3-16x-a.txt`, `q16x-b2.txt`, `qemu-stderr-6.log`, `o16a2.txt`,
    `o16b2.txt`, `b9-15*.txt`; `legs\w2k\q3-1[67]-*.txt`; `legs\xp\xq3-1[45]*.txt`; `legs\vx64\q3-14*.txt`, `legs\vx86\q3-*.txt`;
    `legs\w7x86\q3-hang.txt`; `legs\w98\q3e-trace.txt`.
- **98 SE E-4: FAIL, sent to task 36.55** (worker `p36-dl98`). This is the qemu dead-line leg:
  `XhciQemuDeadLine 1` under `XhciInterruptMode 1`.
  - Reproduced 2 of 2: after the desktop the first Bulk-Only copy never progresses. A true stop, not backstop
    pace:
    - `info blockstats` is unchanged 60 s apart, and the disks idle for 16+ min;
    - the trace is silent, with no drain or backstop lines;
    - all EIP samples are in VMM, none in a WDM image.
  - The 824a6c7 reread instead copied at backstop pace (95 min for 4 MB).
  - Evidence: `legs\w98\e4-regs.txt`, `e4r-regs.txt`, `e4r-blockstats.txt`, `e4r1.txt`, `e4r2.txt`,
    `debugcon-29.log`, `debugcon-30.log`.
- **ME P3 (the same leg on ME): PASS, backstop pace by design** (orchestrator ruling). Every transfer completes
  through the backstop drain at about 2.3 transfers/s: Transfers 778 -> 1261 in 3.5 min, BackstopDrains equal,
  no timeouts. The "freeze" is ME's system VM waiting on that slow disk. The line-alive control is clean.
  Evidence: `legs\me\notes.txt` P3-x, `p3x0-2.txt`.
- **ME S5 shutdown hang: not the driver** (Windows ME, pre-existing).
  - The trigger is a USB mass-storage volume mounted but never accessed since boot. One boot-present disk that
    had been accessed also hung once, so access is not a guaranteed workaround.
  - It hangs the same way on the published 2.2.0.0, and on ME's own UHCI + USBSTOR stack with no xhci98 device.
  - The xhci98 trace shows SET_POWER S5, D3 and halt, with every transfer completed. ME's storage stack then
    issues I/O after the D3, and VMM loops at `c00138xx`. The hub, interrupt mode, transfer size and open files
    make no difference.
  - Candidate release-notes limitation. Evidence: `legs\me\notes.txt` S5-a to S5-f, `s5c-prev-regs.txt`,
    `s5e-uhci-regs.txt`, `s5d-q-trace.txt`, `s5e-dir-trace.txt`.
- **A-98 and A-2000 fault legs (qemu): KNOWN, the documented limitation.** Inject 07 is an all-ones USBSTS
  with no proof that the controller stopped, so containment is PINNED and its transfers are kept for DMA safety.
  A controller disable then hangs: 2000 4 of 4, and the client's state makes no difference. On 2000 the HID
  mouse's REMOVE waits on its kept interrupt-IN read, the CPU idles in the HAL, and IdleAbandoned/Quarantined
  read 0. On 98 SE the hang needs the suspended client; with no client, disable and enable complete.
  Release notes, Known limitations: "disabling the controller hangs after the driver closes off a controller it
  could not prove stopped (since 2.2.0.0)". Evidence: `legs\w2k\f-b-*.txt`, `r2*-*`; `legs\w98\qf2.txt`,
  `qf-trace-tail.txt`, `qj1.txt`, `qj2.txt`.
- **MSI-X BackstopDrains (NT6): attributed to QEMU, no driver change** (orchestrator decision; owner confirmed 2026-10-11).
  - Under `msi=off,msix=on` every NT6 snapshot counts BackstopDrains: 7 x86/x64 3-20, Vista x86 127 and 288,
    Vista x64 115. Under MSI, line and polling it reads 0. Function is clean (fc/VERIFY 0 bad). Plays under
    MSI-X took 21.5-22.9 s against 20.3-21.0 s under MSI.
  - The traced MSI-X play (`qmsixaud-times.txt`, 26.059 s, with tracing on) is a separate reading. No claim is
    made that the mechanism explains every throughput difference.
  - It is not new: `533e721` counts it too (`basemsix.txt`, 4). That reading does not show the same frequency.
  - The attribution is source-backed and consistent with the trace (`qtrace-msix.log` near line 2521: IMAN=1,
    unuse, ERDP, IMAN=2, use):
    - qemu-xhci un-uses and re-uses the MSI-X vector on every IMAN.IE toggle;
    - it raises the interrupt before marking the vector used;
    - `msix_notify` returns on an unused vector before any mask or PBA check, so nothing is pended.
    The trace does not record a dropped `msix_notify` directly.
  - The driver's IE=0 write in the ISR is deliberate (record 05, "The DIRQL exception"). EHB covers the window
    on real hardware.
  - BackstopDrains counts drains queued when the same pending position and generation persist across
    observations. It is not a count of lost messages: scheduling delay can also trip it.
  - MSI-X rows are PASS with this note. Evidence: `legs\w7x64\basemsix.txt`, `qtrace-msix.log`, `qmsix.txt`,
    `msix-rate.txt`; `legs\vx86\m2x0.txt`, `m2x3.txt`; `legs\vx64\m2x0.txt`.
- **B8 with XhciWideMap 1 (qemu, XP x64 and 7 x64): NOT MAKEABLE (QEMU).** The QEMU process aborts:
  `hw/usb/dev-uas.c:895 usb_uas_handle_data: assertion failed: (uas->data3[p->stream] == NULL)`.
  - usb-uas holds one data packet per stream, while the driver legally queues two TDs of one URB on a stream
    ring. B9(a) needs the same, so it is NOT MAKEABLE too.
  - BigRefuse with WideMap 0 passes. WideMap is off by default, so the shipped default is unaffected.
  - Evidence: `legs\xp64\qemu-stderr-4.log`, `qemu-stderr-5.log`, `b8a.txt`.
- **2000 SMP warm restart under mode 2 (WR): NOT RUN, OWED.** No quiet window existed: other workers' guests
  held the host from about 10:50. Moved to the post-rebuild re-read on the re-stamped 2.2.0.3, which needs a
  quiet host anyway.
- **Known or settled, recorded only.**
  - 2000 Code 31 on the BOT stick after S4 resume and after mode-2 recovery.
  - 98 SE Stand by: the console fails after resume (36.28 part 2, Windows 98 SE/QEMU).
  - ME-1 reproduced (one try).
  - S3 not offered on 2000 (`acpi=off`) or 2000 SMP (the Cirrus driver vetoes it).
  - QEMU wedged once on a 2000 qemu-flavour resume (process at 0 CPU).
  - C1: XP's and 7's hidusb never idles QEMU's mouse (Windows).
  - A3/SB4/C4 mid-transition windows cannot be hit in QEMU.
  - U2: installing 2.2.0.2 over this build leaves `xhcimsi` running while Windows deletes the MSI properties
    key, so the older driver runs on the line.
  - An update from 2.2.0.0 keeps `XhciIntelPortSwitch` at 1 (NOCLOBBER over 2.2.0.0's value).
  - Harness: the UAS disk number moves across controller restarts (stale VERIFYs discarded); the Win7 hibernate
    resume needs the boot's device set (0xc0000411 otherwise); one misdirected XFERTEST WRITE damaged a VVFAT
    boot stick (replaced).
  - QEMU aborted once on Vista x86 while a UAS device was re-added after Q3 code 15
    (`dev-uas.c:864`, `status3[stream] == NULL`; `legs\vx86\qemu-stderr-16.log`).
  - Vista/7 hibernate resume sometimes rejected the image with 0xc0000411 before any driver loads; a later
    attempt resumed. Vista hibernates again ~2 min after an unattended resume.
  - One unexplained Vista x64 QEMU exit at 08:15 (no stderr, no crash record).
- **Not run.** ME A4 (time went to the S5 attribution). ME Q4 is NOT MAKEABLE (ME never sends more than
  64 KiB). XP x64 B10 is NOT MAKEABLE (no amd64 test client), as is 7 x64 B10 (no storage suspend requester).
  B12 is NOT MAKEABLE on QEMU.

**Owner bench items added by this run:**
- **BackstopDrains under MSI-X** on a real MSI-X machine. 0 is the expected clean result, not a hardware
  invariant. Confirm that MSI-X is actually in use, then read the counter deltas under sustained audio plus
  storage, across idle-to-active transitions, and across lifecycle cycles.
- **`XhciWideMap 1` with a real UAS drive:** a stage-2 TD on a stream ring, with fc-verified copies of 1 MiB and
  more.
- **A UAS drive under a real transaction error** (a marginal cable or hub): does it recover without a replug?
  (after 36.54).

**Roadmap tasks whose VM legs this run completes** (not ticked here; the orchestrator ticks):
- 36.13 (the owed amd64 and NT dead-line legs; 98 SE's E-4 goes to 36.55);
- 36.16 (M1-M4, F3, H2/H3, U1/U2 on the stamped build);
- 36.17-36.19; 36.22, 36.24 and 36.26 (sections 5 and 5b; B8/B9(a)/B12 NOT MAKEABLE);
- 36.35 (stage A's rest on ME, 2000 SMP + Verifier and 98 SE; bench clause 36.36 stays the owner's);
- 36.39 (stage B SB1-SB7);
- 36.41 (re-measured on the stamped build);
- 36.42, 36.43 (K2, K3);
- 36.45; 36.46 (L5/L6 rerun);
- 36.47 (K6 incl. XP x64);
- 36.48 (QEMU matrix 50/50);
- 36.49 (I18 regression);
- the comppwr / D4 checks (CP1, V1);
- 36.final's install legs from the zip.
Not complete:
- 36.28 part 2: ME and NT read; 98 SE standby settled as Windows 98 SE's.
- The 2000 SMP mode-2 warm-restart timing: owed.
- Q3 on UAS streams: 36.54.
- 98 SE E-4: 36.55.

**Owed to the post-rebuild re-read** on the re-stamped 2.2.0.3. Since this run, `2.3.0.0` has merged 36.53
(`XhciInterruptMode` 1 = line only, 3 = message only), 36.54 (UAS stream fault recovery) and the INF changes
of the install/update legs.
- The 36.54 rows: Q3 bulk codes 14-17 on UAS streams on XP x64, 2000, XP, Vista x64, 7 x86 and 98 SE, and B9(b)
  at 2048 on XP x64.
- 98 SE E-4 (36.55).
- The 2000 SMP warm restart under mode 2 (WR), on a quiet host.
- The rows that depend on what changed:
  - P2 modes 0/1 and M1/F (mode 1 = line);
  - M2 mode 3 under MSI and MSI-X;
  - the qemu dead-line legs E-1/E-2/P3/P4 (mode 1);
  - I2 (the INF values);
  - K3, the update from 2.2.0.0 (values preserved).
- A smoke on each guest of install, CP1 S4 and shutdown with a USB disk and the stage A client on 98 SE and
  ME. These are the power and shutdown paths read above.

The full table follows. Results are the leg agents' verdicts. Where the final attribution differs, it is
appended in bold.

| Guest | Check | Package | Result | Observed (abridged; full line in `legs\<guest>\notes.txt`) |
|---|---|---|---|---|
| 98 SE | I1 | rel | PASS (TMP noted) | Update Driver from D:\XHCI98.INF (zip release-x86 427b9d98...): only the documented usbd.sys/usbui.dll prompts, no resta... |
| 98 SE | I2 | rel | PASS | reg-inst.txt USB\0002: XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, XhciPol... |
| 98 SE | I3 | rel | PASS | i3.txt: 'verbosity tier read 3, APPLIED 3' (3 covers level 2); intr.mode 0, intr.delivery 1, intr.reason 1, intr.pin 1; ... |
| 98 SE | I4 | rel | PASS | xhci98.sys 427b9d98... (zip release-x86) + XHCISNAP 79fe8faa... (zip xhcisnap) - same zip; tool 'xhcisnap 2.2.0.3, built... |
| 98 SE | M5 (down-level 98 SE) | rel | PASS | i3.txt: intr.mode 0 delivery 1 reason 1 (63/64 0 omitted, 65 1, 66 1), msi.* notes all 0, no MSI counter 96-108 nonzero,... |
| 98 SE | K6 | rel | PASS | k6x.txt: 'Windows 98 SE: platform 1, version 4.10, build 2222, A', 'x86 (32-bit Windows)', make/model 'not available on ... |
| 98 SE | SB1 | rel | PASS | i3.txt (-verbosity 3): susp.switch.value FFFFFFFF (absent), susp.switch 0; 'DNCTRL not read (selective suspend off, or n... |
| 98 SE | T1 | rel | PASS (98 SE cap 60 KiB as expected) | C:\BIG.BAT: H:\BIG.BIN -> C: fc clean, C: -> H:\B2.BIN fc clean (t1d.png); C:\BIG.BIN -> E:\B3.BIN (BOT) fc clean (t1e.p... |
| 98 SE | G2 | rel | PASS | g1.txt usbif hubs: port 1 stick 46f4:0001 _EX speed 3, V2 proto 4 flags 3; port 3 UAS 46f4:0003 _EX speed 3; port 6 HS m... |
| 98 SE | P2 mode 0 | rel | PASS (mode 1 below) | i3.txt/t1.txt: intr.mode 0, IntrDelivery 1, IntrReason 1 (63/64 0), PollRole 1, PollPeriodLast 100; mouse moves (m9.png)... |
| 98 SE | B6 (256 MB part) | rel | PASS (256 MB; 98 clause + 64 MB below) | t1.txt stage 2: 'XhciWideMap not set (the default)', 'promotion off; the register-free check not run (XhciWideMap off)',... |
| 98 SE | K2 | rel | PASS (playback: T8) | a1 port 3 (12 Mb/s): parent 'xHCI98 USB Composite Device' installed silently (XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:... |
| 98 SE | G1 (part) | rel | PASS (hub part below) | g1.txt: 3 interfaces (stick 46F4:0001, mouse 0627:0001, UAS 46F4:0003); k2if.txt with usb-audio: XHCI98\COMPOSITE&VID_46... |
| 98 SE | I18 (switch default) | rel | PASS (fallback 0 leg after the restart batch) | i18a.txt: slot 3 hub port 8 FS, slot 4 port 8 route 00001 tier 1 FS configured; hub.speed.value=FFFFFFFF, hub.speed.fall... |
| 98 SE | G1 (hub) | rel | PASS | g1hub.txt: 4 interfaces: stick, root mouse, XHCI98\HUB&VID_0409&PID_55AA\314159-0000:00:03.0-4 (the external hub), USB\V... |
| 98 SE | K4 (36.45 L1-L5) | rel | PASS | UV32: stick 'QEMU'/'QEMU USB HARDDRIVE'/'1-0000:00:03.0-1', Device Bus Speed Super (uv2/uv4.png); HS mouse 'QEMU'/'QEMU ... |
| 98 SE | K5 | rel | PASS (kbd works on this build, unlike wave 3's no-second-wizard hold) | HS mouse port 2 + FS mouse behind hub move the pointer (m9/h5.png); tablet 4.2 bound (HIDDEV), its reports move the poin... |
| 98 SE | S1 | rel | PASS | s1.txt after a 2-minute untouched idle (HS mouse, FS mouse behind the hub, stick attached): susp.switch.value FFFFFFFF, ... |
| 98 SE | P1 mode 2 | rel | PASS | r1.txt (after BIG.BIN UAS copy+fc (r1b.png), E: F.BAT fc x2 (r1c.png), both mice moving (r1m1/r1m2.png), then 60 s idle)... |
| 98 SE | T2 64 | rel | PASS | r1.txt: xfer.max.value 0x40, xfer.max.why 1, xfer.max.bytes 0x10000; 'command size in effect 65536 bytes; asked 1 times'... |
| 98 SE | B6 (98 clause) | rel | PASS (64 MB part on -Tag m) | with XhciWideMap 1 set for this boot: r1.txt 'XhciWideMap 1', xfer.wide.value 1 / switch 1, xfer.wide.check 7, enabled 0... |
| 98 SE | K1 portsw.vbs | rel | PASS (final 0 read below) | 'start D:\portsw.vbs': '0 - off' (k1a.png) -> 1 'now 1 on every xhci98 controller ... RESTART WINDOWS' (k1b.png); after ... |
| 98 SE | G3 FastPoll 1 | rel | PASS (2/3/4 below) | r1.txt: fastpoll.value 1, fastpoll.mode 1, no other fastpoll.* note, no FastPoll counter; FS mouse behind the hub moves ... |
| 98 SE | T8 | rel | NOT MAKEABLE (Windows 98 SE 36.41 audio wedge; attribution Windows, settled) | not reached: the usb-audio hot-add itself wedged 98 SE's UI (Msgsrv32 not responding, VMM EIPs C000A881/C0003621/C005340... |
| 98 SE | K1 (final) | rel | PASS | launch 4 (after the restart): 'start D:\portsw.vbs' reads '0 - off' (k1e.png), Cancel = no change |
| 98 SE | P2 mode 1 | rel | PASS | r2.txt: IntrModeRead 1, IntrMode 1, IntrDelivery 1, IntrReason 5, intr.pin 1, no PollRole/poll activity (promotions 0, p... |
| 98 SE | T2 2048 | rel | PASS (98 cap as expected) | r2.txt: xfer.max.value 0x800, why 1, bytes 0x200000; 'command size in effect 2097152 bytes'; UAS send 65 command size 20... |
| 98 SE | I18 (fallback 0) | rel | PASS | r2.txt: hub.speed.value 0, hub.speed.fallback 0, Usb2HubSpeedSwitchRead 1, 'XhciUsb2HubSpeedFallback read; the default I... |
| 98 SE | G3 FastPoll 2 | rel | PASS | r2.txt: fastpoll.value 2, fastpoll.mode 2, no device-level fastpoll note or counter for the (unpatched) QEMU mice; both ... |
| 98 SE | T2 4096 | rel | PASS | r3.txt: xfer.max.value 0x1000, xfer.max.why 4, xfer.max.bytes 0x100000; 'command size in effect 1048576 bytes'; UAS send... |
| 98 SE | G3 FastPoll 3 | rel | PASS | r3.txt: fastpoll.value 3, fastpoll.mode 3, no device-level fastpoll note/counter; both mice move; mode 0 back: IntrDeliv... |
| 98 SE | T2 0 | rel | PASS | r4.txt: xfer.max.value 0, xfer.max.why 4, xfer.max.bytes 0x100000; command size in effect 1048576; UAS send 61 command s... |
| 98 SE | G3 (FastPoll 0-3 + mode 4) | rel | PASS | i3/r1/r2/r3/r4.txt: fastpoll.value/mode 0/0, 1/1, 2/2, 3/3, 4/4; no fastpoll.open / fastpoll.fallback note and FastPollO... |
| 98 SE | CP1 Stand by | rel | PASS (hub/mouse) / NOT MAKEABLE (composite: Windows 98 SE 36.41 + usbaudio.sys) | hub + mouse part: standby entered, resume reached the desktop, the hub's mouse works after the wake (cp1b-cp1e.png); con... |
| 98 SE | A-98 S1/S2 | rel | PASS | S1.txt 'dev D0 sys S0 started 1 ... config 1 attr 0xA0 in 0x81 status 0'; s1snap.txt susp.switch.value 1, susp.switch 1,... |
| 98 SE | A-98 shutdown with the client in D0 (READS ON) | rel | PASS (4.5 s) | client D0, reads on 1 ok 8 (S2b.txt), DOS box open; ret on 'Shut down' (a98shut-dlg.png) -> QEMU 'paused (shutdown)' aft... |
| 98 SE | A-98 S3 (WW + D2) | rel | PASS | S3a.txt wake req 1 held 1; S3b.txt dev D2, power req 1 cb 1 D2; s3snap1/s3snap2 (~45 s apart): susp.seq=00102110, PORTSC... |
| 98 SE | A-98 S4 reread (remote wake; the 9x WAIT_WAKE read) | rel | PASS | S4a.txt after 2 moves: 'wake req 1 cb 1 last 0x00000000 / irp done 1 last 0x00000000 held 0' (WakeRequests 1, WakeIrpCom... |
| 98 SE | A-98 S5 reread (host resume) | rel | PASS | S5a dev D2 req 3 cb 3; s5snap1 susp.seq 00102110 (third), PORTSC 6 0E63 U3, SuspSuspended 2, no new SuspArms (unarmed); ... |
| 98 SE | A-98 A3 unplug at each state | rel | PASS (c/d: mid-transition not reachable by timing, recorded) | (a) ACTIVE + READS ON: device_del pm1 -> PMCTL 'cannot open \\.\XHCIPM0, error 2' (client gone), port 6 PORTSC 02A0 empt... |
| 98 SE | A-98 A4 controller disable/enable while SUSPENDED | rel | PASS | pm5 WW + D2 (A4d2.txt dev D2, wake held 1); DM controller 'Disable in this hardware profile' -> controller Code 22, clie... |
| 98 SE | A-98 A4 reboot with the device suspended | rel | PASS | WW + D2 (A4r-d2.txt dev D2; a4r-snap.txt susp.seq 00502110, PORTSC 6 0E63 U3) -> Shut down -> 'paused (shutdown)' after ... |
| 98 SE | A-98 A5 behind the hub | rel | PASS | pm1 removed; h1 usb-hub hot-added port 4 (this overlay had no hub driver: 'xHCI98 USB Hub' wizard from D:\XHCI98.INF fin... |
| 98 SE | D-part2 98 SE Stand by (mouse + client only, no disk) | rel | RECORDED (entry no hang; resume to the desktop; post-wake console = Windows 98 SE/QEMU, settled) | h1/pm2 removed, pm3 client on root port 2 in D0 with READS ON (ok 6, D2s1.txt), DOS box open, nothing else on the bus; S... |
| 98 SE | A-98 A6 XhciInterruptMode 2 | rel | PASS | SETV 0002 XhciInterruptMode 2 + restart (launch 17): a6-s3snap1: intr.mode 2, delivery 2, reason 8, PollRole 2; S1 confi... |
| 98 SE | L5r (36.46 L5, every wizard finished before the next removal) | rel | PASS (M-1 resolved: with the wizard finished first, the re-attach behind the hub binds) | h1 usb-hub port 4: 'xHCI98 USB Hub' wizard from D:\ finished (l-h1..l-h4.png); m1 usb-mouse at 4.1: HID wizard (HIDDEV, ... |
| 98 SE | L6 | rel | PASS | l5r.txt (verbosity 3 APPLIED): no cfg.* note, SelectEndpointsRequested 2 = EndpointsOpened 2, SelectEndpointsRefused abs... |
| 98 SE | T6 (98 SE at -Mem 64) | rel | PASS | 1024: UAS H: -> C: -> H:\M1.BIN fc clean (m-t1.png), BOT C: -> E:\M2.BIN fc clean (m-t3.png), m1024.txt command size 104... |
| 98 SE | B6 (64 MB part) | rel | PASS | m1024.txt / m2048.txt vs t1.txt (256 MB): identical stage-2/3 block - XhciWideMap not set, 'promotion off; the register-... |
| 98 SE | K3 update from 2.2.0.0 with usb-audio | prev->rel | PASS (re-detection/values) ; audio function use NOT MAKEABLE (36.41 wedge) | prev install + audio (function directly under the bus, k3-enum-prev.txt); update from the zip package (F:\XHCI98.INF) no... |
| 98 SE | D1 debug package | dbg | PASS | -Tag d (fresh over gold, package-dbg.txt xhci98.sys b108c28f... 273,512 B = zip debug-x86): Update Driver from D:\ with ... |
| 98 SE | Q3 code 16 (bulk Error on the UAS data-in pipe during a copy) | qemu | FAIL SUSPECT-DRIVER (attribution open: the HCD's UAS stream recovery after RESET_PORT on 98 SE, or the qemu layer still withholding the endp...; **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | after the injection xhciuas reset the port (RESET_PORT done, endpoint mask 1A4) and no UAS transfer completed again: can... |
| 98 SE | E-4 (mode 1 + XhciQemuDeadLine 1) | qemu | FAIL SUSPECT-DRIVER (BOT stick I/O stops under the mode-1 dead line on 98 SE, then a ring-0 VMM loop - same shape as ME's 2/2; earlier p36-p...; **final: FAIL -> 36.55 (98 SE dead-line stop)** | desktop reached 13:02, BC clean (e4-d1.png); 'F E' (BOT stick, 4 MB copy + fc x2) listed E: and then never progressed (e... |
| 98 SE | E-4 follow-up (slow progress or true stop?) | qemu | FAIL SUSPECT-DRIVER (true stop under the mode-1 dead line on 98 SE; the 824a6c7 reread's 95-min backstop pace is not what happens here); **final: FAIL -> 36.55 (98 SE dead-line stop)** | TRUE STOP, not backstop pace: QEMU 'info blockstats' for the stick drive (bstk) 60 s apart at ~13:35: Read 142,848 B / 4... |
| 98 SE | Q3 code 15 (bulk Stop on the UAS data-in pipe) | qemu | PASS | launch 32 (mode 0): 02150083 armed before the copy (qemu.inj.bulk.armed 00150207 = slot 2 DCI 7), fired on the copy's TD... |
| 98 SE | Q3 code 17 (bulk Short on the UAS data-in pipe) | qemu | PASS | launch 33 (fresh boot, first uncached BIG.BIN read): 07170083 -> qemu.inj.bulk.armed 00170207, qemu.inj.bulk=17020207 (s... |
| 98 SE | Q3 code 14 (bulk Timeout = lost completion on the UAS data-in pipe) | qemu | PASS | launch 34: 08140083 -> qemu.inj.bulk.armed 00140207, qemu.inj.bulk.lost=0FE10870 (the TD's first TRB), qemu.inj.bulk.rel... |
| 98 SE | Q3 code 16 REPRODUCTION (fresh boot, no DM disable/enable in that boot) | qemu | PASS - the 12:42 failure did NOT reproduce (1 fail of 2). Difference noted: the failing run (launch 27) fired in the boot where the controll... | launch 35: 09160083 -> qemu.inj.bulk=16020207, qemu.inj.ended 16, qemu.inj.done 16; trace: cancelled 1, 'reset port 0003... |
| 98 SE | Q4 XhciQemuMapGrant constrained grant during a UAS read | qemu | PASS | launch 36 (XhciQemuMapGrant 4 + XhciInterruptMode 2): qemu.mapgrant 4, mapgrant.applied 4, 'map grant 4 registers; a chu... |
| 98 SE | Q2 / P8 forced recovery under mode 2 (XhciQemuInject 01050000 form) | qemu | PASS (81 = 1, 82 = 1 -> 2) | launch 36 (mode 2), mouse m1 + BOT stick + UAS attached; q2a.txt before: PollKicks 1, PollParks 0, recovery window 0 of ... |
| 98 SE | CP1 trace (Stand by) | qemu | PASS (4 on sleep / 1 on wake, one parent = the hub; audio parent not makeable) | Dx: 00000404 on sleep, 00000101 on wake; resume finishes; devices work after / launch 37 (qemu, defaults, NO USB disk, n... |
| 98 SE | A-98 qemu fault-injection leg (client SUSPENDED, inject 07 pinned branch, then DM disable + enable) | qemu | FAIL SUSPECT-DRIVER (the configuration manager hangs after QUERY_STOP on a pinned/contained controller with a suspended client; no-injection...; **final: KNOWN - documented limitation (disable after a pinned containment hangs; transfers kept for DMA safety)** | pinned branch reached with the client suspended (qf2.txt, IdleAbandoned/IdleQuarantined 0); DM controller 'Disable in th... |
| 98 SE | A-98 qemu leg CONTROL (same injection, NO client device) | qemu | PASS - the 15:20 hang needs the suspended client (pinned controller + SUSPENDED XHCIPM device + disable), attribution still open (driver's Q... | launch 40 (switch 1, no client attached): 0C070000 -> CONTAINED/PINNED as before (qj1.txt); DM disable: trace teardown, ... |
| ME | I1 | rel | PASS | Found New Hardware -> D:\XHCI98.INF; restart asked (wizard + boot pass): XHCI98.TMP + WININIT [rename] until the restart... |
| ME | I2 | rel | PASS (WideMap: absent, not 0 - recorded for the coordinator) | reg-inst.txt USB\0000: LogVerbosity 0, LogDebugView 0, ForceBulkOnly 0, FastPollFsLs 0, FirstEnumWaitMs 0x1388, FirstEnu... |
| ME | I3 | rel | PASS | i3.txt: tool xhcisnap 2.2.0.3, driver file 2.2.0.3; verbosity read 3 APPLIED 3; intr.mode 0 delivery 1 reason 1 pin 1; p... |
| ME | I4 | rel | PASS (poll block) | xhci98.sys 427b9d98... (zip release-x86) + XHCISNAP 79fe8faa... (zip xhcisnap), both 2.2.0.3 (snapshot header 'tool xhci... |
| ME | SB1 | rel | PASS | i3.txt (-verbosity 3): susp.switch.value FFFFFFFF, susp.switch 0; 'DNCTRL not read (selective suspend off, or no MMIO)',... |
| ME | K6 | rel | PASS | k6x.txt: 'Windows ME: platform 1, version 4.90, build 3000', 'x86 (32-bit Windows)', make/model 'not available on this O... |
| ME | T1 | rel | PASS (64 KiB ceiling on ME recorded, USBNTMAP) | C:\T1 batch: G:\BIG.BIN (UAS) -> C: fc clean; C: -> F:\B2.BIN (BOT) fc clean; C: -> G:\B3.BIN (UAS write) fc clean (me-t... |
| ME | B6 | rel | PASS (the 9x reason is read with WideMap 1 in the T2 reboot cycle below) | fc clean (T1); b6.txt: 'promotion off; the register-free check not run (XhciWideMap off)' - the reason printed is WideMa... |
| ME | K2 | rel | PASS | a1 port 3 (12 Mb/s): ME wizard 'Audio Device' -> 'USB Audio Device' automatic (WDMA_USB); DM by connection: 'xHCI98 USB ... |
| ME | G2 | rel | PASS | usbif hubs (k2if.txt): port 1 vid 46f4 pid 0001 '_EX speed 3' (V2 proto 4 flags 3), mouse port 6 speed 2, audio port 7 s... |
| ME | G1 | rel | PASS | k2if.txt (audio attached): stick, mouse, XHCI98\COMPOSITE&VID_46F4&PID_0002 parent; g1if.txt (hub set): stick, mouse, XH... |
| ME | K4 36.45 L1-L5 | rel | PASS | UV32: stick QEMU / QEMU USB HARDDRIVE / 1-0000:00:03.0-1 (me-uv-p1.png), UAS QEMU / USB Attached SCSI HBA / 27842-...-3 ... |
| ME | T8 | rel | PASS (function) - the slow time base recorded as harness/QEMU | a2 usb-audio port 3 + hub h2 port 4 (stick s3 4.2, tablet t2 4.1) bound silently; C:\T8: C:\BIG.BIN -> F:\B6.BIN (root S... |
| ME | K1 portsw.vbs (part 1) | rel | (continued) | 'XhciIntelPortSwitch ... USB\0000 0 - off' (me-k1a.png); 'portsw.vbs 1' -> '1 - on, on the listed Intel chipsets ... now... |
| ME | T2 64 | rel | PASS | p1a.txt: xfer.max.value 0x40 why 1 bytes 0x10000; 'XhciMaxTransferKB used as set (64)', 'command size in effect 65536 by... |
| ME | P1 mode 2 | rel | PASS | p1a.txt intr.mode.read 2, intr.mode 2, delivery 2, reason 8; IntrDelivery 2 IntrReason 8 PollRole 2 PollPeriodLast 8; US... |
| ME | K1 portsw.vbs | rel | (continued) | after Restart A: 'XhciIntelPortSwitch ... 1 - on, on the listed Intel chipsets' (me-k1c.png); psw.gate 0 (QEMU not Intel... |
| ME | S5 shutdown (every boot) | rel | FAIL SUSPECT-DRIVER; **final: not the driver - Windows ME (a never-accessed USB disk at shutdown; same on 2.2.0.0 and on ME's own UHCI)** | ME shutdown hangs: black screen, vCPU 100 % in ring-0 VMM loop EIP c00138c1/c00138c2/c00511e1/c000b61c, HLT 0, never pow... |
| ME | T2 2048 | rel | PASS | p2b.txt xfer.max.value 0x800 bytes 0x200000, 'used as set (2048)', 'command size in effect 2097152', UAS 'command size 2... |
| ME | P2 mode 1 | rel | PASS | p2b.txt intr.mode.read 1, intr.mode 1, delivery 1, reason 5; IntrDelivery 1 IntrReason 5, no role/watcher counters, no c... |
| ME | P2 mode 0 | rel | PASS | i3.txt (default boot): intr.mode.read 0, mode 0, delivery 1, reason 1, PollRole 1 |
| ME | K1 portsw.vbs | rel | PASS | me-k1a (0 - off), me-k1b (set 1), me-k1c (1 after restart), me-k1d (set 0), me-k1e '0 - off' after the next restart; scr... |
| ME | B6 (9x reason) | rel | PASS | p2b.txt: 'XhciWideMap 1', xfer.wide.check 7, xfer.wide.enabled 0, 'promotion off; the register-free check not run (Windo... |
| ME | M5 | rel | PASS | i3.txt: delivery line (INTx), 0 0 1 1, no counter 96-108 nonzero, MsiFilterMode (109) 4294967295 = FFFFFFFF, 'message in... |
| ME | S1 | rel | PASS | i3/k2a/k4/p1b/p2b.txt: susp.switch.value FFFFFFFF, susp.switch 0, no Susp*/Idle* counter nonzero; idle stretches > 2 min... |
| ME | S5-x0 | rel | PASS (no hang without the hub set) | powered off: 2 s shot shows the shutdown screen (s5x0-2s.png), paused (shutdown) at 7 s |
| ME | S5-a | rel | HANG - mode/size values not involved | hangs: not off after 150 s, black, 12 samples EIP c00138xx/c005xxxx/c000bxxx ring 0 HLT 0 (s5a-regs.txt, s5a-pre.png, s5... |
| ME | S5-b hub set | rel | HANG - the hub set alone is enough (S5-x0: stick+mouse+UAS without the hub powered off) | hangs: black at 2 s, not off after 150 s, ring-0 loop (s5b-hub-regs.txt, s5b-hub-pre.png, s5b-hub-2s.png) |
| ME | S5-b hub only | rel | NO HANG - a device behind the hub is needed | powered off in 7 s (s5b-hubonly-2s.png) |
| ME | S5-b hub+tablet | rel | NO HANG | powered off in 7 s (s5b-hubtab-2s.png) |
| ME | S5-b hub+stick (also S5-e) | rel | HANG - trigger = a Bulk-Only stick behind the hub; no I/O or open file needed. Root SuperSpeed stick + UAS + mouse (S5-x0), hub alone, hub+t... | hangs: black, not off after 150 s, 12 samples ring-0 VMM loop (s5b-hubstk-regs.txt, s5b-hubstk-2s.png) |
| ME | S5-d qemu | qemu | (trace) | HANG reproduced (s5d-q-regs.txt 12 samples ring-0 VMM loop c00138xx/c005xxxx). Trace s5d-q-trace.txt (debugcon-12.log): ... |
| ME | S5-d qemu control | qemu | (bisect) | HANG as well (s5d-qctl-regs.txt; s5d-qctl-trace.txt ends identically: SET_POWER S5, D3, halted, save declined, transfers... |
| ME | S5-e | qemu | NO HANG when the disk was accessed | powered off in 7 s (s5e-dir-2s.png). Trace s5e-dir-trace.txt: transfers 0x31 -> 0xB8 -> 0xBA all BEFORE the S5 power IRP... |
| ME | S5-c prev | prev | PRE-EXISTING (2.2.0.0 hangs the same way; not new in 2.2.0.3) | HANG, same ring-0 loop (s5c-prev-regs.txt, s5c-prev-2s.png); the next boot with the stick accessed (F.BAT fc clean, p-fc... |
| ME | S5-e ME own stack | rel | HANG on Windows ME's own USB stack - not xhci98 | HANG: not off after 150 s, the SAME ring-0 VMM loop (EIP c00138c1/c0013740/c000b5xx-b6xx/c0052563; s5e-uhci-regs.txt, s5... |
| ME | S5 attribution | rel/qemu/prev | NOT DRIVER (Windows ME) | Trigger: a USB mass-storage volume mounted but never accessed since boot, at Start > Shut Down. Not needed: the hub, int... |
| ME | T8 (re-read, supersedes the 08:05 line read under host throttling) | rel | PASS | launch 13 (unthrottled, 10:40): set s1, m1, a1 usb-audio port 3, hub h1 + stick 4.2 + tablet 4.1; C:\T8 C:\BIG.BIN -> F:... |
| ME | CP1 Stand by | rel | PASS | launch 19 set (s1, m1, a1 port 3, hub h1 + stick 4.2 + tablet 4.1, all disks accessed by T8): Stand by (cp1s-dlg.png) ->... |
| ME | CP1 Hibernate | rel | PASS | Hibernate (cp1h-dlg.png) -> image written, 'paused (shutdown)' after 32 s (cp1h-off.png); relaunch 20 on the same overla... |
| ME | K5 | rel | PASS | mouse port 2 moves the pointer (me-hid.png), tablet behind the hub moves it (me-tab.png, cp1h-tab.png), sticks fc clean;... |
| ME | A-ME A0 | rel | PASS | BC clean, F: F.BAT fc clean (s-a0fc.png), USB mouse moves (s-a0m.png); a0.txt susp.switch.value FFFFFFFF, susp.switch 0,... |
| ME | A-ME A1 | rel | PASS | s1.txt 'dev D0 sys S0 started 1 ... config 1 attr 0xA0 in 0x81 status 0'; s1snap.txt susp.switch.value 1, susp.switch 1,... |
| ME | A-ME A2 S1-S6 | rel | PASS | S2 READS ON + tablet reports: ok 0 -> 8 (s2b). S3 WW (wake req 1 held 1) + D2 (dev D2, power req 1 cb 1): susp.seq 00102... |
| ME | A-ME A3 | rel | PASS ((d) could not land before the D0 under QEMU's instant wake - recorded) | (a) del during reads: PMCTL 'cannot open \\.\XHCIPM0' (device object gone), re-plug binds silently, READS ON + reports o... |
| ME | A-ME client STOP/START x2 | rel | PASS | cycle 1: Disable -> Code 22 (ss-dis1.png), Enable -> 'working properly' (ss-en1.png); the pending read was cancelled at ... |
| ME | A-ME shutdown with the client in D0 (READS ON) | rel | PASS (7 s) | switch 1, client D0 with READS ON (reads 15), stick s1 attached (accessed): Start > Shut Down -> shutdown screen at 2 s ... |
| ME | D-part2 D1 | rel | PASS | before: d1d2 dev D2 held WW, d1snap susp.seq 00102110 SuspSuspended 1, stick F: fc clean; Stand by -> 'paused (suspended... |
| ME | D-part2 D2 | rel | PASS | WW + D2 (d2d2: dev D2, held WW; d2snap 00402110 SuspSuspended 4), stick accessed; Hibernate -> 'paused (shutdown)' in 16... |
| ME | D-part2 D3 | rel | NOT HIT (window not provably hit in 2 tries; no hang) | try 1 void (COM1 'Write fault' after the hibernate resume ate the keys - harness); try 2 (delay 6 s) and try 3 (delay 4 ... |
| ME | D-part2 D4 | rel | PASS | SETV InterruptMode 2 + restart: d4a.txt intr.mode 2, delivery 2, reason 8; Stand by -> suspended 4 s, wake: F: F.BAT fc ... |
| ME | ME-1 (one try) | rel | REPRODUCED - known open defect ME-1 (Windows ME storage path above the bus per 36.28 part 1), recorded only | C:\LOOP copying D:\RAND.BIN -> F:\R3.BIN (me1-loop.png), Stand by -> suspended in 4 s; system_wakeup + key: the screen s... |
| ME | CP1 trace | qemu | PASS | D3 shape) per parent on sleep, 00000101 on wake (00000100 on ME = by design) / usb-audio a1 port 3 (composite parent) + ... |
| ME | Q1 no-interrupt start, mode 0 | qemu | PASS | q1m0.txt intr.mode.read 0, mode 0, delivery 2, reason 4, pin 0, qemu.inj.nointerrupt 1; IntrDelivery 2 IntrReason 4 Poll... |
| ME | Q1 no-interrupt start, mode 1 | qemu | PASS (fails as designed; reason-7 channel NOT MAKEABLE on 9x) | DM controller 'not working properly (Code 10)' (q1b-dm.png); XHCISNAP 'cannot open \\.\HCD0, error 2' (no started FDO, q... |
| ME | P3 dead line under mode 1 | qemu | FAIL SUSPECT-DRIVER; **final: PASS - backstop pace (~2.3 transfers/s) by design under mode 1 + dead line** | run 1 (-BootStick, mouse hot-added): desktop froze within ~1 min of the mouse add, vCPU in the ring-0 VMM loop (p3-hang-... |
| ME | P3-d control | qemu | PASS (control) | the same leg works with the line alive |
| ME | S5-f (follow-up) | qemu | HANG recorded | HANG: not off after 100 s, black, VMM loop (s5f-regs.txt, q2-sd.png) - so 'accessed' does not always prevent it (this st... |
| ME | P3-x attribution | qemu | RE-ATTRIBUTED: no driver hang; backstop pace (coordinator to rule PASS/FAIL against 'devices work') | (a) the backstop runs: p3x0 BackstopDrains 1 at idle; after the stick's enumeration p3x1 Transfers 49/49, BackstopDrains... |
| ME | P4 dead line under mode 0 | qemu | PASS | -Tag q2, XhciInterruptMode 0 + XhciQemuDeadLine 1: p4.txt intr.moved 3, qemu.inj.deadline.isr 1, intr.retired 1, intr.mo... |
| ME | Q2 forced recovery | qemu | PASS | the value present at the start (01050000) is latched unfired by design (q2rec.txt qemu.inj.latched 01050000, window 0 of... |
| ME | T2 4096 | rel | PASS | t2c4096.txt xfer.max.value 0x1000, why 4, bytes 0x100000; 'XhciMaxTransferKB out of 64-2048: the default (4096)', comman... |
| ME | T2 0 (and T2 summary) | rel | PASS | 0: t2d0.txt xfer.max.value 0, why 4, 'out of 64-2048: the default (0)', command size 1048576, UAS fc x2 clean (t2d-fc.pn... |
| ME | Q3 bulk faults 14-17 hex under a UAS copy | qemu | PARTIAL: 0x14 PASS, 0x15 fired/no completion note, 0x16/0x17 refused by the injector (harness sequencing) - no driver failure seen | UAS u1 (-Big, F:) on -Tag q2, C:\Q3 = copy F:\BIG.BIN C:\Q.BIN + fc /b, the fault fired at run time before each copy: q3... |
| ME | Q4 XhciQemuMapGrant during a 1 MiB / 2 MiB UAS read | qemu | NOT MAKEABLE (9x request size) | ME's USBNTMAP never sends more than 64 KiB (bulk URBs longest 61440 at every XhciMaxTransferKB, b6/p2b/t2c4096), so a 1-... |
| ME | A-ME A4 (if time) | rel | NOT RUN (time) | not run: the S5 and P3 attributions requested by the coordinator took the time |
| ME | watch XHCI98.TMP / WININIT | rel | PASS (ledger behaviour) | every ME install this run (-Tag r, q, p, s, q2) asked for the restart; -Tag r showed XHCI98.TMP + WININIT.INI [rename] b... |
| 98 SE NUSB clean | 9 install (I1/I2) | rel | PASS | Add New Hardware -> D:\XHCI98.INF (+ the 98 SE CD for USBD.SYS); WININIT rename on the restart (ledger); reg-inst.txt US... |
| 98 SE NUSB clean | 9 bc | rel | PASS | BC 'FC: no differences encountered' after the restart (w98n-e6.png) |
| 98 SE NUSB clean | 9 storage fc + unplug, SuperSpeed BOT fc | rel | PASS | stick s1 port 1 (5000): Add New Hardware 'USB Mass Storage Device' -> C:\WINDOWS\INF\USBSTOR.INF (NUSB) -> Finish; F: F.... |
| 98 SE NUSB clean | 9 HID | rel | PASS | m1 port 2 (480): Add New Hardware 'USB Human Interface Device' -> HIDDEV.INF (+ CD for files) -> Finish; QEMU 'HID Mouse... |
| 98 SE NUSB clean | 9 UAS SuperSpeed fc | rel | PASS | u1 port 3 (5000): wizard 'USB Attached SCSI HBA' -> D:\XHCIUAS.INF 'xHCI98 USB Attached SCSI Storage' -> Finish, 'USB Di... |
| 98 SE NUSB clean | 9 audio bound (composite parent) | rel | PASS (play re-read after the relaunch below) | a1 port 3 (12): wizard 'USB Audio Device' -> C:\WINDOWS\INF\WDMA_USB.INF -> Finish; DM by connection 'xHCI98 USB Composi... |
| 98 SE NUSB clean | 9 controller disable/enable with a mouse | rel | PASS | DM controller Disable -> red X on controller and children (n-c6dis.png); enable -> 'working properly' (n-c6p2.png; the t... |
| 98 SE NUSB clean | 9 hub disable/enable with devices behind | rel | PASS | hub h1 port 4 bound silently ('xHCI98 USB Hub'); tablet t1 4.1 (HID wizard + CD) and stick s2 4.2 (USBSTOR wizard) behin... |
| 98 SE NUSB clean | 9 UAS High Speed fc | rel | PASS | u1 port 3 enumerated at 480 Mb/s (info usb), bound silently (driver already installed), F: F.BAT copy + fc x2 'no differ... |
| 98 SE NUSB clean | 9 audio play (re-read after relaunch) | rel | NOT MAKEABLE (98 SE usbaudio vehicle fault, Windows/QEMU - not the driver; binding PASS above) | a1 on port 4 (new devnode -> wizard; this time the 98 SE CD was asked for ksclockf.ax - the first install at 07:16 never... |
| 98 SE NUSB clean | 9 shutdown | rel | PASS | stick s1 + mouse m1 attached: 'Windows is shutting down' at 2 s (sd-2s.png), QEMU 'paused (shutdown)' after 7 s; quit |
| 98 SE SweetLow clean | 9 install (I1/I2) | rel | PASS | reg-inst.txt USB\0002: all 16 INF values as w98n (IntelPortSwitch 0, MaxTransferKB 0x400, InterruptMode 0, PollIdleMs 0x... |
| 98 SE SweetLow clean | 9 bc | rel | PASS | sl-i1b.png 'FC: no differences encountered' |
| 98 SE SweetLow clean | 9 storage fc + unplug, SuperSpeed BOT fc | rel | PASS | s1 port 1 (5000): wizard USB Mass Storage -> C:\WINDOWS\INF\USBSTOR.INF -> Finish (sl-s1..s4); F: F.BAT copy + fc x2 cle... |
| 98 SE SweetLow clean | 9 HID | rel | PASS | m1 port 2 (480): wizard USB Human Interface Device (+ CD) -> Finish (sl-m1..m4); USB mouse moves the pointer 320,248 -> ... |
| 98 SE SweetLow clean | 9 UAS SuperSpeed fc | rel | PASS | u1 port 3 (5000): wizard -> D:\XHCIUAS.INF 'xHCI98 USB Attached SCSI Storage' -> Finish (sl-u1..u5); F: F.BAT fc x2 clea... |
| 98 SE SweetLow clean | 9 audio bound (composite parent) | rel | PASS | a1 port 3 (12): wizard USB Audio Device -> WDMA_USB.INF (+ CD) -> Finish (sl-a1/a2); DM by connection 'xHCI98 USB Compos... |
| 98 SE SweetLow clean | 9 controller disable/enable with a mouse | rel | PASS | Disable -> red X on controller and children (sl-c6dis2.png); Enable (sl-c6p.png Code 22 before) -> all back after Refres... |
| 98 SE SweetLow clean | 9 hub disable/enable with devices behind | rel | PASS | hub h1 port 4 bound silently ('xHCI98 USB Hub'), tablet t1 4.1 and stick s2 4.2 behind it (wizards, sl-h1..h4); hub Disa... |
| 98 SE SweetLow clean | 9 shutdown | rel | PASS | mouse m1, audio a1, hub h1 (tablet 4.1, stick 4.2) attached: 'Windows is shutting down' at 2 s (sd-2s.png), QEMU 'paused... |
| 98 SE SweetLow clean | 9 UAS High Speed fc | rel | PASS | u1 port 3 at 480 Mb/s (info usb), bound silently, F: F.BAT copy + fc x2 clean (sl-hsfc.png) |
| 98 SE SweetLow clean | 9 shutdown (HS launch) | rel | PASS | UAS attached: 'Windows is shutting down' at 2 s (sd-2s.png), paused (shutdown) after 7 s |
| 2000 SP4 | I1 | rel | PASS | adv.bat > Driver (1.99.0.0) > Update Driver > Display a list > Have Disk E:\ > xHCI98 USB 3.x eXtensible Host Controller... |
| 2000 SP4 | I2 | rel | PASS | key 0000 (rk1.txt): DriverVersion 2.2.0.3, XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPoll... |
| 2000 SP4 | I3 | rel | PASS | verb3.txt key 0000 0->3; dc1/ec1.txt problem 22 -> started; s0.txt (verbosity APPLIED 3): intr.mode.read 0 mode 0 delive... |
| 2000 SP4 | SB1 | rel | PASS | s0.txt: susp.switch.value=FFFFFFFF susp.switch=0; "DNCTRL not read (selective suspend off, or no MMIO)", DnctrlEnables 0... |
| 2000 SP4 | M5 | rel | PASS | s0.txt: delivery the line interrupt (INTA), 0 0 1 1; no MSI counter among the nonzero; MsiFilterMode 4294967295 (=FFFFFF... |
| 2000 SP4 | K6 | rel | PASS | k6-xy.txt: X "Windows 2000: platform 2, version 5.0, build 2195, Service Pack 4", "x86 (32-bit Windows)", make/model "no... |
| 2000 SP4 | P1 | rel | PASS | setv 0000 XhciInterruptMode 2 + dc/ec (p1-setv/p1-dc/p1-ec.txt); f.bat F: copy + both fc no differences (p1-fc.txt); poi... |
| 2000 SP4 | P2 | rel | PASS | mode 1 (p2m1.txt): intr.mode.read 1 mode 1 delivery 1 reason 5 "forced line (XhciInterruptMode 1)", tolerance 0; mode 0 ... |
| 2000 SP4 | S1 | rel | PASS | rk1.txt value absent; s1.txt (controller started ~07:11 at the P2 mode-0 dc/ec, read 07:19 after ~8 min idle with HID mo... |
| 2000 SP4 | T1 | rel | PASS | UAS G:\BIG.BIN->C: + C:->G:\B2.BIN, BOT F:\BIG.BIN->C:\BIG2.BIN + back: 4x "FC: no differences encountered" (t1-fc.txt);... |
| 2000 SP4 | T2 | rel | PASS | t2.ps1 per value (setv + dc/ec + VERIFY 2048 KiB 64 MiB on the raw disk, now \.\PhysicalDrive2 after the restart, 0 bad ... |
| 2000 SP4 | T3 | rel | PASS | t3.ps1 per value: regedit /s (t3?-reg.txt), device_del r4 + re-add on port 4 (new device start reads the key), VERIFY 20... |
| 2000 SP4 | G1 | rel | PASS | g1-usbif.txt "usbif list": 6 interfaces = tablet 4.1, mouse 2, BOT stick 1, BOT stick 4.2, UAS 3, XHCI98\HUB port 4; non... |
| 2000 SP4 | G2 | rel | PASS | g1-usbif.txt "usbif hubs": root port 1 (BOT SS) _EX speed 2, V2 proto 4 flags 3 (operating at + capable of SuperSpeed); ... |
| 2000 SP4 | K4 | rel | PASS | UV32.EXE (SweetLow usbview; UVMS does not run on 2000, wave 3): stick "QEMU"/"QEMU USB HARDDRIVE"/"1-0000:00:03.0-1", Bu... |
| 2000 SP4 | K2 | rel | PASS | k2-ls.txt: XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-4 "xHCI98 USB Composite Device" started + USB\VID_46F4&PID_... |
| 2000 SP4 | I4 | rel | PASS | xhci98.sys 427b9d985d1e0c96154075aaee3d7319b302c43ad58eff90c7b05679c4bb41df + XHCISNAP.EXE 79fe8faae71cee6505d603af0765f... |
| 2000 SP4 | T8 | rel | PASS | HAM.BAT x2 (F:\BIG.BIN BOT SS -> C: + fc) started minimized, TONE.WAV via sndrec32, HS USB mouse moved every 0.2 s for 4... |
| 2000 SP4 | K1 | rel | PASS (read only) | cscript //nologo E:\portsw.vbs: "XhciIntelPortSwitch on each xhci98 controller ... 0 - off" (shot k1a), Cancel, exit=0 (... |
| 2000 SP4 | CP1 S3 | rel | NOT MAKEABLE (no standby on the gold acpi=off guest) | Power Options (acpi=off gold): Power Schemes without a standby setting, Hibernate/APM/UPS tabs (shot pw1); Shut Down dia... |
| 2000 SP4 | CP1 S4 | rel | PASS (composite + hub + audio); stick Code 31 after S4 = the known Windows 2000 limitation (brief: record) | Hibernate support enabled (shot pw2); Hibernate 11:12:42 (shot hib1) -> "It is now safe to turn off your computer" (acpi... |
| 2000 SP4 | K5 | rel | PASS | HS usb-mouse port 2 drove the pointer throughout; usb-tablet 4.1 HID started, strings read (K4), reports counted (CP1 re... |
| 2000 SP4 | shutdown (tag r) | rel | PASS | Start > Shut Down > Shut down (shot sd1) -> 'It is now safe to turn off your computer' (shot sd2), quit 11:36 |
| 2000 SP4 | K3 | prev->rel | PASS | prev: Update Driver > Have Disk E:\ (2.2.0.0 INF lists no composite model) > Driver tab 2.2.0.0 (shots p-u1..p-u6), bc v... |
| 2000 SP4 | A-2000 B0 | rel | PASS | dc/ec; bm2 usb-mouse port 2 -> USB Human Interface Device + HID-compliant mouse started, pointer moved (shot s-b0-mouse)... |
| 2000 SP4 | A-2000 B1 | rel | PASS | setv 0000 XhciSelectiveSuspend 1 + XHCIPM.INF copied to C:\WINNT\INF + dc/ec (s-b1/s-b1-ec.txt); bm2 removed, pm1 usb-mo... |
| 2000 SP4 | A-2000 B2 (S1-S6 + IDLE 9; = the S4/S5 reread) | rel | PASS | S2 READS ON + moves -> ok 0 -> 10; S3 WW + D2: s-s3a/s3b port 7 PORTSC 00000E63 PLS 3 in both (>= 5 s apart), susp.seq=0... |
| 2000 SP4 | A-2000 B3 | rel | PASS ((c) NOT HIT) | b3u.ps1: (a) ACTIVE READS ON: device_del pm1 -> gone ("0 matching", w-b3a-gone), controller running; re-add -> client ba... |
| 2000 SP4 | A-2000 B5 | rel | PASS | setv 0000 XhciInterruptMode 2 + dc/ec; IntrDelivery 2 IntrReason 8 PollRole 2 (w-b5-s2a); S2 reads ok 8; S3 WW+D2: port ... |
| 2000 SP4 | P8 / Q2 | qemu | PASS (Code 31 on the BOT stick after a recovery = the known Windows 2000 limitation, recorded; 81 = 0 / 82 unchanged = M-2, park not require... | setv XhciInterruptMode 2 + dc/ec: p8pre.txt 2/8, PollRole 2, PollKicks 1, PollParks 0; setv XhciQemuInject 01050000 at 1... |
| 2000 SP4 | Q3 | qemu | FAIL SUSPECT-DRIVER (17 and 16 leave the UAS stream device dead until re-plug; same family as the xp64/w98 fault-16 suspects - stream-TD err...; **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | 14 (05140083, 13:29): armed while one VERIFY ran clean (229 MB/s, 0 bad); its lost completion landed on the next run: cl... |
| 2000 SP4 | Q4 | qemu | PASS | setv XhciQemuMapGrant 0x10 + dc/ec: q4.txt "map grant 16 registers; a chunk spans at most 16 pages", chunks a URB 9-16: ... |
| 2000 SP4 | CP1 trace | qemu | PASS | Dx per parent FDO, 00000504 on sleep, 00000101 on wake / sleep (debugcon-5.log tail, cp1-sleep-trace.txt): 2 lines =0000... |
| 2000 SP4 | CP1 resume (tag q, launch 6) | qemu | NOT MAKEABLE on this launch (QEMU process wedge, harness/QEMU); trace row above PASS | the resume's enumeration ran (debugcon-6.log ends with a composite select/configure, "thread URB done"), then the QEMU P... |
| 2000 SP4 | A-2000 fault leg (IDLE 1 + XhciQemuInject 07 + disable/enable/remove) | qemu | FAIL SUSPECT-DRIVER (attribution open: the pinned all-ones containment keeps transfers, and a device PDO's REMOVE then waits forever - drive...; **final: KNOWN - documented limitation (disable after a pinned containment hangs; transfers kept for DMA safety)** | try 1 (f-a*): "PMCTL IDLE 1 & setv XhciQemuInject 0A070000 & dc.bat" on one line: debugcon "idle notification held for s... |
| 2000 SP4 | P5 | qemu | PASS (channel: DebugView / debugcon "M86 S" start line; XHCISNAP cannot reach a failed controller) | setv XhciSelectiveSuspend 0, XhciInterruptMode 1, XhciQemuNoInterrupt 1, XhciLogDebugView 1 (p5-setv.txt); DBGVIEW /f /l... |
| 2000 SP4 | A-2000 fault-leg follow-up (coordinator: R, i, ii; each from a fresh boot, then the controller disable) | qemu | 15:30 | FAIL SUSPECT-DRIVER (pinned-containment removal: outstanding URBs not completed at stop/remove; 4/4 reproduce, R/i/ii id... |
| 2000 SMP + Verifier | verifier /query at start | rel | PASS | 06:33: Level 0000001B, xhci98.sys loads 1 unloads 0 (base's old driver), AllocationsFailed 0 (verifier-start.txt) |
| 2000 SMP + Verifier | check-smp-parallelism.ps1 -MonitorPort 57109 | rel | PASS | 2 vCPUs, distinct thread ids 43144/27020, affinity 0xFFFFFFFF (32 of 32), exit 0 (parallelism-check.txt) |
| 2000 SMP + Verifier | I1 | rel | PASS | adv.bat (Driver tab "xHCI USB 2.0 Host Controller (xhci98)") > Update Driver > list > Have Disk E:\ (manufacturers xHCI9... |
| 2000 SMP + Verifier | I2 | rel | PASS | key 0002 (rk1.txt): DriverVersion 2.2.0.3, IntelPortSwitch 0, MaxTransferKB 0x400, InterruptMode 0, PollIdleMs 0x32, Pol... |
| 2000 SMP + Verifier | I3 | rel | PASS | verb3.txt keys 0002 0->3 and 0005 unset->3; dc/ec; s0.txt (APPLIED 3, "2 processor(s)"): intr.mode.read 0 mode 0 deliver... |
| 2000 SMP + Verifier | SB1 | rel | PASS | s0.txt: susp.switch.value=FFFFFFFF susp.switch=0; DNCTRL not read, DnctrlEnables/Readback 0x0000; no Susp*/Ss*/FuncWake*... |
| 2000 SMP + Verifier | T5 (1024) | rel | PASS | UAS G:\BIG.BIN->C: fc clean, C:->G:\B2.BIN fc clean (t5-uas1024.txt); BOT F:\BIG.BIN->C:\BIG2.BIN fc clean, C:->F:\B3.BI... |
| 2000 SMP + Verifier | B4 (1024) | rel | PASS (check not run / Enabled 0 / big blocks 0 = default WideMap off) | XFERTEST WRITE \.\PhysicalDrive4 1024 64 then VERIFY 64/1024/2048 KiB blocks: 0 sectors bad each (b4-xf1024.txt; host ra... |
| 2000 SMP + Verifier | T5 (2048) + B4 (2048) | rel | PASS | setv 0002 XhciMaxTransferKB 0x800 + dc/ec (t5-setv/t5-dc.txt; all devnodes back started, t5-late.txt); UAS G: and BOT F:... |
| 2000 SMP + Verifier | M6 | rel | PASS | verifier-t5.txt (07:55): Level 1B, xhci98.sys loads 4 unloads 3 (install + 2 dc/ec... ), AllocationsAttempted 396 = Succ... |
| 2000 SMP + Verifier | I4 | rel | PASS | xhci98.sys 427b9d985d1e0c96154075aaee3d7319b302c43ad58eff90c7b05679c4bb41df + XHCISNAP.EXE 79fe8faae71cee6505d603af0765f... |
| 2000 SMP + Verifier | SB3 | rel | PASS | WW: wake req 1 cb 1 last 0xC00000BB (STATUS_NOT_SUPPORTED) (sb3-ww.txt); D2: dev D2, power req 1 cb 1 (sb3-d2.txt); sb3a... |
| 2000 SMP + Verifier | SB4 | rel | PASS (ACTIVE + SUSPENDED); mid-D2 NOT HIT (timing; p36-mtx2 hit it on the integration build with SuspGone 1) | ACTIVE: device_del sb1 after SB3 -> sb4a: port 1 000002A0 empty, slot freed, no new sequence, controller running; SUSPEN... |
| 2000 SMP + Verifier | SB5 | rel | PASS | re-add s1j, D2 -> sb5s: PLS 3, slot 1 SUSPENDED, susp.seq=00D02110, SuspSuspended 6; dc.bat + ec.bat: controller, root h... |
| 2000 SMP + Verifier | SB6 | rel | PASS | setv 0002 XhciInterruptMode 2 + dc/ec (sb6-setv.txt), client rebound; WW refused 0xC00000BB, D2 (sb6-pre.txt) -> sb6a (+... |
| 2000 SMP + Verifier | SB7 | rel | PASS (as expected on QEMU; bench-owed) | no FuncWake*/SsSuspArms/SsSuspHub* counter nonzero in any switch-1 snapshot (sw1-ec, sb3a-c, sb4a/b/bs/c..c5, sb5s, sb5,... |
| 2000 SMP + Verifier | CP1 S3 | rel | NOT MAKEABLE (Cirrus VGA vetoes standby; QEMU/Windows video, as in the early-warning run) | Shut Down dialog offers Log off / Shut down / Restart / Stand by / Hibernate (shot sby1); Stand by -> "System Standby Fa... |
| 2000 SMP + Verifier | CP1 S4 | rel | PASS | Hibernate 09:21:14 (shot hb1) -> VM paused (shutdown) at 09:21:38 (shot hb2); launch 2 (pid 27020, 09:22, same overlay, ... |
| 2000 SMP + Verifier | verifier /query at the end | rel | PASS | verifier-end.txt (09:30, boot of launch 1 continued through the hibernate): Level 1B, xhci98.sys loads 8 unloads 7, Allo... |
| 2000 SMP + Verifier | shutdown (tag r) | rel | PASS | Start > Shut Down > Shut down (shot sd1): VM paused (shutdown) 09:32:23 (ACPI power-off), quit through the monitor |
| 2000 SMP + Verifier | A-SMP verifier /query at start (tag s) | rel | PASS | 09:35 s-verifier-start.txt: Level 0000001B, xhci98.sys loads 1 unloads 0 (base's old driver), AllocationsFailed 0 |
| 2000 SMP + Verifier | A-SMP I1 (tag s) | rel | PASS | Update Driver > list > Have Disk E:\ > xHCI98 USB 3.x eXtensible Host Controller > Finish (shots s-u5..s-u9; a first try... |
| 2000 SMP + Verifier | A-SMP B1 | rel | PASS | XHCISNAP -verbosity 3 + setv 0002 XhciSelectiveSuspend 1 + copy E:\XHCIPM.INF C:\WINNT\INF (s-b1.txt), dc/ec (s-b1-ec.tx... |
| 2000 SMP + Verifier | A-SMP B2 (S1-S6 + IDLE 9) | rel | PASS | S2: READS ON + 5 move pairs -> ReadsCompleted 0 -> 10 (s-s2a/b-st.txt); S3: WW + D2 -> dev D2, wake req 1 held 1; s-s3a ... |
| 2000 SMP + Verifier | A-SMP B3 | rel | PASS ((c) NOT HIT) | b3u.ps1: (a) ACTIVE with READS ON: device_del pm1 -> devnode gone (s-b3a-gone.txt "0 matching"), controller running, no ... |
| 2000 SMP + Verifier | A-SMP B5 | rel | PASS | setv 0002 XhciInterruptMode 2 + dc/ec; s-b5-s1: IntrDelivery 2 IntrReason 8 PollRole 2; S2 READS ON + moves -> ok 8; S3 ... |
| 2000 SMP + Verifier | A-SMP S4 with the client D0 (offered: Stand by is vetoed by Cirrus - tag r CP1 - so S4 only) | rel | PASS (Code 31 on the BOT stick = known limitation, recorded) | Hibernate 10:55:02 -> powered off 10:55:15 (shot s-hb1); launch 4 (pid 35976, same overlay, -BootStick, pm1 hot-added at... |
| 2000 SMP + Verifier | A-SMP verifier /query at the end (tag s) | rel | PASS | s-verifier-end.txt 11:07: Level 1B, xhci98.sys loads 7 unloads 6, AllocationsAttempted 288 = Succeeded 288 = SpecialPool... |
| 2000 SMP + Verifier | A-SMP shutdown (S5) with the client D0 | rel | PASS | s-sdpre-st.txt: dev D0, reads on 1; Start > Shut Down > Shut down 11:09:19 (shot s-sd1) -> VM paused (shutdown) 11:09:41... |
| 2000 SMP + Verifier | WR (warm restart timing, mode 2 vs mode 0 vs cold boot) | rel | NOT RUN (OWED: no quiet host; moves to the post-rebuild re-read on the re-stamped 2.2.0.3). Side reading: the plain 2000 guest (w2k tag p, 1... | budget.ps1 at 15:32: 11 guests, 18/32 vCPUs (other workers' priority runs); per the coordinator's ruling: not waited for |
| 2000 clean | 9-install | rel | PASS | wizard shots boot1, fi1-fi6 ("Completing ... xHCI98 USB 3.x eXtensible Host Controller"); root hub installed silently; n... |
| 2000 clean | 9-values | rel | PASS | key 0000 (rk1.txt): LogVerbosity 0, LogDebugView 0, ForceBulkOnly 0, FastPollFsLs 0, FirstEnumWaitMs 0x1388, FirstEnumPo... |
| 2000 clean | 9-bc | rel | PASS | bc1.txt: xhci98.sys "FC: no differences encountered" (xhcimsi absent on 2000, expected) |
| 2000 clean | 9-HID | rel | PASS | m2: USB Human Interface Device + HID-compliant mouse started problem=0, no wizard (hid-ls.txt); pointer driven by m2 for... |
| 2000 clean | 9-BOT-SS | rel | PASS | s1 port 1 "5000 Mb/s QEMU USB MSD", USB Mass Storage + USBSTOR disk started (hid-ls.txt); f.bat F: copy + both fc "no di... |
| 2000 clean | 9-storage-unplug | rel | PASS | device_del s1: no dialog (shot unplug1), ls: only root hub + mouse left, no stale USBSTOR node (unplug-ls.txt) |
| 2000 clean | 9-audio | rel | PASS | a3 port 3 (12 Mb/s), no wizard: XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-3 "xHCI98 USB Composite Device" starte... |
| 2000 clean | 9-UAS-SS | rel | PASS | u3 port 3 "5000 Mb/s USB Attached SCSI HBA"; Found New Hardware > Search > Specify location E:\ > e:\xhciuas.inf > Finis... |
| 2000 clean | 9-dc/ec with mouse | rel | PASS | dc: problem 22 (dc1.txt); ec: controller + root hub started (ec1.txt), mouse m2 HID started problem=0 (ec-ls.txt), point... |
| 2000 clean | 9-hub | rel | PASS | h4 port 4 (xHCI98 USB Hub, silent install), hm1 mouse 4.1, hs1 stick 4.2 all started (hub-ls.txt); hub stick fc clean (h... |
| 2000 clean | 9-shutdown | rel | PASS | Start > Shut Down > OK (shot sd1) -> "It is now safe to turn off your computer." (shot sd2), VM status running (acpi=off... |
| 2000 clean | 9-UAS-HS | rel | PASS | uh1 port 1 "480 Mb/s USB Attached SCSI HBA"; Found New Hardware wizard again (new devnode at a new port, as on 2.2.0.0's... |
| 2000 clean | 9-shutdown (2) | rel | PASS | "It is now safe to turn off your computer." (shot sd3); quit |
| XP SP3 | I1 | rel | PASS | launch 1 (-Tag r -BootStick -BootMouse -AudioWav, pid 43428): FNH cancelled, i.bat -> Logo alt-i -> 'update E:\xhci98.in... |
| XP SP3 | I2 | rel | PASS | rk-r.txt key 0000: 16 values: XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, XhciFirst... |
| XP SP3 | I3 | rel | PASS | v3.txt (0000 was 0 -> 3; dc problem 22, ec started, root hub started); i3.txt: intr.mode.read 0 intr.mode 0 intr.deliver... |
| XP SP3 | I4 | rel | PASS | xhci98.sys 427b9d98..41df (269400) and XHCISNAP 79fe8faa..5112 both in out\p36-stamp\hashes.txt (2.2.0.3, zip a6d91ec4);... |
| XP SP3 | M5 | rel | PASS | i3.txt: IntrModeRead/IntrMode 0 (zero, omitted), IntrDelivery 1, IntrReason 1; no MSI word 96-113 among the 27 nonzero; ... |
| XP SP3 | SB1 | rel | PASS | i3.txt (verbosity 3, read before any value changed): susp.switch.value=FFFFFFFF susp.switch=00000000; 'DNCTRL not read (... |
| XP SP3 | K6 | rel | PASS | k6.txt (X then Y): X 'Windows XP: platform 2, version 5.1, build 2600, Service Pack 3', 'x86 (32-bit Windows)', 'make/mo... |
| XP SP3 | S1 | rel | PASS | s1f.txt f.bat F copy + fc x2 no differences at 06:43:44; s1.txt at 06:46:10 (~2.5 min untouched): susp.switch.value FFFF... |
| XP SP3 | T1 (copy part) | rel | PASS | boot stick swapped for the BIG stick (sb1 port 1, 5000) + UAS u3 port 3 (5000; FNH cancelled, u.bat + Logo alt-i -> 'xHC... |
| XP SP3 | G1 (root-port part) | rel | PASS | g1.txt usbif list: 4 interfaces = mouse USB\VID_0627&PID_0001, BOT USB\VID_46F4&PID_0001, UAS USB\VID_46F4&PID_0003, XHC... |
| XP SP3 | G2 | rel | PASS | g1.txt usbif hubs: port 1 BOT 46f4:0001 _EX speed 2, V2 proto 4 flags 3; port 3 UAS _EX speed 2 V2 flags 3; mouse port 6... |
| XP SP3 | K2 (structure) | rel | PASS | a4 port 4: FNH 'xHCI98 USB Composite Device' + Logo alt-i + Finish (shots k2w-1/2), USB Audio Device installed silently;... |
| XP SP3 | K4 | rel | PASS | usbview UV32: stick 'QEMU' / 'QEMU USB HARDDRIVE' / serial '1-0000:00:03.0-1' (uv-p1), mouse 'QEMU' / 'QEMU USB Mouse' /... |
| XP SP3 | I18 | rel | PASS | hub h4 hot-added on root port 4 at the desktop (FNH 'xHCI98 USB Hub' + Logo, shots h4w-1/2), usb-mouse m41 at 4.1 + usb-... |
| XP SP3 | G1 (hub part) | rel | PASS | k5.txt usbif list = 6 interfaces: tablet 4.2, kbd 4.3, mouse 2, mouse 4.1, UAS 3, XHCI98\HUB&VID_0409&PID_55AA (one on t... |
| XP SP3 | K5 | rel | PASS | stick BOT SS + UAS (T1 fc clean), usb-mouse root port 2 (HS) and 4.1 (FS) moved the pointer, usb-tablet 4.2 positioned i... |
| XP SP3 | T2 (64) | rel | PASS | setv 40 + shutdown, relaunch 2 (-Display none -BootStick -BootMouse -AudioWav, pid 28680; cold boot = restart); uasraw u... |
| XP SP3 | K1 | rel | PASS (1 leg) | cscript //nologo E:\portsw.vbs: dialog 'XhciIntelPortSwitch on each xhci98 controller ... 0 - off', typed 1 (k1a) -> con... |
| XP SP3 | K2 (audio after) | rel | PASS | relaunch 2: a4 port 4 bound silently, pnpctl disable/enable COMPOSITE -> both started (t8w2 shot), TONE.WAV alone afterw... |
| XP SP3 | T2 (2048) | rel | PASS | setv 800 + shutdown -r (in-guest restart); reg 0x800 (k1e.txt); uasraw PhysicalDrive2 (diskpart after the restart); t2-2... |
| XP SP3 | K1 (0 leg) | rel | PASS | 'cscript //nologo E:\portsw.vbs 0' -> '0 - off', 'now 0 on every xhci98 controller', RESTART note, errorlevel 0 (k1d.txt... |
| XP SP3 | T2 (4096) | rel | PASS | setv 1000 + shutdown -r; reg 0x1000; t2-4096.txt VERIFY 1M + 2M -skew 512 0 bad, READ 1M/2M 79.7/89.0 MB/s; t2s4096.txt ... |
| XP SP3 | T2 (0) | rel | PASS | setv 0 + shutdown -r; reg 0x0; t2-0.txt VERIFY 1M + 2M -skew 512 0 bad, READ 1M/2M 88.0/87.1 MB/s; t2s0.txt xfer.max.val... |
| XP SP3 | B3 | rel | PASS | default (1024, uasraw u1 PhysicalDrive2): b11.txt (the 1024 default reads after the restart: VERIFY 1M + 2M -skew 512 0 ... |
| XP SP3 | T1 (XFERTEST part) | rel | PASS | uasraw u1: t2-2048.txt READ 64K 26.1 / 1M 166.6 / 2M 199.7 / 2M -skew 512 133.5 MB/s, VERIFY 1M + 2M -skew 512 0 sectors... |
| XP SP3 | P1 | rel | PASS | setv XhciInterruptMode 2 + dc/ec (p1set.txt); f.bat F copy + fc x2 no differences (p1f.txt); mouse moved the pointer (p1... |
| XP SP3 | P2 | rel | PASS | mode 1 (setv + dc/ec, p2set.txt): p2m1.txt IntrModeRead 1, IntrMode 1, IntrDelivery 1, IntrReason 5, no PollRole/WatchLo... |
| XP SP3 | P9 (rel part) | rel | PASS | setv XhciLogDebugView 1; DebugView 4.x ('/accepteula' and '/k' not switches of this version: EULA pre-accepted by HKCU\S... |
| XP SP3 | SB2 | rel | PASS | setv XhciLogDebugView 0 + XhciSelectiveSuspend 1 + dc/ec (sb2set.txt, all started); sb2.txt: susp.switch.value 1 switch ... |
| XP SP3 | SB3 | rel | PASS | client bound by 'pnpctl update E:\XHCIPM.INF USB\VID_46F4&PID_0001' + Logo alt-i (sb3a shot): 'xHCI98 selective suspend ... |
| XP SP3 | SB4 | rel | PASS (ACTIVE, SUSPENDED); mid-D2 NOT MAKEABLE (QEMU finishes U3 entry before a device_del lands; 1 try here, 4 in the early-warning run) | SUSPENDED (sb4b0/sb4b.txt): PMCTL D2 (seq 00102110, SuspSuspended 2) then device_del bs1 -> port 1 000002A0 Empty, every... |
| XP SP3 | SB5 | rel | PASS | s4 re-added (client rebound), PMCTL D2 -> dev D2; dc (problem 22) / ec (started) with the device suspended, no hang; aft... |
| XP SP3 | SB6 | rel | PASS | setv XhciInterruptMode 2 + dc/ec (sb6set.txt): intr.delivery 2 intr.reason 8, PollRole 2; PMCTL WW -> 0xC00000BB; D2 -> ... |
| XP SP3 | SB7 | rel | PASS (as expected on QEMU; bench-owed) | sb2.txt, sb3c.txt, sb5a.txt, sb6b.txt: none of FuncWake*/SsSuspArms/SsSuspHub* among the nonzero counters (46 of 167 non... |
| XP SP3 | CP1 S4 | rel | PASS | overlay vm\t36fin\mtx-xp-h.qcow2, launch 3 (-Vga cirrus-vga -BootMouse -AudioWav -Display none): install from the zip (i... |
| XP SP3 | D5 (SUSPENDED) | rel | PASS | -Tag h, XhciSelectiveSuspend 1 + dc/ec (d5set.txt), usb-mouse bm1 removed, the -PmMouse client bound to the usb-tablet t... |
| XP SP3 | D5 (mode 2) | rel | PASS | -Tag h: usb-storage s1 on port 1, setv XhciInterruptMode 2 + dc/ec (d5m2.txt: IntrDelivery 2 IntrReason 8 PollRole 2, f.... |
| XP SP3 | A-XP C1 | rel | PASS (reading recorded: hidusb's own idle does not run on XP SP3 with QEMU's mouse even with the PM checkbox, 30 min - Windows XP behaviour,... | usb-mouse m1 port 2 (HidUsb, HID-compliant mouse); reg add ...\Enum\USB\VID_0627&PID_0001\89126-0000:00:03.0-2\Device Pa... |
| XP SP3 | A-XP S4/S5 reread | rel | PASS | s1/s2.txt READS ON + 3 presses ok 0 -> 6; s3a/s3b.txt (6 s apart) WW + D2: dev D2, wake held 1, SuspSuspendsBegun/SuspSu... |
| XP SP3 | A-XP C3 | rel | PASS | (QEMU's mouse, tablet and kbd all carry 0627:0001 = the client's ID, so the 'other device' is a BOT stick.) Composite: u... |
| XP SP3 | A-XP C4 | rel | PASS (callback-running unplug window NOT MAKEABLE in QEMU, recorded) | (legs\xp\h\c4.ps1) ACTIVE: client behind the hub with reads on, device_del -> c4a.txt client gone, others started, no ne... |
| XP SP3 | A-XP C5 | rel | PASS | setv XhciInterruptMode 2 + dc/ec (c5set.txt; c5d.txt IntrDelivery 2 IntrReason 8 PollRole 2); READS ON + WW + D2: dev D2... |
| XP SP3 | K3 | prev->rel | PASS | -Tag p overlay, launch 9 (pid 8052, -Flav prev = releases\2.2.0.0\release-x86, xhci98.sys 98f1fb1d...b88a, package-prev.... |
| XP SP3 | T6 | rel | PASS | -Tag m overlay, launch 10 (pid 47824, -Mem 256 -Display none): install from the zip (inst-m.txt bc clean), BIG stick sb1... |
| XP SP3 | B7 | rel | PASS | b7set.txt (XhciWideMap 1 + dc/ec): check 1 enabled 1, 'wide entries 8, 0 in use', big stream blocks built 3 refused 0; 5... |
| XP SP3 | D1 | dbg | PASS | -Tag d overlay, launch 11 (pid 48016, -Flav dbg = zip debug-x86, xhci98.sys b108c28f...1a2c 273512 B, package-dbg.txt): ... |
| XP SP3 | E-1 / P3 | qemu | PASS | setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc/ec (e1set.txt: BOT stick + boot mouse started problem=0); usb-mouse m... |
| XP SP3 | E-2 / P4 | qemu | PASS | setv XhciInterruptMode 0 (DeadLine 1 kept) + dc/ec (e2set.txt): IntrDelivery 2, IntrReason 9, 'delivery in effect POLLIN... |
| XP SP3 | Q2 | qemu | PASS | in the polled start: setv XhciQemuInject 01050000 (q2.txt): HchRecoveries 1, 'recovery window 1 of 3 begun inside ten mi... |
| XP SP3 | P5 | qemu | PASS | setv XhciQemuDeadLine 0, XhciInterruptMode 1, XhciQemuNoInterrupt 1 + dc/ec (p5.txt): controller 'not started HAS_PROBLE... |
| XP SP3 | P9 (qemu part) | qemu | PASS | DebugView C:\dv.log across this overlay's starts (e1dc.txt / p5.txt): mode 1 starts 'intr.delivery=1 intr.reason=5' + qe... |
| XP SP3 | Q3 | qemu | FAIL -> 36.54 (UAS stream fault recovery; coordinator ruling - evidence legs\xp\xq3-15*.txt, xq3-14*.txt, C:\dv.log slices in them); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | uasraw u1 on port 3 (xhciuas via u.bat, PhysicalDrive3), legs\xp\h\inj.ps1 VERIFY 1M x128 MiB loops; 15 stop (fire 02150... |
| XP SP3 | Q4 | qemu | PASS | setv XhciQemuMapGrant 0x10 + dc/ec, fresh uasraw q3 (PhysicalDrive3): q4.txt 'map grant 16 registers; a chunk spans at m... |
| XP SP3 | CP1 trace | qemu | PASS | -Tag h overlay relaunched -Flav qemu (launch 13, pid 50636, cirrus, -Display none): sw.bat copied the qemu xhci98.sys + ... |
| XP SP3 | T8 | rel | PASS | final reading (launch 15, -Tag r, pid 49720, after resetting mode 0 / switch absent; the BOT stick on this overlay is ow... |
| XP x64 | I1 | rel | PASS | launch 1 (-Tag r -BootStick -BootMouse -AudioWav, pid 41772): FNH cancelled, i.bat -> Logo alt-i -> 'update ... OK (err ... |
| XP x64 | I2 | rel | PASS | rk-r.txt key 0000: the same 16 values as x86 (XhciIntelPortSwitch 0, XhciIntelIrqRoute 1, XhciMaxTransferKB 0x400, XhciI... |
| XP x64 | I3 | rel | PASS | v3.txt (0000 was 0 -> 3; dc problem 22, ec started, root hub started); i3.txt: intr.mode.read 0 intr.mode 0 intr.deliver... |
| XP x64 | I4 | rel | PASS | xhci98.sys fe1cf7b3..cdb9 (309248) and XHCISNAP 79fe8faa..5112 both in out\p36-stamp\hashes.txt (2.2.0.3, zip a6d91ec4);... |
| XP x64 | M5 | rel | PASS | i3.txt: IntrModeRead/IntrMode 0 (zero, omitted), IntrDelivery 1, IntrReason 1; no MSI word 96-113 among the 27 nonzero; ... |
| XP x64 | SB1 | rel | PASS | i3.txt (verbosity 3, read before any value changed): susp.switch.value=FFFFFFFF susp.switch=00000000; 'DNCTRL not read (... |
| XP x64 | K6 + K6x | rel | PASS | k6.txt: X 'Windows XP x64: platform 2, version 5.2, build 3790, Service Pack 2', '64-bit Windows (x64); this x86 tool ra... |
| XP x64 | S1 | rel | PASS | s1f.txt f.bat F fc x2 clean 06:43:44; s1.txt 06:46:10: susp.switch.value FFFFFFFF switch 0, no Susp*/Idle* among 27 nonz... |
| XP x64 | T1 (copy part) | rel | PASS | boot stick swapped for the BIG stick (sb1 port 1, 5000) + UAS u3 port 3 (5000; FNH cancelled, u.bat + Logo alt-i -> 'xHC... |
| XP x64 | G1 (root-port part) | rel | PASS | g1.txt usbif list: 4 interfaces = mouse USB\VID_0627&PID_0001, BOT USB\VID_46F4&PID_0001, UAS USB\VID_46F4&PID_0003, XHC... |
| XP x64 | G2 | rel | PASS | g1.txt usbif hubs: port 1 BOT 46f4:0001 _EX speed 2, V2 proto 4 flags 3; port 3 UAS _EX speed 2 V2 flags 3; mouse port 6... |
| XP x64 | K2 (structure) | rel | PASS | a4 port 4: FNH 'xHCI98 USB Composite Device' + Logo alt-i + Finish (shots k2w-1/2), USB Audio Device installed silently;... |
| XP x64 | K4 | rel | PASS | usbview UV64: stick 'QEMU' / 'QEMU USB HARDDRIVE' / serial '1-0000:00:03.0-1' (uv-p1), mouse 'QEMU' / 'QEMU USB Mouse' /... |
| XP x64 | I18 | rel | PASS | hub h4 hot-added on root port 4 at the desktop (FNH 'xHCI98 USB Hub' + Logo, shots h4w-1/2), usb-mouse m41 at 4.1 + usb-... |
| XP x64 | G1 (hub part) | rel | PASS | k5.txt usbif list = 6 interfaces: tablet 4.2, kbd 4.3, mouse 2, mouse 4.1, UAS 3, XHCI98\HUB&VID_0409&PID_55AA (one on t... |
| XP x64 | K5 | rel | PASS | stick BOT SS + UAS (T1 fc clean), usb-mouse root port 2 (HS) and 4.1 (FS) moved the pointer, usb-tablet 4.2 positioned i... |
| XP x64 | T2/B2 (64) | rel | PASS | setv 40 + shutdown, relaunch 2 (-Display none, pid 43068); uasraw u1 port 3 (xp64-u1.raw, PhysicalDrive3, disks.txt scru... |
| XP x64 | K1 | rel | PASS (1 leg) | dialog '... 0 - off' (k1a), typed 1 -> 'now 1 on every xhci98 controller' + RESTART note (k1b); after the Windows restar... |
| XP x64 | T8 | rel | PASS | relaunch 2 -Display none, budget 21 vCPUs at the first play; play 1 (after parent disable/enable, with BIG.BIN C:->F: (B... |
| XP x64 | K2 (audio after) | rel | PASS | relaunch 2: usb-audio a4 port 4 bound silently, pnpctl disable/enable COMPOSITE -> both started, then TONE.WAV played wh... |
| XP x64 | T2/B2 (2048) | rel | PASS | setv 800 + shutdown -r (in-guest restart); reg 0x800 (k1e.txt); uasraw PhysicalDrive2 (diskpart after the restart); t2-2... |
| XP x64 | K1 (0 leg) | rel | PASS | 'cscript //nologo E:\portsw.vbs 0' -> '0 - off', 'now 0 on every xhci98 controller', RESTART note, errorlevel 0 (k1d.txt... |
| XP x64 | B1 | rel | PASS | stamped build: promotion is OFF by default (XhciWideMap absent), so at 2048 alone TDs 512/513 = 0 (t2s2048.txt); B1 read... |
| XP x64 | T2/B2 (4096) | rel | PASS | setv 1000 + shutdown -r; reg 0x1000; t2-4096.txt VERIFY 1M + 2M -skew 512 0 bad, READ 1M/2M 543.4/563.1 MB/s; t2s4096.tx... |
| XP x64 | T2/B2 (0) | rel | PASS | setv 0 + shutdown -r; reg 0x0; t2-0.txt VERIFY 1M + 2M -skew 512 0 bad, READ 1M/2M 620.1/658.6 MB/s; t2s0.txt xfer.max.v... |
| XP x64 | B11 | rel | PASS | b11set.txt: setv 400, reg add Control\usbstor\46F40003 MaximumTransferLength REG_DWORD 0x200000 (Services\usbstor has no... |
| XP x64 | B3 | rel | PASS | default (1024, uasraw u1 PhysicalDrive2): b3w0x.txt (usbstor key deleted + dc/ec): VERIFY 1M + 2M -skew 512 0 bad, READ ... |
| XP x64 | B13 | rel | PASS | WideMap removed + dc/ec (defaults); bs1/bm1 removed, usb-mouse m1 on port 1 (HS), uasraw u2 on port 2 (xp64-u2.raw, FNH ... |
| XP x64 | P1 | rel | PASS | setv XhciInterruptMode 2 + dc/ec (p1set.txt); UAS raw VERIFY 1M x256 on PhysicalDrive2 and 3, 0 sectors bad (p1f.txt; BO... |
| XP x64 | P2 | rel | PASS | mode 1 (setv + dc/ec, p2set.txt): p2m1.txt IntrModeRead 1, IntrMode 1, IntrDelivery 1, IntrReason 5, no PollRole/WatchLo... |
| XP x64 | CP1 S4 | rel | PASS | std VGA (XP x64 offers hibernate; -Tag h not needed): powercfg /hibernate on, 'System hibernates (DC) After 15 mins' (cp... |
| XP x64 | E-1 / P3 | qemu | PASS | setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc/ec (e1set.txt): stick bs1 (port 1) + mouse bm1 started; usb-mouse m3 ... |
| XP x64 | E-2 / P4 | qemu | PASS | setv XhciInterruptMode 0 (DeadLine 1 kept) + dc/ec (e2set.txt; a first try was refused 'OK REBOOT-NEEDED' while the mode... |
| XP x64 | Q2 | qemu | PASS | in the moved (polling) start: reg add XhciQemuInject 0x01050000 (q2.txt): tolerance HchRecoveries 1, 'recovery window 1 ... |
| XP x64 | B8 | qemu | FAIL (QEMU abort; attribution QEMU usb-uas, not the driver - 2/2; stage-2 stream fallback bench-owed); **final: NOT MAKEABLE - QEMU usb-uas aborts (dev-uas.c:895, one packet per stream)** | (WideMap 1 needed: blocks are attempted only with promotion on.) Try 1 (launch 4): setv XhciWideMap 1 + BigRefuse 1 + Ma... |
| XP x64 | Q3 (15 stop) | qemu | PASS | q3-15.txt: qemu.inj.bulk.armed 00150307, fire 02150083, qemu.inj.bulk=150D0307 (stream 0x0D, slot 3, DCI 7), bulk.stoppe... |
| XP x64 | Q3 (16 error) | qemu | FAIL SUSPECT-DRIVER (2/2; attribution open: xhciuas/HCD recovery of a UAS stream after a halted data TD, or QEMU usb-uas state after the emu...; **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | q3-16.txt: fire 03160083, qemu.inj.bulk=160F0307, xfer.error=00030704, qemu.inj.ended 16, done 16; Transaction Error 1, ... |
| XP x64 | Q3 (17 short) | qemu | NOT RUN (cleanly: the device was already stuck; re-run below) | fired 04170083 (q3-17.txt: qemu.inj.bulk=17010307, done 17) on the device already stuck by 16 - no reading of its own |
| XP x64 | Q3 (17 short, rerun) | qemu | FAIL SUSPECT-DRIVER (same family as 16; attribution: see Q3-16x); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | fresh uasraw q3 (q3re2.txt VERIFY clean), fire 06170083 (q3-17b.txt: qemu.inj.bulk=17090307, qemu.inj.abandon=00030705, ... |
| XP x64 | Q3-16x (a) | qemu | PASS (non-stream path recovers in the HCD) | fire 07160181 (root port 1, the BOT stick's bulk-in 0x81) during 6x RAND.BIN F:->C: + fc (q3-16x-a.txt): qemu.inj.bulk=1... |
| XP x64 | Q3-16x (b) | qemu | (reading) | fresh uasraw q4, fire 08160083 (q16x-b.txt: qemu.inj.bulk=16050307 = stream 5, slot 3, DCI 7 = data-in 0x83). Trace (qem... |
| XP x64 | Q3-16x (e) | qemu(old) | (attribution reading) | fresh overlay vm\t36fin\mtx-xp64-o.qcow2, the old package staged into xfer-u1 (kit flavour slot u1 reused, contents = ou... |
| XP x64 | Q3-16x (c) | qemu |  | src\hcd_cfg.c ~4345: the soft retry's decision is forced away from replay for 'pipe->Streams != NULL |
| XP x64 | Q3 (16/17) ATTRIBUTION | qemu | (attribution) | Pre-existing (reproduced on the pre-stage-2 build 90c625c), not a 2.2.0.3 regression. Driver-side: a stream TD error has... |
| XP x64 | Q3 (14 timeout) | qemu | PASS (recovers through the class driver's ~160 s timeout - the same timeout that ends the fault-16 stall when it recovers) | launch 8 (pid 28048), fresh uasraw q5 (PhysicalDrive3), fire 09140083 (q3-14.txt: qemu.inj.bulk.armed 00140307, qemu.inj... |
| XP x64 | Q4 | qemu | PASS | setv XhciQemuMapGrant 0x10 + dc/ec (q4set.txt): qemu.mapgrant 10, applied 10, 'map grant 16 registers; a chunk spans at ... |
| XP x64 | B9 (a) | qemu | NOT MAKEABLE (QEMU usb-uas abort on the stage-2 stream path; see B8) | the BigRefuse 1 + WideMap 1 stage-2 stream path aborts QEMU itself (B8: usb-uas data3 assertion, 2/2) before any fault c... |
| XP x64 | B9 (b) | qemu | FAIL SUSPECT-DRIVER (attribution open: not specific to the big-TD path - WideMap 0 stalls too; 2048 vs 1024 differs; one try each); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | setv XhciWideMap 1 + XhciMaxTransferKB 0x800 + dc/ec (b9set.txt), VERIFY 2M loop on the UAS, fire 0A150083 (stop) -> b9-... |
| XP x64 | B10 | qemu | NOT MAKEABLE (no amd64 client; the x86 SB3-SB5 legs on xp cover suspend + unplug + dc/ec of a stick) | on XP x64 nothing can suspend the UAS/BOT device: the stage A/B test client (XHCIPM.SYS) exists for x86 only (stage.ps1 ... |
| XP x64 | B12 | qemu | NOT MAKEABLE (bench / host suite) | suspend: not makeable on x64 (B10); recovery: XhciQemuInject 05 invalidates every slot and re-enumerates the devices, so... |
| Vista x86 | I1 | rel | PASS | elevated cmd (alt-i), pnputil -i -a E:\xhci98.inf -> unsigned box (i1-unsigned.png) Install anyway -> oem3.inf 'Successf... |
| Vista x86 | I2 | rel | PASS (note: INF does not write WideMap 0 explicitly; default off holds) | key 0000 (i2-rk.txt, read before any change): IntelPortSwitch 0, IntelIrqRoute 1, MaxTransferKB 0x400, InterruptMode 0, ... |
| Vista x86 | I3 | rel | PASS | XHCISNAP -verbosity 2 (0000 0->2, i3-verb.txt), dc Code 22 / ec started (i3-de.txt); i3.txt: intr.mode 0, intr.delivery ... |
| Vista x86 | I4 | rel | PASS | package-rel.txt: xhci98.sys 427b9d98.. (zip release-x86) + XHCISNAP 79fe8faa.. (zip xhcisnap), both from out\xhci98-2.2.... |
| Vista x86 | K6 | rel | PASS | k6-xy.txt (ts.bat) X: 'Windows Vista: platform 2, version 6.0, build 6002, Service Pack 2', 'bitness x86 (32-bit Windows... |
| Vista x86 | SB1 | rel | PASS | read BEFORE any value changed: XHCISNAP -verbosity 3 (sb1-verb), dc/ec (sb1-de); sb1.txt: susp.switch.value=FFFFFFFF sus... |
| Vista x86 | S1 | rel | PASS | sb1.txt taken after 2 min 10 s idle with stick port 1 + USB mouse port 2 attached: only susp.switch.value FFFFFFFF / sus... |
| Vista x86 | P1 | rel | PASS | setv 0000 XhciInterruptMode 2 + dc/ec (p1-de.txt); stick fc clean (p1-stick.txt), mouse moved (mp1 x 398-510 y 298-386);... |
| Vista x86 | P7 | rel | PASS | budget at start: 16 qemu guests, 23/32 vCPUs. mode 2 (2 2 2 8), usb-audio a1 root port 3 (12 Mb/s), TONE.WAV via WMPlaye... |
| Vista x86 | P2 | rel | PASS | mode 1 (setv + dc/ec, p2m1-de.txt): p2m1.txt IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5 'forced line', watche... |
| Vista x86 | G1 | rel | PASS | g1-usbif.txt (USBIF list): 5 GUID_DEVINTERFACE_USB_DEVICE: tablet 4.1, mouse 2, stick 1, XHCI98\COMPOSITE&VID_46F4&PID_0... |
| Vista x86 | G2 | rel | PASS | g1-usbif.txt hubs: root port 1 vid 46f4 _EX speed 2 / V2 proto 4 flags 3 (SuperSpeed operating), HS mouse speed 2 proto ... |
| Vista x86 | K4 | rel | PASS | L1 set: stick port 1 (Super), mouse port 2, usb-audio port 3, hub port 4 + tablet 4.1. L2 UV32: stick QEMU / QEMU USB HA... |
| Vista x86 | T8 | rel | PASS | (after the throttling fix; earlier t8/t8b void) usb-audio a1 port 3, BOT stick s3 port 1 (stickbig, 5000), USB mouse m1 ... |
| Vista x86 | T1 (BOT part) | rel | PASS (UAS part below) | stickbig BOT s3 port 1 SuperSpeed: 3 round trips of BIG.BIN (32 MB) in t8c + 2 in t8b + 1 in t8: every fc 'no difference... |
| Vista x86 | K2 | rel | PASS | see (K2 part 1) above: one XHCI98\COMPOSITE&VID_46F4&PID_0002 parent on port 3 + MI_00 Audio Device beneath (k2-ls.txt),... |
| Vista x86 | T1 | rel | PASS | UAS u1 (usb-uas on stickbig, port 3, 5000) bound xhciuas after the unsigned box (u.bat; x64 also the PCA box closed), dr... |
| Vista x86 | M2 (MSI-X launch) | rel | PASS | mode 0 (m2x0.txt): intr.delivery 5 then 4, IntrDelivery 4 IntrReason 10, 'delivery in effect MSI-X', 'messages connected... |
| Vista x86 | T2 (64) | rel | PASS | m2x0.txt: xfer.max.value 0x40, xfer.max.why 1, xfer.max.bytes 0x10000; 'XhciMaxTransferKB used as set (64)', 'command si... |
| Vista x86 | M3 (MSI-X) | rel | PASS (devices back only after the restart the INF asks for) | dc/ec x2 under MSI-X (m2x3-de.txt, m3x-de.txt: Code 22 -> started, composite back); remove + rescan (m3x-rr.txt): reinst... |
| Vista x86 | T2 (2048) | rel | PASS | (set 0x800 before the restart; the remove+rescan had reset the key so verbosity/portsw were set again) t2k.txt: xfer.max... |
| Vista x86 | K1 (1) | rel | PASS (part 1; part 2 below) | before: dialog '0 - off' (k1r.png); typed 1 -> 'XhciIntelPortSwitch is now 1 on every xhci98 controller. RESTART WINDOWS... |
| Vista x86 | K1 | rel | PASS | part 1 above (1 after restart); then typed 0 in the elevated dialog -> 'XhciIntelPortSwitch is now 0 on every xhci98 con... |
| Vista x86 | T2 (4096) | rel | PASS | m2m0.txt: xfer.max.value 0x1000, xfer.max.why 4, xfer.max.bytes 0x100000; 'XhciMaxTransferKB out of 64-2048: the default... |
| Vista x86 | M2 | rel | PASS | MSI-X launch: see 'M2 (MSI-X launch)' above (delivery 4, 'MSI-X', capability MSI-X; modes 0 and 3 all devices + tone). M... |
| Vista x86 | T2 (0) | rel | PASS | t20.txt: xfer.max.value 0, why 4, bytes 0x100000; 'XhciMaxTransferKB out of 64-2048: the default (0)', 'command size in ... |
| Vista x86 | M3 (MSI) | rel | PASS (devices back after the INF-requested restart) | MSI launch n=3: dc/ec (m2m3-de.txt, m3m-de.txt) back started; pnpctl remove + rescan (m3m-rr.txt): reinstalled, Code 1 u... |
| Vista x86 | P7 (re-read, supersedes nothing - confirms) | rel | PASS | relaunch n=3 MSI, hub+mouse 4.1 also attached: setv mode 2 + dc/ec (p7r-de.txt); TONE twice: p7r-audio.wav span 20.030 s... |
| Vista x86 | CP1 S4 | rel | PASS (attempt 1 failure recorded, attempt 3 below) | attempt 2 PASS as above. Attempt 1 (same device set but the stickbig BOT stick, a TONE played just before, after a long ... |
| Vista x86 | K5 | rel | PASS | after the CP1 resume: stick port 1 fc clean (k5-ls.txt), usb-mouse port 2 moved (mk5m / earlier rows), usb-tablet at hub... |
| Vista x86 | H2 | rel | PASS | (on -Tag r, MSI launch n=8) takeown/icacls/del C:\Windows\System32\drivers\xhcimsi.sys (h2-set.txt, computer name scrubb... |
| Vista x86 | H3 | rel | PASS | (H2's file restored first: copy E:\xhcimsi.sys back, fc clean) + sc delete xhcimsi 'DeleteService SUCCESS' (h3-set.txt);... |
| Vista x86 | A-NT6 C0 | rel | PASS | stick s1 port 1 fc clean, USB mouse m1 port 2 moved (mc0 x 400-512 y 300-388) (c0-ls.txt); c0.txt susp.switch.value FFFF... |
| Vista x86 | A-NT6 C1 | rel | PASS (reading recorded: hidusb sends no idle request on QEMU; the idle path itself is exercised by the test client legs elsewhere) | set: SelectiveSuspendEnabled REG_BINARY 01 in Enum\USB\VID_0627&PID_0001\89126-0000:00:03.0-2\Device Parameters (c1-reg.... |
| Vista x86 | U1 | u1 | PASS | i.bat (pnpctl update E:\xhci98.inf PCI\CC_0C0330) -> unsigned box Install anyway (u1-unsigned.png) -> 'OK REBOOT-NEEDED'... |
| Vista x86 | V1 | rel | PASS | Verifier boot -> desktop (display had blanked; v-boot2.png), no bugcheck; v-q1.txt Level 9BB, xhci98.sys loads 1, xhcims... |
| Vista x86 | CP1 trace (S4) | qemu | PASS (trace as expected; first-image resume failure recorded) | hibernate 1 (debugcon-13.log -> q-sleep-trace.txt): 00030005, parent FDO ...=00000504 twice, 00020005/00020104, quiesce ... |
| Vista x86 | Q2 | qemu | PASS | q2-fire.txt; q2.txt qemu.inj.fire=01050000, ctrl.hch 1, ctrl.failed.here 1 (the injected HCH), ctrl.recover.begin 1, ctr... |
| Vista x86 | Q4 | qemu | PASS | UAS raw disk (uasraw port 3, 256 MB) bound xhciuas (unsigned box); XFERTEST WRITE 1M x128 MB + VERIFY 0 bad on PhysicalD... |
| Vista x86 | Q3 (code 14 timeout) | qemu | FAIL -> 36.54 (UAS stream fault recovery); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | XhciQemuInject 02140083 beside VERIFY 1M x128 MB -reps 40 on PhysicalDrive2: qemu.inj.bulk.armed 00140207, lost 7F0D2F60... |
| Vista x86 | Q3 (code 15 stop) | qemu | PASS | (UAS device_del + re-add after the code 14 wedge) XhciQemuInject 03150083 beside VERIFY 1M x128 MB -reps 40 on PhysicalD... |
| Vista x86 | Q3 (code 16 error) | qemu | PASS | (relaunch n=17; first try garbled by a still-running background typing job of the previous QEMU - cancelled, no write is... |
| Vista x86 | Q3 (code 17 short) | qemu | FAIL -> 36.54 (UAS stream fault recovery); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | XhciQemuInject 06170083: qemu.inj.bulk.armed 00170107, qemu.inj.bulk 17050107 (stream 5), done 17 (q3-17.txt); then time... |
| Vista x86 | P3 | qemu | PASS | (UAS device_del'd first) setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc/ec (p3-set3.txt; the enable took minutes, ba... |
| Vista x86 | P4 | qemu | PASS | MSISupported 0 in the controller's hardware key + XhciInterruptMode 0 (DeadLine 1 kept), shutdown /r (p4-set.txt); p4.tx... |
| Vista x64 | I1 | rel | PASS | elevated cmd (alt-i), pnputil -i -a E:\xhci98.inf -> unsigned box Install anyway -> oem3.inf (i1-unsigned/i1-done.png), ... |
| Vista x64 | I2 | rel | PASS (INF does not write WideMap 0 explicitly; default off holds) | key 0000 (i2-rk.txt, before any change): IntelPortSwitch 0, IntelIrqRoute 1, MaxTransferKB 0x400, InterruptMode 0, PollI... |
| Vista x64 | I3 | rel | PASS | -verbosity 2 (0000 0->2, i3-verb.txt), dc Code 22 / ec started (i3-de.txt); i3.txt intr.mode 0, intr.delivery 5 then 3 (... |
| Vista x64 | I4 | rel | PASS | package-rel.txt: amd64 xhci98.sys fe1cf7b3.. (zip release-x64) + XHCISNAP 79fe8faa.. (zip, x86 tool under WOW64); i3.txt... |
| Vista x64 | K6 | rel | PASS | k6-xy.txt (ts.bat) X: 'Windows Vista: platform 2, version 6.0, build 6002, Service Pack 2', 'bitness 64-bit Windows (x64... |
| Vista x64 | SB1 | rel | PASS | read BEFORE any value changed: XHCISNAP -verbosity 3 (sb1-verb), dc/ec (sb1-de); sb1.txt: susp.switch.value=FFFFFFFF sus... |
| Vista x64 | S1 | rel | PASS | sb1.txt taken after 2 min 10 s idle with stick port 1 + USB mouse port 2 attached: only susp.switch.value FFFFFFFF / sus... |
| Vista x64 | P1 | rel | PASS | setv mode 2 + dc/ec (p1-de.txt); stick fc clean (p1-stick.txt), mouse moved (mp1); after 1 min idle p1.txt: IntrModeRead... |
| Vista x64 | M1/F1 (+P2 mode 1) | rel | PASS | setv 0000 XhciInterruptMode 1 + dc/ec (p2m1-de.txt); p2m1.txt IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5 (for... |
| Vista x64 | M1/F4 (+P2 mode 0) | rel | PASS | setv mode 0 + dc/ec (p2m0-de.txt); p2m0.txt intr.delivery 5 then 3 (MSI) reason 0A, IntrDelivery 3 IntrReason 10, MsiRou... |
| Vista x64 | M1/F0,F1r | rel | PASS | F0 = this overlay's install from the zip (I1: pnputil, F8 DSE, mode 0 default MSI delivery 3, 108 = 4); the assignment p... |
| Vista x64 | P2 | rel | PASS | mode 1 = 1 1 1 5 (p2m1.txt), mode 0 = 0 0 3 10 MSI by design (p2m0.txt); mouse + stick fc each |
| Vista x64 | G1 | rel | PASS | g1-usbif.txt (USBIF list): 5 GUID_DEVINTERFACE_USB_DEVICE: tablet 4.1, mouse 2, stick 1, XHCI98\COMPOSITE&VID_46F4&PID_0... |
| Vista x64 | G2 | rel | PASS | g1-usbif.txt hubs: root port 1 vid 46f4 _EX speed 2 / V2 proto 4 flags 3 (SuperSpeed operating), HS mouse speed 2 proto ... |
| Vista x64 | K4 | rel | PASS | L1 set: stick port 1 (Super), mouse port 2, usb-audio port 3, hub port 4 + tablet 4.1. L2 UV64: stick QEMU / QEMU USB HA... |
| Vista x64 | T8 | rel | PASS | (after the throttling fix; earlier t8/t8b void) usb-audio a1 port 3, BOT stick s3 port 1 (stickbig, 5000), USB mouse m1 ... |
| Vista x64 | T1 (BOT part) | rel | PASS (UAS part below) | stickbig BOT s3 port 1 SuperSpeed: 3 round trips of BIG.BIN (32 MB) in t8c + 2 in t8b + 1 in t8: every fc 'no difference... |
| Vista x64 | K2 | rel | PASS | see (K2 part 1) above: one XHCI98\COMPOSITE&VID_46F4&PID_0002 parent on port 3 + MI_00 Audio Device beneath (k2-ls.txt),... |
| Vista x64 | T1 | rel | PASS | UAS u1 (usb-uas on stickbig, port 3, 5000) bound xhciuas after the unsigned box (u.bat; x64 also the PCA box closed), dr... |
| Vista x64 | M2 (MSI-X launch) | rel | PASS | mode 0 (m2x0.txt): intr.delivery 5 then 4, IntrDelivery 4 IntrReason 10, 'delivery in effect MSI-X', 'messages connected... |
| Vista x64 | T2 (64) | rel | PASS | m2x0.txt: xfer.max.value 0x40, xfer.max.why 1, xfer.max.bytes 0x10000; 'XhciMaxTransferKB used as set (64)', 'command si... |
| Vista x64 | M3 (MSI-X) | rel | PASS | dc/ec x2 under MSI-X (m2x3-de, m3x-de: Code 22 -> started, composite back); pnpctl remove PCI\VEN_1B36&DEV_000D + rescan... |
| Vista x64 | T2 (2048) | rel | PASS | relaunch n=4 MSI (msi=on,msix=off) with 0x800 set before: m2m0.txt xfer.max.value 0x800, 'used as set (2048)', 'command ... |
| Vista x64 | K1 (1) | rel | PASS (part 1) | dialog before '0 - off'; typed 1 -> 'is now 1 ... RESTART WINDOWS', rc 0 (k1-set1.txt, again k1-set1b.txt after the rein... |
| Vista x64 | M2 (MSI launch) | rel | PASS | mode 0 (m2m0.txt): delivery 5 then 3, 'delivery in effect MSI', MsiRoutines 1, CompositeParents 1; BOT F: + UAS G: BIG.B... |
| Vista x64 | K1 | rel | PASS | part 1 above (1 after restart); then typed 0 in the elevated dialog -> 'XhciIntelPortSwitch is now 0 on every xhci98 con... |
| Vista x64 | T2 (4096) | rel | PASS | t24k.txt: xfer.max.value 0x1000, xfer.max.why 4, xfer.max.bytes 0x100000; 'XhciMaxTransferKB out of 64-2048: the default... |
| Vista x64 | T2 (0) | rel | PASS | t20.txt: xfer.max.value 0, why 4, bytes 0x100000; 'XhciMaxTransferKB out of 64-2048: the default (0)', 'command size in ... |
| Vista x64 | M3 (MSI) | rel | PASS | MSI launch n=4: dc/ec x3 (m2m3-de, m3m-de) back started; pnpctl remove + rescan (m3m-rr.txt): all devices started proble... |
| Vista x64 | CP1 S4 | rel | PASS (attempt 1 failure recorded, attempt 3 below) | attempt 2 PASS as above. Attempt 1 (same device set but the stickbig BOT stick, a TONE played just before, after a long ... |
| Vista x64 | K5 | rel | PASS | after the CP1 resume: stick port 1 fc clean (k5-ls.txt), usb-mouse port 2 moved (mk5m / earlier rows), usb-tablet at hub... |
| Vista x64 | V1 | rel | PASS | Verifier boot (F8 DSE) -> desktop, no bugcheck (v-boot.png); v-q1.txt: Level 9BB, xhci98.sys loads 1 unloads 0, Allocati... |
| Vista x64 | CP1 trace (S4) | qemu | PASS | devices s1/m1/a1 (usb-audio port 3)/h1 hub port 4 + hm1 4.1 started, fc clean (q-ls.txt); shutdown /h (paused in 24 s); ... |
| Vista x64 | Q2 | qemu | PASS | setv 0000 XhciQemuInject 01050000 (q2-fire.txt); q2.txt qemu.inj.fire=01050000, ctrl.hch=1, ctrl.failed.here=1 (the inje... |
| Vista x64 | Q4 | qemu | PASS | UAS raw disk u1 (uasraw port 3, 256 MB, out\p36-final\disks\vx64-u1.raw) bound xhciuas (unsigned box + PCA); XFERTEST WR... |
| Vista x64 | Q3 (code 14 timeout) | qemu | FAIL SUSPECT-DRIVER (storage stack does not recover from an injected bulk timeout on Vista x64 UAS; recovered only by a controller restart); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | UAS raw disk (PhysicalDrive2 after renumbering), qemu flavour amd64 35845597, XhciWideMap default 0, XhciQemuInject 0214... |
| Vista x64 | Q3 (code 15 stop) | qemu | FAIL -> 36.54 (UAS stream fault recovery); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | (after a guest restart) XhciQemuInject 04150083 beside VERIFY 1M x128 MB -reps 40 on PhysicalDrive2: qemu.inj.bulk.armed... |
| Vista x64 | Q3 (code 16 error) | qemu | FAIL -> 36.54 (UAS stream fault recovery); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | (after a restart) XhciQemuInject 05160083: qemu.inj.bulk.armed 00160207, qemu.inj.bulk 16090207 (stream 9), xfer.error 0... |
| Vista x64 | Q3 (code 17 short) | qemu | FAIL -> 36.54 (UAS stream fault recovery); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | (after a restart) XhciQemuInject 06170083: qemu.inj.bulk.armed 00170207, qemu.inj.bulk 17100207 (stream 16), done 17 (q3... |
| Vista x64 | P3 | qemu | PASS | (UAS u1 device_del'd first: that cleared the Q3 wedge, XFERTEST gone) setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc... |
| Vista x64 | F3 | qemu | PASS | setv XhciQemuDeadLine 0 + XhciMsi86InjNoLine 1 (mode 1 kept), reboot + F8 (f3-set.txt); check-msi86-trace.ps1 on the boo... |
| Vista x64 | P4 | qemu | PASS | on the MSI grant mode 0 never uses the dead line, so (as agent E on w7x64) MSISupported 0 in the controller's hardware k... |
| Vista x64 | Q3 (code 14 timeout) - supersedes the line above per the coordinator's ruling | qemu | 16:32 | FAIL -> 36.54 (UAS stream fault recovery) |
| 7 x86 | I1 | rel | PASS | pnputil -i -a e:\xhci98.inf: unsigned box Install anyway (i1a) -> oem2.inf "Successfully installed the driver on a devic... |
| 7 x86 | I2 | rel | PASS | i1-post.txt key 0000 (reg query): XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x... |
| 7 x86 | I3 | rel | PASS | XHCISNAP -verbosity 3 set before the install restart (i1-pre2.txt); i3-run.txt: intr.mode 0, intr.delivery 5 -> 3 MSI, r... |
| 7 x86 | I4 | rel | PASS | package-rel.txt xhci98.sys 427b9d98.. xhcimsi abb1319c.. XHCISNAP 79fe8faa.. all from the zip; i3-run.txt "xhcisnap 2.2.... |
| 7 x86 | SB1 | rel | PASS | i3-run.txt (verbosity 3): susp.switch.value FFFFFFFF, susp.switch 0, "DNCTRL not read", DnctrlEnables 0x0000, DnctrlRead... |
| 7 x86 | K6 | rel | PASS | k6xy.txt X: "Windows 7: platform 2, version 6.1, build 7601, Service Pack 1", "x86 (32-bit Windows)", SystemManufacturer... |
| 7 x86 | S1 | rel | PASS | s1b.txt after ~2 min 15 s with the USB mouse untouched (s1.txt an earlier read): susp.switch.value FFFFFFFF, susp.switch... |
| 7 x86 | G1 | rel | PASS | g1.txt usbif list: 5 interfaces: tablet 4.1, mouse 2, stick 1, XHCI98\COMPOSITE&VID_46F4&PID_0002 (parent; none on MI_00... |
| 7 x86 | G2 | rel | PASS | g1.txt usbif hubs: port 1 46f4:0001 _EX speed 2, V2 proto 4 flags 3; mouse port 6 speed 2; audio 7 / hub 8 speed 1; V2 n... |
| 7 x86 | K2 | rel | PASS | g1.txt: XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-3 'xHCI98 USB Composite Device' + USB\VID_46F4&PID_0002&MI_00 ... |
| 7 x86 | K4 | rel | PASS | UV32: stick 'QEMU'/'QEMU USB HARDDRIVE'/'1-0000:00:03.0-1' Bus Speed Super (uv-p1); mouse 'QEMU'/'QEMU USB Mouse'/'89126... |
| 7 x86 | T1 | rel | PASS | t1-big.txt: BOT G: (SS port 4) BIG.BIN->C: fc clean, C:->G: fc clean; UAS H: (SS port 3, xhciuas bound via u.bat, Instal... |
| 7 x86 | T8 | rel | PASS | usb-audio a2 on port 4 (-Display none; budget 23/32 vCPUs, 16 guests, host CPU 76%): 6x (UAS H:\BIG.BIN -> C: + fc, C: -... |
| 7 x86 | P1 | rel | superseded (verdict lost to a quoting slip; corrected line follows) | setv XhciInterruptMode 2 + dc/ec OK (p1-set.txt); p1.txt IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, "POLLING... |
| 7 x86 | P7 | rel | PASS | mode 2 (P1 state), usb-audio a2 port 4, -Display none, budget 21/32 vCPUs (14 guests, host CPU 27%): PlaySync 21.05 / 20... |
| 7 x86 | P2 | rel | PASS | mode 1 (p2m1-set/p2m1.txt): IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5 "the line interrupt, forced line", Msi... |
| 7 x86 | M2 (MSI, modes 0 and 3) | rel | PASS | mode 0: p2m0.txt delivery 3 "MSI", reason 10, MsiCapability 3 "capability MSI", MsiMessages 1; mouse moved; BOT fc clean... |
| 7 x86 | M3 (MSI) | rel | PASS (Code 1 needs the documented restart) | dc/ec x6 (P1/P2/M2 legs) devices back each time; pnpctl remove VEN_1B36&DEV_000D + rescan (m3-msi.txt): controller "not ... |
| 7 x86 | P1 (corrected line; the 07:31 P1 line lost its tail to a quoting slip) | rel | PASS | setv XhciInterruptMode 2 + dc/ec OK (p1-set.txt); p1.txt IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, "POLLING... |
| 7 x86 | G3 | rel | PASS | two passes, each value set + dc/ec + XHCISNAP: (a) HS mouse root port 2 only (g3-0..4.txt), (b) + usb-hub port 4 with a ... |
| 7 x86 | K5 | rel | PASS | L1 usb-mouse root port 2 (HS) + fm1 behind usb-hub 4.1 (FS) moved the pointer (i3m, i18am); L2 usb-tablet tb2 at 4.2 "QE... |
| 7 x86 | T3 | rel | PASS | each value by reg add/delete then UAS raw disk re-plugged (device_del + uasraw re-add on port 3) and XFERTEST VERIFY 2M ... |
| 7 x86 | T2 | rel | PASS | each value setv + shutdown /r (x64 F8 DSE), UAS raw disk VERIFY 2M aligned / 2M -skew 512 / 64K, 0 bad each: 2048 (t2-20... |
| 7 x86 | K1 | rel | PASS | elevated cscript //nologo E:\portsw.vbs show: "...\0000  0 - off" (k1a.txt); portsw.vbs 1: "now 1 on every xhci98 contro... |
| 7 x86 | CP1 S4 | rel | PASS (the 0xc0000411 attempts = harness, not driver) | powercfg /a: Hibernate only (VGAPNP blocks S1-S3) (cp1-pre.txt); set: stick port 1, mouse port 2, usb-audio a3 port 3 (c... |
| 7 x86 | D6 | rel | PASS | setv XhciInterruptMode 2 + dc/ec (d6pre.txt: IntrDelivery 2 / reason 8); shutdown /h (paused after 12 s), quit, relaunch... |
| 7 x86 | M2 (MSI-X, modes 0 and 3) | rel | PASS functionally; FINDING (MSI-X lost/late interrupts -> backstop, attribution open) | clean shutdown, relaunch -Xhci p2=4,p3=4,msi=off,msix=on (+UAS raw port 3, audio port 4); msix0: delivery 4 "MSI-X", rea... |
| 7 x86 | M3 (MSI-X) | rel | PASS (Code 1 needs the documented restart) | dc/ec x2 (mode 3 and back) devices back; pnpctl remove + rescan (m3x.txt): problem=1 (x86 INF reboot flag) -> shutdown /... |
| 7 x86 | SB3 (WW step) | rel | PASS (step) | setv XhciSelectiveSuspend 1 + dc/ec (sb3-set.txt; bc no differences); pnpctl update E:\XHCIPM.INF "USB\VID_46F4&PID_0001... |
| 7 x86 | SB3 | rel | PASS | sb3-ww.txt WW 0xC00000BB; PMCTL D2 -> dev D2 (sb3a.txt); sb3a + sb3b (~30 s apart): susp.seq=00102110, SuspSuspendsBegun... |
| 7 x86 | SB4 | rel | PASS (no hang; SuspGone window not hit) | ACTIVE: device_del bs1 -> client devnode gone, controller started (sb4a.txt); SUSPENDED: re-add s1 (client rebinds), PMC... |
| 7 x86 | SB5 | rel | PASS | stick s5 re-added (client bound), PMCTL D2 -> dev D2; dc/ec OK (sb5.txt): controller, root hub back started; the stick r... |
| 7 x86 | SB6 | rel | PASS | setv mode 2 + dc/ec (sb6-set.txt); sb6a/sb6b (~30 s apart): IntrModeRead 2, IntrDelivery 2, IntrReason 8, susp.seq=00102... |
| 7 x86 | SB7 | rel | PASS (as expected; bench-owed) | none of FuncWake*/SsSuspArms/SsSuspHub* printed in sb3c, sb4, sb6c (all 0); QEMU makes no Function Wake and has no Super... |
| 7 x86 | A-NT6 C0 | rel | PASS | fresh -Tag s overlay, rel from the zip (pnputil + restart, bc no differences, s-inst.txt); c0.txt: susp.switch.value FFF... |
| 7 x86 | A-NT6 C1 | rel | PASS (reading recorded: hidusb did not idle - Windows behaviour, not the driver) | setv XhciSelectiveSuspend 1, Enum\USB\VID_0627&PID_0001\89126-0000:00:03.0-2\Device Parameters SelectiveSuspendEnabled R... |
| 7 x86 | T6 | rel | PASS | fresh -Tag m overlay, launch -Mem 512 (XHCISNAP RAM 511 MB), rel from the zip (pnputil + restart, bc no differences, m-i... |
| 7 x86 | U2 (downgrade, part 1) | rel->p202 | PASS (downgrade works on the line; Windows drops the MSI key, keeps the xhcimsi service) | fresh -Tag u overlay: rel from the zip, restart: u2-rel.txt delivery 3 MSI reason 10, Msi86State 3, helper 0x02020003, x... |
| 7 x86 | U2 (recovery ii) | rel->p202 | PASS | u2b-inst.txt rel oem2.inf, bc clean, restart; u2b-uninst.txt: hardware key MessageSignaledInterruptProperties MSISupport... |
| 7 x86 | V1 | rel | PASS | fresh -Tag v overlay: rel from the zip (pnputil, bc no differences xhci98 + xhcimsi), verifier /standard on both (v1-set... |
| 7 x86 | Q2 / M4 | qemu | PASS | fresh -Tag q, qemu flavour db0c4847 (pnputil, restart, bc no differences xhci98 + xhcimsi, q-inst.txt); q0.txt delivery ... |
| 7 x86 | Q3 | qemu | FAIL -> 36.54 (UAS stream fault recovery); **final: FAIL -> 36.54 (pre-existing UAS stream fault recovery gap)** | UAS raw PhysicalDrive2, XFERTEST VERIFY 1M x32 MB -seed 3 -reps 2 per fault, data-in pipe 0x83 (q3y pass): 14 timeout: b... |
| 7 x86 | Q3 (cont.) | qemu | INFO | device_del r1 ended the stuck XFERTEST and its cmd chain returned to the prompt (shot q3del.png; the chain's output was ... |
| 7 x86 | Q4 | qemu | PASS | fresh UAS re-plug r2 after Q3; setv XhciQemuMapGrant 0x10 + dc/ec (q4.txt): qemu.mapgrant 10 applied 10, "map grant 16 r... |
| 7 x86 | CP1 trace (S4) | qemu | PASS | overlay q (MSI): stick 1, mouse 2, usb-audio a3 port 3, usb-hub h3 port 4 + tablet tb3 4.1 (qcp1-pre.txt; the tablet HID... |
| 7 x86 | P3 | qemu | PASS | setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc/ec (p3.txt): intr.mode 1, delivery 1 line, reason 5, qemu.inj.deadlin... |
| 7 x86 | P4 | qemu | PASS | MSISupported 0 in the hardware key (so mode 0 takes the line; with the MSI grant the stand-in dead line is never used, a... |
| 7 x86 | M4 | qemu | PASS | see Q2 / M4 line (q2.txt): HCH recovery 1, delivery 3 MSI after; D3 and back = controller disable/enable (dc/ec) through... |
| 7 x64 | I1 | rel | PASS | pnputil oem2.inf "Successfully installed the driver on a device", unsigned box Install anyway (i1a), then PCA "Windows r... |
| 7 x64 | I2 | rel | PASS | rk1.txt key 0000 (reg query): XhciIntelPortSwitch 0, XhciMaxTransferKB 0x400, XhciInterruptMode 0, XhciPollIdleMs 0x32, ... |
| 7 x64 | I3 | rel | PASS | XHCISNAP -verbosity 3 (v3pre.txt) + dc/ec OK (i3-dcec.txt); i3-run.txt: intr.mode 0, intr.delivery 5 -> 3 MSI, reason 0A... |
| 7 x64 | I4 | rel | PASS | package-rel.txt xhci98.sys fe1cf7b3.. + XHCISNAP 79fe8faa.. both from the zip (2.2.0.3; times normalised 2026-10-10 12:0... |
| 7 x64 | SB1 | rel | PASS | i3-run.txt (verbosity 3): susp.switch.value FFFFFFFF, susp.switch 0, "DNCTRL not read (selective suspend off, or no MMIO... |
| 7 x64 | K6 | rel | PASS | k6xy.txt X: "Windows 7: platform 2, version 6.1, build 7601, Service Pack 1", "64-bit Windows (x64); this x86 tool ran u... |
| 7 x64 | S1 | rel | PASS | s1.txt after ~6 min with the USB mouse untouched: susp.switch.value FFFFFFFF, susp.switch 0, no other susp.* note, no Su... |
| 7 x64 | G1 | rel | PASS | g1.txt usbif list: 5 GUID_DEVINTERFACE_USB_DEVICE: tablet 4.1, mouse 2, stick 1, XHCI98\COMPOSITE&VID_46F4&PID_0002 (par... |
| 7 x64 | G2 | rel | PASS | g1.txt usbif hubs: root port 1 46f4:0001 _EX speed 2, V2 proto 4 flags 3 (SuperSpeed operating + capable); mouse port 6 ... |
| 7 x64 | K2 | rel | PASS | g1.txt: XHCI98\COMPOSITE&VID_46F4&PID_0002\1-0000:00:03.0-3 'xHCI98 USB Composite Device' + USB\VID_46F4&PID_0002&MI_00 ... |
| 7 x64 | K4 | rel | PASS | UV64: stick 'QEMU'/'QEMU USB HARDDRIVE'/'1-0000:00:03.0-1' Bus Speed Super (uv-p1); mouse 'QEMU'/'QEMU USB Mouse'/'89126... |
| 7 x64 | T1 | rel | PASS | t1-big.txt: BOT G: (SS port 4) + UAS H: (SS port 3, xhciuas via u.bat, Install anyway + PCA box closed) BIG.BIN both way... |
| 7 x64 | T8 | rel | PASS | same method (budget 23/32): PlaySync 20.7 s (IsoPacketsAnswered +20110, Ring Underrun +1) during 6x UAS BIG.BIN copies b... |
| 7 x64 | P1 | rel | superseded (verdict lost to a quoting slip; corrected line follows) | setv XhciInterruptMode 2 + dc/ec OK (p1-set.txt); p1.txt IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, "POLLING... |
| 7 x64 | E-3 | rel | PASS | mode 0 (MSI): k2aud-times.txt 20.83 / 20.26 s (+20090/+20100 packets), budget 22/32; mode 2 (P1 state): e3m2-times.txt 2... |
| 7 x64 | P2 | rel | PASS | mode 1 (p2m1-set/p2m1.txt): IntrModeRead 1 IntrMode 1 IntrDelivery 1 IntrReason 5 "the line interrupt, forced line", Msi... |
| 7 x64 | M1/F | rel | PASS | F0 = I1 (mode 0 default: delivery 3 MSI, 108 = 4, i3-run.txt); F1 p2m1.txt: 65 1, 66 5, 108 10 (MsiFilterOutcome 10 repl... |
| 7 x64 | M2 (MSI, modes 0 and 3) | rel | PASS | mode 0: p2m0.txt delivery 3 "MSI", reason 10, MsiCapability 3 "capability MSI", MsiMessages 1; mouse moved; BOT fc clean... |
| 7 x64 | M3 | rel | PASS | dc/ec x6 devices back; pnpctl remove + rescan (m3-msi.txt): controller started problem=0 at once (x64 asks no restart), ... |
| 7 x64 | P1 (corrected line; the 07:31 P1 line lost its tail to a quoting slip) | rel | PASS | setv XhciInterruptMode 2 + dc/ec OK (p1-set.txt); p1.txt IntrModeRead 2 IntrMode 2 IntrDelivery 2 IntrReason 8, POLLING,... |
| 7 x64 | G3 | rel | PASS | two passes, each value set + dc/ec + XHCISNAP: (a) HS mouse root port 2 only (g3-0..4.txt), (b) + usb-hub port 4 with a ... |
| 7 x64 | K5 | rel | PASS | L1 usb-mouse root port 2 (HS) + fm1 behind usb-hub 4.1 (FS) moved the pointer (i3m, i18am); L2 usb-tablet tb2 at 4.2 "QE... |
| 7 x64 | I18 | rel | PASS | usb-hub h2 hot-added on port 4 at the desktop, usb-mouse fm1 (12 Mb/s) at 4.1: USB\VID_0627&PID_0001\...-4.1 + HID mouse... |
| 7 x64 | B3 | rel | PASS | default (absent = 0): t1x.txt "XhciWideMap not set (the default)", promotion off, check not run, promoted 0, big blocks ... |
| 7 x64 | T2 | rel | PASS | each value setv + shutdown /r (x64 F8 DSE), UAS raw disk VERIFY 2M aligned / 2M -skew 512 / 64K, 0 bad each: 2048 (t2-20... |
| 7 x64 | K1 | rel | PASS | elevated cscript //nologo E:\portsw.vbs show: "...\0000  0 - off" (k1a.txt); portsw.vbs 1: "now 1 on every xhci98 contro... |
| 7 x64 | B1 | rel | PASS | MaxTransferKB 2048 by reboot, XhciWideMap 1 still set from B3 (promotion on): t2-2048.txt VERIFY 2M aligned 160.3 MB/s a... |
| 7 x64 | B2 | rel | PASS | see T2 (t2-64/-2048/-4096/-0.txt): 64 -> command size 65536; 2048 -> 2097152; 4096 -> "out of 64-2048: the default (4096... |
| 7 x64 | CP1 S4 | rel | PASS (the 0xc0000411 attempts = harness, not driver) | powercfg /a: Hibernate only (VGAPNP blocks S1-S3) (cp1-pre.txt); set: stick port 1, mouse port 2, usb-audio a3 port 3 (c... |
| 7 x64 | SB2 | rel | PASS | setv XhciSelectiveSuspend 1 + dc/ec (sb2.txt, verbosity 3): susp.switch.value 1, susp.switch 1, DNCTRL 0x0002 (N1, FUNCT... |
| 7 x64 | I4 (stage A word) | rel | PASS | sb2.txt: SuspSwitchRead 1 and SuspSwitch 1 printed by name = susp.switch.value 1 / susp.switch 1; the value deleted afte... |
| 7 x64 | B13 | rel | PASS (drain length not exposed by the release XHCISNAP) | set: usb-mouse port 1, UAS raw ua port 2 (u1) + ub port 3 (u2), usb-audio a4 port 4 (budget 17/32); both disks WRITE 128... |
| 7 x64 | M2 (MSI-X, modes 0 and 3) | rel | PASS functionally; FINDING (MSI-X lost/late interrupts -> backstop, attribution open) | clean shutdown, relaunch -Xhci p2=4,p3=4,msi=off,msix=on (+UAS raw port 3, audio port 4); msix0: delivery 4 MSI-X, reaso... |
| 7 x64 | M3 (MSI-X) | rel | PASS | pnpctl remove + rescan: started problem=0 at once (m3x.txt); driver key recreated -> -verbosity 3 + dc/ec (m3x-after.txt... |
| 7 x64 | MSIX-ATTR (1) new? | base | INFO (pre-existing) | msi=off,msix=on, stick port 1, usb-audio port 4, pnputil install (F8), verbosity 2 + dc/ec (b-inst.txt); 3 x TONE.WAV 21... |
| 7 x64 | MSIX-ATTR (2) QEMU side | qemu | ATTRIBUTED: QEMU (MSI-X emulation), exposed by the driver's spec-legal IE toggling; pre-existing | qtrace-msix.log: every interrupt service is IMAN(off 0x20) write 0x00000001 (W1C IP, IE=0) -> usb_xhci_irq_msix_unuse nr... |
| 7 x64 | Q2 / M4 | qemu | PASS | fresh -Tag q overlay, qemu flavour 35845597 (pnputil, F8; bc no differences, q-inst.txt), msi=on,msix=off relaunch: q0.t... |
| 7 x64 | E-1 | qemu | PASS (stop-dump counter notes not read: DebugView not run) | setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc/ec (e1-set.txt); e1.txt: intr.mode 1, delivery 1 (line), reason 5, qe... |
| 7 x64 | P3 | qemu | PASS (stop-dump counter notes not read: DebugView not run) | setv XhciInterruptMode 1 + XhciQemuDeadLine 1 + dc/ec (e1-set.txt); e1.txt: intr.mode 1, delivery 1 (line), reason 5, qe... |
| 7 x64 | E-2 | qemu | PASS | on the MSI grant mode 0 delivers MSI and the stand-in dead LINE is never used (e2.txt/e2b.txt: delivery 3 MSI, no move, ... |
| 7 x64 | P4 | qemu | PASS | on the MSI grant mode 0 delivers MSI and the stand-in dead LINE is never used (e2.txt/e2b.txt: delivery 3 MSI, no move, ... |
| 7 x64 | F3 | qemu | PASS | MSISupported 1, XhciInterruptMode 1, XhciMsi86InjNoLine 1, reboot (F8): f3-trace.txt (check-msi86-trace.ps1 on f3-debugc... |
| 7 x64 | B8 | qemu | NOT MAKEABLE (QEMU usb-uas assertion; harness/QEMU, not the driver) | setv XhciWideMap 1 + XhciQemuBigRefuse 1 + dc/ec, XFERTEST VERIFY on the UAS raw disk: QEMU ABORTED (qemu-stderr-9.log: ... |
| 7 x64 | B9 (a) | qemu | NOT MAKEABLE (QEMU usb-uas) | needs two TDs of one URB on a UAS stream ring = the B8 configuration, which aborts QEMU's usb-uas (data3 assertion) befo... |
| 7 x64 | Q3 | qemu | PASS | XhciWideMap 1 (BigRefuse 0: stage-3 big blocks built 3, one-TD URBs 1540, 256-TRB TDs), UAS raw disk (PhysicalDrive2), X... |
| 7 x64 | B9 (b) | qemu | PASS | XhciWideMap 1 (BigRefuse 0: stage-3 big blocks built 3, one-TD URBs 1540, 256-TRB TDs), UAS raw disk (PhysicalDrive2), X... |
| 7 x64 | Q4 | qemu | PASS | setv XhciQemuMapGrant 0x10 (16 registers) + dc/ec (q4.txt): qemu.mapgrant 10, applied 10, "map grant 16 registers; a chu... |
| 7 x64 | B10 | qemu | NOT MAKEABLE (no storage-class suspend requester in QEMU/Windows 7) | not makeable on this guest: nothing in Windows 7 selectively suspends a UAS / BOT storage device (usbstor and xhciuas ne... |
| 7 x64 | B12 | qemu | NOT MAKEABLE (QEMU usb-uas; no pause-with-queue in QEMU) | needs a pause with a mix of an older single-TD URB and a promoted two-TD URB queued on one bulk pipe: promotion (stage 2... |
| 7 x64 | CP1 trace (S4) | qemu | PASS | Dx: 00000504 per parent FDO on hibernate, 00000101 on wake; resume finishes; function + hub devices work / qemu flavour ... |
| 7 x64 | V1 | rel | PASS | fresh -Tag v overlay, rel from the zip (pnputil, F8, bc no differences; v1-set.txt "No drivers are currently verified" t... |
| 7 x64 | B5 | rel | PASS (promotion refused at the check; handed 0 - the check gate, not the per-map guard) | V1 overlay (verifier /standard Level 000009BB incl. DMA checking): setv XhciWideMap 1 + dc/ec, UAS raw disk port 3: b5.t... |
| 7 x64 | D1 | dbg | PASS | fresh -Tag d overlay, package-dbg.txt xhci98.sys 38269385.. (= hashes.txt debug-x64); pnputil (F8; the first try hit "Do... |
| XHCIQUAL (DOS) | QM | zip XHCIQUAL.EXE | PASS | QEMU matrix: 50 passed, 0 failed (matrix.txt; exit 0; XHCIQUAL.EXE SHA-256 d5ab9fd6079b0157031998c69cbadb6fbf7e3a5c3cc05... |
| XHCIQUAL (DOS) | QM harness note | - | PASS | no junction made (would replace a tracked dir); main repo tools\w98se.img (92d9b862...) copied to legs\xhciqual\w98se.im... |
| 98 SE 36.41 | W41 binary sw0 | rel (zip) | PASS | AUTOEXEC.BAT copies D:\XHCI98.SYS to C:\WINDOWS\SYSTEM32\DRIVERS before Windows loads (shots vfy0-ae2); file Properties ... |
| 98 SE 36.41 | W41 binary sw1 | rel (zip) | PASS | Properties 269,400 bytes, version 2.2.0.3 (vfy1-prop, vfy1-ver); offline extract SHA-256 427b9d98...41df; SYSTEM.DAT hol... |
| 98 SE 36.41 | W41 vehicle | rel (zip) | PASS (vehicle rebuilt) | on 2.2.0.3 the hot-add raised Add New Hardware for "QEMU USB Audio" = 36.43's XHCI98\COMPOSITE parent, which the stage A... |
| 98 SE 36.41 | W41 120 s switch 0 | rel (zip) | PASS (Windows 98 SE wedge, as 36.41) | 3 of 6 wedged (f0a120-1, -2, -6: frame frozen l2a->l3b diff 0, menu 0/0/0, clock 0); live f0a120-3, -4, -5 (Start menu e... |
| 98 SE 36.41 | W41 120 s switch 1 | rel (zip) | PASS (Windows 98 SE wedge, as 36.41) | 2 of 6 wedged (f1a120-3, -5); live f1a120-1, -2, -4, -6 (-6 judge MIXED: menu x3, tray clock 7:30 unchanged through l3-b... |
| 98 SE 36.41 | W41 300 s switch 0 | rel (zip) | PASS (as 36.41) | 2 of 2 wedged (f0a300-1, -2) |
| 98 SE 36.41 | W41 300 s switch 1 | rel (zip) | PASS (as 36.41) | 2 of 2 wedged (f1a300-1, -2) |
| 98 SE 36.41 | W41 wedge state | rel (zip) | PASS - no SUSPECT-DRIVER | every wedge: desktop frozen, no Start menu, clock frozen, no blue screen in any shot; 12 register samples each in VMM co... |

## 36.55 - the lost-interrupt backstop starved on Windows 98 SE (2026-10-11)

**The finding.** The final matrix's E-4 leg stopped on 98 SE, 2 of 2:
the `qemu` flavour of the stamped `2.2.0.3`, `XhciInterruptMode` 1 with
`XhciQemuDeadLine` 1, and an MS-DOS prompt copy (`F.BAT`) to the stick's
E:. The stick's block counters were unchanged 60 s apart and the UI was
frozen. ME's same leg ran at backstop pace (P3-x). The `XhciQemuInject`
`01160083` left in that key from the Q3 row named endpoint `0x83`.
Neither QEMU's Bulk-Only stick (`0x81`/`0x02`) nor its mouse has it, and
the dumps show it latched and never fired. The matrix trace's last
transfer line, `0x1F`, is that trace site's 32-line budget, not where the
transfers stopped.

**Traced** with a temporary patch, never committed. The tolerance clock's
DPC printed a heartbeat every 2 s: the clock, the controller thread's
loop count and position, the power gate's holders and waiters, the drain
count, backstop drains, transfers submitted and completed, URBs in, and
whether an event stood at the dequeue. Development host A, QEMU, fresh
overlays of the 98 SE gold image, `-display none` with the power-throttle
opt-out, `XhciQemuInject` absent (`qemu.inj.latched` `FFFFFFFF`).
98 SE presents a stick attached at power-on twice: as E:, the BIOS's
INT 13h disk in MS-DOS compatibility mode, and as G:, the protected-mode
drive. Both are the same volume.

| Run | Build | Setup | What happened | Reading |
|---|---|---|---|---|
| A | `4973770` + trace | mode 1, dead line, no mouse, `F E` | about 2 transfers/s for 20 min (2883), then the thread's loop count froze inside its 100 ms timed wait. The DPC heartbeat went on, an event stood at the dequeue, completed = submitted - 1, the gate was free. 60 EIPs, all in the VMM's ring 0 (`C0003xxx`, `C00534xx`). IRQ0 at about 240/s, no interrupt storm | stop |
| B | the same | mode 1, dead line, a HID mouse installed first, `F E` | stopped within 2 min (174 transfers), the thread frozen mid-pass between two steps (runnable code, not a wait), the rest as A | stop |
| C | the same | mode 1, dead line, `F G` | copy and `fc` x2 clean in about 32 min, 3661 transfers; the thread ran throughout. G: wrote 32 KB a request, E: 4 KB | no stop |
| D | the same | mode 0, a working line, 254 lost interrupts injected (`010100FE`), `F E` | 117 lost interrupts picked up by the thread, then the thread froze mid-pass as in B, and the copy stopped | stop |
| E | the same | as D on G: (`020100FE`), then a Win32 `XCOPY32` to E: (`030100FE`) | both clean, all 254 lost interrupts picked up each time | no stop |
| F | + the step from the DPC (experiment) | as B | copy and `fc` x2 clean, 6322 transfers, every drain queued by the DPC; the mouse moves after | pass |

So on 98 SE the controller thread can be left unscheduled while an MS-DOS
prompt's request to a drive in MS-DOS compatibility mode waits. The timer
DPC is not, and it keeps running. Which part of the VMM holds the thread
off was not identified. The backstop ran only on the thread. On a delivery
where it is the only drain, the request therefore waited for a completion
nothing would queue, and the computer stopped. This is the dead line under
mode 1, and also a single lost interrupt in the default mode 0. Every
release since `2.2.0.0` (`v2.2.0.0`'s `hcdPoll` calls `hcdBackstop`) has
the exposure. The `824a6c7` reread's mode 1 copy passed because it went
to that run's protected-mode letter G: (`out\p36-pollv\legs\w98\notes-E2.txt`).

**The fix** (design record 17 section 4.1, "Amended"; records 15 and 13
follow): the step runs first in the tolerance clock's DPC on every tick,
before its re-arm, with no power gate. It is the same admission,
observation and queueing as before. The release closes the event DPC with
an interlocked exchange. Commits `7debbe0` (design), `2ad2b0d` (code),
`5d96e69` (review round 1's one wording point), `b7c2b55` (mode 0 and the
release notes), merged with `2.3.0.0` at `e9d25b9` in `2c74b47`. Review
thread `01a129d5`: round 1, no P1 or P2 and one wording P3; round 2, the
confirmation, nothing; round 3, the merge, nothing. Built x86 and `-amd64`
all, every gate and the host suites, on `2ad2b0d`/`5d96e69` and again on
the merge.

**Legs on the fix**, the `qemu` and `release` packages of `5d96e69`
(`xhci98.sys` x86 `qemu` `daaed75f...`, `release` `b9318092...`):

| Guest | Leg | Reading | Verdict |
|---|---|---|---|
| 98 SE | E-4 run 1: mode 1, dead line, `F E`, no mouse | copy and `fc` x2 clean in about 56 min. `XHCISNAP`: intr.mode 1, deadline 1, latched `FFFFFFFF`, TransfersCompleted 6322 = BackstopDrains 6322 | PASS |
| 98 SE | E-4 run 2: the same after a HID mouse is hot-added and its driver installed (the case that stopped within 2 min) | copy and `fc` x2 clean in about 53 min; TransfersCompleted 6326, BackstopDrains 6327; the mouse moves before and after | PASS |
| 98 SE | mode 0, a working line, 254 lost interrupts during `F E` | copy and `fc` x2 clean in about 2.5 min; BackstopDrains 254 | PASS |
| 98 SE | the same during `F G` | clean; BackstopDrains 508 | PASS |
| 2000 | mode 1, dead line, the stick at boot: 4 MB copy and `fc` x2, then a hot-added mouse | clean in 6 min 14 s; transfers 359 to 1184, BackstopDrains 360 to 1170; the mouse moves; no `cmd.timeout`, recovery window 0 of 3 | PASS |
| 2000 SMP, Driver Verifier `0x1B` | the same, both vCPUs on separate host threads | clean in 7 min 05 s; transfers 350 to 1307, drains 351 to 1234; the mouse moves; `verifier /query` AllocationsFailed 0, no stop | PASS |
| XP | the same | clean in 10 min 30 s; transfers 547 to 2158, drains 547 to 2124; the mouse moves | PASS |
| ME | P3: mode 1, dead line, `F E` | copy and `fc` x2 clean (16:03 to 16:54); transfers 187 to 6349, BackstopDrains 188 to 6350, about 2 a second; the stick's block counters rose every minute; a hot-added mouse moves and clicks | PASS |
| 98 SE | `release`, mode 0 | delivery 1 (the line); `F.BAT` clean on E: and on G:; the mouse moves and clicks; BackstopDrains 0 | PASS |
| 98 SE | `release`, mode 2 | delivery 2, reason 8; `F.BAT` clean on E: and G:; the mouse moves and clicks; BackstopDrains 0 | PASS |
| 7 x64 | `release`, modes 0 and 2 | mode 0: MSI-X (delivery 4), mode 2: polling (delivery 2, reason 8); stick `fc` x2 clean and the mouse moves in both; BackstopDrains 0 | PASS |
| 98 SE | `release`, mode 0, a stick attached at power-on: Stand by and wake (36.28 part 2's stop) | after `system_wakeup` the screen kept the Shut Down dialog and took no input for 2+ minutes; EIPs in the VMM's ring 0 | unchanged: 36.28 part 2's limitation stands; this fix does not reach it |
| 98 SE | the merge with `2.3.0.0` (`2c74b47`, `qemu`, `xhci98.sys` `a30ee7f3...`): E-4 run 1 again | copy and `fc` x2 clean (17:00 to 17:53); the stick's block counters rose every half minute throughout | PASS |

Raw texts: `out\p36-dl98\legs\notes-dl98.txt` and each guest's
`out\p36-dl98\legs\<guest>\notes.txt`, with the debugcon logs, register
samples and screenshots they name.

## 36.56 - `portsw-on.bat` and `portsw-off.bat`, and the re-cut's binaries (2026-10-11)

**The task.** At the E7250 bench a USB 3 stick copied at USB 2.0 speed
with the switchover at its default 0. The owner asked for two batch files
beside `portsw.vbs`, in the current `2.3.0.0` cut, with the driver
binaries unchanged. Design record 16 revision 5, section 5a "The batch
files". Branch `p36-pswbat`: `e67f778` (design), `c5e1e29` (the files and
the packager), `ae65c26` and `9bb2c5b` (review rounds 1 and 2), `837bc17`
(build-and-test), `bb80117` (two text points from the legs). Review thread
`01a12ab5`: round 1, two P2 (a partial write reported as "nothing was
set"; a bare `-PswScriptPath`) and two P3 (escapes in the first,
blacklist-style syntax check; the symptom's wording); round 2, one P2 (a
root-relative `-PswScriptPath`) and three P3 (a BOM or UTF-16 file hidden
by the text read; mixed-case `echo ON`; the package count); round 3,
nothing; round 4, on `bb80117`, nothing.

**The binaries are the first cut's, restored.** The tree was rebuilt (x86
and `-amd64` all, every gate) with no driver source change, and all 15
`.sys` came out the same size but different bytes from
`out\p36-stamp3\hashes.txt`: the PE link timestamp, the PE checksum and
the PDB path, which names the worktree. `make-release.ps1` does not
rebuild; it publishes the build directories. So the first cut's bytes
were restored into this worktree's build directories
(`out\p36-pswbat\restore-stamp3.ps1`, log `restore.log`), with the
orchestrator's approval:

- x86 `release` and `debug` from the cut's `release-x86\` and `debug-x86\`
  (unsigned); `qemu` from `out\p36-stamp3\pkg-qemu-*`;
- amd64 `release` from the unsigned `out\p36-stamp3\pkg-release-amd64`;
- amd64 `debug` from the cut's signed `debug-x64\`, the signature removed:
  the file cut to its unsigned length, the certificate-table entry zeroed
  and the PE checksum recomputed. That gave `C232A155...` (`xhci98.sys`)
  and `D958469D...` (`xhciuas.sys`), the hashes in `hashes.txt`;
- `XHCISNAP.EXE` and `XHCIQUAL.EXE` (with its `.MAP`) from the cut's tree.

All 17 files were checked against `hashes.txt` before anything was
written, and all matched. `git diff f09c6aa..HEAD -- src xhcisnap xhciqual`
(`f09c6aa` built the cut) shows only the two `.bat` files. On that basis
the source stamps were rewritten beside the restored binaries
(`source-stamp.ps1 -Write`, then `-Check` exit 0). The tools' mtimes were
set to the time of the restore, their bytes unchanged. Both steps only
satisfy the gates' "built from these sources" checks.

The unsigned packages staged from them (`out\p36-pswbat\pkg-release-*`)
hold every file of `out\p36-stamp3\pkg-release-*` byte for byte, plus the
two batch files. An unsigned x86 `make-release.ps1` dry run published
`.sys`, `XHCISNAP` and `XHCIQUAL` equal to `hashes.txt`.

**Legs** (development host A, the package above, fresh overlays):

| Guest | Run | Reading | Verdict |
|---|---|---|---|
| 98 SE | double-click in Explorer, typed at `D:\`, the 8.3 name, from `C:\` | 1 then 0 read back; from `C:\` "portsw.vbs was not found", nothing changed; `if errorlevel 2` after an unknown argument: `COMMAND.COM` sees `cscript`'s exit code | PASS (8/8) |
| ME | double-click, after an Add New Hardware install | 1 then 0; the exit code seen as on 98 SE | PASS (6/6) |
| 2000 | double-click, typed | 1 then 0; the output was 26 lines, so the header's first line had scrolled off a 25-line console at the pause | PASS (4/4) |
| XP | double-click, after the `i.bat` install | 1 then 0; the same 26 lines | PASS (4/4) |
| 7 x64 | Run as administrator; not elevated; the prompt declined | the elevated run started in `System32` and found the script; not elevated, the script raised the prompt and its message box showed the result; declined, nothing changed, but the batch said the result was "shown in the window that opened" | PASS |
| 98 SE, 2000, 7 x64 | `bb80117`: three-line header, the declined case's text | every window whole at the pause; the declined case says nothing changed | PASS |

On every target a success prints two restart lines, the script's and the
batch file's. The refusal text (exit code 1) cannot be reached as an
administrator on these guests, and is held by the self-tests alone. Raw
notes and shots: `out\p36-pswbat\legs\<guest>\`.

## Re-read on the stamped 2.3.0.0 (2026-10-11)

The post-rebuild re-read that the final matrix left owed ("Owed to the post-rebuild re-read" above), on the first
stamped `2.3.0.0` cut. Development host A, QEMU 11.1.0 (TCG), 2026-10-11 18:32 to 22:53. Coordinator branch
`p36-reread`. Checklist, kit and evidence: `out\p36-reread\` (`checklist.md` with rows R1 to R6, `brief-legs.txt`,
`kit\`, per-guest `legs\<guest>\notes.txt` with every RESULT line and the files it names, and `suspect.txt`).

**What these readings cover.** The first stamped cut, `DriverVer` 10/11/2026: branch `p36-stamp3` at `c1097f4`,
binaries built at `f09c6aa`, the unsigned packages `out\p36-stamp3\pkg-{release,qemu}-{x86,amd64}`, hashes
`out\p36-stamp3\hashes.txt`.
- `release` x86: `xhci98.sys` `64c6e8fd...`, `xhcimsi.sys` `a36a5007...`, `xhciuas.sys` `65439153...`.
- `release` amd64: `xhci98.sys` `10400ec5...`, `xhciuas.sys` `7579e2ed...`.
- `qemu` (fault injection and traces only): x86 `681fb72d...`, amd64 `e55a8bc5...`.
- `XHCISNAP.EXE` `25a0342d...`, the same build.
- The owner's signed zip of that cut has the same x86 binaries; its x64 binaries are these plus the test signature.
  The packages here are unsigned, so Vista x64 and 7 x64 ran with F8's signature enforcement disabled at every boot.
- They do not cover the re-cut. That is restamped `DriverVer` 10/12/2026 and rebuilt with 36.57 (the Play 2
  async-audio fix: a first-TD Frame ID without CFC, then SIA; issue 14). Its own VM legs are `p36-play2`'s: smoke,
  HID, Bulk-Only, UAS, usb-audio and the isochronous notes on 98 SE, ME, 2000, XP, 7 x86 and 7 x64.

**The harness.**
- The kit is a copy of the final matrix's: locks `<guest>-rr`, QEMU name `p36rr`, overlays `vm\t36rr`.
  - Flavours `rel` and `qemu` from the packages above, `prev` the published `2.2.0.0`.
  - `q203` is the stamped `2.2.0.3` `qemu` build, the build before 36.54, used for one control.
- Every guest ran with `-display none` and the power-throttle opt-out. `-rtc` was the matrix's:
  `base=2026-09-20T12:00:00` on Windows 7, `localtime` elsewhere, 98 SE and ME included.
- Eight leg agents ran under the global admission lock, at most about 22 vCPUs in all; other workers' guests
  shared the host.
- The 2000 SMP warm-restart timing ran last, with the host at 4 vCPUs in all.
- `XhciSelectiveSuspend` is no longer read as absent: since 36.51 every INF path writes all 19 values.

**Summary per guest** (rows = RESULT lines, the attribution and control rows included; "other" = INFO, RECORDED or
QEMU-defect rows):

| Guest | Time (2026-10-11) | Rows | PASS | FAIL | NOT MAKEABLE | NOT RUN | other |
|---|---|---|---|---|---|---|---|
| 98 SE (E-4 alone) | 18:32-19:49 | 1 | 1 | 0 | 0 | 0 | 0 |
| 98 SE (E-4 + mouse) | 18:32-19:49 | 1 | 1 | 0 | 0 | 0 | 0 |
| 98 SE (lost interrupts, mode 3) | 18:32-19:20 | 3 | 3 | 0 | 0 | 0 | 0 |
| 98 SE | 18:32-19:33 | 13 | 12 | 0 | 0 | 0 | 1 |
| ME | 18:32-19:31 | 8 | 8 | 0 | 0 | 0 | 0 |
| 98 SE NUSB clean | 18:32-20:02 | 4 | 4 | 0 | 0 | 0 | 0 |
| 2000 SP4 | 18:33-19:25 | 10 | 10 | 0 | 0 | 0 | 0 |
| 2000 clean | 18:33-19:11 | 6 | 6 | 0 | 0 | 0 | 0 |
| 2000 SMP + Verifier | 20:07-22:53 | 8 | 5 | 0 | 0 | 0 | 3 |
| XP SP3 | 18:32-19:38 | 9 | 9 | 0 | 0 | 0 | 0 |
| XP x64 (qemu) | 18:32-19:51 | 10 | 10 | 0 | 0 | 0 | 0 |
| XP x64 (release) | 18:32-19:08 | 6 | 6 | 0 | 0 | 0 | 0 |
| Vista x86 | 18:33-20:02 | 11 | 11 | 0 | 0 | 0 | 0 |
| Vista x64 | 18:33-20:02 | 11 | 11 | 0 | 0 | 0 | 0 |
| 7 x86 | 18:33-22:12 | 20 | 14 | 0 | 0 | 0 | 6 |
| 7 x64 | 18:33-19:27 | 9 | 9 | 0 | 0 | 0 | 0 |

**36.54, UAS stream fault recovery: PASS everywhere it was owed.** These are the `qemu` flavour's `XhciQemuInject` bulk
faults on a UAS data-in stream (`0x83`). Every run used a fresh UAS device, with the stream rings wrapped by 64 MiB
to 512 MiB of traffic first.
- Coverage:
  - Faults 14 to 17 on XP x64, 2000, Vista x64, 7 x86 and 98 SE.
  - Fault 16 three times on XP x64 and on 98 SE.
  - As extra rows, the matrix's other failures: XP 14 and 15, Vista x86 14 and 17.
- **Fault 16** recovers in place on every guest: the copy or VERIFY loop runs through it with no stall.
  StreamRetryResets and RetryDiverts/Resets/Recovered rise by 1 a run, and xhciuas's timeouts, aborts, LUN resets
  and port resets stay 0.
- **Faults 14 and 17** recover after one class-timeout stall, then run clean:
  - about 160 s on 2000, XP and XP x64;
  - 62 to 63 s on Vista and 7;
  - about 15 s on 98 SE.
  Each stall is one timeout and one abort, with no LUN or port reset.
- **Fault 15** recovers with no stall. The one exception is 7 x86, below.
- QemuStreamQuirk read 1 and QemuStreamResyncFailed 0 throughout. No run left an abandon note, and no run needed a
  replug or a controller restart.
- **B9(b) on XP x64** (fault 15 on a big TD at `XhciMaxTransferKB` 2048): no stall with `XhciWideMap` 0 or 1. With 1,
  1344 promotions and TDs of up to 512 TRBs. The matrix's stall did not recur.
- **The Bulk-Only control on XP x64** (fault 16 on the stick's bulk-in `0x81`): the soft retry recovered it, the retry
  counters rose, and StreamRetryResets did not move.

**7 x86 Q3 fault 15: a QEMU abort, PASS with that note** (orchestrator ruling).
- **What happened.** The first run killed the QEMU process with
  `hw/usb/core.c:574: usb_packet_check_state: assertion failed`, right after the injection's Stop Endpoint. Five
  more runs without a throttle passed: one under a full QEMU trace, four under a light one.
- **The trace of the abort.** Throttling the UAS drive's reads (`block_set_io_throttle`, 20 MB/s, then 3 MB/s) parks
  data-in packets async, and that reproduced it twice. QEMU's own trace (`legs\w7x86\qtrace-q15-tail.txt`,
  `qtrace-qa2-tail.txt`) reads:
  1. `usb_xhci_ep_stop slotid 2, epid 7`;
  2. `running -> stopped`;
  3. the driver's re-ring (`stopped -> running`; in the MSI-X run after one Set TR Dequeue for the stream);
  4. `usb_packet_state_fault ... ep 3 ... state setup, expected async`;
  5. the abort.
  The faulting packet is the data-in packet that was async at the stop. It happened under MSI and under MSI-X alike.
- **The cause** (QEMU 11.1, `hw/usb/dev-uas.c`):
  - `usb_uas_cancel_io()` unlinks a cancelled data packet (`req->data = NULL`) but leaves `req->data_async` set.
  - When the stopped TD is fetched again, `usb_uas_handle_data()` fills the new packet at once from the data that
    arrived meanwhile.
  - `usb_uas_complete_data_packet()` then sees the stale flag and calls `usb_packet_complete()` on a packet still in
    SETUP.
- **The driver's sequence is legal xHCI:** Stop Endpoint on a running endpoint, Set TR Dequeue per stream on the
  stopped endpoint, then the doorbell that restarts it (sections 4.6.9 and 4.6.10). QEMU must not abort on it.
- **Why only since 36.54.** The build before 36.54 (`q203`), under the same throttle, did not abort in 2 of 2 runs.
  QEMU never fetched the stopped TD again: the old ignored-DCS problem left the stream ring invisible. The class
  driver's abort recovered it about 60 s later. So 36.54's correct refetch is what reaches QEMU's stale flag.
- No completed rep read a bad sector.
- Evidence: `legs\w7x86\notes.txt`, `qemu-stderr-2.log`, `-5.log`, `-6.log` and `-7.log`, `qtrace-cq2-stop.txt`.

**36.55, the backstop in the tolerance DPC (98 SE, `qemu`): PASS.**
- **E-4 alone:** `XhciInterruptMode` 1 and `XhciQemuDeadLine` 1 on the 98 SE gold image, a stick present at power-on,
  an MS-DOS prompt `F E` to the BIOS-mapped E:.
  - The copy and `fc` x2 were clean in about 53.5 min, and the stick's block counters rose at every 30 s reading.
  - TransfersSubmitted = TransfersCompleted = BackstopDrains = 6316.
- **E-4 after a hot-added mouse and its driver:** clean in about 53 min. The mouse moves and clicks before and after.
- **Mode 0 with 254 lost interrupts** (`XhciQemuInject` `020100FE`, set while the guest ran) during `F E`: clean in
  about 3 min, BackstopDrains 254. A first try with the value set before the restart did not fire: a value present
  at start is held unfired. That was the harness; it is recorded and not counted.

**36.53, interrupt modes 1 and 3: PASS.**
- **Mode 3 = Code 10:**
  - 98 SE: debugcon's `M86 S` record shows reason 18 and `C0000182`.
  - XP: DebugView shows `intr.reason` and `start.refused.delivery` 18.
  - Mode 0 afterwards starts on the line, with reason 1.
- **7 x64 and Vista x86 under QEMU's default MSI-X** (Vista x86 with the helper bound):
  - mode 0: MSI-X, reason 10;
  - mode 1: the line, reason 5, filter outcome 10;
  - mode 3: MSI-X, reason 15, outcome 12;
  - the mouse and a stick `fc` work in each.

**The INF rows: PASS.**
- **I2.** Every fresh install wrote all 19 values at the driver's defaults: ME, XP, XP x64, Vista x86/x64, 7 x86/x64,
  and the clean 98 SE and 2000 bases.
- **The 98 SE and 2000 gold images** are updates over an older driver. Both kept that driver's
  `XhciImodInterval250ns` 500 and its log values, which is NOCLOBBER's rule, and added the other 16.
- **K3, the update from 2.2.0.0, on the clean 98 SE NUSB and 2000 bases.**
  - `XhciIntelIrqRoute` 0, `XhciInterruptMode` 2 and `XhciMaxTransferKB` 2048 were preserved.
  - The five values 2.2.0.0 neither wrote nor had set by hand were added at their defaults, 19 in all.
  - `XhciIntelPortSwitch` stays 1, 2.2.0.0's value, as the release notes say.
  - The composite was re-detected once and nothing more after a reboot. XHCISNAP reads delivery 2, reason 8.

**The smoke on every regular guest (`release`): PASS.**
- **Each guest.** Install from the package with only the documented prompts; `bc` clean; the Driver tab reads
  2.3.0.0; XHCISNAP's counters land on their named words. Then the mouse, and a stick copy with `fc`.
- **Sleep and resume** with the CP1 device set attached: usb-audio, and a usb-hub on port 4 with a tablet behind it.
  After the resume every device worked again: the mouse, the stick's `fc`, the tablet behind the hub, the audio
  function, and on XP, XP x64 and 2000 a tone played.
  - ME: Stand by and Hibernate.
  - 2000, XP, XP x64, Vista x86/x64 and 7 x86/x64: hibernate. Vista and 7 resumed on the first try.
  - 2000's Bulk-Only stick came back at Code 31, the known limitation.
  - XP x64 has no Cirrus driver, so it hibernated on standard VGA, as in the matrix.
  - 98 SE has no hibernate. One Stand by with the mouse only and no USB disk entered and resumed. Stand by with a
    USB disk is 36.28 part 2's settled limitation and was not run.
- **Start > Shut Down powered off** on every guest:
  - 98 SE 3.5 s, ME 5 s, 2000 3.8 s (to "safe to turn off", with `acpi=off`);
  - XP 17 s or less, XP x64 11.8 s;
  - Vista x64 9.6 s, Vista x86 15.7 s;
  - 7 x86 10.8 s, 7 x64 11 s on a fresh boot and 51.6 s after a long session.
- **The XHCIPM test client** in D0 with reads on: shutdown completed on 98 SE in 5.1 s (4.5 s in the matrix) and on
  ME in 5 s (7 s).

**The 2000 SMP warm restart under mode 2 (WR):** mode 2 is no slower than mode 0. The time goes to QEMU TCG and the Windows 2000 HAL, not to the driver and not to Driver Verifier.
- **The setup.** Release package on a fresh overlay, Driver Verifier `0x1B`, both vCPUs on separate host threads,
  4 vCPUs on the host in all. Times run from OK on Restart to the desktop and to the mouse moving.

  | Boot | Mode 0 | Mode 2 |
  |---|---|---|
  | warm restart 1 | 4:10 (mouse 3:53) | 2:57 (mouse by 2:57) |
  | warm restart 2 | 6:00 (by 6:17) | 5:00 (by 5:17) |
  | warm restart 3 | 8:11 (by 8:28) | 6:54 (by 7:11) |
  | cold boot | 0:59 (by 1:16) | 0:59 (by 1:16; stick listed by 2:06) |

- **Every boot read as expected.** Mode 2 gave delivery 2, reason 8, PollRole 2; mode 0 gave the line, reason 1.
  There was no recovery (0 of 3), and the stick and the HID mouse started. `verifier /query` read AllocationsFailed 0,
  with no stop and no bugcheck.
- **Each warm restart is slower than the one before, in both modes.** The time is lost on the 640x480 "Starting up"
  bar, held at three blocks, before the xHCI controller starts; the controller starts about 10 s before the bar
  ends.
- **The cause.** During the hold one CPU spins in the HAL's `KeStallExecutionProcessor`, an `rdtsc` loop scaled by
  the KPCR's StallScaleFactor.
  - After three warm restarts that factor read `0x00508055` and `0x0050AF1D`, about 5.27 million ticks a microsecond.
  - From a cold boot it read `0x000051AB` and `0x00008073` on a quiet host, and the same within 0.2% under load.
  - So after warm restarts every boot-time stall runs about 160 to 250 times longer.
  - Guest time itself runs at host rate.
- **The early warning's 10.5-14+ min** fits this effect after several resets.
- Evidence: `legs\smp\notes.txt`, `timing-log.txt`, `tw-*.txt`, the per-boot snapshots, `verifier-*.txt`.

**Recorded, not the driver.**
- **Slow UAS under QEMU's MSI-X.** On 7 x86 and Vista x64, UAS VERIFY ran at 4-10 MB/s under QEMU's default MSI-X,
  with BackstopDrains rising, against 160-230 MB/s under MSI. This is the matrix's MSI-X note, attributed to QEMU.
  On 7 x86 the driver takes MSI-X by default since 36.53. The fault rows ran with `msi=on,msix=off`, as 36.54's own
  legs did.
- **The 98 SE audio wedge.** The K3 update leg on 98 SE hit the audio-load wedge right after the composite was
  re-attached, and `USBAUDIO(01)+2ED4` at a boot with usb-audio attached (36.41 and 36.44, Windows 98 SE's).
- **ME's restart rename.** ME's install asked for the documented restart twice. A second `WININIT.INI` with the same
  rename line appeared after the first boot and was consumed harmlessly at the next.

**Roadmap tasks whose legs this re-read completes** (not ticked here):
- 36.51 (I2, K3);
- 36.53 (modes 1 and 3);
- 36.54 (Q3 and B9(b) on the guests the matrix failed, with the Bulk-Only control);
- 36.55 (98 SE E-4 twice, and the mode-0 lost interrupts);
- 36.13's 98 SE E-4 leg;
- 36.final's QEMU regression smoke and the install and update legs on the first stamped cut;
- 36.final's 2000 SMP warm-restart timing, attributed above.
The 36.57 re-cut's legs are `p36-play2`'s.

**Owed to the bench:**
- a real UAS drive under a real transaction error or stop;
- `XhciWideMap` 1 with a real UAS drive;
- BackstopDrains under a real MSI-X controller.
Each is as the matrix listed it.

The full table follows. Results are the leg agents' verdicts; where the final attribution differs it is given in the
Result cell in bold.

| Guest | Check | Package | Result | Observed (abridged; full line in `legs\<guest>\notes.txt`) |
|---|---|---|---|---|
| 98 SE (E-4 alone) | RE4a | qemu | PASS | F E typed 18:46:20; copy + fc R2.BIN and RAND.BIN "FC: no differences encountered" (e4a-end2.png; e4a-end.png = the monitor power-off blank,... |
| 98 SE (E-4 + mouse) | RE4m | qemu | PASS | usb-mouse m1 port 2 hot-added 18:46 after the DOS prompt: HID wizard (HIDDEV.INF, hidclass.sys from F:\WIN98) finished (m1-m6.png); info mic... |
| 98 SE (lost interrupts, mode 3) | RLI0 | qemu | PASS | XhciInterruptMode 0 (s1.txt mode.read 0, delivery 1, reason 1); XhciQemuInject 020100FE set at run time 18:52:40 (the 010100FE present at st... |
| 98 SE (lost interrupts, mode 3) | RM3-98 | qemu | PASS | Device Manager: xHCI98 USB 3.x eXtensible Host Controller with the yellow "!" under Universal Serial Bus controllers, no root hub (m3dm3.png... |
| 98 SE (lost interrupts, mode 3) | RM3-98 (mode 0 again) | qemu | PASS | debugcon-5.log "M86 S 0001 0000 0001 00000000 00000001 00000000 00000000 00000000 00000001 00000001 00000000 cD3" (delivery 1, reason 1, sta... |
| 98 SE | RQ3-16 run 1 (u2 fresh, fault 16 on UAS data-in 0x83, rings wrapped) | qemu | PASS | 01160083 armed 18:44:03 -> qemu.inj.bulk.armed 00160107, qemu.inj.fire 01160083, qemu.inj.bulk 16030107 (stream 3), answer 0E010701, ended 1... |
| 98 SE | RQ3-16 run 2 (u3 fresh after device_del u2, same) | qemu | PASS | 02160083 armed 18:47:05 -> qemu.inj.bulk 16030107 (stream 3), answer 0E010701, ended 16, done 16, no abandon; warm-ups 13/14 s; copy complet... |
| 98 SE | RQ3-16 run 3 (u4 fresh after device_del u3, same) | qemu | PASS | 03160083 armed 18:49:48 -> qemu.inj.bulk 16030107 (stream 3), answer 0E010701, ended 16, done 16, no abandon; warm-ups 14/12 s; copy complet... |
| 98 SE | RQ3-15 (u5 fresh after device_del u4, fault 15 = bulk Stop on UAS data... | qemu | PASS | 04150083 armed 18:52:27 -> qemu.inj.bulk.armed 00150107, fire 04150083, qemu.inj.bulk 15030107 (stream 3), bulk.stopped 0, done 15, no aband... |
| 98 SE | RQ3-17 (u6 fresh after device_del u5, fault 17 = bulk Short on UAS dat... | qemu | PASS | 05170083 armed 18:55:15 -> qemu.inj.bulk.armed 00170107, fire 05170083, qemu.inj.bulk 17030107 (stream 3), done 17, no abandon; copy complet... |
| 98 SE | RQ3-14 (u7 fresh after device_del u6, fault 14 = lost completion on UA... | qemu | PASS | 06140083 armed 18:58:05 -> qemu.inj.bulk.armed 00140107, fire 06140083, qemu.inj.bulk.lost 085F0CE0 (the TD's TRB), qemu.inj.bulk.released 0... |
| 98 SE | RI1 | rel | PASS (XHCI98.TMP noted) | DM > xHCI98 controller > Driver > Update Driver > search > Specify a location D:\ -> 'D:\XHCI98.INF' (up-1..up-5.png); Insert Disk 'Windows ... |
| 98 SE | RI2 (98 SE gold base = an UPDATE over the older gold driver; RK3's rul... | rel | PASS (update: 16 added, 3 preserved, Imod 500 kept as RK3's rule) | reg-inst.txt USB\0002 after the update: all 19 present - XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, Xh... |
| 98 SE | RI4 | rel | PASS | package-rel.txt xhci98.sys 64c6e8fd... 272,281 B (= checklist release x86), xhcimsi.sys a36a5007..., xhciuas.sys 65439153..., XHCISNAP 25a03... |
| 98 SE | RS1 | rel | PASS | 'F E' (boot stick, E:): RAND.BIN 4 MB copied to E:\R2.BIN, 'FC: no differences encountered' x2 (rs1-f.png); m1 usb-mouse hot-added on port 2... |
| 98 SE | RS5 | rel | PASS (3.5 s) | launch 4 with the boot stick (E:) + m1 usb-mouse attached, DOS box exited: Shut Down Windows 'Shut down' (rs5-dlg.png), Enter at 19:19:50 ->... |
| 98 SE | RS4 (98 SE: no hibernate; Stand by with a USB disk = settled 36.28 par... | rel | RECORDED (mouse-only Stand by: entry 2.0 s, resume to the desktop, mouse works, shutdown after 3.6 s) | launch 5 (-Tag r, NO boot stick, no disk): m1 usb-mouse hot-added port 2 (bound, no wizard), pointer moves (sb-0.png); Start > Shut Down > S... |
| 98 SE | RPM (98 SE, stage.ps1 -PmMouse, XhciSelectiveSuspend 1) | rel | PASS (5.1 s) | PS1.txt 'dev D0 sys S0 started 1 ... config 1 attr 0xA0 in 0x81 status 0'; READS ON + 4 moves of mouse #3 -> 'reads on 1 ok 8 failed 0' (PS2... |
| ME | RI2 | rel | PASS | reg-inst-pre.txt key USB\0000: XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, XhciFirstEnumWaitMs 0x1388 (... |
| ME | RI1 | rel | PASS | Found New Hardware -> D:\XHCI98.INF, only the documented ME restart (asked twice: wizard + boot pass, r08/r09); before the restart XHCI98.TM... |
| ME | RI4 | rel | PASS | package-rel.txt: xhci98.sys 64c6e8fd... 272,281 (= hashes.txt release x86), XHCISNAP 25a0342d...; ri4.txt (verbosity 2, after a restart): to... |
| ME | RS1 | rel | PASS | stick s1 port 1 (5000) hot-added: 'New Hardware Found: USB Disk' automatic (r36), F:; F.BAT F: dir + copy D:\RAND.BIN F:\R2.BIN + fc /b x2 '... |
| ME | RS4 Stand by | rel | PASS | Start > Shut Down > Stand by (r59-sb): QEMU 'paused (suspended)' 2 s after OK; system_wakeup + a key: 'running', CPU idle in HLT (EIP ff03a3... |
| ME | RS4 Hibernate (offered) | rel | PASS | the set as above, stick accessed; Start > Shut Down > Hibernate (r71-hibmenu, r72-hib): image written, 'paused (shutdown)' 13 s after OK; qu... |
| ME | RS5 | rel | PASS (5 s) | the RS4 set still attached after the hibernate resume (s1 accessed by F.BAT, m1, a1, hub h1 + tablet 4.1); DOS box exited, Start > Shut Down... |
| ME | RPM | rel | PASS (5 s) | PMCTL STATUS 'dev D0 sys S0 started 1', READS ON -> 'reads on 1 ok 4 failed 0 ... config 1 attr 0xA0 in 0x81' (p15-st, p16-st; tablet report... |
| 98 SE NUSB clean | RI2 | rel | PASS | reg-inst-pre.txt key USB\0002: XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, XhciFirstEnumWaitMs 0x1388, ... |
| 98 SE NUSB clean | RI1 bits | rel | PASS | BC 'FC: no differences encountered' after the restart (i22); Driver tab Provider Yeo Kheng Meng, Date 10-11-2026 (i28-drv); Driver File Deta... |
| 98 SE NUSB clean | RS1 (quick) | rel | PASS | stick s1 port 1 (5000): F.BAT F: copy + fc /b x2 'no differences encountered' (i43-f); usb-mouse m1 port 2 (480): Add New Hardware 'USB Huma... |
| 98 SE NUSB clean | RK3 | prev->rel | PASS (values, composite once, nothing new, mode 2); the wedge + fault recorded as KNOWN 36.41/36.44 | reg-upd.txt (2nd block, after the update + the restart) key USB\0002: user values KEPT XhciIntelIrqRoute 0, XhciInterruptMode 2, XhciMaxTran... |
| 2000 SP4 | RQ3-16 | qemu | PASS | q16.txt: qemu.inj.bulk.armed 00160107, fire 01160083, qemu.inj.bulk=160F0107 (stream 0x0F, slot 1, DCI 7), xfer.error=00010704, answer 0E010... |
| 2000 SP4 | RQ3-14 | qemu | PASS | q14b.txt (VERIFY 1M x256 MiB -reps 8, started 18:48:29): notes bulk.armed 00140107, fire 02140083, qemu.inj.bulk.lost=015111A0, bulk.release... |
| 2000 SP4 | RQ3-17 | qemu | PASS | q17b.txt (VERIFY 1M x256 MiB -reps 8, started 18:55:26): notes bulk.armed 00170107, fire 03170083, qemu.inj.bulk=17040107 (stream 0x04, slot... |
| 2000 SP4 | RQ3-15 | qemu | PASS | q15.txt (VERIFY 1M x256 MiB -reps 8, 19:05:03): notes bulk.armed 00150107, fire 04150083, qemu.inj.bulk=15040107 (stream 0x04, slot 1, DCI 7... |
| 2000 SP4 | RI1 | rel | PASS | adv.bat > Driver > Update Driver > Display a list > Have Disk E:\ > 'xHCI98 USB 3.x eXtensible Host Controller' > Next > Next > Finish (r-u3... |
| 2000 SP4 | RI2 (gold = update over 1.99.0.0) | rel | PASS (recorded: update rule) | r-inst.txt key 0000: DriverVersion 2.3.0.0, InfPath oem1.inf, 19 values: kept from the old driver XhciLogVerbosity 0, XhciLogDebugView 0, Xh... |
| 2000 SP4 | RI4 | rel | PASS | package-rel.txt xhci98.sys 64c6e8fd31c4249d... (= hashes.txt release x86), XHCISNAP 25a0342d...; XHCISNAP -verbosity 2 + dc/ec (r-verb.txt),... |
| 2000 SP4 | RS1 | rel | PASS | the HS usb-mouse (port 2) moved the pointer to the properties dialog's Close and clicked it (r-u7 -> r-u8), moved (298,447) -> (548,297) by ... |
| 2000 SP4 | RS4 | rel | PASS (stick Code 31 after S4 = known Windows 2000 limitation, recorded) | Start > Shut Down > Hibernate (r-hib1) 19:18:08 -> 'It is now safe to turn off your computer' (r-hib2, acpi=off), quit; launch 3 19:19 (pid ... |
| 2000 SP4 | RS5 | rel | PASS | after RS4 (CP1 set + mouse + stick attached): Start > Shut Down > Shut down (r-sd1) -> 'It is now safe to turn off your computer' (r-sdp/r-s... |
| 2000 clean | install (fresh) | rel | PASS | shots boot1, fi1-fi6 ('Completing ... xHCI98 USB 3.x eXtensible Host Controller'), no restart asked; then the known Explorer 'E:\ is not acc... |
| 2000 clean | RI2 | rel | PASS | i-rk.txt key 0000: XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, XhciFirstEnumWaitMs 0x1388 (5000), XhciF... |
| 2000 clean | RI1 bc | rel | PASS | i-rk.txt: 'Comparing files C:\WINNT\SYSTEM32\DRIVERS\xhci98.sys and E:\XHCI98.SYS / FC: no differences encountered' (xhcimsi absent on 2000,... |
| 2000 clean | RI1 Driver tab | rel | PASS | adv.bat > Driver tab: Provider (the INF's ProviderName), Date Not available, Version 2.3.0.0, Not digitally signed (shot drv2); General: wor... |
| 2000 clean | RS1 (quick) | rel | PASS | m2 usb-mouse port 2 (480) + s1 usb-storage port 1 (5000) hot-added, no wizard: HID mouse + HID-compliant mouse + USB Mass Storage + USBSTOR ... |
| 2000 clean | RK3 | prev->rel | PASS | prev: FNH > Specify a location E:\ > e:\xhci98.inf > Finish (shots k-fi3..k-fi6, then the Explorer 'E:\ is not accessible' box k-fi7, OK'd);... |
| 2000 SMP + Verifier | R6 check-smp-parallelism.ps1 -MonitorPort 55109 | rel | PASS | 2 vCPUs, distinct thread ids 17840/54828, affinity 0xFFFFFFFF (32 of 32), exit 0 (parallelism-check.txt) |
| 2000 SMP + Verifier | R6 verifier /query at start | rel | PASS | 20:41 verifier-start.txt: Level 0000001B, xhci98.sys loads 1 unloads 0 (the base's old 1.0.0.0 driver), AllocationsFailed 0 |
| 2000 SMP + Verifier | R6 install (as I1) | rel | PASS | adv.bat > Driver tab (1.0.0.0) > Update Driver > list > Have Disk E:\ > xHCI98 USB 3.x eXtensible Host Controller > Next > Finish: Found New... |
| 2000 SMP + Verifier | R6 WR mode 0 warm restart x3 | rel | BENCH (works every time; slow and growing per warm restart in mode 0 too) | vCPU total incl. ours 4 at each (smp 2 + p36rr w7x86 2; timing-log.txt): m0a desk 4:10 / mouse 3:53 / stick listed by 5:23; m0b desk 6:00 / ... |
| 2000 SMP + Verifier | R6 WR mode 2 warm restart x3 | rel | BENCH (works; same growth as mode 0) | vCPU total 4 at each: m2a desk 2:57 / mouse <=2:57; m2b desk 5:00 / mouse <=5:17 / stick by 6:15; m2c desk 6:54 / mouse <=7:11 / stick by 8:... |
| 2000 SMP + Verifier | R6 WR cold boot (quit + relaunch) | rel | BENCH (cold 0:59 both modes vs warm 3-8 min) | c0a mode 0, total 4: desk 0:59 / mouse <=1:16 / stick listed (c0a-ls.txt); c2a mode 2, total 3 (22:49, p36p2 w98 1 + ours 2): desk 0:59 / mo... |
| 2000 SMP + Verifier | R6 WR attribution | rel | PASS (no driver fault; finding: QEMU TCG warm reset miscalibrates the 2000 MP HAL stall factor) | QEMU TCG + the 2000 HAL, not xhci98: (1) the time goes to the 640x480 "Starting up" bar stuck at 3 blocks BEFORE the xHCI controller starts ... |
| 2000 SMP + Verifier | R6 verifier /query at the end | rel | PASS | verifier-end.txt (22:46, launch 3 boot): Level 1B, xhci98.sys loads 1 unloads 0, AllocationsAttempted 33 = Succeeded 33 = SpecialPool 33, Al... |
| XP SP3 | RI1 | rel | PASS | controller FNH cancelled, h\inst.ps1: i.bat -> Logo 'xHCI98 USB 3.x eXtensible Host Controller' alt-i (inst-r-1); the root hub FNH opened un... |
| XP SP3 | RI2 | rel | PASS | rk-r.txt (UTF-16 as sent; rk-r-a.txt de-nulled) key 0000: XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, X... |
| XP SP3 | RI4 | rel | PASS | package-rel.txt xhci98.sys 64c6e8fd... 272281 B (= hashes.txt), XHCISNAP 25a0342d...; first t.bat: 'request was refused: 6 (DRIVER DECLINED)... |
| XP SP3 | RS1 | rel | PASS | usb-mouse bm1 (port 2, HS): pointer moved 513,393 -> 680,500 (rs1mv0 -> rs1mv1), right click opened the desktop menu (rs1clk), left click fo... |
| XP SP3 | RS4 | rel | PASS | launch 1 cirrus: 'System does not support hibernation' before a restart (hw.txt); shutdown -r, then powercfg /hibernate on accepted + every ... |
| XP SP3 | RS5 | rel | PASS | launch 2 after RS4 (CP1 set + stick + mouse attached): ctrl-esc, Turn Off Computer, Turn Off (sd1) at 19:04:46 -> HMP 'VM status: paused (sh... |
| XP SP3 | RM3-XP | rel | PASS | launch 3 (-Tag r, pid 41436, cirrus, -BootStick -BootMouse): setv XhciLogDebugView 1 + dc/ec (m3set0.txt); DebugView 'DBGVIEW /f /l C:\dv.lo... |
| XP SP3 | RQ3-15 (extra; the matrix's XP Q3-15 FAIL) | qemu | PASS | x15a.txt / x15a-meta.txt / x15a-run.log: device_del u1 + uasraw u2 port 3 (same disk, PhysicalDrive2, x15a-disks.txt computer name scrubbed)... |
| XP SP3 | RQ3-14 (extra; the matrix's XP Q3-14 FAIL) | qemu | PASS | x14a.txt / x14a-meta.txt / x14a-run.log: device_del u2 + uasraw u3 port 3 (PhysicalDrive2), warm-up VERIFY 300 MiB 85.5 MB/s 0 bad; armed 00... |
| XP x64 (qemu) | RQ3-16 run 1 | qemu | PASS | r16a.txt: armed 00160207, fire 01160083, qemu.inj.bulk=16040207 (stream 4, slot 2, DCI 7), xfer.error=00020704, answer 0E020701, ended/done ... |
| XP x64 (qemu) | RQ3-16 run 2 | qemu | PASS | r16b.txt: fire 02160083, qemu.inj.bulk=160F0207 (stream 15, slot 2, DCI 7), xfer.error 00020704, answer 0E020701, ended/done 16, no abandon;... |
| XP x64 (qemu) | RQ3-16 run 3 | qemu | PASS | r16c.txt: fire 03160083, qemu.inj.bulk=16090207 (stream 9, slot 2, DCI 7), xfer.error 00020704, answer 0E020701, ended/done 16, no abandon; ... |
| XP x64 (qemu) | RQ3-14 | qemu | PASS | r14.txt: armed 00140207, fire 04140083, qemu.inj.bulk.lost=066C6990, bulk.released=00010207, done 14 (no abandon); rep 3 162501 ms (the one ... |
| XP x64 (qemu) | RQ3-15 | qemu | PASS | r15.txt: armed 00150207, fire 05150083, qemu.inj.bulk=15060207 (stream 6, slot 2, DCI 7), bulk.stopped 0, done 15 (no abandon); reps 1-8 141... |
| XP x64 (qemu) | RQ3-17 | qemu | PASS | r17.txt: armed 00170207, fire 06170083, qemu.inj.bulk=17070207 (stream 7, slot 2, DCI 7), done 17 (no abandon); rep 2 162839 ms (the one sta... |
| XP x64 (qemu) | RBOT16 | qemu | PASS | BOT stick s1 (usb-storage on VVFAT legs\stickbig, F:, BIG.BIN 32 MiB) hot-added on root port 1 (botdrv.txt USBSTOR started); h\bot.ps1: inje... |
| XP x64 (qemu) | RQ3-15t | qemu | PASS | q15t.txt: armed 00150207, fire 08150083, qemu.inj.bulk=150B0207 (stream 11, slot 2, DCI 7), bulk.stopped 0, done 15, no abandon; reps 1-8 21... |
| XP x64 (qemu) | RB9b (WideMap 0) | qemu | PASS | b9set.txt setv XhciMaxTransferKB 0x800 (getv: WideMap 0, 19 values), trace events switched off (HMP), shutdown -r (in-guest restart, QEMU pr... |
| XP x64 (qemu) | RB9b (WideMap 1) | qemu | PASS | b9set1.txt setv XhciWideMap 1 + dc/ec (2048 kept); fresh device u10 after 320 MiB: b9w1.txt xfer.wide value/switch/check/enabled 1, 'promoti... |
| XP x64 (release) | RI1 | rel | PASS | launch 1 (pid 43904, -Tag r fresh overlay, -Vga cirrus-vga -BootStick -BootMouse -AudioWav -Display none): controller FNH cancelled, i.bat -... |
| XP x64 (release) | RI2 | rel | PASS | rk.txt key 0000: XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, XhciFirstEnumWaitMs 0x1388 (5000), XhciFir... |
| XP x64 (release) | RI4 | rel | PASS | package-rel.txt xhci98.sys 10400ec5...cadab 313344 B, xhciuas.sys 7579e2ed...461c, XHCISNAP 25a0342d...; after XHCISNAP -verbosity 2 + dc/ec... |
| XP x64 (release) | RS1 | rel | PASS | rs1f.txt f.bat F: RAND.BIN -> F:\R2.BIN '1 file(s) copied', fc /b E:\RAND.BIN vs F:\R2.BIN and vs F:\RAND.BIN 'FC: no differences encountere... |
| XP x64 (release) | RS4 | rel | PASS | relaunch 2 (pid 18428, std VGA, -BootStick -BootMouse -AudioWav -Display none): pwr1.txt powercfg /hibernate on accepted, timers Never ('Sys... |
| XP x64 (release) | RS5 | rel | PASS | after RS4 with the CP1 set + stick + mouse attached: Start (sd0) -> Turn Off Computer -> dialog 'Hibernate / Turn Off / Restart' (sd1, Hiber... |
| Vista x86 | RI2 | rel | PASS | r-i2-rk.txt (read right after pnputil, before any change): XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, ... |
| Vista x86 | RI1 | rel | PASS | pnputil -i -a E:\xhci98.inf -> unsigned box Install anyway (r-i1a.png) -> oem3.inf 'Successfully installed' (r-i1c.png); before the restart:... |
| Vista x86 | RI4 | rel | PASS | package-rel.txt: xhci98.sys 64c6e8fd.. (272281), xhcimsi.sys a36a5007.. (13312), XHCISNAP 25a0342d..; (XHCISNAP -verbosity 2 + dc/ec, r-i1-p... |
| Vista x86 | RS1 | rel | PASS | stick s1 port 1 (5000) + usb-mouse m1 port 2 hot-added (r-i1-post.txt: USB Mass Storage, HID-compliant mouse, USBSTOR disk started); info mi... |
| Vista x86 | RS4 | rel | PASS | before: stick s1 port 1, mouse m1 port 2, usb-audio a1 port 3, hub h1 port 4 + tablet t1 4.1 all started (r-cp-ls.txt: composite + MI_00 Aud... |
| Vista x86 | RM-vx86 mode 0 | rel | PASS | first boot after install (r-i4.txt) and again at the end after setv 0 + shutdown /r (rm0.txt): intr.mode.read 0, intr.delivery 5 then 4, int... |
| Vista x86 | RM-vx86 mode 1 | rel | PASS | r-m1-set.txt, shutdown /r, Activation 'Activate later'; rm1.txt: intr.mode.read 1, intr.delivery 1, intr.reason 5; IntrDelivery 1, IntrReaso... |
| Vista x86 | RM-vx86 mode 3 | rel | PASS | r-m3-set.txt, shutdown /r, Activation 'Activate later' (the first elevated-cmd attempt typed into Start search - closed, redone, rm3b-cmd.pn... |
| Vista x86 | RS5 | rel | PASS | (devices attached: stick, mouse, usb-audio, hub + tablet) cmd closed, desktop focused, Alt-F4 -> 'Shut Down Windows' dialog with Shut Down s... |
| Vista x86 | RQ3-14 (extra; the matrix's Vista x86 failure) | qemu | PASS | u1 (fresh, first UAS device of launch n=3, 1 GiB WRITE/VERIFY + 512 MiB VERIFY before), 01140083 (xq14-pre/-fire/-post.txt, xq14-run.log): q... |
| Vista x86 | RQ3-17 (extra; the matrix's Vista x86 failure) | qemu | PASS | u2 (device_del u1 + fresh uasraw u2), 02170083 (xq17-pre/-fire/-post.txt, xq17-run.log): qemu.inj.bulk.armed 00170107, qemu.inj.fire 0217008... |
| Vista x64 | RQ3-14 | qemu | PASS | u2 (fresh, first UAS device of launch n=2), 01140083 (rq14-pre/-fire/-post.txt, rq14-run.log): pre VERIFY 2x256 MiB 126/132 MB/s 0 bad; qemu... |
| Vista x64 | RQ3-15 | qemu | PASS | u3 (device_del u2 + fresh uasraw u3, port 3), 02150083 (rq15-pre/-fire/-post.txt, rq15-run.log): pre VERIFY 2x256 MiB 174/178 MB/s 0 bad; qe... |
| Vista x64 | RQ3-16 | qemu | PASS | u4 (device_del u3 + fresh uasraw u4), 03160083 (rq16-pre/-fire/-post.txt, rq16-run.log): pre VERIFY 2x256 MiB 152/162 MB/s 0 bad; qemu.inj.b... |
| Vista x64 | RQ3-17 | qemu | PASS | u5 (device_del u4 + fresh uasraw u5), 04170083 (rq17-pre/-fire/-post.txt, rq17-run.log): pre VERIFY 2x256 MiB 134/136 MB/s 0 bad; qemu.inj.b... |
| Vista x64 | RQ3-15c | qemu | PASS | u6 (first UAS device of launch n=3), 05150083 (rq15c-pre/-fire/-post.txt, rq15c-run.log): qemu.inj.latched 04170083 (rq17's value latched at... |
| Vista x64 | RI1 | rel | PASS | pnputil -i -a E:\xhci98.inf -> unsigned box Install anyway (r-i1a.png) -> oem3.inf, PCA 'requires a digitally signed driver' box closed (r-i... |
| Vista x64 | RI2 | rel | PASS | r-i2-rk.txt (read right after pnputil, before any change): XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFastPollFsLs 0, ... |
| Vista x64 | RI4 | rel | PASS | package-rel.txt: amd64 xhci98.sys 10400ec5.. (313344), xhciuas.sys 7579e2ed.., XHCISNAP 25a0342d.. (x86 tool under WOW64); XHCISNAP -verbosi... |
| Vista x64 | RS1 | rel | PASS | stick s1 port 1 + usb-mouse m1 port 2 started (r-i4pre.txt); info mice '* Mouse #3: QEMU HID Mouse': pointer 513,393 -> 382,285 (r-rs1-mb/ma... |
| Vista x64 | RS4 | rel | PASS | before: stick s1, mouse m1, usb-audio a1 port 3, hub h1 port 4 + tablet t1 4.1 all started (r-cp-ls.txt: composite + Audio Device, hub, 2 HI... |
| Vista x64 | RS5 | rel | PASS | (stick, mouse, usb-audio, hub + tablet attached) cmd closed, desktop focused, Alt-F4 -> 'Shut Down Windows' dialog with Shut Down selected (... |
| 7 x86 | RQ3-14 | qemu | PASS | u3 (fresh), pre 164.2/173.4 MB/s (q14-pre.txt); fire 01140083 (q14-fire.txt): qemu.inj.bulk.armed 00140207, qemu.inj.bulk.lost 7FAC75E0, qem... |
| 7 x86 | RQ3-15 (run 1) | qemu | QEMU abort (core.c:574); **ruling: PASS with the QEMU note - usb-uas stale data_async, legal Stop EP + re-ring (5 of 6 unthrottled runs clean)** | pre 161.0/135.3 MB/s, QemuStreamQuirk 1, Resyncs 16, failed 0 (q15-pre.txt); fire 02150083 typed 19:15:57; debugcon-2.log last line 'xhci98:... |
| 7 x86 | RQ3-15 (run 2, TRACED repro) | qemu | PASS (this run) - the abort did not reproduce under the full trace | clean shutdown of the aborted overlay (chkdsk ran once at the next boot, q3-boot), relaunch n=4 -Tag q msi=on,msix=off + trace; u5 fresh, pr... |
| 7 x86 | RQ3-15 (run 3, light trace) | qemu | PASS (this run) | 04150083 (q15c-pre/-fire/-post.txt, q15c-run.log): armed 00150207, qemu.inj.bulk 150E0207 (stream 14, slot 2, DCI 7), bulk.stopped 0, done 1... |
| 7 x86 | RQ3-15 (run 4, light trace) | qemu | PASS (this run) | 05150083 (q15d-*.txt, q15d-run.log): qemu.inj.bulk 150B0207 (stream 11), done 15; reps 1.31-1.79 s all 0 bad, loop-exit 0; further VERIFY 17... |
| 7 x86 | RQ3-14 (2nd run, light trace; sets up run 1's sequence) | qemu | PASS | 06140083 (q14b-*.txt): reps 1.25-1.48 s except rep 3 62.59 s (one class timeout), all 0 bad, loop-exit 0; further VERIFY 206.8 MB/s 0 bad; x... |
| 7 x86 | RQ3-15 (run 5, light trace, right after a fault 14 = run 1's order) | qemu | PASS (this run) | 07150083 (q15e-*.txt): pre Resyncs 36; reps 1.28-1.40 s all 0 bad, loop-exit 0 (no stall); further VERIFY 208.2 MB/s 0 bad; xhciuas 0/0/0/0;... |
| 7 x86 | RQ3-15 (throttle runs qt1/qt2, n=5 untraced launch, trace enabled at r... | qemu | INFO | qt1: block_set_io_throttle iops_rd 40 on fresh t1 (2.5 MB/s with the stderr trace on), the fire line lost one ')' while typing (cmd 'More?' ... |
| 7 x86 | RQ3-15 (run qt3, REPRODUCED) | qemu | QEMU abort reproduced under a read throttle; **ruling: QEMU defect (dev-uas.c usb_uas_cancel_io), not the driver** | fresh t3 (n=5), pre 2 x 256 MiB 20.1/20.0 MB/s 0 bad (qt3-pre.txt = XHCISNAP just before the injection); fire 0A150083 typed 20:36:44 (debug... |
| 7 x86 | RQ3-15 control (a): the matrix's interrupt config (QEMU default msi+ms... | qemu | QEMU abort reproduced under MSI-X too; **ruling: QEMU defect, independent of the interrupt config** | qa1: fresh a1, bps_rd 20 MB/s (above MSI-X's own ~4 MB/s, so no effect), fire 0B150083 (qemu.inj.bulk 150E0207): the stop met no async data-... |
| 7 x86 | RQ3-15 control (b): the PRE-36.54 qemu build (stamped 2.2.0.3, out\p36... | q203 | no abort on the pre-36.54 build: QEMU never refetches the stopped TD (DCS ignored), class abort recovers after ~60 s; **ruling: the abort is reachable only thro... | INFO (control: no abort on 2.2.0.3 = the old stall instead) |
| 7 x86 | RQ3-16 | qemu | PASS | relaunch n=8 -Tag q msi=on,msix=off untraced, no throttle; v1 fresh, pre 183.9/209.1 MB/s (q16-pre.txt: StreamRetryResets 0, no Retry* count... |
| 7 x86 | RQ3-17 | qemu | PASS | v2 fresh (device_del v1), pre 226.7/228.1 MB/s; fire 0E170083 (q17-fire.txt): armed 00170207, qemu.inj.bulk 170C0207 (stream 12, slot 2, DCI... |
| 7 x86 | RQ3-15 (verdict) | qemu | agent: FAIL (QEMU defect); **ruling (orchestrator): PASS with the QEMU note - QEMU usb-uas stale data_async on a legal Stop EP + re-ring, reachable since 36.54 ... | 5 runs without a read throttle passed (q15b/c/d/e and qt2 with no async data-in at the stop); run 1 (19:17, untraced) and the targeted repro... |
| 7 x86 | RI1 | rel | PASS | launch n=9 -Tag r (fresh) -BootStick, default xHCI (msi+msix); elevated cmd, ShellHWDetection stopped (r-hw.txt: no xHCI devices before); pn... |
| 7 x86 | RI2 | rel | PASS | r-inst.txt (reg query key 0000 right after pnputil, before any change): XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, XhciFas... |
| 7 x86 | RI4 | rel | PASS | package-rel.txt xhci98.sys 64c6e8fd.. (272281), xhcimsi.sys a36a5007.., xhciuas.sys 65439153.., XHCISNAP 25a0342d.. (no stage warning); XHCI... |
| 7 x86 | RS1 | rel | PASS | usb-mouse m1 hot-added port 2 (HID mouse started, r-post.txt): pointer ~400,308 -> ~263,447 (rs1-m-b/-a), a right-click opened the desktop c... |
| 7 x86 | RS4 | rel | PASS | rs4-pre.txt: powercfg /a Hibernate only (VGAPNP blocks S1-S3); stick port 1, mouse m1 port 2, usb-audio a3 port 3 (XHCI98\COMPOSITE parent +... |
| 7 x86 | RS5 | rel | PASS | n=10 after RS4 (stick, mouse, usb-audio, hub + tablet attached): Start menu opened with the usb-mouse (rs5-start2; a first click landed on t... |
| 7 x64 | RI1 | rel | PASS | launch n=1 -Tag r (fresh) -BootStick, F8 DSE (f8sel); elevated cmd, ShellHWDetection stopped (r-hw.txt); pnputil -i -a E:\xhci98.inf -> unsi... |
| 7 x64 | RI2 | rel | PASS | r-inst.txt (reg query key 0000 right after the install, before any change): XhciLogVerbosity 0, XhciLogDebugView 0, XhciForceBulkOnly 0, Xhc... |
| 7 x64 | RI4 | rel | PASS | package-rel.txt xhci98.sys 10400ec5.. (313344), xhciuas.sys 7579e2ed.., XHCISNAP 25a0342d.. (built 18:17:57 / 18:19:12, no stage warning); X... |
| 7 x64 | RS1 | rel | PASS | usb-mouse m1 hot-added port 2 (480 Mb/s): pointer moved ~400,308 -> ~575,420 (rs1-m-b/-a), a click opened the Start menu (rs1-click2); f.bat... |
| 7 x64 | RS4 | rel | PASS | rs4-pre.txt: powercfg /a Hibernate only (VGAPNP blocks S1-S3); stick port 1, mouse m1 port 2, usb-audio a3 port 3 (XHCI98\COMPOSITE parent +... |
| 7 x64 | RM-7x64 mode 1 | rel | PASS | setv 0000 XhciInterruptMode 1 (rm1-set.txt), shutdown /r, F8 DSE (rm1-f8sel); rm1.txt: intr.mode.read 1, intr.mode 1, intr.delivery 1, intr.... |
| 7 x64 | RM-7x64 mode 3 | rel | PASS | setv 0000 XhciInterruptMode 3 (rm3-set.txt), shutdown /r, F8 DSE (rm3-f8sel); rm3.txt: intr.mode.read 3, intr.mode 3, intr.delivery 5 -> 4, ... |
| 7 x64 | RM-7x64 mode 0 (and the end state) | rel | PASS | setv 0000 XhciInterruptMode 0 (rm0-set.txt), shutdown /r, F8 DSE (rm0-f8sel); rm0.txt: intr.mode.read 0, intr.mode 0, intr.delivery 5 -> 4, ... |
| 7 x64 | RS5 | rel | PASS | run 1 (n=2, after RS4 resume + RM modes, mode 0, with stick, mouse, usb-audio a3, hub h3 + tablet tb3 attached; rs5-start2/rs5-hover2): clic... |

## The owner's bench, 2026-10-11

The owner's real-hardware bench for `2.3.0.0`, read after the cut on the
owner's own machines. The orchestrator recorded the readings as they came
in. Raw files: the `XHCISNAP` reports in `temp\2300bench\` (`.TXT`, `.BIN`,
`.PSC`; git-ignored, kept on development host A) and the `XHCIQUAL` logs
in `xhciqual\results\e7250-2026-10-11\` (git-ignored; its `README.md` has
the commands). The bench plan is roadmap 36.final's checkpoint and 36.14.

**The packages.**
- The first signed cut, `DriverVer` 10/11/2026 (`out\p36-stamp3`, zip
  SHA-256 `879768458F92...2265E`).
- The 36.57 test build, `dad964f`, test-signed, its `xhci98.sys` put in
  place by hand on the E7250 under Windows 7 x64.
- The final signed re-cut, `DriverVer` 10/12/2026 (`out\p36-stamp4`, zip
  1,057,739 bytes, SHA-256 `025EA20C...1A1EE3`; `verify-cut.ps1` 84 PASS,
  0 FAIL), now `releases\2.3.0.0\`.

**Dell Latitude E7250** (Intel Wildcat Point-LP, `8086:9CB1`, subsystem
`1028:062D`, xHCI 1.0, with the EHCI beside it; BIOS A17).
- **Windows 7 x64, first cut.** A fresh install with test-signing on: the
  install asked about the publisher, as documented, and the driver loaded
  with no F8.
  - **Default** (`e7250-w7-msi`): a message (MSI) delivery, reason 10.
    The routing repair decided `12 MESSAGE` with no `D20IR` write.
    BackstopDrains 0 (no tolerance counter non-zero).
  - **The switchover.** At its default `0` the USB 3 stick ran at USB 2.0
    speed on the EHCI, as designed (that reading led to 36.56).
    The switchover turned on (`XhciIntelPortSwitch` `1`) and a restart gave SuperSpeed on port 13 (speed ID 4,
    5000 Mbit/s).
  - **Port 2**, an empty switchable USB 2.0 port, logged one failed reset
    at the start (`enum.port.fail`, PLS 5) and nothing after. Harmless.
  - **`XhciInterruptMode` `1`** (`e7250-w7-line`, and
    `e7250-w7-line-noehci` with the EHCI disabled in Device Manager): the
    line, reason 5, the filter's outcome 10. The routing repair wrote
    `D20IR` `3210` -> `3215` (INTA from PIRQA to PIRQF, IRQ 21) and read it
    back. Transfers completed on the line with the EHCI disabled, and no
    tolerance counter moved.
  - **USB audio.** The C-Media (`0D8C:0014`, adaptive) played
    (`cm`: IsoPacketsAnswered 6492). The Creative Sound Blaster Play! 2
    (`041E:323D`, asynchronous with a feedback endpoint) played only hiss
    (`e7250-w7-play2-hiss`: IsoSubmits 0, 1882 Stopped - Length Invalid):
    task 36.57, `docs/issues/14-async-audio-explicit-startframe-no-cfc.md`. Each showed as a composite devnode
    (CompositeParents 1). USBView read both devices' strings (`play2.txt`,
    `cmedia.txt`).
- **Windows 7 x64, the 36.57 test build `dad964f`.** The Play! 2 played
  cleanly at 44.1 and 48 kHz (`pfix2`: IsoPacketsAnswered 13462), and over a
  listen of about 4.75 minutes at 48 kHz (`p2long`: IsoPacketsAnswered
  296075). The feedback endpoint (DCI 7) completes about half its reads
  with a USB Transaction Error (4441 of 8922 in `p2long`), steadily; the
  data endpoint is clean and nothing recovers. Harmless; cause open (issue
  14). A `p2fix` capture of an earlier test build stayed silent.
  - **Have Disk did not take the test build.** Its INF was byte-identical
    to the installed one, so Windows 7 reused the package in its driver
    store. The `.sys` was replaced by hand, and the re-cut took a new
    `DriverVer` (10/12/2026) for that reason.
- **Windows 7 x64, the final re-cut.** Installed over the first cut with
  Have Disk; the Play! 2 plays well.
- **Windows XP x64, first cut.**
  - **Default** (`e7520-xp-default`): the routing repair wrote `D20IR`
    `3210` -> `3215` (INTA from PIRQA to PIRQF, IRQ 21) and read it back;
    delivery by the line, reason 1, no tolerance counter non-zero.
  - **`XhciIntelIrqRoute` `0`** (`e7520-xp-route0`): the repair decided
    `20 OFF` and wrote nothing, so the line was dead. Automatic mode moved
    to polling at run time (delivery 2, reason 9; LineMoves 1). The line's
    retirement was proven by a masked acknowledgement (LineMoveMasked 1).
    A SuperSpeed stick on port 13 worked: copies compared clean with `fc`,
    with no transfer error.
  - **Polling on XP runs at the timer's granularity**, not at 1 ms:
    PollIntervalMinMs 15 and PollIntervalMaxMs 62 over 3558 runs.
- **XHCIQUAL `2.3.0.0` under MS-DOS 6.22**, booted from a USB 2.0 stick
  left on the EHCI (`PROBE.LOG`, `POLL.LOG`, `IRQ.LOG`).
  - `--probe-only --gsi 21`: no disqualifiers; `--gsi 21` **MISMATCH** (the
    pin reaches PIRQA, GSI 16; 21 is PIRQF). This is right, and it is the
    first real-hardware reading of `--gsi`.
  - `--poll-only`: C1 to C3 PASS (PROVISIONAL, as designed).
  - `--irq-selftest`: C4 PASS on IRQ 11. In PIC mode the BIOS routes PIRQA
    to IRQ 11 and the Interrupt Line agrees, so under DOS the line works.
    The E7250's fault is on the APIC side only. A dead line under DOS
    cannot be read on this machine.
- **Not read on the E7250:** standby (Windows 7 x64 has no graphics
  driver there, so no S3), selective suspend and SuperSpeed remote wake
  (the owner skipped them), and Windows 98 SE (not installed).

**ThinkPad E460** (Intel Sunrise Point-LP, `8086:9D2F`), Windows 98 SE, the
first cut.
- **`XhciInterruptMode` `0`** (`e460def`): the line (delivery 1, reason 1),
  with no dead-line move. About 18 GB through a SuperSpeed stick on port
  14 (bulk data URBs 18,143,002,498 bytes; 607,199 commands), with no
  transfer error, timeout or reset on the stick. The control endpoint
  took two STALLs. The owner replugged the stick four times.
- **`XhciInterruptMode` `2`** (`e460pol`): polling (delivery 2, reason
  8), about 4 GB through the same stick (81,473 commands), with no error.
- `fc /b` compared clean in both modes; the fastest copies ran at about
  200 MB/s (the owner's reading).
- A Low-Speed mouse worked on port 3. Ports 6 to 8, the machine's internal
  devices, were addressed only, since Windows 98 SE has no drivers for
  them.
- **Not read:** standby. That Windows 98 SE install offers no Stand by.

**ThinkPad P14s Gen 1**, Windows 98 SE: the owner's brief checks only,
"driver works". No snapshot and no figure. The short list in the bench
plan (an `fc`-checked copy, ATTO with the MSSU10 against the 36.21
baseline, `XhciInterruptMode` `2`, a standby) was not run.

**Not read on this bench** (the release notes' and README's "Untested
ground"; roadmap "Planned - bench follow-up after `2.3.0.0`"):
- standby (S3) on any real machine;
- 36.40's SuperSpeed remote wake, at a root port and behind a USB 3 hub,
  and 36.36's remote-wake HID device;
- MSI-X on a real controller, and BackstopDrains under it;
- a dead line under DOS;
- Windows 98 SE on the E7250;
- `XhciWideMap` on an updated 32-bit Windows 7, and `XhciWideMap` `1`
  with a real UAS drive;
- a real UAS drive under a transaction error or a stop;
- the ATTO and MSSU10 speed comparison against 36.21's baseline (36.27).
  The E460's about 200 MB/s is the only real-hardware speed reading.

**Roadmap tasks whose bench clauses this reads:** 36.14 (the E460's mode
2 and the E7250's dead-line move), 36.16 (MSI on the E7250 under 7 x64),
36.43 (two composite audio devices on the E7250), 36.45 (USBView strings
on the E7250), 36.57, and 36.final's E7250 check, but for S3 and Windows 98
SE.

## Moved from the roadmap at the phase's close (2026-10-12)

When Phase 36 closed, its roadmap entry was cut to one line a task
(`roadmap-hcd.md`, "Phase 36"), as the earlier phases' are. The entry as
it stood at the close follows verbatim, so the decisions, the reviewed
design facts and the evidence pointers inside the task text stay on
record. Its title was "Phase 36 - The Dell E7250 under Windows XP x64: a Device Present at Boot, and UAS With No Disk".


Goal:
- xhci98 corrects the E7250 BIOS's `D20IR` interrupt routing, so the xHCI's
  interrupts reach the ISR. Then a device present at boot enumerates, and a
  hot-plugged disk mounts, on the Dell E7250 (Intel Wildcat Point-LP,
  `8086:9CB1`).
- Polled event delivery (design record 15) keeps a controller working when
  its interrupt cannot be delivered or repaired: no legacy interrupt at
  all, or a line proven dead at run time.
- MSI on Vista and 7 bypasses PIRQ routing altogether wherever the OS
  offers it (36.15).
- Three items from SweetLow's `2.x` testing (GitHub issue 17): the USB
  device interface, the device's real speed in the hub queries, and
  polling rates a client chooses (36.17 to 36.19).
- UAS commands of 1 MiB by default, up to 2 MiB by a registry value, with
  the HCD's TDs grown only as far as measurement calls for (36.21 to
  36.27).
- Selective suspend, off by default (`XhciSelectiveSuspend` 0), with the
  first standby and hibernate resume readings and the 98 SE audio-load
  wedge re-measured (36.28 to 36.41).
- Taken in during the phase (36.42 to 36.48): the Intel port switchover
  off by default with a script to set it, composite devices as devnodes,
  the 98 SE hot-plug hangs, string descriptors through the door, an
  interface with surplus endpoint descriptors, and the system named in
  `XHCISNAP`'s and `XHCIQUAL`'s reports.

Status: opened 2026-10-07 (owner) from a user report against `2.2.0.0`,
the first report of the HCD on real hardware under Windows XP x64 (issue
12). The root cause was found on 2026-10-08 (36.2). Its branch and target
version are `2.3.0.0` (owner, 2026-10-08): the branch was `2.2.1.0` until the
phase grew to take the routing quirk, polling and MSI.

Why a phase: its only evidence was a machine nobody here had, until the
owner reproduced the report on the owner's own E7250. The fix writes a
chipset register.

How it got here:
- **Report.** Finding 1: a device present at boot fails "no slot", because
  Enable Slot is refused BUSY behind the start's No Op. Finding 2: a
  hot-plugged UAS reader shows no disk. About half of all events came from
  the backstop.
- **2026-10-07.**
  - An interim build, `2.2.0.1`, held a Finding 1 fix and diagnostics. It
    was withdrawn before it was sent, and its code was reverted. Its record
    stays in issue 12 section 3.2.
  - The owner reproduced Finding 2 under Windows 7 x64 through Microsoft's
    `usbstor.sys` (issue 12 section 3.1).
- **2026-10-08.**
  - The diagnostic build `2.2.0.2` (36.1) and its read on the E7250 (36.2)
    found the cause (issue 12 sections 2 and 3). The E7250's BIOS routes the
    xHCI's INTA in the PCH's `D20IR` (RCBA+3160) to PIRQA (GSI 16), while
    ACPI tells Windows the xHCI is on GSI 21 (PIRQF). The ISR runs only
    when the EHCI on the shared line interrupts.
  - Both findings, and the sluggish HID, follow from that.
  - Rewriting `D20IR` to PIRQF live fixed the mouse and mounted the stick.
  - The owner chose a permanent fix in xhci98 behind a registry value that
    is on by default for this machine and controller only. No separate
    tool. The policy follows a Codex review (issue 12 section 5.1).
  - 36.3 comes first. The rest are separate changes, each committed on its
    own.
  - Later the same day the owner moved polled event delivery (`POLL.*`, until
    then planned work) into this phase. Its automatic mode gains one rule:
    a line interrupt proven dead at run time moves to polling (record 15
    section 4.2). That replaces the stalled-event watchdog 36.4 held, and
    36.4 keeps only the BUSY retry.
  - The owner then added MSI on Vista and 7, for both architectures, as
    36.15 and 36.16. The builds and E7250 check moved to the end of the
    list.
- **2026-10-09.** The owner took three of issue 17's six items into this
  phase as 36.17 to 36.19, item 6 in both its halves (36.18). The closing
  task, the builds and E7250 check, was renamed `36.final` the same day
  (owner), so a task added later goes above it and nothing is renumbered.
  Commits before then call it 36.17, which is now the device interface
  task. The 98 SE silent install
  is planned work ("Issue 17 follow-up"). The `XhciIntelPortSwitch`
  default waits on the reporter's reason, and the UAS figures are a report.
  The owner tests these items and answers the issue after.
- **2026-10-09, later.** A Codex review of the plan at `7567dec` (2 P1, 10
  P2) was taken into the tasks: 36.12's order corrected to xHCI 4.6.8; the
  dead-line rule's observer and live transition left open for 36.5 (record
  15 section 4.2); 36.4 kept, with its rules; 36.3's PIC IRQs limited to
  the routable ones; 36.17's lifecycle, 36.18's `_EX_V2` sources and 36.19's
  SuperSpeed composite path made explicit; the docs and install legs moved
  here from SUSP.3 and SUSP.4 (36.20, 36.final). The owner then decided
  the six points it left open: MSI's modes (3 as 0, the line forced on
  Vista and 7 by `MSISupported` 0, a failed message connection polls),
  single-message MSI-X admitted, the `_EX` Speed 3 and 4 on 98 and ME
  only, the poller's timer as the dead-line watcher, mode 4's loss across
  `SELECT_INTERFACE` documented, and the composite interface on the
  first-interface function, following its state. Rounds 2 to 4 followed
  (`0bacda3`, `6cac234`, `ef7fbac`); round 4 was clean.
- **Where it stands (end of 2026-10-09).** The plan is reviewed and no code
  of this phase is written: 36.1 was reverted and 36.2 was a reading. Next
  is 36.3, the routing quirk, which settles its LPC configuration-space path
  first. 36.5 settles the WATCHING design and record 15's mode rules before
  any polling code. After testing, the owner answers issue 17 on GitHub;
  that reply says Speed 3 and 4 are 98 and ME only, and that the composite
  interface follows the first-interface function.
- **2026-10-09, evening.** The owner moved the 1 MB transfer plan, drawn
  up that day as planned work with a registry value reaching 2 MB and
  reviewed once, into this phase as 36.21 to 36.27, above 36.final.
- **2026-10-09, later.** 36.4 done. The owner then moved the selective
  suspend work (`SUSP.*`, planned work until then) into this phase as
  36.28 to 36.41, its docs into 36.20 and its cut and install legs into
  36.final, with `XhciSelectiveSuspend` now defaulting to 0, since off by
  default it changes nothing for a user who does not set it.
- **2026-10-09 and 10, during the run toward the `2.2.0.3` test build.**
  The owner took in 36.42 to 36.48 above 36.final: from SweetLow's
  comments on issue 17, the port switchover off by default (36.42),
  composite devices as devnodes (36.43), string descriptors through the
  door (36.45) and the surplus-endpoint mouse (36.46); from the legs, the
  98 SE hot-plug hangs (36.44); and the system named in `XHCISNAP`'s and
  `XHCIQUAL`'s reports (36.47, 36.48). Also decided: MSI on Vista and 7
  x86 through an NT 6-only helper driver (design record 18), mode 1
  forcing the line, a per-device `MaximumTransferLength`, and stage 2's
  `XhciWideMap` read for every target's HAL, then off by default
  (owner, 2026-10-10: 1 opts in).
  `2.2.0.3` is a local test build for the owner's bench; `2.3.0.0` is cut
  after it.

- [x] 36.1 **done 2026-10-08** (`3d667f1`); **reverted 2026-10-08 (owner) after its read**: the code and the `XHCISNAP` decoder are removed, the tree is back to `2.2.0.0`, and the description is kept as the record (issue 12 section 3.3) - the diagnostic build, numbered `2.2.0.2` (owner, 2026-10-07), on `2.2.0.0`'s code with no change in behaviour (issue 12 section 3.3):
  - per open pipe of every slot: its flags, queue and oldest outstanding transfer (URB function, setup bytes, length, age, TD address), with the Output Endpoint Context's state, TR Dequeue and DCS read at dump time;
  - a timeline of the last RESET_PIPE, ABORT_PIPE, RESET_PORT and cancel requests: arrival, completion, endpoint, the CLEAR_FEATURE outcome, the commands and their status, and the slow-IRP depth;
  - the interrupter's registers (USBSTS, IMAN, ERDP, IMOD, USBCMD) and the pending event before each backstop drain, with ISR and DPC times.

  `XHCISNAP` decodes all of it. Host tests, every gate, a Codex review loop.
- [x] 36.2 **done 2026-10-08** (issue 12 section 3) - the read (owner): `2.2.0.2` on the E7250 under Windows 7 x64 and XP x64, with the stick hot-plugged and present at boot, and with the EHCI disabled. Then RW-Everything reads of the PCI and PCH registers, and the live `D20IR` write. The root cause is found, and the fix (36.3) replaces the superseded check.
- [x] 36.3 the routing quirk, `XhciIntelIrqRoute` (REG_DWORD on the controller's software key; the full design, with the reviewed ID table, is issue 12 section 5.1):
  - **The values.**
    - **0**: no writes; report only.
    - **1** (default, also for missing, wrong-type or out-of-range values, logged): only xHCI `8086:9CB1` with subsystem `1028:062D` and LPC `8086:9CC3`.
    - **2** (opt-in, never the default): any reviewed xHCI/LPC pair regardless of subsystem. The pairs are 1E31, 8C31, 9C31, 8CB1, 9CB1 and 8D31, each with its exact LPC IDs (issue 12 section 5.1). It never admits unlisted Intel, 100-series and later (PCR/ITSS) or non-Intel.
  - **Writes only when all hold:**
    - a valid, enabled RCBA read from LPC `00:1F.0` F0;
    - D20IP agrees with the xHCI's INTA;
    - a line-based, unambiguous interrupt;
    - a target PIRQ derived from the raw IRQ, and D20IR's IAR differing from it:
      - **APIC mode** (raw IRQ 16-23): the target is IRQ-16;
      - **PIC mode** (raw IRQ 3-7, 9-12, 14 or 15; Windows 98 and ME, or NT without the APIC): the target is a PIRQ whose LPC `PIRQx_ROUT` (`60h`-`63h`, `68h`-`6Bh`) is enabled and names that IRQ. The current one is kept if it matches, else the lowest is taken. Those registers are read, never written. **Written on 98 and ME only** (owner, 2026-10-09, after the rule's review): on 2000, XP, Vista and 7 an I/O APIC HAL can hand out an IRQ below 16 too and its route is not proven, so there the target is recorded and nothing is written, at every value (`STEERED_NT`). The other values below 16 (0, 1, 2, 8 and 13) are reserved encodings in `PIRQx_ROUT`'s IRQ field (Intel 330837-002 10.1.17, and each pair's datasheet), name no routable interrupt, and are report-only; host vectors cover each of them.

    Then it writes `(old & ~7) | target` as a 16-bit write and reads it back.
  - **Which deliveries it serves:** every line delivery. That means Windows 98, ME, 2000 and XP, and Vista and 7 with `MSISupported` 0 in the controller's hardware key or without an MSI grant (36.15).
  - **Not active under MSI or polling** (owner, 2026-10-09): when the delivery in effect is a message interrupt (Vista and 7 with an MSI grant) or a poll (`XhciInterruptMode` 2, or mode 0 with no line), it writes nothing and only reports. It decides at each run, so a later start or D0 return on the line repairs the route then. A move to polling at run time (the dead-line rule, 36.7) does not undo a write already made. This replaces "under polling it still repairs, for a later start on the line".
  - **Report-only** when no target can be derived, or the raw value is outside both ranges. The PIC-mode rule (owner, 2026-10-08) replaces an earlier "report-only in PIC mode"; its own review, in 36.3's Codex loop, led to the 98-and-ME-only write above.
  - **The parser** keeps the raw interrupt resource (level, vector, flags, share) (`hc->IntRaw`).
  - **Reaching LPC config space:** settled as `HAL.dll!HalGetBusData` over bus 0, from offset 0 (`hcd_ctl.c` `hcdPirqSlot`), since `HcdSvcConfigSpace` reaches only the xHCI's own PDO. It is exported on every NT target and named in 98 SE's `ntkern.vxd` (`legal-provenance.md` section 4, static); `...ByOffset` has no Windows 98 evidence. Whether 98 SE's and ME's answer with real data is the `qemu` flavour's probe, a VM leg still owed. Not CF8/CFC.
  - **Placement:** in the HCD layer.
    - At start: after IoConnectInterrupt and `XhciInitController`, before `XhciEnableInterrupts`.
    - On each D0 return: before `XhciResumeController`.
  - **What is recorded:** what it found, decided and wrote, in the snapshot and the note ring, decoded by `XHCISNAP`.
  - **Tests:** host tests of the pure policy, the delivery included. The INF and `docs/using` document the value, modelled on `XhciIntelPortSwitch`; copy its strict DWORD reader, not its "2 = unlisted Intel" meaning. `docs/using` says plainly that the value does nothing on Vista or 7 with MSI, or under polling.
  - **Done 2026-10-09** (`b71bd64..95b2c31`, the Codex loop clean at its fourth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `86db9a3`). Owed, in VM legs: the `qemu` flavour's probe on 98 SE and ME (`HalGetBusData` and the USHORT register accessors answering there, not only resolving); 2000, XP, Vista and 7, x86 and x64; a start the lower driver refuses, for the record's reset site. The E7250 itself is 36.final. **Legs run 2026-10-09/10** (worker p36-misc, development host A, QEMU 11.1, `qemu` flavour at `ebe3ec9` and `04ef848`): the probe on 98 SE, ME, 2000, XP, Vista and 7, x86 and x64, read `pirq.qemu.device` 3, the xHCI's own slot (00:03.0), so `HalGetBusData` answers with real bus-0 data on 98 SE and ME too; `pirq.qemu.lpc.bytes` 2 (00:1F.0 absent), decision `NOT_REVIEWED`, no write, devices working; `XhciIntelIrqRoute` 0 and 2 on 7 x86 the same. Still owed: the USHORT register accessors executing on 98 and ME (no QEMU path reaches them: they run only on a reviewed Intel PCH), and a start the lower driver refuses.
- [x] 36.4 the bounded BUSY retry: `hcdCommand` on `XHCI_CMD_BUSY` waits a bounded time for the engine to go idle, asking for a drain, and retries under one deadline, instead of failing the port at once. This is the start No Op case of issue 12 Finding 1. A small change of its own; the stalled-event watchdog that 36.4 also held is replaced by polling's dead-line rule (36.5, 36.7).
  - **Still needed after 36.3, polling and MSI** (Codex review of `7567dec`, and the owner's reading). They deliver completions; none guarantees that the start's asynchronous No Op has completed before the next submit, and `XhciCommandSubmit` also answers BUSY during an abort (`xhci_cmd.c` ~1004). Its remaining cases: a dead line inside the window before the dead-line rule moves to polling; mode 1, which never moves; a completion waiting for the next poll at the idle period; and any transient BUSY, which is never a device's failure.
  - **The rules.** One deadline across waiting and resubmission; a completion counts only by its own TRB, as now; the retry stops on controller failure or a change of reset generation; it resubmits only once the engine reads idle. It complements the dead-line detector and does not replace it.
  - **Leg.** A `qemu`-flavour delay of the start No Op's completion, so BUSY is met deterministically with a healthy line: the device present at boot enumerates.
  - **Done 2026-10-09** (`9a8f32e..9dca404`, the review loop clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `9dca404`). `hcd_enum.c`'s `hcdCommandBusy`, its look the pure `XhciEnumBusyStep` (`test_enum`); the drain `HcdCommandDrain` under the backstop's admission; the waiter's marks cleared in the same controller-lock hold that reads the engine idle (`hcdCommandArm`), which also closes an older race where an earlier command's loss was taken for the next one's. The aid is the `qemu` flavour's `XhciQemuStartHold`, held to 3000 ms (`build-and-test.md`, "The qemu flavour's test aids"). Owed, in VM legs: the leg above on 98 SE and 2000 at least, reading `enum.cmd.busy.end` and no `cmd.timeout` after the hold; a hold with no device present, read as `qemu.inj.starthold.expired`. **Legs run 2026-10-09/10** (worker p36-misc, development host A, QEMU 11.1, `qemu` flavour): on 2000 and 98 SE a hold of 2000 ms with a stick present read `qemu.inj.starthold.released`, `enum.cmd.busy.end` 09000002 and no `cmd.timeout`, the stick working; the stop's counter block (DebugView) read `enum.cmd.busy` and `enum.cmd.busy.cleared` 1 each per hold start (cumulative on 98, which keeps the driver loaded across a disable); with no device, `qemu.inj.starthold.expired` 1 and a stick hot-added after it working. Passed.
- [x] 36.5 (was POLL.0) the polling design: record 15 reviewed to convergence before code (the owner's decisions of 2026-10-05 are its section 12), with its amendment for issue 12 reviewed too (section 4.2, decision 6):
  - automatic mode (0) also moves to polling when a line interrupt is proven dead at run time;
  - **two gaps in that amendment, settled before code** (Codex review of `7567dec`):
    - **the detector.** Section 4.2 proved a line dead by repeated backstop looks, but `hcdBackstop` runs on `hcdThread` (`hcd_ctl.c` ~898), which `hcdCommand` blocks waiting for the very completion that never arrives (issue 12 section 2). **Decided (owner, 2026-10-09):** the poller's own timer and DPC watch instead. Under every line delivery in mode 0 the poller runs in a slow WATCHING state that peeks `IMAN`, `USBCMD.INTE` and the event ring and never drains; it runs whatever `hcdThread` is doing, command waits and `XhciTolerance` 0 included. The record settles its period, count and window;
    - **the transition** follows from that: proving the line dead is the poller's own move from WATCHING to in service, under `PollLock`, with the interrupter masked first, the ISR admitting nothing after, and a concurrent close. Section 5.2 gains WATCHING and that move, since today it lets the poller leave CLOSED only at a start under a poll delivery;
    - **WATCHING is a role, not an owner state** (Codex review of `6cac234`). Section 5.2's states say who owns the timer (PARKED, ARMED, FIRING, CLOSING, CLOSED); WATCHING says what the callback does while it owns it. A start whose effective mode is 0 under a line delivery opens the poller (CLOSED to PARKED, then the kick) in the watch role, so the ownership rules, the hardware admission under the controller lock and `PollService`, park and kick across D3 and the in-place recovery (section 5.3), and the close apply to a watcher unchanged. "Mode 0" is the effective mode: 0, 3, and a reserved value run as 0. Host vectors: a watcher's close against its expiry, and a watcher across recovery and D3;
    - **record 15's mode rules reconciled before code**, in this task, not in 36.20: section 1's "not in this design" for MSI, section 3's mode 3, section 4's table (written for a line resource), and section 11's expectation that modes 0 and 1 use the line on Vista and 7, all to 36.15's decisions;
    - and the legs gain a line dead from the first No Op and one that dies during a command wait;
  - the dead-line predicate (`IMAN.IP` set, nothing claimed) stays line-only: xHCI 4.17.5 clears `IP` itself after an MSI or MSI-X write, so it cannot detect a lost message;
  - the hardware statements of its section 4.1 (`IMAN.IP`, `USBSTS.EINT` and `ERDP.EHB` with the interrupt enables clear) checked against `xhci-data-structures.md`
  - **Done 2026-10-09** (`a744811..f7f0907`, record 15 revision 5; the Codex loop converged at its eighth round, section 13.5). Settled: the watcher (100 ms, three stale pairs over at least 300 ms, `PollServiceGen`), the move and the line's retirement under `KeSynchronizeExecution` with its containment (PCI Interrupt Disable, then the recovery), WATCH and SERVE as roles with `ParkPending`/`KickOwed`, the grants after D0 under the power gate, the period's inputs and the doorbell hook, and 36.15's design. MSI's x86 route: no safe way was found for the x86 binary alone, and the owner decided (2026-10-09) on an NT 6-only x86 helper driver, designed separately; amd64 calls `MmGetSystemRoutineAddress`; both feed one connection seam (36.15, 36.16 and the decisions table amended).
- [x] 36.6 (was POLL.1) the three values (record 15 section 3):
  - `XhciInterruptMode` 0: 0 automatic, 1 the line interrupt, 2 polling, 3 the same as 0 (MSI where granted is already 0's first choice, 36.15), 4 and above reserved and run as 0;
  - `XhciPollIdleMs` 50, held to 5000;
  - `XhciPollActiveMs` 1, held to the idle period;
  - neither period 0 (owner, 2026-10-05).

  They are read at each start with the out-of-range rules, and written by both INFs on every install path under 34.1's don't-overwrite rule, with the INF gate's `VAL-*` rows and the footprints. 36.final's install and update legs read them
  - **Done 2026-10-09** (`158ad18..53cb9e2`, one Codex loop over 36.6 to 36.8, clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `53cb9e2`). `XhciInterruptMode`, `XhciPollIdleMs` and `XhciPollActiveMs` read at each start (`hcdSettleDelivery`, `XhciPollValues`), in both INFs on every path with the gate's value rows, footprints and mutations. Owed: 36.13's VM legs (no guest was run by this task), and 36.9/36.10's tool work.
- [x] 36.7 (was POLL.2) the delivery and the poller (record 15 sections 4 and 5):
  - the delivery and its reason settled at start and kept through recovery and resume;
  - mode 0's run-time move from a dead line to polling (section 4.2), kept the same way, with the delivery and its reason in the counter block updated at the move, not only set at start;
  - `ResourcesTypes` truthful, and the core's interrupt-bit and pin-0 gates applied to a line delivery only;
  - under a poll delivery: the ISR unconnected, or claiming nothing after a run-time move; `XHCI_EXT_FLAG_INTERRUPTS` never set; every poll drain passing no re-arm;
  - `PollTimer` with its own DPC, so polling does not depend on `hcdThread`, which can be blocked in a command wait (issue 12 section 2);
  - `IsrDpc` queued by the peeks, `PollLock` admission, and close by `KeCancelTimer` with the timer DPC retiring itself;
  - park and kick across D3 and the in-place recovery
  - **Done 2026-10-09** (`158ad18..53cb9e2`, one Codex loop over 36.6 to 36.8, clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `53cb9e2`). `hcd_poll.c` and `xhci_poll.c` (`test_poll`): the delivery settled before connecting, `hcdConnect` as MSI's seam, the poller and its WATCH and SERVE roles, the dead-line move with the line's retirement and containment, the grants after D0, park and kick. Owed: 36.13's VM legs (no guest was run by this task), and 36.9/36.10's tool work.
- [x] 36.8 (was POLL.3) the adaptive period and the opportunistic checks (record 15 sections 6 and 7.1):
  - **The pure core:** `xhci_poll.c` with host suite `test_poll`. It covers ACTIVE, PERIODIC with interrupt IN and OUT, IDLE, the hysteresis and bounded-drain clauses, the clamps, promotion and the stale-decision rule.
  - **The peeks:** under the controller lock, after a doorbell, after a drain, on each thread pass, health poll and frame DPC. Each queues the drain and never drains inline; poll delivery only.
  - **The counters:** the observed interval, and the delivery in effect with its reason (automatic, forced, no interrupt, or line not delivered). They are in the counter block and in the start log line (a run-time move logs its own line). `XHCISNAP` reads them from `XhciLogVerbosity` 1, and the DebugView sink emits them from 2 with `XhciLogDebugView` set (owner, 2026-10-05; section 8)
  - **Done 2026-10-09** (`158ad18..53cb9e2`, one Codex loop over 36.6 to 36.8, clean at its fifth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `53cb9e2`). The adaptive period, the peeks and the doorbell hook, and 33 counter words appended to `XHCIHC_COUNTERS` (63 to 96) with their log names. Owed: 36.13's VM legs (no guest was run by this task), and 36.9/36.10's tool work.
- [x] 36.9 (was POLL.4) `XHCISNAP` (record 15 section 9):
  - the three values in the companion, as `XhciImodInterval250ns` is;
  - the delivery in effect and its reason;
  - the moderation value said to have no effect when polled;
  - the poll counters;
  - the coherence wording for drains polled or not (the tear detector itself already counts them);
  - the header grown under the schema rule, with older headers still read
  - **Done 2026-10-10** (`fe2dcdc`, review fixes `e7d0da8`, `1de1913`; one Codex loop over 36.9 and 36.10, clean at its fourth round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `9935572`, `XHCISNAP`'s selftest 13 cases). Counter words 63 to 95 named and decoded in an "event delivery" section (the three values with their range rules, delivery and reason, pin, the moderation note under polling, poller, watcher, move, retirement); HCD header words 202 and 203 place the six extension words (header 204 words, version unchanged; `test_snap`); one layout check (`hcd_shape_ok`) before every HCD-derived reader; a 63-word block and a 202-word header still read. The transfer-size words of 36.21/36.22 were already decoded; 36.11 and 36.12 added no words. Read in QEMU on development host A on the `release` packages: Windows 98 SE mode 0 (the line, automatic, watcher looking) and mode 2 (polling, forced, poll runs and the observed interval, the moderation note), Windows 7 x64 mode 0 (the line, automatic), every counter word named.
- [x] 36.10 (was POLL.5) `XHCIQUAL`'s polling verdicts (record 15 section 10):
  - `Interrupt Pin = 0` no longer disqualifies: `quick_classify` and `quick_reason` say the controller needs polling. The other disqualifiers stand.
  - C4's "event in the ring, no interrupt" outcome reads "needs polling". Its tool failures, and a controller that fails even polled, do not.
  - The polled tests run after the interrupt hook is removed.
  - `main.c`'s C6/C8 gate and `report.c`'s verdict and exit code learn the outcome, with vectors through the verdict, not only `test_mmiodiag`.
  - **Its own verdict** (owner, 2026-10-09): a controller with no working interrupt (pin 0, or C4's "event in the ring, no interrupt") that passes the rest is **QUALIFIED WITH CAVEATS** - not plain QUALIFIED, not "QUALIFIED (with warnings)", not a failure, not provisional - with its own exit code, 3. It prints the caveats: the driver polls (`XhciInterruptMode` 0 or 2: more wake-ups, latency up to the poll period), or an MSI-capable OS; and from the PCI capability list (05h MSI, 11h MSI-X) whether Vista and 7 can take a message interrupt (x64 directly, 32-bit with the `xhcimsi.sys` helper) or polling is the only route. Warnings, if any, beside it. Record 15 section 10, `xhciqual/README.md`, `hardware-testing.md` and the release notes say so.
  - **Done 2026-10-10** (`d1bbaa9`, review fixes `e7d0da8`, `1de1913`, `9935572`; the Codex loop shared with 36.9, clean at its fourth round; `XHCIQUAL` built, host suites passing with the new `test_verdict` (80 checks), the QEMU matrix 46 cases and the Win98 batch harness 5 cases passing on development host A). `xhciqual/verdict.c` (pure, out of `report.c`): C4's PASS, POLL or FAIL; the C6/C8 gate; QUALIFIED WITH CAVEATS with its caveats and exit code 3; the quick scan's LOOKS QUALIFIED WITH CAVEATS. A controller whose IP was never seen is told `XhciInterruptMode` 2 (automatic mode's watcher needs IP); its MSI route is said as untested. The matrix reaches POLL through `SETIL05`/`SETILFF.COM`, which rewrite the xHCI's Interrupt Line before the run (IRQ 5, which nothing drives; FFh, none), reading C4 POLL, the verdict and exit 3. Orchestrator decision, owner confirmed 2026-10-10: an unusable Interrupt Line and a stuck PCI Interrupt Disable also read "needs polling" when the command completes polled. Owed to the bench: a real pin-0 or dead-line controller under DOS (36.14 / 36.final).
- [x] 36.11 `XHCIQUAL`'s routing report, read-only:
  - It reports the LPC identity, RCBA, D20IP and D20IR, the selected PIRQ, and the PIC destination and its disable bit.
  - It flags a mismatch only against a GSI supplied separately.
  - **Only on the reviewed RCBA chipsets** (the xHCI/LPC pairs of issue 12 section 5.1: 7, 8 and 9 series and C610/X99). The registers do not exist elsewhere:
    - 100 series and later, and the Apollo Lake-class Atom SoCs, route through ITSS behind P2SB;
    - Bay Trail and Braswell route through the iLB;
    - non-Intel controllers have no equivalent.

    Elsewhere it reports "routing registers not applicable" and reads nothing. Support for ITSS is not in scope.
  - **Done 2026-10-09** (`9c8a5ec`, `4848c08`, the review loop clean at its second round; `XHCIQUAL` built, its host suite passing with the new `test_route`, the QEMU matrix 42 cases passing on development host A). `xhciqual/route.c` (pure) and `pci.c`'s `route_read`, the reviewed pairs taken from the driver's own `src/xhci_pirq.c`; read before any active test in every mode, reads only. The GSI is `--gsi N`, accepted only beside `--quick` or `--probe-only`, so it never starts an active run; an IRQ below 16 is compared on the 8259 path alone, as DOS found it, and says that it proves nothing for an NT APIC HAL. No guest has these registers (QEMU reads "not applicable"); a reading on a reviewed chipset, the E7250 under DOS, is owed to the bench (36.final).
- [x] 36.12 RESET_PIPE in xHCI 4.6.8 order in `hcdCfgReset`. It is a spec fix, kept apart so it does not muddy the interrupt tests (issue 12 section 3.2).
  - **The order** (xHCI 1.2c 4.6.8, p.116, "reset a pipe"): Reset Endpoint (TSP 0); then `CLEAR_TT_BUFFER` to the hub, when the device is behind a TT and the endpoint is control or bulk (`HcdHubClearTt`); then `CLEAR_FEATURE(ENDPOINT_HALT)` to the device, for any endpoint but control; then Set TR Dequeue past the failed TD; then the doorbell. Today the device's clear comes first, as usbport ordered it (`hcd_cfg.c` ~2875).
  - **Halted and not.** This order is the Halted endpoint's. A non-Halted one keeps today's path, brought to Stopped and given a fresh context (`hcdCfgQuiesce`, `hcdCfgRecycle`), since Reset Endpoint is refused on it.
  - **A device clear that fails.** A successful Reset Endpoint has already reset the host's toggle and moved the endpoint from Halted to **Stopped** (4.6.8, pp.115-116), and 4.6.8 warns that the endpoint's behaviour is undefined if the device's toggle is not reset too. So a clear that stalls, fails or times out does not ring the doorbell and resume the pipe, as today's failure branches do (`hcd_cfg.c` ~2886). The pipe stays paused, the client gets the failure, and the pipe records that the host's half is done and the device's is owed.
  - **The next RESET_PIPE** reads that record, not only the Endpoint Context: Reset Endpoint would now be refused with Context State Error, since the endpoint is Stopped, not Halted. It sends the TT clear and the device clear again, then Set TR Dequeue (legal on a Stopped endpoint), then the doorbell. A pipe with no such record follows the order above if Halted, and today's quiesce and recycle if not.
  - **The record's lifetime** is the endpoint context's. It is cleared when a RESET_PIPE completes both halves, and whenever the context is replaced: `RESET_PORT`'s rebuild, which keeps the pipe objects and handles but builds new contexts (`hcdCfgResetPort`, `hcd_cfg.c` ~3089-3173), the endpoint recycle, a new select, and the device's invalidation. A record never outlives the context it describes.
  - **A device clear that times out** is not this path. It sets `Ep0Stuck`, taints the scratch and asks for the controller's recovery, which invalidates the device (`hcd_enum.c` ~1104-1113); that containment stays as it is, and the pipe is not retried on its old context.
  - **Tests.**
    - the same pipe: a device clear that stalls, and one that fails, each followed by a RESET_PIPE that succeeds without a second Reset Endpoint;
    - a failed clear, then `RESET_PORT`, then a RESET_PIPE that takes the Halted order or the recycle, not the owed-half path;
    - a device clear that times out, read as the recovery and re-enumeration it is today;
    - a TT-attached bulk endpoint, read for the TT clear's place in the order; a non-Halted endpoint.
  - **Done 2026-10-10** (`3c0fe00`, `0953831`, `04ef848`, the review loop clean at its second round; built x86 and `-amd64`, all flavours, every gate and the host suites passing at `04ef848`). `hcdCfgReset` takes its steps from the pure `XhciPipeResetSteps` and its clear's verdict from `XhciPipeClearVerdict` (`test_pipe`, which carries the order, the owed path, a failed clear on each type, and the not-Halted path); the owed record is `HCD_PIPE.ClearOwed`, holding one pause, ended by `hcdCfgOwedEnd` (both halves done, any successful Configure Endpoint that adds the endpoint, or a state other than Stopped). SYNC_CLEAR_STALL on an owed endpoint finishes the reset; SYNC_RESET_PIPE answers success and leaves it owed (round 1, finding 1). The `qemu` flavour's fault 13 answers the next device clear (`build-and-test.md`). **Legs** (2026-10-09/10, development host A, QEMU 11.1): on 2000 and 98 SE, fault 08 with `arg` 4 on the HID pointer, with fault 13 (STALL, failed, never sent) and without, reached RESET_PIPE only after the class driver's abort had reset the Halted endpoint, so every reset took the not-Halted path (the clear refused, the request failed, the pipe resumed, the mouse working after each): the Halted order and the owed path are covered by the host vectors alone, and QEMU's `usb-mouse` STALLs every CLEAR_FEATURE(ENDPOINT_HALT). A TT-attached bulk endpoint and a clear that times out cannot be made on QEMU. The `release` regression on 2000 and 98 SE with a mouse and two sticks passed (one 98 SE shutdown hang at "Windows is shutting down", not reproduced on a repeat). Owed: a vehicle whose class driver resets a Halted endpoint with nothing outstanding, or real hardware.
- [x] 36.13 (was POLL-V) the polling legs (record 15 section 11):
  - **Guests:** mode 2 on 98 SE and 2000 (SMP, Driver Verifier) first, then ME, XP, Vista and 7, x86 and x64.
  - **Devices and actions:** HID, Bulk-Only and UAS storage, composite audio playing, behind QEMU's hub, hot-plug, disable and enable, remove and rescan.
  - **The no-interrupt start,** made deterministic by a `qemu`-flavour switch: mode 0 polls with its reason, and mode 1 fails as today.
  - **The dead-line move:** a `qemu`-flavour line that delivers nothing after start, under mode 0, read as the move to polling with its reason; under mode 1, as no move.
  - **Recovery and power under polling:**
    - a forced recovery;
    - the controller's D3 and back, where a guest offers one without system standby. Where none does, the forced recovery is read in its place (park and kick only where the poll timer's DPC reads the service failed or revoked during it; corrected 2026-10-10, wave 3 matrix M-2), and a disable and enable reads the close (record 15 section 11).
    - system standby and hibernate under polling are 36.28's.
  - **The races, on the SMP guest:** promotion against re-arm, close against peeks and expiry, a bounded drain continued.
  - **Measurements:** the period each kernel gives, from the counters; line against poll on the same guest (storage copy rate, audio continuity, idle CPU load).
  - **Regression:** modes 0 and 1 on a pin reading as the release before it.
  - **Visibility:** the delivery and its reason read in `XHCISNAP` and in DebugView for each of modes 0, 1 and 2, for the no-interrupt start and for the dead-line move.
  - x86 and amd64, and every gate
  - **Read 2026-10-10, not done** (`runs/run-36.md`, "36.13"; fixes `5bca7bd`, `824a6c7`). Every leg on `bfec7b0` passed on 98 SE, ME, 2000, the 2000 SMP guest under Driver Verifier, XP, XP x64, Vista and 7 x86 and x64, except the dead line under mode 1: a command or EP0 wait expired into a reset, since the backstop runs on the waiting thread. Fixed: the waiting thread queues the drain itself at `XhciTolerance` 1 (`enum.wait.drains`); one Codex loop, clean at round 2; built x86 and `-amd64`, every gate and the host suites passing at `824a6c7`. Read on the fixed build on 2000 and 98 SE. Owed to the matrix: the fixed build's dead-line legs on XP, XP x64 and 7 x64, and its `release` smoke on 7 x64.
  - **Done 2026-10-11** (`runs/run-36.md`, "Matrix (final, stamped 2.2.0.3)" and "Re-read on the stamped 2.3.0.0"): the fixed build's dead-line legs on XP, XP x64 and 7 x64 (E-1/E-2, P3/P4) and the `release` smoke on 7 x64 passed; 98 SE's E-4 stopped in the matrix (fixed by 36.55) and passed in the re-read.
- [x] 36.14 (was POLL-E) the polling bench (owner):
  - mode 2 on the E460 and the P14s Gen 1 under Windows 98 SE;
  - an MSI-only controller if one is at hand (the synthetic no-interrupt leg does not wait for one);
  - the E7250 under XP x64 with `XhciIntelIrqRoute` 0 and mode 0, read as the dead-line move to polling.
  - **Windows half done 2026-10-11** (owner; `runs/run-36.md`, "The owner's bench, 2026-10-11"): mode 2 on the E460 under 98 SE, about 4 GB through a SuperSpeed stick, compared clean; the E7250 under XP x64 with `XhciIntelIrqRoute` 0 and mode 0 moved to polling at run time, the line's retirement proven by a masked acknowledgement, and a SuperSpeed stick copied clean (the poll ran every 15 to 62 ms there). Bench: not read (owner, 2026-10-11): mode 2 on the P14s Gen 1, and no MSI-only controller was at hand. The DOS half, a line dead under DOS, cannot be read on the E7250, whose line works in PIC mode: **moved** to "Planned - bench follow-up after `2.3.0.0`" (BF.4).
- [x] 36.15 MSI on Vista and 7, x86 and amd64 (owner, 2026-10-08; x86 through a helper driver, owner, 2026-10-09), designed as a record 15 amendment (section 4.3) and reviewed before code:
  - **Why.** A message-signalled interrupt bypasses PIRQ routing, so a BIOS routing fault like the E7250's cannot affect it. It is also how an MSI-only controller gets real interrupts on NT6 instead of polling. Windows 98, ME, 2000 and XP have no MSI, so they keep the line, the routing quirk and polling.
  - **The INF.** `MSISupported` 1 under `Interrupt Management\MessageSignaledInterruptProperties` in the controller's hardware key, with a message limit of 1 (Interrupter 0 only). The Vista and 7 install paths only (`Xhci.Dev6.NTamd64`; and `Xhci.Dev6.NTx86` with the helper's boot-start service and `Reboot`, design record 18 section 7); earlier systems ignore it.
  - **The resources.** `hcdParseResources` takes a message interrupt (`CM_RESOURCE_INTERRUPT_MESSAGE`) as well as a line one. Windows may still grant the line, so both paths stay supported.
  - **The connection.** `IoConnectInterruptEx` (`CONNECT_MESSAGE_BASED`, with `CONNECT_LINE_BASED` or `CONNECT_FULLY_SPECIFIED` for a line) and `IoDisconnectInterruptEx`, on Vista and later only.
    - **The single x86 binary** also loads on 98, ME, 2000 and XP, so it must not import these routines. `MmGetSystemRoutineAddress` is absent on 98 and ME too.
    - (Superseded: "so both architectures resolve them at run time by a bounded walk of the kernel image's export table".) **Amended in 36.5's review, then decided by the owner (2026-10-09; record 15 section 4.3).** No way was found for the one x86 binary to reach `IoConnectInterruptEx` safely by itself (a walk of kernel memory cannot prove its pages mapped; a parse of the kernel file proves its imports' places, not the target's). So amd64 imports `MmGetSystemRoutineAddress` (exported by every amd64 target), and x86 gets the pair from `xhcimsi.sys`, a small NT 6-only x86 helper driver installed only by the Vista and 7 x86 INF section, over a named device opened with `IoGetDeviceObjectPointer` (the x86 image's one new import, with stock Windows 98 SE precedent; owner, 2026-10-09) and a versioned internal device control (design record 18). Both feed one connection function (`hcdMsgConnect`). Without the helper, the requirements filter (`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, record 18 section 4.2) keeps only the line configurations, so Vista and 7 x86 keep the line, the routing quirk and polling, as XP does. When the routines are not found, a line resource connects through `IoConnectInterrupt`; a message resource has no line behind it, so that start polls (below), or fails under mode 1 on x86 with no line offered.
    - WDK 7.1's `iointex.lib` (`WdmlibIoConnectInterruptEx`) was considered for amd64 and not taken: no further third-party object in the amd64 image, and since 36.5's review amd64 calls the documented `MmGetSystemRoutineAddress` the library wraps (record 15 section 4.3).
  - **The ISR.** A message ISR: EINT then IP acknowledged and the drain queued on a claim, as now. It returns `XhciIsr`'s answer: Windows permits shared message interrupts, so FALSE stays the answer for an interrupt this controller did not raise (record 15 section 4.3, corrected in 36.5's review; this line said "an MSI is never shared" until then).
  - **Decided (owner, 2026-10-09)**, after a Codex review of `7567dec`:
    - **The modes on Vista and 7.** 0: MSI where Windows granted a message, else the line, else polling. 3: the same as 0. 1: **the line, forced by itself** (owner, 2026-10-09; design record 18 section 4.3a): the requirements filter keeps only the configurations offering a line, on x86 and amd64, so the start receives the line; where a message is granted anyway (no line offered, the filter disabled or not replacing the list) it runs as 0 and logs that the line was asked for - except on x86 with no helper bound and no line offered, where the start fails, strictly, as XP's forced line with no line does (owner, 2026-10-09). 2: polling.
    - **Forcing the line** on Vista and 7 is mode 1 (above), or `MSISupported` 0 in the controller's hardware key, documented in `docs/using`.
    - **A message connection that fails**, or a message resource with no `IoConnectInterruptEx` found, polls for that start with its reason; there is no line to fall back to. The one exception is mode 1 on x86 with no helper bound and no line offered (above); a bound helper's failed connection still polls.
    - **MSI-X with one message is admitted.** The message resource does not tell it from MSI, and QEMU offers both. The driver reads which capability is enabled in the controller's configuration space (MSI or MSI-X) and records it, so a leg shows which one ran. More than one interrupter stays out. `CONNECT_MESSAGE_BASED` takes no message count, so a grant of more than one message (a raised `MessageNumberLimit`) is connected and served as one: only Interrupter 0 is enabled, so only its message is ever written, and the count granted is recorded (record 15 section 4.3, 36.5's review).
    - **The dead-line rule is the line's only.** xHCI 1.2c 4.17.5 clears `IMAN.IP` once a message write completes, so its predicate cannot show a lost message; a message delivery is not watched.
    - **The routing quirk** is not active under a message delivery (36.3).
    - **XP x64** ignores the key and keeps the line.
  - **What is recorded:** the delivery (MSI, MSI-X, line or poll) and its reason, in the counter block, the start log line and `XHCISNAP`.
  - **Done 2026-10-10** (design record 18 revisions 1 to 12, record 15 section 4.3; merged `6f5d3ea` and `7d3b454`; 36.53 later made mode 1 the line only and 3 the message only): MSI on Vista and 7, x64 directly and x86 through `xhcimsi.sys`. Read on the owner's E7250 under Windows 7 x64: MSI, the routing repair writing nothing, BackstopDrains 0. MSI-X on real hardware: not read, "Planned - bench follow-up after `2.3.0.0`" (BF.6).
- [x] 36.16 the MSI legs:
  - **Design record 18's legs, first**: R (the requirements list each guest's PCI driver offers, x86 and x64, read with the strip off: R1 and R2 decide whether it is enabled), R3 (the unsigned boot-start helper loads), R4 (the device before the restart the INF asks for); then F (mode 1 with `MSISupported` 1 read as the line, x86 and x64), H (the helper missing, broken, old; H1a mode 1 with no line offered failing the start), L (two controllers, rebalance, surprise removal, a failed start), U (updates, the downgrade) and D (98 SE's load of the new import - a required check -, ME, 2000, XP, the x64 package unchanged, Driver Verifier).
  - **R read (2026-10-10, QEMU, development host A; record 18 section 11.1):** Vista SP2 and 7 SP1, x86 and x64, all three `qemu-xhci` variants offered the same list: one configuration of the memory BAR and its alternative, a device-private (the device's own: Windows keeps it with `MSISupported` 0 too), then the message heading the interrupt run with the line as its alternative. A line is offered beside the message everywhere, so the strip is enabled (`HCD_MSI_STRIP_DEFAULT` 1) for that one admitted shape; any other shape is kept as "unsupported shape". R3: the unsigned boot-start helper loads on an ordinary boot on both x86 guests. R4: before the install's restart Vista and 7 x86 hold the controller unstarted (problem code 28 on 7, 1 on Vista, not 14); x64 asks no restart and starts at once. M (MSI, MSI-X and the default on all four; the helper disabled on x86 polls, then binds again) and D (98 SE, ME, 2000, XP: the line, no helper, the new import loads on 98 SE) passed.
  - **QEMU** (`qemu-xhci` offers MSI and MSI-X): Vista and 7 x64, and x86 with the helper driver, with HID, Bulk-Only and UAS storage, and audio, under mode 0 and mode 3. Also disable and enable, remove and rescan, a forced recovery, and the controller's D3 and back. These run three ways, because the launchers' `qemu-xhci,p3=0` (`scripts/qemu-nt6-common.ps1` ~253) leaves QEMU's default, under which `qemu-xhci` offers MSI-X and not MSI (Codex review of `6cac234`, from QEMU's `hw/usb/hcd-xhci-pci.c`; confirmed on the guest before the legs), and Windows prefers MSI-X where both are offered: `msi=on,msix=off`, read as MSI; `msi=off,msix=on`, read as MSI-X; and the launchers' default, read as whatever Windows granted. `XHCISNAP` must name each correctly. Then `MSISupported` 0, read as the line; mode 1 under a grant, read as the message with its log line; and a `qemu`-flavour failed message connection, read as polling with its reason. Mode 1 under a grant reads as the line once the filter is enabled, and as the message with its log line where no line is offered.
  - **Down-level:** 98 SE, 2000, XP x86 and x64, and Vista and 7 x86 without the helper, reading as the release before it, with the line (on Vista and 7 x86 through the filter); where no line is offered there, a message grant reads as polling with "message not connected", or a failed start under mode 1. The x86 binary loads on 98 and ME with the new import.
  - **The owner's E7250** under Windows 7 x64, with `XhciIntelIrqRoute` 0, read as MSI with the mouse and the stick working.
  - **The owner's E460 and P14s Gen 1** under Windows 7 if installed, read as MSI.
  - **Done 2026-10-11** (record 18 sections 11.1 and 11.2; `runs/run-36.md`, the final matrix's M1-M4, F3, H2/H3 and U1/U2, and the bench): every leg above but two, which no guest can make: L2's rebalance (no Resources tab offered; host-tested) and a true surprise removal (QEMU's `device_del` is an orderly eject). The E7250 under Windows 7 x64 read as MSI at the routing default (decision MESSAGE, no write, as route 0 would read), and as the line under mode 1 with the repair rewriting the route. The E460 and the P14s Gen 1 had no Windows 7 on this bench. MSI-X on real hardware: "Planned - bench follow-up after `2.3.0.0`" (BF.6).
- [x] 36.17 `GUID_DEVINTERFACE_USB_DEVICE` (`{A5DCBF10-6530-11D2-901F-00C04FB951ED}`) on every device the bus presents (issue 17 item 3):
  - **Where.** Every device PDO and every external hub's PDO, as usbhub registers it; not the root hub, on which usbport registers none.
  - **A composite the bus splits** has no parent devnode, so the interface goes on one function's PDO: the function that owns the device's first interface (its `FirstInterface`, the group identity the door already uses for the connection's driver key, design record 13 section 8.10), not a hard-coded `MI_00`. Functions start and stop independently (`hcdPdoFunctionRelease` leaves siblings running). **Decided (owner, 2026-10-09):** the interface stays on that function and follows its state, so when it has no driver, fails its start or is disabled, the device has no interface while its siblings run. This is documented in record 13 section 8.10 and `docs/using`.
  - **The lifecycle** (`IoSetDeviceInterfaceState`'s contract):
    - registered once, and enabled after a successful start;
    - disabled at stop, at surprise removal, and at removal, never twice: a removal after a surprise removal finds it already off;
    - the string freed, idempotently, on every path that destroys the PDO: the normal remove, the retained PDO after a disable, and Windows 98's deferred deletion (`hcd_pdo.c` ~504, ~2317, ~2532), never only at `IoDeleteDevice`;
    - enabled again when a retained PDO is started again.
    - Best effort, as `HcdDoorHubStart`, `HcdDoorHubStop` and `HcdDoorHubRemove` are for the hub interface: a failed registration is a missing interface, not a failed device.
  - **Legs.** On every guest, x86 and amd64:
    - a SetupDi enumeration of the class: a device, a hub and a device behind it, a split composite;
    - arrival and removal notifications for the class (`RegisterDeviceNotification` with `DBT_DEVTYP_DEVICEINTERFACE`, on 98 and ME as on NT) across a hot-plug, a surprise unplug and replug, a disable and enable of one device in Device Manager, and a disable and enable of the controller;
    - a split composite whose representative function is unbound or disabled while a sibling works, read as no interface, as documented.
  - **Done 2026-10-10** (`465825d..b489836`, design `5b073c3`; the Codex loop clean at its fourth round; ticked at the integration of wave 2 on the i17 worker's recommendation, which the owner accepted). The interface sits on the group-first PDO (`Group` equal to its serial) and its string is freed in `hcdDeletePdo`. Legs on the `release` packages (worker p36-i17, development host A, QEMU, 2026-10-09/10; `out\p36-i17\legs\report.md`): on 98 SE, ME, 2000, XP, Vista x86 and 7 x86 and x64, the SetupDi list found one interface per device PDO, one on the external hub's PDO and the device behind it, one for the split audio device, and none on the root hub; on 98 SE, 2000, XP and 7 x86 and x64, arrival and removal notifications across a hot-plug, a surprise unplug and replug, a device's disable and enable, and the controller's (every interface away and back). The composite clause was read only in part: QEMU's `usb-audio` splits into one function, so disabling `MI_00` removed its interface and enabling it brought it back, but no sibling function was there to keep working. Passed. Three hangs of the 98 SE guest during these legs (one after a surprise removal of a stick whose install was still pending, then its replug) are tracked separately, by worker p36-w98hang; from release evidence they are not attributed to the driver.
- [x] 36.18 the device's real speed on every hub door (issue 17 item 6), both halves (owner, 2026-10-09):
  - **The `_EX` form's `Speed` byte** reads 3 for a SuperSpeed connection and 4 for SuperSpeedPlus, as Intel's USB 3 drivers for Windows 9x report it, **on Windows 98 and ME only** (owner, 2026-10-09, after the static reading below): where `IoIsWdmVersionAvailable(1, 0x10)` is FALSE, the test `hcd_pdo.c` already uses for them. Every NT target keeps 2 for both, and `_EX_V2` gives NT tools the real speed. 0 to 2 are unchanged. No Microsoft header defines 4, and the WDK 7.1 headers stop at 2, so design record 13 section 8.4 is amended to replace the High Speed ceiling.
  - **`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`** (`0x0022045C`), new.
  - **The structure.** `ConnectionIndex`, `Length`, `SupportedUsbProtocols` and `Flags`, 16 bytes, written byte by byte like the rest of the door. Three separate sources (Codex review of `7567dec`, from Microsoft's `USB_PROTOCOLS` and `_EX_V2_FLAGS` pages):
    - **`SupportedUsbProtocols`, out:** the **port's** signalling protocols, not the device's and not their intersection. An empty port has them; a Full Speed device on a USB 2.0 port leaves `Usb200` set. A USB 3 logical port does not gain `Usb110` or `Usb200` from its USB 2.0 companion, which is a port of its own here.
    - **The operating flags** (`DeviceIsOperatingAtSuperSpeedOrHigher`, `...SuperSpeedPlusOrHigher`): from the connection's negotiated speed.
    - **The capable flags** (`DeviceIsSuperSpeedCapableOrHigher`, `...SuperSpeedPlusCapableOrHigher`): from the device's BOS (its SuperSpeed and SuperSpeedPlus capability descriptors), not from the speed, since a SuperSpeedPlus device may be running at 5 Gbit/s. The BOS is read only on the SuperSpeed enumeration path today (`xhci_enum.c` ~280), so a USB 3 device running at High Speed reports neither capable flag unless its BOS is read too; this task decides which, and says so.
  - **Input and output** (Microsoft's IOCTL page): the caller sets `SupportedUsbProtocols` to the protocols it asks about, and Windows 8 refuses with `STATUS_INVALID_PARAMETER` unless `Usb300` is set; that is a property of the request, not of the port. `Length` and the input buffer must be `sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2)`, and the output buffer at least that, checked apart as `hcdDoorConnInfo` checks both (`hcd_door.c` ~1453); the byte count returned is the structure's. A port out of range is refused as the `_EX` form refuses it.
  - **Static reading of the `_EX` byte's consumers** (Codex review of `7567dec`, disassembly in `tools\win7-x86-extracted\ui` and `tools\vista-x64-extracted\ui`): Vista's and 7's `usbui!UsbItem::IsHighSpeed` tests the byte at `+0x17` for exactly 2, so 3 and 4 read as not High Speed, and the hub page's `AddFullSpeedDevicesToListView` lists such a device among the Full Speed devices: a misclassification, not a crash. XP's and XP x64's pages send the older `0x22040C` and never read the byte. Hence 98 and ME only; the reading goes into `legal-provenance.md` when the change is made.
  - **Host vectors:** the input flags and lengths, a valid input with a short output buffer, the returned byte count, an empty port, a slower device on a faster port, a SuperSpeedPlus-capable device at 5 Gbit/s, and each `_EX` value on 98 and ME and on NT.
  - **Legs.**
    - SweetLow's usbview (http://sweetlow.orgfree.com/download/usbview.zip) on 98 SE and 7: a SuperSpeed device on a root port and behind a USB 3 hub, a High Speed device, and a hub port with nothing on it.
    - Microsoft's own pages with a SuperSpeed device attached: the hub's Power tab on 98 SE and ME with 3 and 4 (a synthetic 4 on the `qemu` flavour where no SuperSpeedPlus device is at hand), and the hub's Advanced tab on Vista and 7, read for **which list the device lands in** (High Speed, as before). Read by the owner.
  - **Done 2026-10-10** (`465825d..b489836`, with 36.17 and 36.19; ticked at the integration of wave 2 on the i17 worker's recommendation, which the owner accepted; **bench clause owed (2.2.0.3)**: the owner's own reading of Microsoft's pages). The capable flags come from the BOS or the operating speed, and no BOS read was added at High Speed (record 13 section 8.4). Legs on the `release` packages (worker p36-i17, development host A, QEMU): on every guest, x86 and amd64, the `_EX` byte read 3 for a SuperSpeed stick on 98 SE and ME and 2 on 2000, XP, Vista x86 and 7 x86 and x64; `_EX_V2` read 16 bytes, `SupportedUsbProtocols` 4 on USB 3 ports, 3 on USB 2.0 root ports and 1 behind the Full Speed hub, flags 3 for the SuperSpeed stick and 0 elsewhere, and refused a request without `Usb300`, a `Length` of 12 and a port past the end with error 87 and an 8-byte output with error 122. SweetLow's usbview walked the tree on every guest and named the stick "Super". On 98 SE the root hub's Power tab rendered with the stick attached; on Vista and 7 the Advanced tab put the stick in no Full Speed list. No Low Speed device was attached. Passed, apart from the bench clause.
- [x] 36.19 polling rates a client chooses: `XhciFastPollFsLs` 4 (issue 17 item 2; design record 13 section 13 gains a section for it):
  - **The codes.** Under mode 4, an interrupt endpoint whose `bInterval` in the caller's descriptor is 128, 129 or 130 is programmed at Interval 2, 1 or 0 (2000, 4000 or 8000 Hz). Every other `bInterval` reads by Table 6-12. Mode 4 does not do modes 2 and 3's 16-63 mapping, and they do not honour the codes.
  - **Why an opt-in.** 128 to 255 ms are legal Full- and Low-Speed values, so a device may declare a code. And a client that sends codes with the mode off gets what they legally mean: 128 ms at Full Speed. The procedure is mode 4 first, then hidusbf set to the codes, and the release notes say so.
  - **The ceiling.** 125 us, Interval 0, at any speed; 131 and above are not codes.
  - **Scope.** Full and Low Speed on a root port only, as modes 2 and 3; High Speed and SuperSpeed anywhere. Never isochronous, bulk, or a hub's own endpoint.
  - **Keyed on the descriptor's byte** (`BInterval`), not on the Interval Table 6-12 derives from it: 10 at Full Speed, 15 after the High Speed clamp.
  - **Applied before the interval cap** (35-T.7), which leaves Interval 2 or less unchanged. Today a capped endpoint is never fast-polled, so on an AMD controller the cap would otherwise silence every code.
  - **A refusal falls back to Interval 3 (1 ms)**, not to Table 6-12's reading of the code, which would leave the device polled every 128 ms. The endpoint keeps its Table 6-12 Interval and its fallback apart, so modes 2 and 3 revert exactly as today. `RESET_PORT` and the endpoint recycle rebuild from the stored `pipe->Ep` (`hcdCfgConfigureAdd`), so they keep the chosen Interval and fallback without reading the descriptor again.
  - **SuperSpeed composite functions.** `hcdCfgFunctionEndpoint` (`hcd_cfg.c` ~654) does not take the caller's `bInterval` at SuperSpeed or SuperSpeedPlus, so a function's code there would be lost. This task changes it: the endpoint and its companion are parsed from the device's own configuration (`dev->Selected`), keeping burst, Mult and Max ESIT Payload; then the caller's matched byte replaces `BInterval`, the ordinary Interval is derived from it, and mode 4 is applied through the same decoder and fallback bookkeeping as every other endpoint (`hcdCfgFastPoll`). Not by sending a seven-byte copy through the USB 2.0 parser, and not by writing the Interval directly. An integration vector with a split SuperSpeed device, a nontrivial companion, and a device byte that differs from the caller's code.
  - **A composite function's later `SELECT_INTERFACE`** rebuilds from the device's own configuration (`hcd_cfg.c` ~1549) and so loses a code, as it loses modes 2 and 3's mapping today (design record 13 section 13.5). **Decided (owner, 2026-10-09):** accepted and documented, as for modes 2 and 3, in record 13 section 13 and the release notes; HID class drivers do not reselect an interface. Revisited if a report shows it.
  - **Docs:** record 13 section 13 gains mode 4; record 17 section 4.7, which orders the caller's `bInterval`, the cap and then fast polling, gains mode 4's place before the cap; the release notes' registry table and the procedure.
  - **Host vectors** in `test_fast_poll`: 4 accepted and 5 off; each code at Low, Full and High Speed and SuperSpeed; 127, 131 and 255 untouched; Full Speed behind a hub untouched and High Speed behind one applied; isochronous and bulk untouched; the two conventions kept apart; no double application; the revert landing at 3. **Legs:** hidusbf set to the codes on 98 SE and 7 in QEMU, read in the `fastpoll.*` records; mode 4 with an unpatched device reading as the release before it.
  - **Done 2026-10-10** (`465825d..b489836`, with 36.17 and 36.18; ticked at the integration of wave 2 on the i17 worker's recommendation, which the owner accepted). Built x86 and `-amd64`, all flavours, every gate and the host suites passing at `3283220` (after it, release-notes text only). Legs on the `release` packages (worker p36-i17, development host A, QEMU, 2026-10-09/10): on 7 x86 and 98 SE, hidusbf's codes 128, 129 and 130 under mode 4 programmed Interval 2, 1 and 0 (`fastpoll.open` records; on 98 SE also read from the Output Endpoint Context), modes 2 and 3 ignored the codes, a Full Speed mouse behind a hub was not sped up, mode 5 read as 0, and modes 0 to 3 with the mouse unpatched read as before; the mouse moved throughout. Passed.
- [x] 36.20 the docs of what `2.3.0.0` ships, moved here from SUSP.3 because this phase cuts polling and MSI (Codex review of `7567dec`):
  - record 15 section 14's list: `implementation-invariants.md`'s "Interrupt Delivery" (a pin-0 controller polled, not refused), `architecture.md`'s line-based delivery, the release notes' "Controller" row and rows for the switches, the acceptance test's step 3 and its stop on the pin, `xhciqual/hardware-testing.md` and record 01's C4 note, record 13 where it describes the ISR, `source-files.md`, the locking record, `xhcisnap/README.md`;
  - the MSI delivery, 36.3's `XhciIntelIrqRoute`, 36.17 to 36.19 and 36.22's transfer-size value, in the release notes, `docs/using` and the records they amend;
  - the user-facing statements of 36.15's decisions (record 15's own mode rules are reconciled in 36.5, before code);
  - selective suspend's (was SUSP.3, joined 2026-10-09): record 13 sections 5, 6.5, 10.2 and 10.9; the release notes (the idle-policy limitation replaced by the switch, off by default, and what turning it on does); the decisions table's 28.3 row; `source-files.md`; the locking record; the run record.
  - **Done 2026-10-10** (docs only, branch `p36-docs`: `6c7559b`, `75eb39e`, `2007beb`, `df04e7a`, `ccf3036`, `ac90c14`, `64e72b0`, `265cda0`, `1bdd0eb`, `59364b3`; `265cda0` also maps strict mode's files and `src\uas\`, which no earlier task had): the release notes marked "TEST 2.2.0.3, bench pending" (a "What `2.3.0.0` changes" section and Targets row; polling, MSI, `XhciMaxTransferKB` and the per-device value, `XhciWideMap` off, the port switchover, selective suspend, composite parents, USBView strings, the Low-Speed mouse, `XHCIQUAL`'s caveats and system header, the Verifier fix; sixteen INF values; the known notes), the acceptance test, the README's Tuning, `source-files.md`, three `lessons.md` entries, the invariants, `architecture.md`, record 13 (5.4, 5.5, 6.5, 10.2, 10.9), the 28.3 row, `xhcisnap/README.md`. Codex docs review thread `01a12517`: round 1 eleven P2s (`64e72b0`), round 2 seven (`1bdd0eb`), round 3 four and a P3 (`59364b3`), round 4 clean - the loop ended. **Owed** (ticked with these left to their owners): the run record's selective suspend legs (36.35, 36.39); the matrix's readings for the release notes' "being read" sentences (composite on the other systems, strings on 98 SE and ME, the update from `2.2.0.0`, the downgrade); 36.41's re-measure on stage A.
**The transfer size (36.21 to 36.27; owner, 2026-10-09).** Storage over
UAS moves 1 MiB per command by default, where today `xhciuas.sys` offers
64 KiB, and a bulk transfer goes to the controller in as few TDs as
measurement shows is worth it (today at most 128 KiB each). A registry
value sets the command size from 64 KiB up to 2 MiB: lower for a device
or controller that misbehaves at large transfers, higher to try more.
Everything below is sized for the 2 MiB the value can reach, not the
1 MiB default. Sizes here are binary (KiB, MiB); the value's name keeps
`KB`. Drawn up as planned work and reviewed twice, once before it was
moved here; the findings are folded in.

**Two sizes, not one.** The command size is what one UAS command IU asks
the device to move: `xhciuas.sys` sends each SRB as one command and one
data URB whatever its length (`uas_xport.c` `uasSlotPrepare`), and the
adapter descriptor's `MaximumTransferLength` is what tells the storage
class driver how large an SRB to build. The TD size is how much of one
data URB the HCD puts on the ring at once: `hcdPlanChunk` (`hcd_io.c`)
cuts a URB into chunks of at most 32 pages, packet-aligned, and maps and
submits the next only after the previous one completes. A 1 MiB command
therefore does not need a 1 MiB TD, and a sized SRB does not make a
sized TD: the planner still bounds each chunk by the map grant and 32
SG elements (`hcd_io.c` ~99). The work is staged so each stage is
measured before the next is taken:

- **Stage 1, the command size.** Raise what `xhciuas.sys` advertises to
  the value in effect, with the HCD's transfer engine unchanged (the
  value, its query and the instrumentation are new HCD code). With
  page-aligned buffers and a grant of at least 32 map registers a 1 MiB
  command goes as eight sequential 128 KiB TDs and 2 MiB as sixteen; an
  unaligned buffer needs nine or seventeen, and a smaller grant more
  (`hcd_io.c` ~96's offset subtraction and packet rounding). Where the
  storage stack does send full-size SRBs, full 1 MiB commands in place of
  full 64 KiB ones remove 15 of every 16 command and status IU pairs,
  and 2 MiB 31 of 32. Two consequences are kept, not changed: the
  whole-command timeout (the SRB's, or 30 seconds, taken once and counted
  down over the command's life, `uas_xport.c` ~569 and ~915, never
  scaled or restarted per chunk), and the caller's payload held under
  partial MDLs over pages its caller already locked (`uas_mem.c` ~31):
  up to 30 active streamed commands (`uas_xport.c` ~1595) represent
  60 MiB of caller payload at 2 MiB each against 1.875 MiB at 64 KiB;
  page coverage of unaligned buffers and requests still queued can
  retain more. That is residency, not new common buffer, and 36.26
  validates it.
- **Stage 2, bounded pipelining**, if stage 1's readings show the gaps
  between chunks cost throughput (the ring empty while the next chunk is
  mapped): more than one chunk of one URB on the ring at once. Not a
  removal of `Exclusive` (`hcd_io.c` ~938), which keeps another request
  from landing between two chunks: it needs per-chunk mappings, ordered
  completion of the URB, an early short or error with later chunks
  already queued, and a forward-progress rule when map registers are
  short. Two 32-TRB chunks do not fit today's 62 usable TRBs, so this
  stage also grows the ring modestly (128 TRBs, or 31-page chunks). Even
  128 TRBs moves the shared pool geometry: the pool/EP0 stride assert
  (`xhci.h` ~988), the isochronous limits derived from the pool count
  (~3721) and stream allocation (`hcd_cfg.c` ~3294). So the isochronous
  decoupling and the memory, stream and snapshot tests those touch come
  with whichever stage first changes the pool ring: stage 2 if it grows
  it. The event-burst review below comes with stage 2 whatever its ring,
  since two 31-page chunks in today's ring already put more than one TD
  of a URB on it.
- **Stage 3, larger TDs**, only if stage 2 falls short: the limits below.

**What the TD size touches** (stage 3, and in part stage 2):

- the transfer rings: 64 TRBs (1 KiB) each, 62 usable (one Link, one kept
  empty; `XhciRingCapacity`, `xhci_ring.c` ~38), carved from the one
  fixed common buffer (`xhci.h` `XHCI_POOL_RING_TRBS`, ~840 and ~875). A
  1 MiB TD over the HCD's page-split mapper is 256 data TRBs aligned and
  257 unaligned (the mapper splits at page boundaries, so no extra 64 KiB
  splits: `hcd_dma.c` ~305); 2 MiB is 512 and 513. Fixed single-segment
  ring sizes and what fits are in the table below; a segmented layout
  needs its own Link-TRB and allocation accounting. Above one page a ring
  must be placed by its physical address, not an aligned offset, to keep
  off a 64 KiB boundary (`xhci_mem.c` ~37's proof holds only to a page);
- the stream rings, outside the pool: up to 31 rings on each of a UAS
  device's three stream endpoints, limited to one page today
  (`xhci_stream.c` `XhciStreamLayout` ~130, `hcd_cfg.c` ~3294). With the
  context array and its alignment, 16 KiB rings are 512 KiB per endpoint,
  1.5 MiB per UAS device before placement padding, and 32 KiB rings
  3 MiB, so status streams may keep small rings;
- the isochronous limits, which derive from the pool ring today
  (`XHCI_XFER_MAX_ISO_TRBS`, `IsoScratch`, `HCD_ISO_BLOCK`, `hcd_urb.c`
  ~409): kept at today's values, decoupled from the bulk ring in the
  first stage that changes the pool ring;
- each record's SG list (`HCD_SG_ELEMENTS`, 24-byte elements): 257 or 513
  per record that can be mapped at once (four per pipe, two per stream
  pipe), allocated with the pipe, not one list shared by the records;
- the TRB build arrays on the stack (`hcd_io.c` ~1160, `hcd_enum.c` ~939,
  `hcd_hub.c` ~1218): bulk moves off the stack to a per-pipe scratch
  reused under the lock; control and enumeration keep their small arrays;
- the pure core's caps (`XHCI_XFER_MAX_DATA_TRBS`, `XHCI_PIPE_CHUNK_ELEMENTS`,
  `XHCI_PIPE_TD_MAX_BYTES`), and `hcdPlanChunk`, which bounds a chunk by
  pages and never by a byte limit: it becomes min(remaining, the size in
  effect, the map grant, the ring's room), packet-aligned when not the
  last, with the host tests calling the production arithmetic;
- the SG walk `xhciXferEmitSgList` (`xhci_xfer.c` ~543), which rescans the
  list for each element: linear for the HCD's ordered lists, its time
  under the controller lock measured;
- admission: a bulk request with no ring room completes busy today; with
  large TDs it waits unmapped instead, and the design says how many TDs
  a ring holds.

| Fixed ring TRBs | Usable | Worst-case 1 MiB TDs | Worst-case 2 MiB TDs | Controller common buffer, before padding |
|---:|---:|---:|---:|---:|
| 64 (today) | 62 | 0 | 0 | 400 KiB |
| 128 | 126 | 0 | 0 | 432 KiB |
| 512 | 510 | 1 | 0 | 624 KiB |
| 1,024 | 1,022 | 3 | 1 | 880 KiB |
| 2,048 | 2,046 | 7 | 3 | 1,392 KiB |

Unchanged by design: the 64 KiB TRB buffer rule and TD Size's five bits,
UAS's command and 288-byte status buffers, `usbstor.sys`'s own limits on
the Bulk-Only path (`XhciForceBulkOnly`), and the event ring's 256
entries, provided the burst review passes. A TD gives one normal
successful completion, not one per TRB, but an early short, an error or
a stop adds trailing events (`xhci.h` ~3638 remembers four tails); with
more TDs outstanding, the review argues those tails, the 256 entries,
the drain budget and moderation together, in the first stage that puts
more than one TD of a URB on the ring.

**The value.** A working name `XhciMaxTransferKB`, a `DWORD` in the
controller's driver key, default 1024, range 64 to 2048 in 4 KiB steps;
absent, mistyped, out of range or off-step reads as the default, with a
note. It is read once at controller start (`hcdReadValues`) and applies
to every device on that controller; it is not a per-device quirk. It is
an **advertised command limit**: what the adapter descriptor tells the
class driver, not a ceiling enforced on whatever SRB arrives.

The query. `xhciuas.sys` learns the value through a query of its own on
the private interface beside `xhci98_streams.h`, separate from the
streams requests (a High-Speed UAS device opens no streams), at its start
before pipe selection and before any LUN is exposed. Its contract:
- the request is pointer-free, carries a signature, version and size,
  and the reply is the normalised command-policy bytes, independent of
  streams and of the map registers available at the moment;
- `xhciuas.sys` sets its value to 64 KiB on every initialisation path
  before it asks, and the descriptor and the pipes'
  `MaximumTransferSize` both read that one stored value (no second
  registry read);
- unsupported (an older HCD completes an unknown internal IOCTL
  `STATUS_NOT_SUPPORTED`, `hcd_urb.c` ~1685) means 64 KiB, never an
  assumed 1 MiB;
- a malformed successful reply (signature, version, size, range or step
  wrong) means 64 KiB with its own note and counter;
- a transport failure (removal, no memory) is kept apart from both, as
  `uas_streams.c` ~35 keeps transport status apart from reply status: it
  means 64 KiB with its own note, and the start goes on through its
  ordinary sequence, whose own failures (selection, streams) still fail
  it;
- host vectors for the production reply-validation and fallback helper
  (unsupported, malformed, transport failure, each valid value); the
  mixed binaries themselves are guest legs in 36.26, since `test_uas`
  links only the pure `uas_iu.c` and reaches no dispatch or start.
- the counters handed to the HCD (36.21) go in bounded records copied
  into HCD-owned storage, never through the synchronous PASSIVE-only
  request helper from a path holding the UAS lock, and unavailable or
  stale telemetry reads apart from zero.

Activation. Storage class drivers keep the adapter descriptor they read
in their own device extensions, and a UAS stop and start keeps the LUN
PDOs already known (`uas_fdo.c` ~397 and ~537), so a controller restart
alone does not show a class driver re-reading it. A change therefore
takes effect at the next reboot; a controller restart with the storage
stack removed and enumerated again counts as well only once 36.26 shows
it refreshes the descriptor. An SRB already sized to the old value is
carried as it is (the HCD chunks it) and never cut short or refused.
Holding a device to a size live would need an SRB splitter (CDB and LBA
rewritten, sense and status gathered), which is not planned.

**Windows 98 and ME.** NUSB's storage path runs through `USBNTMAP.SYS`,
read statically as doing its I/O through a 64 KiB bounce MDL
(`runs/run-31.md`, NUSB's storage path); if that holds, a larger
descriptor changes nothing there. 36.21 measures the SRB sizes 98 SE and
ME actually send before anything is promised for them.

- [x] 36.21 (was MB.0) instrumentation, then the baseline. Each counter has a
  named producer:
  - `xhciuas.sys` counts SRB lengths and commands outstanding (beside
    its `Counters[]`, `uas.h` ~219, which nothing reports today) and
    hands them to the HCD over the private interface, which carries them
    in its snapshot;
  - the HCD counts data URB length, chunks per URB and the map grant;
  - the gap, per data pipe or stream and only while a URB has data not
    yet on the ring, measured as two parts: software time from a chunk's
    retirement to the next chunk's publication, and the observed
    software-empty interval (the ring empty in software's view with that
    URB unfinished). Plain ring-empty time includes idle and is not the
    gap. The ring's dequeue is software's view, advanced as events are
    handled (`xhci_ring.c` ~27), and a Transfer Event carries no
    timestamp. So the interval starts when software processes the
    completion and observes the ring empty, and omits any hardware-empty
    time before that while event delivery, moderation or DPC scheduling
    delayed the observation. Hardware starvation itself is recorded
    unmeasured unless a separate trace method shows it, and 36.23 decides
    on the observed interval, CPU cost and throughput;
  - the HCD region's header grows append-only (`xhci.h` ~8525's
    size-based extension), the outer snapshot header and schema
    unchanged, and `XHCISNAP` decodes the new fields; `test_snap` and
    `XHCISNAP`'s selftest show an older snapshot still read, and an
    actual older `XHCISNAP` reading a newer snapshot with the new fields
    simply absent.

  Then on this phase's build before 36.22, plus that instrumentation, on
  the E460 and the P14s Gen 1 under 98 SE and 7 (owner): ATTO Disk
  Benchmark and a large copy over UAS and Bulk-Only, recording the
  benchmark's version, queue depth, the device, connector,
  `XhciImodInterval250ns` in effect, flavour and repetitions. The SRB
  sizes Windows ME sends are read in its QEMU guest, a VM leg of this
  task
  - **Done 2026-10-09, bench clause owed (2.2.0.3)** (`f3a395c`, review
    fixes `ac5bbd0`; built x86 and `-amd64`, all
    flavours, every gate and the host suites passing). `xhci_xstat.c`
    (stamps by the time-stamp counter with the processor's APIC ID, the
    gap's two parts, publication, completion and mapping costs);
    `xhciuas.sys`'s record over `IOCTL_XHCI98_TELEMETRY`; the HCD header
    appended to 202 words and decoded by `XHCISNAP`; `releases\2.2.0.0`'s
    `XHCISNAP` read the newer snapshot with the new words absent. The
    QEMU baseline (development host A, `out\p36-mb\xp64\notes.md`): XP
    x64, UAS at SuperSpeed, raw reads at 64 KiB commands 214 / 281 / 343 /
    353 MB/s at 64 KiB / 256 KiB / 1 MiB / 2 MiB requests, every SRB at
    most 64 KiB; Bulk-Only 210 / 340 / 373. Windows 98 SE (NUSB) and ME,
    read in their QEMU guests (`out\p36-mb\w98n`, `out\p36-mb\me`): no
    SRB above 64 KiB reaches `xhciuas.sys` or the HCD, one at a time,
    whatever the descriptor says (`USBNTMAP.SYS`), and file reads no faster
    at 1024 than at 64. The x86 `hcdCpuId` did not preserve EBX around
    CPUID (STOP 0xB8 on Windows 2000, found by the integration smoke leg;
    fixed by `f22a42b`, here `fe76f91`); on 98 SE and ME the same clobber
    lowered the IRQL inside the controller lock, which is why those
    readings' HCD bulk-URB counts fell short of their chunk counts - their
    sizes come from `xhciuas.sys`'s record, which it did not touch; re-read on
    the fixed build, 98 SE's counts agree (3408 URBs, 3408 chunks). Owed:
    the bench (E460, P14s Gen 1, 98 SE and 7, ATTO and a large copy)
- [x] 36.22 (was MB.1) stage 1: the value and its query (the contract
  above), `xhciuas.sys`'s descriptor (`MaximumTransferLength`,
  `MaximumPhysicalPages` as bytes / page + 1) and pipes'
  `MaximumTransferSize` from its one stored value, the INF rows and gate,
  the docs; host vectors in `test_uas`. Also what 36.26 needs from its
  first run, whatever stage the work stops at:
  - the test client that sends sized SRBs;
  - a constrained map grant;
  - the qemu flavour's injection extended to bulk and stream TDs (short,
    error, stop, timeout), since today's endpoint faults target a
    one-TRB interrupt TD (`hcd_inj.c` ~36, ~710).

  36.21 repeated at 64 KiB, 1 MiB and 2 MiB
  - **Done 2026-10-09** (`4909421`, review rounds to `ac5bbd0`, clean at
    round 7, thread `01a1209e`; the device's own limit `8a7c21b..90c625c`,
    clean at round 2, thread `01a12143`; built x86 and `-amd64`, all
    flavours, gates and host suites passing). `XhciMaxTransferKB`
    (`hcd_ctl.c`, `XhciXpolValue`), `IOCTL_XHCI98_TRANSFER_POLICY`
    (`hcdPolicyIoctl`; `uasAskPolicy`, `UasPolicyValue`, `test_uas`), the
    note in the UAS device's hardware key, the INF rows and gate. Owner,
    2026-10-09: the default stays 1024 in both INFs; and `xhciuas.sys`
    also honours Microsoft's per-device `usbstor\VVVVPPPP`
    `MaximumTransferLength` (65535 to 2097120, nearest 4 KiB; record 13
    section 11.5, `UasDeviceLimit`); widened by the owner on 2026-10-10 to
    65535 to 2097152, so the 2097152 tools write is 2 MiB (with 36.24). The test client is
    `test\xfertest`; the constrained grant `XhciQemuMapGrant`; the qemu
    bulk faults 14 to 17 hex (`build-and-test.md`). 36.21 repeated (XP x64,
    QEMU): at 1024, 134 / 394 / 593 / 600 MB/s at 64 KiB / 256 KiB / 1 MiB
    / 2 MiB requests, a 1 MiB URB 8 chunks (9 unaligned); at 2048, 1 MiB
    696, 2 MiB 711 (550 unaligned), 16 chunks (17); at 64, 177 / 264 /
    243; VERIFY clean at each; 4096 noted out of range and 1024 used.
    The device's own value read on XP x64 in QEMU (`out\p36-mb\xp64\notes.md`):
    none, 1 MiB commands; 65535, 64 KiB commands (source 5); 2097120, 2 MiB
    commands with the controller at 1024; 2097152, refused (reason 3) and
    1 MiB, with another model's key ignored. Owed in 36.26: the rest of the
    geometry legs
- [x] 36.23 (was MB.2) the decision on stage 2, from 36.22's readings (owner): command
  rate, chunk rate, the gap's two parts, CPU cost and throughput
  - **Taken by owner decision 2026-10-09**: stages 2 and 3 are taken
    whatever the readings, and the decision does not choose the default
    (it stays 1024). The readings, recorded anyway (XP x64 in QEMU,
    development host A, guest QPC under TCG, so large run-to-run variance):
    command rate at 1 MiB reads 566 commands/s at 1024 against 5230 at 64
    KiB commands (baseline, 343 MB/s); chunk rate 4524 chunks/s at 1024
    (8 a URB); the gap, retirement to the next chunk's publication, 120 us
    mean at 1024 and 84 us at 2048, the software-empty interval the same
    (the ring empty for all of it); CPU cost a publication 34 us at 1024
    (27 at 2048) against 14 us at 64 KiB, a completion pass 3.8 us against
    2.0, a next chunk's mapping 22 us (9 at 2048); throughput as 36.22
    above, 1 MiB reads 343 MB/s at 64 KiB commands, 593 at 1024, 696 at
    2048
- [x] 36.24 (was MB.3) stage 2, if taken: its design reviewed to
  convergence, the event-burst review included whatever the ring, then
  the code with host vectors in `test_td`, `test_xfer` and `test_ring`.
  If it grows the pool ring, also the isochronous decoupling, the stride
  assert, `test_iso`, `test_membuf`, `test_stream` and `test_snap`
  - **Done 2026-10-10, legs owed to 36.26** (design `b76c16f..6aa2ae2`; the HAL reading, branch `p36-hal`, merged at `bb007ed`; code `b26f5c8`, `9b0c85f`, review fixes to `297d42a`, clean at round 4, thread `01a1236a`; the suspended-controller ordering `f9c02ec`). Record 13 section 11.6: `XhciWideMap` on by default (owner, 2026-10-10, on 11.6.1), 0 off - **then off by default, 1 opts in (owner decision 2026-10-10, after Codex session `01a124ef`; p36-int4)**; eligibility: every qualifying bulk OUT URB, bulk IN only on a stream; promotion only on NT, at the grant `0x80001`, past the start's register-free check and on a NULL base. Built x86 and `-amd64`, every gate and the host suites passing. Smoke (XP x64 in QEMU, development host A, `out\p36-mb\xp64\notes.md`): promotion fires at the default and not at 0, VERIFY clean either way
- [x] 36.25 (was MB.4) stage 3, if taken: its design reviewed to convergence (fixed
  rings placed by physical address, or a fixed set of page segments
  allocated with the pipe, sized from the table above and qualified on a
  low-memory Windows 98 machine and across repeated starts and
  hot-plugs), then the code; host vectors in `test_td` (which models
  the planner and mapper and hard-codes 32 elements and 80 pages today),
  `test_xfer`, `test_ring`, `test_stream`, `test_iso`, `test_membuf` and
  `test_snap`, for whatever 36.24 did not already cover; the injection
  reaching a 257- and 513-TRB TD
  - **Done 2026-10-10, legs owed to 36.26** (design, record 13 section 11.7, `8ee1c0d..cb4f0b0`, clean at round 4, thread `01a1239f`; code `a008b3e`, `520e0f5`, clean at round 1, thread `01a123f9`). One TD a URB on 1024-TRB stream rings placed by physical address; built x86 and `-amd64`, every gate and the host suites passing. Smoke (XP x64 in QEMU): three big blocks built, 1 MiB URBs as one TD of 256 TRBs (257 unaligned), VERIFY clean, 1 MiB reads 861 MB/s against 665 with `XhciWideMap` 0
  - **Design scope (record 13 section 11.7; orchestrator decision, owner
    to confirm):** the fixed rings placed by physical address are the
    **stream rings** of a streams endpoint (UAS at SuperSpeed), 1024 TRBs
    each, on NT starts where 36.24's promotion is enabled; Bulk-Only and
    High-Speed UAS keep 64-TRB pool rings and stage 2 - their large TDs
    are deferred, not refused. The TRB scratch is one per controller
    beside the wide pool, not per pipe, since every publication holds the
    controller lock. The low-memory qualification becomes an NT one (the
    big blocks, granted or refused into the fallback) plus a Windows 98 SE
    low-memory regression leg showing its allocations unchanged, since
    nothing of stage 3 runs on 98 or ME
- [x] 36.26 (was MB-V) the legs, two claims kept apart. Compatibility: every guest, x86
  and amd64, UAS (streams, and High-Speed without them) and Bulk-Only,
  file compares, the value at 64, 1024, 2048 and out of range, the
  activation (a reboot takes a new value; a controller restart with the
  storage stack re-enumerated counts only if shown to refresh the
  descriptor), mixed binaries (this `xhciuas.sys` on the `2.2.0.x` HCD
  and the `2.2.0.x` `xhciuas.sys` on this HCD, streams and High-Speed
  without them, each showing the descriptor it advertises and file I/O
  passing), the query failing in transport alone (qemu injection) with
  the start otherwise completing, and removal during the query ending in
  the ordinary start failure and cleanup, Windows 2000 SMP under Driver
  Verifier, stop and restart,
  D3 and resume, unplug with work outstanding, audio and input under
  storage load. Geometry, through 36.22's test client rather than the
  file system's own SRBs, with assertions per stage:
  - stage 1: one large command as several sequential chunks; a short or
    error after earlier chunks completed, with the accumulated length and
    the termination right; 1 MiB and 2 MiB at unaligned and fragmented
    buffers, back-to-back, across the ring's wrap, under a constrained
    grant; a slow device completing successfully within the
    whole-command timeout, and a timeout, an abort and removal recovered
    from; full queue depth at 2 MiB under memory pressure;
  - stage 2: several chunks of one URB shown outstanding at once, and an
    early failure with successors queued;
  - stage 3: 257- and 513-data-TRB TDs observed on the ring where the
    geometry and the grant allow them. A 2 MiB SRB sent is not proof of
    a 513-TRB TD.
  - **Done 2026-10-11** (`runs/run-36.md`, final matrix sections 5 and 5b: T1-T8, B1-B3, B5-B7, B11, B13; the re-read's B9(b)): passing at 64, 1024, 2048 and out of range on every guest that sends above 64 KiB (98 SE and ME never do). Not makeable in QEMU: B8 and B9(a) (usb-uas holds one packet per stream), B10 on x64 (no amd64 test client) and B12. The DMA legs above 4 GB and under PAE were not shown: untested. `XhciWideMap` 1 with a real UAS drive and on an updated 32-bit 7: "Planned - bench follow-up after `2.3.0.0`" (BF.6).

  DMA: an amd64 guest whose source pages are shown above 4 GB with
  mapped addresses below it (the bounce path), and x86 PAE likewise,
  each recorded untested if not shown; the amd64 leg does not cover PAE
- [x] 36.27 (was MB-E) the bench (owner): 36.21's readings repeated on the same
  machines, devices and settings
  - **Moved** to "Planned - bench follow-up after `2.3.0.0`" (BF.1; owner, 2026-10-11): not read on the bench. The E460's copies at about 200 MB/s are the only real-hardware speed reading.
**Selective suspend, standby resume and the 98 SE audio wedge (36.28 to
36.41; owner, 2026-10-09).** Moved here from planned work the same day,
because the switch is now off by default: `XhciSelectiveSuspend` defaults
to **0** (owner, 2026-10-09, replacing the default 1 of 2026-10-05; design
record 14 sections 7 and 11), so a user of `2.3.0.0` runs today's
behaviour unless the user sets 1. The bus then suspends a device when
Windows asks for it - a function's D-state in S0, or the idle notification
on XP onward - and wakes it on D0 or on the device's remote wake, never on
a timer of its own; USB 2.0 devices (stage A, 36.29 to 36.36) and
SuperSpeed devices (stage B, 36.37 to 36.40). Design record 14 is the
design; stage A's half converged over ten review rounds (`b2d03d5`). The
first standby and hibernate resume readings under the HCD (36.28) come
first, because the suspend design rests on them, and the wedge (36.41) is
re-measured against stage A's capabilities, so it waits for them. Their
docs join 36.20 and their install legs 36.final. The owner's other
decisions of 2026-10-05 stand: the test client is tracked under `test\`,
never shipped, refused by the packaging gate; stage A's checkpoint
includes the owner's bench; stage B ships in the same release, so the cut
waits for it. Stages A and B are built on branches of their own, both
merged into `2.3.0.0`. Their functional legs set `XhciSelectiveSuspend` to
1, and run once more under `XhciInterruptMode` 2, since suspend and
polling both touch the controller's power and teardown paths.

- [x] 36.28 (was SUSP.1) system standby (S3) and hibernate (S4) resume, read for the first time under the HCD: the controller's D3 and D0 paths in `hcd_power.c` - the D0 resume, the work-item route and the direct transition - have never run on a guest (its header; neither primary's test guest sleeps, Windows 2000's runs `acpi=off`), and record 14 section 4.4 rests on them (pended D0s across a controller transition, a device's link still in U3 after a restore). Read on guests that sleep - QEMU with S3 enabled, ME (which hibernates on QEMU's power-down), and each target that can be made to - and on the E460 and the P14s Gen 1 (owner): standby and resume, hibernate and resume, with devices idle, devices suspended by stage A, and a D0 pended across it; a controller with and without Force Save Context (QEMU reads FSC 0); what breaks is fixed in this phase. The release notes' "a wake from standby" row moves from untested to read **Part 1 done 2026-10-10** (today's code, base `ebe3ec9`, then the fix builds; QEMU 11.1.0, development host A; readings in `out\p36-susp-3628\notes.md`, not tracked): XP and Vista offer hibernate only (no display driver for S3; XP's Cirrus driver vetoes standby), Windows 7 x86 hibernate only, ME standby and hibernate, 98 SE standby only. PASS: XP, Vista and Windows 7 hibernate idle and with Bulk-Only I/O (the I/O fails for about 10 s while the stick re-enumerates, then recovers), ME idle standby and hibernate - every resume through `XhciResumeController`'s reinitialization (QEMU has no FSC), every device re-enumerated. FAIL, open: ME standby or hibernate with a copy running on an xHCI stick hangs (no S0 or D0 reaches the controller after the wake; the hibernate never writes its file); `651d0d4`..`69d3b52` (every client request completed at the controller's D3, nothing published while suspended) did not change it, so the wait looks to be above the bus, and that attempt was reverted (owner, 2026-10-10; no proven benefit) - this ME standby/hibernate-with-I/O defect (ME-1) stays open with its evidence (`out\p36-susp-3628\me\`); and 98 SE's idle standby leaves USB dead after the wake on the base build already (untraced). **Part 2 owed** (after stage A and polling): the same with devices suspended by stage A, a D0 pended across it, and `XhciInterruptMode` 2; the bench (E460, P14s Gen 1) is the owner's. **Part 2, 98 SE standby resume, read 2026-10-11 (`p36-sby`, no code change; run-36.md "36.28 part 2 - 98 SE standby resume"): not this driver.** Traced on the `qemu` flavour: every 98 SE resume took `hcdD0Finish` inline (completion at PASSIVE_LEVEL) through the reinitialization, and with mice and hubs the resume completes and the devices work; the fatal exception 0E in KERNEL32 (`BFF9DFFF` and neighbours) follows a standby with no USB controller in the guest at all; with a USB stick attached Windows stops after the stick's D0 with this driver idle and owing nothing (unchanged with the bus's re-enumeration held), and the same stick stops the resume under NUSB's EHCI stack, while 98 SE's own UHCI driver does not resume on QEMU even with a mouse alone. Release-notes limitation drafted for 36.20. Still owed: part 2's stage A, pended-D0 and `XhciInterruptMode` 2 resumes on guests that resume (combined matrix), ME-1, and the bench.
  - **VM half done 2026-10-11** (`runs/run-36.md`, "36.28 part 2", the final matrix and the re-read's smoke): part 2 on ME (D1/D2/D4), XP (D5) and 7 x86 (D6), with stage A's client and under `XhciInterruptMode` 2; standby or hibernate with the CP1 device set on every guest that sleeps. 98 SE standby is Windows 98 SE's (a known limitation). ME-1 (ME standby or hibernate with a copy running) stays recorded, above the bus by part 1's reading. The bench half was not read (the E460's 98 SE offers no Stand by, the E7250 has no graphics driver, the P14s Gen 1 was not run): **moved** to "Planned - bench follow-up after `2.3.0.0`" (BF.5; owner, 2026-10-11).
- [x] 36.29 (was SUSPa.1) the link machine in the pure core, `xhci_susp.c`, with host suite `test_susp`: states and outcomes, the composite last-Dx rule, D0 during SUSPENDING, epoch-stamped Dx, wake classification, the episode's arming and completion by stamp, and the interleavings record 14 section 10 lists. Done 2026-10-09 (`2bb96c0`..`3915496`): `src/xhci_susp.c` and `.h` (in `src/sources`, called by nothing until 36.30), `test_susp` (445 checks, with exhaustive interleaving models of the cancel handshake and of the callback context's phases against teardown); record 14 revision 11 adds section 3.9, the split between the pure core and its executors, with the rules implementing it settled (the refusal latch by a per-slot change count `PwrSeq`, `WokeHold` per PDO after a claimed wake, START on a suspended device through `PwrWanted`, wake evidence only after the U3 request, `EXPIRED` apart from a transport timeout, teardown's step (5)). Codex-clean at round 4 (thread `01a1206f`); built x86 and amd64, every gate and the host suites
- [x] 36.30 (was SUSPa.2) the gate and the thread: one `SuspendPause` per pipe, the survivor restart, per-device slow-IRP deferral, root-port suspend and resume, the full-sample handoff at the port acknowledgement, USB 2.0 hub port suspend and both branches of the hub port look (record 14 section 3). Done 2026-10-09 (`a02e8b0`..`404feb1`): `src/hcd_susp.c` (new) with hooks in `hcd_enum.c`, `hcd_hub.c`, `hcd_cfg.c`, `hcd_io.c`, `hcd_pdo.c` and `hcd.h`; record 14 section 3.10 says how the thread carries sections 3 and 3.9 out (the thread's half of stage A: the decision pass, its completions and the wake claim are here; dispatch's half is 36.31). Inert until 36.31 fills the slots and 36.33 reads the switch. Codex-clean at round 5 (thread `01a120ba`); built x86 and amd64, every gate and the host suites. Its legs are 36.35's
- [x] 36.31 (was SUSPa.3) the PDO's power: slots, claiming, the composite rule, capabilities, WAIT_WAKE, GONE at the unlink, teardown in its order, START reopening, `PowerDown` admission and the controller's transitions (section 4 and 5). Done 2026-10-10 (`4b211ca`..`8d80e17`): dispatch's half in `src/hcd_susp.c`, called from `hcd_pdo.c`, `hcd_power.c`, `hcd_ctl.c` and `hcd_enum.c`; record 14 section 3.11 (Busy across the power dispatch, a Dx reported before its slot is filled, a per-PDO report flag ordering D0 and Dx reports, START's D0 reported once powered, the WAIT_WAKE cancel completing through a work item at PASSIVE_LEVEL, the epoch and `PowerDown` at the D3, recovery and stop). The idle rundown and 4.4 (b)'s split stay 36.32's; the 9x wake defect (5.5) and usbhub's latencies (5.2) are read in 36.35. Codex-clean at round 6 (thread `01a1214d`); built x86 and amd64 with the merge of `2.3.0.0` (`693f84f`), every gate and the host suites. Inert until 36.33 reads the switch
- [x] 36.32 (was SUSPa.4) the idle notification on the bus's callback thread: batches, the three-way claim, the context's phases, settlement of every idle IRP, quarantine (section 6 and 4.4). Done 2026-10-10 (`d2bf5b9`..`6093614`): `src/hcd_susp.c` (the idle slot and its triggers, the batch, the callback thread with its context and teardown's handoff, the rundown and quarantine), with `hcd_urb.c`, `hcd_pdo.c` (teardown (a) to (e) in order, the PDO's deferred slow IRPs flushed, a quarantined PDO never revived or started), `hcd_cfg.c`, `hcd_ctl.c` and `hcd_entry.c` (the unload waits out an abandoned callback thread); record 14 section 3.12 says how, including its deviations (no deferred-delete list, the phase moved under a spin lock, a 9x join polling an exit flag). The composite logic stays in two functions for the composite-parent devnode of 36.43. Codex-clean at round 3 (thread `01a1234a`); built x86 and amd64, every gate and the host suites. Inert until 36.33 reads the switch
- [x] 36.33 (was SUSPa.5) the switch, the counters in the counter block and `XHCISNAP`, the bounded log lines. Done 2026-10-10 (`77e5c62`, `eead296`): `XhciSelectiveSuspend` read in the start's value pass before `XhciInitController` (only DWORD 1 is on, stages A and B together; record 14 section 7); 28 counters at words 114-141 after the MSI words (`SuspSwitchRead` to `SuspNotesSuppressed`), the snapshot's counter block 142 words, the stop's dump and `XHCISNAP` naming them; bounded notes (64 per start: `susp.seq`, `susp.seq.epoch`, `idle.call`, `idle.abandon`, `idle.quarantine`); build-and-test.md says how to read them. Reviewed with 36.34 to convergence at round 7 (thread `01a1237f`); merged with `2.3.0.0` 533e721 (`4e3dea9`, its review clean, thread `01a123b9`); built x86 and amd64, every gate and the host suites
- [x] 36.34 (was SUSPa.6) the test client under `test\`, and the packaging gate refusing it. Done 2026-10-10 (`fd2cc2d`..`8860100`): `test\pmclient` - `XHCIPM.SYS`, a WDM function driver and power policy owner for QEMU's USB HID devices (`USB\VID_0627&PID_0001`) that on `PMCTL.EXE`'s demand requests D-states, WAIT_WAKE (its requester's callback counted apart from the IRP's completion: the 9x defect's read) and the idle notification, and keeps a read posted; `make-release.ps1` refuses its names or its marker `XHCI98_TEST_CLIENT_DO_NOT_SHIP` in every package, the upload set and the staged release before either rename (`Find-TestClientFiles`), with executed fixture tests in `test-package.ps1`. Codex-clean at round 7 (thread `01a1237f`)
- [x] 36.35 (was SUSPa-V) stage A's legs (record 14 section 10): the test client on 98 SE, 2000 (SMP, Driver Verifier), ME and XP, with the 9x WAIT_WAKE completion read and wake disabled on 9x if the defect holds; hidusb's own idle on XP, Vista and 7; behind QEMU's hub; composite interleavings; unplug at each state; controller disable, enable, D3 and restart mid-transition; the fault-injection leg; `XhciSelectiveSuspend` at 0 reading as the build before it; the matrix unchanged; x86 and amd64 and every gate
  - **Done 2026-10-11** (`runs/run-36.md`, "36.35 - stage A's completion pool", the final matrix's stage A rows, the re-read's RPM): the client on 98 SE (shutdown in D0 in 4.5 s, S3-S5, A3-A6), ME (A0-A3, STOP/START, shutdown), 2000 and 2000 SMP + Verifier (B0-B5, shutdown with a read pending), XP (S4/S5, C3-C5) and 32-bit Vista and 7 (C0/C1). Windows' HID driver never idles QEMU's mouse (C1), and A3/C4's mid-transition windows cannot be hit in QEMU.
- [x] 36.36 (was SUSPa-E) stage A's bench (owner): a real remote-wake HID device on a 98 SE machine and on the E460's Windows 7
  - **Moved** to "Planned - bench follow-up after `2.3.0.0`" (BF.2; owner, 2026-10-11): the owner skipped the selective-suspend and remote-wake readings on the bench.
- [x] 36.37 (was SUSPb.1) stage B's design: record 14 section 8 grown into the record proper - Function Suspend with wake enable per interface, the function remote-wake capability, Function Wake Device Notifications (DNCTRL, the event, interface-to-function ownership), PLS=Resume on USB 3 root ports and the `xhci_link.c` classification, SuperSpeed hub ports (no C_PORT_LINK_STATE on a wake) - reviewed to convergence before code. Done 2026-10-10 (`2e9540a`..`1109793`, branch p36-suspb, merged into p36-suspbc `a899d6e`): record 14 sections 8.1 to 8.13 and a stage-B table in section 9; Codex-converged at round 3 (thread `01a12333`); the decisions of 8.13 are orchestrator decisions, owner confirmed 2026-10-10
- [x] 36.38 (was SUSPb.2) stage B built on stage A's machine, with host vectors. Done 2026-10-10 (`fe68330`..`a75c19b`, branch p36-suspbc): `xhci_susp.c` (STOP_FUNCS appended as step 13, the SuperSpeed order, `Armed` from the PDOs' `FwArmed`, the refusal's disarm, the strict disarm, every SuperSpeed wake the host's resume, the notification window and latch in the link record, per-PDO `ArmNow` and the per-function rearm, SuperSpeed hub observation, verification and evidence), `xhci_link.c` (WANT_RESUME on Resume with CCS and PED), `xhci_func.c` (first interfaces, ownership), `hcd_susp.c` (FUNCTION_SUSPEND per wake function, the latch's three consumers, the DPC hook, the configuration transaction with GetStatus(interface)), `hcd_sshub.c`, `hcd_cfg.c`, `hcd_pdo.c`, `hcd_dev.c`, `xhci_evt.c`, `xhci_init.c` and `hcd_ctl.c` (DNCTRL N1 only with `XhciSelectiveSuspend` 1), counters 142-159, the HCD header's words 204-272 (237-305 after the merge with 36.24's stage 2 words, integration wave 4) and XHCISNAP; record 14 section 8.10's as-built paragraph. Codex-converged at round 3 (thread `01a12405`); built x86 and amd64 all, every gate, the host suites
- [x] 36.39 (was SUSPb-V) stage B's legs on QEMU (root ports only: QEMU has no SuperSpeed hub). Smoke boot 2026-10-10 (owner rule: the condensed matrix runs the full set on the integrated build), release build `300fc27`, development host A, a QEMU usb-storage on a qemu-xhci SuperSpeed port: Windows 7 x64 with `XhciSelectiveSuspend` 1 - DNCTRL read back `0002h`, GetStatus(interface) STALLed by QEMU's model (`SsSuspCapFailed`, no capable function, so no arm); usbstor sends no Dx on a disable, so x64 (no amd64 test client) shows no suspend - and at 0 DNCTRL not read, enables 0, no stage A or B counter, the stick read; Windows 7 x86 with the test client bound to the stick (a copy of `XHCIPM.INF` naming `USB\VID_46F4&PID_0001`, not tracked): WAIT_WAKE refused NOT_SUPPORTED, `PMCTL D2` suspended the link (susp.seq `00102110`, PORTSC PLS 3 in two readings 6 s apart), `PMCTL D0` resumed it (susp.seq `00120250`, PLS 0). Still open: the matrix's 36.39 checks
  - **Done 2026-10-11** (`runs/run-36.md`, the final matrix's SB rows): SB1 (the switch at 0) on every guest; SB2-SB7 on 2000 SMP + Verifier, XP and 7 x86, SB2 on 7 x64, SB6 under `XhciInterruptMode` 2. SB4's mid-D2 window cannot be hit in QEMU, and SB7 (function wake) has nothing to read there: QEMU makes no function wake.
- [x] 36.40 (was SUSPb-E) stage B's bench (owner): a SuperSpeed device with remote wake on a root port and behind a USB 3 hub
  - **Moved** to "Planned - bench follow-up after `2.3.0.0`" (BF.3; owner, 2026-10-11): VM-only, not read on the bench. The owner skipped it (the AX88179 behind its USB 3 hub was not run); both halves are untested ground.
- [x] 36.41 (was SUSP.2) the Windows 98 SE audio-load wedge (carried since `2.0.0.0`, release notes' known limitations; 28.3): a USB audio device attached about 40 s after a cold boot wedged the guest 5 of 6 times (taskbar clock stopped), after 120 s none of 20; every IRP the bus was given had been answered, the wait is in the configuration manager after SELECT_INTERFACE to alternate 0 and QUERY_CAPABILITIES, and `1.2.0.0` showed it too. In this phase because stage A changes what QUERY_CAPABILITIES answers (D1/D2, wake): re-measure it on stage A's build first (better, worse or the same), then find where the configuration manager waits (a debug `VMM`/`CONFIGMG` or a trace of the devnode's state), and fix it if a bus answer is the cause; if it is Windows 98's own, it stays a documented limitation with the measurement refreshed. Done 2026-10-10: the baseline (`4b5c936`, run-36.md) found it not bus-side (UHCI with no `xhci98` wedges too); the qualifying re-measure on stage A's build (`4282007`, run-36.md "36.41's qualifying re-measure") reads the same at `XhciSelectiveSuspend` 0 and 1 (120 s: 6 of 9 and 5 of 9; 300 s: 3 of 3 and 2 of 2) - Windows 98's own, a documented limitation; the release notes' wedge text no longer advises waiting
- [x] 36.42 the Intel port switchover off by default (owner, 2026-10-09, after SweetLow's issue 17 comment 6082858626: an unrouted port stays on the EHCI and works there; the decisions table, "The Intel port switchover"): `XhciIntelPortSwitch` defaults to **0** in both INFs on every install path and in the driver when the value is missing, of another type or unreadable, reversing 34.3's default 1; 1 and 2 mean what they meant. Design record 16 revision 4. A user script, `portsw.vbs`, Windows Script Host VBScript (98 SE and ME have no `reg.exe`, 2000 none by default), at the root of every package directory: shows the value on every xhci98 controller (found by `DriverDesc` among the USB class key's `NNNN` subkeys, listed by WMI on NT and a `regedit /e` export on 98 and ME), sets 1 or 0, and says a restart is needed. Host vectors, the INF gate's row, footprints and self-tests, the packager and its self-tests, the release notes (an existing install keeps its value under FLG_ADDREG_NOCLOBBER; the script changes it), README, readme template, build-and-test.md. VM legs on the `release` package: the script on 98 SE, 2000 and 7 x64 (show, 1, 0, each across a restart) and a fresh install writing 0; QEMU's controller is not Intel, so the switchover itself is the owner's bench (a listed Intel chipset at 0 and at 1)
  - **Done 2026-10-10** (`dfb0508`, `aa4f84f`, `1d50090`; the Codex loop clean at its third round; built x86 and `-amd64`, all flavours, every gate and the host suites passing). VM legs on the `release` package (development host A, QEMU, `qemu-xhci`): Windows 98 SE (NUSB base, Add New Hardware wizard), 2000 SP4 (Found New Hardware wizard) and Windows 7 x64 (command-line install, F8 boot) each a fresh install showing `0 - off`; then `portsw.vbs 1`, a restart, `1` kept; `portsw.vbs 0`, a restart, `0` kept; exit code 0 throughout; on 7 x64 a non-elevated `1` raised the elevation prompt and the elevated copy set it. `XHCISNAP` at verbosity 2 after each restart: `psw.gate=00000000` and no other `psw.` record on all three, because QEMU's `1B36:000D` is not Intel and the value is not read there. Bench clause owed (2.3.0.0): the switchover at 0 and 1 on a listed Intel chipset
- [x] 36.43 composite devices as devnodes (owner, 2026-10-10, after SweetLow's comment on issue 17; the decisions table, "Hub class and composite splitting"; design record 13 section 10.12): every composite device the bus splits gets a composite-parent PDO under the project-owned `XHCI98\COMPOSITE` (`&VID_&PID_[&REV_]`, no compatible id; its instance id the device's serial id or place; its text `iProduct`), bound by both INFs to `xhci98.sys` as a composite FDO (`hcd_hubfdo.c`'s code, no door), with the function PDOs as its children, so a hub port maps to one devnode; 33.4's parent rules (relations, ancestor, invalidation, let-go, dormant tiers) for a hub or a composite parent; a parent's disable and enable re-presenting its functions in place rather than cycling the port; the serial tests on group heads; `GET_NODE_CONNECTION_DRIVERKEY_NAME` answering the parent's key and 36.17's USB device interface moved to the parent; `txtsetup.oem` mapping the id; the INF gate's `HCD-COMP` and `HCD-COMPPAGE` and `HCD-HUBCOPY` over the composite install, with self-tests and footprints; two counters appended. Host vectors (`test_func`), the Codex loop, x86 and `-amd64` all with every gate. Stage A's code (branch `p36-susp`) needs, and this task does not make: its slot and batch walks skip a composite parent, its eligibility refuses one as a hub, its chain cap counts functions only (record 13 section 10.12, "Power"). Legs on the `release` package: QEMU's `usb-audio` (a parent with one function) on 98 SE, ME, 2000, XP, Vista and 7, x86 and x64 - one parent per port in Device Manager by connection with the function beneath it, `usbview` and Windows' USB pages showing the parent, the parent's and the function's disable and enable, a surprise removal, an update from `2.2.0.0` (re-detection once, the function working), non-composite devices unchanged; a real multi-function device (UAC with HID) by passthrough where the host offers one, else owed to the bench
  - **Progress 2026-10-10** (not ticked): design `364e69b..90b9655` (Codex clean at round 2), code `80eb758`, `5edd643` (Codex clean at round 1); built x86 and `-amd64`, all flavours, every gate and the host suites. Legs on the `release` package (development host A, QEMU): Windows 2000 legs 1-8 and 10 pass (both parents installed silently, the tree, `USBIF list` on the parent only, usbview and the Power tab naming the parent, parent disable/enable three times with the function re-presented in place, function disable/enable, surprise removal, hub and controller disable/enable); Windows 98 SE install, tree and interfaces pass, then the guest UI froze after usbview was closed, the signature of 36.17's earlier 98 SE hangs, not shown to be this task's. Owed: XP, Vista, 7 x86 and x64 and ME, the update from `2.2.0.0` everywhere, 98 SE's remaining legs, the release notes' TODOs; stage A's changes (the parent excluded from its votes) wait for both merges
  - **Done 2026-10-11** (`runs/run-36.md`, the final matrix's K2 and K3, and the bench): the legs on every guest, and the update from `2.2.0.0` re-detecting the composite once. A real multi-function device on the bench: the Sound Blaster Play! 2 and the C-Media, each a composite devnode on the E7250 under Windows 7 x64 (CompositeParents in its snapshots).
- [x] 36.44 the Windows 98 SE hot-plug hangs seen in 36.17-36.19's legs (2026-10-10): Windows stopped responding with the pointer still moving (1) after a stick with a never-seen serial was unplugged while its Add New Hardware wizard was open and the same serial plugged back 40 s later, the replugged stick never addressed; (2) after eight devices were hot-added about a minute after the desktop came up; (3) after the root hub's Power properties page was closed. Each is reproduced on the `qemu` flavour, against `2.2.0.0`, and with Windows 98 SE's own UHCI stack as a control (36.41's method); one found to be the bus's is fixed with host vectors and a review loop, the others recorded with their evidence (`runs/run-36.md`).
  - **Done 2026-10-10** (`e811dbf`; docs only - no hang is the bus's, so no code, review loop or build; `runs/run-36.md`, "98 SE hot-plug hangs"). Reproduced on `2.3.0.0` at `bfec7b0` (`qemu`), on `2.2.0.0` (`release`) and with Windows 98 SE's own UHCI stack, development host A, QEMU 11.1.0 TCG, on a copy of the guest that hung. (1) **Not the bus's:** the configuration manager sends no relations query while the wizard holds the gone devnode, so the port waits in Gone by design (no slot, nothing in flight) and enumerates the replug once the query comes; the wedge comes from keystrokes typed into the wizard and is identical on the UHCI control and on `2.2.0.0`. (2) **Not the bus's:** 36.41's audio-load wedge (its UHCI control's VMM EIPs; the bus serves throughout; never without the audio device; also on `2.2.0.0`). (3) **Not a hang:** Windows' Power popup ignores Esc on every stack (UHCI control, `2.2.0.0`); Alt+C closes it and the UI is live. (4) Added by the orchestrator from 36.43's legs (UI frozen after usbview closed): **not the bus's** - it needs the audio device and usbview together and comes on `2.2.0.0` and the UHCI control alike (`USBAUDIO(01)+00002ED4` or a frozen UI), none in 32 control cycles with audio and no usbview. QEMU answered its monitor through every freeze, so none is the QEMU process stop.
- [x] 36.45 string and other uncached descriptors through the door (issue 17 item 8; owner, 2026-10-10): `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION` answers a request the cache cannot (any string and language id, the language list, another configuration, the BOS, any other type) with a real `GET_DESCRIPTOR` on the device's default endpoint, with usbhub's length semantics, pended to the controller thread's EP0 path; design record 13 section 8.11, reviewed before code, host vectors for the request's checks; legs with SweetLow's usbview (x86 and x64) on 98 SE, ME, 2000, XP, Vista and 7 x86/x64 showing the Manufacturer, Product, Serial and interface strings of a stick, a mouse or tablet and a hub, a stick copy with `fc` and the mouse moving while usbview hammers the strings, and an unplug during a pending request
  - **Done 2026-10-11** (record 13 section 8.11; `96b9a27`..`cdd2fb2`, merged `18b49e9`; the final matrix's K4): usbview's strings, a stick `fc` and the mouse under the hammer, on every guest. On real hardware USBView read the Play! 2's and the C-Media's strings on the E7250 under Windows 7 x64 (`temp\2300bench\play2.txt`, `cmedia.txt`).
- [x] 36.46 an interface with more endpoint descriptors than its `bNumEndpoints` (GitHub issue 17 item 7, SweetLow): a low-speed HID mouse, VID `073A` PID `2230`, whose interface declares one endpoint and carries two (`0x81` IN and `0x01` OUT, interrupt, 8 bytes, `bInterval` 10) is configured with one pipe under Microsoft's stack and never configured under `xhci98` (Current Config Value 0, no open pipe). Find every point where it fails, with its exact bytes as host vectors through every pure path (the configuration walk, the composite split and filtered descriptor, the select's pipe set and Configure Endpoint plan, the Low-Speed interrupt endpoint context at `bInterval` 10, the 239-byte report descriptor over an 8-byte EP0, hidusbf's rewritten `bInterval`), and take such a device as usbport does: honour `bNumEndpoints`, ignore the surplus, never less strict than xHCI requires. Regression legs on 98 SE and 7 x64 with QEMU's mouse, tablet, keyboard and a stick; the mouse itself on the bench (owner or SweetLow)
  - **Done 2026-10-11** (`8ea873f`, `6463fc9`; the final matrix's K5 and the L5/L6 rerun): the host vectors and the regression legs. The mouse itself was not read on the bench (owner, 2026-10-11).
- [x] 36.47 `XHCISNAP`'s header block names the system it was captured on (owner, 2026-10-10): the OS version, build and service pack, x86, x64 or 32-bit on x64, the CPU, the memory, and the manufacturer, model, board and BIOS where the OS exposes them (Vista SP1 and 7 in `HARDWARE\DESCRIPTION\System\BIOS`; 2000 and XP the BIOS version and date only; 98 and ME what `Enum\Root\*PNP0C01` holds). Never the computer name, the user name, a domain, a serial number, a UUID or an asset tag, because the `.TXT` is pasted publicly. Legs: the header read on every guest.
  - **Done 2026-10-10** (`c68391d`, review fixes `3796b59`, `40dade1`, `5e85c5f`, `3bdf8c4`; `-nosysinfo` (owner addition, 2026-10-10) `3c074d1`, `297c945`; the Codex loop clean at its seventh round, thread `01a12386`; `XHCISNAP` built, `selftest.cmd` 15 cases passing, imports KERNEL32 and ADVAPI32 only, source charset clean; `src\` untouched). The header's "captured on" block: the OS by `GetVersionEx` with its name, x86 or WOW64, the allow-listed SMBIOS strings (Vista SP1 and 7), `SystemBiosVersion`/`SystemBiosDate` (2000, XP, XP x64), `BIOSVersion`/`BIOSDate` (98 SE and ME: the export shows no `BIOSName` on SeaBIOS), the CPU and processor count, the RAM; any registry string holding the computer, user, domain or workgroup name is withheld (code-page fold); `-nosysinfo` beside a dump skips all of it. Legs (development host A, QEMU 11.1, x86 `release` package of integration wave 2): 98 SE, ME, 2000, XP and Vista x86 read and correct, `-nosysinfo` on ME, XP and Vista x86; XP x64, Vista x64, 7 x86 and 7 x64 owed to the combined matrix (default and `-nosysinfo`).
  - **Follow-up 2026-10-10 (wave 3 matrix, M-3; `126a7e9`):** XP x64 has neither `SystemBiosVersion` nor `SystemBiosDate` and keeps the SMBIOS strings in `HARDWARE\DESCRIPTION\System\BIOS`, so the matrix read "BIOS not in the registry" there. 2000, XP and XP x64 now read that key, through the same allow list, when both values are absent; the existing sources keep their order (`-selftest-sysinfo` checks the choice; `selftest.cmd` 19 cases). Read on the XP x64 guest (development host A, `-selftest-sysinfo`): SystemManufacturer QEMU, SystemProductName, SystemVersion, BIOSVendor SeaBIOS, BIOSVersion, BIOSReleaseDate, no name. A dump's header on XP x64 is owed to the final matrix.
- [x] 36.48 `XHCIQUAL`'s report header names the machine too (owner, 2026-10-10), read under DOS from the SMBIOS tables (types 0, 1 and 2: BIOS vendor, version and date, the system's manufacturer, product, version and family, the board's manufacturer and product), `CPUID` (vendor and brand), the memory and the DOS version, with the BIOS date and model byte where there is no SMBIOS. The same rule: no serial number, UUID, asset tag or SKU. Host vectors for valid, 3.0, truncated and garbled tables; a QEMU case with `-smbios` strings set, serials included, shows them left out.
  - **Done 2026-10-10** (`dfa5222`, review fixes `5256531`, `e7d31d3`, `8bf7f4a`, `7738ec8`, `90fc4fe`; the owner's `--no-sysinfo` `044921a`; README sample `8e2ebf6`; the Codex loop clean at its sixth round; `XHCIQUAL` built, its host suites passing with the new `test_sysinfo` (243 checks, windows on a guard page), `check-build-exit-codes.ps1` 6 cases (a crashing suite now fails the build), the QEMU matrix 50 cases on development host A). `xhciqual/sysinfo.c` (pure) and `sysprobe.c`, `sysasm.asm` (CPUID, every register saved): SMBIOS `_SM3_`/`_SM_`/`_DMI_` with checksums, the table direct below 1 MB, DPMI `0800h` above, assembled across 1 MB, at most 64 KB and 4096 structures walked; Types 0, 1, 2 and 4; CPUID vendor, brand, family/model/stepping; RAM from E820 (DPMI `0300h`), else Type 17, else Type 19; DOS `30h`/`3306h` and INT 2Fh `1600h`. No identifiers: serials, UUID, asset tags, SKU and part numbers are never copied, and any printed string containing one (any length, any case; the UUID as hex in both byte orders) prints `(withheld)`, as does every SMBIOS string when the walk cannot finish. No SMBIOS: the ROM date and model byte. The quick scan: two lines on the screen, the block in the log. `--no-sysinfo` skips all probing and says so. Owed to the bench: a real machine's header under DOS (the E7250 or any fleet machine at 36.final) and a pre-SMBIOS one if one turns up.
- [x] 36.49 Low- and Full-Speed devices behind a hub on a USB 2.0 root port whose speed table names neither (GitHub issue 18, SweetLow; `docs/issues/13-via-vl805-usb2-integrated-hub-speed-ids.md`): on the VIA VL805 (`1106:3483`) the USB 2.0 port lists only its integrated hub's High Speed, as xHCI 1.2c 7.2.1 allows, and `HcdHubPlace` refused every Full- and Low-Speed device behind that hub. A general rule, on by default (orchestrator decision on Codex's advice, owner confirmed 2026-10-11): such a device takes the default ID (1 Full, 2 Low) where the table has no entry of that speed and does not list the ID; an explicit entry always wins; root-port decoding and task 35.1 unchanged; the provenance on the device record for every later decode; `XhciUsb2HubSpeedFallback` `0` turns it off; counters at words 162 to 166, bounded notes, `XHCISNAP`, the VL805's firmware dword noted at start; `implementation-invariants.md`, "Port Speed Decoding", amended. Host vectors on the VL805's table and on conflicting ones, the Codex loop, x86 and `-amd64` all with every gate; a QEMU regression leg (QEMU publishes no USB 2.0 table). The hardware result is not observed before release: the fix has never run on a VL805, and it does not block the cut.
  - **Done 2026-10-10** (`e7d4243` the code and host vectors, `d213f62` the docs, `2fddc57` round 1's fix; Codex thread `01a12643`: round 1 no P1/P2, its P3 fixed; round 2, the confirmation of that fix, returned nothing). Built x86 and `-amd64` all, every gate, the host suites, `XHCISNAP` and its self-test. QEMU on development host A, Windows 98 SE, the `qemu` flavour: QEMU's Full-Speed hub hot-added on a root port with a mouse and a keyboard behind it, both enumerated at Full Speed (route `00001`, tier 1), the mouse moving the pointer, `XHCISNAP` showing the switch on and no fallback taken (QEMU publishes no speed table); QEMU models no Low-Speed device. Not observed on hardware before release.
- [x] 36.50 the x64 binaries test-signed - **a prerequisite of the `2.3.0.0` cut** (owner, 2026-10-11; design record 19, reversing design record 11 decision 9 for the x64 package only). Done when all of these are: the packager option and the `make-release.ps1` refusal; the certificate made by the owner (`scripts\local\testsign\xhci98-test.pfx`, thumbprint `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF`); the `2.2.0.3` re-cut signed; the Vista x64 and Windows 7 x64 test-signing legs, on and off, and XP x64; the procedure in `README.md`, the release readme and the release notes. Every published x64 directory carries `xhci98.sys` and `xhciuas.sys` with an embedded Authenticode signature by the project's own test certificate - SHA-1 file digest, no timestamp, no catalog, both INFs byte-identical - applied by `make-package.ps1 -TestSignPfx` at staging, with `xhci98-test.cer` beside them, and `make-release.ps1` refuses an x64 cut without it; the x86 binaries are never signed. So Vista x64 and Windows 7 x64 load the driver in test-signing mode (`bcdedit /set testsigning on`, once) instead of only on an F8 start. The key is made by the owner (`scripts\package\make-testcert.ps1`) and held only in the git-ignored `scripts\local\testsign\` or outside the tree. Into the `2.2.0.3` TEST build at once (re-cut, same version and date, the x86 halves byte-identical to the unsigned cut's) and every cut after it. Legs: Vista x64 and Windows 7 x64 in test-signing mode, installed from the signed zip and started with no F8, twice, the controller and devices working and "Test Mode" shown; the same without test-signing mode (the Code recorded); XP x64 installing and loading the signed binary as before. - **done 2026-10-11:** the rehearsal legs on Vista x64, 7 x64 and XP x64 (run-36 "36.50"; Code 39 on Vista, Code 52 on 7 with test-signing off), then the owner's signed `2.3.0.0` cut (`out\p36-stamp3`, zip SHA-256 `879768458F92CE14EC225DFB45E1EDDB982D1CE2DB7BD9555560A1DD3112265E`): all four x64 images signed by `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF` (SHA-1, no timestamp) and the build plus the signature only, the x86 images byte-identical to the stamped build, the key containers SignTool left deleted by the owner
  - re-cut 2026-10-12 owed (36.56 + 36.57): restamped `DriverVer` `10/12/2026,2.3.0.0` on `p36-stamp4` (`c93037d`), so the INFs differ from the first cut's and Windows 7's driver store takes the new package on Have Disk; the signed cut and its signature check to follow.
- [x] 36.51 every registry value the driver reads written by the INFs at the driver's own default (owner, 2026-10-11; the decisions table, "Every registry value written by the INFs"): the inventory from the code (`build-and-test.md`, "The registry values the driver reads"), `XhciWideMap` 0, `XhciSelectiveSuspend` 0 and `XhciUsb2HubSpeedFallback` 1 added to `[Xhci.AddReg]` and `[Xhci.AddReg.NT]` of `src\xhci98.inf` and `[Xhci.AddReg.NT]` of `src\xhci98-amd64.inf` with the same `0x00010003` flags as the sixteen, the INF gate's `VAL-*` table, self-tests and footprints, the README, release notes, release readme template and records; fresh install and update over `2.2.0.0` on 98 SE, ME, 2000, XP, XP x64, Vista x86 and 7 x64 - done (p36-regall, merged at `8e74422`): review converged in two rounds; both builds, the INF self-tests (1118), the packager (471) and the host suites pass; legs on development host A, every guest PASS: a fresh install wrote all nineteen at their defaults and `XHCISNAP` read the three new ones as set; an update over `2.2.0.0` (whose INFs wrote eleven) kept the user's values and added the eight missing at their defaults. `XhciIntelPortSwitch` stays at `2.2.0.0`'s `1` on update, as 36.42 documents; the root hub's Driver tab keeps the old version, and 98 SE and ME keep the old driver until a restart they do not ask for, both already documented and now worded for any update
- [x] 36.53 `XhciInterruptMode` 1 the interrupt line only and 3 a message-signalled interrupt only (owner, 2026-10-11; design record 15 revision 7 and decision 8, design record 18 revision 12 and decision 8, amending the 2026-10-09 mode rules of 36.5, 36.15 and record 18 sections 4.2 to 4.3a). Mode 1 on every system: on Vista and 7 a message Windows grants anyway fails the start (Code 10) on x86 and amd64, helper bound or not - it no longer runs as 0, and reasons 11 and 13 are no longer produced. Mode 3, which read as 0: on Vista and 7 the requirements filter keeps only the message configurations (outcome 12, `XhciMsiKeepMessage`), the message runs where granted and connected (reason 15), and the start fails (Code 10) with no message granted (16), the message not connected - 32-bit without `xhcimsi.sys` bound, a refused connection, or Windows connecting the line instead (17) - or on Windows 98 SE, ME, 2000 and XP, 32- and 64-bit (18). 0 and 2 unchanged; 4 and above still run as 0. A start the delivery refuses records itself before it returns: the counter block, the `qemu` flavour's `M86 S` record, and the start's notes with `start.refused.delivery`, flushed to DebugView through `HcdLogFlush` when `XhciLogDebugView` is set. Host vectors in `test_poll` for every row by OS family, mode, grant, routines and pin, and `XhciPollConnected`; `test_msi` for the message keep; `XHCISNAP` names reasons 14 to 18 and outcome 12. Legs: 7 x64 and Vista x86 with the helper under modes 0 to 3 with MSI offered; 7 x86 without the helper under mode 3; XP and 98 SE under mode 3 and modes 0 to 2.
  - **Done 2026-10-11** (`5b6ddfc` design, `4856baa` code, `943f551` docs, `a2d5837` round 1's documentation fixes; review round 1 no P1/P2, round 2, the confirmation of that fix, returned nothing). Built x86 and `-amd64` all, every gate, the host suites, `XHCISNAP` and its self-test, on `4e11974` (2.3.0.0 merged). QEMU on development host A, every row passed (`docs/contributing/runs/run-36.md`, "36.53"): 7 x64 and Vista x86 with the helper ran a message under 0 and 3 (outcome 12 under 3) and the line under 1; mode 1 with only a message offered, mode 3 with a failed connection, with `MSISupported` 0, and on 7 x86 with the helper disabled each failed with Code 10 and reasons 14, 17, 16 and 17; XP and 98 SE failed mode 3 with Code 10 and reason 18 and kept modes 0, 1 and 2. Not read: a controller with no line, mode 3 on MSI rather than MSI-X, 64-bit Vista, ME, 2000, XP x64.
- [x] 36.54 Transaction Error recovery on a stream endpoint (owner, 2026-10-11: fixed before the `2.3.0.0` cut; was the planned item STE.1). A UAS drive whose data stream takes a Transaction Error moves data again promptly, without a replug. Design record 17 left stream endpoints out of the soft retry (EP0, isochronous and stream endpoints keep their handling), so the failed command was left to `xhciuas`'s class timeout, abort, LUN reset and port reset. Phase 36's matrix (XP x64, `qemu` flavour, `XhciQemuInject` fault 16 on a UAS data-in stream TD) read the failed read completing cleanly, then every later command timing out until the device was removed and added again (2 of 3; the third recovered after 266.6 s); the 36.22 stage-1 build behaves the same, so this predates stages 2 and 3, and a Bulk-Only stick under the same fault is recovered by the soft retry with its copies `fc` clean. Done when: the stuck runs' failure to converge is diagnosed from traces; record 17 gains a reviewed section choosing between a soft retry for stream TDs (Reset Endpoint and Set TR Dequeue for the halted stream, the stream's doorbell rung with its stream ID, bounded as the Bulk-Only retry is), a prompt `xhciuas` recovery in place of the class timeout, or a fix of whatever keeps the existing recovery from converging; host vectors for the stream ring's dequeue and DCS after the reset; legs: the fault 16 injection on XP x64, 2000 and 7 x64, a UAS copy `fc` clean after it with no replug, the Bulk-Only control unchanged, faults 15 and 17 on UAS still recovering, and a plain UAS copy and VERIFY on 98 SE and 7 x64 (`release`) unchanged.
  - **Done 2026-10-11** (p36-ste: design `9b41fee`, review fixes `10460c8`, `86f8393`, `c1c5029`, `f9714be`; code `9f1f11f`, review fixes `c6408d0`; docs `d753a28`, `deb6269`, `250065a`; design review converged at its fifth round, code review at its second; built x86 and `-amd64` all, every gate, the host suites and `XHCISNAP`'s selftest passing). Root cause, traced on development host A (QEMU trace events and guest memory): QEMU 11.1's xHCI ignores a Stream Context's DCS bit (`hcd-xhci.c`, `xhci_find_stream` calling `xhci_ring_init`, which forces the consumer cycle to 1) after every configure, stop or reset of a streams endpoint, so each stream whose ring sat on a cycle-0 lap went dead, and every `xhciuas` recovery step re-forced it; the driver's contexts and Set TR Dequeue DCS were correct. Fault 17's and B9(b)'s "no timeout" was the class driver's length-scaled timeout (160 s for 1 MiB, observed); the 98 SE `XHCISNAP` hang was Windows 98's file system queued behind the stuck read, not the door. Record 17 section 4.12: the soft retry on a stream; stream rings restarted at their base at RESET_PORT; a QEMU-only (`1B36:000D`) stream resync; a `StreamFault` leak after client resets fixed. Counter words 167-172 appended. Legs on the `qemu` flavour, each fault run on a fresh UAS device after its rings had wrapped: fault 16 on the data-in stream on XP x64 (3), Windows 2000 SP4 at two processors under Driver Verifier (3) and 7 x64 (3), and on 98 SE during a copy with `fc` (3), and on the data-out stream on XP x64 (3): every run recovered in place with no stall, no `xhciuas` timeout or reset and no replug (`StreamRetryResets` +1 each); faults 17 and 14 recover after one class-timeout stall (XP x64, 2000, 7 x64); fault 15 clean at 1 MiB and, B9(b), at 2 MiB with `XhciWideMap` 1 and 0; the Bulk-Only fault-16 control unchanged (XP x64, 2000); `release` regressions on 98 SE and 7 x64: UAS copies `fc` clean and `VERIFY` 0 bad. Evidence `out\p36-ste\legs`. A real drive's Transaction Error, and a real controller's partial progress across the soft retry, are bench items.
- [x] 36.55 the lost-interrupt backstop is taken in the tolerance clock's DPC, not on the controller thread (design record 17 section 4.1, "Amended"; orchestrator decision, owner confirmed 2026-10-11). The final matrix found it: on Windows 98 SE, under `XhciInterruptMode` 1 with the `qemu` flavour's dead line, a Bulk-Only copy from an MS-DOS prompt stopped, 2 of 2. Traced, the controller thread stopped being scheduled while the copy's request waited for a completion only the backstop would drain; the tolerance clock's timer DPC kept running and an event stood at the dequeue. Done when the step runs in that DPC (one peek and at most one queued drain per tick, no power gate, the drain's own admission under the controller lock), the release closes the event DPC with an interlocked exchange, records 17, 15 and 13 say so, the Codex loop converges, x86 and `-amd64` build all with every gate and the host suites, and these legs pass on the `qemu` and `release` packages: 98 SE dead line mode 1 twice (an MS-DOS prompt copy, and the same after a HID mouse is installed), 98 SE mode 0 with injected lost interrupts during a copy, ME dead line mode 1, XP and 2000 dead line mode 1, 2000 SMP with Driver Verifier dead line, and the mode 0 and 2 regressions on 98 SE and Windows 7 x64
  - **Done 2026-10-11** (p36-dl98: design `7debbe0`, code `2ad2b0d`, review round 1's wording fix `5d96e69`, docs `b7c2b55`, merged with `2.3.0.0` at `e9d25b9` in `2c74b47`; review round 1 no P1/P2 and one wording P3, round 2 the confirmation and round 3 the merge returned nothing). Built x86 and `-amd64` all, every gate and the host suites, `XHCISNAP` and its self-test, on the merge. Every leg named above passed on `5d96e69`'s packages, and the 98 SE copy again on the merge's `qemu` build (runs/run-36.md, "36.55"). Found on the way: the stop needs a drive in MS-DOS compatibility mode (the BIOS-mapped E: of a stick attached at power-on), and is reachable in mode 0 by one lost interrupt; every release since `2.2.0.0` has the exposure. 98 SE Stand by with such a stick still stops after the wake (36.28 part 2), so that limitation is unchanged
- [x] 36.56 `portsw-on.bat` and `portsw-off.bat` beside `portsw.vbs` in every package directory (owner, 2026-10-11, at the E7250 bench, where a stick ran at USB 2.0 speed with the switchover at its default 0): each changes to its own directory, shows the value, sets 1 or 0 through `portsw.vbs` and says a restart is needed; run as administrator. Folded into the next cut after the bench (a recut of `2.3.0.0` or the following release), never into the benched zip. Design record 16 section 5a, the packager's staging and copy lists, its self-tests and the package-content gates, the README's switchover section and package table, the release notes and the readme template; legs: each batch on 98 SE, 2000, XP and 7 x64 (elevated and not), the value read back with `portsw.vbs show`. **Done 2026-10-11** (p36-pswbat: design `e67f778`, files and packager `c5e1e29`, review fixes `ae65c26`, `9bb2c5b`, build-and-test `837bc17`, the legs' two text points `bb80117`; review round 1 two P2 and two P3, round 2 one P2 and three P3, rounds 3 and 4 nothing). One file for `COMMAND.COM` and `cmd.exe`: `if "%OS%"=="Windows_NT" goto nt`, `goto run`, and only `cmd.exe` reaches `cd /d "%~dp0"`. Self-tests hold every line to a whitelist (`Get-PswBatchFaults`) with broken controls. Legs (development host A, the stamped `2.3.0.0` release package plus the two files): 98 SE (double-click, prompt, from `C:\`), ME, 2000, XP, 7 x64 (Run as administrator, not elevated, the prompt declined) all PASS, the revised text read again on 98 SE, 2000 and 7 x64; `COMMAND.COM` sees `cscript`'s exit code. A rebuild does not reproduce the cut's bytes (link timestamp, checksum, PDB path), so the re-cut publishes the first cut's binaries, restored and verified against its hashes (run-36, "36.56"). Re-cut owed to the owner.
- [x] 36.57 an explicit isochronous `StartFrame` is never refused for want of Frame IDs: without CFC a request that starts a data flow carries a Frame ID on its first TD, and anything the controller cannot be told goes out with Start Isoch ASAP (owner's bench, 2026-10-11, on the signed `2.3.0.0` cut: a Sound Blaster Play! 2, `041E:323D`, asynchronous playback with feedback endpoint `0x83`, played only hiss on the E7250, Windows 7 x64, at 44.1 and 48 kHz; a C-Media device played; design record 13 section 6.10). The snapshot read `IsoSubmits` 0, 4,420 commands and 1,882 Stopped - Length Invalid events, and the controller (`8086:9CB1`) has no CFC. Windows 7's `usbaudio.sys` gives every data URB and every feedback read of an asynchronous stream an explicit `StartFrame` (static, `legal-provenance.md` section 4), and `hcdIsoPublish` refused each one as `BAD_START_FRAME` when its packets could not all have Frame IDs; the commands are `usbaudio.sys`'s `RESET_PIPE` before each feedback read. Plain SIA is not enough: the feedback read is paced by its completion at the frame it names (xHCI 4.11.2.5 p.200 allows a first-TD Frame ID without CFC). Done when: such a request is published with a first-TD Frame ID when it starts a data flow without CFC (a new context, or an Underrun or Overrun handled with nothing queued; an Interval of one frame or less), Frame IDs on every TD with CFC, and SIA otherwise (a flow under way without CFC, no frame sample, a frame outside the window, a cadence the Interval does not share), a start beyond 895 frames still refused; `iso.fid1` and `iso.sia` noted for the first four each; host vectors for the plan against the builder's TRBs, 300 paced feedback reads inside the window, the first and later data URBs, the notes, and the Play 2's descriptors (two 9-byte isochronous endpoints in one alternate setting, Interval 3, Max ESIT Payload 3 and 200, 3-byte packets, 192/196-byte packet offsets); the review loop converges; x86 and `-amd64` build all with every gate and the host suites; regression legs: QEMU's `usb-audio` on 98 SE and 7 x64 at the same timing, and a stick copy. Bench clause: the Play 2 plays a tone without hiss on the E7250 at 44.1 and 48 kHz, `iso.fid1` for the feedback endpoint with a lead near 34, Stopped events at about 31 a second.
  - **Bench clause read 2026-10-11** (owner, the E7250, Windows 7 x64, the test-signed build of `dad964f` installed by replacing `xhci98.sys`): the Play 2 plays cleanly at 44.1 and 48 kHz, `iso.fid1` for DCI 7 at leads 34 and 32, `IsoPacketsAnswered` 13,462; 202 USB Transaction Errors on the feedback endpoint (about half its reads), per packet, nothing escalated (issue 14, section 7). The review loop converged at `d2f4be2` (three rounds).
  - **Done 2026-10-11** (`f142b89`, `dad964f`, `717865f`, `d2f4be2`; docs `826f20c`, `1f4c2dc` issue 14, `5c251bf`, `074ef39`; the review loop converged at its third round, thread `01a12b05`): built x86 and `-amd64` all with every gate and the host suites at `d2f4be2`. Regression legs on development host A (QEMU, `release` package at `d2f4be2`, x86 `5D0DE84B...`, amd64 `5869031D...`): 98 SE, ME, 2000, XP, 7 x86 and 7 x64 each passed smoke, a HID mouse, a Bulk-Only stick (4 MB and 32 MB, `fc /b` clean both ways) and a UAS disk on a SuperSpeed port (32 MB, clean); `usb-audio` played TONE.WAV whole on ME, 2000, XP, 7 x86 and 7 x64 (audchk 19.995-20.000 s, no gap, one composite parent; XP's first play had a 35.5 ms gap at its start, its re-run none); 98 SE's play was not makeable (Windows 98 SE's own `USBAUDIO.VXD` fault at stream start, known). No `iso.sia` or `iso.fid1` note on any guest: every guest's audio class driver sent QEMU's adaptive device ASAP requests, so the new paths ran only on the bench. Evidence `out\p36-play2\legs\` (held off-tree).
- [x] 36.final the `2.3.0.0` builds, x86 and `-amd64`, after every task above:
  - the stamp, and the x64 binaries test-signed by the project certificate (design record 19; `make-release.ps1` refuses otherwise); **stamped 2026-10-11** (`46c5e23`: `2.3.0.0`, `DriverVer` `10/11/2026,2.3.0.0`; owner, 2026-10-11: the bench build is cut directly as `2.3.0.0`, under `out\` until the bench is good, and the TEST `2.2.0.3` label is dropped), the signed cut to follow;
    - re-cut 2026-10-12 owed (36.56 + 36.57): restamped `DriverVer` `10/12/2026,2.3.0.0` (`c93037d`, date only, version unchanged).
  - the host suites and every gate;
  - a Codex review loop per task;
  - the QEMU regression legs (still owed from `2.2.0.2`);
  - the ten install legs read from the asset, with the update legs reading 36.3's, 36.6's and 36.22's values preserved across an update (34.1's rule), moved here from SUSP.4, and `XhciSelectiveSuspend` absent (0, today's behaviour) after an install;
  - then the owner's check on the E7250 under XP x64 and Windows 7 x64, with no manual `D20IR` write, which verifies:
    - the mouse;
    - the stick hot-plugged and present at boot;
    - the EHCI disabled in Device Manager, read as delivered by the line interrupt (not moved to polling), with the backstop near idle;
    - S3 resume;
    - Windows 98 SE (PIC mode), and Windows 7 with `MSISupported` 0 (the line forced, 36.15), with the routing fix repairing the route in both;
    - Windows 7 with MSI granted, and any target under `XhciInterruptMode` 2, with the routing fix reporting only and `D20IR` left as the BIOS set it.
  - **Done 2026-10-12** (`runs/run-36.md`, "Matrix (final, stamped 2.2.0.3)", "Re-read on the stamped 2.3.0.0", "36.56" and "The owner's bench, 2026-10-11"; `releases/2.3.0.0/`): the signed re-cut, `DriverVer` `10/12/2026,2.3.0.0`, zip SHA-256 `025EA20C...1A1EE3`, verified 84 PASS, 0 FAIL. The E7250 check: under Windows 7 x64, MSI with the repair reporting only and BackstopDrains 0, the line under mode 1 with the repair rewriting `D20IR` and the EHCI disabled, the stick at SuperSpeed with the switchover on; under XP x64 the repair rewriting the route at the default and the dead-line move at `XhciIntelIrqRoute` 0. Bench: not read (owner, 2026-10-11): S3 on the E7250 (no graphics driver), Windows 98 SE on the E7250, 36.27, 36.28's bench half, 36.36 and 36.40 - moved to "Planned - bench follow-up after `2.3.0.0`".

Checkpoint: `2.3.0.0` on the E7250 under XP x64 and Windows 7 x64, with
`XhciIntelIrqRoute` at its default:
- the xHCI's interrupts delivered by the ISR (the backstop near idle);
- a device present at boot enumerated;
- the stick mounted;
- the controller working with the EHCI disabled, its interrupts delivered
  by the line rather than polled;
- the route still correct after S3;
- 36.13 passing, with its functional and regression legs on the `release`
  package and its fault-injection legs on the `qemu` flavour, kept apart.
  The `qemu` flavour is never published;
- 36.14 read by the owner;
- `XhciInterruptMode` at 0 and at 1, on a controller whose line interrupt
  works, reading as the release before it on the down-level systems;
- 36.16 passing: MSI on Vista and 7, and the x86 binary still loading on
  98 and ME;
- 36.17 to 36.19's legs passing, and `XhciFastPollFsLs` 0 to 3 reading as
  the release before it;
- 36.26 passing at every setting it lists (64, 1024, 2048 and out of
  range), with the default finally chosen identified and the DMA legs
  not shown named as untested; a passing default does not stand for a
  failing 2048. 36.27 read against 36.21 with the same settings and
  repetitions: 1 MiB stays
  the default (owner, 2026-10-09: measurement decides) only if it is
  no slower than 64 KiB anywhere beyond the
  readings' own spread; otherwise the default becomes the size that is,
  and the INF's default changes with it (an existing installation keeps
  a value it already has, under the don't-overwrite rule, so the release
  notes say how to take the new one);
- 36.35 and 36.39 passing (stage A's and B's legs): their functional
  and regression legs on the `release` package with `XhciSelectiveSuspend`
  1, 36.35's fault-injection leg on the `qemu` flavour, kept apart; 36.35's
  functional legs on 98 SE and 2000 (SMP, Driver Verifier) and 36.39's
  root-port legs run once more under `XhciInterruptMode` 2; 36.36 and
  36.40 read by the owner; 36.28's resumes read on guests and the bench;
  36.41 re-measured and, if bus-side, fixed; `XhciSelectiveSuspend` at 0,
  the default, reading as the release before it;
- 36.20's docs done, and the ten install legs read from the asset.

The phase is cut as `2.3.0.0` (owner, 2026-10-08).

Records: `docs/issues/12-e7250-xp64-boot-enable-slot-refused.md`;
`design/15-polled-event-delivery.md`; `design/14-selective-suspend.md`;
GitHub issue 17
(https://github.com/yeokm1/xhci98/issues/17).
