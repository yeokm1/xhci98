# XHCIQUAL Bare-Metal Test Runbook

This is the field procedure for testing `XHCIQUAL.EXE` on physical
machines. The automated QEMU regression (see "QEMU matrix test" in
[README.md](README.md)) covers the code paths: a built-in-help case, the
isolated xHCI IRQ self-test, the read-only probe and quick-scan cases, and
the controller/device cases including four `--poll-only` runs. QEMU cannot
validate a machine's BIOS ownership handoff, physical port routing, or
motherboard PIC wiring. That is what this procedure is for.

Keep the `XHCIQUAL.MAP` produced next to the `.EXE` together with the logs:
after a DOS/32A exception, the printed fault EIP can only be resolved to a
symbol against the MAP from the same build. The run header prints a build
stamp so a saved log or a photographed fault screen identifies the binary.

## Safety and preparation

The active tests take ownership of each selected controller, reset it,
enable bus mastering, and reset connected USB ports. Before running them:

1. Boot clean MS-DOS 7.1 (the Windows 98 target DOS) without EMM386, a V86
   monitor, or a paging memory manager. The DMA buffers require
   identity-mapped conventional memory.

   `HIMEM.SYS` is not what that rule excludes, and on some machines it is
   what makes the tool run at all. HIMEM is an XMS driver; it does not put the
   CPU into V86 mode and it pages nothing. `XHCIQUAL.EXE` carries the DOS/32A
   extender as its EXE stub and runs 32-bit flat, so it needs extended memory
   to move into. If the program will not run on a boot that loads nothing,
   this is the first thing to try:

   ```text
   DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V
   ```

   Point the path at wherever `HIMEM.SYS` actually is: `C:\WINDOWS\` when
   booting a Windows 98 machine off its internal disk, the root of the medium
   on a bare boot floppy. `/M:1` pins the A20 handler to method 1 (the AT
   keyboard-controller method) instead of letting HIMEM autodetect it. `/V`
   makes HIMEM report at boot what it did, so the screen says whether it
   loaded. A machine where autodetection works needs no more than the
   `DEVICE=` line.

   The failing symptom is not recorded here. If you meet it, write down what
   the screen said before adding this line, so the next person can recognise
   it.

   The `.BAT` wrappers use only `COMMAND.COM` built-ins (`IF ERRORLEVEL`,
   `IF EXIST`), with no `FIND.EXE` or other external tool. The QEMU matrix
   (`run-qemu-matrix.ps1`) and the wrapper harness (`run-win98-batch.ps1`)
   both boot the bare Win98SE target DOS, so they exercise the same
   `COMMAND.COM` this step runs on.
2. Use a PS/2 keyboard. A USB keyboard on the controller under test can
   stop working as soon as firmware ownership is transferred.
3. Do not boot or write the result log through the USB controller being
   tested. Prefer an IDE/SATA disk, floppy, or a USB controller that is not
   selected. Copy `XHCIQUAL.EXE` there before testing.
4. Disconnect valuable USB storage. Use an expendable flash drive or blank
   card reader. The qualifier does not issue storage writes, but controller
   and port resets can disrupt an in-progress device operation.
5. Record the machine model, BIOS version, and BIOS settings for Legacy
   USB, xHCI mode, EHCI handoff, and xHCI handoff.
6. If the machine stops responding, power it off completely and cold boot.
   Do not continue from controller state left by a failed run.

Build on the Windows host with:

```bat
xhciqual\build.cmd
```

For manual commands, `xhciqual\XHCIQUAL.EXE` is the only executable needed.
Keep the matching `XHCIQUAL.MAP` on the test medium for fault diagnosis. For
the packaged five-stage xHCI procedure, copy these seven files into the same
writable DOS directory on non-target storage:

```text
XHCIQUAL.EXE
XHCIQUAL.MAP
1PROBE.BAT
2XPOLL.BAT
3XIRQ.BAT
4XEMPTY.BAT
5XDEV.BAT
```

The executable carries this essential field guidance. Display it with:

```bat
XHCIQUAL --help
```

`-h` and `/?` are aliases. Help is paginated; use `--help --no-page` or
press ESC at a pager prompt to disable further pauses.

## Recommended test devices

| Controller | Best first device | Useful second device | Notes |
|---|---|---|---|
| xHCI | An inexpensive, expendable USB 2.0 flash drive or blank USB 2.0 card reader | A basic wired USB mouse/keyboard; then a USB 2.0 hub | The project manages xHCI USB2 protocol ports only. An explicit USB 2.0 device is less ambiguous than a USB 3.x storage device. |
| EHCI | An inexpensive USB 2.0 High-Speed flash drive/card reader | Another known High-Speed USB 2.0 device | EHCI owns High-Speed devices. Full-/Low-Speed HID is commonly routed to an OHCI/UHCI companion and may correctly produce C6 WARN/SKIP on EHCI. |
| OHCI | A simple wired USB 1.1/2.0 mouse or keyboard that operates at Full-/Low-Speed | An old Full-Speed flash drive or card reader | Use a direct port for the first pass. OHCI C4 WARN is currently expected; see below. |
| Mixed controllers | One visibly different device per controller | A basic USB 2.0 hub after direct-port tests pass | Physical connector-to-controller routing is machine-specific. Use the single-family runs to map ports before the mixed run. |

Avoid using an external SSD, a phone, irreplaceable media, a complex audio
interface, or a wireless receiver for the first pass. Add hubs and unusual
devices only after direct-port tests complete normally.

## Test order and commands

Use DOS-compatible short log names and retain every `.LOG` file.

### Step 0: `XHCIQUAL` with no arguments

Before any of the staged runs below, run the tool with no arguments. That is
a read-only quick scan: no PCI configuration writes, no ownership taken, one
screen, no log file. It answers "is this machine worth the rest of this
procedure" and it cannot disturb the machine while answering.

It has three outcomes, because a read-only pass cannot observe C2, C3 or C4:
`LOOKS QUALIFIED` (subject to the active tests), `DISQUALIFIED` (something a
read-only pass genuinely sees, above all `Interrupt Pin = 0`, which no BIOS
setting can work around on either target), and `CANNOT SAY` (the controller
is not in D0, or Memory Space Enable is clear). Each ends with the next
command to run.

**A `DISQUALIFIED` here ends the session for that controller.** Nothing in
the staged runs below can overturn it; the disqualifiers a read-only pass
sees are all structural.

Before v0.11 this was the most invasive thing the tool could do: with no
arguments it ran a full active bring-up of all three families (handoff,
`HCRST`, DMA, port reset, a 15-second plug wait, device identification),
which is what the safety section above says not to do first. If you are
working from notes written before v0.11, `XHCIQUAL --full` is that old
behaviour.

### Quick xHCI procedure: run the five batch files

Boot clean MS-DOS 7.1 (the Windows 98 target DOS), change to the writable
directory containing all seven files listed above, and run the batch files by
name. DOS accepts either the short
name shown here or the name with `.BAT` appended. The batch files invoke
`XHCIQUAL.EXE` from the current directory and write their logs there.

1. Disconnect external USB test devices and run:

   ```bat
   1PROBE
   ```

   This is read-only and writes `PROBE.LOG`. Archive the log, then power the
   machine off completely and cold boot.

2. With external test devices still disconnected, return to the same
   directory and run:

   ```bat
   2XPOLL
   ```

   This takes xHCI ownership and exercises reset/DMA/ports without installing
   an ISR. Expect C4 `SKIP`, verdict `PROVISIONAL`, and `XPOLL.LOG`. Archive
   the log, then cold boot again.

3. Keep external test devices disconnected and run:

   ```bat
   3XIRQ
   ```

   This runs only the locked one-shot IRQ test after C1/C2/C3; C6/C8 are
   skipped. Require `IRQ SELF-TEST PASS`, `Done.`, and `XIRQ.LOG`. Repeat it
   across several cold boots on a machine that previously faulted. Archive
   the log and cold boot before continuing.

4. Keep external test devices disconnected and run:

   ```bat
   4XEMPTY
   ```

   This is the first full ISR run; `--no-wait` keeps the no-external-device
   pass bounded. Expect xHCI C4 `PASS`, normal completion, and `XEMPTY.LOG`.
   Archive the log, then cold boot again.

5. Attach an expendable USB 2.0 test device directly to the target connector,
   return to the same directory, and run:

   ```bat
   5XDEV
   ```

   This performs the full connected-device run and writes `XDEV.LOG`. Keep all
   five logs with the `XHCIQUAL.MAP` of the exact build used to create them;
   the `.EXE` itself is not archived, since the MAP plus the version and build
   stamp in each banner already pin the build.

The batch files do not reboot the machine; perform the cold boots yourself.
Each batch first verifies that its directory is writable. The three active
batches (`3XIRQ`, `4XEMPTY`, `5XDEV`) then pass `--done-flag`, test the
completion flag with `IF EXIST` (the flag is written on a normal exit,
including a usage error) and bucket the exit code with `IF ERRORLEVEL`, so
they label normal qualified/provisional/failed results separately and
preserve a partial log after an abnormal termination. `1PROBE` and `2XPOLL`
are read-only and only check that their log exists; read the log's `Done.`
line yourself. Each batch replaces an existing same-named log, so
archive that file before repeating a stage. If a batch prints `ERROR`, or the
machine faults/freezes/reboots, stop the sequence, photograph any fault,
power off, and preserve every completed log. These five helpers cover the
staged xHCI path; use the manual commands below for EHCI/OHCI.

### 1. Read-only inventory

Run with no USB test device connected:

```bat
XHCIQUAL --probe-only --no-page --log PROBE.LOG
```

This is the detailed read-only pass: one full controller report per
function, saved to a file. The no-argument quick scan of step 0 reaches the
same verdict from the same classifier (there is only one, so the two cannot
disagree) but prints one line per controller and keeps no log. Use the quick
scan to decide whether to continue; use this to record why.

Expected output begins like:

```text
XHCIQUAL 1.0.0.0 (build <date time>) - Win98/Win2000 USB qualification
Mode: PROBE-ONLY (read-only)
Families: xHCI EHCI OHCI
Found N selected USB host controller(s).
```

Expect one controller report and one `FACT type=...` line per detected
xHCI/EHCI/OHCI function. A probe-only run intentionally does not qualify
the machine and returns exit code 1 even when no static disqualifier is
found. It ends with
`Probe safety: PASS - no PCI configuration writes.` If firmware left Memory
Space Enable clear, Tier B is unavailable and the report says the read-only
probe left MSE unchanged; use an active mode only after the inventory is
captured.

Each controller report carries a PCI Power Management block, which on real
silicon looks like:

```text
  PCI subsys: 17AA:5048
  PCI caps: PM=1 MSI=1(en=0) MSI-X=0 PCIe=1
  PCI PM: v2  state=D0  D1=0 D2=0  PME_En=0 PME_Status=0
    PME_Support: D0, D3hot, D3cold
    NoSoftRst=1 - keeps state across D3hot->D0
    flags: DSI=0 PMEClk=0  Aux=375mA
    raw: PMC=C1C2 PMCSR=0008
