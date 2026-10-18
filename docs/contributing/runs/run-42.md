# Run 42 - The Windowed Installer, the Uninstall, the 98 SE and ME Device Install

Phase 42 of `roadmap-usb4.md`, for release `3.1.0.0`, opened by the owner
on 2026-10-18. This sheet records what each task did and every reading;
the roadmap's task list and decisions table are the current state.

## 42-0.1 and 42-0.2: design records 26 and 27 (2026-10-18)

Written on branch `3.1.0.0` from the owner's decisions of 2026-10-18 (the
decisions table, "Phase 42, the open choices"), before any code:

- Design record 26, `docs/contributing/design/26-windowed-installer.md`:
  the wizard over one engine shared with the console tool, its pages, the
  components (the bundled Stor98 only when ticked, with the record the
  uninstall reads), the settings written to the machine-wide key of task
  42-A.17, the 98 SE and ME steps page, the Add/Remove Programs entry and
  the uninstall's relaunch from a temporary copy, the WOW64 self-start.
- Design record 27, `docs/contributing/design/27-win9x-device-install.md`:
  the 16-bit helper over the setup engine's documented device installer,
  what counts as documented (the DDK header's own documentation blocks and
  the DDK's samples; the 98 DDK's help file carries no page for these
  routines, and the Windows 95 DDK is not at hand), the call sequence, the
  devices in order with Windows' own wizards waited for, the removal, the
  toolchain and the cut's prerequisite.

