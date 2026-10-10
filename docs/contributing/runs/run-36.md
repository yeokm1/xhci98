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
place - park and kick where an expiry falls inside it (finding 3) - and
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
   mouse back). The window is wider than this finding said - from the
   health poll's `ControllerFailed` to the grant, all in one controller
   thread pass, and on one processor a tick inside the raised stretch
   runs its DPC as the IRQL drops, still before the grant - but on QEMU
   it is still far shorter than the poller's one-tick period, so most
   recoveries fall between two expiries. Record 15 sections 5.3 and 11
   now say so: park and kick are read where they happen, and the leg's
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
