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
- **MSI-X BackstopDrains (NT6): attributed to QEMU, no driver change** (orchestrator decision; owner to confirm).
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
