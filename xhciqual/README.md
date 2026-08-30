# XHCIQUAL - DOS USB Host-Controller Qualification Tool

XHCIQUAL implements `docs/contributing/design/01-hardware-qualification-tool.md`:
a DOS program that vets xHCI, EHCI and OHCI controllers on bare metal before
any driver effort is spent on a machine. With no family selector it scans and
qualifies all three controller types.

The verdict is about the controller, not an OS. It supplies hardware evidence
for both driver targets (Win98 SE and Win2000 SP4), and the machine needs
neither installed to be tested. The one place the evidence is
target-dependent is C4; see "What a C4 PASS proves for each target" in the
design doc. A PIC-mode pass is exact for Win98 and for Win2000's PIC HALs.
Both the uniprocessor and multiprocessor APIC HALs route through the IOAPIC,
which DOS does not test.

The verdict line itself does not say so. It used to print
`Win2000 APIC-HAL routing remains untested by DOS C4` under every QUALIFIED
verdict on every machine, which made it the one line in a per-machine report
that carried no per-machine information. The caveat now lives here, in
[`hardware-testing.md`](hardware-testing.md), and in the `readme.txt` that
`..\scripts\package\make-release.ps1` generates beside `XHCIQUAL.EXE` (the
only document that ships with the tool).

The FAIL-branch sibling stays:
`Win98/PIC-HAL routing failed; Win2000 APIC-HAL routing remains inconclusive`
does the opposite and more useful job, stopping a reader from writing a
machine off for Windows 2000 over a PIC-routing failure, and it varies with
the machine. Saved logs under `results/` predate the change and still quote
the removed line; they are evidence and are not rewritten.

The interrupt-path repair, reopened by an early ThinkPad E460 bare-metal run
and confirmed fixed on that machine, is documented in
[`../docs/contributing/lessons.md`](../docs/contributing/lessons.md) (the
DOS/32A IRQ entries). It records the evidence, root cause and confirmed
findings; read it before changing the xHCI interrupt or C6 paths.

## Versioning

The tool's own version line ended at v0.11. `TOOL_VERSION` in `qual.h` now
expands `XHCI_VER_STR` out of `../src/xhci_version.h`, so the tool reports the
driver's package version. The qualifier is published inside a release
directory beside `xhci98.sys`, and a tool answering with a different number
would tie a saved log to the wrong artifact. `make-release.ps1` refuses a cut
whose `qual.h` has been edited back to a literal. So there is nothing to bump
here: bump the header, then rebuild this tool, which the release script also
checks. See `../docs/contributing/build-and-test.md`, "Versioning the driver".

The `v0.x` numbers in the version history below, and in `results/*/README.md`,
record which build produced what and are not rewritten.

## The runs on file

One directory per machine and trip, each with its own README as the record of
what that run read:

- `results/e460-2026-07-25/README.md`: ThinkPad E460, Sunrise Point-LP,
  QUALIFIED under v0.9. This is the run carrying the `lspci -vv` cross-check
  of the A4 power-management decode, which does not repeat per run.
- `results/p14s-gen1-2026-07-25/README.md`: ThinkPad P14s Gen 1, Comet
  Lake-LP, QUALIFIED under the same v0.9 binary. The second fleet Intel
  machine, and the second board the same decode was read on.
- `results/e460-2026-08-22/README.md`: the E460 again under the 0.0.0.2
  build, mapping all three external USB-A connectors before the bench trip
  (stage E0 of `../docs/contributing/runs/run-13e.md`). It supersedes neither
  record above, and `../docs/using/release-acceptance-test.md` cites it
  as its worked example of a report.

A new trip adds a directory here rather than editing one. A saved log is what
a machine said on a day.

## What it runs per controller

- Tier A: PCI discovery for class codes `0C0330` (xHCI), `0C0320` (EHCI) and
  `0C0310` (OHCI): VID/DID/rev, Command/BME, BAR0 below 4 GB, MSI/MSI-X caps,
  Interrupt Pin/Line, and VID/DID quirk lookup. The Intel `XUSB2PR` routing
  registers are reported here but are read by C7, which is Tier C, so
  `--probe-only` shows no routing values.
- Tier B: family-specific capability introspection. xHCI reports HCIVERSION,
  context size, slots/interrupters/ports, scratchpads, PPC, `HCCPARAMS2`
  (U3C/CMC/FSC), USBLEGSUP, and USB2/USB3 protocol topology. FSC is the one
  worth knowing before a suspend/resume matters: with `FSC = 0` the driver
  declines every controller Save State, so a resume rebuilds the bus instead
  of restoring it (`docs/using/release-notes.md`, "Known limitations"). Read the bit,
  never the version: it is optional at xHCI 1.0 rather than absent, so a 1.0
  controller may advertise it. A controller whose `CAPLENGTH` stops before
  `0x20` has no such register and the line says `absent`. EHCI reports
  HCIVERSION, HCSPARAMS/HCCPARAMS, root ports, PPC, and its PCI legacy
  capability. OHCI reports HcRevision and both root-hub descriptors.
