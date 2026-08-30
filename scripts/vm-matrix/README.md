# `scripts/vm-matrix` - the automated VM device matrix

Phase 10's harness. It boots each target VM, walks every USB device model this
QEMU build can present, and emits a diffable per-device report, so "does this
driver still handle the device population" stops being a hand-driven session
and becomes something that can be run after any change.

Read `docs/contributing/design/06-device-matrix-verdict.md` first if you are
going to change what a row asserts. It is the design; this directory
implements it.

## One-line invocation

```powershell
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1
```

Before the first run, once:

```powershell
copy scripts\vm-matrix\config.sample.psd1 scripts\vm-matrix\matrix.config.psd1   # then edit paths
powershell -File scripts\vm-matrix\gen-offsets.ps1             # after any driver change
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -ValidateOnly
```

Useful narrowings:

```powershell
-Target 2b              # one target
-Group hid              # one group
-ValidateOnly           # boot nothing; just check the matrix resolves
-KeepGuestOnFailure     # leave a failed group's guest running to look at
```

## What is in here

| File | What it is |
|---|---|
| `run-matrix.ps1` | The runner. Boot, stage, drive, collect, verdict, teardown. |
| `matrix.psd1` | The rows and what each is expected to move. The design lives here, not in the runner. |
| `config.sample.psd1` | Per-host paths. Copy and edit; nothing committed knows where your QEMU or images are. |
| `probe-devices.ps1` | Guestless population probe: what this QEMU build can present, and at what speed. |
| `prepare-image.ps1` | One-off, operator-driven: boots a target image without `-snapshot` so a class's driver install is answered once and persisted, so the matrix run itself meets no wizard. On a fresh target it also clones the base image out of a pre-driver snapshot (`-Clone`) and takes the stamp the post-release run checks (`-Stamp`). |
| `selftest.ps1` | Negative controls: drives `lib/verdict.ps1` with synthetic deltas, guestless, and asserts the cases whose answer must not be PASS; then drives the runner's own decisions (`lib/fresh.ps1`) the same way: the snapshot reader against a stand-in `qemu-img`, the target split, the writable-image refusal, the two-leg loop, the verdict, the report file, and `prepare-image.ps1`'s clone and stamp refusals. It prints its check count. |
| `soak-11v.ps1` | Batch 11-V stage F: N unplug/replug cycles per class, then a sustained multi-class load, against an already-running guest whose monitor it is pointed at. |
| `lifecycle-11v.ps1` | Batch 11-V stage G: an orderly guest shutdown with traffic in flight, a device churned across the whole teardown, and the stop-time counters read out of the stopped guest. Drives an already-running guest. |
| `wedge-observe.ps1` | Finding 3's wedge as an observatory: the bench plug/pull recipes replayed in QEMU with every trace channel open. |
| `matrix.broken.psd1` | A mutated matrix, run against a real guest as the whole-pipeline half of the same proof. Tracked on purpose; the self-test's guestless half cannot replace it. Do not "fix" its rows. |
| `gen-offsets.ps1` | Derives the counter offset table from the driver's own sources. |
| `offsets.txt`, `offsets.labels.txt` | Generated. Do not edit. |
| `lib/monitor.ps1` | QEMU monitor transport. |
| `lib/qemu.ps1` | QEMU discovery, launch, and the traps enforced in code. |
| `lib/counters.ps1` | Reading the miniport extension out of a live guest. |
| `lib/verdict.ps1` | Parsing and evaluating expectations; deciding a row's outcome. |
| `lib/fresh.ps1` | The post-release run's own rules: the image stamp and its refusals, the replug verdict, `ExpectNoDriver`, the report header. Every refusal in it has a self-test case. |
| `guest/` | The load scripts that run inside the guest for the concurrent-load stage, in two non-interchangeable pairs (Windows 2000 `cmd.exe`, Windows 98 `COMMAND.COM`). `guest/README.md` says which pair is which and why running the wrong one fails silently. |

