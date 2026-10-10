# Phase 36 Record

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 36". The
roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

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
  (36.44/36.46 area).
- 2000, `qemu` P8 (finding against record 15 section 11): a forced
  recovery (`XhciQemuInject` 01050000) under mode 2 recovered and the
  mouse worked, but `PollParks` stayed 0 and `PollKicks` did not move,
  as in 36.13's Q5 on 98 SE and 7 x64; and the Bulk-Only stick came back
  with Code 31 until `pnpctl restart` (as 36.13's Q5 on 2000). `p8pre.txt`,
  `p8.txt`, `debugcon-2.log` lines 1015-1120. Route: 36.13 (poll) and the
  recovery's re-enumeration.
- XP x64, 36.47 header (tool or expectation): no SystemBios lines - this
  XP x64 keeps the BIOS strings in `HARDWARE\DESCRIPTION\System\BIOS`, not
  in `SystemBiosVersion`/`SystemBiosDate`, and `XHCISNAP` reads only the
  latter there ("BIOS not in the registry"). Everything else as expected.
  `i3k6.txt`. Route: 36.47.
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
| 98 SE | K5 36.46 L5 | rel | FAIL (re-attach on a hub port after a removal not seen; evidence l5.txt, l5b.txt, l5d.png, ham3.png) | m1 device_del (root port 2), re-plugged as m2 BEHIND THE HUB at 4.1 (root-port re-add avoided: QEMU wedge rule with the hub on port 4): QEMU lists m2 at 4.1 (12 Mb/s) but the driver never addressed it: l5.txt/l5b.txt ... |
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
| 2000 SP4 | P8 | w2k | FAIL (81 = 0 and 82 not increased - finding vs record 15 section 11 "park expected", as Q5 on 98 SE / 7 x64; plus stick Code 31 after recovery) | mode 2 (p8pre.txt: 2/8, PollRole 2, PollKicks 1, PollParks 0); debugcon-2.log: "qemu inject: fired=01050000", "controller halted itself, USBSTS=00000009", controller reset, init complete, "slots invalidated, dropping ... |
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