- Tier C: the shared silent-death gates. C1 firmware->OS handoff, C2
  halt+reset, C3 controller-visible DMA, C4 interrupt delivery on the legacy
  8259 IRQ with the C5 poll-vs-interrupt differential, and C6 port
  power/connect/reset/speed. The DMA proof is an xHCI No-Op command
  completion, an EHCI halted-QH DMA read confirmed by asynchronous-schedule
  status plus Interrupt-on-Async-Advance, or an OHCI HCCA frame-number
  writeback. xHCI and EHCI prove CPU ISR delivery with a No-Op and
  Interrupt-on-Async-Advance respectively. OHCI verifies Start-of-Frame and,
  when PCI 2.3 interrupt reporting is implemented, requires PCI Interrupt
  Status, without installing a DOS/32A vector; it therefore reports C4 WARN
  (see the QEMU coverage note below).

  xHCI also runs C7, the Intel EHCI->xHCI
  routing inspection and optional port switchover (read-only unless
  `--set-intel-ports`; unusable words report `UNDETERMINED`, not routed), and
  C8 device identification (informational, DOSUSB-style): per port that
  passed the C6 reset, Enable Slot -> Address Device -> GET_DESCRIPTOR,
  reporting VID/PID, device/interface classes, and manufacturer/product
  strings. That is proof that the command ring and EP0 control transfers work
  against a real device. C8 is xHCI-only, never changes the verdict, and can
  be skipped with `--no-devid`.
- Tier D: fact sheet, a `FACT` line (machine-readable seed for the Phase 4
  parameter tables) plus one `DEV` line per identified device, and the
  section 8 go/no-go verdict.

## Build (Windows host)

Open Watcom 2.0 at `C:\WATCOM`. From the repository root, initialise its
environment once per shell, then build:

```bat
call xhciqual\SETENV.BAT
xhciqual\build.cmd
```

This produces `XHCIQUAL.EXE` (32-bit flat) as a single standalone file:
`wlink system dos32a` embeds the open-source DOS/32A extender as the EXE stub,
so no separate extender (DOS4GW.EXE) travels with it. DOS/32A is a drop-in
DOS/4GW replacement with the same zero-based flat model and the DPMI services
the tool uses (0x0800 physical mapping, 0x0100 conventional alloc). HX/HDPMI
was considered and rejected: it targets Win32-PE console apps and would have
replaced the whole build format. CauseWay was ruled out by the owner.

The build also emits `XHCIQUAL.MAP` (the wlink `option map`). Keep it paired
with the `.EXE` and the logs from each field run. If a run faults with a
DOS/32A exception, the reported EIP is only resolvable to a symbol against the
matching MAP. The run header prints a build stamp
(`XHCIQUAL 1.0.0.0 (build <date time>)`, the version being `XHCI_VER_STR`) so a saved log or a photographed crash
screen ties back to the exact binary and its MAP.

One deviation from the design doc is worth recording. The design sketches two
build targets (a real-mode Tier A probe plus an extended Tier B/C tool).
Every xHCI machine has a 386+ CPU by definition, so a single 32-bit executable
covers all tiers, and `--probe-only` provides the safe read-only subset. That
keeps one toolchain and one binary to carry.

## Host-side unit tests

```bat
xhciqual\test\run-host-tests.cmd
```

Builds and runs `xhciqual/test/test_mmiodiag.c` on the Windows build host in
seconds, with no VM and no DOS. It covers the pure `PCIINFO` -> report logic
in `mmiodiag.c`: the PCI Power Management block, sticky PCI Status errors,
the two dead-MMIO classifiers, and the three-state C7 Intel routing
classifier.

That code lives in its own translation unit so it can be tested here, because
the QEMU matrix cannot reach the field-dependent branches. SeaBIOS leaves
Memory Space Enable set and the BAR assigned, so neither dead-MMIO classifier
runs; no emulated USB controller exposes a PM capability, so only the
"capability absent" branch executes; clean emulated PCI Status words cannot
exercise the sticky-error reports; and no QEMU controller has the Intel
7/8-series mux needed to reach C7. Without this runner, those branches would
first execute on a field machine. It is also the cheapest check on the PCI PM
bit positions: expected strings are transcribed by hand from the
specification rather than generated by the code under test. It does not
replace the bare-metal `lspci -vv` or C7 cross-checks.

`build.cmd` runs it before `wmake`; `build.cmd NOHOSTTEST` skips it. It needs
only Open Watcom (targeting NT instead of DOS). Any C89 compiler would do,
since the code under test has no DOS dependency.