```

`state=D0` is what you want. Any other state means the controller is powered
down and decodes no MMIO until a driver moves it to D0. The report says so,
and the qualifier does not write PMCSR to fix it: probe-only must take no
ownership, and the D0 transition is the driver's job.

The rest of the block, added in v0.9 after the E460 cross-check showed what
`lspci` reports that the qualifier did not:

- `v<n>`: the PM capability version, printed as the raw field as `lspci`
  prints it ("Power Management version 2"), so the cross-check below is a
  literal comparison. It also tells you whether `NoSoftRst` is meaningful;
  that bit is reserved before version 2.
- `NoSoftRst`: set means the controller keeps its internal state across a
  D3hot -> D0 transition, so a resume path need not treat every D0 return as
  a cold controller; clear means it resets and must be reinitialised. A
  Win2000 resume-path input; it changes no verdict.
- `DSI`: set means device-specific initialisation is required after reaching
  D0 (restoring config space is not enough), and the report adds an explicit
  NOTE when it is. `PMEClk` should be 0 on the PCIe-era silicon this project
  targets; a 1 is worth noticing because it is unexpected. `Aux` is the
  auxiliary current the device needs in D3cold to signal PME, meaningful only
  when `PME_Support` includes D3cold. That is a platform power-budget fact,
  not a driver one.
- `raw:`: the two capability words the whole block decodes from. Record
  these. They make any later dispute re-decodable from the log without
  another trip to the machine, and they are what the `lspci` cross-check
  should be compared against if a decoded field ever looks wrong.

These fields matter mainly for the Win2000 half of the project. That target
issues real D-state transitions and acts on PME, so a controller carrying the
PME-stuck quirk (`quirks:` line) plus a PME_Support list that includes D3 is
where Phase 11's Win2000 power/stability work should look first. On Win98,
which barely exercises power management, the same block is background
information.

`PCI subsys:` above it is the subsystem vendor/device pair. The VID/DID names
the silicon; this names the board it is fitted to, which is what to match a
`results/` record on if a quirk ever turns out to be board-specific rather than
silicon-specific.

#### The PCI status lines

Most runs print nothing here, which is the expected case. When they do:

```text
  PCI status: pre-existing errors - received master abort
    (sticky bits, set before this run - not attributed to it)
  PCI status: NEW error(s) during the active tests - received target abort
    caused by this run's traffic; read with the C3 result. Bits left set (RW1C).