What was read for them, on development host A: the Windows 98 SE DDK's
`inc16\setupx.h` and `lib\win98\setupx.lib` (`tools\win98ddk-extracted\`,
their sizes and SHA-256s now in `docs/references/README.md`), the DDK's
`HELP_WIN98DDK.CHM` (extracted with 7-Zip and searched: no device-installer
page), the DDK's `net\ipnew` and `plugplay\custom` samples, and the Open
Watcom 2.0 install at `C:\WATCOM` (its 16-bit compiler, `wlink`, `wrc`,
`h\win\windows.h`, `lib286\win\windows.lib`). Both records were
reviewed and revised until the review converged, the same day (record
26 at draft 7, record 27 at draft 6); their status lines record what
each revision changed.

Owed to the code tasks: record 23's revision (record 26 section 12), the
section 2a provenance entry for `XHCIDI.EXE` with its first build (record
27 section 3.4), and every reading of record 27 section 12 on the 42-V.1
legs.

## 42-0.4 to 42-0.7: the connection-manager designs (2026-10-18)

Written on branch `3.1.0.0` the same day, after records 26 and 27, from the
owner's decision of 2026-10-18 that the safe-removal mark's writer is a
privileged NT service (the decisions table, "Phase 42, the open choices")
and from the task texts; each as a revision of the record that owns the
mechanism, except the one that needed a record of its own:

- 42-0.4, design record 21 section 7.4, revision 3: the mark kept across a
  stack rebuild - a device-specific `LowerFilters` value on the unit,
  written by the service `xhcimark` (`XHCIMARK.EXE`, `XHCIMK64.EXE`) from
  what the driver publishes through a fourth query-device request
  (`IOCTL_XHCICM_MARKS`), validated against the devnode tree; the child
  filter also created by `AddDevice`, never failing it; the first plug's
  documented, veto-respecting restart; the value removed at departure and
  at the service's reconciliation; the installer's install, update, stop
  and removal with the marks removed before the filter hosts and `xhcicm`'s
  service and file kept while a value may remain.
- 42-0.5, design record 22 section 7.7, revised: the recovery of a
  contained USB4 host interface in place - the precondition on reads
  (the header as the healthy start read it, the two Command bits
  restored, a changed BAR0 refused, the host interface's version and the
  selection's tests again, the size register programmed or reset), the
  kept buffer reused only after a verified stop of the rings in it, the
  port records recovered as section 7.6's resume obligations through
  section 9.2's discover-and-preserve with no reset, one RESYNC "controller
  recovered"; decision U17 revised by the owner's promotion of the work.
- 42-0.6, design record 28, `28-win9x-no-interrupt.md`: an NVMe function
  behind a tunnel whose interrupt pin `PCI.VXD` cannot route started
  through a second basic logical configuration without the IRQ descriptor
  (the configuration manager's log-conf services from the 98 worker, then
  `Reprocess_DevNode`), the forced form as the leg's fallback, the pin-0
  function needing nothing, the pairing with Stor98's polling mode,
  `XhciCmNoIrqStart` and `XhciCmNoIrqDiag`.
- 42-0.7, design record 21 section 7.2, "The 9x half": the tools' 9x eject
  asked of the connection manager through `XhciCmEjectAsk` and
  `XhciCmEjectResult` and run by the worker with Stor98's handshake, the
  USB4 per-port teardown after a clean removal and the isolation before a
  removal after an answer that is not clean; the 9x statement through
  `PCI.VXD`'s dword write of the SHPC bridge's DWORD Select on the worker,
  posted by the thread with the 2 s bound, 98 SE and ME admitted one at a
  time on their G3 readings; the per-member owed removal on the thread and
  the per-member state machine on the worker in place of `CmNvlinkAwait`
  and the worker's holds, with interface 3's R6f handoff word.

What was read for them: the Windows 98 SE DDK's `CONFIGMG.H`, `CFGMGR32.H`
and `PCI.H` (`tools\win98ddk-extracted\incs\`) and its help file
(`HELP_WIN98DDK.CHM`, extracted into a scratch directory: no Configuration
Manager page among its 1,548), the Windows 2000 DDK's `SETUPAPI.H` and
`cfgmgr32.h`, `src\cm\stor98cm.h` (interface 3's R6 to R12), Stor98's
`README.md` and `docs/PLAN.md` at its `audit-2026-10-18` head, and the
connection manager's sources (`cm_cfgwk.c`, `cm_nvlinkos.c`, `cm_reenum.c`,
`cm_filt.c`, `cm_nhi.c`, `cm_live.h`, `tbt_u4cm.h`, `tbt_brnum.c`,
`cm_statos.c`, `cm_query.h`). Records 24 (revision 6), 25 and 26 gained
the pointers.

The two record 21 designs were reviewed and revised the same day until
the review converged: 42-0.4 over nine rounds (the first draft's kernel
registry lookup and location-based roles withdrawn; the service's
binary moved to `system32`; a restart latch tried and withdrawn in favour
of the eject posted on a positive confirmation; Windows 2000's problem
24 refused as a confirmation), 42-0.7 over six rounds (the transactions
moved from the worker to the thread with one call worker per member; the
detached block and the persistent member blocker; acceptance as the
ownership boundary of an ask; O6 under the firmware connection manager
left as an open interface exception). 42-0.5 converged over six rounds
(the first draft's recovery transaction restructured: the recovery
restores and commits, every rebuild and publication afterwards going
through the ordinary machinery behind a per-member retirement barrier;
an immutable snapshot per containment; nothing built or published
before a guarded commit; decisions U18 to U29). 42-0.6, design record
28, converged over eleven rounds at draft 11 (the candidate's ownership
by Stor98's binding and version; a journal of the configurations this
driver created, never recognised by priority and never freed; the work
as a per-member transaction on the thread with the member's call worker,
its own admission window independent of safe eject; the descendant walk
below the member; every hypothesis about `PCI.VXD` marked as one;
decisions N1 to N10). With it every design of Phase 42 has converged
before its code, as the checkpoint asks.

## 42-A.1: XHCIDI.EXE, first build and the 98 SE smoke readings (2026-10-18)

Branch `p42-di`. The helper of design record 27 (`xhciinst\di\`), its
pure part and host suite `test\test_di.c`, its NE import gate, its
staging in `make-release.ps1`, and the legs' test driver `DITEST.EXE`
(`xhciinst\di\test\`), committed as `8a8d276b` and `0d5381c6`; reviewed
and revised over two rounds, the second clean at every priority. The x86
gated build (`build-driver.cmd all`) passed on `8a8d276b` before the
review: host tests 2080 checks, 0 failures (`test_di` 376 since
`0d5381c6`), the helper's self-test (25 imports by ordinal from KERNEL,
USER and SETUPX, each the allowlist's, every refusal case of the gate,
the stamp). The full gated build of both architectures after the loop
and the 42-V.1 legs were **not run**: two smoke readings on a 98 SE
guest, taken before the legs to test the record's two untested
foundations, stop the task for a decision (the coordinator's brief:
stop and report when a SETUPX routine does not behave as the record
assumed).

The guest: a fresh overlay of `vm\t26\win98-clean.qcow2` (98 SE with
NUSB, no xhci98) on `vm\usb4\bin-2a893ea`, i440FX, one vCPU, the
emulator's Thunderbolt 3 layout; QMP 47980; the kit (the staged
`release-x86` package, `XHCIDI.EXE`, `DITEST.EXE`) copied to `C:\XHCI98`.
Readings by `DITEST.EXE` over COM1 into the host's serial log.

| Reading | Result |
|---|---|
| probe | `XHCIDI 3.0.0.0`, `JOB probe`, `RESULT probed`, `END 3`; END read and the process handle signalled 49 ms after the start, `GetExitCodeProcess` 0. The image loads on 98 SE (its ordinals resolve) and the handle of a 16-bit process **does** signal at its exit here |
| hwnd | A 32-bit window's handle (844, 1048) passed in decimal is accepted by the helper's `IsWindow` |
| A devnode whose Found New Hardware wizard was **cancelled** | Not in the devnode tree for the rest of that boot (`cfgmgr32` walk; its `Enum` entry stays, `Class` `Unknown`), so the helper answers `FOUND 0`, `RESULT not-present`. Taking the wizard to its end with no driver ("Windows has not installed a driver") leaves it present at problem 28, class `Unknown` |
| **The command line** | `CreateProcessA` fails with error 31 when the helper's command tail is 127 characters or more (126 worked, 127 failed, the same job with a four-digit hwnd for a three-digit one): the MS-DOS command tail of a 16-bit program. A PCI device id alone is 66 characters, so record 27 section 4.1's line (version, job, result path, hwnd, device id, INF path, flags) does not fit for a controller in the install directory `C:\XHCI98`, let alone a USB id with a serial |
| **`DIF_SELECTBESTCOMPATDRV`** | On the controller at problem 28 with `xhci98.inf` (tail kept under 127): `DiGetClassDevsEx` 0, `FOUND 1`, `DiGetINFClass` `USB` 0, `DiCreateDeviceInfo` 0, `DiBuildCompatDrvList` 0, then `DiCallClassInstaller(DIF_SELECTBESTCOMPATDRV)` returned **506, `ERR_DI_DO_DEFAULT`**, with `DI_NODI_DEFAULTACTION` clear: the dispatcher does not perform a default for this function (record 27 section 5.2 assumed it would) |
| With a selection of the helper's own (an experimental build, not committed: the lowest-rank node of `lpCompatDrvList`, the network sample's `lpSelectedDriver = lpCompatDrvList` shape) | `DRIVER XHCI98.INF Xhci.Dev "xHCI98 USB 3.x eXtensible Host Controller" rank 2005`, `NODE 00000040`; `CtlGetLddPath` `D:\WIN98\` 0, `CtlSetLddPath C:\XHCI98` 0; **`DiCallClassInstaller(DIF_INSTALLDEVICE)` 0** (its default install did run): `xhci98.sys` copied from `C:\XHCI98`, the Windows CD prompt for `usbd.sys` with "Copy files from" `D:\WIN98` (the setup source, not the `LDID_SRCPATH` set), answered with the CD's path; `FLAGS 00010111` (`DI_NEEDREBOOT`), `RESULT installed-restart`, 114.6 s with the prompt; `Enum` key `Class` `USB`, `Driver` `USB\0002`, the driver key with `NTMPDriver` `xhci98.sys`, `DevLoader` `*NTKERN`, every `Xhci*` default, `InfPath` `YEOKHE~1.INF`, `InfSection` `Xhci.Dev`; the devnode at problem 2 until the restart |

What follows from them is for the owner and the coordinator (the
report of 2026-10-18): the helper's interface needs a carrier other than
the command line for the job's operands, and section 5.2 needs a
selection for `ERR_DI_DO_DEFAULT`. The legs owed are all of record 27
section 12 on 98 SE and ME, after that decision.

## 42-A.1: the redesign, its review, the gated build and the helper legs (2026-10-19)

The owner's decisions on the smoke readings (2026-10-18): the job's
operands in a job file, `XHCIDI.EXE <version> <job file>`, and the
helper's own choice of the best-ranked node when the dispatcher answers
`DIF_SELECTBESTCOMPATDRV` with `ERR_DI_DO_DEFAULT`. Committed as
`9d3f17a3` with record 27 at draft 7 (section 4.1's job file format and
the 112-character bound; sections 5.1 to 5.3 and 6 for a cancelled
hardware wizard and `LDID_SRCPATH`), reviewed and revised over two more
rounds (`6df7f6c8`: each job-file line held strictly per byte, a NUL
refused, the test driver's writer bounded, the legs table), the last
clean at every priority. Then the gated build of both architectures on
`6df7f6c8`: `build-driver.cmd all` and `all -amd64`, BUILD + GATES PASSED
each, host tests 2080 checks with 0 failures, `test_di` 419 checks with
0 failures, the helper's self-test (26 imports by ordinal) in each.

### The legs (42-V.1, the helper's rows, by `DITEST.EXE`)

Fresh overlays of `vm\t26\win98-clean.qcow2` (98 SE with NUSB) and
`vm\t37me\winme-stock-base.qcow2` (stock ME) on `vm\usb4\bin-2a893ea`,
i440FX, one vCPU, the emulator's Thunderbolt 3 Alpine Ridge layout
(`PCI\VEN_8086&DEV_15BF`, its xHCI `DEV_15C1`), the QEMU xHCI
`PCI\VEN_1B36&DEV_000D` as the controller, a tablet and a stick on it;
QMP 47980 and 47982; the staged `release-x86` package with the helper
and `DITEST.EXE` of `6df7f6c8` in `C:\XHCI98`; every result over COM1.

| Row | 98 SE | ME |
|---|---|---|
| Probe | `RESULT probed`; the handle signalled at the helper's exit (78 ms), exit code 0; with a window (hwnd 1172) too | `RESULT probed` (199 ms), exit code 0: the image loads on ME, its SETUPX ordinals resolve |
| Version refusal | `RESULT refused version`, no `JOB` line, exit code 1 | the same |
| Job refusal | an INF name of nine characters: `RESULT refused command inf-path`, exit code 1 | not taken (the parser is the same image) |
| Not present | a made-up id: `FOUND 0`, `not-present`; the controller whose wizard was cancelled that boot: `not-present` (its `Enum` entry stays), its wizard back at the next start | the controller's wizard cancelled, back at the next start, the same as 98 SE |
| Controller, `xhci98.inf` | after its wizard was finished with no driver (problem 28, class `Unknown`): `DiGetINFClass` `USB`, `DIF_SELECTBESTCOMPATDRV` 506, `DEFAULT ... lowest-rank` (rank 2005), `DIF_INSTALLDEVICE` 0, `FLAGS 00810111`, `installed-restart`; the Windows CD's "Insert disk" in front of `DITEST`'s window with no taskbar entry of its own (owned), "Copy files from" `D:\WIN98` answered with `F:\WIN98`; after the restart the controller started and Windows installed the root hub itself from its `INF\OTHER` copy with no wizard | the same calls and verdict, no prompt at all (ME's setup source `C:\WINDOWS\OPTIONS\INSTALL\` holds the files); after the restart the root hub installed silently, the tablet and the stick enumerated behind it, the stick on ME's `USBSTOR` |
| Root hub, `force` | a same-version reinstall: `NODE 00000040 force`, `DIF_INSTALLDEVICE` 0, `installed`: no `ERR_DI_DONT_INSTALL` | the same |
| No match | `xhciuas.inf` on the root hub: `IDS HardwareID USB\XHCI98_ROOT_HUB`, `IDS CompatibleIDs -`, `no-match` | not taken |
| Thunderbolt, `xhcicm.inf` | `DiGetINFClass` `System`, 506 and the helper's choice (rank 2001), `installed` with no restart and no prompt; the NHI started (problem 0) | the same, `installed` |
| Removal | NHI `removed` (`Enum` key gone); controller `removed` (`Enum` key and driver key `USB\0002` gone, the root hub gone from the tree but its `Enum` entry left); the root hub's entry, not present, found among every entry and `removed` (its `Enum` key and driver key `USB\0003` gone) | the same three removals, the same readings |
| Bookkeeping | driver keys with `NTMPDriver` `xhci98.sys`, `DevLoader` `*NTKERN`, every `Xhci*` default; `INF\OTHER` held `YEOKHE~1.INF` (`xhcicm.inf`) and `YEOKHE~2.INF` (`xhci98.inf`) | not listed |

Two readings for the record: the controller's removal takes its devnode
subtree out of the tree but leaves the child's `Enum` entry, so the
uninstall's one-by-one removal of section 9 (the child's entry found
among every entry) is what removes it; and `DRIVER_NODE.DriverDate`,
read as an MS-DOS date, says 2026-10-18 on 98 SE and 2026-10-17 on ME
for the INFs' `DriverVer` of 10/17/2026, so the `DRIVER` line's date is
informational only (the driver key's `DriverDate` said `10-18-2026` on 98
SE).

Owed, written down rather than taken: the UAS and Stor98 rows (this leg's
layout had no UAS device on the controller, and the package carries no
Stor98), the update rows U24 and U23 over an
installed `2.4.0.0` or `2.3.1.0`, the uninstall's last reading (the
controller back as an unknown device after the `INF\OTHER` copies are
deleted, which is the installer's step), and every row through the
installer's window rather than `DITEST.EXE`: those are the joint legs
after the merge with 42-A.2 and 42-A.3. No real hardware ran the helper.

## 42-A.2 and 42-A.3: the legs (2026-10-19)

Branch `p42-wiz`, the installer's window (`XHCIWIZ.EXE`, `XHCIWZ64.EXE`)
and the uninstall from Add/Remove Programs, taken over at `b869a42f`
(Codex rounds 1 to 16, the last clean).

### Builds and the cut

| Build | Result |
|---|---|
| `build-driver.cmd all`, `b869a42f` | BUILD + GATES PASSED (debug release qemu); host tests 2080 checks, 0 failures |
| `build-driver.cmd all -amd64`, `b869a42f` | BUILD + GATES PASSED (debug release qemu); host tests 2080 checks, 0 failures |
| `build-driver.cmd all`, `a0bfc2b1` (the leg fix below) | BUILD + GATES PASSED (debug release qemu); host tests 2080 checks, 0 failures |
| `build-driver.cmd all -amd64`, `a0bfc2b1` | BUILD + GATES PASSED (debug release qemu); host tests 2080 checks, 0 failures |

The TEST cut: `make-release.ps1 -Flavor release -Arch x86 -TestLabel
"bench pending, window installer" -ReleasesDir out\legrel -UploadDir
out\legup -SkipQualtool` with Stor98 `0.4.0.0` pinned (`-Stor98Dir`
`stor98\releases\0.4.0.0`, `SHA256SUMS.txt` `ef7a2302...96b7`, interface
4), so `out\legup\xhci98-3.0.0.0.zip` carries Stor98 in `release-x86\`;
the readme opens "TEST 3.0.0.0, bench pending, window installer". The
x64 half is `make-package.ps1 -Flavor release -Arch amd64` with the same
pins, unsigned (no `-TestSignPfx`; a cut with x64 refuses to run without
it), added as `release-x64\` to the unpacked zip for the legs. The cut
first stopped twice: `xhcicln\XHCICLN.EXE` and `xhcitr\XHCITR.EXE` were
older than `xhciinst\inst_core.c`, which both compile, and the gated
build does not rebuild either; `xhcicln\build.cmd` and
`xhcitr\build.cmd` by hand, then the cut passed. The branch's version is
still `3.0.0.0`, the published release's.

### The guests

Overlays under `E:\xhci98-vm\p42-wiz` on `vm\usb4\bin-2a893ea`, QMP
47970 to 47975: 98 SE on `vm\t26\win98-clean.qcow2` (NUSB, no xhci98)
for the fresh legs and on `vm\t26\win98-gold.qcow2` (an earlier xhci98
installed) for the update; ME on `vm\t37me\winme-stock-base.qcow2`; XP
on `vm\winxp.img`; 7 on `vm\win7.img`; 7 x64 on
`vm\v-x64-base\w7x64-ts\disk.qcow2`; 2000 on `vm\win2k-xonly.img` and
`vm\t26\win2k-clean.qcow2`. Each has `qemu-xhci` with a tablet and a USB
stick on it; 98 SE, 7 and the first 2000 overlay also a QEMU NVMe
(`nvme`, no driver in the base). The package reached 98 SE and ME on the
transfer drive (FAT, read at the start), the NT guests on an ISO swapped
into the CD drive over QMP. Readings: `regedit /e` of `HKLM` (98 SE, ME)
or of `CurrentControlSet`, the Uninstall key and `Software\xhci98` (NT),
listings of `INF`, `INF\OTHER`, the driver directories, the install
directory and `TEMP`, and `C:\XHCIINST.LOG`, each sent over COM1 to the
host's serial log, before the install (B0), after it, and after the
uninstall, compared key by key and value by value.

### The legs

| Target | Fresh install through the window | Update over 3.0.0.0 | Uninstall from Add/Remove Programs |
|---|---|---|---|
| 98 SE (clean, Stor98 ticked) | pass: both wizards finished with no driver first (problem 28); the Found page listed the controller and its old moved entry; the helper installed the controller (`installed-restart`), the Windows CD's "Copy files from" answered; Stor98 installed on the NVMe (`stor98`, started) and recorded; after the cold start the root hub came by itself, Windows asked for the HID and mass-storage drivers (the Windows CD, now `G:` behind the NVMe and the stick's letters), the tablet worked, the tray tool ran at login | see the gold row | **fail on `b869a42f`, pass on `a0bfc2b1`**: the first uninstall left `INF\OTHER\YEOKHE~2.INF` (Stor98's copy) and the next start's wizard offered it for the NVMe; fixed (below) and taken again on a fresh overlay: every devnode removed through the helper, both `INF\OTHER` copies, `xhci98.sys`, `stor98.mpd`, the install directory, `Software\xhci98` and the Uninstall entry gone; at the cold start the controller and the NVMe came back as unknown devices and neither wizard found a driver |
| 98 SE (gold: an earlier xhci98, then 3.0.0.0's console install, which ended at exit 4 with the Device Manager steps) | - | **owed**: the Found page read "update (an older release)", the helper's `DIF_INSTALLDEVICE` returned `installed` with no restart flag, but `SYSTEM32\DRIVERS\XHCI98.SYS` stayed the old 114,704-byte file while the driver key took the new INF's date; the engine's last reading compared the binary's version and said "did not end as installed" with the Device Manager steps, correctly; why SETUPX kept the old file (in use, or its version resource) is not read | not taken |
| ME | pass: no CD prompt; after the cold start the root hub, the tablet and the stick (ME's `USBSTOR`) came with no wizard; tray tool at login | not taken | pass: `INF\OTHER`, the driver files, the install directory, `Software\xhci98` and the entry gone; at the cold start the controller's wizard found no driver |
| 7 x86 (Stor98 ticked) | pass: two "Windows can't verify the publisher" prompts (unsigned x86), then the window's "Restart Windows now" restarted it; the tablet and the stick worked behind xhci98, the NVMe ran on `stor98` (`Software\xhci98\Stor98\Devices` and `Staged` `oem4.inf` recorded) | **owed by the version**: over 3.0.0.0 installed by its console tool the Found page read "current (this release)" for both devices (the same version), and named only `xhciuas.inf` as a different build in the driver store; with `/restage` the window replaced `oem3.inf` and staged this `xhci98.inf` as a second store copy beside 3.0.0.0's `oem2.inf`, and `xhci98.sys` stayed 3.0.0.0's after the restart | pass: services `xhci98`, `xhcihlpr`, `stor98`, the `oem` INFs, the driver files, `C:\XHCI98`, `Software\xhci98` and the entry gone, the NVMe and controller back with no driver; left: `Control\Class\{3C8E5F21-...}` (the helper's class key), `Enum\Root\LEGACY_XHCIHLPR`, `CriticalDeviceDatabase\PCI#CC_010802` (`Service` `stor98`, written by Stor98's INF) and `Services\EventLog\System\stor98`; taken again after the `/restage` update: `oem2.inf` (3.0.0.0's), `oem3.inf` and `oem4.inf` all removed, the same four keys left and nothing else |
| XP | pass: three "has not passed Windows Logo testing" prompts and the root hub's Found New Hardware wizard (answered "No, not this time", install automatically); restart by the window | not taken (the same version as 3.0.0.0, as on 7) | pass, the same as 7 but for Stor98 (not ticked); left: the helper's class key and `Enum\Root\LEGACY_XHCIHLPR` |
| 7 x64 (unsigned) | pass with a start that has signature enforcement off (`bcdedit /set {current} advancedoptions on` in the overlay, then "Disable Driver Signature Enforcement"): the window ran as `XHCIWZ64.EXE`, said the x64 drivers carry no signature, waited its ten minutes for devices that cannot start on an ordinary boot, and ended "not everything is done"; after the start with enforcement off the tablet worked behind xhci98 | not taken | pass: the `oem` INFs, `xhci98.sys`, the install directory and the entry gone, nothing of the package left |
| 2000 | **owed**: on `win2k-xonly.img` (an old 1.x miniport installed) QEMU stopped at boot three times, its process using no CPU and QMP silent; on `win2k-clean.qcow2` the window ran to its Summary and QEMU stopped the same way as the install started the controller; the emulator, not the guest, so not read further | not taken | not taken |

Two misses in driving the guests, for the record: on the first 98 SE
uninstall an Enter meant for a message box landed on the Finish page
with "Restart Windows now" selected, and that 98 SE guest warm-restarted
(it did not hang this time); every other 98 SE and ME restart was a shut
down and a cold start. The window's Finish page offers "Restart Windows
now" first on 98 SE and ME after a full install or uninstall (record 26
W5).