### The build's exit-code contract

```bat
powershell -ExecutionPolicy Bypass -File xhciqual\test\check-build-exit-codes.ps1
```

A guard that cannot fail the build is worse than no guard. An earlier if/else
form of the host-test wiring in `build.cmd` stopped the build (no
`XHCIQUAL.EXE`) while exiting 0, so a caller reading the exit code saw a
failing test suite as a passing build. `wmake` failures were unaffected, so
it went unnoticed.

This check asserts the contract with five cases: a clean build succeeds and
produces the EXE, `NOHOSTTEST` still builds, a failing host test fails the
build, a failing test runner fails the build, and an invalid `wmake` target
fails the build. The first case is a positive control; without it, a
`build.cmd` broken in some unrelated way would make every negative case pass
for the wrong reason.

Run it after touching `build.cmd`'s control flow. It is not called from
`build.cmd` (that would recurse). The failing cases mutate a source file and
restore it in a `finally` block, then verify the restored bytes against the
pre-run snapshots before exiting, so pre-existing source edits are preserved.

## Run (real hardware)

### Start here: `XHCIQUAL` with no arguments

With no arguments at all the tool does a read-only quick scan: no PCI
configuration writes, no ownership taken, one screen. That is the answer to
"will this machine run the driver", and it is safe to run first on a machine
you know nothing about.

Before v0.11, no arguments ran the most invasive mode the tool has: a full
active bring-up of all three families with BIOS handoff, `HCRST`, DMA, port
reset, a 15-second wait for a plug, and device identification. That is the
opposite of what the safety section in `hardware-testing.md` tells a
first-time user to do. If you have older notes, `--full` is that behaviour.

A read-only pass cannot observe C2, C3 or C4, so it has three outcomes, and
each says what to do next:

| Outcome | Means |
|---|---|
| `LOOKS QUALIFIED` | no read-only disqualifier; the active tests still decide |
| `DISQUALIFIED` | something a read-only pass genuinely sees: `Interrupt Pin = 0` (MSI-only, and neither target has an MSI path), no xHCI function at all, BAR0 above 4 GB or unassigned, a dead MMIO window with no excuse, or no USB2 protocol ports |
| `CANNOT SAY` | a state this pass may not change is in the way: the controller is not in D0, or Memory Space Enable is clear |

The `Probe safety: PASS - no PCI configuration writes.` line is printed rather
than suppressed for brevity. It is the proof that the read-only contract held.

"One screen" was measured in QEMU: three controllers print 11 lines under 4
rows of DOS/32A startup plus the command line, 16 of the pager's 23 usable
rows (`PAGE_ROWS`, `report.c`). Each controller is one line, and from the
seventh controller the output ends in an `N more controller(s) - for the full
list run: XHCIQUAL --probe-only --no-page --log PROBE.LOG` tail (or `the full
list is in the log` when one is open) rather than scrolling. The real-console count on a multi-controller machine
was never taken: no machine left in the fleet has more than one USB
controller, so it is published as a limitation and reopens for one DOS boot
on any two-controller machine.

Every other invocation is unchanged. The trigger is literally "no arguments
at all", so the five batch wrappers, the QEMU matrix and both bare-metal
results directories behave as before, including the ones that pass only a
family selector, which still mean a full active run on that family. `--full`
is the old no-argument behaviour spelled out; `--quick` asks for the scan
explicitly and can be combined with `--serial`, `--log` and `--no-page`.

### Does it ship with the driver? No, and here is the reason

`XHCIQUAL.EXE` is not in `scripts\package\make-package.ps1`'s output. That
was decided in roadmap task 11-V.8 rather than left implicit. Two reasons,
and the second is the one that decides it:

- The install media is a Windows setup-engine payload. Every file on it is
  named by `[SourceDisksFiles]`, copied by an install section and
  hash-checked against `scripts\package\usbd-sources.expected`. A DOS
  executable that no INF section references would be the only file on that
  media the engine never touches, and the packaging gate's coverage rules
  would have to be taught to ignore it.
- It is built by a different toolchain, Open Watcom, which the driver build
  does not need at all. Putting it in the package would make a driver
  release unbuildable without it.

The obligation that replaces it: the release notes must say where to get the
qualifier, because the recommended procedure begins with it. Build it from
this directory (see "Build (Windows host)" above), or take it from the same
place the driver package came from. That obligation is met by the "Before
you install: check the machine" section of
[`../docs/using/release-notes.md`](../docs/using/release-notes.md), which
also carries the three quick-scan verdicts. Keep the two in step: that
section is a rendering of this one, and this file is the normative copy.

### The staged procedure

Follow [`hardware-testing.md`](hardware-testing.md) for the exact staged
commands, expected xHCI/EHCI/OHCI output, recommended test devices, failure
interpretation, and the per-machine results template.