```

The PCI Status register's error bits (master/target abort, parity, SERR) are
sticky: firmware, a previous OS, or an earlier run of this tool can have set
them long before now. So the tool snapshots them at probe, re-reads after the
active tests, and reports the two separately. A bit already set at probe says
something about the machine and nothing about this run, while a bit that
turns on in between was caused by the tool's own traffic.

Neither line changes a verdict. A new abort bit is real evidence and belongs
in the report next to the C3 result, but the tool cannot tell a controller
that faulted the bus from one whose neighbour did while sharing it. The tool
also never clears these bits. They are RW1C, and clearing them would destroy
evidence for the next person to look.

#### Cross-checking the PCI block against `lspci`

The QEMU matrix cannot validate the PM values: `qemu-xhci`, `usb-ehci` and
`pci-ohci` expose no PM capability, so every automated case prints
`PCI PM: capability absent` and the populated path only ever runs on metal.
The PCI Bus Power Management specification is also not mirrored in
`docs/references/`, unlike the xHCI spec. So on a machine whose PCI block has
not been verified before, cross-check it against Linux on the same box.

Capture the whole function, not just the PM capability:

```sh
lspci -vv -s <BDF>
```

It is tempting to `grep -A2 "Power Management"` and move on, but the full
output independently confirms more than PM does: the subsystem IDs, the BAR
address/width/prefetchability, which capabilities exist at all (an absent
MSI-X or PCIe capability confirms the qualifier's `=0`), and that no
bus-error bit is set. Reading the whole thing is also how the v0.9 fields got
added in the first place; it is a good source of requirements for what the
qualifier should report. Note the comparison in the machine's `results/`
README once; it does not need repeating per run. Use `sudo`: without root,
`lspci -vv` quietly omits capability detail, which can look like a missing
capability.

Weight the hardwired fields over the live ones. The `PME_Support` mask, D1/D2
support, `DSI`, `PMEClk`, `Aux`, the subsystem IDs and the BAR attributes
cannot be changed by an OS, so a disagreement there is a real defect: trust
`lspci` and treat the qualifier's decode as the bug. The
`raw: PMC=.... PMCSR=....` line is what to compare against when a decoded
field looks wrong.

Expect the live fields to differ, and do not chase them. Linux binds
`xhci_hcd` and reconfigures the device, so on a machine Linux has booted you
will typically see `MSI: Enable+`, `DisINTx+` in the Control word, and
`Interrupt: pin A routed to IRQ <high number>` where DOS reported a legacy
line. That is one coherent consequence of Linux switching the controller to
MSI. The current D-state, `PME_En` and `PME_Status` can move for the same
reason. None of it says anything about the silicon. It is, incidentally, the
configuration the target drivers must avoid: neither Win98 nor Win2000 SP4
has an MSI path, so both depend on MSI staying disabled and INTx staying
enabled.

That routed IRQ number is also the clearest illustration of the C4 caveat.
The same pin the qualifier reports as a legacy `line=IRQ n` shows up under
Linux routed through the IOAPIC, which is the path a Win2000 APIC HAL would
use and the one DOS cannot exercise.

This was done on both fleet Intel machines and passed on every hardwired
field: the E460 (`results/e460-2026-07-25/README.md`) and the P14s Gen 1
(`results/p14s-gen1-2026-07-25/README.md`). So the decoder itself is
validated on real silicon, twice, on PCH generations four years apart that
returned bit-identical PM capability words. On a new machine the check is
about that machine's reading, not the decoder. Run it if a value looks
surprising, and skip it otherwise.

One thing the P14s comparison surfaced, worth knowing before it looks like a
bug: `lspci` may list capabilities the qualifier does not print, such as that
machine's Intel vendor-specific structure at `[90]`. The `PCI caps:` line
reports the presence of four specific capability IDs the tool has a use for;
it is not an enumeration of the chain. A capability appearing in `lspci` and
not in the report is only a disagreement if it is one of those four.

### 2. Poll-only active probe (xHCI, no interrupt handler)

Before the full active run, exercise ownership, reset, DMA and port reset
without installing a protected-mode interrupt handler:

```bat
XHCIQUAL xhci --poll-only --no-wait --no-devid --no-page --log XPOLL.LOG
```

Expected: C1/C2/C3 run, `C4 IRQ: SKIP`, C6 resets each already-connected
managed USB2 port, the run reaches `Done.` with no DOS/32A exception, and
the verdict is `PROVISIONAL` (not a full qualification). This is the first
thing to run after any prior DOS/32A interrupt-path fault, and the first
active run on a machine whose interrupt behaviour is unknown, because it
separates a controller/port problem from the extender's interrupt
reflection. If C6
faults here (with no ISR installed), the problem is in the port/DMA path,
not the interrupt path. Cold boot before the next active run.

### 3. Isolated xHCI IRQ self-test

After a cold boot, keep external devices disconnected and run:

```bat
XHCIQUAL xhci --irq-selftest --no-page --log XIRQ.LOG
```

Expected: C1/C2/C3 PASS, a real one-shot ISR count for C4, C6/C8 SKIP,
`IRQ SELF-TEST PASS`, and `Done.`. This mode keeps the line masked until the
xHCI source is pending and restores the protected-mode vector before the PIC
masks. It is intentionally narrower than the full run and does not itself
qualify the machine. Cold boot before the full test.

### 4. Empty-controller safety passes

Disconnect all USB test devices. Run each family that appeared in
`PROBE.LOG`:

```bat
XHCIQUAL xhci --no-wait --no-page --log XEMPTY.LOG
XHCIQUAL ehci --no-wait --no-page --log EEMPTY.LOG
XHCIQUAL ohci --no-wait --no-page --log OEMPTY.LOG
```

Expected: the program reaches `Done.` without a DOS/32A exception, C2 and
C3 pass, xHCI/EHCI C4 passes, and C6 reports `SKIP` because no device is
connected. OHCI C4 reports the expected `WARN` described below. An empty
port is not a controller failure.

### 5. Attached-device passes

Connect the recommended device directly, or leave the port empty and plug
it in when C6 displays its 15-second prompt:

```bat
XHCIQUAL xhci --no-page --log XDEV.LOG
XHCIQUAL ehci --no-page --log EDEV.LOG
XHCIQUAL ohci --no-page --log ODEV.LOG
```

Move the device between physical connectors and repeat when necessary to
discover which controller owns each connector. Do not use `--no-wait` when
you want the plug prompt.

### 6. Default all-controller pass

After the single-family passes have mapped usable ports, connect one
suitable device to each available family and run:

```bat
XHCIQUAL --no-page --log ALLDEV.LOG
```

No selector means xHCI, EHCI, and OHCI. Only controllers actually present
are reported. To test a pair explicitly, selectors are repeatable:

```bat
XHCIQUAL --scan ehci --scan ohci --no-page --log EOALL.LOG
```

The all-controller run must complete every controller section and print
`Done.`. A selected family that is absent instead prints:

```text
No selected USB host controller found on this machine.
```

and returns exit code 2.

## Expected attached-device results

Vendor/device IDs, IRQ numbers, port numbers, and timings vary. Judge the
named checkpoints rather than comparing every number literally.

There is one thing a QUALIFIED verdict does not cover, and the verdict line
no longer says so. C4 proves the controller asserts INTx and that the PIC
path delivers it, which is exact for Windows 98 and for Windows 2000's PIC
HALs. Both APIC HALs, uniprocessor (`halaacpi`/`halapic`) and multiprocessor
(`halmacpi`/`halmps`, the likely HAL on any multi-core machine installing
Windows 2000), route through the IOAPIC, which DOS cannot exercise. Design
doc
[`01-hardware-qualification-tool.md`](../docs/contributing/design/01-hardware-qualification-tool.md)
section 3 has the per-target table. Read every QUALIFIED verdict below with
that gap in mind.

Until `0.0.0.5` the tool printed
`Win2000 APIC-HAL routing remains untested by DOS C4` under each verdict. The
line was removed because it was the only line in a per-machine report that
said the same thing on every machine, so logs taken before then, including
the ones under `results/`, still carry it. The FAIL-branch
`Win2000 APIC-HAL routing remains inconclusive` is conditional and still
prints.

### xHCI

With a USB2-path device on an xHCI port, expect:

```text
Families: xHCI
Found 1 selected USB host controller(s).
...
C1 handoff:  PASS  ...
C2 reset:    PASS  ...
C3 DMA:      PASS  ...
C4 IRQ:      PASS  ISR fired on IRQ ...
C6 ports:    PASS  ...
C8 devices:  PASS  ...
DEV port=... vid=.... pid=....
FACT type=xHCI ...
==> CONTROLLER QUALIFIED for cross-target driver development
Done.
```

C1 may be WARN if firmware never advertised or released ownership; retain
the log and BIOS settings. C8 is informational: a C8 WARN/FAIL does not by
itself disqualify a controller that passed C1-C4. A USB 3.x device placed
only on an xHCI SuperSpeed logical port is intentionally unmanaged and can
produce C6 SKIP; attach an explicit USB 2.0 device for the qualification
pass.

### EHCI

With a High-Speed USB 2.0 device on an EHCI-owned port, expect:

```text
Families: EHCI
Found 1 selected USB host controller(s).
...
C1 handoff:  PASS  ...
C2 reset:    PASS  ...
C3 DMA:      PASS  halted QH DMA-read/IAA proof ...
C4 IRQ:      PASS  ISR fired on IRQ ...
C6 ports:    PASS  ... High-Speed ...
FACT type=EHCI ...
==> CONTROLLER QUALIFIED for cross-target EHCI development
Done.
```

If a mouse or keyboard produces no EHCI-owned connect, repeat with a known
High-Speed flash drive. A Full-/Low-Speed device being handed to a
companion controller is not evidence that EHCI is broken.

### OHCI

With a Full-/Low-Speed device on an OHCI-owned port, expect:

```text
Families: OHCI
Found 1 selected USB host controller(s).
...
C1 handoff:  PASS  ...
C2 reset:    PASS  ...
C3 DMA:      PASS  HCCA frame writeback ...
C4 IRQ:      WARN  SOF and PCI INTx asserted on IRQ ...; ISR hook skipped
C6 ports:    PASS  ...
FACT type=OHCI ...
==> CONTROLLER QUALIFIED (with warnings) for cross-target OHCI development ...
Done.
```

That C4 WARN is expected. It proves the OHCI interrupt source and PCI
INTx assertion without installing the DOS/32A vector that crashed under
QEMU. It does not prove CPU/PIC delivery, so preserve the warning and do
not report OHCI interrupt delivery as fully qualified.

A second WARN form exists: `SOF asserted on IRQ ...; PCI 2.3 INTx status
unsupported`. The tool gets this result only when the paired PCI 2.3
Interrupt Disable bit does not read back and the function is not PCIe, as
on older hardware. SOF alone then counts as partial proof. If the PCI 2.3
mechanism is implemented or the function is PCIe but Interrupt Status
remains clear, C4 FAIL reports `SOF set but PCI INTx did not assert (PCI
2.3)`.

## Interpreting failures

| Result | Meaning / next action |
|---|---|
| No controller found | Confirm the family exists in `PROBE.LOG`, is enabled in BIOS, and the selector is correct. |
| BAR unusable or above 4 GB | The controller is not usable by the driver's 32-bit addressing (either target) in its current firmware configuration. |
| `MMIO: NOT ACCESSIBLE` | The report now names the cause it can prove on the following line: not in D0, Memory Space Enable clear, BAR above 4 GB, or BAR unassigned. A `cause: undetermined` line means all four checks passed and the silence is the controller's - record the full log. |
| `PCI status: NEW error(s) during the active tests` | The tool's own traffic set a bus-error bit. Not a disqualifier by itself - a shared bus means a neighbour could be the culprit - but read it with the C3 result: a new master abort alongside a C3 failure is a much stronger signal than either alone. Save the log; the tool leaves the bits set. |
| `PCI status: pre-existing errors` | Set before the tool ran, by firmware, a previous OS, or an earlier run. Says nothing about this run and is never attributed to it. If you want a clean baseline, cold boot and re-probe. |
| `PCI PM: state=D1/D2/D3hot` | The controller is powered down. Not a controller fault and not a disqualifier: the driver transitions it to D0. It does explain an otherwise inexplicable `MMIO: NOT ACCESSIBLE` on the same controller. |
| C1 WARN | Record BIOS version/settings. Retry once after a cold boot and once with Legacy USB disabled. |
| C2 FAIL | Cold boot and retry. A repeatable reset failure disqualifies that controller. |
| C3 FAIL | DMA/schedule setup did not complete. Cold boot, verify no memory manager is loaded, and retain the full log. |
| C3 SKIP | The tool could not run the test: out of conventional memory (a large scratchpad, or several controllers in one run) or PAGESIZE without 4 KB support. Not a controller fault and never a disqualification. Boot with more conventional memory free, or qualify one family at a time (`XHCIQUAL xhci`). |
| xHCI/EHCI C4 FAIL | Polling may distinguish a live controller from broken PIC routing. A repeatable legacy-IRQ failure disqualifies the controller for Win98; for Win2000 under an APIC HAL it is suspicious rather than proven - see design doc 01, "What a C4 PASS proves for each target". |
| OHCI C4 WARN | Expected partial proof. The note says whether PCI INTx status was observed or the PCI 2.3 status mechanism is unavailable; CPU delivery remains unproven. |
| OHCI C4 FAIL | The controller never asserted SOF, or a PCI 2.3-capable function failed to assert INTx. |
| C6 SKIP with no device | Expected for the empty pass. The verdict reads "QUALIFIED (with warnings)" because connect/reset was never exercised. |
| C6 WARN/SKIP with a device | Try a direct port and the recommended speed. Check whether the connector belongs to another controller or requires an Intel xHCI routing BIOS setting. |
| C6 FAIL | A port reported a connect but did not enable after reset. Not disqualifying on its own, and the verdict says "with warnings" - retry on a different port with a known-good USB 2.0 device, and check the C7 routing lines on Intel 7/8-series. |
| C8 WARN/FAIL | Save `DEV` output and retry with a simple USB2 device or `--no-devid`; C8 is informational. |
| C8 `PSIV n has no USB2 speed-class mapping` | Inconclusive capability mismatch, not a qualification failure. The port's PORTSC speed ID was absent from (or unrecognised in) the controller's advertised Protocol Speed ID table, so C8 declined to guess an EP0 packet size. This can indicate a decoder defect, inconsistent controller/firmware capability data, or an unrecognised encoding; the message alone does not distinguish them. QEMU reports `PSIC 0` and never exercises this path, so bare metal is where it is first proven. Save the log with the `Protocol USB x.y: ... PSIC n` lines and the C6 speed strings. |
| C6 speed reads `PSIV n not advertised...` or `PSIV n, X Kb/s per protocol cap` | The advertised table did not yield a recognised USB speed class. The port still reset successfully - C6's verdict is unaffected - but preserve the full log and report the mismatch for decoder-versus-controller investigation. |
| Poll-only PROVISIONAL | Expected: `--poll-only` never tests C4, so it cannot qualify a machine. It confirms reset/DMA/port reset work with no ISR. Re-run without `--poll-only` for a verdict. |
| Poll-only C6 fault | A fault under `--poll-only` (no ISR installed) points at the port/DMA path, not interrupt reflection. Save `XPOLL.LOG` and the MAP, cold boot, and report the last checkpoint. |
| Full run faults at C6 but `--poll-only` C6 is clean | The interrupt/ISR path is implicated, not the ports. Report both logs plus the MAP so the fault EIP can be resolved. |
| DOS/32A exception, freeze, or reboot | Photograph the screen, cold boot, save any completed log and the matching MAP, and report the last printed checkpoint and the build stamp. Do not immediately run another active test. |

Exit codes are:

- `0`: every actively tested controller met its implemented qualification
  criteria (OHCI may still carry its documented C4 warning).
- `1`: at least one controller was not qualified, or this was a
  probe-only run.
- `2`: usage error or no selected controller was found.

## What to record for each machine

Archive the `.LOG` files under `xhciqual/results/<machine>-<YYYY-MM-DD>/` with
a `README.md` holding the entry below; see
[results/e460-2026-07-25](results/e460-2026-07-25/README.md) for a worked
example. A re-run gets its own dated directory; retire the superseded one once
its differences have been written up, so `results/` holds the current
evidence per machine rather than a pile of near-identical runs. Those `.LOG`
files are exempted from the repo's global `*.log` ignore rule, so they commit
normally.

Keep the `.LOG` files and add a short entry like this:

```text
Machine/model:
Chipset/CPU:
BIOS version/date:
Legacy USB setting:
xHCI/EHCI handoff settings:
xHCI mode/routing setting:
DOS version (MS-DOS 7.1/Win98) and boot medium:
PS/2 input available: yes/no