The matrix harness itself (`run-matrix.ps1`, `prepare-image.ps1`,
`probe-devices.ps1`, `selftest.ps1` and `lib/`) does not depend on
`scripts/local/`, which is git-ignored per-host tooling. The three 11-V
drivers and the guest BATs are historical run tooling: they drive a guest an
operator has already started, and their comments name the per-host launchers
(`qemu-win98-run-11v.cmd`, `qemu-win2k-smp-run-11v.cmd`, `hidpump-11v.ps1`)
those runs used. What such a run needs from a launcher is stated in each
script's header: a monitor port, the drives, the trace event list and, for
the SMP guest, `-smp`.

## Prerequisites, and the one that bites

The guest must already have the `qemu` build of `xhci98.sys` installed and
bound. The harness reads counters out of the miniport extension by byte offset
and cross-checks the offset table's `SIZEOF` against the
`MiniPortExtensionSize` the running driver prints. A guest running a different
build is refused, loudly, rather than measured, because a stale offset table
surfaces as a wrong value, never as an error.

The flavour has to be `qemu`, not `debug`. The identity line this harness waits
for (`cb ... a=<VA>` on the port-`0xE9` debug console) depends on two things
that only the `qemu` flavour has: `XHCI_DBG_CB` compiles to nothing outside
`qemu` (`src\xhci_dbg.h`), and the `0xE9` sink is gated on the same define
(`src\sources`). A `debug` guest boots, binds and drives devices, and then
produces no identity line at all, which surfaces as the boot-deadline timeout
rather than as "wrong flavour". Build it with:

```bat
scripts\build-driver.cmd qemu
powershell -File scripts\package\make-package.ps1 -Flavor qemu
powershell -File scripts\vm-matrix\prepare-image.ps1 -Xfer ...
```

`qemu` is never published, so this is a harness prerequisite and not something
a user is ever asked to install.

After any change to `XHCI_EXTENSION`, regenerate the offsets and reinstall the
driver in the guest images:

```powershell
powershell -File scripts\vm-matrix\gen-offsets.ps1
```

Windows 98 needs one persisted install pass per device class. Measured in
Phase 10's matrix: a boot-attached mouse enumerates fine (`devices addressed`
= 1) but `endpoints opened` stays 0 for ten minutes, because Win98 puts up a
modal Add New Hardware Wizard for any class the image has not been taught, and
it blocks the bind indefinitely. The harness detects this and says so by name
rather than letting it read as a driver defect, which is what that dialog looks
like from the counters. Windows 2000 SP4 binds HID and mass storage itself and
needs no such pass.

Because the harness boots with `-snapshot`, an install done during a matrix
run is discarded. Do the install on purpose, with `Snapshot = $false`.

## The post-release run (Phase 16, task 16.1)

The same harness, on guests installed for the occasion. The design, and the
owner's decisions it carries out, are in
`docs/contributing/design/09-post-release-unattended-run.md`; read it before
changing what this mode refuses or reports. In one line:

```powershell
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -PostRelease
```

What differs from the ordinary matrix, all of it in code:

- It boots only the fresh targets, the ones whose config entry carries
  `CloneFrom` (`2a-fresh`, `2b-fresh` in `config.sample.psd1`), and the
  ordinary matrix never boots those. Naming one from the other set with
  `-Target` is an error.
- Before anything boots, each image's newest qcow2 snapshot must be a
  `base-<DriverVer>-qemu` stamp for the version `src\xhci_version.h` states.
  No stamp, another version's stamp, another flavour's, a stamp that is not
  the newest snapshot, or an image named `win98.img`, `win2k.img` or
  `win2k-smp.img`: the run refuses and says which. `-PostRelease
  -ValidateOnly` runs those checks, and the monitor-port bind test every
  validation makes, and boots nothing.
- Every row is attached, detached, and attached again on the same root port.
  The replug is read and judged against the same expectations, printed as
  `<row>/replug`, and the row's outcome is the two legs combined: the same
  outcome twice is that outcome, and a device that came back differently from
  how it arrived is a `FAIL`.
- `NODRIVER` counts against the target's verdict unless the row carries an
  `ExpectNoDriver` entry for it in `matrix.psd1`. Both cases are printed, and
  so is an entry that did not apply, because the entries are guesses the
  first fresh run exists to correct.
- A group that ends on a row the matrix declares `MayWedgeGuest` for the
  target is still `ERROR` in the report and does not count against the
  verdict. On Windows 98 that is the composite (`usb-audio`) row's pinned
  reading.