For the packaged xHCI path, copy `XHCIQUAL.EXE`, its matching
`XHCIQUAL.MAP`, and `1PROBE.BAT`, `2XPOLL.BAT`, `3XIRQ.BAT`, `4XEMPTY.BAT` and
`5XDEV.BAT` into the same writable DOS directory on non-target storage. Boot
clean MS-DOS 7.1 (the Windows 98 target DOS), change to that directory, and
run:

```bat
1PROBE
```

Cold boot, return to the directory, then run `2XPOLL`. Cold boot again and run
`3XIRQ`; it is the isolated one-shot interrupt test and does not run C6/C8.
After another cold boot, run `4XEMPTY` with no external test device. Cold
boot, attach an expendable USB 2.0 device directly, and run `5XDEV`. The
resulting logs are `PROBE.LOG`, `XPOLL.LOG`, `XIRQ.LOG`, `XEMPTY.LOG` and
`XDEV.LOG`. Archive each before repeating its stage, because the batch
replaces the same-named log. The full preparation, expected results and stop
conditions are in the runbook; the five batch files are xHCI-specific.

Run `XHCIQUAL --help`, `XHCIQUAL -h`, or `XHCIQUAL /?` for the field guide
built into the executable. It is paginated by default; add `--no-page` or
press ESC at a pager prompt to show the remainder without pausing.

The general procedure, for any family:

1. Boot clean MS-DOS 7.1 (the Windows 98 target DOS) from a non-target
   IDE/SATA disk or floppy when possible, with no EMM386 or any paging/V86
   manager. If USB boot is unavoidable, its controller must not be selected
   for the active run and logs must be written elsewhere. Conventional memory
   must be identity-mapped for the DMA buffers; see design doc section 4.
   `HIMEM.SYS` is not excluded by that rule and may be needed: this
   executable carries the DOS/32A extender as its EXE stub and needs extended
   memory, so if it will not run on a boot that loads nothing, add
   `DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V` to `CONFIG.SYS`
   ([hardware-testing.md](hardware-testing.md), "Safety and preparation" step
   1).
2. In BIOS: disable "Legacy USB Support" if possible and use a PS/2 keyboard.
   Otherwise expect the USB keyboard to drop once the tool takes ownership of
   the controller (design doc section 9).
3. Run `XHCIQUAL` for the read-only quick scan, then `XHCIQUAL --full` for
   the active run across xHCI, EHCI and OHCI. Have suitable devices ready for
   C6: High-Speed for EHCI, Full-/Low-Speed for OHCI, and any USB2-path
   device for xHCI.
4. Read the verdict. To save it, run `XHCIQUAL --log` for `XHCIQUAL.LOG`, or
   `XHCIQUAL --log filename` to choose the file. Archive confirmed findings
   under `xhciqual/results/<machine>-<date>/`.

### Command-line reference

Family selection: `XHCIQUAL xhci`, `XHCIQUAL ehci`, or `XHCIQUAL ohci` scans
only that family. `--scan TYPE` and `--scan=TYPE` are equivalent; repeat
`--scan` to combine families (for example, `--scan ehci --scan ohci`). `all`
restores the default all-family scan. The long shorthand forms `--xhci`,
`--ehci` and `--ohci` are also accepted.

Other flags:

- `--probe-only`: read-only Tier A plus Tier B when MSE is already enabled;
  it never changes PCI Command.
- `--poll-only` and `--irq-selftest`: see the two sections below.
- `--set-intel-ports`: xHCI C7 writes `XUSB2PR` = `XUSB2PRM`, classifies the
  read back, and does not restore the pre-write value on exit. It claims
  success only when the read back confirms every switchable port on xHCI,
  otherwise reporting still-on-EHCI or `UNDETERMINED`. Prefer the firmware's
  own xHCI-mode setting where one exists; see
  `docs/usb-xhci-info/xhci-programming.md`.
- `--no-wait`: skip the C6 plug prompt.
- `--no-devid`: skip C8 device identification.
- `--no-page`: do not pause the screen every page. The report is paginated
  by default so it can be read on bare metal; press any key to continue, ESC
  to stop pausing.
- `--serial`: mirror output to COM1 115200 8N1 (used by the QEMU smoke test;
  disables paging).
- `--log [filename]`: save the report; the filename defaults to
  `XHCIQUAL.LOG`. No log file is created unless `--log` is present.
  `--log=filename` is also accepted.

### Poll-only active mode (`--poll-only`)

