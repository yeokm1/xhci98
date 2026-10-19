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

## 42-A.5: the safe-removal mark across a stack rebuild (2026-10-19)

The code of 42-0.4 (design record 21 section 7.4, revision 3), on branch
`p42-ejrb` from `3.1.0.0` at `2a89ba74`: the driver half in `src\cm\`
(the roles by id tables, the `AddDevice` child, the attachment list and the
per-domain marks table `cm_marks.c`, `IOCTL_XHCICM_MARKS`, the departures
held pending and the eject posted only on `XhciCmEjectConfirm`), and the
mark service `xhcimark\` (`XHCIMARK.EXE`, `XHCIMK64.EXE`) with its pure
half, self-test and import check, staged by `make-release.ps1` into
`tools\xhcimark\`. The installer's hooks for the service (its install,
start, update and removal, and the marks removed at the uninstall and
after a failed upgrade) are task 42-A.3's code, not this branch's; record
21 section 7.4's "The installer" paragraph is their specification.

Reviewed and revised until the review converged. Among the changes the
review brought: the reconciliation judges a record by its
owning domain and never reads a failed lookup or an unreadable location as
an absence; the restart's fresh reads re-check the NHI, both answers, the
member and the unit's actual parent; the marks table keeps each member's
sets apart; the confirmation's writers share the tools' eject mutex.

### The legs (42-V.2, the 42-A.5 part)

In QEMU guests on development host A, the emulator and recipe of
"Bridges behind the tunnel port on NT" (`runs/run-41.md`): the
`tb3-alpine-ridge` host with the SHPC tunnel bridge `tb0` and a chained
upstream port (`1B21:2421`) and bridge behind it, an NVMe at 01:01.0 and
another behind the chain. Each guest took the published `3.0.0.0`
package's `INSTALL /yes /thunderbolt /notray /nopause`, then this
branch's qemu-flavour `xhcicm.sys` copied over the installed one and the
service registered by hand (`XHCIMARK -register` from `system32`),
because the installer's hooks come with 42-A.3. Disables, enables and the
driver update were made by a test helper of the guest's own
(`SetupDiCallClassInstaller(DIF_PROPERTYCHANGE)`,
`UpdateDriverForPlugAndPlayDevices` with `machine.inf`, forced); "listed"
is `XHCIINST /devices` printing "removable: yes ... listed in Safely Remove
Hardware" for the unit.

- **Windows 2000 SP4** (one processor, no ACPI). The first build of the
  driver bugchecked at the next boot (0x7F, a double fault, read with the
  emulator's debugger: the kernel stack overflowed): a filter's capability
  handler asked `IoGetDeviceProperty` for its own address, which the PnP
  manager answers with an `IRP_MN_QUERY_CAPABILITIES` down the same stack.
  Fixed in the same session (the capability answer's own address; no
  location read while the object is itself asking). With the fix: the
  first plug of a never-seen unit listed, the upstream port and the NVMe
  marked within a second of the plug (`3.0.0.0` left this unit unlisted,
  the recipe's step 4); a disable (the mark kept while disabled, problem
  22) and an enable, listed; a forced driver update, listed; an unplug,
  both marks removed within a poll (2000's Unsafe Removal dialog as
  before); the service stopped, the unit unplugged, the service started:
  both marks removed at its start. Pass.
- **Windows XP SP3** (ACPI, two processors). The first plug of a
  never-seen unit: the marks written within a second; Windows' driver
  installation of the upstream port then deleted the port's own
  `LowerFilters` value, the service wrote it again, held its restart while
  the NVMe's Found New Hardware wizard was open ("an installation is
  pending"), and once it was cancelled restarted the port
  (`DIF_PROPERTYCHANGE`), which came back listed. A disable and an enable,
  listed. A forced driver update deleted the value again; the first build
  of the service had spent its one restart on the first plug and left the
  unit unlisted, so a unit seen listed after its restart now re-arms it
  (record 21 section 7.4, "As built"); read again from a fresh install: the
  update's install deleted the value, the service wrote it, restarted the
  port, listed. An unplug, both marks removed within a poll; the
  stopped-service departure caught up at the service's start. Pass.
- **Windows 7 SP1, 32-bit** (two processors, from an elevated prompt).
  The first plug of a never-seen unit listed (7 kept the first plug in
  `3.0.0.0` too); the driver installation deleted the upstream port's new
  value within two seconds and the service wrote it again, so the later
  rebuilds kept it. A disable (the mark kept while disabled) and an enable,
  listed; a forced driver update (the value deleted by the install and
  written again), listed; an unplug, both marks removed within a poll; a
  departure while the service was stopped caught up at its start. Pass.
- **Windows XP Professional x64 SP2** (the amd64 build; XP x64 enforces
  no kernel-mode signature, so this branch's unsigned build loads). The
  same readings as 32-bit XP: the first plug's marks written, the port's
  value deleted by its install and written again, the gated restart held
  while a wizard was open and then made, listed; a disable and an enable,
  listed; a forced driver update, the value deleted and written again, the
  re-armed restart made, listed; an unplug, both marks removed within a
  poll; a departure while the service was stopped caught up at its start.
  Pass.

**Owed** (42-V.2's 42-A.5 part, written down here rather than read):

- **Vista (both architectures) and 7 x64**: not read in this run. The x64
  binaries of a cut are test-signed by the packager only, with the owner's
  key and password, which this run does not have, so an unsigned
  `xhcicm.sys` loads on those systems only from an F8 boot with signature
  enforcement disabled; Vista x86 needs no signature and was not reached
  for time.
- **The value gone after the uninstall**, and every leg of the installer's
  part (its install, update, stop and removal of the service, step 0, the
  failed upgrade, the hand-deleted service's Code 39 repaired): they need
  42-A.3's hooks, which are not on this branch.
- The legs of record 21 section 7.4's list that need more than this run
  did: the restart vetoed by an open file (no volume behind the chain: the
  emulator's NVMe has no driver on these guests), an eject and its
  confirmation (an applet stop on XP and later confirmed by the service, on
  2000 by `XHCIINST /eject`'s own write, which is 42-A.3's code), the USB4
  profile's legs (a driver update and a disable held under the USB4
  connection manager with the tunnel kept, a delayed confirmation across a
  connection change), more than 32 departures in a minute, the crash
  between the write and the record, Driver Verifier on the 2000 SMP guest
  with the `AddDevice` child, `XhciCmSafeEject` 0 with a value in place,
  a restart of Windows with the unit present, and the dock's upstream port
  as a unit whose id the filter INF names. Every guest here ran the
  firmware connection manager's emulated Alpine Ridge host.

## 42-A.12: the per-member, non-blocking Stor98 waits and the 9x call workers (2026-10-19)

Code on branch `p42-nvw` (from `3.1.0.0` at `db921f49`): `44eaf47c`, then
`4a31f545` and `bddf936d`; reviewed and revised until the review
converged at `bddf936d` (its third round returned nothing). Built at
`bddf936d` with every gate, `scripts\build-driver.cmd all` and
`all -amd64`: both pass; the host suites pass (`test_cm_nvlink` 391
checks, `test_cm_call` 175).

What changed:

- The acknowledgement before a removal is no longer a hold of the
  connection-manager thread (`CmNvlinkAwait` is gone). Each entry marked
  `DOWN_QUIESCED` keeps a pending wait with its mark's time, stepped by
  `CmNvlinkStep` on every pass and after every piece of every sleep of the
  core and the seam (the host sleep is taken in pieces of at most 50 ms
  while a wait is pending), the deadline read before the answer. A
  member's gate (`CmNvlinkHeld`) is raised by its own waits alone: the
  seam holds that member's exposure and re-enumeration, its baseline ask,
  its owed re-enumeration and its NT resume release (`CM_REENUM_PORT.Held`,
  the pure `CmNvlinkHoldReenum` and `CmNvlinkHoldRun`) and runs them from
  the first pass that finds the gate down; a held re-enumeration waits
  for the transport after a host-interface loss, a held release does not.
  The thread passes at most 50 ms apart while a wait is pending (R10).
- The 9x per-member call workers of record 21 section 7.2 part 0: the
  R6f handoff word (`cm_call.c`, pure) and one system thread per member
  with its own CONFIGMG route (`cm_callwk.c`), started beside the CONFIGMG
  worker, stopped without waiting; an operation of kind
  `CM_CALL_K_DETACH` still runs after the stop, and every outcome the
  thread did not take (a result, or a withdrawal) goes to the operation's
  late taker on the worker. In this branch the only operation run on them
  is each worker's own route opening; record 28's machine (task 42-A.15)
  and the removal and statement transactions attach to `CmCallwkSubmit`
  and `CmCallwkTake`. Moving `cm_cfgwk.c`'s operations onto them is owed
  (that file was being edited on another branch).
- `stor98cm.h` R8b (as Stor98 revised it on 2026-10-18): the retirement
  before an exposure withdraws a statement without advancing the
  generation, so a re-plug after a statement costs one increment, the new
  `UP`'s, not two. Keeping the retirement's increment for the next `UP` to
  reuse was rejected: Stor98's 9x late lookup takes a generation read not
  `UP` as its floor and binds only an `UP` above it.

The legs, on the reviewed code: the `usb4-emu` QEMU of `bin-2a893ea`
(SHA-256 checked), i440FX, `usb4-host profile=usb4-generic` at `00:04`
with tunnel bridges `tb0` at `00:05` (port 1) and `tb1` at `00:06` (port
2), NVMe enclosures `enc0` and `enc1` over the FAT image of
`vm\u4-asm\base`, the display on VNC; the gated `qemu` `xhcicm.sys` of
`bddf936d` (SHA-256 `a83f7f9a7b25ec65...`) copied over the installed one.

**Windows 2000** (the one NT target the statement is qualified on): the
signed `3.0.0.0` asset installed in the guest (`XHCIINST /dir release-x86
/defaults /yes /thunderbolt`), Stor98 `0.4.0.0` from its release through
the Found New Hardware wizard for each drive, then the new `xhcicm.sys`.
Both enclosures hot-plugged: one `UP` each (`cm.nvlink.up=01000101`,
`01000201`), F: and G: mounted, `TESTDATA.BIN` on both compared clean
(`fc /b` against a copy taken first), a file written to F: and compared.
`enc0` unplugged: the statement made (`cm.nvlink.down=00000001`), the
member's re-enumeration held behind its gate (`cm.nvlink.up.held=00000028`,
`cm.reenum.held=00000028`), the acknowledgement 110 ms after the mark
(`cm.nvlink.await.end=00000100`, `cm.nvlink.await=0000006E`), then the
held work run (`cm.reenum.held.run=03040028`) and the entry retired. Later
`enc0` and `enc1` unplugged one second apart: each member held behind its
own statement and released by its own acknowledgement (`...0028` at 110
ms, `...0030` at 50 ms), and `enc1` plugged again published its `UP`
with nothing held. Port 2's next statement read generation 2
(`cm.nvlink.down=00010002`) after one re-plug that followed a statement,
where the double increment gave 3. F: and G: compared clean after every
re-plug. Pass, with one gap: both acknowledgements came within one Stor98
poll, so no leg had a long wait on one port while the other worked; an
attempt to make one (the port-1 controller disabled in Device Manager)
ended in the safe-eject teardown of that port instead, as disabling a
tunnelled function does under the USB4 profile. A held member left
unacknowledged for its 5 s while the other plugs and unplugs is the host
vectors' (`test_cm_nvlink.c`, `test_hold`, `test_pending`); on a guest it
needs a Stor98 whose acknowledgement is stretched (42-V.6's harness).

**Windows 98 SE** (the `t26` gold image with the `3.0.0.0` asset and
Stor98 `0.4.0.0`, its `XhciCmMode` put back to 1): the call workers at
the start (`cm.callwk.started=00000202`, both made) and each opening its
own route (`cm.callwk.open=0080040B`, `0180040B`: opened, version 040B).
Both enclosures hot-plugged, F: and G: mounted, compared clean and
written; `enc0` unplugged (the statement withheld, as 98 SE makes none:
`09000001`) and plugged again (one `UP`, port 1's entry alone), both
volumes compared clean. Then the USB4 host router disabled in Device
Manager (`cm.stop`, the workers stopped without a wait, no hang) and
enabled again: a second start with two new workers, both routes opened,
and F: and G: still compared clean. Pass.

Owed: Windows ME and 7 were not run for this task; the long-wait
two-port leg above; the call workers carrying a real operation (record
28's, the removal and statement transactions) and their stop with an
operation in flight, which come with those clients.
## 42-A.14 and 42-A.15: PCI below a tunnel port on 9x; a 9x function with no interrupt (2026-10-19)

Worker p42-9x, branch `p42-9x` from `3.1.0.0` at `2a89ba74`; code
`f747fce3`, reviewed and revised through the branch's last commit.
Emulator legs in QEMU (the `usb4-emu` build `bin-2a893ea`), guest 98 SE
from `win98-gold` on i440FX, TCG, the Thunderbolt 3 layout of the
PCI-page legs (the tunnel bridge, the Alpine Ridge host, the ASM2464
enclosure), with the tunnel bridge at 00:1Ch, where SeaBIOS's routing
table has no entry, so a function added below it after POST gets no route
from `PCI.VXD`. xHCI98 and `xhcicm.sys` installed by hand from the
package (the 3.0.0.0 console installer refused a `qemu` build in
`release-x86`, as it should); Stor98 0.4.0.0 on the enclosure's NVMe; the
installer record `HKLM\Software\xhci98\Stor98` `Version` = `0.4.0.0`
imported by hand, since this branch's installer does not write it (task
42-A.3, branch `p42-wiz`); `XhciCmLogVerbosity` 2.

- **The interim (owner's coordinator, 2026-10-19).** Design record 28
  places each call on the member's call worker of task 42-A.12, which no
  branch has. The pure half, `cm_noirq.c`, is a state machine of
  operations that the existing 98 worker drives at its 200 ms tick until
  42-A.12 lands; record 28's dated note says what that costs (a blocked
  CONFIGMG call holds the domain's other 9x work) and what changes then.
  **Owed to 42-A.12**: driving the same machine from the thread's pass
  through each member's R6f handoff word, the 50 ms cadence, and the
  liveness state read by the thread itself.
- **42-A.14, read on 98 SE.** The xHCI98 USB Tree's PCI page of the
  Thunderbolt controller lists the functions below the tunnel port with
  their configuration space, read by the worker through `PCI.VXD`'s
  guarded route: the enclosure's NVMe at 01:01.0 (1B36:0010, class
  010802, pin INTA, line 0, its 64-bit memory BAR, its PCI Express and
  power management capabilities; "35 configuration reads, 0 refused");
  with a bridge chain behind the port, the switch's upstream port at
  01:03.0, its downstream port 02:00.0 and the chain's NVMe 03:00.0, after
  `xhcicm.sys` numbered the chain (buses 02-03, the window
  FE900000-FE9FFFFF) - "102 configuration reads, 0 refused";
  `cm.query.pci.9x` 00010000 (walked, nothing held). ME not read.
- **42-A.15, read on 98 SE: the basic form does not apply.** The NVMe at
  01:01.0 (its pin swizzling off the port's pin, UNROUTABLE by the fresh
  classification) sat at Code 29, "This device is disabled because the
  BIOS for the device did not give it an IRQ", its Resources tab empty.
  The worker's walk, rules and gate ran: one basic configuration at
  `LCPRI_NORMAL` (`cm.reenum.9x.noirq.lc` 01003000), whose descriptors
  read one memory descriptor of 40 bytes and nothing else - no IRQ, no
  I/O, no DMA, and the every-type read met the one memory descriptor
  (`.rd` 01012800, 0100000F, 0400000F, 0200000F, 0300000F, 00012800,
  0000000F). The device was refused, EXCLUDED "configuration"
  (`XhciCmNoIrqDiag` word 4 00060203, word 6 0006000F), nothing added;
  the same after a re-plug. Record 28 section 3's hypothesis - an IRQ
  requirement in `PCI.VXD`'s configuration that cannot be met - is
  refuted for this case; record 28's note says so, and a redesign is owed
  before 42-V.6. Stor98 wrote no `StorInterrupt` (it never started). An
  earlier build of the branch (`f747fce3`) gave no diagnostic at all (a
  refusal wrote none, fixed in review); a later one (`d8c73375`) read the
  descriptors with `ResType_All` and refused at the same place, which
  `2d38fc77`'s commit message took for a failed `ResType_All` read; the
  leg above corrects that.
- **Not read**: the chain NVMe's start (the 98 SE "PCI standard
  PCI-to-PCI bridge" wizard for the hot-added upstream port stopped
  advancing, so the chain's NVMe was never enumerated); the routed case;
  ME; the pin-0 NVMe, which the emulator cannot present (QEMU's NVMe
  hardcodes INTA; a property for it would be a request to `usb4-emu`);
  the file write and compare (nothing started).
- **Owed on real hardware**: 42-A.14 on the owner's P14s Gen 1 under 98
  SE with the ORICO enclosure, the PCI page below the port; nothing at
  hand presents 42-A.15's case (record 28 section 8.2).

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

## 42-A.4 and 42-A.16: XHCISNAP retired into XHCITR; the Safely Remove stop investigated (2026-10-19)

Branch `p42-tr` from `2a89ba74`: `99c5fc3a` (the code), `955525c1`,
`3ed6761d` and `ccd9e028` (Codex rounds 1 to 3), `afc2b317` (the gated
build's fix: `hcd_pnp.c` and `hcd_rh.c` include `xhci_hw.h`). Codex
reviewed it over five rounds: 2 P1, 3 P2 and 1 P3, then 4 P2, then 1 P2,
then nothing, then nothing on the build fix.

- 42-A.4, as record 25 section 15 has it. Round 1's P1s were real: the
  configuration manager's `LOG_CONF` and `RES_DES` handles are pointer
  sized, which `XHCISNAP` (x86 alone) never met and `XHCITR64` does; and
  the report's level-4 withholding must read the dump's own header line,
  not a tail of the file. Read on the x64 build host after the fix:
  `XHCITR64 /snapshot` decodes allocated memory ranges and MSI interrupts.
  `test-package.ps1`: 1114 checks pass; host suites pass; `snaptest.cmd`
  33 cases pass.
- 42-A.16, read in QEMU on 98 SE with the released `3.0.0.0` (folder
  `E:\xhci98-vm\p42-tr\a16`): XHCITR's Safely Remove of a Bulk-Only stick
  stopped that stick alone, at SuperSpeed and at High Speed, on a root
  port and behind a hub; a second stick kept reading, the tablet kept
  moving, and the stick came back when plugged again. The confirmation
  listed the stick's own three devnodes. `XHCIINST /eject` of `3.0.0.0`
  offers only Thunderbolt units, so it could not be compared. Not
  reproduced; no cause found. The level-2 log held no record of the stop,
  so `xhci98.sys` now writes `pnp.pdo`, `pnp.rh` and `pnp.hc` for every
  stop, query-remove, remove, cancel and surprise removal its device PDOs,
  root hub PDO and controller receive: the owner's Save Report on the
  ThinkPad P14s Gen 1 under 98 SE says which devices the PnP manager
  stopped. Still to read there: a UAS stick, the internal devices on the
  same root ports, the stick model. The task stays open.
- The gated build at `afc2b317` (`build-driver.cmd all` and `all
  -amd64`, every gate): BUILD + GATES PASSED on both.
- Then 3.1.0.0 (`b3b7f5e0`) merged in (`88fcc7e0`) and 42-A.5's tool
  part added (`5d3ec2dd`, Codex round 6's three P2s fixed in `31952bdd`,
  round 7 clean): per Thunderbolt unit on NT "Safe-removal mark: listed by
  Windows" / "mark written; takes effect at the next start of the device"
  / "mark written, but Windows does not list it" / "not marked", the NHI's
  "Mark service" line, and `XhciCmEjectConfirm` after the tool's own
  successful eject when it runs as an administrator, bound to the entry
  serial read before the call. `cm_live.h` names no PREPARING state yet,
  so none is shown. The gated build at `31952bdd`: BUILD + GATES PASSED on
  x86 and amd64; `test-package.ps1` 1146 checks.
- Legs at `31952bdd` on an x86 TEST cut (`-TestLabel "p42-tr legs"`;
  evidence `E:\xhci98-vm\p42-tr\legs\`):
  - The package has no `tools\xhcisnap\` (98 SE): pass.
  - 98 SE: `XHCITR /snapshot S1` redirected wrote `.DEV`, `.TXT`, `.BIN`,
    `.PSC`, the `.TXT` with the slots and root port enumeration; exit 0;
    `/dump -verbosity 2` and `/snaptest -on 2` refused; `/dump /o C:\D1`;
    `/snapshot`, `/probe` and `/snapshot /?` as message boxes;
    `/report` carries the Snapshot part: pass. The window's Save Snapshot
    and Save Report on 98 SE: owed.
  - 98 SE, 42-A.16's regression (the owner's case): stick 1 stopped from
    the window, stick 2 kept reading, the tablet kept working, stick 1
    read again after the re-plug; the report holds `pnp.pdo=00030104` and
    `pnp.pdo=00030204` (port 3, query-remove then remove) and no
    `pnp.rh` or `pnp.hc`: pass.
  - 7 x86: `/snapshot` from an elevated and a normal prompt, File > Save
    Snapshot..., `/report` with both controllers' dumps: pass.
  - 7 x86, 42-A.16: the stick stopped (Code 47), the other stick and the
    tablet kept working, the report holds the same two `pnp.pdo` records
    and nothing else: pass.
  - 7 x86 with the emulator's Thunderbolt layout, the mark service
    registered by hand (`XHCIMARK -register`): "Mark service: running
    (xhcimark)" and, after `net stop`, "stopped": pass. The NVMe unit
    (Code 28 on 7 x86, no NVMe driver) read "Safe-removal mark: not
    marked." while the service's log said "mark written" at 07:21:48 and
    "mark removed ... (no longer behind an active member)" one second
    later, the unit still behind an active tunnel port: a finding for
    42-A.5's service (`xhcimark`), not for XHCITR, which showed the state
    as it was. The disable and enable reading and the elevated Safely
    Remove with the confirmation: owed.
  - Owed besides: every x64 leg (the x64 package needs the owner's signing
    key), the 98 SE window's Save Snapshot and Save Report, and the A.16
    reading on the P14s Gen 1.
  - Found, not changed (for a later round): `/snapshot`'s and Save
    Snapshot's message boxes run past the bottom of a 640x480 or 800x600
    screen (Enter closes them); `/snapshot /?` names `XHCISNAP` as the
    default `/o` base (the engine's default, now misleading); on 7 `start
    /wait XHCITR ... > FILE` loses the output (cmd's start, not the tool).
  - On 98 SE the test cut's XHCIINST left the installed `xhci98.sys` in
    place (the same version string, 3.0.0.0, judged current); the leg
    copied the new driver over it by hand.

## 42-A.10: recovery of a contained USB4 host (2026-10-19)

Code on branch `p42-u4rec` (from `3.1.0.0` at `db921f49`): `cd9cf616`,
`2cee3dcc`, then the review's fixes `e0407f6c` to `6450dd09`, reviewed and
revised until the review converged at `6450dd09`; the emulator leg below
then found the 98 SE defect fixed in `963618bb`, `4198e863` and `81c7ec93`,
reviewed again until clean at `81c7ec93`. Built at `81c7ec93` with every
gate, `scripts\build-driver.cmd all` and `all -amd64`: both pass (the x86
`qemu` `xhcicm.sys` SHA-256 `3F1EFA7535F8DCE7...`); the host suites pass
(`test_u4cm` 1154 checks, `test_cm_live` 209, `test_cm_nvlink` 287).

What it is: record 22 section 7.7's recovery as revised for 42-0.5 -
CONTAINED's substates and PREPARING, the probe (configuration header first,
then the size register), the preparation (Command, the platform step, the
selection again), the recovery in the kept buffer after a verified stop,
the per-tunnel classification and drains, the PCI side revalidated, the
guarded commit, then the retirement barrier, the removal-only requests and
the per-tunnel rebuilds after it; the commit messages map every rule.

**The defect the 98 SE leg found** (on `6450dd09`): after `x-gate closed`
and `normal` the instance stayed contained for 199 probes with 597
`cm.config.failures`, every strict configuration read of the probe
refused, while `XHCITR` read the same header through `xhcicm.sys`'s
ordinary route. 98 SE's PCI bus driver completes `IRP_MN_READ_CONFIG` with
the data and reports no length, so the strict check never passed and the
recovery could not happen on 98 SE at all. A strict read that reports no
length is now made twice, through a buffer of all ones and then of zeros,
and counts only when both read the same (record 22 section 7.7 part 1).
The ICM profile's validation shares the route.

The legs, on the reviewed and gated `81c7ec93`: the `usb4-emu` QEMU of
`bin-2a893ea` (SHA-256 checked), i440FX, `usb4-host profile=usb4-generic`
behind a gated `usb4-tunnel-bridge` of its own (`nhirp`, `00:06.0`,
`x-force-tunnel=on`, as run-40's containment leg placed it), the tunnel
bridge `tb0` at `00:05.0`, the NVMe enclosure `enc0` attached at power-on;
the loss is the harness override `qom-set /machine/peripheral/nhirp x-gate
closed`, the return `x-gate normal` (BAR0 and configuration space all ones
and the NHI's DMA closed meanwhile, no PnP request, no endpoint reset: the
case of a platform that restores the host interface with its
configuration intact). Overlays: copies of the p42-cm1 legs' (the signed
`3.0.0.0` asset and Stor98 `0.4.0.0` installed in the guest), the NHI
installed again at its new place by the New Hardware wizard, then the
gated `qemu` `xhcicm.sys` copied over the installed one; every leg on a
cold boot. The data check is a 4 MB file copied from the transfer disk to
the NVMe volume and compared with `fc /b`.

**Windows 98 SE** (`l3`): F: written and compared clean; `x-gate closed`:
`cm.live.state=00000107`, `u4.contained`, the worker's
CONTROLLER_UNAVAILABLE removal of the NVMe devnode (Stor98 answered the
removal request, `Remove_SubTree` succeeded): F: gone, as record 22 says of
98 SE, so no I/O crosses the loss there. `x-gate normal` 5 s later, the
next probe: `u4.recover.probe=03000010` (header proven, size register as
programmed), PREPARING, the selection unchanged, `rings-kept`, the host
router adopted, port 1 classified the same router with its tunnel intact
(`u4.recover.class=00000103`, preserved), `u4.recover.ok`, no barrier,
ACTIVE; then the worker's re-enumeration, Stor98 bound again
(`cm.nvlink.9x.proof-read`), no new `cm.nvlink.up` (the entry kept UP at
generation 1), F: back with its files and the file written and compared
clean after it. Four more losses on the same boot: each contained and
recovered (episodes 2 to 5, the tunnel preserved every time); the
volume did not come back after episodes 2 to 5 while a DOS prompt sat on
F:\ (no Stor98 binding after the re-enumeration), and came back after
episode 6 with nothing holding it. **Pass**, with that observation.

**Windows 7 x86** (`l2`): F: written and compared clean; a copy to F:
started and `x-gate closed` while it ran: contained at once (`CmNvlinkLost`
found the NVMe still answering, `cm.nvlink.withheld`, no entry out of UP:
`u4.contained.lost-up=00000000`), and the copy, started a moment before
the gate closed, and its compare finished clean across the loss; `x-gate normal` 4 s later: the same recovery sequence,
preserved, `u4.recover.ok`, no barrier, ACTIVE, nothing published; a
third copy compared clean after it. **Pass.** A second loss inside the
attempt (the gate opened, then closed again 50 to 800 ms later, five
tries): at 600 and 800 ms the attempt was under way - the restart of the
rings in the kept buffer read all ones (`tbt.ring.tx-index=FFFFFFFF`), the
attempt failed (`u4.recover.refused=00000303`), the failure path's stop
not verified, the buffer kept, CONTAINED (cooldown) - and with the gate
open again a new episode began at once (the outage the attempt saw
counted as its all-ones probe), whose recovery verified the stop, found
the rings lost (`rings-lost`), restarted them and preserved the tunnel.
The other three tries closed the gate before the attempt began. **Pass.**
The final refusal (a stop that cannot be verified) was not reached: the
gate would have to close in the moment between the preparation's last
read and the stop's read-back.

**Owed, and seen:**

- A 98 SE boot (`l2`) where `x-gate closed` was followed by nothing: no
  containment, no further pass of the connection-manager thread (its last
  pass at the gate's closing, no NHI access after the gate reopened) and
  later a shutdown that hung at the hourglass; one of three 98 SE boots
  and seven losses. Not reproduced, not attributed.
- Windows 7 under this topology stopped QEMU itself (QMP and the guest
  frozen a few seconds into the start) with the SDL display, three cold
  boots in three; with `-display none -vnc` it ran every time. An
  emulator display matter, as the p42-nvw legs saw on 2000.
- Not exercised: the host router reset by the platform (the drain, the
  barrier, the rebuild), a device unplugged or replaced during the loss,
  a vetoed removal, the bind or resume the loss interrupts, a Thunderbolt
  3 router, Windows 2000 - 42-V.5's list, which the emulator's root
  hot-plug `restore` and `acpi` modes serve.
**After the merge of `3.1.0.0` (`2d2703b9`)**, at `3f646cc8` and its
review fixes to `03ce451c` (the AddDevice child filters counted for the
retirement proof, publication mode none kept through 42-A.12's held work,
the no-IRQ machine admitted against the retirement hold), reviewed until
clean: `build-driver.cmd all` and `all -amd64` pass every gate at
`03ce451c` (x86 `qemu` `xhcicm.sys` `2D6F769852663DDE...`). The 98 SE leg
read again on that build (`l4`, a cold boot): F: compared clean, two
losses each contained and recovered with the tunnel preserved
(`u4.recover.class=00000103`, `u4.recover.ok`, no barrier). As before the
merge, the volume came back after the first recovery and not after the
second (no Stor98 binding after that re-enumeration, the prompt on C:
this time, so not a volume held open); owed to the 9x Stor98 rebinding,
not read further.

### 42-A.5: a unit at Code 28 lost its mark (2026-10-19)

p42-tr's Windows 7 x86 leg read the service write an NVMe's mark (the NVMe
directly behind the tunnel port, at Code 28: 7 has no NVMe driver) and
remove it a second later "no longer behind an active member", the unit
still behind the active port. The marks answer can report a unit's entry
with no location - an entry made again after its member was inactive,
with no attachment of the driver left on the PDO to read the location
from - and the reconciliation read "no located entry here" as the unit
gone. Two changes, reviewed and revised until the review converged: the
service treats a reported, unlocated entry behind the member as an answer
it cannot judge (nothing removed, as for an incomplete one; `mk_listed`,
with the leg as a self-test vector), and the bridge filter keeps the
location read for each PDO of its snapshot while the PDO stays in it, so a
remade entry takes it back (`CmMarksLocate`, `test_cm_marks`). Read again
on the 7 x86 guest, the NVMe behind the port with no chain: the mark
written within a second of the plug, deleted by Windows' failed driver
search and written again, then kept (two minutes, "removable: yes ...
listed in Safely Remove Hardware" at Code 28); unplugged, removed within a
poll ("the unit left").

## Phase 42 integration: the installer's hooks, the bench cut and its smoke legs (2026-10-19)

Branch `p42-int` from `3.1.0.0` at `2d2703b9`, with `3.1.0.0` merged in
again at `916d6936` (42-A.4, 42-A.10) and `103974c2` (the `p42-ejrb` fix
`fda7a2fe`).

### What it adds

- **42-A.5's installer hooks** (`ca16de51`; record 21 section 7.4, "The
  installer"; record 23 revision 12): at the end of every NT install or
  update run, with `xhcicm` registered and the Thunderbolt component in
  the run on 2000 to 7, the mark service `xhcimark` copied from
  `tools\xhcimark\` into `system32` through a `.TMP`, compared byte for
  byte, registered (own process, automatic start, `LocalSystem`, the
  quoted path) or set up again, `system32\LogFiles\xhci98\` made, and
  started, counted only at `SERVICE_RUNNING`; with no `xhcicm` service
  left, step 0 instead. The uninstall's step 0 before the filter hosts:
  the service stopped (30 s), `Global\Xhci98MarkWrite` taken, `xhcicm`
  removed from every PCI devnode's `LowerFilters` (present or not, order
  kept, deleted when empty, read back), then `Marks`, the `system32`
  copy (`.DEL` and the delayed deletion when in use) and the service; a
  failure stops with steps left; the inventory read again before
  `xhcicm`'s service and file go. `XHCIINST /eject` on NT writes
  `XhciCmEjectConfirm` after its own successful
  `CM_Request_Device_EjectA`. The Advanced row for the service's
  `Restart` is not offered (record 23 D16).
- **42-A.17's installer part**: `Services\USB` made at every install and
  update; the machine-wide value read by the drivers' rule, a value that
  is not a `DWORD` shown as unknown and replaced by a value chosen for it;
  every write there. `tuning.md`, the release notes and record 13 no
  longer say the installer writes the controllers' own keys.
- **The build gap** (`c4119049`): the x86 gated run rebuilds `XHCICLN`
  and `XHCITR`.
- **Version `3.1.0.0`** (`cebca284`), `DriverVer` 10/19/2026, so the bench
  package updates over `3.0.0.0`; 42.final's cut sets its own date.
- **A fix from the 98 SE update leg** (`be9b9bf6`): see below.

Reviewed and revised until the review converged (five rounds; the last
two clean, the merge resolutions of `3.1.0.0`'s merges and of
`916d6936` included). Host suite `test_inst`: 1374 checks, 0 failures.

### Builds

| Build | Result |
|---|---|
| `build-driver.cmd all`, `72c69b18` | BUILD + GATES PASSED (debug release qemu) |
| `build-driver.cmd all -amd64`, `72c69b18` | BUILD + GATES PASSED (debug release qemu) |
| `build-driver.cmd all` and `all -amd64`, `916d6936` (merge) | both PASSED |
| `build-driver.cmd all` and `all -amd64`, `103974c2` (the last merge) | both PASSED (debug release qemu); host tests 2080 checks, 0 failures |

The bench cut, at `103974c2`: `make-release.ps1 -Flavor release -Arch
x86 -TestLabel "bench, Phase 42 integration" -ReleasesDir out\benchrel
-UploadDir out\benchup -SkipUploadSet` with Stor98 `0.4.0.0` pinned
(`SHA256SUMS.txt` `ef7a2302...96b7`, interface 4; a `3.1.0.0` entry in
the cut's own copy of `history.md`, not the tracked one), its
`release-x86\` replaced by the staged `out\pkg-release-x86\` (which
carries Stor98) and `release-x64\` added from `make-package.ps1 -Flavor
release -Arch amd64`, unsigned (the x64 half of a cut needs the owner's
key); zipped as `out\xhci98-3.1.0.0-bench.zip`.

### The legs (development host A, QEMU, release flavour)

Kit `E:\xhci98-vm\p42-int` (QMP 48070 to 48079). The first package was
cut at `72c69b18`; the 7 x86 leg was read again on the final package.

- **Windows 7 SP1 x86, update over `3.0.0.0` with the Thunderbolt
  component** (`vm\win7.img`, the emulator's `tb3-alpine-ridge` host,
  the tunnel bridge `tb0` with the chained upstream port `1B21:2421` and
  an NVMe behind it, and the enclosure `enc0` plugged): `3.0.0.0`'s
  `INSTALL /yes /thunderbolt /notray /nopause` from its own package, a
  restart, both units listed (first plug). Then the bench package's
  `INSTALL /console /yes /thunderbolt /nopause`: every device updated to
  `3.1.0.0` and started, "The Thunderbolt safe-removal mark service
  xhcimark is installed and running: "C:\Windows\system32\XHCIMARK.EXE"",
  exit 3; `sc qc xhcimark`: `WIN32_OWN_PROCESS`, `AUTO_START`, the quoted
  path, `LocalSystem`; `XHCIMARK.LOG` in `system32\LogFiles\xhci98`. After
  the restart (and a re-plug of the enclosure, which the emulator drops
  across a guest restart) both units' `LowerFilters` read `xhcicm` and
  `Services\xhcimark\Marks` held both records. **Pass.** Then
  `INSTALL /console /uninstall /nopause`: step 0 stopped the service,
  removed both marks ("The safe-removal mark ... was removed from ..."),
  deleted `XHCIMARK.EXE` and the service, then the rest of the package
  went; afterwards no `LowerFilters` value under `Enum\PCI`, no
  `xhcimark` service, no `XHCIM*` in `system32`; exit 3. **Pass.** Read
  again from a fresh overlay on the final package (`103974c2`): the
  same, step by step. **Pass.**
- **Windows 98 SE, update over `3.0.0.0`** (a flattened copy of the
  `3.0.0.0` guest of 42-A.4's legs): the helper installed the controller
  and the root hub (`RESULT installed`), but the engine said "did not end
  as installed" with the Device Manager steps, exit 2: the setup engine
  had copied the new `xhci98.sys` to `XHCI98.TMP` and asked for the swap
  in `WININIT.INI` (`[rename]`, `xhci98.sys=...\xhci98.tmp`), since the
  loaded binary cannot be replaced, and the check read the old binary's
  version. After a shut down and a cold start both devices read "current
  (this release)" (the swap made). **Pass for the driver, fail for the
  installer's verdict on the first run**, fixed in `be9b9bf6`: the check
  now reads a pending rename of a file of the package version as
  "updated; the new driver file replaces the running one at the next
  start" with the restart flag. (This is the gold row 42-A.2's legs left
  owed.)
- **Windows 98 SE, fresh install and uninstall**
  (`vm\t26\win98-clean.qcow2`): the controller's wizard finished with no
  driver, then `INSTALL /console /yes /nopause`: "installed, restart
  pending", exit 3; after a shut down and a cold start the root hub came,
  the tablet's HID wizard installed from Windows' own `HIDDEV.INF`, the
  tray tool ran. `INSTALL /console /uninstall /nopause`, `REMOVE`, the
  wizard word: both devnodes removed through the helper, the `INF\OTHER`
  copy and its `.PNF`, `XHCI98.SYS`, the tray tool, the Add/Remove
  Programs entry and `C:\XHCI98` gone, exit 3. **Pass.**

Not taken here: `XHCIINST /eject`'s confirmation in a guest (host vectors
only), the window's settings pages over a machine-wide value that is not
a `DWORD`, 2000, XP, Vista and the x64 targets.
