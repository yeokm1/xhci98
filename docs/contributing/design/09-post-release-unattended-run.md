# 09 - The post-release unattended run

Phase 16, task 16.1 (added after the `1.0.0.0` cut as task 14.3, carried for a day as task 15.5). What an
automated run on freshly installed guests has to do, what it is allowed to
cost a tester, and which readings it can honestly produce. Written before the
script, because the decisions in sections 2 and 7 were taken by the project
owner and the script is what carries them out.

Companion documents:
`docs/contributing/design/06-device-matrix-verdict.md` (the verdict model this
reuses rather than replaces), `scripts/vm-matrix/README.md` (the harness this
extends), `docs/using/release-acceptance-test.md` (the hand procedure whose
step 4 and step 5 this automates), and `docs/contributing/build-and-test.md`
("The automated VM device matrix (Phase 10)", "Reading counters out of a live
guest").

## 1. The question this run answers, and the two it does not

Three things look similar and are not the same measurement.

| | Subject | Vehicle | Who runs it |
|---|---|---|---|
| Phase 10's matrix | A change to the driver | A guest carried along, prepared once and re-used | A script, after any change |
| This run | A release, on an OS with no history | A guest installed for the occasion | A script, after any release |
| The acceptance run | The published asset and the document that ships with it | A machine this project does not own | A person, by hand, from the download |

Phase 10 cannot answer this one, because its guests have been carried along:
whatever their images learned, they learned before the release under test
existed, and an install onto them is an upgrade rather than an install. The
hand-run acceptance of a published release cannot be automated into it either,
because what that run measures is whether a stranger can follow a document,
and a script following it is not that reading.
What is left in the middle is worth a script because nobody re-runs it by hand
more than once per release: the install path, the first bind, and then the
plug and unplug, on an operating system installed for the occasion. Two
operating systems, on one CPU each, per section 2.4.

Where this overlaps Phase 10, it extends `scripts/vm-matrix/` rather than
growing a second harness beside it. That directory already boots, stages,
drives, collects, decides and tears down; what it has never done is start from
an OS installed for the run, or assert that a device comes back after a
replug.

## 2. The decisions already taken

These are the project owner's, recorded here so the implementation inherits
them rather than re-argues them.

### 2.1 One manual rung, and the run stops for nothing else

Installing the driver inside the guest may be done by hand, once, against an
image that persists it. Everything else in the run belongs to the script: the
boot, the staging, the plug, the unplug, the replug, the readings, the verdict
and the teardown. A run that stops halfway for a person has not met task 16.1.

The rung exists because of a measured cost, not a preference.
`scripts/vm-matrix/prepare-image.ps1` records it: Windows 98 raises a modal Add
New Hardware Wizard for every device class its image has not been taught, and
it blocks the bind indefinitely. A boot-attached `usb-mouse` reached `devices
addressed` = 1 and `slots enabled` = 1 with `endpoints opened` stuck at 0 for
776 seconds, on a guest whose CPU was perfectly alive. It also disables the
keep-alive pump, so the next hot-plug is invisible too. Driving that wizard is
the expensive part of an unattended run, and it is the least interesting thing
this project has left to prove.

The consequence to hold on to: the tester's install is a preparation, not a
step of the run. Once it is persisted, the run itself must meet no wizard and
answer no prompt.

### 2.2 The `qemu` flavour is the binary, and what that costs

The `qemu` build is what is installed, because the identity line and the
counters only it carries are what make an unattended run fast to drive and
readable when it fails. Both halves of that are gated on the same define
(`src/xhci_dbg.h` and `src/sources`), so a `debug` guest produces no identity
line at all and surfaces as a boot-deadline timeout rather than as "wrong
flavour".

The price is stated rather than hidden. `qemu` is never published, so a run on
it measures this driver and not the release a user downloads. A run on the
`qemu` flavour may not be reported as acceptance of a release. It is a driver
reading taken after a release, which is a different sentence.

The shipped `release` binary is not run here at all. It has no identity line
and no counters, so the harness cannot tell a `release` guest that has booted
from one that has hung, and giving it a second readiness signal would be a
second harness. The owner's decision is that the `release` binary gets its
reading on a physical machine, by hand, in the acceptance run. Nothing in this
design has to keep a `release`-flavour mode possible.

### 2.3 The qualifier and the log channel are out of scope

Neither `XHCIQUAL` (step 3 of the acceptance test) nor `XHCISNAP` (step 8) is
run here.

The qualifier judges a machine: its BIOS handoff, its interrupt pin, its
memory window. An emulated controller is not the machine anyone asks it about,
so a `LOOKS QUALIFIED` from a VM is a reading about QEMU. The log channel step
tests an instruction given to a user, in the user's own words, in the release
notes and `readme.txt`. A script reading the channel back tests the script.

Excluding them narrows nothing above: the install and the plug and unplug are
untouched by either.

### 2.4 Two guests, both single-processor

The run covers Windows 98 SE and Windows 2000 SP4 on one CPU, which is targets
`2a` and `2b`. Phase 2d's multiprocessor Windows 2000 guest is not in it.

That is a scope decision and not a claim that the SMP guest does not matter.
It is Phase 11's and Phase 13's stress vehicle, and the one live residual this
project has published on it, a suspend arriving during an in-place controller
recovery on a multiprocessor machine, is untested ground the release notes do not
list. What that guest measures is a driver under concurrency; what
this run measures is a release installing onto an operating system with no
history and surviving plug and unplug. A third guest would multiply the
preparation of section 2.1 for a reading this run was not built to take.

Nothing here forecloses adding it. If the SMP guest is added later it is a
third target of the same shape, prepared the same way, and it inherits every
rule in this document.

### 2.5 The accelerator is TCG, and the run has no time budget yet

The guests run on QEMU's default accelerator, TCG, the same as Phase 10's
matrix, so a row's reading here is comparable with the same row there. WHPX
is faster, but on this host it needs `kernel-irqchip=off` to create a
partition at all (Phase 2d), and it would add a variable the comparison does
not have.

No wall-clock budget is set before the first run. The run records its own
elapsed time per target in the report header (section 9), and a budget, if one
is ever wanted, is set from that measurement rather than guessed.

## 3. What "fresh" has to mean here

Fresh is about history, not about the last five minutes. The guest is an OS
installed for this work, carrying the driver under test, whatever staging the
run needs, and nothing else this project put there: no build tree, no
development share, no tooling from `scripts/local/`.

### 3.1 Where the fresh images come from

The operating-system installs are not automated. The owner's decision is that
a base image is built by hand once per release, and this project already
holds the installs to build from: two internal qcow2 snapshots taken before
any build of this driver existed, one per target.

| Target | Source image | Snapshot | Taken | State |
|---|---|---|---|---|
| `2a` | `vm/win98.img` | `post-nusb` | 2026-07-22 | Windows 98 SE, NUSB 3.3 installed, the xHCI controller unclaimed (`Code 28`), no `usbd.sys` |
| `2b` | `vm/win2k-xonly.img` | `win2k-xonly-clean-install` | 2026-09-03 | Windows 2000 SP4 installed with no USB controller of any kind attached, so no `usbport.sys`, `usbhub.sys` or `usbhub20.sys` on disk |

The Windows 2000 row changed on 2026-09-03 (roadmap task 19.5). Until then
it was `vm/win2k.img @ phase2b-clean` (2026-07-24, the xHCI unclaimed,
`Code 1`), an install that had booted with an EHCI, and the in-box driver
install for that EHCI is what had placed `usbport.sys`: a fresh clone of it
could never show whether the `1.0.0.2` INF places the file itself, which is
what a stranger's xHCI-only machine needs. The first post-release run
(section 9) ran on the old base. `build-and-test.md`, "Windows 2000
xHCI-only VM", has the recipe, and `scripts\setup-qemu-win2k-xonly.ps1`
the launchers.

`post-nusb` is chosen over the later `phase2a-usbd-ok` on purpose. The
difference between them is one file, `usbd.sys`, copied in by hand on
2026-07-24 as an environment fix. NUSB does not ship it, an xHCI-only machine
never triggers the Windows 98 path that would, and `xhci98.inf` carries it for
that reason. A base image that already has the file would hide whether the
INF still delivers it, and a stranger's machine does not have it either.

The snapshots are cloned out, never reverted in place. `vm/win98.img` and
`vm/win2k.img` are Phase 10's carried-along images and every later snapshot in
them is a child of the state a revert would discard; the harness has no
business writing to either. The preparation (section 8) produces two new
files, `vm/fresh-2a.img` and `vm/fresh-2b.img`, each starting from the
snapshot above, and those are the only images this run knows about.

### 3.2 Two artefacts with different lifetimes

Section 2.1's persisted install sits inside the definition of fresh rather
than against it, and the seam has to be respected in code. The matrix harness
boots with `-snapshot` and never writes to a guest image, because a run that
mutates its own starting state is not reproducible. That rule survives here:
the image is written once, by the tester, in the preparation; the run boots it
read-only, and every run starts from the same state.

So the run has two artefacts, and confusing them is the failure mode to design
against: a prepared base image, which is a release's worth of setup, and a
run, which is repeatable and disposable. A base image prepared for one release
may not be used to judge the next.

### 3.3 The stamp that ties an image to a release

Discipline is not a mechanism, so the preparation writes a stamp and the run
checks it. The stamp is an internal qcow2 snapshot on the fresh image, named
`base-<DriverVer>-<flavour>`, taken by `prepare-image.ps1` as the last act of
the preparation, after the driver install has been persisted and the guest
has shut down cleanly. `qemu-img snapshot -l` reads it back without booting
anything.

The run reads the stamp before it boots, and refuses when any of these fail:

- there is no `base-` snapshot at all (the image was never prepared, or is one
  of Phase 10's);
- the version in the stamp is not the version of the build under test, taken
  from the same single source the packager uses;
- the flavour in the stamp is not `qemu`;
- the stamp is not the newest snapshot on the image (something was persisted
  after the preparation finished, and the image is no longer the state the
  stamp names).

The running driver's `MiniPortExtensionSize` cross-check stays as the second,
independent witness: the stamp says what was installed, the identity line says
what is running, and the run wants both to agree.

## 4. The population, and what this vehicle cannot present

The devices are those step 5 of `docs/using/release-acceptance-test.md` names,
intersected with what this QEMU build can present, which
`scripts/vm-matrix/probe-devices.ps1` reports without a guest. The intersection
is smaller than the step-5 list, and the gaps are measured rather than
suspected:

- Low Speed does not exist in this build. Task 10.1 measured it and design
  record 06 section 7 records it: the Low-Speed HID clause is bare metal's, and
  here it is recorded as not reached.
- Composite is present only as multi-interface models such as `usb-audio`, and
  on Windows 98 that particular unit is confounded: the release notes' USB Audio entry
  records a failure inside Windows 98's own `USBAUDIO.VXD` seen so far only
  with QEMU's emulated device. The composite-parent clause that issue 3
  (`docs/issues/03-usbhub-sys-composite-devices.md`) exists for has its real
  reading on metal.
- Full and High Speed HID, storage and hubs are the models that carry weight.

The rule is design record 06's, unchanged: a model this vehicle cannot present
is a reading of the vehicle. It is recorded as not reached, never as a pass and
never as a silence.

### 4.1 The composite row on Windows 98 runs, pinned to the known failure

The `usb-audio` row stays on target `2a` and is attached like any other. Its
expectation on that target is written as the reading the release notes' USB
Audio limitation records,
so a run that reproduces the `USBAUDIO.VXD` failure passes its own pinned
clause and a run that does not reproduce it shows in the diff either way. On
Windows 2000 the row keeps its ordinary expectation. Excluding it would have
been cheaper to prepare and would have turned a known reading into a silence.

### 4.2 A class the fresh OS has no driver for

A freshly installed guest has fewer class drivers than one that has been
taught for a month, so more rows will resolve to `NODRIVER` here than in
Phase 10, and most of those are the operating system's fact rather than a
regression. The outcome word does not change: design record 06's five
outcomes stand, and `NODRIVER` still derives from `devices addressed` moving
while `endpoints opened` does not.

What changes is the matrix file. A row known to lack a class driver on a fresh
install of a given target carries an `ExpectNoDriver` entry for that target,
with a one-line reason, written before the run. A row that reaches `NODRIVER`
with the entry is reported as `NODRIVER` and does not count against the
target's verdict; a row that reaches `NODRIVER` without the entry is reported
the same way and does count, because it is a class this document said the OS
would claim. Both stay in the report, so the diff between two releases shows
every one of them. Writing the entry is a guess until the first run confirms
it, and the first run is where the entries get corrected, not where they get
invented after the fact.

### 4.3 The storage row enumerates, and does not round-trip a file

Step 5.2 writes a file to the flash drive and reads it back. That leg is not
taken here. The harness drives the guest from the QEMU monitor and runs
nothing inside it; a file round-trip would need a resident agent in the base
image, which is something this project put there, and section 3 says the
image carries nothing of the kind. The storage row asserts what the counters
can see: the drive addressed, its endpoints opened, on every leg of section 5.
The matching-contents reading stays the hand run's.

## 5. The lifecycle, and what counts as the reading

Per model: attach, let it settle, read, detach, let the bus empty, read, attach
it again, let it settle, read. The last leg is the one Phase 10's matrix does
not systematically assert and the acceptance test does: a device that comes
back without a Refresh and without a prompt is a different claim from a device
that enumerated once.

### 5.1 What "came back" means in counters

The reattach leg is judged from the counters alone, on the `qemu` flavour,
against expectations written in the committed matrix file before the run. For
a row whose first attach was expected to open endpoints, the reattach is
expected to do the same: `devices addressed` and `slots enabled` advance a
second time, and `endpoints opened` advances again to at least one, inside the
same settle deadline the first attach was given. For a row that resolved to
`NODRIVER` on the first attach, the reattach is expected to reach the same
outcome, and a reattach that opens endpoints where the first attach did not,
or the reverse, is a `FAIL` on that row.

This does not literally see Device Manager. A wizard that appears on the
reattach would stop the bind, and the counters would show it as the same
776-second shape section 2.1 measured, which the harness already names. A
node that comes back with a warning mark is not visible here at all, and the
design says so rather than pretending a counter covers it. The screenshot
reading of that leg is the hand run's, on both targets.

### 5.2 What is inherited unchanged

No second verdict model is invented. The five outcomes, the four kinds of
expectation, and the rule that a number is not a verdict until something said
in advance what it was supposed to do, are design record 06's and are
inherited whole. The reattach leg adds a third reading point to a row, not a
new kind of expectation.

## 6. What the run must refuse to do

Refusals belong in code, not in a comment. The harness already enforces four
of its own; these are this run's.

- Refuse to continue where a person would have to answer something. A prompt
  that appears is a defect in the preparation, and the run says so and stops
  rather than waiting on it.
- Refuse to boot an image without a valid stamp (section 3.3), and refuse to
  measure a guest whose running driver does not match the build under test.
  The matrix already refuses the second by cross-checking the offset table's
  `SIZEOF` against the running driver's `MiniPortExtensionSize`; the stamp is
  what stops a base image prepared for the last release being reported as
  this one.
- Refuse to boot `vm/win98.img` or `vm/win2k.img`, or any image whose newest
  snapshot is not a `base-` stamp. Phase 10's images are not fresh and the
  run has no way to make them so.
- Refuse to write to the image it booted.
- Refuse to turn "this vehicle cannot present it" into a pass.

## 7. The questions that were open, and how they were settled

The first version of this record listed these as open. The owner settled them
on 2026-08-29, and they are recorded here so the implementation does not
reopen them.

| Question | Decision | Where |
|---|---|---|
| Automate the OS install, or build a base image by hand per release | By hand, from the two pre-driver snapshots, cloned out | 3.1, 8 |
| How the package reaches a guest whose USB does not work yet | The VVFAT transfer drive `prepare-image.ps1` already uses; the same path as Phase 10's preparation | 8 |
| A new report, or an extension of the matrix report | The matrix report with a header block, in its own output directory | 9 |
| Whether "came back without a Refresh" is machine-readable | Counters only; the Device Manager reading stays the hand run's | 5.1 |
| What `NODRIVER` means on a fresh guest | The same word; rows expected to lack a class driver are marked in the matrix file | 4.2 |
| Whether the `release` binary is ever run here | No; it is read on a physical machine, by hand | 2.2 |
| The storage row's file round-trip | Not taken; enumerate, detach, reattach | 4.3 |
| The composite row on Windows 98 | Run, pinned to the release notes' USB Audio reading | 4.1 |
| Accelerator and time budget | TCG; no budget until one run has been measured | 2.5 |

## 8. The preparation

This is the manual part, taken once per release by a tester, and it is the
only part a person does. Each step is one command or one action; the wizard
in step 5 is the rung section 2.1 allows.

1. Build and package the `qemu` flavour of the version under test
   (`scripts\build-driver.cmd qemu`, then
   `scripts\package\make-package.ps1 -Flavor qemu`), and regenerate the offset
   table (`gen-offsets.ps1`).
2. Clone the source snapshot into a new image, without touching the source:
   `prepare-image.ps1 -Target 2a-fresh -Clone`, and the same for `2b-fresh`.
   Underneath it is `qemu-img convert -O qcow2 -l snapshot.name=<tag>`, which
   opens the source read-only; the target's `CloneFrom` names the image and
   the snapshot. The source images stay closed for the whole preparation.
3. Boot the fresh image with `prepare-image.ps1` against a new target id
   (`2a-fresh`, `2b-fresh` in `scripts\vm-matrix\matrix.config.psd1`, pointing at the new
   files), with the VVFAT transfer drive carrying the package.
4. Install the driver from the transfer drive, following `readme.txt` section
   4 for the target, the way step 4 of the acceptance test says: point at the
   directory, not at a loose file. On Windows 98 this is where the CD image in
   `Win98Cd` is needed.
5. Teach the Windows 98 image each device class the matrix will attach, using
   `prepare-image.ps1 -Attach` per class, so no wizard appears in the run.
   Windows 2000 needs none of this for HID and storage.
6. Shut the guest down cleanly and take the stamp:
   `prepare-image.ps1 -Stamp`, which snapshots the image as
   `base-<DriverVer>-qemu` and is refused if the guest is still running.

Steps 2 and 6 are new to `prepare-image.ps1`; steps 3 to 5 are what it does
today. The stamp is taken last so that anything persisted after it fails the
newest-snapshot check in section 3.3.

## 9. The record

The run writes Phase 10's report format, one line per target, row and
expectation, into a directory of its own: `out\post-release\<DriverVer>\`.
Each target gets its own report, debug-console log and QEMU trace, the way a
matrix group does.

The report gains a header block, which is what makes two releases' runs
diffable against each other and is absent from the matrix report today:

```text
# post-release run
# driver:    1.0.0.0 qemu, <bytes> B, sha256 <first 16 hex>
# image:     vm\fresh-2a.img, stamp base-1.0.0.0-qemu, from win98.img post-nusb
# qemu:      <version string>, accel tcg
# offsets:   SIZEOF <n>, <m> counters
# started:   <date time>, elapsed <h:mm:ss>
# verdict:   2a-fresh PASS|FAIL, <rows> rows, <n> NODRIVER expected, <n> not reached
```

The console prints the same tally per target when the target's groups are
done, with one more number the header does not carry:
`=== 2a-fresh: PASS (<rows> rows, <n> NODRIVER expected, <n> not reached,
<n> against; <h:mm:ss>)`, where `against` is the count of rows that counted
against the verdict under section 4 and section 5 (a `FAIL`, an `ERROR` the
matrix did not declare, a `NODRIVER` without an `ExpectNoDriver` entry).

Every line after the header is a row line in the existing format, so a diff
between `out\post-release\1.0.0.0\` and the next version's directory reads
the header first and then the rows, and a row that changed outcome between
releases stands out in the same way a row changed by a driver edit does in
Phase 10.

Whether a run's report is committed under `docs/contributing/runs/` is the
owner's call per release and is not part of this design.

## 10. What this design does not do

- It is not acceptance of the published release. That is the hand-run
  procedure the roadmap ends on, it is taken by a person from the download,
  and nothing here closes any of it.
- It says nothing about bare metal. Every clause that needs a BIOS handoff, a
  real interrupt pin or an uncharacterised controller stays where it was.
- It does not run the `release` binary (section 2.2), the qualifier or the
  log channel (section 2.3).
- It does not see Device Manager. The warning mark and the "no Refresh"
  reading on a replug are the hand run's on both targets (section 5.1).
- It does not round-trip a file through storage (section 4.3).
- It does not replace the hand-run acceptance procedure, and a script that has
  never disagreed with one is untested rather than correct.

## 11. What was built, and what the task still waits on

The implementation went into `scripts/vm-matrix/` on 2026-08-29, as an
extension of the Phase 10 harness rather than a second one, which is what
section 1 asked for. `scripts/vm-matrix/README.md` has the commands; this
section records the readings taken and the places where the code had to
choose something this record left open.

What exists:

- `lib/fresh.ps1` carries every rule above that can be stated without a
  guest: the stamp name and its parser, the four refusals of section 3.3 and
  the image-name refusal of section 6 as one function over a snapshot list,
  the version under test read from `src/xhci_version.h`, the inherited
  per-target keys, `ExpectNoDriver`, the replug verdict of section 5.1, what
  counts against a target, and the header of section 9. `selftest.ps1` feeds
  each of them made-up inputs and asserts every refusal fires and every
  acceptance holds, guestless; it also drives the runner's own decisions
  (the target split, the writable-image refusal, the two-leg loop, the
  verdict and the report file) and `prepare-image.ps1`'s clone and stamp
  refusals through the same functions, and reads the snapshot list through a
  stand-in `qemu-img` so a nonzero exit is shown to be refused. The suite
  prints its own check count.
- `prepare-image.ps1 -Clone` and `-Stamp` are steps 2 and 6 of section 8. The
  clone is `qemu-img convert -l snapshot.name=<tag>`, which opens the source
  read-only; the stamp is refused while the prep guest is up and refused when
  the last prep boot's debug console never showed the qemu build running with
  the offset table's `SIZEOF`, so the stamp's claim about what was installed
  rests on a reading and not on the operator's word. On a fresh target the
  transfer drive carries the whole qemu package, INF and per-target
  `usbd.sys` included, because the install goes through the INF on a guest
  that has never had the driver. No safety snapshot is taken on a clone; the
  clone is the safety, and a snapshot under the stamp would only be a state
  the run has to read past.
- `run-matrix.ps1 -PostRelease` boots the fresh targets and nothing else,
  checks every stamp before the first boot (`-ValidateOnly` runs the same
  checks and boots nothing), forces `-snapshot`, takes each row through two
  attach legs, honours `ExpectNoDriver`, treats a declared wedge as
  non-counting, and writes one report per target under
  `out/post-release/<DriverVer>/` with the header of section 9. A fresh
  target names the Phase 10 target it inherits from (`Like = '2a'`), so the
  tablet exclusion, the audio row's inert clauses and its `MayWedgeGuest` all
  apply to the fresh Windows 98 guest as they do to the taught one.

Choices the code made where this record was silent:

- The replug leg is printed as a second row named `<row>/replug`, and the
  row's own outcome line follows only when the two legs disagree. The report
  format is otherwise Phase 10's, with the row column widened to fit.
- A five-second pause separates the detach from the replug, so the guest can
  finish removing the node; the acceptance test's warning about rapid cycling
  on Windows 98 is the reason.
- The composite row's pinned reading (section 4.1) is carried as the existing
  `MayWedgeGuest` declaration: a group that ends on such a row is still
  `ERROR` in the report and does not count against the target. No new outcome
  word was added, and a run that does not reproduce the wedge shows in the
  diff because the line changes.
- Rows are still booted in Phase 10's groups. The group is the blast radius,
  and nothing here changed that argument.
- The `ExpectNoDriver` entries in `matrix.psd1` were written from Phase 10's
  measurements on the carried-along images (`usb-net`, `usb-serial`,
  `usb-braille`, `usb-ccid` on both targets; the two `usb-mouse` rows, the
  Wacom tablet and `u2f-emulated` on Windows 2000). They are the guesses
  section 4.2 says the first run corrects.

What the run cannot see, stated rather than pretended: a prompt. The harness
has no guest agent, so a wizard that appears mid-run is visible only as the
counter shape of section 2.1 and in the screenshot every non-`PASS` row
takes; the readiness wait names the wizard when it happens at boot.

Status on 2026-08-29: both fresh images were cloned (`vm/fresh-2a.img` from
`post-nusb`, `vm/fresh-2b.img` from `phase2b-clean`), and the run refused
each of them as unstamped, which is the reading the check exists to give.

## 12. The first run, 2026-08-30

The manual rung was taken on both images on 2026-08-30 against the re-cut
`1.0.0.0` (`qemu` flavour, sha256 `2024bdd751dbf202`, SIZEOF 90924): the
driver installed through the INF from the transfer drive on each guest, the
Windows 98 image was taught thirteen classes through `-Attach`, both images
were stamped `base-1.0.0.0-qemu`, and `run-matrix.ps1 -PostRelease` ran to
completion with nobody at the keyboard. Windows 98 took 51 minutes for 17
rows, Windows 2000 70 minutes. The verdict was `FAIL` on both targets, and
none of the rows that counted against either was a driver reading.

What the driver read on a clean install: every leg of every row that got a
reading had zero fatal controller status, zero refused endpoint opens, zero
interrupt mask failures, zero abandoned commands, and the transfer identity
held. On Windows 2000, eleven rows passed both legs, the Wacom tablet and
`usb-audio` among them, which means two `ExpectNoDriver` guesses (section 4.2)
were wrong in the good direction and are corrected by this run. On Windows 98,
eight rows passed both legs, and `usb-audio` bound on its first arrival.

What counted against, and what each was:

- `usb-serial/fs` and `usb-braille/fs` read `ERROR` on both targets: the
  device occupied its port but was never electrically attached. QEMU's `null`
  chardev is never open, and both models attach themselves only when their
  chardev is; `qom-set attached true` is refused for them. Phase 10 recorded
  the same state as `FAIL +0` on both targets and wrote `ExpectNoDriver`
  entries on it, so those entries were never measurements. The runner now adds
  a `file` chardev (measured with `qom-get`: `null` gives `false`, `file`
  gives `true`, for both models), and the prep script declares its two
  chardevs the same way.
- `usb-uas/fs` on Windows 98 read `NODRIVER` behind an Add New Hardware
  Wizard, then `FAIL +0` on the replug because the modal wizard blocks the
  PnP stack. The class had not been taught: `prepare-image.ps1 -Attach uas`
  attached the bare adapter, which QEMU never presents without a LUN, so the
  prep pass saw no wizard and the operator read the silence as "already
  taught". The prep script now adds the matrix row's `scsi-hd` child and
  repairs `attached` the way the runner does, and the same fix covers
  `usb-bot`. On Windows 2000 the row read `NODRIVER` on both legs with a
  Found New Hardware wizard open, which does not block enumeration there;
  `matrix.psd1` carries that as an `ExpectNoDriver` entry now.
- `usb-audio/fs` on Windows 98 passed its first leg and read `FAIL +0` on the
  replug: the second arrival raised an Insert Disk prompt for the Windows 98
  CD, which the run does not attach. Attempting to teach the second instance
  in a prep pass with the CD attached wedged the guest on the device's third
  arrival (shell dead, one processor spinning, the driver's interrupt path
  still logging port changes), which is the reading the row's `MayWedgeGuest`
  already carries. The audio device is not to be cycled in a prep pass; the
  replug reading stands as this run's fresh-guest measurement of the USB
  Audio limitation.

Two things the preparation learned, recorded in `scripts/vm-matrix/README.md`:
a class the prep pass attaches without a wizard appearing is not necessarily
taught, and the counters (`devices addressed`) are the check; and Windows 2000
on the Standard PC HAL ends its shutdown at "It is now safe to turn off your
computer" rather than powering off, so the prep pass ends with `quit` at the
monitor at that screen.

The Windows 98 image was reverted to its stamp, taught `uas`, `serial` and
`braille`, and re-stamped, and the run was made again on both targets with
the corrected harness. That second run's reports are the ones the checkpoint
reads; they are committed as `docs/contributing/runs/run-16-post-release/`
(the screenshots, traces and scratch disks stay under
`out/post-release/1.0.0.0/`, which is not tracked):

- `2b-fresh`: `PASS`, 17 rows, 0 not reached, 0 against, 1:16:07. Eleven
  rows passed both legs; `usb-net`, `usb-serial`, `usb-braille`, `usb-ccid`,
  `u2f-emulated` and `usb-uas` read `NODRIVER` on both legs with the device
  attached and addressed, each with its entry, so every entry for Windows
  2000 is now a measurement.
- `2a-fresh`: `FAIL`, 17 rows, 3 not reached (the standing exclusions), 2
  against, 0:57:08. `usb-serial` and `usb-braille` now read `NODRIVER` on an
  addressed device. `usb-uas/fs` read `NODRIVER` on both legs with no wizard,
  and its `ExpectNoDriver` entry was written on that reading after the runner
  had loaded the matrix, so it still counted; a third run counts it expected.
  `usb-audio/fs` passed its first leg and failed its replug on the same Insert
  Disk prompt as the first run, so that reading is stable and it is the one
  row that stands against the fresh Windows 98 guest.

What the two runs say about the driver, in one sentence: on a clean install
of either target, every device this QEMU build can present that the operating
system has a driver for binds, unbinds and binds again on the same root port
with no fatal status, no refused endpoint open and the transfer identity
intact, and the one row that fails does so on a Windows 98 file-copy prompt
the harness is not allowed to answer.
