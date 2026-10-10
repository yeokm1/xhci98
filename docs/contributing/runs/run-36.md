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
one, so (record 15 section 11) the forced recovery below reads park and
kick, and the disable and enable rows above read the close.

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
