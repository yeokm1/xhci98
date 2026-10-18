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

## 42-A.6, 42-A.7 and 42-A.9: the connection-manager follow-ups (2026-10-18 and 19)

Code on branch `p42-cm1` (from `3.1.0.0` at `2a89ba74`): `beedb87d`, then
`c1848d21` and `4a708676`; reviewed and revised until the review
converged at `4a708676`. Built at `4a708676` with every gate,
`scripts\build-driver.cmd all` and `all -amd64`: both pass (the x86 `qemu`
`xhcicm.sys` SHA-256 `e5af1748f54203a9...`); the host suites pass
(`test_cm_nvlink` 275 checks, `test_cm_query` 259, `test_u4cm` 1058).

What changed:

- 42-A.6: `CmNvlinkPublishUp` advances an entry already `UP` only for a
  tunnel other than the one its `UP` was published for (`CM_NVLINK_TUNNEL`:
  the USB4 port and its connection generation), as the text of
  `stor98cm.h` of 2026-10-18 has it (entry protocol step 1, R8b). The UP
  hook names its port's tunnel on the port's bridge; the seam's tunnel-up
  exposure names it to the action's member alone; a tunnel down, a
  RESYNC, a baseline, an owed re-enumeration and a tunnel up whose member
  is not known (always under the firmware connection manager) name none.
  Each bridge keeps the latest tunnel named for it, which every exposure of
  the bridge publishes for. The USB4 down hook, where the port's member is
  not known, takes an entry out of `UP` when its function is read absent,
  on every member no other live port corresponds to. Record 21 section 7.2
  and record 22 sections 7.7 and 10 say so.
- 42-A.7: the query device's seeded answer takes its inert flag from the
  thread's recorded wait (`CM_NHI.ThreadWait`, `CmQuerySeedInert`),
  a start's bind still running included; record 25 section 14.
- 42-A.9: a Thunderbolt 3 router adopted at the start or rebuilt under a
  resume budget is owed its names (`DromOwed`); `TbtU4Poll` reads its
  EEPROM once, with no budget running and the port `UP`, taking the names
  only (`u4.drom.late`); record 22 section 8.

The legs, on the reviewed code: the `usb4-emu` QEMU of `bin-2a893ea`
(SHA-256 checked), i440FX, `usb4-host profile=usb4-generic` at `00:04`
with two tunnel bridges, `tb0` at `00:05` (USB4 port 1) and `tb1` at
`00:06` (port 2: not `00:08`, where U.11 of run-41 left the port-2 NVMe
without an interrupt on 98 SE); NVMe enclosures `enc0` and `enc1` over the
FAT image of `vm\u4-asm\base`; overlays and launchers in a VM folder of
this branch's own. Each guest got the signed `3.0.0.0` asset installed
inside the guest (`INSTALL /defaults /yes /thunderbolt`), Stor98 `0.4.0.0`
from its release, then the gated `qemu` `xhcicm.sys` of `4a708676` copied
over the installed one for its log.

**42-A.6, Windows 98 SE** (the `t26` gold image): `enc0` hot-plugged: the
tunnel up, the 9x late scan publishing the NVMe once
(`cm.nvlink.up=01000101`, new, bus 1), F: mounted with no Refresh, a file
written and compared. `enc1` hot-plugged on port 2: one publication, its
own (`cm.nvlink.up=01000201`, new, bus 2), and no `cm.nvlink.up` for the
entry of port 1, where U.11 of run-41 logged its republication at a new
generation (`02000201`) and Stor98 then dropped the first disk. F:
compared clean after it, and G:, the volume of port 2, mounted beside it
and was written and compared: two NVMe volumes mounted together on 98 SE.
`enc0` unplugged (path closed, the statement withheld as 98 SE makes none,
`cm.nvlink.withheld=09000001`) and plugged again: the entry of port 1
alone advanced, once (`cm.nvlink.up=02000101`, generation 2), that of port
2 untouched; F: back with its file, G: compared clean throughout. Pass.