Build stamp (from run header):
Controller FACT lines:
Physical port -> controller mapping:

XPOLL:  completed / fault, C2, C3, C6 (C4 SKIP), PROVISIONAL
XEMPTY: completed / fault, C2, C3, C4, C6
XDEV:   device used, port, C2, C3, C4, C6, C8, verdict
EEMPTY: completed / fault, C2, C3, C4, C6
EDEV:   device used, port, C2, C3, C4, C6, verdict
OEMPTY: completed / fault, C2, C3, C4, C6
ODEV:   device used, port, C2, C3, C4, C6, verdict
ALLDEV: devices/ports, controller order, verdicts, completed normally

Unexpected behavior and last printed line:
Cold-boot retry result:
```

For a useful bug report, provide `PROBE.LOG`, `XPOLL.LOG`, the affected
family log, `ALLDEV.LOG` if it completed, the `XHCIQUAL.MAP` from the same
build, the device make/model and advertised USB speed, a photo of any fault
screen (with the build stamp visible), and the BIOS-setting record above.

## Machine notes

Per-machine facts that change how the generic procedure above is run. Add a
subsection when a machine turns out to need one.

### Intel 7/8-series (Panther Point / Lynx Point) - the `XUSB2PR` port mux

> **Nothing in this section has been observed.** No machine of this class is
> in the project's fleet, and none ever ran this tool. Read every expectation
> below as a prediction.

The section is written from the Intel 7-series PCH datasheet vol. 2 and from
Linux's `usb_enable_intel_xhci_ports()` in `drivers/usb/host/pci-quirks.c`,
and it is kept because it is the procedure someone with such a machine would
follow. The same warning applies to the `XUSB2PR` section of
[../docs/usb-xhci-info/xhci-programming.md](../docs/usb-xhci-info/xhci-programming.md),
whose register table and BIOS-mode behaviour come from the same two sources.

Why this class gets its own note: 7- and 8-series are the last Intel
generations carrying both EHCI and xHCI, so they are the only parts with the
`XUSB2PR` EHCI-to-xHCI port mux. Skylake (100-series) and later Intel have no
EHCI, the ports are hardwired to xHCI, and these registers do not exist; nor
do they on modern AMD. So this is the only class of machine where the routing
questions in the programming guide can be observed at all.

Two safety rules from "Safety and preparation" are usually already satisfied
on a laptop of this era, which is what would make one the safest rig to run
first. The internal keyboard and pointing device are typically PS/2 through
the embedded controller, so a hung active test cannot lock you out (verify
this on the machine rather than assuming it), and the boot/log volume is
internal SATA, so you are not writing the log through the controller under
test.

Connector inventory. Read the vendor's own specification for the machine and
write down every built-in external USB-A receptacle, its side, and its
advertised speed; do not include connectors on a dock or port replicator.
Record the insert colours actually present. Colour is a locator, not routing
evidence.

BIOS settings. Firmware of this era usually exposes the routing as a setting
named something like "USB 3.0 Mode", with values along the lines of
`Disabled` / `Enabled` / `Auto`. Predicted, from Linux and the datasheet: the
"route to xHCI" value writes `XUSB2PR = XUSB2PRM`, claiming every switchable
USB2 port rather than a chosen subset; the "Auto" value leaves `XUSB2PR = 0`,
leaving the switchover to the OS driver. That is the trap: a perfectly
initialised xHCI that never sees a connect. Connectors outside the switchable
set stay on EHCI in every setting, so a machine may retain a working EHCI
file-transfer path either way; which connectors those are is a per-machine
fact and must be measured.

Prefer the BIOS setting over having the tool write
the register (below). Run the sheet once per value the firmware offers,
setting it yourself rather than reading anything into whatever the machine
happens to be set to now. Record Legacy USB and both handoff settings, and
leave those two alone.

Which stage answers the routing question. C7 is four PCI config reads
(`XUSB2PR` 0xD0, `XUSB2PRM` 0xD4, `USB3_PSSEN` 0xD8, `USB3PRM` 0xDC) and needs
no USB device attached. But it is a Tier C test gated on C4 passing or
`--poll-only`, so `1PROBE` does not print it and neither does the `3XIRQ`
self-test. The earliest stage that shows the routing is `2XPOLL`. Look for
either "all switchable USB2 ports already routed to xHCI" or "USB2 ports NOT
routed to xHCI - a perfect init will see no connects".

Demonstrating the mux does need a device, because "routed to xHCI" is a
register value while "the port therefore reports a connect" is C6/C8. Run
`5XDEV` with an expendable USB 2.0 flash drive in the first blue connector; a
USB 3.x device on a SuperSpeed logical port is intentionally unmanaged and
can report C6 SKIP. Then run one scan per remaining connector to map every
physical receptacle to a controller. Together with C7's register reading,
that physical map is the routing evidence, so it belongs in the results
README.

**Do not reach for `--set-intel-ports` casually.** It writes
`XUSB2PR = XUSB2PRM`, taking all switchable USB2 ports away from EHCI, and it
leaves that routing in place on exit (only USBLEGSUP and PCI Command are
restored). On a machine that has EHCI, that is the Win98 file-transfer safety
net, so a cold boot to restore the firmware default is the way back. If you
do use it, read the line after the write rather than treating "a write was
issued" as "the ports are routed". It reports one of three things: confirmed
and left in place; the read back still shows ports on EHCI, so the write did
not take; or the read back cannot say whether it took, in which case
attribute the C6 result to neither routing nor the ports and re-run.

What a xhciqual run here does and does not settle. It closes the Phase 0
prerequisite and answers the routing question on its own. It does not cover
the driver-side observation (`xhci98.sys` run against the routing), which
additionally needs Win98 SE (+NUSB) or Win2000 SP4 installed on the machine
plus built install media; see
[../docs/contributing/build-and-test.md](../docs/contributing/build-and-test.md),
"Option C: Real Hardware". That observation is published as an unreachable
limitation for want of any machine of this class.

One cross-check you can skip: the `lspci -vv` PCI-PM comparison. It existed
to validate the tool's PM decoder, and it passed field-for-field on the E460
and the P14s Gen 1 independently, so no Linux boot is needed.

#### `XUSB2PR` run sheet

The generic procedure above with this class's specifics filled in, in the
order to do them. It is one pass per value of the firmware's xHCI/USB 3.0
mode setting, and nothing branches on what the setting currently is; step 1
sets it. Every value gets run, so whichever one the machine shipped with is
covered either way, and any single pass can be repeated later on its own.

| Pass | Mode value | Stages | What the pass is for |
|---|---|---|---|
| A | the one that disables xHCI | `1PROBE` | Whether the xHCI PCI function is present at all. One read-only stage. |
| B | the one that routes ports to xHCI | all five, then the connector sweep | The machine's Phase 0 qualification, plus the routed `XUSB2PR` reading and the connector-to-controller map. |
| C | each remaining value (typically `Auto`) | `2XPOLL`, `5XDEV` | The differential: the other `XUSB2PR` reading, and the deceptive C6 SKIP with a device attached. |

Why that order. Pass A is one read-only stage and costs nothing, so it goes
first. The full five stages go in pass B because the routed state is where
every checkpoint can pass, which is what makes a C6 SKIP there a real finding
instead of the expected result, and because the staged escalation (poll-only,
then IRQ self-test, then full ISR, then a device) has to happen once on a
machine before pass C is allowed to jump straight to `5XDEV`. Pass C is last
because it only has to produce the contrasting reading.

Step 0, on the Windows host:

```bat
xhciqual\build.cmd
```

Copy the seven files listed under "Safety and preparation" to a writable DOS
directory on the machine's internal disk, not a USB stick, since the
controller under test may be the one serving it. The `.MAP` must come from
the same build as the `.EXE`; it is the only way a DOS/32A fault EIP becomes
a symbol.

Step 1, firmware setup: photograph everything, then change two settings.
Photograph every USB-related page before touching anything. "What to record
for each machine" wants the model, BIOS version, Legacy USB, xHCI mode and
both handoff settings, and photographs are faster than transcribing. Then:

- Serial ATA mode -> Compatibility (IDE), so DOS can see the internal disk
  at all. **If a modern Windows is installed on that disk, this will likely
  stop it booting until you set it back to AHCI.** Know that before flipping
  it, and set it back at the end.
- The xHCI/USB 3.0 mode setting -> the value that disables xHCI. This is
  pass A. Write down what the setting was before you changed it, as a fact
  about the machine, but nothing in this run sheet depends on it and every
  value gets run regardless.

Step 2, run each pass in order, following the generic five-stage procedure
above and renaming each pass's logs before the next pass overwrites them.
The `C7:` line inside a log identifies which pass it belongs to on its own,
which is what makes an un-renamed log recoverable; the filename is a
convenience, the C7 words are the evidence.

Step 3, afterwards:

1. Set the mode back to the value that routes ports to xHCI if any
   driver-side work on this machine is planned
   (`docs/contributing/build-and-test.md`, "Option C: Real Hardware"), and
   record which connectors that leaves on EHCI as the file-transfer path.
2. Set Serial ATA back to AHCI if anything else on that disk needs it.
3. Archive every `.LOG` plus the `XHCIQUAL.MAP` into
   `xhciqual/results/<machine>-<YYYY-MM-DD>/` with the README template from
   "What to record for each machine". The additions this class needs are
   every mode value with what each produced, both `XUSB2PR` readings (the
   routed pass and an unrouted one), the connector map, and the setting the
   machine was found on before step 1 changed it.

Suggested log set:

```text
PROBEDIS.LOG                      pass A
PROBE.LOG XPOLLEN.LOG XIRQ.LOG    pass B
XEMPTY.LOG                        pass B
XDEV1.LOG XDEV2.LOG ...           pass B, one per connector
XPOLLAU.LOG XDEVAU.LOG            pass C
XHCIQUAL.MAP                      the build all of them came from
```

If any stage prints `ERROR`, or the machine faults, freezes or reboots: stop
the sequence, photograph the screen with the build stamp visible, power off,
and keep every completed log and the `.MAP`. Do not immediately run another
active test.