### The bug the legs found, and its fix

On 98 SE and ME the setup engine's `INF\OTHER` copy has a long name
("Yeo Kheng Mengstor98.inf") and the driver key's `InfPath` its 8.3 alias
(`YEOKHE~2.INF`). `s98_inf_names` compared the two as strings, so the
install logged "appeared in this step, named by no device of this step:
left", recorded nothing under `Staged`, and the uninstall never deleted
the copy. `60a8f225` compares the alias too (`GetShortPathNameA`, already
imported); Codex rounds 17 to 21 (thread `01a14f91-...`) made the answer
three-valued (unknown when the alias cannot be read, and for a bare name
that `INF` and `INF\OTHER` could both hold; a qualified `InfPath`
matched against the copy's qualified name or alias), committed as
`db7b5470`, `53ba12da`, `d818fcd5`, `06eb6160` and `a0bfc2b1`; round 22
was clean. The fresh install on `a0bfc2b1` logged "made in this step and
named by the device: recorded" for the copy, and its uninstall "Stor98's
INF copy ... was deleted".

### What the legs leave for the owner

- The update rows need a version above 3.0.0.0: with the branch at the
  published version the window treats 3.0.0.0's devices as current, and
  `/restage` leaves two store copies of `xhci98.inf` and the old binary.
- 98 SE's update over an earlier xhci98 through the helper (the gold
  row): the old `XHCI98.SYS` stayed in place.
- The NT uninstall leaves the helper's class key and its legacy devnode,
  and Stor98's critical-device entry and event-log source; the 98 SE
  uninstall leaves `Services\stor98` (a value Stor98 writes at run time,
  `StorInterrupt 0.4.0`) and `Software\Stor98\Pool`, both Stor98's own.
  Which of these the uninstall owns is a record 26 section 9.3 decision.
- The 98 SE and ME uninstall shows its "If Windows has a hardware wizard
  open" message once per helper step (twice on 98 SE with Stor98).
- On 98 SE and ME the window shows no USB settings page on a fresh
  install (Components goes to Stor98's settings, or to the Summary).

Owed: 2000 (the emulator), the update over 3.0.0.0 on every target with
a version that differs, the helper rows U24, U23, UAS, Thunderbolt, an
older Stor98's update and two bundled Stor98 versions, the uninstall
after an update, and XP's and ME's updates. No real hardware ran the
window.
