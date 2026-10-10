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