- It always boots with `-snapshot`, and refuses a config that turns that off.
- Each target gets its own report, `out\post-release\<DriverVer>\post-release-<target>.txt`,
  with a header block (driver, image and stamp, QEMU, offsets, start and
  elapsed time, verdict) above the usual row lines. The exit code is the
  worst target verdict.

The preparation is the manual part, once per release, and it is the only part
a person does. The driver install inside the guest is the one rung the design
allows; everything else is a command:

```powershell
scripts\build-driver.cmd qemu
powershell -File scripts\package\make-package.ps1 -Flavor qemu
powershell -File scripts\vm-matrix\gen-offsets.ps1
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Clone        # post-nusb -> vm\fresh-2a.img
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Boot -Xfer   # the whole qemu package on the transfer drive
#   in the guest: install from the transfer drive's root directory, readme.txt section 4; restart
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Status       # endpoints opened >= 1 on the keep-alive
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Attach kbd-hs -AtPort 2   # once per class the matrix attaches (Windows 98)
#   shut the guest down from the Start menu
powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-fresh -Stamp        # base-<DriverVer>-qemu, taken last
```

Then the same for `2b-fresh` (`phase2b-clean` -> `vm\fresh-2b.img`), which
needs no `-Attach` pass for HID and storage. Three things the first run
(2026-08-30) taught about the Windows 98 pass: a class that raises no wizard
when attached is not necessarily taught, so read `-Status` and expect
`devices addressed` to advance for every attach (`uas`, `serial` and
`braille` once attached nothing at all, see notes 14 and 15 below); the
`audio` device is attached once and not cycled, because its third arrival
wedged the guest; and on Windows 2000 the shutdown ends at "It is now safe to
turn off your computer" rather than powering off, so end that pass with
`quit` at the monitor once that screen is up. `-Clone` refuses to overwrite an
existing fresh image without `-FreshCopy`, and `-Stamp` refuses while the prep
guest is running and refuses when the last prep boot's debug console never
showed the qemu build running with the offset table's `SIZEOF`, so a stamp is
never written on an image that has not been seen to carry the driver.
Anything persisted after the stamp fails the newest-snapshot check; re-stamp
after any further preparation.

What this run is not: acceptance of the published release (that is
`docs/using/release-acceptance-test.md`, by hand, from the download), a
reading of the `release` binary, the qualifier, the log channel, Device
Manager, or a file round-trip through storage. Design record 09 section 10
lists each with its reason.

## What a report says

```text
TARGET  ROW                  OUTCOME    EXPECTATION                          READING
2b      usb-kbd/hs           PASS       advance devices addressed            +1
2b      usb-braille/fs       NODRIVER   advance endpoints opened >= 1        +0
2a      usb-audio/fs         NODRIVER   advance endpoints opened >= 1        +0  (USBAUDIO.VXD...)
```

Five outcomes: `PASS`, `FAIL`, `NODRIVER`, `INERT`, `ERROR`. The middle two are
results, not silences. A device the OS never claimed says nothing about this
driver, and a row whose every expectation is structurally zero on this vehicle
can never be a pass.

`INERT` has no reachable row in the current population. Every row inherits the
live `Always` block, so no row's whole expectation set is structurally zero;
the clause is met at expectation level and the reasoning is in
`docs/contributing/design/06-device-matrix-verdict.md`. The disagreement is
carried here as an open item: no row-level `INERT` instance exists in this
population, and a reader requiring one should treat that clause of Phase 10's
checkpoint as unmet.

The body has no timestamps, durations or paths, so a regression shows up as a
changed line in a diff. Everything variable is in the header.

A row that is not a clean `PASS` also gets a screenshot. The two commonest
causes on these targets, a modal wizard and a wedged PnP tree, are invisible
to every counter and to the liveness probe alike.

## The traps this harness enforces rather than documents

Each is in `lib/qemu.ps1`, with the run that paid for it named there.

1. Two `-trace` arguments. QEMU keeps the last, so `-trace events=X -trace
   file=Y` silently discards the event list. `Assert-SingleTraceArg` throws.
2. `screendump foo.png` writes a PPM. `Get-ScreendumpPath` renames.
3. A stale `offsets.txt`. `Assert-OffsetsFresh` compares `SIZEOF` against the
   running driver's reported size and voids the run.
