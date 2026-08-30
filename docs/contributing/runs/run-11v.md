# Batch 11-V Run Sheet

The run sheet for roadmap batch 11-V, Phase 11's VM half. It was written
before any of it ran, so that each clause's pass condition was decided in
advance rather than in front of a booted guest under time pressure. Each stage
below has its plan first and a "Run" section after it recording what was
observed, on which target, with what result. Where the two disagree, the run
record wins.

**On `vm\...` paths in this file.** The per-run evidence they name - debugcon
traces, QEMU trace logs, screenshots, counter dumps - was discarded on
2026-08-30 after the run records below had transcribed every reading it rests on. A
`vm\` path here says where a reading was taken and what the file was called;
it is not a file a clone, or the maintainer's own tree, still has. `vm/` now
holds only the guest images and transfer disks the harness needs.

The whole batch has run, stages A to H, and Phase 11's checkpoint was declared
met on the last of those days. The clauses this vehicle could not reach are
listed at the Phase 11 checkpoint in `docs/contributing/roadmap.md`, each with
the Phase 12 or Phase 13 task that owns it; none of them is recorded here as
passed. A stage that turns out to cost three boots instead of one is worth
more written down than the estimate it replaces, so the run sections record
boot counts too.

Before using this sheet again:

- Division of labour: the operator drives everything inside the guest window
  (wizards, Device Manager, dialogs). The agent keeps the QEMU monitor, the
  traces and the counters (`docs/contributing/build-and-test.md`).
- **If you re-run this sheet, every `-debug` media below means `qemu` now.**
  Task 13-L.1 split the build three ways and moved every `XHCI_DBG_*` site and
  the port-`0xE9` mirror into the `qemu` flavour, so the shipping `debug` build
  prints nothing as it runs. Every clause here that blocks on a trace (the
  `DriverEntry (built ...)` identity line, the `MiniPortExtensionSize` read,
  the counter reads from stage C onwards) needs a `qemu` guest. A `debug` guest
  boots and drives devices perfectly well and then fails as a boot TIMEOUT with
  no hint of the cause, which is how the vm-matrix harness broke.
  `make-11v-media.ps1` now defaults to `qemu` for that reason. The stage B
  upgrade legs, which are about install behaviour rather than the trace, still
  want the two shipping flavours. The run sections record what was actually
  installed at the time.
- A healthy trace is not a living guest, and a refused monitor port is a dead
  QEMU process rather than a dead guest. Screenshot before concluding anything
  about liveness.
- `+0` is not a pass. Several clauses below are satisfied by a counter moving.
  A counter that reads zero because the code path was never reached looks the
  same as one that reads zero because nothing went wrong, and the difference
  is the whole result. Where a zero is the expected reading, this sheet says
  which other counter makes it readable.

---

## Before the first boot

| | |
|---|---|
| Media | `powershell -File scripts\package\make-11v-media.ps1 -Flavor both`. Produces `out\media-11v\{new,dated2003}-1.0.0.3-{debug,release}\` and checks the `old-1.0.0.2-*` baseline is staged. Those version numbers are the development builds this batch was run against and neither was released; the numbering was restarted at `0.0.0.1`, so this command no longer runs as written. See the note under stage B. |
| Counter table | `powershell -File scripts\vm-matrix\gen-offsets.ps1`, then check its `SIZEOF` against the `MiniPortExtensionSize` in the first trace of the session. Equality is the only thing that says the table describes the deployed layout. Regenerate the host-local reader's table too, and check the two `SIZEOF`s against each other: the host-local one is built from a hand roster rather than derived, so it goes stale silently and reads every counter from the wrong place while looking plausible. One was found still describing a 70,608-byte extension against a 74,776-byte driver. |
| Snapshots | Stage B needs a clean snapshot of 2a and of 2b: NUSB installed on 2a, no `xhci98` ever installed on either. Take them as a copy rather than by reverting in place. Every other stage runs on the working images, and stage B is 7-9 boots and the one most likely to want a second attempt. |
| Expected footprint | `scripts\inf-gate\expected-footprint.txt`, what one install claims to place. Stage B's uninstall leg is checked against it. |
| Verifier | 2b and 2d runs are under Driver Verifier with Force IRQL checking, as every phase since 7b. Confirm it is actually on rather than assuming the setting survived. |

The build under test is one binary per flavour, and every result must record
which. The two differ in size and in import profile, so "the driver" is not a
sufficient identity for a Phase 11 result.

---

## Stage order, and why it is not the task order

| Stage | Task | Vehicle | Starts from | Est. boots |
|---|---|---|---|---|
| A | 11-V.6 | 2a | working image | 1 |
| B | 11-V.3 | 2a, 2b | clean snapshot | 7-9 |
| C | 11-V.7 | 2a, 2b | stage B's installed machines | 3-4 |
| D | 11-V.2 | 2b, then 2a | working image | 2-3 |
| E | 11-V.4 | 2a, 2b | working image | 3-4 |
| F | 11-V.5 | 2a, 2b, 2d | working image | 4-6 |
| G | 11-V.1 | 2a, 2b | working image | 2-3 |
| H | 11-V.9 | 2a, then 2b | working image | 2 |

Estimates, not commitments; the total is roughly 22-30 guest boots, plus
stage H's two.

H is last on purpose. Task 11-V.9 moves the extension layout, which forces
both offset tables to be regenerated and the staged media rebuilt. Doing that
mid-batch would have cost every stage above it the "both targets, one binary"
property its results rest on. It is also the only stage whose task is mostly
host-side: everything but its own run tail was done without a guest.

Three of the orderings matter and the rest is convenience:

- A is first because it can end its own task, in one Windows 98 boot, before
  anything else is set up. If the differential says NUSB's own EHCI miniport is
  equally deaf, task 11-V.6 becomes a documented limitation and every later
  stage stops being an opportunity to re-investigate it.
- B is early because it is the only stage that needs a clean snapshot, and it
  produces the installed machines every stage after it uses. Running it late
  means restoring a snapshot and losing the state the stages before it built.
- C follows B for a narrow reason. Task 11-V.7's switch reaches the registry
  through the INF's `AddReg`, and the existing 2a/2b installs predate the INF
  that carries it, so only a fresh install can show that the engine wrote the
  default. That is stage B3. The flush itself could be measured on an older
  guest with the value typed in by hand: the driver starts with logging
  disabled, and an absent value, a failed read and an explicit `0` all leave it
  that way, so there is no runtime state that a fresh install reaches and a
  hand edit does not. C sits after B because that is the cheap way to get both
  halves out of one installed machine.

G carries a clause that will not close here. Task 11-V.1's `ResumeController`
half is measured unreachable in QEMU (no available guest offers a resumable
power transition), so it needs a bare-metal Windows 2000 machine, which this
project has no vehicle for. Run G's other clauses and record the resume half
as deferred with its reason, not as unattempted.

---

## Stage A - task 11-V.6, the Windows 98 idle hot-plug differential

One boot on 2a, and it may end the task. Add an EHCI controller beside the
xHCI (`-device usb-ehci`; the Windows 2000 ACPI launcher already has the shape).
Let the machine go fully idle. Then hot-plug a device on each controller and
see which one notices.

| Observation | Reading |
|---|---|
| A device hot-plugged on the xHCI while idle | expected: nothing until a Device Manager *Refresh* |
| A device hot-plugged on the EHCI while idle | this is the whole experiment |
| `RootHubInvalidates`, `SuspendCount`, `ResumeReinits` | the suspend/resume cycling that makes the window; `SuspendCount` climbing with no `RootHubInvalidates` is the defect in numbers |

- If EHCI is equally deaf: this is the back-ported `usbport.sys`, no miniport
  can fix it, and task 11-V.6's stop rule fires. Write it up as a measured
  limitation in `docs/contributing/build-and-test.md` and
  `docs/using/release-notes.md` (the `DisableSelectiveSuspend` entry), which is drafted for this outcome
  and needs its "OWED" line replaced rather than removed.
- If EHCI enumerates: the shipping miniport is doing something this one is
  not, and finding out what becomes the deliverable. Do not start writing a
  wake path. Task 11-V.6's second bullet lists three candidates and the
  objection that kills each, and the standing rule since Phase 4 task 7 is to
  derive the mechanism from the binaries first.
- Either way, do not ship a partial `USB_MINIPORT_FLAGS_WAKE_SUPPORT`. A flag
  whose obligations are half-met is worse than the honest gap, because it
  changes what `usbport.sys` believes about a controller this driver cannot
  wake.

Guest-side note: Windows 98 idle-suspends within about half a second of the
last transfer, so the idle state is easy to reach and easy to leave by
accident. An attached hub or a device generating traffic keeps the controller
out of idle suspend, so the bus must be bare while waiting.

### Run

Done, and it cost three boots rather than one, because the differential came
out the way that opens work. The EHCI does enumerate, so the stop rule's first
branch did not fire and the remaining candidates each had to be disposed of by
measurement: one extra boot with a `CheckCallbacks` entry counter (usbport does
not call the miniport at all while suspended) and `pci_cfg_*` tracing (no PM
capability on `qemu-xhci`; no power register written at suspend). The full
record is roadmap task 11-V.6; the outcome is `docs/using/release-notes.md`
(the `DisableSelectiveSuspend` entry). Budget three boots for a stage whose differential goes this way.

Two things that cost time: this Win98 guest cannot be restarted from inside
(shut down, close QEMU, relaunch), and an idle-suspended controller is a
per-controller state, so the EHCI must be measured halted immediately before
its attach or its wake is inferred rather than observed.

One reading in this stage's territory is still owed, carried from batch 7b-V's
rider. Repo-audit item B1, `DisownPending` cleared across the resume path's
shadow rebuild, could not be exercised there: both counters read 0/0 on both
targets because no port disable happened at all (the trigger is a cancelled
driver wizard, and neither guest raised one), and 2a never idle-suspends once a
hub is attached. Its target reading belongs on a boot where a root-port
device's wizard is cancelled and the bus then left idle. The fix is not in
doubt on correctness grounds; it is unexercised on a target.

---

## Stage B - task 11-V.3, the package on clean snapshots

The only stage that starts from a clean snapshot, and the order within it is
fixed by what each step leaves behind.

About the version numbers in the `out\media-11v\` paths below: this stage needs
two packages differing in the field the Windows 2000 setup engine ranks by, so
it was run against a baseline and a candidate built from the tree during
Phase 11. Both were development builds and neither was released. The project's
version numbering was later restarted at `0.0.0.1`, so the numbers survive here
only as the directory names the run used. Read every reading below as "the
baseline's" or "the candidate's", and see `docs/contributing/build-and-test.md`,
"Versioning the driver". One consequence: `make-11v-media.ps1` will not rebuild
this media at the tree's current version, and re-running this stage means
supplying a baseline of your own.

### B1. Fresh install of the baseline

`out\media-11v\old-1.0.0.2-debug` on 2a and on 2b.

| Observation | Reading |
|---|---|
| Device Manager name | `USB 2.0 eXtensible Host Controller (xhci98)`, no yellow bang |
| Driver tab, Windows 98 | Provider `xhci98 Project`, Date: the driver file's UTC mtime (see the run below), no version; the version is under *Driver File Details* |
| Driver tab, Windows 2000 | Provider `xhci98 Project`, Version the baseline's, Date: see B5 |
| `SYSTEM32\DRIVERS` | `xhci98.sys` only. Not `xhci98.tmp`: in the INF's `xhci98.sys,,xhci98.tmp` the third field is the temporary name the engine uses when the destination is in use, so a fresh install leaves none. It is upgrade residue, and B4's footprint check is where to look for it. |
| `usbd.sys` size | 19 KB on Windows 98 (18,912, the `usbd98` build), 20 KB on Windows 2000 (20,688, `usbd2k`). This is the per-target split validated on a real install rather than by hash on the host, and the sizes are the cheapest discriminator in the guest. |
| `usbd.sys` | present, and not replaced if one was already there; the copy carries `COPYFLG_NO_OVERWRITE` |
| Trace | `DriverEntry (built ...)`, and the built stamp must be the baseline's |

#### Run, 2a

Passes, with two findings the table did not anticipate.

(a) An in-place upgrade bugchecks Windows 98, at the same `0028:C00312EE` as
the teardown fault documented under B4, because an upgrade stops the running
driver and that stop is the teardown. So the Windows 98 leg costs three
crashes, not the two B4 budgets. This is not a defect of this driver: the same
address reproduces on Microsoft's own `usbehci.sys`.

(b) Windows 98's Driver tab "Date" is the driver file's timestamp, not the
INF's `DriverVer` date. A fresh install of the `08/08/2026` baseline displayed
`8-12-2026`, which is that binary's mtime (local `08/13/2026 00:52`) shown as
UTC by QEMU's vvfat on a UTC+8 host. Batch 8-V read `8-8-2026` off a package
whose file date and INF date happened to agree, so it could not tell the two
apart. Read B1's and B2's date rows against the file's UTC mtime, and treat any
date clause on this target as answering "which file", not "which `DriverVer`".
B5 is where `DriverVer`'s date is tested, and it is a Windows 2000 experiment.
B4's uninstall cycle later refined this further: the displayed date is cached
in the registry at install time, derived from the file's timestamp, and is not
read from the file live.

#### Run, 2b

Passes, and the Driver tab is the mirror image of 2a's: Provider `xhci98
Project`, Version the baseline's, Date `Not available` (batch 8-V's reading
reproduces on a pristine image rather than being an artifact of that machine's
history), and a fourth field 2a does not have, `Digital Signer: Not digitally
signed`, which B5's table names as one of the two differences that stay
confounded if the date never renders.

`xhci98.sys` 138,939 (13-08-26 12:52a, the baseline) and `usbd.sys` 20,688
dated 19-06-03, the `usbd2k` build. Against 2a's 18,912 dated 04-23-99, that
pair proves the per-target `usbd.sys` clause from both sides: each engine
reached only the file its own sections name, and each file carries its own
OS's date.

Driver Verifier was off on this image and had to be turned on. The
`phase2b-clean` snapshot predates the Verifier setup later phases used, so the
setting had never been there to survive. `verifier /flags 27 /driver
xhci98.sys` plus a restart gives `Level: 0000001B` with `xhci98.sys` listed
under *Verified drivers*, `loads: 1, unloads: 0`, `AcquireSpinLocks: 2850`.
Confirm it rather than assuming it; this run was the first to catch it.
Incidentally every pool counter reads 0 (`AllocationsAttempted`,
`UnTrackedPool`), which confirms the no-pool-allocation rule from the other
side of the ABI.

### B2. Upgrade over it

`out\media-11v\new-1.0.0.3-debug`, without uninstalling first. This is the
clause that must be proved rather than assumed: Windows 2000 ranks candidate
drivers by date and then version, and B5 below is open about whether it is
recording our date at all.

| Observation | Reading |
|---|---|
| The install is accepted rather than declined as not-better | the clause |
| Driver tab afterwards | the candidate's version / `08/13/2026`; on Windows 98, *Driver File Details* is where the version is |
| Trace after the reboot | `DriverEntry (built ...)` naming the new build, not the old one |

If the upgrade is declined, that is a result and not a blocked stage: record
which target declined it and go straight to B5, which is the experiment that
explains it.

#### Run, 2a

Run twice, the second time from a regenerated pristine image. The upgrade is
accepted, and it crashes, and the two facts are independent. The binary lands
(`DriverEntry` names the new build after the reboot) while the crash aborts
everything after the file copy. Three independent witnesses agree the registry
phase never ran: `Services\USB` did not exist, the driver key's `DriverDate`
still named the baseline file (` 8-12-2026`), and `XhciLogEnable`'s key was
otherwise untouched. So a Windows 98 upgrade delivers a new driver onto an old
registry description. The machine reports the previous version while running
the new binary, and any INF value introduced by a new package cannot arrive
this way.

B2a, required on Windows 98: after the upgrade, run the INF's right-click
Install (`D:\PKGNEW` -> `xhci98.inf` -> Install). `[DefaultInstall]` touches
no device, so it cannot hit the teardown fault, and it is what delivers
`[Xhci.AddReg.Global]`. Verified: `Services\USB\DisableSelectiveSuspend = 1`
appears, and the next boot reads `SuspendController: 0`, `USBCMD 0x00000005`,
with a hot-plugged keyboard enumerating unaided. On Windows 2000 the same step
must leave that value absent; the NT path does not carry it.

The driver key on this run was
`HKLM\System\CurrentControlSet\Services\Class\USB\0002`, with `XhciLogEnable`
present and `0`, written by the engine, alongside `InfPath XHCI98~1.INF`,
`InfSection Xhci.Dev` and `NTMPDriver xhci98.sys`. `<NNNN>` is not fixed by
anything; it read `0002` on two independently built images.

How to find that key matters, and "the subkey whose `NTMPDriver` is
`xhci98.sys`" is only safe on a clean image. A working image accumulates one
driver key per PCI address the controller has ever been enumerated at, and this
batch's launchers move it: stage A adds an EHCI, stage D a second xHCI. On 2a
at stage H there were three subkeys whose `NTMPDriver` is `xhci98.sys`
(`0002`, `0004` and `0009`) and only one was live. Values typed into a stale
one are read by nothing, and `UsbPortGetMiniportRegistryKeyValue` reports that
as a plain `MP_STATUS_UNSUCCESSFUL`, indistinguishable from an absent value.

Use the devnode's own `Driver` value instead, which is what
`IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DRIVER)` resolves to:
`HKLM\Enum\PCI\VEN_1B36&DEV_000D&...\BUS_00&DEV_nn&FUNC_00` -> `"Driver"`.
Confirm the slot with `info pci` on the monitor. On Windows 2000 the equivalent
is the devnode's `Driver` under `HKLM\SYSTEM\CurrentControlSet\Enum\PCI\...`,
and the same caution applies for the same reason.

#### Run, 2b

Declined, and this is the stage's most consequential result. Setup refused to
upgrade the baseline to the candidate with the new package as the only search
location (`Specify a location` = `E:\PKGNEW`, floppy/CD-ROM/Windows Update all
unticked), answering *"A suitable driver for this device is already installed"*
and naming the incumbent `c:\winnt\inf\oem0.inf`. Its `Next` there means
reinstall the current driver, which is what "Windows has finished installing
the software for this device" then reported, so that page is not evidence of
an upgrade.

Three independent witnesses that nothing was upgraded: the wizard's own text;
the loaded binary still `built Aug 13 2026 00:52:10` across four `DriverEntry`
cycles; and `xhci98.sys` on disk still 138,939, not 139,995.

The offered package was newer on both ranking keys (the candidate's
`08/13/2026` date and higher version against the baseline's `08/08/2026`), so
this is not Setup applying date-then-version correctly. Read beside `Driver
Date: Not available`, the hypothesis is that Setup never parses our `DriverVer`
date, and a candidate with no usable date cannot win a date-first ranking, so
the incumbent always stays.

This is a user-facing limitation, not a test artifact: the package cannot
upgrade itself on Windows 2000. It also promotes B5 from an explanatory
footnote to the experiment that matters. `PKG2003` is byte-identical except
that `DriverVer` reads `02/15/2003`, both zero-padded, so it varies only the
date value.

### B3. The log switch's default

Taken here because B4 destroys the install it is about. With B2's install
still in place and nothing uninstalled yet, read `XhciLogEnable` in the
device's driver key on both targets: it must exist and be `0`. The INF gate
checks that the INF says so; nothing but an install checks that the engine did
it. Record the literal key path each target used. `docs/using/release-notes.md`
owes it to its readers, and stage C needs it to turn the log on.

The value this clause names has since been retired: task 11-V.9 replaced
`XhciLogEnable` with `XhciLogFile` (`REG_SZ`, empty) and `XhciLogDebugView`
(`DWORD`, 0). The clause is unchanged in substance (both must exist and both
must be at their defaults), and the key path it records is the same one, which
is what stages C and H use it for.

#### Run, 2b

Passes, and the key carries two findings past its own clause. `XhciLogEnable`
exists and is `0x00000000` at the literal path

```
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\0002
```

found by content (`DriverDesc`, `InfPath oem0.inf`) rather than by the number,
which is not fixed by anything on either target.

There is no `DriverDate` value in that key at all. `DriverVersion` reads the
baseline's version; no date entry exists. So Setup did not merely fail to
render our date, it never stored it, which makes `Driver Date: Not available`
and B2's decline the same defect rather than two. `DriverVersion` = the
baseline's is also a fourth witness that the upgrade never happened.

Also recorded from the engine's own values: `InfSection` = `Xhci.Dev` with
`InfSectionExt` = `.NTx86` stored separately, i.e. the NT engine resolved
`Xhci.Dev.NTx86`. That is the decoration model confirmed from the target, and
the same mechanism that keeps `[Xhci.AddReg.Global]` off this path. Plus
`EnIdleEndpointSupport` = 0, which is usbport's own value and not ours, and
belongs in B4's footprint reading as something this package did not write.

The 2a reading is in B2's run above (`Services\Class\USB\0002`, `XhciLogEnable`
present and `0`).

### B4. Rollback, disable/re-enable, uninstall, reinstall

Neither target has a *Roll Back Driver* button; it arrived in Windows XP.
Windows 2000's Driver tab offers only `Driver Details... | Uninstall | Update
Driver...`. So on both targets "rollback" is an uninstall plus a reinstall of
the baseline media. What Windows 2000 does more cleanly than Windows 98 is the
teardown itself: no `C00312EE`, so no rename trick and no crash budget.

On Windows 98 this stage costs crashes, and that is expected rather than a
defect to chase. Disabling any USB host controller devnode bugchecks that guest
(`A fatal exception 0E ... at 0028:C00312EE`), and driver rollback and
uninstall run the same teardown, so all three are equally fatal there. It
reproduces at the identical address on Microsoft's own `usbehci.sys`, and on a
build of ours asking for 4 KB instead of 372 KB of controller memory, so it is
neither this driver nor its declared limits. Disabling the *USB Root Hub* is
fine.

Sequence the Windows 98 leg so the price is paid as few times as possible: take
every reading that needs the install present (B1, B2, B3) before touching any
of this, and expect to reboot into the footprint check rather than to observe
it live. The steps need two teardowns there (rollback is an uninstall plus a
baseline reinstall, and the uninstall leg is another), so budget two crashes
and two reboots on 2a, and treat a third as a sign the sequence was not
followed. Windows 2000 does all three cleanly.

- Rollback: an uninstall and a reinstall of the baseline media, on both
  targets. Record the teardown differences between the two, but not as a
  difference in mechanism, because there is only one mechanism.
  - This exercises rollback over a working upgrade, which is not the clause
    task 11-V.3 asks for. That clause says "rollback after a failed start",
    and nothing in batch 11-B stages a package that installs and then fails
    to start: `make-package.ps1` refuses a probe build outright, and an INF
    naming a missing binary fails at install rather than at start. The missing
    artifact is named as owed in task 11-V.3 itself. It is host-side work, and
    building it means deciding how a package is allowed to carry a non-starting
    binary past a gate whose purpose is to stop that. Record the
    working-upgrade rollback here and name the other half as not staged.
- Disable/re-enable: on Windows 2000 only, for the reason above.
- Uninstall, then check what is left against
  `scripts\inf-gate\expected-footprint.txt`:

| Footprint row | After uninstall |
|---|---|
| `xhci98.sys` in `System32\Drivers` | gone |
| the device's driver key, and every `HKR` value in it (`DevLoader`, `NTMPDriver`, `XhciLogEnable` on 9x; `XhciLogEnable` on NT) | gone with the devnode's key |
| the `xhci98` service (Windows 2000) | measure it, do not assume it. The footprint says this install wrote the service; it says nothing about whether removing the devnode takes it away, and nothing in this repository establishes that either. `SPSVCINST_ASSOCSERVICE` is what associates the two, and the INF sets it, but "associated" is not a documented deletion rule. Record what is left in `HKLM\SYSTEM\CurrentControlSet\Services\xhci98`. |
| `usbd.sys` | still there, and that is correct. The footprint marks it `keep`: it was copied with "do not overwrite", so this package cannot claim it, and removing it leaves `usbhub20.sys` unable to load. |
| `xhci98.tmp` (Windows 98) | measured residue, not an INF claim. Record whether the uninstall takes it. It is absent from the footprint file, which derives what the INF says. |
| `oemN.inf` (Windows 2000) | same: the engine's own copy, not ours. Record what happens to it. |

- Reinstall and confirm the machine returns to a working state.

#### Run, 2a, first attempt (three boots): the uninstall does not happen at all

Device Manager -> the controller -> Remove crashes the guest at the documented
teardown. The trace ends after four `RH_ClearFeaturePortEnable` calls and
`RH_DisableIrq`, before `StopController`
(`vm\11v-stageB\b4-leg1-uninstall-teardown.log`), and the removal does not
commit. The next boot raises no Add New Hardware Wizard: the controller is
back, named, with no yellow bang and a root hub under it, the running binary is
still `built Aug 13 2026 17:02:15` (the candidate upgrade), and
`suspends=00000000`, so `Services\USB\DisableSelectiveSuspend` is still in
place and still working.

So on Windows 98 the supported uninstall route leaves the package installed,
and B4's footprint check cannot be reached through it. This is the same shape
B2 established for the upgrade, now shown to apply to removal as well.

The Driver tab's Date is what discriminates. It reads `8-12-2026` while the
file on disk is the newer binary (`PKGOLD` and `PKGNEW` UTC mtimes are one day
apart, `PKGNEW` the later). Had the devnode been removed and silently
reinstalled from the cached INF, which B2 witnessed is the new one, the driver
key would have been rewritten and the date would now read `8-13-2026`. It does
not. That is strong evidence the driver key was never rewritten, though it
does not exclude a partial commit that deleted the `Enum` devnode and left the
driver key, so read it as strongly indicated rather than proven.

Safe Mode was the obvious next route and it is a dead end on this guest. The
Startup Menu is reachable (hold Ctrl; `F5` selects Safe mode directly from it;
injecting `sendkey ctrl`/`sendkey f8` from the monitor does not work, 32
back-to-back 1 s holds of each were ignored). But Safe Mode cannot start the
GUI here: it bypasses `CONFIG.SYS`, nothing loads `HIMEM.SYS`, and the boot
falls back to a DOS prompt, where `WIN /D:M` answers `HIMEM.SYS is missing`
although `dir` shows the file present at 33,191 bytes. Both choice `3` and
`F5` land there. Do not spend boots on Safe Mode or on `BootMenu=1` in
`MSDOS.SYS`.

#### Run, 2a, second attempt on a regenerated pristine image: the uninstall works once the driver cannot load

The route that gets there costs no crash at all:

1. Fresh install of `D:\PKGNEW` (the candidate) from the wizard. A fresh
   install completes its registry phase, unlike B2's upgrade, so
   `Services\USB\DisableSelectiveSuspend` is written by `[Xhci.Dev]` ->
   `[Xhci.AddReg.Global]` with no right-click Install needed. Confirmed on the
   next boot: `DriverEntry (built Aug 13 2026 17:02:15)`, zero
   `cb SuspendController`, `suspends=0` at 31 health polls.
2. `ren c:\windows\system32\drivers\xhci98.sys xhci98.sav` from an MS-DOS
   Prompt, then reboot. The devnode comes up with a yellow bang and no root
   hub, and the debug console stays at 0 bytes for 105 s: nothing loaded.
3. Rename the file back inside Windows (it does not load, since binding
   already failed this boot; do not press Refresh), then Device Manager ->
   Remove.
4. It completes with no bugcheck and the whole `Universal Serial Bus
   controllers` class node disappears (`b4v2-03-uninstall-completed.png`).

Take the footprint before rebooting. A reboot lets PnP re-detect
`PCI\CC_0C0330` and possibly reinstall from a cached INF, which would
contaminate the rows being read.

| Footprint row | Expected above | Measured |
|---|---|---|
| `xhci98.sys` | gone | PRESENT: the uninstall does not delete the driver file |
| driver key `Services\Class\USB\0002` + its `HKR` values | gone | gone (`0000`/`0001`/`0003` remain) |
| `usbd.sys` | survives | survives (`NO_OVERWRITE`, not this package's to remove) |
| `Services\USB\DisableSelectiveSuspend` | the open question | SURVIVES, `= 1` |
| `xhci98.tmp` | measure it | absent, consistent with upgrade-only residue |
| the engine's cached INF | measure it | SURVIVES: `C:\WINDOWS\INF\OTHER\XHCI98~1.INF`, 17,428 bytes before and after |

The cached-INF row took four readings to get right. A device install does
cache our INF, at `C:\WINDOWS\INF\OTHER\XHCI98~1.INF`: 17,428 bytes, i.e.
`xhci98.inf` byte-for-byte, long name `xhci98 Project.xhci98.inf`, so the
engine names it `<Provider>.<inf>`. It was found only after `dir /a
c:\windows\inf | find "08-13-26"` showed `OTHER` as a directory dated that day.
The earlier readings used `dir /a c:\windows\inf\xhci*.*`, which answered
`File not found` both after the uninstall and after a successful reinstall; a
wildcard in a directory does not search its subdirectories, so that instrument
could never have shown the positive.

What settled it was taking the "before" on the same boot as the Remove. A
second uninstall cycle was run for that: rename-away -> reboot (trace 0 bytes
for 105 s, nothing loaded) -> rename-back -> read both paths -> Remove -> read
both paths again, with no reboot and no Refresh in between.

| | before | after |
|---|---|---|
| `C:\WINDOWS\INF\OTHER\XHCI98~1.INF` | 17,428 | 17,428, survives |
| `C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS` | 139,995 | 139,995, survives |

So on Windows 98 the uninstall is registry-only: it takes the devnode and its
driver key, and leaves every file and the machine-wide value (`xhci98.sys`,
`usbd.sys`, the engine's cached INF and
`Services\USB\DisableSelectiveSuspend`). Free space rose by about 4.19 MB
across the Remove, which is the driver database being rebuilt rather than any
of these files going.

One caveat: the `xhci98.sys` row here was read on a file that had been renamed
out and back, so it cannot be fully excluded that the engine stopped regarding
the file as its own. The Windows 2000 leg below needs no rename trick and is
where that row gets its clean confirmation.

B4's last step passes: the reinstall from `D:\PKGNEW` raised the wizard,
completed, and the machine came back working (`DriverEntry (built Aug 13 2026
17:02:15)`, zero `cb SuspendController`, `suspends=0` at 31 health polls). The
wizard appearing at all corroborates that the uninstall committed: after the
failed Remove earlier the same day, no wizard appeared, because the machine
still knew the driver.

`Services\USB\DisableSelectiveSuspend` surviving is the answer this stage owed.
`docs/using/release-notes.md` (the `DisableSelectiveSuspend` entry) states it as measured rather than
derived, and its `OWED - task 11-V.3` row is discharged.

#### Run, 2b

Disable/re-enable and uninstall both pass, with no crash and no rename trick.
The disable/enable, the clause 2a can never run, gave a clean `Stop`/`Start`
pair and a driver unload/reload under Verifier armed with special pool, force
IRQL, pool tracking and I/O verification, every failure counter `00000000`.
Session totals: `StartController` 5, `StopController` 4, `DisableInterrupts`
5, `SuspendController` 1.

Rollback is not exercisable on this target, for a recorded reason: B2's
upgrade was declined, so there is no newer driver to roll back from. And task
11-V.3's actual clause, rollback after a failed start, still has no package
staged.

| Footprint row | After uninstall on 2b |
|---|---|
| `xhci98.sys` | survives, 138,939 |
| `usbd.sys` | survives, 20,688 (correct, `keep`) |
| driver key `Class\{36FC9E60-...}\0002` | gone, and `0003` with it (the root hub's key, created underneath it) |
| the `xhci98` service | SURVIVES, fully intact: `DisplayName`, `ImagePath system32\DRIVERS\xhci98.sys`, `Type 1`, `Start 3`, `ErrorControl 1`, `Group Base`, plus `Enum` and `Security` |
| engine's cached INF `%SystemRoot%\inf\oem0.inf` | survives, 13,169, i.e. PKGOLD's `xhci98.inf` byte-for-byte |
| `Services\USB\DisableSelectiveSuspend` | the whole `Services\USB` key does not exist |

The service row is the one the sheet said to measure rather than assume, and
the answer is that `SPSVCINST_ASSOCSERVICE` does not take it away. After
uninstalling, this target keeps a demand-start kernel service pointing at a
driver file that is also still on disk. 2b's leftovers are a superset of 2a's:
files, the cached INF, and a service.

Two things this closes. First, 2a's caveat: `xhci98.sys` survived there on a
file that had been renamed out and back, and here nothing was renamed, so "an
uninstall does not delete the driver binary" is a property of the packaging on
both targets. Second, the NT-path exclusion of task 11-V.6's fix is now
measured on the machine rather than derived from the INF: no `Services\USB`
key exists, and the same devnode's `InfSection` = `Xhci.Dev` / `InfSectionExt`
= `.NTx86` shows the decorated path is what ran.

### B5. The `DriverVer` discriminating experiment

On Windows 2000, from a freshly uninstalled devnode, install
`out\media-11v\dated2003-1.0.0.3-debug`: byte-identical to the upgrade
candidate except that `DriverVer`'s date reads `02/15/2003`, which is the date
Microsoft's own EHCI entry displays correctly on the same machine.

Both this variant and our current package are zero-padded, and that is what
makes the experiment readable at all: it is a one-variable comparison against
our own `08/13/2026`, not against Microsoft's `2/15/2003`.

| Result | Means |
|---|---|
| Driver Date now shows `15-Feb-03` | padding and signing are both exonerated (a zero-padded, unsigned package has displayed a date) and the changed date value is what made the difference |
| Driver Date still `Not available` | the date value is exonerated (the variant matches Microsoft's control on it), and two differences remain confounded: signing and padding. This outcome does not isolate signing and must not be written up as if it did |

Neither outcome says anything about how Setup ranks. This experiment observes
what the Driver tab renders, and a date the UI does not show may still have
been parsed and used as a ranking key. The only observation in this stage that
says whether ranking worked is B2, the upgrade being accepted rather than
declined as not-better. Read B5 as an explanation of B2's result, never as a
substitute for it.

Padding cannot be controlled without bypassing a gate, so it survives the
second outcome. Microsoft's `[EHCI.NT]` carries the unpadded `2/15/2003`,
and this repository's own INF gate rule (`^\d{2}/\d{2}/\d{4}`) refuses that
form, so an unpadded variant is not something `make-11v-media.ps1` can
produce. Separating signing from padding needs the gate taught to allow an
unpadded `DriverVer` for a test package, and that is a decision to take rather
than a run to improvise.

Either way, `docs/using/release-notes.md` (the Windows 2000 upgrade entry) and its `OWED` row are
what this replaces.

#### Run, 2b

B5 lands on the second outcome: the date value is exonerated, and signing and
padding stay confounded. `PKG2003` installed (Driver tab showing the
candidate's version, trace `built Aug 13 2026 17:02:15`), and Driver Date
still reads `Not available` with no `DriverDate` value in the driver key at
all, the same absence B3 measured for `08/13/2026`. So a `02/15/2003` date,
the exact value Microsoft's own EHCI entry renders correctly on this machine,
is neither stored nor displayed by our package. `Digital Signer: Not digitally
signed` and our zero-padded `02/15/2003` against Microsoft's unpadded
`2/15/2003` both remain.

Getting the variant installed at all took five attempts, and the reason
outranks B5's own result. `c:\winnt\inf\oem0.inf`, the cached baseline INF
that survived B4's uninstall, beat our package on every route: search with
`Specify a location` set to the package, Have Disk with `Show compatible
hardware`, Have Disk with `Show all hardware of this device class`, and a copy
of the package on the local disk. All four answered either *"a suitable driver
is already installed"* or *"The specified location does not contain
information about your hardware"*. Deleting `oem0.inf` and `oem0.pnf` made
Setup pick our package instantly and call it "a closer match".

So the remedy for B2's decline is `del %SystemRoot%\inf\oemN.inf` plus the
`.pnf`, and `docs/using/release-notes.md` carries it beside "the package cannot
upgrade itself on Windows 2000". Two cautions: the engine re-creates `oem0.inf`
for the newly installed INF (the driver key's `InfPath` reads it again), and
with the incumbent's INF deleted there is nothing left for Setup to rank
against, so "closer match" here is not a ranking observation and does not
soften B2.

A hypothesis this run raised and refuted by control: that the 17,428-byte
candidate INF fails to parse on Windows 2000, which would have made B2's
decline a packaging defect. PKGOLD's 13,169-byte INF, demonstrably installable
since `oem0.inf` was a copy of it, produced the identical "does not contain
information about your hardware" message through Have Disk. The message is
about the route, not the file.

### B6. Install the release package on a clean snapshot of each target

This is the stage's last step for a reason. B1 to B5 install only the debug
media, and task 11-V.3's clause is "the distributable package on clean
snapshots of both targets": the release build is what an end user receives,
and it differs from the debug one in size and in import profile, which is the
kind of difference that fails a load rather than a test. Fresh install of
`out\media-11v\new-1.0.0.3-release` on 2a and on 2b, checking the same B1
rows.

The release build has no trace channel at all, and every debug macro compiles
out of it. There is no `DriverEntry (built ...)` line, no counter block, and
nothing for the live-guest reader to attach to. So:

- Verify this install from Device Manager and the file version, not from a
  trace. No bang, the right `DriverDesc`, and *Driver File Details* showing the
  candidate's version. An absent trace here is the expected reading.
- Restore the debug media before leaving stage B. Stage C and everything after
  it read counters, and a guest left on the release build cannot produce one.
  Reinstall `out\media-11v\new-1.0.0.3-debug` as the last action of this
  stage, and confirm the `DriverEntry (built ...)` line comes back.

Budget three boots for B6: release, release, and the restore.

#### Run, 2a

Passes on the release build, on an image regenerated from `post-nusb`
immediately beforehand. B4 had just proved that necessary: a Windows 98
uninstall leaves the files behind, so the previous image was no longer a clean
snapshot.

| Observation | Reading |
|---|---|
| Device Manager | `USB 2.0 eXtensible Host Controller (xhci98)`, no bang, `USB 2.0 Root Hub` beneath it, "This device is working properly" |
| Driver File Details | `File version` reading the candidate's version. Both flavours carry the same string, so this confirms the package version, not which build is installed |
| `xhci98.sys` | 75,003, the release binary. This is the only reading that discriminates the flavour, since every Device Manager string is identical between the two |
| `usbd.sys` | 18,912, dated 04-23-99: the `usbd98` build placed by our own INF on an image that had none, still carrying Win98 SE's own date |
| Trace | 0 bytes, and that is the pass condition |

The load path is proved by the root hub, not by any string: a root hub exists
only if the release binary registered with usbport, started the controller and
reported its ports.

`xhci98.tmp` is listed in Driver File Details and is not on disk (`dir /a
...\xhci98.*` -> `1 file(s)`, `XHCI98.SYS` alone), on a fresh install onto an
image that had no `xhci98.sys` to be in use. So that list is derived from the
INF's `CopyFiles` entry, not from the filesystem: it names the third field
whether or not the engine ever needed it. B1's row stands (`.tmp` is upgrade
residue), and the listing is not evidence for or against a file being there.

The Driver tab Date read `8-13-2026` here and must not be cited: the free
binary's UTC mtime and the INF's `DriverVer` date agree, so this install
cannot tell them apart. B4's uninstall cycle already separated them (the newer
binary displaying `8-12-2026`).

The restore cost no boot of its own and no crash. The devnode and registry
were already correct for the candidate; only the build flavour differed. So
the debug binary went back with one command in the same session, `copy
d:\pkgnew\xhci98.sys c:\windows\system32\drivers\xhci98.sys`, overwriting the
loaded `.sys` in place (Phase 3 task 8). Running the INF again would have been
an update over an existing install, which bugchecks this target. Confirmed on
the next boot: `DriverEntry (built Aug 13 2026 17:02:15)`, `suspends=0`, zero
`cb SuspendController` at 31 health polls. B6's three-boot budget is two when
the restore is a file swap rather than a reinstall.

#### Run, 2b

Passes identically, same day: release binary 75,003 installed on a freshly
regenerated `phase2b-clean`, no bang, `USB 2.0 Root Hub` present, Driver tab
showing the candidate's version (Date `Not available`, limitation 4, expected
here), trace 0 bytes. Restored with the same one-command file swap. On this
target that is the only sane route, since B5 established a reinstall would
meet the `oem0.inf` ranking wall for no benefit. `DriverEntry (built Aug 13
2026 17:02:15)` returned on the next boot.

### Stage B result

Complete on both targets. 2a: B1, B1a, B2, B2a, B3, B4, B6, B7. 2b: B1, B2,
B3, B4, B5, B6. Task 11-V.3 is discharged apart from two things named rather
than ticked, both of which have Phase 12 owners: rollback after a failed start
(no package is staged, and building one is host-side work) is task 12.3, and
why Windows 2000 rejects our `DriverVer` date (signing vs zero-padding, the
date value ruled out by B5) is task 12.4. Both need an artifact one of this
project's own gates refuses to build, which is what makes each a decision
rather than another run.

### B7. Re-run the import allowlist on both flavours

Record the pair count. Done host-side in batch 11-A at 15 pairs. This is a
host-side gate re-run against the binaries the media carries; B6 is what puts
the release one on a target. It needs no boot and can be done before the stage
or after it.

---

## Stage C - task 11-V.7, the log flush

Runs on stage B's installed machines, per target.

This stage ran and is closed; task 11-V.9 has since changed the switch it
turns on. `XhciLogEnable` is retired, replaced by `XhciLogFile` and
`XhciLogDebugView`, and `xhciLogReadSwitch` is now `xhciLogReadValues`. The
shape of the procedure still holds. Stage H is this stage re-run against the
two values, and it is where the current procedure is written out. The record
below names the values as they were at the time.

The switch is read at start and the log is written at stop, so one transition
cannot do both. `xhciLogReadSwitch` runs inside `StartController`; the flush
runs inside `StopController`. Edit the registry and then stop the driver, and
the stop flushes a log that is still switched off; the following start turns
it on and there is nothing left to write it out. The sequence needs two
transitions after the edit, in this order: a start to pick the switch up, some
bus activity to put records in the ring, and then a stop to write them.

The only routine flush is `StopController`'s. Windows 98 idle-suspends this
controller dozens of times in one idle run, and flushing on suspend would be a
file write every half second forever. There is one other site and it is not a
way to get a log: the fail-closed DMA teardown flushes before it bugchecks. If
that one fires, the run has a bigger finding than the log.

The sequence, per target:

| | Windows 2000 (2b) | Windows 98 (2a) |
|---|---|---|
| 1 | Set `XhciLogEnable` = `1` in the driver key recorded in B3 | the same |
| 2 | Start: enable the device (after a disable), or reboot | Reboot. Do not disable; it bugchecks (stage B4) |
| 3 | Confirm `log enabled` reads 1 now, before going further | the same |
| 4 | Plug and use a device, so the ring has records | the same |
| 5 | Stop: disable the device, or shut down | Shut down |
| 6 | Boot again and read `C:\XHCI.LOG` | the same |

Do not reach step 2 by reinstalling on Windows 98. A reinstall re-runs the
INF's `AddReg`, which writes `XhciLogEnable` back to `0`, so the reinstall
would undo step 1 and the run would read a switch it had just cleared. Reboot
is the only start on that target that leaves the edit standing.

Read these at step 4, while the driver is running. They are the only ones a
counter read can get.

| Counter (the human label in `offsets.labels.txt`; the reader accepts either that or the field name in `offsets.txt`) | Reading |
|---|---|
| `log switch read` / `log switch status` | the switch was actually read, and with what status |
| `log enabled` | 1 |
| `log records appended` | climbing |
| `log records suppressed`, `log records truncated`, `log bytes dropped by the wrap` | 0, 0, 0 on a short run; a nonzero wrap is a sizing fact rather than a fault |

The flush's own counters cannot be read after the flush. `log flushes`, `log
file create status`, `log file write status` and `log flush write failures`
are all written by the flush, which happens inside the stop, and usbport
zeroes the whole miniport extension before every `StartController`, so the
next boot or re-enable erases them. The file cannot supply them either: the
flush appends the counter block to the ring before it begins writing, so the
values it is about to set are by construction not in what it writes.

So the flush's outcome is observable as the file, and in the trace. The four
exits of `xhciLogFlush` each write a bounded debug-build line, which is what
separates the causes of an absent file:

| Trace line | What it means |
|---|---|
| `log: flush skipped - switch off` | the switch was not on at the stop. Its value is the flush reason. |
| `log: flush refused - above PASSIVE_LEVEL` | the platform reached the flush site above PASSIVE. This is task 11-V.7's stop rule, as a line rather than as a counter that the next start erases. |
| `log: flush declined - nothing to write` | the ring was empty. (Structurally unreachable, see the run below.) |
| `log: flush wrote bytes` | the drain ran; the value is what reached the writer. A `log: flush create status` / `log: flush write status` pair follows it only when the write failed, and those are the two NTSTATUS values to quote. |

The trace is host-side and survives the guest, so on Windows 98, where the
stop is the shutdown and there is no counter window at all, this is the
channel. It is debug-build only, like every other trace site, so stage B6's
release-build leg cannot see it.

Delete or rename any existing `C:\XHCI.LOG` before step 5, or the file proves
nothing about this flush.

| Result | Means |
|---|---|
| the file ends with `flush.truncated=...` | the flush ran and wrote all of it. This is the pass. The ring appends `flush.reason`, `flush.dropped` and `flush.truncated` last, immediately before the drain, so that record is by construction the final one a complete flush writes. |
| the file exists, ends at a clean record boundary, but not that record | a partial flush. The drain is many independent 256-byte writes, so a later one can fail after earlier ones succeeded, and the last successful chunk can land on a record boundary by coincidence. "Ends in a complete record" is therefore not a pass condition. |
| the file exists but is empty or truncated mid-record | a partial flush whose failure happened to be visible. Same verdict as the row above. |
| no file | the flush did not write. Which of the three reasons (switch off at the stop, refused above `PASSIVE_LEVEL`, or the create failed) cannot be told from the guest afterwards; the trace lines above can. |

On Windows 2000 there is one narrow window worth trying: after the disable and
before the re-enable, the extension has not yet been re-zeroed, so a counter
read through the monitor may still reach it. Try it; do not build the stage on
it, and record whether it worked. On Windows 98 there is no counter window at
all, since the stop is the shutdown.

Task 11-V.7's stop rule was written as `log flushes refused - above
PASSIVE_LEVEL` nonzero with `log flushes` zero. On Windows 2000 take that
reading in the window above if it works. Where the counters are gone (every
Windows 98 stop, and a Windows 2000 window that closed) the trace line `log:
flush refused - above PASSIVE_LEVEL` is the same fact from the same decision,
and it is the line to quote. What the trace cannot do is count: it says the
refusal happened, not how many times, since the site is budgeted. An absent
file with none of the four lines present is a fifth outcome and means the
flush was never reached at all; check `log enabled` first.

Then confirm `C:\XHCI.LOG` exists and is readable. Getting it out of the guest
is the ordinary transfer path; on Windows 98 the raw image is the fallback
that has worked before.

Record for `docs/using/release-notes.md`: the literal key path (B3), whether
the file appeared on each target, and what one flush's worth of it actually
contains. The release notes make a claim about what the log is worth, and this
is the only stage that can check that claim.

### Run

Both targets, one binary: debug build, built `Aug 13 2026 17:02:15`,
`MiniPortExtensionSize=0001241C` = 74,780 on both, equal to the `SIZEOF` in
both offset tables. The stage passes on Windows 2000, and the feature cannot
be produced at all on Windows 98, the target it was built for. Evidence in
`vm\11v-stageC\`.

#### Windows 2000 (2b): pass, by the disable route

| Step | Reading |
|---|---|
| before the edit | `log switch read` 1, `log switch status` 0, `log enabled` 0, `log records suppressed` 2: B3's default confirmed at runtime, and the two suppressed records are the "costs nothing when off" property measured rather than argued |
| after the edit + re-enable | `log enabled` 1, `log records appended` 2, `suppressed` 0: the same two records, landing instead of being dropped |
| the flushing disable | `log: flush wrote bytes=000002B2` (690), and no `create status` / `write status` lines, which the code emits only on failure |
| `C:\XHCI.LOG` | 690 bytes, equal to the traced count, ending in `flush.truncated=00000000`, the pass condition above |

The file's contents agree with the counters read live through the monitor
minutes earlier (`cmd.issued` 6, `opens.total` 6, `slots.enabled` 1,
`devices.addressed` 1, every failure counter 0; `xfer.submitted` =
`completed` = `1CD` against a live read of 455 before the stop, the difference
being the teardown's own). `log.appends=19` is internally consistent too: 2
start notes plus the 23 rows that precede it in the block, counted as it was
itself appended. So the release notes' claim about what the log is worth is
honest on this target: the block names the device count, the transfer totals
and every refusal class.

The between-disable-and-enable counter window is refuted as a channel. The
first read (after the switched-off disable) returned values identical to the
pre-disable state and looked like a working window. The second, after the
flushing disable, returned `Log.Appends` = `Log.BytesDropped` = `Log.Flushes` =
`0x77239CD0`, one pointer-shaped value in three counters, with `log switch
read` = 65536 where the code writes only 0 or 1. usbport frees the extension at
removal and the pool is reused. The first read was therefore
freed-but-untouched memory and is indistinguishable from a live one.

That is why the discriminator was chosen in advance: `log flushes` is written
by the stop, so a correct reading there would have proved the read reached
memory the stop wrote. It did not. 11-V.7's stop rule has no route to a number
on either target; the trace line is the channel.

The stop rule is measured not applicable rather than fired. Every
`cb StopController` on both targets arrives at `irql=00`, so the flush's IRQL
refusal never fires here and `log: flush refused - above PASSIVE_LEVEL` was
never seen. That is the good outcome; it is not a pass of the refusal branch.

`log: flush declined - nothing to write` is structurally unreachable and no
run will witness it. `XhciLogFlushBegin` returns `EMPTY` only on `Used == 0`,
and its caller appends the whole counter block (26 rows when this was run, 33
today) under the lock immediately before calling it (`src/xhci_dispatch.c`).
With the switch on, the ring is never empty at that test. Read the
four-outcome table above as three reachable outcomes and one that cannot
occur.

#### Windows 98 (2a): the flush fails at the create, and there is no other door

`cb StopController irql=00` then:

```
log: flush wrote bytes=00000000
log: flush create status=C000003A     <- STATUS_OBJECT_PATH_NOT_FOUND
log: flush write status=00000000      <- never attempted
```

`dir c:\xhci.log` on the next boot reads `File not found`, so the trace and
the guest agree. `C000003A` is read out of the DDK's own `ntstatus.h` (`{Path
Not Found} The path %hs does not exist.`).

Two causes were possible at this point: (a) shutdown ordering, the volume
already gone when the stop runs, which is demonstrably what happens on 2b,
where the shutdown flush creates the file and then loses the write to
`C0000189` `STATUS_TOO_LATE` ("a write operation was attempted to a volume
after it was dismounted"); or (b) the path form `\??\C:\XHCI.LOG` does not
resolve under Windows 98's `ntkern` at all, in which case the log has been
uncreatable there since it was written and the shutdown is a red herring. The
probe boots below settled it as (b).

Both attempts to reach `StopController` on Windows 98 outside a shutdown
failed, and each is worth carrying:

- `device_del` of the xHCI PCI function is inert on Windows 98. `info qtree`
  still lists `dev: qemu-xhci` afterwards and the driver keeps polling: a PCI
  hot-eject needs the guest to acknowledge an ACPI request and Win98 does not.
  Stage D's "orderly `device_del` of one instance" route does not exist on
  this target.
- A Device Manager disable bugchecks above this miniport. The trace ends at
  `RH: port disown` / `RH ports disabled=00000004` / `cb RH_DisableIrq` and
  `StopController` is never called, with the guest dead at `0028:C00312EE`,
  stage B's address. So Windows 98's shutdown is not merely the usual stop, it
  is the only stop that reaches this driver, which is the same fact task
  11-V.1 records from the other side.

The vehicle finding that cost the first boot: `vm\win98-11v-clean.img` was
found pristine, not installed. No `xhci98.sys` in `SYSTEM32\DRIVERS`, no
`INF\OTHER` directory, the xHCI sitting as `PCI Universal Serial Bus` under
*Other devices*, although the handoff recorded both clean images as installed
with the debug build. It is not the uninstalled state either, which stage B
measured as leaving `xhci98.sys` and the OEM INF behind. The cause is not
recoverable from what survives.

The lesson belongs to every remaining stage: confirm the vehicle's state with
one cheap reading (a `DriverEntry` line, or the devnode's presence) before
planning around a handoff's claim about it.

Recovery was a fresh install of `D:\PKGNEW`, which also re-exercised B1's
clause: `[Xhci.Dev]` carries `AddReg=Xhci.AddReg,Xhci.AddReg.Global`, so a
fresh install delivers task 11-V.6's global value with no separate right-click
`Install`. B2a's extra step is an upgrade remedy only.

Incidental, and the first runtime confirmation of it on this target:
`UsbPortGetMiniportRegistryKeyValue` returns `MP_STATUS_SUCCESS` on Windows 98
/ NUSB (`log switch read` 1, `log switch status` 0, and the value it returned
was acted on). Batch 11-A derived that ABI statically from four binaries; this
is the Win98 half answering a real call.

Verdict for the release notes: the log is a Windows 2000 diagnostic as it
stands. Its Windows 98 route, "shut down, then read `C:\XHCI.LOG`", does not
work. Since the whole
justification for the feature is that DebugView plus any device plug
bugchecks Windows 98 on real metal, Phase 12 task 12.2's counter channel is
unmet, and task 11-V.7's stop rule is live rather than hypothetical.

The experiment that decides (a) vs (b) is also the fix. A retry at the flush
cannot decide anything, because it runs at the one moment both causes predict
failure, and it would be three creates per flush rather than one, the writer
being a loop over 256-byte chunks. What shipped instead is a read-only probe at
`StartController`: PASSIVE, and with the file system demonstrably up, which is
what the stop lacks.

Each form is opened with `FILE_OPEN` (open existing, never create), which makes
the two outcomes different status codes: `STATUS_OBJECT_NAME_NOT_FOUND`
(`0xC0000034`) means the form resolves and only the file is absent, while
`0xC000003A` means it does not. The first resolving form is what the flush then
uses; if no form resolves, the flush uses the form that shipped, so this can
improve a target and cannot regress one. It creates nothing, so it also cannot
forge the "was there a file" reading.

#### Windows 98 (2a), boots 5 to 8: the probe answers (b) and then finds something worse than an absent log

Each boot swapped the probe binary in at `D:\PATHFIX\XHCI98.SYS` from the
`xfer98` volume rather than reinstalling, since a reinstall resets
`XhciLogEnable` to 0.

| Boot | Binary | What it added |
|---|---|---|
| 5 | `built Aug 14 2026 00:08:34`, ext `0001242C` = 74,796 | two forms, read mask: `\??\C:\XHCI.LOG` -> `C000003A`, plain `C:\XHCI.LOG` -> `C000003B` |
| 6 | the same binary | the same two readings, on a second boot: the reproduction |
| 7 | `built Aug 14 2026 00:20:27`, ext `00012430` = 74,800 | three roots, read mask: `\??\` -> `C000003A`, `\DosDevices\` -> `C00000BB`, `\SystemRoot\` -> `C00000BB` |
| 8 | `built Aug 14 2026 00:30:28`, ext `0001243C` = 74,812 | the write mask. `\??\` -> `C000003A` again, then `\DosDevices\` opened for write never returned |

(b) is the answer, and it is settled by the boot rather than by the stop.
`\??\` answers `STATUS_OBJECT_PATH_NOT_FOUND` at `StartController` (machine
up, file system mounted, PASSIVE), so the form does not resolve on Windows 98
at any time, and shutdown ordering is exonerated on that target. The plain
`C:\XHCI.LOG` tried beside it answered `STATUS_OBJECT_PATH_SYNTAX_BAD`
(`C000003B`), which says `ntkern` is parsing NT object-namespace syntax and
rejects a drive-relative path outright: the answer is a different root, not a
non-NT path style, and the plain form left the table by that measurement.

The roots to try came out of `ntkern.vxd`, not out of a guess: `\??` does not
appear in that binary in either encoding, while `\DosDevices`, `\DosDevices\`,
`\SystemRoot\` and `\Device` all do, which is the shape that produces "the
path does not exist" for a `\??\` path.

The measured matrix. Read the empty cells as empty. `C00000BB` is
`STATUS_NOT_SUPPORTED`.

| Form | read mask (`FILE_READ_ATTRIBUTES`) | write mask (`FILE_APPEND_DATA`) |
|---|---|---|
| `\??\C:\XHCI.LOG`, what shipped | `C000003A` no such root | `C000003A` no such root |
| `C:\XHCI.LOG`, drive-relative | `C000003B` syntax rejected | never tried; dropped from the table by the read |
| `\DosDevices\C:\XHCI.LOG` | `C00000BB` not supported | never returned; hung the boot |
| `\SystemRoot\XHCI.LOG` | `C00000BB` not supported | never tried, and now unreachable by design (see the gate below) |

The hang is the finding that outranks the log, and it was caught before it
shipped. Boot 8's trace ends mid-probe on `log: path probe DosDevices
form=C00000BB` with no line after it (`c-2a-boot8-writemask-hang.log`, 25
lines); the guest sat on the splash screen with the CPU live and the boot
blocked inside `StartController`, which is on the boot path
(`c-2a-boot8-hang.png`). Boot 7 differed only in using the read mask and
booted through all three forms to a working desktop and its own shutdown
flush, so the attribution is tight: it is the write request that does not
return, not the form.

So the probe now asks for the write mask only where the read mask resolved,
and that gate costs no reachable measurement: a path that will not answer a
request for attributes has nothing to offer a request for write access. It is
also why `\SystemRoot\`'s write cell is unreachable rather than merely
unmeasured, since its read answered `C00000BB`. A diagnostic switch that can
leave a user unable to boot is worse than an absent diagnostic, and that is a
shipping decision taken on this measurement.

Task 11-V.7's stop rule therefore fires, on the branch that publishes a
limitation rather than the branch that keeps hunting: no in-guest log file on
Windows 98, capture with DebugView on Windows 2000, and the Windows 2000 sink
retained as the 2b leg measured it. Phase 12 task 12.2 inherits the Windows
98 trace-channel question with this matrix behind it.

#### Stage C result

Complete on both targets. 2b: the disable route, pass, a 690-byte complete
file. 2a: eight boots ending in a published limitation. Evidence
`vm\11v-stageC\`, with boot 7's log preserved there as
`c-2a-boot7-probe-3forms-readmask.log`; it had been left in the launcher's
rolling `vm\win98-11vc-debugcon.previous.log`, one stage's worth of rotation
away from being lost, and it is the only witness of the third root.

---

## Stage D - task 11-V.2, more than one controller and more than one device

The global-state audit half is done (batch 11-A: three writable process
globals in the release build, `.data` all zero). What is left is the run.

- Two xHCI controllers, and QEMU does give them. Falsified against a guestless
  paused QEMU (11.0.92) rather than by spending a boot: both instantiate, at
  00:04.0 and 00:05.0 with distinct ids, and a `device_add usb-kbd,bus=xhci2.0`
  attaches to the second. So the open question is whether the guest binds
  both, not the vehicle. Keep the habit for anything else this stage adds: a
  `-device` that does not instantiate is much cheaper to find without a guest.
- The known limitation this stage will notice first: the debug build's trace
  budgets are per macro expansion, therefore per driver image rather than per
  controller, so on a two-controller machine the two share one budget at each
  site. A count from that run is a floor for the pair, not a total for either.
  Diagnostic only, already documented at the macros.
- Recovery after one controller fails without corrupting the other is the
  clause with teeth, and this sheet has no way to make one controller fail.
  QEMU's emulated devices produce no error completion codes at all (about
  19,000 transfer events across four target runs, zero), and there is no
  monitor command that faults a controller. What is reachable is a teardown of
  one instance while the other runs: `device_del` the first xHCI, or disable
  it in Device Manager on 2b (never on 2a, see stage B4), and read
  `DevicesInvalidated`, `SlotsDisabled` and the command-failure block on the
  surviving controller. Record that as an orderly removal, not a fault, and
  name the injected-failure half as not reachable in this vehicle. Task 11-V.2
  carries this as a named vehicle limitation, so the clause has a legal
  closure.
- Scratchpad-limit refusal has no trigger either: no configurable QEMU xHCI
  asks for more scratchpad buffers than the declared cap. Same treatment, same
  place.
- Many simultaneous devices and endpoint types: HID + storage + Ethernet at
  once, and the endpoint refusal block (`endpoint refusals - *`, `endpoints
  refused - no resources`, `ring pool refusals - *`) is where a resource
  ceiling reports itself.

`MaxSlotsEn`, the scratchpad count, `InitStep` and `InitStatus` have
`XHCI_DBG_VALUE_CHANGED` print sites in `xhciCheckController` (`init step`,
`init status`, `MaxSlotsEn`, `scratchpad buffers`), so they are readable both
as counters and from the init trace lines. Read them the way the clause
needs: `MaxSlotsEn` is the ceiling the refusal must arrive at, and the refusal
is still the reading. A slot-exhaustion clause closed on "MaxSlotsEn was 32"
and three devices is the `+0` error again.

### Run, 2b (two boots)

The Windows 2000 leg passes every clause this vehicle can reach, and the
slot-exhaustion clause is exercised rather than recorded unexercised.
Evidence in `vm\11v-stageD\`, which carries its own `README.txt` naming the
binary, the QEMU line and every file. Binary: the debug build, built `Aug 14
2026 00:36:27`, `MiniPortExtensionSize=0001243C` (74,812) equal to the
`SIZEOF` in both offset tables.

The first boot went entirely on the vehicle. The working 2b image came up
running the Aug 11 binary, extension `000113D0` = 70,608, 4,204 bytes short of
what both offset tables describe. Every counter below would have been read
from the wrong offset and looked plausible. Swap in place from the vvfat
`CHECKED` directory, restart, and the second boot is the one that measures.

| Clause | Reading |
|---|---|
| The guest binds both controllers | one `DriverEntry`, two `cb StartController` with distinct bases (`8184292C`, `8182592C`). The second devnode was bound by the already-loaded image, from the cached `c:\winnt\inf\oem0.inf` through the Found New Hardware Wizard |
| Independent per-controller state | the two counter blocks advance separately from the first reading: `HealthPolls` 264 against 381 at the same instant, each equal to its own `CheckCallbacks`. Both report `MaxSlotsEn 32`, `InitStep 22`, `NoOpWitnessFired 1` |
| Multiple simultaneous devices and endpoint types | 32 live slots on one controller (1 HS keyboard, 4 hubs, 27 FS keyboards) with storage and audio on the other: control, interrupt, bulk and isochronous active at once. Audio gave `DescIsoEntries 2`, `iso intervals: 1 DERIVED`, zero refusals |
| Slot exhaustion at `MaxSlotsEn` | exercised. See below |
| Orderly teardown of one instance while the other runs | passes, and it is a teardown, not a failure. See below |
| Recovery after a controller fails | not reachable: no failure exists to inject, as task 11-V.2 names it. The disable below is not a substitute |
| Scratchpad-limit refusal | not reachable: `scratchpad buffers=00000000` on this controller; nothing asks for more than the cap |
| No mutable controller state in process globals | behavioural corroboration only, as planned; the measurement is batch 11-A's static audit |

Slot exhaustion, and how the bus was built. No root-port count reaches 32, so
the tree is three `usb-hub`s on root ports 2/3/4 of `xhci1`, a fourth nested
at `2.8`, and keyboards on every downstream port. A `usb-hub` creates no bus;
children attach by hierarchical `port=` (`port=3.1`, `port=2.8.4`), the Phase
10 fact that had already cost a matrix row. The count went 4 -> 12 -> 27 ->
32 = `MaxSlotsEn`, with `BehindHubAddressed` tracking it and route strings
`0x00001`..`0x00008` decoded correctly at tier 1.

Then the 33rd:

```
SlotsEnabled           32     (unchanged)      OpenRefusals            3
DevicesAddressed       32     (unchanged)      OpenRefusalsNoRecord    3
                                               BehindHubNoRecord       3