`--poll-only` is a third mode between the read-only `--probe-only` and the
full active run. It does take controller ownership, halt/reset, exercise DMA
and reset ports, but it never installs a protected-mode hardware interrupt
handler. C1/C2/C3/C6 run by foreground event-ring polling; the interrupter
and `USBCMD.INTE` stay disabled, so the controller never asserts INTx during
the port resets. For xHCI and EHCI, C4 is reported `SKIP` and the verdict is
`PROVISIONAL`, never a full qualification, since polling an event or a status
bit does not prove CPU/PIC ISR delivery. OHCI's C4 still runs in this mode,
because it never installs a vector in any mode: it polls SOF and PCI INTx
status and reports its usual `WARN` (see the QEMU coverage note below).

This is the safe next step after a run faults inside the DOS/32A interrupt
path (see [`../docs/contributing/lessons.md`](../docs/contributing/lessons.md)).
It determines whether reset, DMA and port reset work when the extender's
interrupt reflection is removed from the path. Recommended first bare-metal
use:

```bat
XHCIQUAL xhci --poll-only --no-wait --no-devid --no-page --log XPOLL.LOG
```

Even in the full active run the qualifier shuts the interrupt source down and
removes the vector the instant C4 finishes (pass or fail), so C6 never resets
a port with a live handler installed.

### Isolated IRQ mode (`--irq-selftest`)

This xHCI-only mode runs C1/C2/C3, installs the protected-mode vector with
DPMI `0204h`/`0205h`, locks the complete assembly ISR and its state with
`0600h`, and keeps the PIC line masked while the xHCI No-Op source is armed.
Only after `USBSTS.EINT` and `IMAN.IP` are pending does it unmask the line.
The ISR disables and acknowledges xHCI, re-masks the line, increments its
locked counter, sends EOI, and returns with an explicit 32-bit `IRETD`.
Teardown restores the prior protected-mode vector before restoring the saved
PIC masks. C6/C8 do not run and the result is `IRQ SELF-TEST PASS/FAILED`,
not a full machine qualification verdict.

```bat
XHCIQUAL xhci --irq-selftest --no-page --log XIRQ.LOG
```

## What a verdict means

QUALIFIED = C2 reset, C3 family-specific DMA proof, and C4 legacy-IRQ
delivery all PASS, with C1 handoff either PASS or a recorded firmware warning
and a non-empty root-port set. For xHCI the USB2 protocol topology and
context size are also required. OHCI currently returns QUALIFIED WITH
WARNINGS when SOF assertion passes and PCI INTx status is either observed or
unavailable on a pre-PCI-2.3 interface, because CPU ISR delivery is not
claimed by the safe DOS/32A path.

DISQUALIFIED on: Interrupt Pin = 0 (MSI-only), C4 never fires, PCI 2.3 INTx
status fails, C3 never completes, C2 reset fails, or BAR0 above 4 GB.

A C1 handoff timeout is a WARN, not a disqualifier. Some firmware ignores the
semaphore but stops touching the controller anyway, and the driver may still
work. It costs the run its clean verdict ("QUALIFIED with warnings"), nothing
more.

Anything short of C6 PASS, whether SKIP (no device was attached) or FAIL (a
connected port refused to reset), is likewise a warning: never a clean
QUALIFIED and never a disqualification.

A C3 SKIP is neither. It means the tool could not run C3 (out of conventional
memory, or PAGESIZE without 4 KB support), so the run reports NOT QUALIFIED
with that reason rather than blaming the controller's bus-mastering.

C8 (xHCI device identification) is informational only. A C8 failure on a
machine that passes C1-C4 is worth recording in the machine's `results/`
archive but does not disqualify; the driver's own enumeration path (Phase 6)
differs enough that Phase 0 should not judge it.

## QEMU matrix test (host, no real hardware)

After building, run the deterministic headless matrix from the repository
root:

```powershell
powershell -ExecutionPolicy Bypass -File xhciqual\test\run-qemu-matrix.ps1
```

The runner cold-boots the bare Win98SE (MS-DOS 7.1) target DOS for every
case, drives the console through QEMU's monitor, captures serial/VGA/stderr
output, requires the expected results, scans for DOS/32A fault text, and
always stops the VM. A single case can be selected with `-CaseName NAME`. The
older `xhciqual\test\run-qemu-test.cmd` remains as an interactive xHCI smoke
test.

One failure mode is a host flake, not a result. `QEMU monitor did not open`
with `Failed to bind socket` means the monitor port the runner picked was
momentarily unavailable on the host, so that case never ran the tool at all.
Re-run before investigating; it is not a qualifier regression, and it moves
between cases. Observed once (`xhci_poll_hub`), clean on the immediate
re-run. Any other single-case failure should be reproduced with
`-CaseName NAME` and taken seriously.

Running on the real target DOS matters. An earlier FreeDOS stand-in has a
more lenient shell (its FreeCOM expands `%ERRORLEVEL%`, which MS-DOS 7.1
`COMMAND.COM` does not) and once hid a batch-wrapper bug. The matrix boots the
same `COMMAND.COM` the field uses, and a companion harness exercises the
`.BAT` field wrappers by name on that disk:

```powershell
powershell -ExecutionPolicy Bypass -File xhciqual\test\run-win98-batch.ps1
```

The wrappers use only `COMMAND.COM` built-ins: `IF ERRORLEVEL` for the exit
code and `IF EXIST` on XHCIQUAL's `--done-flag` completion sentinel (no
`FIND.EXE`).

### The Win98 boot image

The boot image is proprietary Microsoft software and is not committed. Place
a bare, automation-ready boot floppy at `tools\w98se.img` and both harnesses
use it. If it is missing, they fetch it from a source you configure (the repo
hardcodes none), in this order:

1. `-BootImageUrl <url>` on the harness command line;
2. `$env:XHCIQUAL_WIN98_URL`;
3. the first non-comment line of `tools\w98se.url` (gitignored; copy the
   tracked `tools\w98se.url.example` template and edit it).

Set `$env:XHCIQUAL_WIN98_SHA256` to also verify the download. If no source is
configured either, the harnesses skip cleanly (they never fail for a missing
image). Once a source is configured, download, size or hash failures stop the
harness with a nonzero exit instead of being treated as a skip.

The URL must point to an automation-ready image: a bare disk that boots
straight to a DOS prompt, with no boot menu and no DATE/TIME prompt. Build one
from a stock Win98 boot floppy by keeping only `IO.SYS`, `MSDOS.SYS`,
`COMMAND.COM` and `HIMEM.SYS` with a minimal `CONFIG.SYS` (`himem`,
`dos=high`) and `AUTOEXEC.BAT` (`@echo off` + `prompt $p$g`); mtools
(`mdel`/`mcopy`) can strip and rewrite the FAT12 image directly.

### What the matrix covers

The matrix stands at 40 cases: one standalone-help case, the isolated xHCI
IRQ self-test, 27 controller/device cases, two `--probe-only` cases (one
scanning xHCI alone, one scanning xHCI, EHCI and OHCI together), one
read-only quick scan per controller-presence mask (none, each family, every
pair, all three), and one that checks `--full` still reaches the old
no-argument behaviour. The quick-scan cases matter because that path is the
first thing a user runs.

The cases cover all eight controller presence masks, empty and attached
variants, HS storage, FS HID, OHCI HID/storage, a USB hub, an unmanaged
SuperSpeed-only xHCI attachment, mixed devices across multiple controllers,
combined family selectors, a selected-but-absent family, and `--log ohci`
log-name/selector disambiguation. Four `--poll-only` cases (xHCI empty, xHCI
HS storage, an xHCI hub that generates repeated port-status-change events,
and EHCI HS storage) confirm the no-ISR active path reaches a `PROVISIONAL`
verdict and `Done.` with no DOS/32A exception.

QEMU proves these code paths and cleanup behaviour, not real firmware.
Bare-metal BIOS handoff and PIC routing still require a physical-machine run.

The tool does not use the DOS vector helpers or a C interrupt handler.
xHCI/EHCI use the locked DPMI/assembly one-shot described above and never
chain into DOS/32A's 16-bit reflection thunk. Both controller sources must
become pending while the line is masked before CPU delivery is exposed, and
both tear down immediately after C4. A genuinely unowned entry masks the line
and fails the test; after the qualifier restores the old vector and PIC
masks, the prior owner can receive its pending interrupt. See
`docs/contributing/lessons.md`.

OHCI is different. QEMU's OHCI SOF assertion caused a repeatable DOS/32A
fault at interrupt-vector entry even with a minimal ISR. The production test
therefore avoids that unsafe vector hook, proves OHCI SOF and PCI INTx
assertion by polling, and reports C4 WARN. CPU delivery for OHCI must be
verified later with a safer extender/ISR implementation or a dedicated
bare-metal test. The PCI Interrupt Status bit is a PCI 2.3 (2002) addition
that most OHCI-era chipsets hardwire to zero. The tool probes the paired
Interrupt Disable bit and recognises PCIe functions as modern: an older
interface without either signal gets C4 WARN for SOF alone, while a PCI
2.3-capable function that fails to assert Interrupt Status gets C4 FAIL.

For QEMU USB2/USB3 placement details, see "How QEMU places devices on USB2
vs USB3 logical ports" in `docs/contributing/build-and-test.md`.

## What the matrix cannot reach

Three parts of the tool only run on real hardware.

The PCI Power Management path and the dead-MMIO cause lines. QEMU's
`qemu-xhci`, `usb-ehci` and `pci-ohci` expose no PM capability at all
(`PM=0`), so every matrix case takes the "capability absent" branch; real
controllers take the other one. See "Cross-checking the PCI block against
`lspci`" in `hardware-testing.md` for how to verify a machine's first
bare-metal run. The PCI PM specification is not mirrored in
`docs/references/`, so that cross-check is the field verification for these
bit positions.