**42-A.6, Windows 7 x86** (the NT target): `enc0`: the UP hook published
generation 1 (`cm.nvlink.up=01000101`) and the seam's exposure of the same
tunnel left it (no second publication; before 42-A.6 that exposure was
fresh and advanced it again); F: mounted (AutoPlay "ORICO (F:)"), a file
written and compared. `enc1` on port 2: its own entry alone
(`cm.nvlink.up=01000201`), that of port 1 untouched; F: compared clean
after it. The NVMe of port 2 gave no volume on 7: both enclosures' disks
are overlays of one image with one disk signature, which Windows 7 does
not mount twice (not read further; the publications are the log's).
`enc0` plugged again: the down withheld as 7 is not a qualified system
(`09000001`), then the entry of port 1 alone advanced, once
(`cm.nvlink.up=02000101`); F: back with its file. Pass.

The RESYNC was not reached on either system. Under the USB4 profile a
RESYNC comes from a router removed below depth 1, an action queue that
overflowed, or, on NT, the host interface lost: this connection manager
enumerates no depth-2 router (as U.12 of run-41 read; a depth-2 router
plugged behind `enc0` and removed logged nothing), an overflow cannot be
driven from QMP, and after a loss `CmNvlinkLost` has already taken the
entries out of `UP`. The RESYNC case is the host vectors'
(`test_cm_nvlink.c`, `test_two_members`).

**42-A.7, Windows 98 SE**: `XhciCmMode` 0 written to the connection
manager's driver key (`Class\System\0031`) in the guest, restarted:
`cm.mode=00000000`, `cm.inert=00000001`; `XHCITR /report` shows the
controller "[no connection manager], idle (inert)", stage "idle (inert)
(reason 1)". It also says the query device "did not answer for it", and
so it does with the `3.0.0.0` `xhcicm.sys` on the same guest and sequence
(a control): on 98 SE XHCITR does not take the query answer of an inert
start, a behaviour older than this change; noted for the owner, not read
further.

**42-A.7, Windows 7 x86**: the same value in the driver key
(`{4D36E97D-...}\0025`), a cold start: `cm.inert=00000001`; `XHCITR
/report`: source "the driver's query device", XHCICM0, stage "idle
(inert) (reason 1)", "Counters (pass 1, ...; the driver is idle)": the
answer read carries the inert flag, from the one pass the creation of the
device woke. Pass. The moment between the device's creation and that pass
is too short to read in a guest; the choice of the seed there is the host
vector's (`test_cm_query.c`, `test_seed_inert`).

**42-A.9, Windows 98 SE**: the TS3 Plus model (`usb4-device ...
dialect=tb3`) at power-on: the DROM read at the connect
(`u4.drom.unused=00000620`) and `XHCIINST /devices` naming it "QEMU USB4
NVMe Enclosure", Thunderbolt 3, 8086:15D3. Then Stand by, the dock removed
and plugged again while the guest slept (its configuration lost across
the sleep), `system_wakeup`: `cm.resume=00000001`, the router rebuilt
inside the resume with no EEPROM read (`u4.router.up=808615D3` before any
`u4.drom` line), then on the first pass after it `u4.drom.unused=00000620`
and `u4.drom.late=01000001` (port 1, a model name read). Its name could
not be read back after this resume: the display stayed in text mode with
a DOS box open. On an earlier run of the same sequence a bridge's Add New
Hardware wizard came up after the resume, and keys sent into it ended in
"A fatal exception 0E has occurred at 017F:BFF87EDE" in an application;
the same with the `3.0.0.0` `xhcicm.sys` (a control), so not of this
change; unattributed. Pass on the reading of the log.

**42-A.9, Windows 7 x86**: 7 x86 offers no standby in this emulator
(`powercfg -a`: "The VGAPNP.SYS display driver does not support
standby"), so the resume read is a hibernation's: the dock attached,
`shutdown /h`, QEMU started again with the dock attached: `cm.resume`, the
router rebuilt inside the resume (`u4.router.up=808615D3`, no DROM read),
then `u4.drom.unused=00000620` and `u4.drom.late=01000001`; after the
logon `XHCITR /report` names it "QEMU USB4 NVMe Enclosure (Thunderbolt 3),
approved", 8086:15D3. Pass.

Not shown: an adopted Thunderbolt 3 router. With `usb4-host preos-cm=on`
and the dock attached at power-on the emulator left no tunnel to adopt
(`u4.router.kind=01000502`, no preserved bit), and the driver connected it
as a fresh plug, reading its DROM at the connect; the adopted case is the
fake fabric's (`test_u4cm.c`, `test_tbt3_adopted`). Windows 2000 was not
run for these tasks.

## 42-A.20: Stor98 consolidated into `src/stor98` (2026-10-19)

Branch `p42-stor98`, from `3.1.0.0` at `94d30a7c`, with `3.1.0.0` merged
in for the task's roadmap row. Owner decisions of 2026-10-19: the source
is Stor98's branch `audit-2026-10-18` at `4253b58`; one snapshot commit
and no history; this repository's commit rule (no trailer) applies to
`src/stor98`; Stor98 still builds as its own files.

- **The snapshot** (`b6f53b1b`): 312 files, every blob and mode equal to
  `git ls-tree -r 4253b58` of the Stor98 repository; after a fresh
  checkout under this host's `core.autocrlf true` the files are byte for
  byte what `git archive 4253b58` of that repository gives. Under
  `core.autocrlf false` ten files (its `LICENSE`, its `sources` files and
  `.rc`, and the same in its `third_party` copy) would have been checked
  out CRLF by this repository's name-only rules; `src/stor98/** !eol` in
  the root `.gitattributes` (`4d196db7`) leaves them to Stor98's own file,
  and every file's `text`/`eol` attributes now equal those of Stor98's own
  checkout. Two root `.gitignore` patterns hid tracked Stor98 files
  (`*.cmd`: its four build scripts; `*.log`: ten gate logs under its
  `docs/gates/`) and are negated for `src/stor98`.
- **Build** (`4d196db7`): Stor98's scripts find the kits under
  `STOR98_TOOLS`, by default its own `tools\` or else the one two levels
  up; `SCSI.H` and `SRB.H` were copied into this repository's
  `tools\w98ddk\inc\` (SHA-256 `809d57e6...39b7` and `4b3c3305...8396`).
  `src\stor98\scripts\build.cmd all free`, the same with
  `STOR98_DBGPORT=1`, `tests\unit\build.cmd` (11 executables, all
  passed), `tests\cm9x\build.cmd` and `tests\cmnt\build.cmd` all succeed
  here. Python is not a build dependency.
- **The header gate** (`ecffe0da`): `scripts\check-stor98-header.ps1`
  in `build-driver.cmd`. The two copies differed on `3.1.0.0` (Stor98's
  contract text of 2026-10-18 was on branch `stor98-audit-g` only), so
  that branch's header commit `96fa2d6c` was taken as `ffc98acd`, the
  same blob.
- **Packaging** (`ecffe0da`): `-Stor98Dir` defaults to
  `src\stor98\releases\<version>`; the `0.4.0.0` folder's
  `SHA256SUMS.txt` hashes to `ef7a2302...96b7`, the pin `3.0.0.0`
  bundled (`releases/3.0.0.0/readme.txt`), and `test-package.ps1`
  bundles it from there.
- **Review**: reviewed and revised (`4da44d52`), then clean.
- **Gated build** at `4da44d52`: `build-driver.cmd all` and
  `all -amd64` passed every gate, the records clean (`dirty=0`), and the
  Stor98 builds and unit tests passed again at the same commit.

## 42-A.17 and 42-A.18: the machine-wide registry key, and value 4's codes (2026-10-18 and 19)

Written on branch `p42-reg` from `3.1.0.0` at `2a89ba74` (`7f25ca3c`, the
implementation; `2a3e7e9c`, `33e98bc3` and `2b9a2ae2`, its revisions),
reviewed and revised until the review converged. The owner's decisions
are the decisions table's two
rows of 2026-10-18.

- 42-A.17: every `Xhci*` value of `xhci98.sys` (`hcd_ctl.c`
  `hcdReadDword`, which every read goes through) and every `XhciCm*`
  value of `xhcicm.sys` (`cm_reg.c` `cmQuery`) is read first from
  `HKLM\System\CurrentControlSet\Services\USB` and, when it is not there
  in any type, from the driver key; the decision is the new pure
  `xhci_regsrc.c` (host suite `test_regsrc`), compiled into both drivers.
  "There" is the query routine having been called with a value (a
  zero-length `REG_NONE` excepted), not a status code. A `reg.machine`
  log note counts the start's values taken machine-wide. The INFs are
  unchanged. The six helpers write the machine-wide value, create the key,
  leave the per-controller values alone and show both; XHCITR's levels
  block and `/report` show both and which won, and its Set writes the
  machine-wide value where one is in force. `INSTALL /set` and
  `INSTALL.BAT`'s questions still write the controllers' own keys; the
  installer's machine-wide write is the installer task's (design record 26
  section 5.4).
- 42-A.18: `XhciFastPollFsLs` 4 programs Interval = `bInterval` - 128 for
  an interrupt endpoint's code 128 to 143, at every speed, on a root port
  or behind a hub, replacing task 36.19's codes with no compatibility; a
  code whose Interval is Table 6-12's own is left unmarked; the fallback
  stays Interval 3; the interval caps still run after it (design record 13
  section 13.7).

Built after the review converged: `scripts\build-driver.cmd all` and
`all -amd64` at `2b9a2ae2`, every gate passing, the host suites (with
`test_regsrc`, `test_fast_codes`, `test_cap_codes` and the XHCITR level
vectors) passing.

### The legs (development host A, QEMU, the `debug` flavour at `2b9a2ae2`)

Each guest had two `qemu-xhci` controllers, a `usb-tablet` on controller
1's root port 2 (High Speed), a `usb-mouse` behind a `usb-hub` on its port
1 (Full Speed behind a hub) and a `usb-mouse` on controller 2's root port
1. The values were set with a leg aid that writes one DWORD machine-wide
or on one controller's driver key and lists both (not shipped), and with
the package's own helpers; each reading is the `XHCITR /report` of the
next start, searched for `fastpoll.*`, `reg.machine`, `silent.gate` and
the levels block, and screenshotted. The `qemu` flavour could not be
used: `XHCIINST` refuses a `qemu` build in the `debug-x86` directory, as
it should. Installed with `INSTALL /debug /yes` from a package made of
`3.0.0.0`'s `INSTALL.BAT` and `XHCIINST`, this branch's helpers and
XHCITR, and the staged `debug` x86 drivers.

| Target | Reading | Result |
|---|---|---|
| 98 SE | Absent: no machine-wide value | PASS: `fastpoll.value 0`, `reg.machine 0`; the levels block "Machine-wide ... not set". One controller recorded nothing: its own `XhciLogVerbosity` was the `0` an older INF had written |
| 98 SE | Machine-wide `XhciFastPollFsLs 3` and `XhciLogVerbosity 3`; controller 2's own `XhciFastPollFsLs 2`, controller 1's own `0` | PASS: both controllers `fastpoll.value 3`, `reg.machine 2`; both now log, the machine-wide level overriding the `0`; XHCITR "3 - adds port status (machine-wide)" for each and "its own value, 0 / 2, is not in force" |
| 98 SE | `silent-on.bat`, `portsw-on.bat` | PASS: each wrote the machine-wide value, read it back and showed both controllers' own `0` as "NOT IN FORCE"; `portsw.vbs show` before it read the missing value as "no machine-wide value is set" (WSH's not-found error matched on 98 SE). At the next start both controllers `silent.gate.value 1`, `silent.gate 1` |
| 98 SE | Machine-wide `XhciFastPollFsLs` deleted | PASS: controller 1 `fastpoll.value 0`, controller 2 `2`, each its own; `reg.machine 2` (the log value and the silent switch) |
| 2000 SP4 | Absent | PASS: `fastpoll.value 0`, `reg.machine 0`, "not set"; `Services\USB` did not exist (`regedit /e` of it wrote no file) |
| 2000 SP4 | `portsw-on.bat` | PASS: the key created, holding `XhciIntelPortSwitch` dword 1 (`regedit /e`); both controllers' own `0` shown "NOT IN FORCE" |
| 2000 SP4 | Machine-wide `3` over controller 2's own `2` | PASS: both `fastpoll.value 3`, `reg.machine 2`, XHCITR's "not in force" lines for `0` and `2` |
| 2000 SP4 | Machine-wide deleted | PASS: `0` and `2`, `reg.machine 1` |
| 7 SP1 x86 | Absent; `portsw-on.bat`; machine-wide `3` over controller 2's own `2`; deleted | PASS: `0`/`0` and `reg.machine 0`; the helper wrote and showed as on 2000 (`reg query` of the key: `XhciIntelPortSwitch 0x1`); `3`/`3` and `reg.machine 1`; `0`/`2` and `reg.machine 0`. Whether the key existed before the helper ran on 7 was not read |

42-A.18 on 98 SE, hidusbf (the 98/ME `hidusbf.sys` of 2026-10-03)
on every QEMU HID device as design record 13 section 13.6 step 1 puts it
by hand, `XhciFastPollFsLs` 4 machine-wide; the programmed Interval is the
`fastpoll.open` record's last byte (slot, DCI, Table 6-12's Interval, the
one programmed):

| Code | Tablet, High Speed, root port | Mouse, Full Speed, behind the hub | Mouse, controller 2 root port | Result |
|---|---|---|---|---|
| 135 | `02030F07` | `03030A07` | `01030F07` | PASS: Interval 7 (16 ms) everywhere; Full Speed behind a hub applied, as task 42-A.18 asks |
| 128 | `02030F00` | `03030A00` | `01030F00` | PASS: Interval 0 (125 us); no `fastpoll.fallback`; the tablet's pointer followed |
| 143 | no record | `03030A0F` | no record | PASS: at High Speed 143 is Table 6-12's own clamp, left unmarked; Full Speed Interval 15 |
| 144 | no record | no record | no record | PASS: not a code |

What these legs do not show: QEMU accepts any Interval (design record 13
section 13.4), so no refusal and no fallback was exercised, and nothing
says how a real controller or a transaction translator schedules
Interval 0 to 2 or 11 to 15 below High Speed; the 7 guest's hidusbf
reading of 36.19 was not repeated; the A.17 legs read `XhciFastPollFsLs`,
`XhciLogVerbosity` and `XhciSilentInstall` as the driver takes them, and
the port switch only as the helpers write and show it (QEMU's controller
is not Intel, so the driver never reads that value); no `XhciCm*` value
was read on a guest with a Thunderbolt or USB4 controller; XHCITR's Set
under a machine-wide value was not run; `silent-off.bat` and
`portsw-off.bat` were not run. Owed to 42-V.2 as its text says, with
these taken.