```

The refusal is the right one, and `src/xhci.h` says so in as many words:
`OpenRefusalsNoRecord` "separates the sub-case where every record was in use,
which is a controller running at its slot limit rather than a failure of the
association", and `BehindHubNoRecord` "is the record table full". Three
refusals is usbport retrying the open, not three devices. Windows shows it as
a Found New Hardware Wizard ending "the installation failed because a
function driver was not specified for this device instance"
(`d-2b-04-after33rd.png`); a slot ceiling reached is supposed to look like
one device not coming up.

And the ceiling releases, which is what makes it a limit rather than a wall.
Two devices deleted, one attached: `SlotsEnabled 33` / `SlotsDisabled 1` = 32
live, `DevicesAddressed 33`, and `OpenRefusals` did not move. The new device
was admitted the moment a record came free. So the reading is a live-slot
ceiling, not a lifetime count.

The teardown leg, and the thing in it that no single-controller run could
show. A Device Manager disable of `xhci2` while `xhci1` ran, then an enable.
Across the disable the survivor changed in nothing but its free-running
counters: `SlotsEnabled` 33, `SlotsDisabled` 1, `DevicesAddressed` 33,
`DevicesInvalidated` 0, `CommandFailures` 0, `OpenRefusals` 3,
`TransfersSubmitted`/`Completed` 2658/2538, every one identical before and
after; only `HealthPolls`/`CheckCallbacks` (still equal to each other) and
`InterruptCount` advanced.

The re-enable then produced `cb StartController a=8178C92C`, a new extension
base, with no new `DriverEntry`. Phase 3 task 9 measured that Windows 2000
unloads and reloads the image on a disable/enable cycle; that was with one
controller. With two, the surviving devnode holds the image loaded, so the
restart ran against driver-image globals that were never reinitialised: the
controller lock and the start epoch of Phase 4 task 7. That is the
configuration this stage exists for and it is only reachable here.

The restarted controller reached `InitStep 22` / `InitStatus 0`,
`NoOpWitnessFired 1` (so the command engine works on the restarted instance
under the sibling's still-live lock), re-enumerated both its devices
(`SlotsEnabled 2`, `OpensTotal` = `OpensAccepted` = 9), and ran 256 transfers
submitted = completed with zero command failures.

Two things this run measured that belong to later work:

- A 32-device population overflows the TT-pair probe table. `topology pairs:
  12 distinct, 204 observations, 132 dropped`, and the identity holds exactly
  (12 x 6 + 132 = 204), so the table is doing what it declares. But it filled
  with the first 12 distinct pairs and dropped the rest, which makes it a
  sample rather than a census on a bus this size. Anything reading TT pairs
  off real hardware must not assume the table saw everything.
- The per-image trace budget is real and it is the first thing this stage
  notices, as the launcher predicted. The refusal counters print `3 / 0 / 3 /
  0` alternating, because a value that differs between the two controllers
  makes every sample "changed" at a site whose budget is per macro expansion.
  Diagnostic only; the counters are per extension and unaffected.

Ethernet did not run and is not a vehicle limitation. This project has only
ever driven USB Ethernet by `usb-host` passthrough of a physical ASIX adapter,
and none was attached to the host on the day; QEMU's emulated `usb-net` is
RNDIS, which Windows 2000 has no in-box driver for and which no Phase 10
matrix row uses. Recorded as unavailable on this host at this time. Stage F's
sustained HID + storage + Ethernet load has the same dependency and should
not be planned around an emulated adapter.

The extension bases are not stable across a restart (`8182592C` became
`8178C92C` on the re-enable), so write both down before reading either. The
blocks are otherwise indistinguishable, which is the same trap stage C's
refuted counter window turned on.

### Run, 2a (three boots)

The Windows 98 leg passes every clause it can reach, and the slot ceiling
behaves identically on the NUSB `usbport.sys`. Same binary as the 2b leg
(debug build, `Aug 14 2026 00:36:27`, `MiniPortExtensionSize=0001243C`), so
the "both targets, one binary" property is intact.

- Both controllers bind here too, and with no wizard at all: one
  `DriverEntry`, two `cb StartController` (`C14668C4`, `C14858C4`). The 16-bit
  engine matched the second devnode from its own cached
  `C:\WINDOWS\INF\OTHER\XHCI98~1.INF` without asking, where Windows 2000
  needed the Found New Hardware Wizard for the same step.
- The working image was again running an old binary: the earlier build,
  extension `0001241C` = 74,780 against the tables' 74,812. Confirm the
  vehicle on this target too; it is the second time in two legs.
- 32 live slots, a 33rd attempted, and the refusal is the same one to the
  count: `OpenRefusals` 3, `OpenRefusalsNoRecord` 3, `BehindHubNoRecord` 3,
  with `SlotsEnabled 33` / `SlotsDisabled 1` = 32 live and unmoved. NUSB 5652
  and SP4 6681 each retry the refused open exactly three times and then stop.
  The ceiling is the driver's, and both usbport builds meet it the same way.
- The other controller was serving a device throughout: storage attached to
  `xhci2` while `xhci1` sat at its ceiling enumerated normally. 1 slot, 4
  opens all accepted, 53 transfers submitted = completed, zero refusals.
- Clean throughout: no command failures, no controller failures, and
  `SuspendCount` 0, which is task 11-V.6's `DisableSelectiveSuspend` fix still
  holding on this image. The only nonzero failure-shaped counters are the
  documented-benign `CommandStaleCallbacks` and `RhStaleTimers`, which
  ordinary successful completion produces.
- No teardown leg here, recorded rather than attempted. A Device Manager
  disable bugchecks this guest and `device_del` of the PCI function is inert
  on it (stage C measured both). The teardown clause is Windows 2000's and is
  discharged above.

The tree was built from hubs rather than devices, and the reason is a finding
in its own right. The first keyboard raised the Add New Hardware Wizard, which
then demanded `kbdhid.vxd` and `hidclass.sys` from Windows 98 SE media that
was not attached, and 30 leaf devices would have been 30 modal dialogs. A hub
is itself a device holding a slot and binds the already-installed hub driver,
so three hubs on the root ports, eight nested under each and four at a third
tier reach 32 with no class install at all. Route strings decoded correctly
to tier 3.

The class-install gap is a stage E/F prerequisite and was closed in passing.
The roadmap's run strategy needs "one persisted install pass per device class
on the working 2a image" to convert stages E and F to machine time. That pass
needs media, `w98se.iso` was on the host, and the leg was re-launched with it
attached (`scripts\local\qemu-win98-run-11vd-cd.cmd`). The CABs were then
copied to `C:\WINDOWS\OPTIONS\CABS`, so the image no longer needs the ISO for
the next class, and USB HID installed cleanly off them. A cancelled install is
remembered: after the first cancel, re-plugging raised no wizard at all; the
banged devnode had to be removed in Device Manager before the replug would
re-offer it.

One reading that must not be cited, the `+0` trap in another costume. Device
Manager ends this leg showing `Other devices -> Unknown Device`, which looks
like the refused 33rd device. It is not: the same entry is visible in
`d-2a-09-hubs.png`, taken when `OpenRefusals` still read 0. So on this target
the guest-side face of the refusal is not separable from a pre-existing
unknown devnode, and only 2b's contemporaneous wizard error is attributable.
Read the counters.

Both legs overflow the TT-pair probe table at this population (2a reads `12
distinct, 204 observations, 96 dropped` against 2b's 12/204/132), and the
hub-marking table stops at 8 folded descriptors with 19 hubs on the bus. Both
are bounded diagnostics behaving as declared; both are a sample rather than a
census on a bus this size, which is what anything reading TT pairs off real
silicon needs to know first.

Method note, paid for in this leg: `device_add` replies were piped to
`Out-Null` for the first hub batch, so a `port=2` collision with the keyboard
already sitting there failed silently and read as "one hub did not
enumerate". Never suppress the monitor's reply. `info usb` is the check, and
the script's own quiet is not evidence.

---

## Stage E - task 11-V.4, the compatibility subset

Direct root-port attach and re-attach at LS, FS and HS; single-TT and multi-TT
hub trees; mixed interrupt, bulk and isochronous devices active together;
multi-tier hub paths.

Three of those clauses cannot be met in this vehicle, and the stage is not
closable without saying which. They are not failures and they are not blanks;
each has a Phase 12 or Phase 13 owner.

| Clause | Why not here | Owner |
|---|---|---|
| Low Speed at a root port, through to Device Manager | no emulated QEMU device declares Low Speed (measured, not recalled) | a bare-metal run; the behavioural half closed on the E460, the trace half is published as not taken |
| Single-TT and multi-TT hub trees | QEMU's only hub is a Full-Speed 1.1 hub, so there is no real translator here at all; every multi-TT hub known to this project is the High-Speed half of a USB 3.0 hub unit | task 13-E.2 for the behavioural half; the numbered half has no vehicle and is published as a limitation |
| Isochronous traffic mixed with the rest, on 2a | Windows 98's own audio stack fails on the only isochronous device this vehicle has (stage F). The 2b leg runs; the 2a leg does not, and a physical device on real silicon is what would change that | Phase 13 |

What remains is runnable: FS and HS root-port attach and re-attach, multi-tier
hub paths through QEMU's Full-Speed hubs, mixed interrupt and bulk traffic on
both targets, and isochronous alongside them on 2b.

Most of that population has been driven before and the clauses read as before.
One item was not routine going in: `usb-bot` and `usb-uas` had failed at
`devices addressed` +0 on both targets in every earlier run, and this was the
one open item in Phase 11 that might have been a defect in this driver. The old
explanation, a shared `-drive` backend, was fixed and did not fix the rows.

The plan was a debugcon read on one boot, letting it separate the two halves:
`RH first decode of a speed` says a connect arrived at all; the `endpoint
refusals - *` block says enumeration saw it and refused. No decode line means
the change never reached us (the vehicle, or the root-hub path); a decode line
with a refusal names which refusal. A `+0` here is not a pass and must not be
closed as "a SCSI adapter Windows cannot bind": the counters already refute
that, since the failure is upstream of any bind, at addressing.

### Run

The `usb-bot`/`usb-uas` `+0` is settled, on the 2b leg, in one boot: it is a
defect in the harness's attach step and not in this driver. Both models set
`auto_attach = 0` at realize (a SCSI host adapter is meant to be presented to
the guest only once its LUN exists), so QEMU puts the device in the port and
never electrically attaches it. `qom-get /machine/peripheral/bot1 attached`
read `false` against the control's `true`, and the QEMU-side trace showed
`usb_xhci_port_link port 2, pls 5` (RxDetect, nothing there) three times and
never a connect.

No `PORTSC` change reached the driver, so there was nothing to refuse, so every
refusal counter read zero. One `qom-set ... attached true` per adapter
enumerated both immediately: `DevicesAddressed` 1 -> 3, `OpensTotal` 6 -> 14
all accepted, 573 submitted = 573 completed, zero refusals; detach and
re-attach clean after it.

The reason it survived three runs of being looked at is this sheet's own rule
turned on the instrument. The harness already had a guard for this ("an
attach is only an attach if the device arrived") and it asks `info usb`.
`info usb` lists a device that merely occupies a port, so the guard answered
"it arrived" about a device the controller could not see. Fixed in
`scripts\vm-matrix\run-matrix.ps1`: the on-bus check keeps its own
necessary-but-insufficient job, and `Confirm-DeviceAttached` reads the attach
state, repairs it only when it reads false, and reads it back.

A modal dialog blocks the PnP install queue on Windows 2000. The second-tier
keyboard at port `3.2.1` sat at `Device 0.0` (unaddressed, no refusal counter
moving) for over a minute behind a Found New Hardware Wizard and an "Unsafe
Removal of Device" box, and was addressed within seconds of both being
cleared. This is the 2a "the modal wizard freezes an untaught image" fact in a
milder form: an unaddressed device is unreadable while a dialog is up, not
negative. Clear the desktop before concluding anything about enumeration.

The 2a leg ran the same day and reproduces it to the counter: `attached`
`false` against the control's `true`, `DevicesAddressed` and `RhFirstDecodes`
unmoved, every refusal 0, `pls 5` and no connect; then 1 -> 3 addressed and
`OpensTotal` 4 -> 10 all accepted after the `qom-set`. Two-tier hub tree and a
root-port detach/re-attach both clean. So it is closed on both targets.

Three things the 2a leg corrects or adds for later stages:

- The storage class install is not owed on 2a: `usb-storage` raised no wizard
  and bound clean, because Phase 8 installed it. Stage F does not pay for it.
  HID still wizards per new device instance even so.
- `FS at a root port` in this vehicle is the 1.1 `usb-hub` at 12 Mb/s, not a
  HID: QEMU's `usb-kbd` carries high-speed descriptors and negotiates 480 Mb/s
  on a root port. Do not write "attach an FS HID to a root port" into a sheet.
- Read the transfer identity as steady-or-growing, never as nonzero. The 2a
  leg ended 358 submitted / 348 completed / 2 cancelled, a gap of 8 that was
  static across reads twelve seconds apart on an idle bus, because interrupt
  IN transfers sit parked on HID and hub status-change endpoints.

The counter readings both legs took, for the next sheet that has to read them.

2b positive control first: `RH first decode of a speed=00010103`, 346 submitted
= 346 completed. Mixed interrupt, bulk and isochronous traffic active at once:
`IsoSubmits` 516 carrying 5,160 packets, 5,160 answered, `IsoPacketErrorsTotal`
0, `IsoMissedServiceTotal` 0, `IsoRefusalsMalformed`/`IsoSubmitsWrongType` 0,
`SlotsEnabled` 9 / `DevicesAddressed` 9, `InterruptCount` 4,723 =
`InterruptsClaimed`, every refusal counter 0. `IsoCadenceMismatches` fired on
all 516 root-port submits and `IsoSubmitsWithFrameId` stayed 0; both are the
shapes batch 9-V measured (the High-Speed root-port report forces SIA;
`HCCPARAMS1` CFC = 0), not new readings. The single `IsoRingUnderruns` and
single `IsoEventsUnattributed` are one stream's stop. `OpensTotal` 6 -> 14 all
accepted and 573 = 573 after the `qom-set`.

2a: positive control 53 = 53; root-port detach/re-attach `SlotsEnabled` 9 /
`SlotsDisabled` 3, `DevicesAddressed` 9 / `DevicesTornDown` 3, `OpensTotal` 28
all accepted, `CommandFailures` 0. The operator's "very minor crackles" on 2b
is a debug-build artefact (batch 9-V: one VM exit per traced character), not a
reading.

Evidence and the worked order for both legs in `vm\11v-stageE\` with its own
`README.txt`; launchers `scripts\local\qemu-win{2k,98}-run-11ve.cmd`, whose
controller id is `xhci` so their monitor commands transfer verbatim to and
from the harness.

---

## Stage F - task 11-V.5, the stability matrix

The long stage, run last of the exercising stages because everything it
stresses should already be known-good.

- At least 20 unplug/replug cycles per representative class (HID, storage,
  Ethernet, and audio where it runs). The identity to check at the end is the
  one Phase 8 settled: `transfers submitted = transfers completed + transfers
  cancelled`, with the abort block read beside it. A submitted/completed gap
  is not a leak until the cancel and abort counters are read next to it.
- Sustained concurrent HID + storage + Ethernet load, plus audio.
- Audio is a Windows 2000 leg only, and that is measured rather than a choice.
  Task 11-V.5's own text says "plus audio if Phase 9 passed", and Phase 9's
  Windows 98 half is named rather than met: that OS's audio stack failed five
  of five in this vehicle, in a way two independent controls place outside
  this driver. Running an audio churn on 2a would re-measure somebody else's
  defect and read as a failure of this stage. Record it as excluded with that
  reason.
- Driver Verifier on Windows 2000, and the 2d SMP guest under concurrent
  transfer load.
- Repeated controller disable/re-enable, Windows 2000 only, for the reason in
  B4.

Three vehicle facts that have cost time before:

- QEMU completes about 99% of transfers synchronously inside the doorbell
  kick, so a blind unplug almost never catches a live transfer. If a clause
  needs an in-flight cancel, engineer the window (throttle the block backend,
  `throttling.iops-total=30`) rather than pulling more often.
- `device_del` of a `usb-storage` destroys its `-drive if=none` backend, so
  every replug in a 20-cycle loop needs a `drive_add` first. A cycle loop
  written without that silently stops testing after the first pull.
- A pull is only a pull if the device left: the unplug helper has printed a
  confident `FIRED` while `info usb` still listed the device. Confirm
  departure from `info usb`, not from the script's own output.

### Run, 2b

Every clause of task 11-V.5 passes on Windows 2000 uniprocessor. 80 replug
cycles (HID, storage, physical ASIX Ethernet, audio, 20 each), 0 failed and 0
EP0 refusals, with `devices addressed` climbing 1:1 on every leg; the identity
`submitted = completed + cancelled + aborted` closing exactly on all four at a
settled bus; the in-flight cancel caught; and five controller disable/re-enable
cycles with a device proven to enumerate afterwards. Driver Verifier confirmed
live on every boot. Full record and caveats: `vm\11v-stageF\README-2b.txt`.

The sustained concurrent load, the clause that had never run in any phase, ran
with all four classes at once and gave the cleanest reading of the stage: HID
+ storage + Ethernet + audio on ports 1-4, about 213 transfers/s (vs about
118/s on three classes), 90,447 transfers, 92 MB of isochronous audio, and at
the settled bus `submitted 90,447 = completed 90,443 + cancelled 4`, gap zero,
with `transfers aborted` and every abort sub-counter at 0. Four actively
transferring devices were torn down and the books closed with nothing left to
account for. Each class was verified live from outside its own counters
(storage by `usb_msd_cmd_submit` rate off the QEMU trace, audio by the wav
capture growing, Ethernet by ping replies), because the first attempt at this
window reported four classes while the disk moved zero SCSI commands.

Four findings from that run that outlast it:

1. A root-hub disable never reaches the controller. It produces
   `RH_DisableIrq`, then `RH_GetRootHubData` + `RH_GetStatus` on re-enable,
   and leaves `DriverEntry`/`StartController`/`StopController` counts
   unchanged. A controller disable gives `DisableInterrupts` (irql=02) ->
   `StopController` (irql=00) -> unload -> `DriverEntry` ->
   `StartController`, and the extension base moves every cycle. Do not read
   one as the other.
2. That teardown carries no `SuspendController`, unlike the measured Windows
   98 shutdown (`Suspend` -> `DisableInterrupts` -> `Stop`). A Device Manager
   disable and a shutdown are different paths; stage G inherits this.
3. Cycling a `usb-audio` device is not isochronous traffic. Twenty cycles
   opened one endpoint per device and moved no cancels; Windows opens an iso
   pipe when an application streams, not when the device binds. The iso
   clause needs playback during the load window, and the witness worth having
   is QEMU's own wav capture backend, which is outside our counters.
4. The USB medium arrives full. `usb8v-2b.img` carries batch 8-V's payload
   (260 MB of a 256 MB volume), so every copy failed instantly, the disk
   window closed, and the load reported as running while the trace showed
   zero `usb_msd_cmd_submit`. Checking that a drive exists is not checking
   that it can be written to. `scripts\vm-matrix\guest\LOAD.BAT` now
   write-tests it, and those guest-side scripts are tracked there rather than
   living only on the gitignored transfer volume.

### Run, 2d: HID cycles on both CPU counts

On host FW-W11P-YKM the same image was reverted to `stagef-2d-prepared` and
soaked twice with `-Classes hid -Cycles 20 -ReadEvery 1`, changing only the
CPU count (`qemu-win2k-smp-run-11v.cmd` unmodified, then the same file with
`-smp 2` changed to `-smp 1`). Both legs: 20/20 cycles, 0 failed, `EP0 opens
refused` 0 throughout, `devices addressed` climbing 1:1, a flat +18/+16/+2 per
cycle, and the settled identity closing identically at `366 = 324 + 40 + 2
aborted` with every health counter 0. QEMU 11.0.0, WHPX. Record:
`vm\11v-stageF\README-2d-discriminator.txt`.

- The SMP leg is proved to have executed concurrently, which is the only
  thing that makes an SMP pass mean anything: `deferred-work re-entries
  declined` reads 301 on `-smp 2` and 0 on `-smp 1`, a counter that cannot
  move unless a second CPU enters deferred work while the first is inside it.
  Of all 631 counter rows, 21 differ between the legs and every one is
  liveness or timing; none is in the identity or the refusal block. A soak
  that passes says nothing unless something witnesses that the condition
  under test was present.
- The healthy enumeration baseline, read for the first time: `EnumSequence`
  40, `EnumClaimSpent` 1, `EnumResetsSuppressed` 20 (exactly one suppression
  per claim across 20 cycles), so task 7b-A.1.1's one-per-claim bound holds,
  with every refusal counter 0 and `RhPortsReset` 40 (usbhub's known double
  reset per port). Read a future enumeration state against these numbers
  rather than against zero.

### Run, 2d: storage and net cycles

Host FW-W11P-YKM, scoop QEMU 11.0.0, WHPX, the stage launcher unmodified,
image reverted to `stagef-2d-prepared`, one continuous driver load. `-Classes
storage,net -Cycles 20 -ReadEvery 1`. Record:
`vm\11v-stageF\README-2d-cycles.txt`.

20/20 and 20/20, 0 failed, `EP0 opens refused` 0 throughout, with `devices
addressed` climbing 1:1 across both classes (1-20, then 21-40). At the settled
empty bus, read twice 30 s apart: `submitted 3358 = completed 3332 + cancelled
26`, gap 0, and `transfers aborted` 0, with every abort sub-counter 0. That is
stronger than either HID leg, which needed the `+ aborted` term to close (`366
= 324 + 40 + 2`): here nothing is left over to account for. Endpoint
accounting closes independently: 20 storage x 2 + 20 ASIX x 3 = 100 endpoints
opened, which is what was read. So does open accounting (`OpensTotal` 277 =
`OpensAccepted` 277, `OpenRefusals` 0). Every health counter 0; 40 slots
enabled = 40 disabled. Driver Verifier confirmed live on the boot (`Level:
0000001B`, `xhci98.sys` verified, `RaiseIrqls: 0`).

- Concurrency is witnessed again rather than assumed: `deferred-work
  re-entries declined` 868, against the 0 measured on the `-smp 1` control
  the day before, with `info cpus` showing two distinct `thread_id`s.
- Each class was witnessed from outside its own counters: storage by 506
  `usb_msd_cmd_submit` in the QEMU trace, the ASIX by the `480 Mb/s` +
  `AX88772A` gate on every attach (so the 1.5 Mb/s unclaimed-stub mode could
  not pass silently) and by the guest naming its own bound driver, and the
  xHC's 80 `usb_xhci_slot_address`, all `slotid 1, port 2`: the pinned DUT
  port with slot 1 released and re-acquired each cycle.
- The enumeration baseline extends to 40 cycles and two classes:
  `EnumSequence` 80, `EnumResetsSuppressed` 40 (exactly one per cycle),
  `RhPortsReset` 80, `EnumClaimSpent` 1.

Three things recorded rather than ticked.

(i) The mid-TD conformance verdict is not available on this leg:
`MidTdShortRetiresTotal` 21 with `MidTdTailsCensoredTotal` 19,
`MidTdTailsDroppedTotal` 2 and `MidTdVerdictVoided` 1, and batch 9-0 settled
that the verdict is readable only while dropped and censored are both 0. Task
9-0.2's own measurement is where that verdict lives; these numbers must not be
mined for a claim they cannot carry.

(ii) The `net` cycles leave a modal "Unsafe Removal of Device" dialog naming
the ASIX, plus one tray icon per cycle. It blocked nothing here (the counter
channel is the monitor, not the GUI), but stage E measured that a modal dialog
blocks the PnP install queue on both targets, and the load phase needs three
classes to attach, so revert to `stagef-2d-prepared` before it rather than
clearing the dialog by hand.

(iii) `ProbeSetups`/`ProbeEndpoints` overflowed (502 and 273) as stage D
predicted: a sample, not a census.

A harness defect older than this run, found and fixed in passing.
`soak-11v.ps1`'s `Write-Block` emitted through `Write-Host` only, so the
persisted report carried the identity and nothing beside it: the baseline,
the abort block and the health block were all dropped. That is backwards,
since this stage's reading rule is that a gap is not a leak until the abort
counters are read next to it. They survived only when a run happened to be
teed to a console log. Every report written before stage E has the hole, the
2b legs included. Now routed through `Add-Line`.

### Run, 2d: the concurrent load

Host FW-W11P-YKM, scoop QEMU 11.0.0, WHPX, the stage launcher unmodified,
image reverted to `stagef-2d-prepared`, one continuous driver load, Driver
Verifier live (`Level: 0000001B`, `xhci98.sys` loads 1 / unloads 0). `-Classes
"hid,storage,net" -Cycles 0 -LoadSeconds 600 -NoRepin`. Record:
`vm\11v-stageF\README-2d-load.txt`. `vm\usb8v-smp.img` was checked host-side
beforehand and has 226 MB free of 256 MB, so unlike 2b's first attempt the
medium was not full.

Three classes on root ports 1/2/3 together for 600 s, 46,128 transfers in the
window at about 118/s, the same rate 2b's three-class window gave. At the
settled empty bus, read twice: `submitted 47,585 = completed 47,580 +
cancelled 3 + aborted 2`, the gap stable at 2 and exactly `transfers
aborted`, with every abort sub-counter 0 and every health counter 0.
Accounting closes three further ways independently of the identity: 4
addressed / 4 slots enabled = 4 disabled / 9 endpoints opened (1 mouse + 2
storage + 3 ASIX + 3 for the pre-window probe), `OpensTotal` 25 =
`OpensAccepted` 25 with `OpenRefusals` 0, and `CommandsIssued` 37 =
`CommandsCompleted` 37 with `CommandFailures` 0.

- The concurrency witness is an order of magnitude above the cycle leg:
  `deferred-work re-entries declined` 8,806, against 868 on the same guest's
  cycle run and 0 on the `-smp 1` control. `info cpus` shows two distinct
  `thread_id`s. Under real multi-class load, two CPUs are demonstrably inside
  this driver's deferred work together.
- Every class witnessed from outside its own counters, and the xHC too.
  Storage by 10,837 `usb_msd_cmd_submit` (bulk data, against the cycle leg's
  506 of enumeration chatter) plus the medium's own host-side mtime inside the
  window; Ethernet by 5/5 ping replies at <10 ms taken in the guest console
  during the window on a real LAN address (192.168.1.235), with the guest
  naming its own bound driver; and the xHC by `usb_xhci_xfer_success` 47,580
  = `TransfersCompleted` 47,580 to the unit (`xfer_start` 47,583 vs
  `TransfersSubmitted` 47,585: the two aborted transfers never reached the
  ring).
- Simultaneity is measured, not inferred from the attach list. Per-slot from
  the trace: slot 2 (storage) 21,054 bulk OUT + 10,939 bulk IN + 23 control;
  slot 3 (ASIX) 4,716 + 3,348 + 3,347 + 3,302 control; slot 1 (mouse) 698
  interrupt IN. Control, bulk IN, bulk OUT and interrupt IN live across three
  slots, and consecutive transfers on the ring change slot 10,991 times in
  47,583, longest single-slot run 540. A run that merely visited three
  classes in turn would show a handful of switches.
- The enumeration baseline holds on a simultaneous population, not only on
  serial replugs: `EnumSequence` 8 (2 per device x 4), `EnumResetsSuppressed`
  4 (exactly one per device), `RhPortsReset` 8, `EnumClaimSpent` 1.
- A positive worth more than the clause it came from. This is the first
  bulk-heavy load this driver has run under Verifier on SMP, and
  `ProbeSgDisordered`, `ProbeSgGapped`, `ProbeSgHighDwords` and
  `ProbeSgLengthGaps` are all 0. Phase 3 task 10 said usbport's SG high DWORD
  is structurally zero rather than masked and to check it; this is that
  check, at scale, and it is clean. `ProbeSgShape` read 758,622, and the 2a
  load leg read the identical value on a different target with ten times the
  transfers. It is not an observation count: `src/xhci.h:6369` and
  `src/xhci_probe.c:299` make it an OR-accumulated bitmask of shape
  properties, and the firing count is the separate `ProbeSgShapeFirings`. The
  agreement across the two targets is the real finding: Windows 98
  uniprocessor and Windows 2000 SMP present usbport SG lists of the same
  shape set. A counter's name is not its semantics; two runs agreeing to the
  unit is the signal that one of them was read wrong.

Six things recorded rather than ticked.

(i) The gap is 2, not zero: it closes through the `+ aborted` term (the HID-leg
shape) rather than 2b's four-class gap-zero or this guest's own cycle leg.
Closed and stable, but not the strongest form, and it must not be cited as
gap-zero.

(ii) Storage was idle at the pull, so 2b's "actively transferring devices torn
down" is not reproduced here: `STAGEF.BAT`'s ten passes take about 3.5 min
against a 600 s window, and although the disk half was restarted by hand at
about t+300s it had finished again by t+584. Net and HID were active.

(iii) The mid-TD verdict is unavailable and further from readable than on the
cycle leg (`MidTdTailsDroppedTotal` 3,348); task 9-0.2 is where it lives.

(iv) `devices addressed` is 4, one more than the three classes, which is the
pre-window ASIX probe below.

(v) Audio did not run, via `LOAD.BAT`'s new `NOAUDIO` switch, and the usual
shorthand for why is wrong: the measured Phase 9 reason excludes audio on 2a,
not on 2d, which is Windows 2000 like 2b. Audio is absent because 2d's clause
is the concurrency half and the isochronous clause was placed on 2b; that is a
scoping decision baked into the launcher, and adding it would also destroy the
one-token difference that makes the `-smp 2`/`-smp 1` A/B valid.

(vi) `ProbeSetups.Overflows` 44,223 against a 16-row cap (cycle leg: 502): a
sample, not a census, where stage D predicted it would bite hardest.

Four vehicle lessons this leg paid for, each of which would have cost a
window:

1. Prove the Ethernet is reachable before spending the window. The `net`
   cycles proved the ASIX attaches and binds; they never proved the cable
   goes anywhere. The adapter was attached alone first, `ipconfig`/`ping`
   read (192.168.1.235, 3/3 replies), and removed; that is the fourth
   `devices addressed`. The removal raised the predicted "Unsafe Removal" box; it
   was dismissed and the desktop re-checked clean before the load, because
   this phase needs three classes to attach.
2. `-NoRepin` matters. The default re-pins QEMU's pointer to PS/2 after the
   HID attach, and `mouse_move` drives whichever handler is current, so with
   the re-pin the pump would have moved the PS/2 mouse and the USB mouse would
   have sat on one parked interrupt IN for 600 s while reporting as attached
   throughout. `+0` is not a pass, in another costume. Confirmed after the
   fact: slot 1 ep 3 moved 698 transfers. The host-side driver for that half
   is written down as `scripts\local\hidpump-11v.ps1`.
3. Three concurrent monitor clients is one too many. QEMU's socket monitor
   serves one client at a time, so soak + pump + screendump gives "the target
   machine actively refused it", and worse, a `Send-Mon` that waits out its
   900 ms idle timeout returns an empty reply, which is a counter read that
   fails rather than one that is slow. Give the pump a gap, type into the
   guest immediately after a `t+` line, and read anything optional from files
   (the trace and debugcon need no monitor and answered most of the questions
   here).
4. A directory listing cannot witness these writes. After the run `7z l
   vm\usb8v-smp.img` shows W1-W4.BIN at the same sizes and the same
   timestamps batch 8-V left, free space unchanged, which reads like nothing
   was written. `copy` preserves the source timestamp, and four files of
   identical size rewritten in place move neither sizes nor free space. The
   instruments that could show the positive: the medium's own host-side mtime
   and 10,837 `usb_msd_cmd_submit`.

### Run, 2a: the cycle clauses

All three cycle clauses pass; the concurrent load was blocked on guest-side
tooling on this run, not on this driver, and passed on the next (below). Host
minis-w11p-ykm (a third machine; the repo is on `D:` there, so the hard-coded
launchers work unedited), scoop QEMU, `qemu-win98-run-11v.cmd` unmodified,
one continuous driver load per run, `MiniPortExtensionSize` `0001243C` =
offsets `SIZEOF` 74,812 on every boot. Record: `vm\11v-stageF\f-2a-*`.

```
hid      20/20   0 failed   addressed 1:1   EP0 opens refused 0
storage  20/20   0 failed   addressed 1:1   EP0 opens refused 0
net      20/20   0 failed   addressed 1:1   EP0 opens refused 0
```

The `net` leg was run alone from a fresh boot and closes in the strongest
form: settled bus, read twice, `submitted 3180 = completed 3159 + cancelled
21`, gap 0, with `transfers aborted` 0. `EndpointsOpened` 60 = 20 x 3 closes
endpoint accounting independently, `OpensTotal` 100 = `OpensAccepted` 100,
`SlotsEnabled` 20 = `SlotsDisabled` 20, `CommandsIssued` 183 =
`CommandsCompleted` 183, and the enumeration baseline holds as on 2d:
`EnumSequence` 40 = 2x20, `EnumResetsSuppressed` 20 (one per claim),
`RhPortsReset` 40.

Three clauses are excluded on this target with their measured reasons. Audio,
for the Phase 9 reason (that OS's audio stack failed five of five, placed
outside this driver by two independent controls). Driver Verifier, a Windows
2000 facility. Repeated controller disable/re-enable, which bugchecks Windows
98 through every door (stage C).

Four findings, and the first two would each have closed this leg wrongly.

1. The handoff's stated blocker did not exist. It opened this leg with "2a
   has never had an Ethernet class driver installed" and budgeted a boot for a
   modal wizard. Batch 8-V.2 had installed it: the snapshot
   `8v2-asix-installed` is on `vm\win98.img` with later snapshots on the same
   chain, and 8-V.2's own install and DHCP screenshots are in `vm\`. A
   fresh-boot attach on port 2 bound with no wizard: 3 endpoints, 1,583
   transfers, a clean desktop. Before budgeting a boot for an install, look
   for the snapshot that says it already happened; the tags on the working
   image are a cheaper and better-dated record than a prose handoff.
2. The harness scored "20/20, 0 failed" three separate times while nothing
   reached the driver. Windows 98 keys a USB devnode by bus location, and a
   modal Add New Hardware Wizard fired for HID, then for Mass Storage, then
   again for HID and the ASIX when the load phase moved them to ports 1 and 3.
   In each case `soak-11v.ps1` reported a clean pass while `devices
   addressed` sat frozen and `endpoints opened` did not move. `addressed` is
   the only counter that separates a cycle that ran from a cycle that was
   reported. A 2-cycle smoke test per class, read on `addressed` rather than
   on `ok`, caught each one for about a minute of machine time; without it
   the 20-cycle run would have reported 20/20 and measured one enumeration.
3. The `net` leg is a settle-time race, and at 6 s it takes the guest down. A
   one-parameter A/B, same binary, same launcher, same port, only
   `-SettleSeconds 6 -> 15`, built the way 2d's `-smp 2`/`-smp 1` pair was:

   | | settle 6 s | settle 15 s |
   |---|---|---|
   | `DevicesAddressed` | 4 of 20 (1,3,5,6 then dead) | 20 of 20 |
   | `DevicesReopened` vs addressed | 49 vs 50, one short | 20 vs 20 |
   | `OpenRefusals` | 1 | 0 |
   | outcome | usbport stops enumerating, shell wedges | clean |

   At 6 s, cycles 1-5 alternate (every other replug enumerates) and at cycle 6
   the driver refuses one EP0 reopen at `src\xhci_slot.c:4358`
   (`xhciDevByAddress(ext, 1)` returned NULL, the task 6-B.4 rebuild path),
   after which not one further transfer arrives across fourteen cycles. The
   guest is not hung (`Test-GuestAlive` reports alive throughout, the clock
   advances, `HealthPolls` reads 576,627, every controller counter is clean
   and the settled identity still closes exactly) but the shell stops
   responding to clicks with no dialog on screen. `DevicesReopened` exactly
   one short of `DevicesAddressed` is the compact signature. Two things
   follow, and neither is closed here: whether a refused EP0 reopen should be
   survivable above us (the same seam as the Phase 6 batch V blocker, in the
   opposite direction; that one was a record holding an address usbport had
   recycled), and that the refusal cannot say why in a release build.
4. That refusal site carries no sub-counter, so the endstate reads
   `OpenRefusals` 1 with `OpenRefusalsNoRecord`, `NoClaim`, `Buffer`,
   `Malformed` and all five `EndpointRefusals*` at 0. Only the debug build's
   trace named the reason. This is the diagnosability gap already written
   down as "eleven `OpenRefusals++` sites, three sub-counters, a counter that
   cannot say why", hit in the wild on the one failure of the stage. Two
   reading traps beside it: the trace's `=00000001` is the device address,
   not a count (the macro prints `properties->DeviceAddress`), and
   `EndpointRefusalsNoDevice` is a similarly-worded counter for a different
   site.

The load clause was blocked on this run by two guest-side facts, both
Windows-98-only, so 2b and 2d never met them. Neither is a driver
problem: the teach-check immediately beforehand read `EndpointsOpened` 6
(mouse 1 + storage 2 + ASIX 3) with `OpenRefusals` 0 and all three classes
bound.

- Windows 98's `COMMAND.COM` can `dir` and `type` a batch file on the QEMU
  `fat:` volume but cannot execute one from it: "Bad command or file name",
  with the bus idle and nothing running. Ruled out by measurement: CRLF (all
  four .BATs are CRLF, and `LOAD.BAT` was byte-identical to a boot where it
  had run), 8.3 alias generation (`dir` lists correct 8.3 names and byte
  counts), VVFAT size (33 MB of a 504 MB volume), `COMSPEC`/environment (both
  normal), and internal-vs-external commands (`xcopy` is external and runs).
  The decisive test was the negative control on C:, where the same batch
  runs. Workaround: `copy` the scripts to `C:\` and run them there; reading
  from the volume is unaffected.
- `scripts\vm-matrix\guest\LOAD.BAT` and `STAGEF.BAT` are Windows 2000
  scripts. `cmd /c` (Win98's shell is `COMMAND.COM`), `%~f0` and `for /L`,
  all on the lines that start traffic (`LOAD.BAT:115/133/138`,
  `STAGEF.BAT:70-75`). They were written for 2b and reused on 2d, and 2a is
  the first Windows 98 target to need them. Symptom: the load reports as
  running while the trace carries 28 `usb_msd_cmd_submit` against 2d's
  10,837.

What the load clause needed was script work: a Win98 variant using `command
/c`, an explicit `C:\` path in place of `%~f0`, the `for /L` audio loops
dropped (2a is `NOAUDIO` regardless), run from `C:\`. That is the
`load98`/`STGF98.BAT` pair the next run used.

A vehicle artefact that imitates a crash. After about 15 minutes with no guest
input (a `net`-only soak touches neither keyboard nor mouse) Win98 blanks the
display and QEMU reverts the console surface to its default 720x400 text
geometry: a black screen with a cursor at the top-left, indistinguishable from
a drop to DOS. A `mouse_move` restores the 640x480 desktop intact. Separate it
from a real wedge from files, without touching the monitor: count
`DriverEntry` in the debugcon log (one = no reload) and grep for
`cb SuspendController`/`cb StopController` (absent = never torn down).

Vehicle left behind: `vm\win98.img` gained `pre-11v-stagef-2a-2026-08-15`
(taken before any of this) and now carries the HID, storage and ASIX class
installs at root ports 1, 2 and 3, which persist across reboots. `savevm`
cannot preserve a live 2a wedge (the launcher's `-drive
if=floppy,...,format=raw` makes QEMU refuse), so a future wedge investigation
must drop the floppy or accept counter-dump evidence.

### Run, 2a: the concurrent load

Passes at gap zero, and with it stage F is complete on all three targets. Host
minis-w11p-ykm, scoop QEMU, `qemu-win98-run-11v.cmd` unmodified, one
continuous driver load, `MiniPortExtensionSize` `0001243C` = 74,812 = offsets
`SIZEOF`, one `DriverEntry` and one `cb StartController` (extension
`C14658C4`, unmoved). The image was not reverted: the state the cycle legs
left is what carries the class installs at root ports 1/2/3, and a revert
would have thrown them away; an offline
`pre-11v-stagef-2a-load-2026-08-16` was taken instead. `-Classes
"hid,storage,net" -Cycles 0 -LoadSeconds 600 -NoRepin`. Record:
`vm\11v-stageF\README-2a-load.txt`.

Three classes on root ports 1/2/3 together for 600 s, 478,036 transfers in
the window at about 860/s: four times 2b's four-class rate and seven times
2d's. At the settled empty bus, read twice: `submitted 495,138 = completed
495,135 + cancelled 3`, gap zero, with `transfers aborted` 0 and every abort
sub-counter 0. That is the strongest available form (2b's four-class shape)
and stronger than 2d's load, which needed the `+ aborted` term to close.
Accounting closes three further ways independently: 3 addressed = 3 reopened
= 3 slots enabled = 3 disabled = 3 torn down, 6 endpoints opened (mouse 1 +
storage 2 + ASIX 3), `OpensTotal` 12 = `OpensAccepted` 12 with `OpenRefusals`
0, and `CommandsIssued` 23 = `CommandsCompleted` 23. Every health counter 0;
`InterruptCount` 492,204 = `InterruptsClaimed` 492,204.

- `DevicesReopened` equal to `DevicesAddressed` is worth naming on this
  target: one short is the compact signature of the 6-second settle-time
  failure the cycle legs found, and it is absent here.
- Every class witnessed from outside its own counters, and the xHC too:
  storage by 162,365 `usb_msd_cmd_submit` (against the blocked attempt's 28
  and 2d's 10,837, the decisive proof the new guest scripts move data); HID
  by 454 host-side `mouse_move` producing 249 interrupt IN transfers on slot 1
  ep 3; Ethernet by the physical ASIX admitted only through the `480 Mb/s` +
  `AX88772A` attach gate and moving 7,743 data transfers; and the xHC by
  `usb_xhci_xfer_success` 495,135 = `TransfersCompleted` 495,135 to the unit
  (`xfer_start` 495,137 vs submitted 495,138; the one transfer that never
  reached the ring is the abort block's `No-Opped by a cancellation` 1).
- Simultaneity measured per slot: slot 1 (mouse) 12 control + 249 interrupt
  IN; slot 2 (storage) 15 control + 162,429 + 324,654 bulk; slot 3 (ASIX) 35
  control + 2,991 + 2,615 + 2,137. Control, bulk IN, bulk OUT and interrupt IN
  across three slots at once, 3,894 slot switches, longest single-slot run
  984. Read that ratio honestly: 2d switched 10,991 times in 47,583, far more
  often; the difference is this leg's unbounded disk stream, which is 487,083
  of the 495,137. It supports "three classes interleaved throughout", not
  "interleaved evenly".
- The enumeration baseline holds on a simultaneous population: `EnumSequence`
  6 (2 per device), `EnumResetsSuppressed` 3 (exactly one per device),
  `EnumClaimSpent` 1.
- `DeferredReentries` 0, the expected uniprocessor reading and a third
  independent confirmation that 2d's 8,806-vs-0 pair measured concurrency
  rather than something incidental to that vehicle.

The storage stream was actively transferring at the pull, which 2d could not
reproduce: the teardown raised Windows 98's full-screen `Disk Write Error -
Unable to write to disk in drive F:`. That property came from deleting the
Win2000 script's ten-pass bound rather than adding anything; on both Windows
2000 legs those passes had finished before the teardown, hence "storage was
idle at the pull" against 2d.

Four things recorded rather than ticked.

(i) The Ethernet guest-console reply count was not taken on this leg, unlike
2b's and 2d's. That Disk Write Error box owns the console absolutely: Enter,
Alt+Tab, Alt+Enter, Ctrl+Alt+Del and even an ACPI `system_powerdown` were all
swallowed (no `cb SuspendController` or `cb StopController` ever reached the
driver), so the five PING windows behind it were unreachable and the guest had
to be closed with `quit`. Take guest-console readings before the teardown, not
after it. What the trace does establish is that frames really moved: an
unreachable ping is answered by the local IP stack and generates zero USB
traffic, which is what the no-adapter control earlier in the session showed,
against 7,743 transfers here. (Stage G's 2a leg later saw `Reply from
192.168.1.10x TTL=64` on the guest console, so the ASIX was working.)

(ii) The mid-TD verdict is unavailable (`MidTdTailsDroppedTotal` 2,610,
`MidTdVerdictVoided` 1); task 9-0.2 is where it lives.

(iii) Audio, Driver Verifier and controller disable/re-enable stay excluded on
2a with their measured reasons, and the Win98 script pair has no `NOAUDIO`
switch to forget because it has no audio half at all.

(iv) `ProbeSetups.Overflows` 16 and `ProbeEndpoints.Overflows` 25: a sample,
not a census, far below 2d's 44,223 because this population is 3 devices rather
than 40.

`ProbeSgShape` reads 758,622 here too, the same value as 2d on a different
target with ten times the transfers; see the 2d load run for what that
counter is. The clean part is Phase 3 task 10's own check, now run at 495k
transfers: `ProbeSgDisordered`, `ProbeSgGapped`, `ProbeSgHighDwords` and
`ProbeSgLengthGaps` all 0.

The guest scripts were validated before the window, in about three minutes,
and that is the transferable part of this leg. `load98` with no arguments
printed its usage (the file parses and its labels resolve under COMMAND.COM);
`load98 F D 192.168.1.100` with nothing attached ran the 8 MB canary and took
the refusal path; and `start /m command /c c:\stgf98.bat NET 192.168.1.100`
put a PING box on the taskbar running `-n 150 -l 1400` and restarting itself,
which confirmed START syntax, argument passing, the worker re-entry and the
`:netloop` in one command. The load line was then pre-typed at the guest
prompt without Return while the soak attached, so starting it cost exactly
one `sendkey ret` and the two-monitor-client limit was never approached.

A 720x400 screendump is not automatically the idle blanking artefact the
cycle run records. Here it was a full-screen DOS box. Both are separated from
files the same way: one `DriverEntry` = no reload, no `cb SuspendController`
/ `cb StopController` = nothing torn down.

---

## Stage G - task 11-V.1, the lifecycle with traffic

Suspend and resume, disable/re-enable on Windows 2000 only (that transition
bugchecks Windows 98 whichever door it is reached through, see B4, so the
reachable Windows 98 lifecycle here is shutdown and boot, and the roadmap
records the clause as unreachable rather than substituted), orderly shutdown,
restart after controller invalidation, and a stop while asynchronous command
and port-reset callbacks are armed. All with traffic running, which is what
makes this different from the Phase 4 lifecycle runs.

### How the leg is driven, per target

The bus population and the guest-side load are stage F's; "with traffic in
flight" is the only thing stage G adds. The estimate was two boots per target,
one for the stop and one for the restart. It took three on 2b and four on 2a,
and the guest-side load turned out not to be reusable on Windows 98, because
it blocks that guest's own shutdown. The steps below branch per target
wherever the two differ.

| # | Where | What |
|---|---|---|
| 1 | host, before launching | Stage the guest scripts onto the `fat:` volume: `vm\xfer98` for 2a, `vm\xfer` for 2b. A `fat:` volume is snapshotted when QEMU opens it, so a file copied afterwards is invisible to the guest. |
| 2 | host | Launch the workhorse: `qemu-win98-run-11v.cmd` (2a, monitor 55555) or `qemu-win2k-run-11v.cmd` (2b, monitor 55556). Both carry `-no-shutdown`, which is what makes step 7 possible. |
| 3 | guest | Boot. On 2a the previous leg closed with `quit`, so ScanDisk runs; let it finish. On 2b only: `copy d:\*.bat c:\`. |
| 4 | host | `soak-11v.ps1 -Cycles 0 -LoadSeconds 900 -NoRepin -Classes "hid,storage,net"` against that monitor. It attaches the three classes on root ports 1/2/3 and holds them, printing the identity every 15 s. Kill it once they are attached: at the end of its load window it `device_del`s all three, which would strip the bus before the stop. |
| 5 | guest | Start the load. The two targets need different traffic sources (see "The Windows 98 shutdown block" below). 2b: `LOAD.BAT`, pre-typed without Return. 2a: an Explorer copy of `D:\BIG96MB.BIN` to `F:`, plus `hidpump-11v.ps1` from the host. A DOS-box load on 2a blocks the shutdown outright and cannot be used. |
| 5a | host, 2a only | Re-attach storage throttled before the stop, or the copy outruns the harness: `soak-11v.ps1 ... -StorageImage vm\usb8v.img -LoadStorageDrive ""` attaches it at `throttling.iops-total=30`. Unthrottled, a 96 MB copy finishes in about 20 s while `lifecycle-11v.ps1` spends 60-90 s on its preamble before committing. |
| 6 | host | `hidpump-11v.ps1` for the HID half. On 2b let it finish before step 7 (two monitor clients is the limit). On 2a run it across the stop: it is host-driven, costs the guest no process, and is what keeps HID traffic live through the teardown. |
| 7 | guest + host | Bring up Start -> Shut Down and focus its OK, then run `lifecycle-11v.ps1 -Commit sendkey`. It gates on measured traffic, commits the stop, churns a `usb-hub` on the free root port across the whole teardown, waits for `cb StopController` in the debug console, and reads the stop-time counters out of the stopped guest. `-NoChurn` is the setting both replications used. |
| 8 | host | Relaunch, boot, and repeat steps 4-5 with a shorter `-LoadSeconds`. Soak's settled identity is the restart clause's reading. |

Why the arming is a loop and not a single plug. "Fire the stop into a plug"
assumes the stop reaches the miniport when the operator commits it. It does
not: the guest runs its own shutdown for seconds first, by which time one
plug's port reset and its Enable Slot / Address Device commands are long
retired. So the window is held open across the whole teardown instead, a hub
added and removed every few hundred milliseconds.

A hub because it is not the storage device: pulling that one mid-write raises
Windows 98's Disk Write Error box, which owns the console and would swallow the
shutdown itself. A hub does need a class install on 2a: the first churn hub
raised an Add New Hardware Wizard for Generic USB Hub. This still does not
guarantee the overlap, and a run that gets no witness is recorded as the clause
attempted and not entered.

The stop-time counters are readable after the stop, on both targets. The
extension is zeroed at the next start, and only then. `-no-shutdown` leaves
QEMU holding the machine with its memory intact (Windows 2000 on `acpi=off`
never even asks to power off; it ends on "It is now safe to turn off your
computer" with the VM still running), so between the teardown and the
relaunch the miniport extension is still there and `x/Nwx` reads it. The
trace and the counters are two independent channels here, and both are
reported.

| Observation | Reading |
|---|---|
| Windows 98 shutdown | the measured `Suspend -> DisableInterrupts -> Stop` shape |
| `SuspendCount`, `SuspendFailures` | failures 0 |
| `port teardowns skipped - suspended` | expected to move, and a zero is the suspicious reading. On the ordinary Win98 shutdown the stop arrives on a controller the suspend has already halted, and `PORTSC` is unwritable once the controller stops, so the port pass cannot run and is skipped. What this counter proves is that the skip is counted rather than silent, which was the defect found in review. Do not read it as a failure, and do not expect the port pass on this path. |
| Stop with an armed callback: that the window was entered | `commands abandoned` moved, or the trace shows `root hub: port operations retired by the quiesce`. These are the teardown's own witnesses; see the note below for how to read them and what they cannot say. |
| Stop with an armed callback: that nothing broke in it | `RH resumes abandoned`, `command timer failures`, `RH resets not confirmed` all 0 |
| DMA ownership after shutdown | `DMA failures closed` 0 |
| `cb ResumeController` on Windows 2000 | will not appear. See below. |

How to arm a callback so the stop lands on one, because "with callbacks armed"
is a state and not an action:

- A port reset is armed for the length of the reset the driver drives, so a
  stop must arrive during it. Script it: `device_add` a device and issue the
  stop from the monitor immediately afterwards. Have the stop command ready
  and fire it into the plug, not after observing one.
- A command watchdog is armed for every command the engine issues, and an
  idle bus does not supply any: the health poll samples state, it does not
  post commands. So the window has to be made by device churn (a plug or an
  unplug issues real commands) and a stop fired into that.

No counter at the callback end proves the overlap happened. `RH resets not
confirmed` counts another port operation finding a live reset, nothing to do
with a stop. `RH stale timers` and `command stale callbacks` do count a
callback arriving for an operation that is gone, but ordinary successful
completion produces exactly that: the reset completes and disarms, the
command retires, and the uncancellable timer then arrives and finds nothing.
The source says so at both sites and calls it the common case. And a callback
arriving after a stop and a restart fails the epoch check and increments
nothing at all. So those counters can move without the race, and the race
can happen without moving them. The trace ordering is no help either: the
stale branches print nothing, in `src/xhci_cmd.c` and `src/xhci_rh.c` alike.

The oracle is at the other end of the mechanism. The question is what the
teardown does when it finds a callback armed, and the teardown says so:

| Witness | Where | How to read it |
|---|---|---|
| `commands abandoned` | counter and trace (`command: abandoned outstanding TRB`) | the quiesce found a command outstanding, i.e. the stop landed on an armed command watchdog |
| `root hub: port operations retired by the quiesce` | trace, uncapped; and the counter `RH operations retired by the quiesce` in `offsets.txt` | the quiesce found armed port operations and retired them: the stop landed inside a reset or resume |

Both witness that this driver's operation was armed; neither can say anything
about usbport's own timer bookkeeping, which the miniport cannot see. They
are written by the quiesce, which on the measured Windows 98 shutdown runs
inside `SuspendController`, ahead of the stop, so read them the way stage C's
are read: the extension is zeroed at the next start. The trace is live and
keeps them; the stopped-guest window above reaches the counters; and if the
log is working, `cmd.abandoned` is in the log's own stop-time counter block.

The failure-shaped zeros are the other half (`RH resumes abandoned`, `command
timer failures`, `RH resets not confirmed`, `DMA failures closed`): they say
nothing was recorded as broken. A pass is a witness above moving and those
staying at zero. Either alone is not the clause, and a run that gets no
witness has not tested it, however many stops were fired.

- Controller invalidation is not directly triggerable from the monitor. The
  driver raises it itself on a divergence it detects; nothing in this vehicle
  injects one (the same reason stage D cannot fail a controller). If it does
  not occur naturally, record it as not reached rather than as passed.
- If several attempts do not land inside a window, that is a result too:
  record how many were tried and what the counters read.

`ResumeController` has never executed on Windows 2000 on any vehicle, and the
QEMU avenue was closed on evidence: the ACPI guest that was built has no
`_S1_` in its DSDT, and neither this controller nor Microsoft's own EHCI
exposes device power management there. So this half needs bare-metal Windows
2000, which this project has no vehicle for. Record it as deferred with that
reason.

When it eventually runs, two things are to be observed: that `cb
ResumeController` appears at all and is followed by a clean reinitialisation
and a rebuilt port shadow, and that `RhPortsDriventoU0` reads zero. Read
roadmap task 11-V.1 in full first; it explains why zero is the expected
reading on both candidate paths, so that counter checks an expectation and
does not by itself select one.

`RhPortsDriventoU0` is readable two ways. `src/xhci_rh.c` publishes it through
`XHCI_DBG_VALUE`, which `gen-offsets.ps1` does not match, and it also has a
change-gated site in `xhciCheckController` (`RH ports driven out of U3`),
which puts it in `offsets.txt` and makes the expected reading print, since
that macro prints its first sample even when the sample is zero. If neither
the `src/xhci_rh.c` line nor a counter row is present when this finally runs,
the hoist task 6-B.6 carries did not land, and an absent line is unreadable
rather than negative.

### Run, 2b

Both clauses this stage owes pass on Windows 2000: the orderly shutdown with
traffic in flight, and the restart after it. Host minis-w11p-ykm, scoop QEMU
11.0.0, `qemu-win2k-run-11v.cmd` unmodified, debug build, built `Aug 14 2026
00:36:27`, `MiniPortExtensionSize` `0001243C` = 74,812 = offsets `SIZEOF` on
both boots, extension VA `8184292C`. Driver Verifier confirmed live:
`verifier /query` reads `Level: 0000001B` with `Name: xhci98.sys, loads: 1`,
so special pool, force IRQL, pool tracking and I/O verification were all on
for both boots. Record: `out\phase11-stageG\` (`stageG-2b-stop.txt`,
`stageG-2b-stoptime-counters.txt`, `stageG-2b-debugcon.log`,
`stageG-2b-trace-summary.txt`, screenshots).

The stop. Three classes on root ports 1/2/3, `LOAD.BAT F E ... NOAUDIO`
running, and the gate measured 8,340 transfers/s in the twelve seconds before
the commit. The teardown arrived 10.4 s after the click, in the measured
shape and all at `irql=00`:

```
cb SuspendController irql=00 a=8184292C
cb DisableInterrupts  irql=02 a=8184292C
cb StopController     irql=00 a=8184292C   (b=00000001)
teardown: the stop arrived on a suspended controller - PORTSC is unwritable
```

`suspends` 1, `teardowns` 1, and `port teardowns skipped - suspended` 1, the
counter this sheet says must move. Every failure-shaped counter 0: `DMA
failures closed`, `suspend failures`, `command timer failures`, `RH resumes
abandoned`, `RH resets not confirmed`, `teardowns without a stop`, `devices
abandoned without evidence`.

The identity closes across the teardown itself, which no earlier leg has
read: `submitted 1,799,681 = completed 1,799,677 + cancelled 1 + aborted 3`.
Those 3 aborts are the objective witness that the stop landed on traffic; a
teardown on an idle bus has nothing to abort, which is what "storage was idle
at the pull" meant on 2d. The trace corroborates the counters to the unit:
`usb_xhci_xfer_start` 1,799,681 = submitted, `usb_xhci_xfer_success`
1,799,677 = completed, with 602,815 `usb_msd_cmd_submit`.

The stop-time counters were read out of the stopped guest through the
`-no-shutdown` window described above, with `info status` still `running`
after "It is now safe to turn off your computer". The trace is the
independent second channel and both are recorded.

The restart. Relaunched, booted, `init step=0x16` / `init status=0`,
`MaxSlotsEn` 0x20, one `DriverEntry`, one `cb StartController`. All three
classes re-bound with no wizard, the load restarted, and the driver carried
1.02 M transfers with `InterruptCount` = `InterruptsClaimed` (882,352).
Detached and read twice at a settled bus, identical both times: `submitted
1,108,317 = completed 1,108,314 + cancelled 3`, gap zero, with `transfers
aborted` 0 and every abort sub-counter 0 (`TransfersNoOpped` 1). 3 addressed
= 3 torn down = 3 slots disabled, 6 endpoints opened.

The armed-callback clause was attempted twice and entered neither time, and
is recorded as attempted rather than passed. What the attempts add is why:

1. Into the shutdown: a `usb-hub` added and removed on free root port 4 every
   600 ms across the whole teardown, 3 attach/detach pairs in the 10.4 s
   window, of which the counters show one actually enumerated (`devices
   addressed` 4 = 3 classes + 1 hub). `commands abandoned` and `RH operations
   retired by the quiesce` both stayed 0, and neither trace line appeared.
   The plug rate the driver sees is set by the guest's PnP path, not by the
   churn rate, so the window cannot be brute-forced from the monitor.
2. Into a Device Manager disable, which should have been the better vehicle
   because the OS is alive, and it produced a new vehicle limitation instead:
   with a storage stream writing, the disable never reaches this miniport at
   all. Zero `cb StopController` in the whole boot, the devnode carried no
   red X afterwards, and MMC sat blocked on the operation while the driver
   kept serving about 470 transfers/s and usbport kept calling it
   (`HealthPolls` = `CheckCallbacks`, both advancing). Windows 98 has "the
   shutdown is the only stop that reaches this driver" for its own reason
   (stage C); on Windows 2000 a disable behind a busy volume blocks above us
   for a different one. An open Windows menu also holds an input capture, so
   a blocked MMC makes the whole desktop stop taking keys;
   `ctrl-alt-delete` breaks that capture, and a screendump of a frozen menu
   is a picture of what is painted, not evidence of a wedge.

The most valuable reading of the leg came out of that failed attempt. The
250 ms churn drove about 4 plugs/s at the guest for five minutes, and the
driver logged the seam that wedged 2a in stage F:

```
cb RH_ClearFeaturePortEnable irql=02 a=8184292C b=00000004
slot: EP0 open for an address no record holds=00000004
EP0 opens refused=00000001
```

- The `=00000004` is the device address, not a count, the same reading trap
  the 2a stage F record names. And the two lines are not a chain: that `b` is
  the port number, the refusal's value is the device address, and the two 4s
  are unrelated quantities that happen to coincide. The lines are 203 apart
  in the log, with a complete enumeration between them (`slots enabled` and
  `devices addressed` both reaching 10, `RH ports reset` 0x13 -> 0x14). What
  precedes the refusal is a successful enumeration, after which usbport asked
  to open EP0 for address 4, an address whose record had been torn down
  earlier in the storm (`devices torn down` 7). Nothing here establishes what
  triggered it.
- On this target the refusal was survivable. After it, enumeration continued
  to 10 devices addressed, open accounting stayed balanced (45 opens = 44
  accepted + 1 refused, "open accounting: OK"), `CommandFailures` 0,
  `EndpointHalts` 0, `TransfersAborted` 0, transfers kept flowing, and the
  guest kept running normally; the Device Manager tree in
  `asset-2b-devmgr-three-classes.png` was taken after it. 2a's signature was
  the opposite: not one further transfer arrived across fourteen cycles.
- So the open question from stage F (does the refusal cause the 2a wedge or
  merely precede it?) now has a differential: the refusal alone does not
  wedge Windows 2000. That does not acquit it on Windows 98, and this is one
  observation on one target; but "the refusal is fatal" is no longer the
  simplest reading. The site still carries no sub-counter, the "eleven
  `OpenRefusals++` sites, three sub-counters" gap hit in the wild for the
  second time.

The shutdown clause was then replicated on a third boot with no churn at all,
so nothing the harness was doing can be implicated in the shape. Extension
`8183F92C` (moved from `8184292C`, as stage D says bases do), three classes
attached, `LOAD.BAT` running, gate 10,410 transfers/s:

| | first shutdown | replication |
|---|---|---|
| teardown after the commit | 10.4 s | 28.9 s |
| callback shape | `Suspend -> DisableInterrupts -> Stop`, `irql=00`, suspended-stop branch | identical |
| `suspends` / `teardowns` / `port teardowns skipped - suspended` | 1 / 1 / 1 | 1 / 1 / 1 |
| failure-shaped counters | all 0 | all 0 |
| identity across the teardown | 1,799,681 = 1,799,677 + 1 cancelled + 3 aborted | 754,079 = 754,076 + 0 + 3 aborted |
| transfers during the shutdown window | - | 102,694 |

Two shutdowns an order of magnitude apart in transfer count and 3x apart in
duration, both closing exactly, both with the same 3 aborts, one per attached
class. Record: `stageG-2b-shutdown-clean-*`.

Asset captured: `asset-2b-devmgr-three-classes.png`, one Device Manager frame
with `xHCI USB 2.0 Host Controller (xhci98)`, its USB 2.0 Root Hub and USB
Mass Storage Device, plus `USB Human Interface Device`, the `ASIX AX88772
USB2.0 to Fast Ethernet Adapter` and a `Generic volume`, none of them banged.

Two harness notes. `soak-11v.ps1` is not safe to leave running while anything
else drives the monitor: its liveness probe read an empty `info irq` reply
mid-run and threw *"whether the guest is executing was NOT established"* on a
guest that was alive (irq delta 122,740). Kill it once the population is
attached, or accept that the second client can end it. And `device_del` of a
`usb-storage` destroys its `-drive if=none` backend, so the re-attach after a
settled read needs a `drive_add` first; documented in both launchers.

### Run, 2a

Both clauses this stage owes pass on Windows 98: the orderly shutdown with
traffic in flight, and the restart after it. The shutdown is replicated on
two runs an order of magnitude apart in traffic. Host minis-w11p-ykm, scoop
QEMU 11.0.0, `qemu-win98-run-11v.cmd` unmodified, debug build, built `Aug 14
2026 00:36:27`, `MiniPortExtensionSize` `0001243C` = 74,812 = offsets
`SIZEOF` on every boot. Extension VA `C14668C4` on boot 1, `C14658C4` on
boots 2-3 (bases move, as stage D says). Record:
`out\phase11-stageG\stageG-2a-*`.

It took four boots against an estimate of two, and the first one is why: the
procedure written for 2a before the first boot cannot work as written (see
"The Windows 98 shutdown block" below). Boots 2-4 used a different traffic
source.

The stop (boot 2). Three classes on root ports 1/2/3, traffic from a
host-driven HID pump plus an Explorer file copy, gate 22 transfers/s. The
teardown arrived 1.6 s after the commit, in the measured shape, all at
`irql=00`:

```
cb SuspendController irql=00 a=C14658C4
cb DisableInterrupts irql=02 a=C14658C4
cb StopController    irql=00 a=C14658C4   (b=00000001)
teardown: the stop arrived on a suspended controller - PORTSC is unwritable
```

`suspends` 1, `teardowns` 1, `port teardowns skipped - suspended` 1. Every
failure-shaped counter 0. The identity closes across the teardown itself:
`submitted 7,638 = completed 7,635 + 0 cancelled + 3 aborted`, and those 3
aborts are one per attached class, the same witness 2b produced.
`EndpointStops` 6, every abort sub-counter 0. Trace corroborates to the unit:
`usb_xhci_xfer_start` 7,638, with 2,044 `usb_msd_cmd_submit`.

22/s is not 2b's 8,340/s. The Explorer copy had finished before the commit,
because `lifecycle-11v.ps1`'s own preamble (identity read, liveness probe,
traffic gate, pre-stop counter block) runs 60-90 s before it commits, and an
unthrottled 96 MB copy on this vehicle completes in about 20 s. So boot 2 is a
pass in shape and accounting and a weak one in load. That is what the
replication was for.

The restart (boot 3). Relaunched, booted, one `DriverEntry`, one `cb
StartController`, `init step=0x16` / `init status=0`, `MaxSlotsEn` 0x20. All
three classes re-bound with no wizard. 96 MB was copied over USB and the load
window moved 12,393 transfers. Detached and read twice at a settled bus,
byte-identical both times: `submitted 12,763 = completed 12,760 + cancelled
3`, gap zero, with `transfers aborted` 0, every abort sub-counter 0,
`TransfersNoOpped` 1, and 3 addressed = 3 slots enabled = 6 endpoints opened.
The same closing numbers 2b gave.

The shutdown clause was then replicated (boot 3, second half) with the
storage backend throttled so the write could not outrun the harness.
Re-attached with `throttling.iops-total=30`, the mechanism `STGF98.BAT`'s own
header names (*"without the throttle an unplug essentially never catches a
transfer in flight"*), which stretched the same 96 MB copy from about 20 s to
over 100 s and put the commit inside it. Gate 342 transfers/s:

| | first stop | replication |
|---|---|---|
| traffic gate before the commit | 22/s | 342/s |
| teardown after the commit | 1.6 s | 17.0 s |
| callback shape | `Suspend -> DisableInterrupts -> Stop`, `irql=00`, suspended-stop branch | identical |
| `suspends` / `teardowns` / `port teardowns skipped - suspended` | 1 / 1 / 1 | 1 / 1 / 1 |
| failure-shaped counters | all 0 | all 0 |
| identity across the teardown | 7,638 = 7,635 + 0 + 3 aborted | 24,439 = 24,433 + 3 cancelled + 3 aborted |
| transfers during the shutdown window | - | 3,190 |
| `AbortsBeforeStopped` | 0 | 1 |
| `EndpointStops` | 6 | 8 |
| `usb_msd_cmd_submit` | 2,044 | 6,266 |

Two shutdowns 15x apart in gate rate and 10x apart in duration, both closing
exactly, both with the same 3 aborts, one per attached class.

`AbortsBeforeStopped` = 1 is the reading that makes the replication stronger
than the first stop, and it is worth more than the gate rate. The gate says
the bus was busy in the twelve seconds before the commit; this says the
teardown aborted a transfer on an endpoint it had not yet stopped, i.e. the
abort caught something in flight rather than a parked interrupt IN. A first
stop with all abort sub-counters 0 and a replication with this one set is the
difference between "three classes each had something outstanding" and "the
stop landed on a live write".

The stop-time counters were read out of the stopped guest on both, through
the `-no-shutdown` window: Windows 98 ends at `paused (shutdown)`, and
`x/Nwx` reads the block. It is the next start that zeroes it.

The armed-callback clause is attempted and not entered, as on 2b. Boots 2-4
ran `-NoChurn` (see below); boot 1 did churn, 122 attach/detach pairs across
a 420 s window, of which the counters show six actually enumerated (`devices
addressed` 9 = 3 classes + 6 hubs, `devices torn down` 6). `commands
abandoned` and `RH operations retired by the quiesce` both stayed 0 and
neither trace line appeared. So 2a independently reproduces 2b's finding that
the plug rate the driver sees is set by the guest's PnP path, not by the
churn rate. On 2a the churn does reach the driver, which 2b's did only once.
It still did not enter the window.

### The Windows 98 shutdown block

Found on boot 1. This is a vehicle limitation, not a driver defect, and it
invalidates the original procedure for 2a only.

Windows 98 raises a modal box for every running DOS program when you shut
down (*"You must quit this program before you quit Windows"*, one per box),
and `STGF98.BAT` leaves eight of them: one disk worker, five ping workers,
the launcher, plus any validation worker. Worse, its disk loop is `goto
diskloop` with no bound, so the box marked *"Closing - MS-DOS Prompt"* never
yields and the shutdown stalls there indefinitely. Boot 1 fired the stop into
a bus doing 1,909 transfers/s and got no `cb StopController` in 420 s.

The Enter was delivered and the shutdown did start; that is measured, not
assumed: the guest went on to raise Windows 98's shutdown-time close-programs
dialogs, which only appear once shutdown is under way. `lifecycle-11v.ps1`'s
own report says *"which on Windows 98 is what a swallowed keypress looks
like"*; on this run that sentence is wrong, and a leg that accepted it would
have recorded the wrong cause. A tool's built-in explanation for a negative
is a hypothesis, not a reading.

`STAGEF.BAT`'s ten-pass bound was removed because a finished stream cannot
supply a stop that lands on traffic, which is true on 2b. On 2a the opposite
holds: an unfinished stream cannot supply a shutdown at all. The one change
that made this clause reachable on Windows 2000 is what made it unreachable
on Windows 98.

So on 2a the traffic must come from something Windows will close by itself.
What worked: an Explorer file copy (a Windows program, which Windows closes at
shutdown instead of prompting) against a throttled storage backend, plus
`hidpump-11v.ps1` driving HID from the host, which costs the guest no process
at all. `vm\xfer98\BIG96MB.BIN` is staged for this and is the instrument; it
is not a `.BAT` and the staging line does not copy it.

And a fatal 0E, which boot 1 could not attribute. During boot 1's blocked
shutdown the guest took `0028:C002FF2A in VXD NTKERN(01) + 0000E32E`. The
driver's own instruments exonerate it as the thing that stopped: `OpenRefusals`
0, `CommandFailures` 0, `EndpointHalts` 0, `TransfersAborted` 0, no
failure-shaped counter moved, and `HealthPolls` = `CheckCallbacks` = 23,140, so
usbport was still calling this miniport at the end.

The guest kept executing behind the box (timer IRQ about 181/s, our IRQ 11
still being serviced at about 5/s, IDE IRQ 14 frozen), and a second full dump
20 minutes later showed 224,569 further transfers with not one failure-shaped
counter moving and the identity gap constant at 3. Boot 1 was the only run that
churned and also the only run that crashed, a coincidence of one; boots 2-4 ran
`-NoChurn` to keep it out of the clause.

The negative control (churn with no shutdown) was spent at stage H's H2 and
came back positive: the churn wedges this guest with no shutdown involved.
150 `usb-hub` add/remove pairs on free root port 4 over 415 s against a
populated bus (boot 1's window was 122 pairs over 420 s), at four times the
enumeration rate that reached the driver (25 against boot 1's 6). The guest
stopped: IDE IRQ 14 frozen at 24,986 from the end of the churn onward, guest
clock stopped at 9:24, no bugcheck. A silent wedge rather than boot 1's fatal
`0E`, but the same IDE signature. The full record, with the two-sample
tables and the counter diffs, is in stage H's H2 run below.

That control then had a control of its own, spent by roadmap task 12.5: the
churn wedges Windows 98 only when this driver carries it. The same churn on
the same boot, carried by Windows 98's own UHCI stack, ran 122 enumerations
with the guest still responsive; carried by `xhci98.sys` it wedged at 12. So
"the driver is exonerated as the thing that stopped" stays true and stops
being the interesting sentence: the instruments still say this miniport is
being serviced normally while the machine dies, and moving the identical load
off it removes the death. Whether the fatal `0E` and the silent wedge are one
failure or two is still open. Written back to roadmap task 11-V.1 and
`lessons.md`, "Windows 98 blocks its own shutdown on every running DOS
program".

One reading fell out of the 2a leg free: `Reply from 192.168.1.10x TTL=64` on
the guest console, the Ethernet ping-reply count stage F recorded as not
taken. The ASIX was working.

### Restart after controller invalidation

Not reached on either leg. Nothing in this vehicle injects an invalidation
and none occurred naturally during the stage, so per the rule above it is
recorded as not reached, not as passed. The release notes do not list
it.

---

## Stage H - task 11-V.9's run tail, and it is the last boot this batch needs

One boot per target, and it is small on purpose. Everything else in task
11-V.9 was done host-side; what a guest has to say is four things, one of
which could be a shipping blocker.

H4 exists because the task's stop rule points at "the sustained-load stage"
for its evidence, a stage that ran before the producer set existed. Without
H4 the rule would have been left permanently unmeasurable while looking
complete. It rides H3's boot and costs no extra one.

2a runs first, then 2b. 2a carries the only clause here that can fail hard,
and a failure there is a defect to fix rather than a reading to write down.
Spending 2b's boot first would mean finding that out one boot later with
nothing gained, because 2b's result does not depend on 2a's and would still
be valid after a fix.

Read the "Before the first boot" items at the top of this sheet again before
starting, because this task moved the extension layout. `SIZEOF` went 74,812
-> 87,488. Both offset tables were regenerated and `out\media-11v\` was
rebuilt; what is owed here is the check. The session's first trace must read
`MiniPortExtensionSize = 000155C0`, and a mismatch means the guest is running
a binary the tables do not describe. Stop and re-stage rather than reading a
single counter through the wrong table.

What changed since stage C, in one line each, because this stage is stage C
re-run against two values rather than a new procedure:

- `XhciLogEnable` is gone. Setting it does nothing.
- `XhciLogFile` (`REG_SZ`) is the file's path and its enable.
- `XhciLogDebugView` (`DWORD`) emits the ring through `DbgPrint` at the flush.
- The file sink is honoured only where the path probe resolved a root under
  the write mask, which on Windows 98 is nothing. So 2a is expected to
  decline, and to say so.

Stage C's two-transition rule is unchanged and still governs: the values are
read at start, the log is handed over at stop, so an edit needs a start, then
bus activity, then a stop. And on Windows 98 do not reach the start by
reinstalling; the INF's `AddReg` would write both values back to their
defaults.

### H0 - getting the new binary onto both guests, and why not by installing it

Both guests are running a driver that predates this task, so H1 and H2 have
to start by replacing it. Installing the rebuilt `out\media-11v\new-1.0.0.3-*`
is the wrong route on both targets, for reasons stages B and C measured:

- On Windows 98 an upgrade over an existing install bugchecks at
  `0028:C00312EE` (stopping the running driver is that fault), and stage B2
  measured that the registry phase never runs afterwards. So an install would
  deliver the binary and not the two new values.
- On Windows 2000 the package cannot upgrade itself (stage B2, declined four
  ways), and the remedy, deleting the cached `oemN.inf`, costs more of the
  session than this stage is worth.

So do it the way stage C's own note says the flush can be measured: put the
`.sys` in place and type the values in by hand. The driver starts with
logging disabled, and an absent value, a failed read and an explicit `0` all
leave it that way, so there is no runtime state that a fresh install reaches
and a hand edit does not. That holds for these two values as it held for the
one they replace. On these guests the values will be absent rather than
present, so create them.

| | Step |
|---|---|
| 1 | Copy the rebuilt `xhci98.sys` (debug flavour; this stage wants the trace) to the guest's transfer volume and over `C:\WINDOWS\SYSTEM32\DRIVERS\xhci98.sys` (2a) / `C:\WINNT\SYSTEM32\DRIVERS\xhci98.sys` (2b). A loaded `.sys` can be overwritten in place (Phase 3 task 8 measured that on 2a) and the running image is unaffected until the next start |
| 2 | Reboot, and read the first trace: `MiniPortExtensionSize` must be `000155C0` |
| 3 | Only then start H1 or H2 |

The INF's own delivery of the two values is therefore not exercised by this
stage, and that is a gap. What proves the engine writes them is a fresh
install on a clean snapshot, which is stage B3's clause, and B3 ran against
`XhciLogEnable`. Re-running it costs a clean snapshot and a full stage B,
which this tail is not worth. The INF gate's `VAL-*` and `VAL-SZ` rules stand
in: they check that the INF says it on both paths, and the engine's half is
owed the next time stage B is run for any other reason. Record it as owed
rather than reporting the values as install-delivered.

### H1 - 2a (Windows 98): the interlock, and the DebugView sink

This is the boot that could produce a shipping blocker, and the thing it is
watching for is a machine that does not finish starting.

| | Step | Reading |
|---|---|---|
| 1 | Create `XhciLogFile` (`REG_SZ`) = `C:\XHCI.LOG` and `XhciLogDebugView` (`DWORD`) = `1` in the driver key stage B3 recorded | the file value is set to something plausible on purpose: the clause is that the driver declines it, and a value nobody set would prove nothing. Create rather than edit; see H0 |
| 2 | Start DebugView v4.64 with *Capture Kernel* and *Log to File*, then reboot | |
| 3 | Watch the boot itself. It must reach the desktop | a splash screen with the CPU live and no progress is the failure this clause exists to catch; it is what 11-V.7 measured when a write-mask open was asked of an unresolvable root. If it happens, the run has found a shipping blocker: capture it, do not retry, and stop |
| 4 | Read the counters | `log file requested` 1, `log file sink enabled` 0, `log file status` 8 (`XHCI_LOG_FILE_NO_ROOT`), `log path probed` 1, and all three `log path write status` values non-resolving |
| 5 | | `log DebugView sink enabled` 1, `log enabled` 1: the log runs on that target through the other sink |
| 6 | Plug and use a device | `log records appended` climbing |
| 7 | Stop: shut down. Leave DebugView capturing across it | |
| 8 | Read the capture | the ring's records are in it, ending with the counter block, and its first line is `log.file.status=00000008`, the driver saying it declined rather than failing silently. (`log.file.path` is absent, because there is no path: on this target that record is the one that does not get written) |
| 9 | Boot again and look for `C:\XHCI.LOG` | it must not exist. Nothing was opened |

Step 8 is the interlock's own witness. "No file appeared" is also what a
broken log looks like; "no file appeared and the driver said which refusal it
made" is the clause.

The Windows 98 bare-metal observation belongs to Phase 13 and must not be
reported here; what task 12.2 owns is the machine-free decision about whether
there is a channel to observe at all. What this stage can establish is that
the DebugView sink is reachable on that target. Whether it is safe at
real-hardware interrupt rates is a different claim and no VM can make it.

### H2 - the ride-along, on the H1 boot above

Batch 11-V's one unspent negative control, and it belongs to task 11-V.1, not
to this one. Run the same hub churn stage G's boot 1 ran, let it complete,
and do not shut down. It must not displace a clause above; if the two
conflict, it waits for another boot.

- A crash implicates the churn independently of the shutdown.
- No crash breaks the coincidence: the record becomes "the churn alone does
  not do it".
- Either way it is written back to stage G above and to `lessons.md`'s task
  11-V.1 entry. A boot that skips it leaves it open, which is an acceptable
  outcome and must be said rather than quietly dropped.

Note the ordering it needs: the churn has to run before H1 step 7's shutdown,
or on a second boot of its own. A churn after the stop has no driver to
churn.

### H3 - 2b (Windows 2000): a file at a path the user chose

Run this after H1 and H2. Its result does not depend on theirs.

| | Step | Reading |
|---|---|---|
| 1 | In the driver key stage B3 recorded, create `XhciLogFile` (`REG_SZ`) = `C:\LOGS\XHCI.LOG` and `XhciLogDebugView` (`DWORD`) = `1`, and create the `C:\LOGS` directory | create, not edit (see H0): this guest's install predates both values |
| 2 | Start: disable and re-enable the device | |
| 3 | Read the counters before going further | `log file requested` 1, `log file sink enabled` 1, `log file status` 1 (`XHCI_LOG_FILE_ON`), `log DebugView sink enabled` 1, `log file path length, characters` 20 |
| 4 | Plug and use a device; a HID device and a storage copy are enough | `log records appended` climbing |
| 5 | Stop: disable the device | |
| 6 | Read `C:\LOGS\XHCI.LOG` | it exists, at the path that was asked for and not at `C:\XHCI.LOG`, and its first line is `log.file.path=\??\C:\LOGS\XHCI.LOG` |
| 7 | Read the file's body | the tier-2 records are there by name: `port.connect`, `port.reset.begin`/`port.reset.done`, `slot.enabled`, `slot.addressed`, `slot.route`, `ep.open`, and the counter block still ends it |
| 8 | Read the DebugView capture over the same disable | the same bytes as the file. One drain feeds both sinks, so a difference between them is a defect and not a timing artefact |

Step 6 is the clause, and step 7 is the task. The file existing at a
user-chosen path is what proves the value is honoured; the records in it are
what the task exists for. Stage C's file could say how many devices were
addressed and never which one arrived, at what speed, on which port.

Then set `XhciLogFile` to something the driver must refuse (`logs\x.log`,
relative) and repeat steps 2-3 only. `log file status` must read 6
(`XHCI_LOG_FILE_RELATIVE`) with `log file sink enabled` 0, and `log path
probed` must stay 0: a value that cannot produce a usable path may not buy
three `ZwCreateFile` calls on the boot path. Put the good path back
afterwards.

### H4 - the stop rule's own reading, on the H3 boot

Task 11-V.9's stop rule says to publish what the log is for and what it is
not "if the producer set cannot be kept inside the ring's budget on the
sustained-load stage". That is stage F, which ran against a driver whose log
produced two lifecycle notes, so nothing re-measures it unless this stage
does. This is that measurement and it rides H3's boot.

The discriminating case is cycles, not sustained load, and that is a
correction of the stop rule's wording. The tiers were built so that nothing
fires per transfer: on a settled bus under load the producer set is silent
(no connects, no resets, no slot changes, and error completions budgeted).
The case that fills the ring is enumeration churn, where each cycle costs a
connect, a reset pair, a slot enable, an address, a route, a parent hub and
one record per endpoint. Twenty cycles across three classes is order 14 KB
against a 16 KB ring, so the answer is not obvious and is worth the reading.
A sustained-load run would have measured the quiet case and reported a
comfortable margin that says nothing.

| | Step | Reading |
|---|---|---|
| 1 | With the file sink on and the good path restored, run `soak-11v.ps1 -Classes "hid,storage,net" -Cycles 20 -ReadEvery 1` against monitor 55556 | the cycles pass as stage F measured them; this leg is borrowing stage F's traffic, not re-running its clause |
| 2 | Before stopping, read `log bytes dropped by the wrap` | 0 is the stop rule satisfied with a measurement. Nonzero is the reading that fires it |
| 3 | Stop, and read the flushed file's `flush.dropped` and `log.dropped` lines | they must agree with step 2: the file's own statement of whether it is the run or a window on it |

If it reads nonzero, do not grow the ring. That is the stop rule's actual
instruction: publish what the log is for and what it is not, in
`docs/using/release-notes.md`, in the shape 11-V.7's limitation took. The
release notes already describe the bounded behaviour ("you get the most
recent part and a line saying how much was dropped"); a nonzero reading turns
that into a measured statement about a real workload, and it should then say
which workload wraps the ring, because "20 replug cycles" is something a user
can recognise and "16 KB" is not.

### Run, 2a (five boots)

H1 passes on every clause it can reach, H2's control is spent and comes back
positive, and H1 step 8 turns out to be unobtainable on this vehicle for a
reason worth more than the tick. Host minis-w11p-ykm, QEMU 11.0.0
(`v11.0.0-12122-ga4bb4b10c9`), launcher `scripts\local\qemu-win98-run-11v.cmd`,
monitor 55555, flavour debug, `built Aug 16 2026 16:13:43`,
`MiniPortExtensionSize=000155C0` = 87,488, equal to both offset tables'
`SIZEOF`, checked before a counter was read. Evidence in `vm\11v-stageH\`.

| Boot | What it was for | Outcome |
|---|---|---|
| 1 | H0 step 1 | old binary confirmed (`built Aug 14 2026 00:36:27`, ext `0001243C`); `CHECKED\XHCI98.SYS` copied over the loaded `.sys`, which is unaffected until the next start |
| 2 | H0 step 2 | `built Aug 16 2026 16:13:43`, ext `000155C0`: identity established. Both values created, in `Class\USB\0002` |
| 3 | H1, attempt 1 | both reads returned `MP_STATUS_UNSUCCESSFUL`. Wrong key, see below. No H1 clause executed, and in particular the interlock was not tested: `log path probed` 0, so no path was ever composed |
| 4 | H1 | steps 3-7 pass |
| 5 | H1 step 9, then H2 | step 9 passes, the interlock reading replicated, and the churn control spent |

H1's readings, boot 4, against the table above:

| Step | Expected | Measured |
|---|---|---|
| 3 | reaches the desktop | desktop; no splash-screen wedge |
| 4 | `log file requested` 1 | 1 |
| 4 | `log file sink enabled` 0 | 0 |
| 4 | `log file status` 8 (`NO_ROOT`) | 8 |
| 4 | `log path probed` 1 | 1 |
| 4 | the three write statuses | all 0, never asked; see below |
| 5 | `log DebugView sink enabled` 1 | 1 |
| 5 | `log enabled` 1 | 1 |
| 6 | `log records appended` climbing | 30 -> 54 on two class attaches |
| 7 | the stop | `Suspend -> DisableInterrupts -> Stop`, `irql=00`, suspended-stop branch; `log: flush wrote bytes=00000000`, `log: flush emitted bytes to DebugView=000007F6` (2,038) |
| 8 | the capture holds the ring | not obtainable, see below |
| 9 | `C:\XHCI.LOG` must not exist | `dir c:\xhci.log` -> `File not found` |

The three roots answered exactly as stage C measured them, on a new binary
and a moved extension layout: `\??\` -> `C000003A`, `\DosDevices\` ->
`C00000BB`, `\SystemRoot\` -> `C00000BB`. That is a replication.

The write-mask row: those three counters read 0 because no form was ever
asked for write access, not because a request resolved and failed. That is
stage C's shipping gate (ask the write mask only where the read mask
resolved) working on a live boot, and it is why the boot did not hang: the
request that hung stage C's boot 8 was never issued.

Key discovery cost this session a boot. Finding the driver key as "the subkey
whose `NTMPDriver` is `xhci98.sys`" only works on a clean image with one
install. This image has three such subkeys (`0002`, `0004` and `0009`)
because the same controller has been enumerated at three different PCI
addresses by different launchers in this batch (stage A adds an EHCI, stage D
a second xHCI; each shift mints a new devnode with its own driver key). That
rule silently selects the wrong one. The unambiguous form is the devnode's
own `Driver` value, which is what
`IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DRIVER)` returns:

```
[HKLM\Enum\PCI\VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01\BUS_00&DEV_03&FUNC_00]
"Driver"="USB\\0004"
```

cross-checked against `info pci` on the monitor, which put `id "xhci"` at bus
0, device 3, function 0. The mapping here was `DEV_03` -> `0004` (live),
`DEV_04` -> `0002`, `DEV_05` -> `0009`. B2's run above states the rule.

The diagnosis is proved rather than inferred: on an unchanged binary, moving
the two values from `0002` to `0004` took `log switch status` from 8 to 0 and
`log records appended` from 0 to 30, with `log records suppressed` going the
other way. `UsbPortGetMiniportRegistryKeyValue` had been answering correctly
throughout. Method note: the service's return collapses every failure to 8
(abi section 6, `neg/sbb/and 8`), so no further boot could have narrowed it;
what settled it was two registry branches exported to the floppy and read on
the host, which is also the way round the screendump-shows-what-is-painted
trap that made a third key look like a repaint artefact.

H1 step 8 cannot be taken on this vehicle, and that is the finding. DebugView
v4.64 was configured with *Capture Kernel* and *Log to File*, was
demonstrably capturing this driver's lines live (707 lines), and was still
open when the shutdown was commanded. Its capture contains zero occurrences
of the flush, the ring, `StopController` or `Suspend`, while the `0xE9`
trace of the same shutdown carries `flush emitted bytes to
DebugView=000007F6`. Windows closes DebugView before the driver's
`StopController` runs, so the 2,038 bytes went to a listener that was already
gone.

The consequence is larger than the missing tick. On Windows 98 the log ring
cannot be delivered to anyone: the file sink declines by design (the
interlock proved above), the DebugView sink emits too late, and the ring is
drained only at `StopController`, with a shutdown the only stop available,
since a Device Manager disable bugchecks this target (`0028:C00312EE`, Phase
3 task 8). So `XhciLogDebugView` is reachable on Windows 98 during operation
and not at the flush, which is the only moment it carries anything. That is a
sharper statement than stage C's "the log is a Windows 2000 diagnostic", and
it is published in `docs/using/release-notes.md` and carried into task 12.2.

The interlock clause is nonetheless met, by three independent witnesses
rather than the fourth the sheet named: `log file status` = 8 in the
counters, `flush wrote bytes=00000000` in the trace, and `File not found` on
the disk. "No file appeared and the driver said which refusal it made" is
satisfied.

Incidental, recorded so a reader does not think one of them is wrong: the
trace prints `log: path form chosen=00000001` while the counter block prints
`log path form in use=00000000`. Both are correct and they report different
quantities. The counter is the raw `PathForm` (`XHCI_LOG_PATH_NONE`, nothing
resolved); the trace line is `XhciLogPathToUse()`, which falls back to the
shipped `\??\` form. Moot on this target, since the file sink never runs.

#### H2: the NTKERN negative control is spent, and it comes back positive

The churn wedged the guest with no shutdown involved. Boot 5, after H1 was
complete so it could not displace a clause. 150 `usb-hub` add/remove pairs on
free root port 4 at 600 ms per operation, 415 s, directly comparable to stage
G boot 1's 122 pairs across 420 s, against a populated bus (HID at port 1,
storage at port 2). 0 refused by the monitor, and the guest did not survive
it.

| | stage G boot 1 (churn + shutdown) | H2 (churn, no shutdown) |
|---|---|---|
| pairs / window | 122 / 420 s | 150 / 415 s |
| actually enumerated | 6 | 25 (`SlotsEnabled` 2 -> 27, `DevicesAddressed` 2 -> 26, `DevicesTornDown` 25) |
| shutdown in the picture? | yes, blocked | no |
| outcome | fatal `0E` in `NTKERN(01)` | silent wedge: IDE IRQ frozen, clock stopped, no bugcheck |

The guest wedged during or at the end of the churn and never recovered. The
operator reported it minutes later. A screendump showed a painted desktop and
a first `info irq` sample looked healthy; a single sample is not a delta, and
a screendump shows what is painted. The measurements below were then taken
in the order the harness rules require (full counter dump first, because
`savevm` refuses on these launchers and a live wedge cannot be snapshotted;
then a second dump, because the diff is the evidence):

| | end of churn | wedge, sample 1 | wedge, sample 2 |
|---|---|---|---|
| IRQ 0, timer | - | 389,111 | 394,118, climbing |
| IRQ 11, ours | 1,069 | 1,485 | 1,485, frozen |
| IRQ 12, PS/2 | 1,668 | 1,668 | 1,668, frozen |
| IRQ 14, IDE | 24,986 | 24,986 | 24,986, frozen |
| guest clock | 9:24 | 9:24 | 9:24, frozen |

`IRQ 14` reads 24,986 at the end of the churn and is still 24,986 twenty
minutes later. IDE interrupts had already stopped while the desktop still
looked painted and healthy. That is also the signature of stage G's crashed
boot: the timer running, IDE frozen.

The driver is not what stopped, and its instruments say so, the same
exoneration shape stage G used: `HealthPolls` = `CheckCallbacks` climbing and
equal across both wedge dumps (87,203 -> 87,967 -> 88,305), transfers still
moving (549 -> 842 -> 966 submitted, completions tracking) with the
outstanding gap constant at 15, `CommandFailures` / `EndpointHalts` /
`TransfersAborted` all 0, `fatal controller status` 0. No bugcheck and no
fatal exception; unlike stage G's `0E`, this is a silent wedge.

So the churn can wedge Windows 98 with no shutdown involved. Stage G's
coincidence-of-one now has a second observation beside it, and this one
removes the shutdown from the picture. Every instrument this miniport owns
says it is servicing usbport normally while the rest of the machine is
stopped. That makes it not the thing that stopped; it does not make it
uninvolved, because the churn is delivered through it. The honest statement
is that 150 hub attach/detach pairs on this vehicle stop Windows 98 without
stopping this driver.

One misreading caught before it reached the record: the trace's trailing run
of `root hub: announcing a port change to usbport` looks like a live
announcement loop and is not one. `RootHubInvalidates` is frozen at 357
across both wedge dumps and the trace holds exactly 357 such lines. It is a
static tail, not a livelock. A repeated line at the end of a log is where the
log stopped, not necessarily what the machine is doing.

The driver's instruments are clean across it: `CommandFailures` 0,
`EndpointHalts` 0, `TransfersAborted` 0, `HealthPolls` = `CheckCallbacks` =
87,203, hub marking 22 descriptors folded with 0 failures. The non-zero
refusals are the known churn seam and not driver faults: `OpenRefusals` 4 /
`OpenRefusalsNoClaim` 3 / `TransfersFailedGone` 2, i.e. `EP0 open for an
address no record holds` reproduced on Windows 98, which stage F found on
2a's net refusal and stage G reproduced on Windows 2000. It was survived here
too, a third target-independent witness that the refusal alone does not wedge
a usbport.

A reading H4 wants, taken here for free: 25 hub enumerations appended 566
records (`Log.Appends` 54 -> 620) into the 16 KB ring with `Log.BytesDropped`
0 and `Log.Truncated` 0. It does not answer H4 (that is 20 replug cycles
across three classes on 2b, a richer per-cycle record set) but it is the
first measurement of the producer set under enumeration churn and it did not
come close to wrapping.

#### H2's control was spent by roadmap task 12.5, and it implicates this driver

The next step named above was a control against Microsoft's own stack. That
control has been run, on 2a, one boot, `built Aug 18 2026 11:58:03` / ext
`00015620` = 87,584. The identical churn wedges Windows 98 when `xhci98.sys`
carries it and does not when Windows 98's own UHCI stack does: 122
enumerations through UHCI left the guest responsive, 12 through this driver
wedged it, both legs on the same boot, same vehicle, same populated bus, same
`usb-hub`, same 600 ms interval. Reproduced twice on the xHCI side. Full
record at roadmap task 12.5; evidence `vm\12v5-legA\`, `vm\12v5-legB\`,
`vm\12v5-legA2\`.

Two of H2's own readings are corrected by that run, and both are method
rather than detail:

- "Mouse cursor still moving but no click accepted" was never evidence the
  guest was alive. PS/2 IRQ 12 sits frozen across the whole window on a guest
  the operator confirmed was fully responsive, and equally frozen on the
  wedged one. QEMU is not delivering mouse input to the guest at all; what
  moves on screen is the host cursor over an ungrabbed window. So IRQ 12 is
  not a wedge indicator in this vehicle in either direction. A moving cursor
  shows the host is alive, not the guest.
- The wedge is not a failure of interrupt delivery. Under active typing IRQ 1
  climbs (110 -> 242 on the task 12.5 leg) while the guest does nothing with
  the keys. Interrupts arrive and are acknowledged; nothing is scheduled off
  them. The stall is above the interrupt layer. Of the three IRQs the table
  above shows frozen (11, 12 and 14), two do not discriminate: IRQ 12 is
  frozen on healthy guests too, for the ungrabbed-window reason, and IRQ 11
  is ours, which the task 12.5 legs show still taking and claiming interrupts
  on a wedged guest (`InterruptsClaimed` 6,076 -> 6,244 on leg A'). The one
  that carries the signature is IDE IRQ 14, which moved on the healthy
  control leg and was frozen solid across every wedged sample.

### Run, 2b (four boots)

H3 passes every clause including step 8, and H4 fires the stop rule. Host
minis-w11p-ykm, QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), launcher
`scripts\local\qemu-win2k-run-11v.cmd`, monitor 55556, flavour debug, `built
Aug 16 2026 18:01:46`, `MiniPortExtensionSize=000155C0` = 87,488, equal to
both offset tables' `SIZEOF`, checked before a counter was read. Driver
Verifier confirmed: `verifier /query` lists `xhci98.sys, loads: 1`, `Level`
`0000001B` = special pool + force IRQL checking + pool tracking + I/O
verification. Evidence in `vm\11v-stageH\`, prefixes `h3-2b-*` and `h4-2b-*`.

| Boot | What it was for | Outcome |
|---|---|---|
| 1 | H0 | old binary confirmed (`built Aug 14 2026 00:36:27`, ext `0001243C`); new `.sys` copied over the loaded one; `C:\LOGS` created; both values created in the driver key found below |
| 2 | H0 step 2 + H3 steps 1-8 | identity established; H3 passes every clause, incl. step 8 |
| 3 | H3's tail, the relative-path refusal | passes, and `log path probed` stayed 0 |
| 4 | H4 | 60/60 cycles; the stop rule fires |

H3's readings against the table above:

| Step | Expected | Measured |
|---|---|---|
| 3 | `log file requested` 1 | 1 |
| 3 | `log file sink enabled` 1 | 1 |
| 3 | `log file status` 1 (`XHCI_LOG_FILE_ON`) | 1 |
| 3 | `log DebugView sink enabled` 1 | 1 |
| 3 | `log file path length` 20 | `0x14` = 20 |
| 4 | `log records appended` climbing | 31 -> 55 on a HID and a storage attach |
| 5 | the stop | a Device Manager disable; `flush wrote bytes=0000083B`, `flush emitted bytes to DebugView=0000083B` |
| 6 | the file at the chosen path | `C:\LOGS\XHCI.LOG`, 2,107 bytes = `0x83B`; nothing at `C:\XHCI.LOG` |
| 6 | first line `log.file.path=\??\C:\LOGS\XHCI.LOG` | exactly that |
| 7 | the tier-2 records by name | `port.connect`, `port.reset.begin`/`.done`, `slot.enabled`, `slot.addressed`, `slot.route`, `slot.parenthub`, `ep.open` (+`ep.open.rate`), counter block last |
| 8 | the same bytes in both sinks | identical, see below |

The three root forms all resolve here, which is the other half of stage C's
matrix and the mirror of 2a: `\??\`, `\DosDevices\` and `\SystemRoot\` each
answered `C0000034` `STATUS_OBJECT_NAME_NOT_FOUND`, which `xhci_log.h:214`
defines as the form resolving with only the file absent. The write mask was
asked for here and answered the same, the request 2a's interlock correctly
never issued.

Step 8, which 2a could not take, is taken and it passes. The flush block in
the DebugView capture (`h3-2b-dbgview-capture.log` lines 820-917) and the
file agree character for character, 1,927 payload characters, and the
driver's own counters say it handed `0x83B` = 2,107 bytes to each sink. The
only differences are DebugView's own rendering, and both are worth naming so
a later reader does not mistake either for a defect: it shows the line
terminator as a trailing space rather than CRLF, and it chops long `DbgPrint`
bursts across its capture buffer (`hc.` + `maxintrs=00000010` on two rows).
Compare the payloads with whitespace stripped, not the lines.

The tier-3 producers fired for real, unplanned: `xfer.error=00010106`,
`ep.halted=00000001`, `ep.recovery=00010801`. The change-gated halt/recovery
path is therefore exercised rather than only argued.

The relative-path refusal (H3's tail) passes on every clause. With
`XhciLogFile` = `logs\x.log`: `log file status` 6 (`XHCI_LOG_FILE_RELATIVE`),
`log file sink enabled` 0, `log path probed` 0, all six path-status counters
0, `log file path length` 0. A value that cannot produce a usable path bought
zero `ZwCreateFile` calls on the boot path.

The key-discovery trap is live on this target too. This image holds two
devnodes for `VEN_1B36&DEV_000D`, `2&ebb567f&0&18` and `2&ebb567f&0&20`
(Windows 2000 encodes devfn, so `0x18` = device 3 and `0x20` = device 4).
`info pci` put `id "xhci"` at bus 0 device 3 function 0, and that devnode's
own `Driver` value reads `{36FC9E60-C465-11CF-8056-444553540000}\0023`,
cross-checked by that key also carrying this driver's `DriverDesc` and
`InfSection`. `0023`, not `0002` as the release notes' example key says.

The shutdown route is still dead here, replicated on the new binary. An
ordinary shutdown taken between boots produced `flush create
status=00000000` but `flush write status=C0000189` `STATUS_TOO_LATE` ("a
write operation was attempted to a volume after it was dismounted"), `flush
wrote bytes=00000000`, and no file at all afterwards, not even a zero-length
one. This is stage C's own reading reproduced against the producer set and
the moved layout; what it adds is that H3 specifying a Device Manager disable
is essential rather than incidental.

#### H4: the stop rule fires

20 cycles x `hid,storage,net` on the H3 boot, `soak-11v.ps1 -Cycles 20
-ReadEvery 1`, 60/60 completed, 0 failed. The run itself is faultless and is
not what fires the rule: the settled reading is `submitted = completed +
cancelled` exact across two samples with the bus empty (3,866 / 3,805 / 61,
gap 0), 60 devices addressed, 120 endpoints opened, and every health counter
0 (endpoint halts, EP0 opens refused, transfers refused ring-full, DMA
failures closed, command timer failures).

| Step | Expected | Measured |
|---|---|---|
| 1 | the cycles pass as stage F measured them | 60/60, 0 failed |
| 2 | `log bytes dropped by the wrap` | 5,633 (`0x1601`) live, before the stop: NONZERO |
| 3 | the file's `flush.dropped` and `log.dropped` agree with step 2 | `log.dropped` 6,168, `flush.dropped` 6,341, `flush.truncated` 0; see below |

On what "agree" can mean here, because the three numbers are not equal and
the mechanism forbids them being equal. They are three readings of a still
filling ring taken at three different moments, and they are monotonic as
they must be: the live read (5,633) < the counter block's own snapshot
written into the ring (6,168) < the flush's final figure (6,341), the last
two differing because composing the counter block pushed more records out.
What agrees is the answer: nonzero, same magnitude, about 6 KB, and that is
what the clause turns on. Do not record this as an equality; a later reader
who expects one will think an instrument is broken.

What the 16 KB the ring kept actually contains, counted rather than
estimated: the flushed file is exactly 16,384 bytes / 698 lines, holding 42
enumerations (42 each of `slot.enabled`, `slot.addressed`, `slot.route`,
`slot.parenthub`; 41 `port.connect`; 84 reset pairs; 102 `ep.open` with 102
`ep.open.rate`). So about 14 replug cycles across three device classes fill
the ring, and 20 cycles lose roughly a quarter of what they produced. The
file's own `xfer.submitted`/`completed`/`cancelled` are 3,866 / 3,805 / 61,
identical to the soak's settled reading, so the log and the counters are
demonstrably the same run.

The wrap takes the header first, which contradicted a published claim. H4's
file has no `log.file.path`, no `log.file.status`, no `start=`, no `hc.pci`;
it begins mid-record with a bare newline and `port.reset.begin=`. The header
records are written once at the start, so they are the oldest and the first
the wrap discards. `docs/using/release-notes.md` had promised "the driver
records the path it actually chose as the log's first line" without
qualification; that holds only where the ring did not wrap. H3's 2,107-byte
file has the line, H4's 16 KB file does not. The release notes now say so.

The stop rule's instruction is therefore in force: do not grow the ring.
Publish what the log is for and what it is not, naming the workload, which is
what "about 14 replug cycles" is for.

---

## Asset capture

Phase 11 doubles as asset capture, and a screenshot taken during a run is free
while recreating it costs the run again. Capture as you go:

- The Device Manager entry on each target, named and without a bang: the
  single most legible proof this project works.
- Driver tab and Driver File Details on each target, which are also stage B's
  own evidence.
- A device working that a viewer recognises: a flash drive with a drive
  letter, an Ethernet adapter with an IP address, audio playing.
- The hub tree on the deepest topology stage E reaches.
- The counter block at the end of the stability run. The numbers are the
  claim, and a screenshot of them is what a result cites.

Screendumps are PPM, not PNG, and a screendump shows what is painted, which
is not the same as a guest that is responding.

---

## Recording results

One directory per session under `vm/`, holding the traces, the counter dumps
and the screenshots, with a note naming: the guest, the flavour and build
stamp of the binary, the `usbport.sys` build, the QEMU command line, and
which stages were attempted. A result that does not name its binary cannot be
compared with the next one, and Phase 11's results cite two flavours.

Roadmap batch 11-V's checkbox is not ticked on "it did not crash": each stage
above has a reading, and a stage whose reading was not taken is a stage that
did not run.