It is done and passing on both fleet Intel machines (ThinkPad
E460 and ThinkPad P14s Gen 1): the populated path ran on silicon and
`lspci -vv` independently decoded the same capability identically, every
field including the hardwired ones no OS can change (`PME_Support` mask,
D1/D2 support, `DSI`, `PMEClk`, `Aux`). The two machines are PCH generations
apart (Sunrise Point-LP and Comet Lake PCH-LP) and returned bit-identical PM
words, `PMC=C1C2 PMCSR=0008`. See `results/e460-2026-07-25/README.md` and
`results/p14s-gen1-2026-07-25/README.md` for the comparison tables. It does
not need repeating per machine.

The strict-PSI lookup. `qemu-xhci` reports `PSIC 0` on both its protocol
capabilities, so every matrix case takes the default-ID fallback and the
advertised-table lookup never executes. Both fleet Intel machines do
advertise a table (PSIC 3 on the USB2 capability), so bare metal is where
that code first runs.

It is bounded, since C8 is informational and never
reaches the verdict, but the failure mode is a device that stops being
identified: if a PORTSC PSIV is missing from the advertised table, C8 prints
`PSIV n has no USB2 speed-class mapping` and reports NOT IDENTIFIED. So run
`5XDEV` as well as `3XIRQ`, and read the C8 block. That note is an
inconclusive capability mismatch: it can reflect a decoder defect,
inconsistent controller/firmware capability data, or an unrecognised
encoding. It is not by itself a qualification failure.

Both fleet Intel machines have cleared it: the E460 on Sunrise Point-LP
(`results/e460-2026-07-25/`, C8 3 of 3) and the P14s Gen 1 on Comet Lake-LP
(`results/p14s-gen1-2026-07-25/`, C8 5 of 5), each with PSIC 3 advertised on
the USB2 capability and each identifying every device it saw across
Full-Speed and High-Speed, with no PSIV mismatch line.

AMD has never run it and no longer can: the B650M was the project's only AMD
machine and became unavailable, so AMD xHCI silicon is untested ground rather
than pending work. C6's speed line carries the same evidence; preserve and
report any
`PSIV n not advertised by protocol cap` or `PSIV n, X Kb/s per protocol cap`
string with the full log.

C7, the Intel routing classifier. It has never run on hardware and now
cannot: no machine in this project has an Intel 7/8-series mux. Every
`XUSB2PR` value quoted anywhere in this repository is read off the Intel
7-series datasheet and Linux's `usb_enable_intel_xhci_ports()` rather than
measured. The pure reporter is host-tested instead.

## Version history

The `v0.x` line ended at v0.11 (see "Versioning" above). Changes by version:

v0.11 (roadmap task 11-V.8) made the no-argument default a read-only quick
scan, added the three-outcome classifier every read-only verdict now comes
from (one classifier, so the quick scan and the full run cannot disagree),
and took the host suite to 146 checks and the QEMU matrix to 40 cases.
Re-run `3XIRQ` on a known-good machine before trusting a new bare-metal
verdict.

v0.10 made the Intel C7 routing result follow the evidence.
`XUSB2PRM=00000000` or `FFFFFFFF`, and `XUSB2PR=FFFFFFFF`, could make the old
mask comparison look routed even though the words cannot answer the routing
question. C7 now reports those cases as Routing UNDETERMINED, explicitly
reports when an Intel controller is absent from the quirk table, and never
turns either non-answer into "routing is fine".

`--set-intel-ports` applies
the same rule to its read back and reports one of three outcomes: routing
confirmed and left in place, switchable ports still shown on EHCI, or an
unusable read back that cannot say whether the write took. In the last case
it also declines to blame either routing or the ports for the following C6
result. The pure reporter is host-tested because no QEMU controller exposes
this Intel 7/8-series mux; the host suite reached 113 checks. The saved E460
and P14s qualification records remain v0.9 evidence.

v0.9 completed the PM block, after the E460 bare-metal cross-check showed
what `lspci -vv` reports that the qualifier did not. Added: the PM capability
version (raw field, as `lspci` prints it; it also says whether `NoSoftRst` is
meaningful, that bit being reserved before version 2); `NoSoftRst`, which
says whether the controller keeps its internal state across a D3hot -> D0
transition or must be reinitialised on resume; `DSI`, which says whether
device-specific initialisation is required after reaching D0 (with an
explicit NOTE when set); `PMEClk` and `Aux` current, for completeness against
`lspci`; and the raw `PMC`/`PMCSR` words, so any later dispute is
re-decodable from the log without another trip to the machine. All of it is
Phase 11 Win2000 resume-path input; none of it touches a verdict.