4. A healthy trace is not a living guest. `Test-GuestAlive` watches the
   guest's own interrupt counters, and is shown to fail on a paused VM.
5. A pull is only a pull if the device left. Every `device_del` waits on
   `info usb`.
6. A leftover guest from a previous run listens on the same monitor port, and
   the harness would otherwise drive it. The port is tested free first.

Traps the harness paid for and documents rather than enforces:

7. The monitor echoes every prefix of a command (`iininfinfoinfo info uinfo
   usinfo usb`), so a reply parser drops the lines that end with the command
   text rather than trusting the first line. `Send-Checked` also once reported
   progress through `Write-Output` and polluted its own return value.
8. `probe-devices.ps1` selects on `bus usb-bus`, not on the monitor's "USB
   devices:" heading (which lists host controllers). Selecting on the bus is
   what makes `usb-storage`, filed under "Storage devices", appear at all.
9. On a default `qemu-xhci` ports 1-4 are USB 2.0 and 5-8 SuperSpeed, and
   there are 15 USB 2.0 root ports in all. Attaching the whole population at
   once exhausted them and QEMU cascaded the remainder onto the probe's own
   `usb-hub` (`Port 6.1`), reporting a non-root port. Probe one device at a
   time.
10. Three PowerShell traps. `Start-Process -ArgumentList` quotes nothing, so a
    `-name "..."` with spaces arrived as six arguments and QEMU died on its
    command line (and with no `-RedirectStandardError` the harness reported
    "the monitor never answered"). One row's failure before its `device_del`
    made every later row fail with `Duplicate device ID 'dut'` and report it
    as its own refusal; fixed at both ends, with a per-row id and an
    unconditional cleanup. And `powershell -File ... -Group storage,hub`
    passes one string, because `-File` does not parse array arguments, so a
    filter matching nothing ran nothing and exited 0; both filters are now
    validated against the matrix and a run that evaluated no rows exits 2.
11. Two self-test defects that each made a check unable to fail, found by
    writing `selftest.ps1`. `(pipeline).Count` is `$null` when the pipeline
    yields exactly one object, so the unread-counter guard was `$null -gt 0`
    for the likeliest case and an unread counter was reported as `FAIL` ("the
    counter did not move", the one reading a broken read must never produce).
    And `NODRIVER` swallowed a failure on the driver's own side, because the
    test was "every failed expectation is an `advance`". Trap 4 above has no
    guestless self-test either: it was demonstrated against a `stop`ped VM
    (`Alive=False` with an IRQ delta of 0, `Alive=True` after `cont`) and
    that demonstration is the only evidence for it.
12. `prepare-image.ps1 -Status` once read a stale extension address and
    reported every counter as 0. It recomputed the default debug-console
    path, read a log from an earlier boot and took the address out of it
    (`C14668C4` against a live `C14658C4` with 162 transfers on the wire),
    and told the operator twice that an install had failed when it had
    succeeded. A stale address reads as a plausible set of zeros and never as
    an error, the same shape the `SIZEOF` check exists to refuse.
13. The tablets are excluded from the Windows 98 prepare pass after the QEMU
    main-loop hangs seen on prep boots (cause unresolved; the live VVFAT
    transfer directory is the correlated variable and is now off by default),
    so `usb-wacom-tablet/fs` stays untaught and fails. That is not a driver
    result.
14. `usb-serial` and `usb-braille` never attached. Both models put themselves
    on the bus only when their chardev is open, and a `null` chardev never
    is, so with `chardev-add null` they sat in their port with
    `attached=false` and `qom-set attached true` was refused ("not writable").
    Phase 10 read that as `FAIL +0` on both targets and wrote
    `ExpectNoDriver` entries on it; the first post-release run read it as
    `ERROR`. The runner and the prep script now use `file` chardevs
    (measured: `null` gives `false`, `file` gives `true`, both models).
15. `prepare-image.ps1 -Attach uas` (and `bot`) taught nothing, because the
    bare adapter is never presented by QEMU without a LUN; the wizard the
    matrix then met unattended was the first anyone saw of the class. The
    prep spec now adds the matrix row's `scsi-hd` child on a second scratch
    drive and repairs `attached`, and `-Detach` removes the child first.