The same cross-check prompted two additions outside the PM block:

- PCI subsystem IDs (`PCI subsys: VVVV:DDDD`, config 0x2C/0x2E). The VID/DID
  names the silicon; these name the board it is fitted to. Two laptops can
  carry the same xHCI DID behind different firmware, and an add-in card's
  board vendor is otherwise invisible, so this is what a `results/` record
  should be matched on when a quirk turns out to be board-specific.
- The sticky PCI Status error bits (config 0x06: master/target abort,
  parity, SERR), snapshotted at probe and re-read after the active tests.
  The bits are RW1C and can predate the run by an entire boot, so a bit
  already set at probe is reported as pre-existing and explicitly not
  attributed, while only a bit that turns on across the tests is attributed
  to the tool's own traffic; read that one alongside the C3 result.
  Verdict-neutral either way, since a shared bus means a neighbour could be
  the culprit. The tool never clears them; clearing would destroy evidence
  for the next diagnosis.

v0.8 added PCI Power Management reporting. The capability walk already noted
whether the PM capability was present; it now reads the capability's
contents and reports them: current D-state, D1/D2 support,
PME_En/PME_Status, and the PME_Support state list. Reads only. The tool never
writes PMCSR, so `--probe-only` remains free of any ownership or state
change, and transitioning a D3 device back to D0 stays the driver's job.
Probe-only also counts every call through the PCI configuration-write
helpers and prints `Probe safety: PASS - no PCI configuration writes` before
exiting; the QEMU matrix requires that line.

Two reasons this exists, with Windows 2000 SP4 a co-primary target: Win2000
issues real D-state transitions and acts on PME (so `QF_PME_STUCK` is a
genuine work item there, not a Win98 footnote; see
`../docs/usb-xhci-info/xhci-programming.md`), and a "MMIO: NOT ACCESSIBLE"
result is no longer a dead end. The report names the cause it can prove:
device not in D0, Memory Space Enable clear, BAR above 4 GB, or BAR
unassigned.

v0.7 touched the interrupt path, which the earlier repair had stabilised, so
it was re-confirmed on metal: the E460 run has `C4 IRQ PASS` with one ISR
entry on IRQ 11 in both `3XIRQ` and the two full runs, no DOS/32A fault at
any stage. The v0.7 changes, from a source review:

- The ISR and every teardown acknowledge `USBSTS.EINT` before `IMAN.IP` (it
  was the reverse, which the spec note says can lose an interrupt).
- Event-ring dequeue publishing follows the documented EHB rule:
  intermediate `ERDP` writes leave EHB set, and only the write after the
  ring reads empty releases it. Command/transfer waits therefore drain to
  empty before returning.
- Conventional memory is released between controllers, so a machine with
  more than one controller no longer fails allocation on the second and gets
  reported as a DMA failure. Cleanup releases DMA blocks only after a
  completed controller reset or confirmed PCI BME disable; otherwise the
  blocks remain protected from reuse and the cleanup failure disqualifies
  the run.
- Verdict fixes: a C6 FAIL can no longer print a clean QUALIFIED, and an
  allocation/PAGESIZE failure reports C3 SKIP instead of "bus-mastering
  broken".
- Port speeds are named from the Protocol Speed ID table when the controller
  advertises one (PSIC > 0) instead of assuming the default IDs. The
  complete 15-entry table is retained, missing IDs stay unknown, and C8 uses
  the decoded speed class for its EP0 packet-size decisions.

The v0.9 source, including the follow-up cleanup/strict-PSI review fixes,
v0.8 PM reporting, v0.9 PM fields, and two read-only probe regression cases,
passed the 31-case QEMU matrix and the 5-case Win98 batch harness in Phase 0
(including both C4 IRQ cases).

## Open item: dump the raw extended-capability DWORDs

The report prints a decoded fact sheet (protocol groups, port map, PSI
counts) and never the register words behind it. Driver Phase 4 task 3 turned
both machines' reports into host-test replay vectors (`test_replay_e460` /
`test_replay_p14s` in `test/test_caps.c`), and that is where the limit shows:
a vector can carry a capability's shape but not its bytes, so xECP itself is
synthetic in the vectors and the PSI DWORDs are placeholders that no check
reads. Both fleet controllers advertise a PSI table (PSIC 3 on the USB2
capability, 3 and 8 on the USB3 ones), so the driver's speed decode really
does run through that table on this hardware, and it is the one part of the
classification path real machines cannot currently be replayed against.

The fix is small and belongs here rather than in the driver: emit a hex dump
of the xECP chain (`xECP` itself, then each capability's DWORDs) alongside
the decoded block. It needs a bare-metal run on each machine afterwards to be
worth anything, so do it at the next bench session rather than as a session
of its own.
