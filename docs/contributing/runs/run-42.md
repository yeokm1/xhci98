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

### 42-A.24: the CriticalDeviceDatabase copies of xhcicm (2026-10-19)

The matrix's 2000 legs found `LowerFilters=xhcicm` under
`Control\CriticalDeviceDatabase\pci#cc_0604` and `pci#cc_010802`, copied
there by Windows from a marked unit's install, and the filter INF's
`UpperFilters` under the tunnel port's id; every later bridge and NVMe
drive took them with no record, and after an uninstall a new NVMe drive
stopped at Code 19. XP and 7 x86 made the `cc_0604` copy for a marked
bridge's driver installed again. The fix (record 21 section 7.4 "As
built", record 23 section 9.2 step 0): the service takes `xhcicm` out of
every entry's `LowerFilters` at every poll, on every system it runs on
(2000 to 7), and writes no new mark while that scrub is unproven; its
inventory runs at once after a hit and every minute, an unrecorded
device-level mark recorded only to be judged (kept on a unit behind an
active member, removed elsewhere); the uninstall's step 0, and any run that
ends with no `xhcicm` service, take `xhcicm` out of every entry's
`LowerFilters` and `UpperFilters` first, then every PCI devnode's, and read
both again before the records and the service go; `XHCICLN` lists such
values and points at that uninstall (`inst_filter_drop`, with host vectors;
the service's self-test vectors). Reviewed until the review converged;
gated builds passed, x86 then amd64, at 5ac042b9.

Legs, release flavour, the gated binaries in the bench package:

- **2000** (a copy of the matrix's 2000 disk, its leak cleaned by the new
  service's first poll): after a plug and install the database held only
  the filter INF's `UpperFilters`; a never-seen chain (USP, DSP, NVMe) got
  a recorded mark on its unit and nothing on the DSP or the drive; the
  uninstall removed the `UpperFilters` copy and the unit's mark, and
  afterwards nothing under `Enum\PCI` or the database named `xhcicm`;
  booted with a new bridge, an NVMe drive behind it and another on bus 0,
  all three started (Code 0). Installed again on a two-port USB4 host
  (`usb4-generic`): a second enclosure on the other tunnel port was marked
  and listed, the `cc_010802` copy 2000's Found New Hardware install of
  Stor98 made for its drive was taken out at the next poll ("Windows copied
  it there from a marked unit's install"), and an NVMe drive added at the
  next boot and installed through the wizard came up with no filter. The
  unit present when the new `xhcicm` loaded was not marked until the
  restart (the matrix's XP finding; 3.0.0.0 did the same).
- **7 x86**: a new bridge added at boot while 3.0.0.0's service ran had no
  filter; updated to this build with a chain and an NVMe drive at Code 28
  behind the port, both units were marked and kept, the database held only
  the filter INF's `UpperFilters`; the uninstall left no value naming
  `xhcicm` among 178 read.

The eject on 7 (`cm.reenum.eject.stale` after every confirmation): the
eligibility now reads a function at Code 28 - only this driver's
BusRelations child on its PDO, nothing above it - as without a stack, with
the member held against a start until the hook returns (host vectors for
`CmPortsStackless`). Read again on 7 x86 the same layout still ended
`stale`: the ejected bridge's PDO keeps ACPI's bus filter
(`cm.reenum.child.under` `ACPI`) after its stack is removed. Open: telling
that filter from a rebuilt stack needs another driver's chain top read
safely, an import this driver does not have; under the USB4 profile on an
ACPI machine an eject leaves the tunnel up until the unplug.

### 42-A.24, continued: the service entries, and the mark service following XhciCmSafeEject (2026-10-19)

The matrix found `pci#cc_0c0340` and the NHI's own id left in the
CriticalDeviceDatabase naming `xhcicm` after an uninstall (2000, XP and 7),
the NHI back at Code 19 after the restart. The uninstall now deletes every
entry whose `Service` is a service of the package that is gone (record 23
section 9.2 step 6a, `inst_cddb_service_gone` with host vectors; Stor98's
only when its removal took the service), and every NT install run ends with
the same pass. Read on the 2000 disk on a two-port USB4 host with the
connection manager and Stor98 installed: the uninstall deleted the two
entries naming `xhcicm` and kept `pci#cc_010802` naming the Stor98 a drive
still used; after the restart the NHI had no driver (Code 1 on that disk,
as before the first install; not Code 19) and no entry named a removed
service. `xhci98` and `xhciuas` left no entry on that 2000 disk, nor on
the XP guest with the package installed (its database named `xhcicm` for
the NHI's id alone). Gated builds passed, x86 then amd64, at 3fec8fd2 (the
merge of 3.1.0.0 over this work).

The owner's decision: the mark service follows the `XhciCmSafeEject` in
force after a run (record 23 section 5.1 step 12, `inst_mk_safe_eject` and
`inst_mk_end_action` with host vectors). Read on 7 x86: an install with
`/set XhciCmSafeEject=0` left no `xhcimark`; `/set XhciCmSafeEject=1`
installed and started it, and after the restart and a plug the unit was
marked; `/set XhciCmSafeEject=0` again stopped and deleted the service and
took the unit's mark out, nothing in the database naming `xhcicm` in a
`LowerFilters`. The first run of that leg also took the filter INF's
`UpperFilters` copy out of the database while `xhcicm` stayed; that copy
is the tunnel-port filter's, and a run with safe eject off now leaves it.

Reviewed until the review converged (its last round found one wording
point in these records, fixed).

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
  owed.) Read again on the bench zip as cut (SHA-256 `7034b1b5...6cad1b`)
  from a fresh overlay: both devices "updated; the new driver file
  replaces the running one at the next start", no steps, "Changes were
  made. Please restart Windows now.", exit 3; `WININIT.INI` held
  `xhci98.sys=...\xhci98.tmp` (`XHCI98.TMP` 300,160 bytes beside the old
  299,298). After a shut down and a cold start `XHCI98.SYS` was the
  300,160-byte file, the `.TMP` gone, and `/list` read both devices
  "current (this release)" (the base image's old `DEV_04` entry, not
  present, still "install (no driver)" as on every run). **Pass.**
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

## 42-A.21: the connection manager alone, and Windows 8 and 8.1 for it (2026-10-19)

Branch `p42-cmo`, from `3.1.0.0` at `3e35879f`. The owner, 2026-10-19:
"The installer should give people the option not to install xhci98 but
just the CM. Reason is that in case some Win 8 users which have native
USB3 but not USB4 or TB may want it"; and, the same day, "let user update
existing driver by installing over it. Should not have multiple entries
in Add remove programs in Control Panel." Design: record 23 revision 13
(section 4.5), record 26 draft 8, record 21 section 3.5; reviewed and
revised (`24eec496`, then `7cc49d41`, `dfc07321`, and the leg fix below).

### What it does

- On 2000 to 7 the USB drivers (`xhci98.sys`, `xhciuas.sys`,
  `xhcihlpr.sys`) can be left out: the window's box, or `/cmonly`; the
  Thunderbolt support is then in the run unasked and the box locked on.
- On Windows 8 and 8.1 (NT 6.2 and 6.3, read through `RtlGetVersion`,
  since `GetVersionEx` answers 6.2 to these tools on 8.1, 10 and 11) the
  connection manager alone is the only install; the bundled Stor98 is not
  offered there. 10 and later are refused, as before.
- On 98 SE and ME the USB drivers stay required; `/cmonly` is refused.
- A run without the USB drivers never updates or removes an installed
  `xhci98.sys`; it says so. One Add/Remove Programs entry,
  `Uninstall\xhci98`, named by what is registered.

### Builds

| Build | Result |
|---|---|
| host suites, `7cc49d41` | 2080 checks, 0 failures (`test_inst` 1431) |
| `build-driver.cmd all`, `dfc07321` | BUILD + GATES PASSED (debug release qemu) |
| `build-driver.cmd all -amd64`, `dfc07321` | BUILD + GATES PASSED (debug release qemu) |
| `xhciinst\build.cmd` and `selftest.cmd`, the final head | passed (the four executables, their imports, the screens of 8.1 and of 7 with `/cmonly`) |

The legs' package: `make-release.ps1 -Flavor release -Arch x86 -TestLabel
"connection manager alone legs" -ReleasesDir out\cmorel -SkipQualtool
-SkipUploadSet` at `dfc07321` (no Stor98), `xhcimark\`, `xhciinst\` and
`xhcitr\` rebuilt by hand first (the gated build rebuilds neither `xhcimark` nor
`xhciinst`; its x86 run rebuilds `xhcitr`, which a later edit had made
older than its sources again).

### The legs (development host A, QEMU, release flavour)

Kit `E:\xhci98-vm\p42-cmo` (QMP 48080 to 48089): Windows 7 SP1 x86 on a
fresh overlay of `vm\win7.img` (no xhci98), `qemu-xhci` with a tablet and
a stick, the emulator's `tb3-alpine-ridge` host and its tunnel bridge
`tb0`, the enclosure `enc0` plugged by QMP. The package reached the guest
on an ISO; the tablet has no driver without xhci98, so the publisher
prompts were answered with the PS/2 mouse.

| Step | Result |
|---|---|
| `INSTALL /console /cmonly /yes` on the clean guest | **pass**: the plan named only the NHI and the tunnel port; `xhcicm.inf` and `xhcicmf.inf` staged and nothing else; the NHI on `xhcicm` and the port filtered, both started; `xhcimark` installed and running; no `xhci98` service; Add/Remove Programs "xHCI98 Thunderbolt 3 / USB4 connection manager 3.1.0.0", one key; exit 3 |
| restart, `enc0` plugged, `C:\XHCI98\XHCIINST /devices` | **pass**: the device approved, PCIe up, the NVMe behind the tunnel enumerated (Code 28: no NVMe driver on 7), "removable: yes ... listed in Safely Remove Hardware" |
| `XHCIINST /eject`, device 1 | **pass**: "Windows has stopped PCI Device", exit 0 |
| `INSTALL /console /yes` over it (the whole package) | **pass**: the same local copy reused; `xhci98.inf` and `xhciuas.inf` staged; both xHCI controllers (`qemu-xhci`, the emulator's) on `xhci98`, pending the restart as 32-bit 7 does; the entry renamed "xHCI98 USB 3.x drivers 3.1.0.0", still one key |
| `INSTALL /console /cmonly /yes` over the whole package | **pass**: "The xHCI98 USB drivers already installed stay as they are", the `xhci98` service kept, the entry keeps its name; exit 0 |
| restart, `INSTALL /console /uninstall /yes` | **pass**: the usual warning; every service, file, INF, the copy, the directory and the entry removed; exit 3 |
| restart, `INSTALL /console /cmonly /yes` again, then `INSTALL /console /uninstall` | **pass**: the connection manager's warning ("This removes the xHCI98 Thunderbolt 3 / USB4 connection manager") and `REMOVE`; the mark service, the filter (the port given back to `machine.inf`), the NHI, both INFs, `xhcicm`, the tools and the entry removed; afterwards no entry, no `xhcicm` or `xhcimark` service, no `LowerFilters` under `Enum\PCI` |
| the window: `INSTALL` | **pass**: the USB drivers' box enabled; unticked, the Thunderbolt box ticked and disabled with the sentence "Left out: the connection manager alone ..."; the USB settings page skipped, the Thunderbolt page shown; the Summary "xhci98.sys and xhciuas.sys: left out (the connection manager alone)"; Install, the Finish page's Restart |
| the window's uninstall, `C:\XHCI98\XHCIWIZ.EXE /uninstall` | **pass**: the title "Remove the xHCI98 connection manager", the connection manager's warning and box; removed; after the restart `C:\XHCI98` and the temporary copy gone |

The leg found one wording defect: a run without the USB drivers ended
with "If the controller was updated from 2.3.x on 32-bit Vista or 7, run
INSTALL.BAT again ..."; that sentence now needs the USB drivers in the
run.

Not taken: Windows 8 and 8.1 (no image of either on this host; none was
downloaded); Windows 10 or 11's refusal in a guest (the one Windows 11
guest here, `usb4-emu`'s, did not boot under a new overlay and its own
emulator build; the refusal rests on the host vectors of `inst_os_kind`
and the code read); 2000 and XP; 98 SE's `/cmonly` refusal (host vector
only); the x64 half.

## 42-A.12 follow-up: busy waits serviced, held work through a loss (2026-10-19)

Code on branch `p42-callfix` (from `3.1.0.0` at `5f63e8e4`): `f0e10a02`,
then `2f02d5de`, `231fe2ca` and `1ec1a7e2`; reviewed and revised until the
review converged at `1ec1a7e2`. Built there with every gate,
`scripts\build-driver.cmd all` and then `all -amd64`: both pass (x86
`qemu` `xhcicm.sys` SHA-256 `401C566CA834DB78...`); `test_cm_nvlink` 449
checks, 0 failures.

Four points were raised against the merged 42-A.12 code, read at
`4a31f545`; each was read again at `5f63e8e4`:

- **The servicing of the acknowledgement waits inside a pass.** Long
  sleeps: already taken in 50 ms pieces with a step after each while a
  wait is pending (`bddf936d`), and every mark that starts a wait runs on
  the NHI's own thread, so a sleep begun with nothing pending in its
  domain cannot gain one. Busy waits: real. A wait's first 2 ms are
  stalls, so a run of requests that each end inside them (the TB3 EEPROM
  reader's hundreds) slept nowhere and stepped nothing. `cmStall` now
  steps the waits once per 50 ms of the clock since the last step
  (`CmNvlinkServiceDue`, pure; `cmSleep` records its own step).
- **Held and owed work during a host-interface loss.** Real on NT: the
  unavailable reconciliation's action, finding a member's gate down,
  folded that member's held work and owed change into its own call, with
  the owed exposure, while the transport was lost; and the pass's
  owed-change loop exposed and called with no transport check. Both now
  wait for the transport (`cmTransport`), as the held loop already did.
  With the gate up, the reconciliation's call is held as an obligation of
  its own (`CM_HELD_LOSS`), made once the gate falls whatever the
  transport - a removal of vanished children with no exposure, behind the
  retirement barrier too - and paid instead by any call made with the gate
  down, so it is made once.
- **An action between the held path and the owed path.** Not real at
  `5f63e8e4`: `bddf936d` made the action's call pay the owed change too.
  Its vector (held, owed, then an action before the pass: one call) is
  added.
- **A result finished before the stop, handed over on the stopping
  thread.** Not real at `5f63e8e4`: since `bddf936d` the stop only marks
  the slots and wakes the workers, and each worker hands an outcome the
  thread did not take to the late taker as it leaves; `test_cm_call`
  covers both orders.

The leg (development host A, QEMU, the 42-A.10 topology and the `l4`
overlay's copy, `E:\xhci98-vm\p42-callfix`): **Windows 98 SE**, the gated
`qemu` `xhcicm.sys` of `1ec1a7e2` copied over the installed one, then a
shut down and a cold start (`c2`), `enc0` at power-on. The NVMe `UP`
published (`cm.nvlink.up=01000101`), `DATA.BIN` written to F: and compared
clean; `x-gate closed`: contained (`cm.live.state=00000107`), the worker's
CONTROLLER_UNAVAILABLE removal answered by Stor98; `x-gate normal` 8 s
later: `u4.recover.probe=03000010`, `rings-kept`, the tunnel preserved
(`u4.recover.class=00000103`), `u4.recover.ok`, no barrier, ACTIVE, the
re-enumeration, Stor98 bound again (`cm.nvlink.9x.proof-read`); F: back,
a second file written and compared clean. **Pass.** The same sequence as
42-A.10's `l4`. The NT paths the change is about (the reconciliation
behind a gate, a pass of busy waits with a wait pending) were not reached
in a guest: on 98 SE no statement is made, so no wait is pending; they
rest on the host vectors.

## XHCITR on Windows 2000: the disk names, the letters and the stopped mark (2026-10-19)

The `3.1.0.0` matrix read two faults on Windows 2000 SP4 with the bench
cut: the tray menu and the tree named a Bulk-Only stick and a UAS disk
by the USB device's own name with no drive letter, where 98 SE reads
"... (F:)", and a device stopped from the tray showed no
"[stopped - unplug it]", which the README promises. Branch `p42-trfix`
from `3.1.0.0` `4b6d3477`: `5494c288` (the fix), `e1ff85f5` and
`d74bf6cf` (revisions after review), `def3d558` and the record's
revision 15 (design record 25 sections 6.5 and 14); roadmap-usb4 task
42-A.28.

**The causes**, read in a 2000 guest (below) with the bench's
`XHCITR.EXE` (SHA-256 `a9931e76...48d787`):

- The disk. `HKLM\SYSTEM\CurrentControlSet\Enum\USBSTOR\Disk&Ven_QEMU&...`
  holds `ClassGUID` `{4D36E967-...}`, `DeviceDesc` "Disk drive" and
  `FriendlyName` "QEMU QEMU HARDDISK USB Device", and no `Class` value.
  `CM_DRP_CLASS` answered "no such value", so the node's setup class was
  empty and it was no disk: no friendly name was read, `disk_letters_nt`
  skipped it, and the label fell back to the USB device's name. The
  device was still offered because its removal relations hold the
  volume ("Generic volume"). The letter lookup itself works on 2000: a
  scratch program in the guest read every disk interface by its
  instance ID and `IOCTL_STORAGE_GET_DEVICE_NUMBER` on each `\\.\X:`
  (`E:` 7/3 for the stick, `F:` 7/2 for the UAS disk, matched). XP and
  7 write the `Class` value (7's bench report already named both disks
  with their letters).
- The mark. After the stop ("Windows has stopped USB Mass Storage
  Device. You can now unplug the device.") the devnode read status
  `0x01806400`, problem code 24, not 47: 2000 has no Code 47. Only the
  after-stop reading knew Code 24 (run 41); the rule saw no disk and no
  problem it knew, and said "not offered - no disk on it".

**The fix.** `dn_read` takes an empty class name from the class key the
devnode's `ClassGUID` names. `tr_held_for_eject` (Code 47, or Code 24
when allowed) now decides the USB rule, the tree and tray suffix,
`tr_stopped_text` and `tr_usb_after_kind`. Since Code 24 is also 2000's
failed start, it is allowed only for a devnode this process stopped (the
USB stop's after-reading at Code 24, a unit's accepted stop), until it
starts again or leaves a whole walk, and, at a USB port, only while the
port still reports a configuration (`tr_w2k_code24`): the stopped device
kept "Configuration: 1", and one plugged in again starts unconfigured. A
stop made elsewhere (Windows' own Unplug or Eject Hardware) reads as
before. Reviewed and revised twice (`e1ff85f5`, `d74bf6cf`); the last
review found nothing.

**Tests and builds.** `xhcitr\build.cmd`, `selftest.cmd` (11 and 10
checks), `snaptest.cmd` (33 cases) and `test\run-host-tests.cmd`
(`test_tr` 909 checks) passed; the gated build on `def3d558`,
`build-driver.cmd all` then `all -amd64`, one after the other:
BUILD + GATES PASSED (debug release qemu) on both, the build record
naming `def3d558` with no uncommitted change. The leg binary, `XHCITR.EXE` SHA-256
`018aef42...877ff7` (`XHCITR64.EXE` `f7f6002d...7e8554`), was built
from `d74bf6cf`'s sources, which `def3d558` does not change.

**The legs** (QEMU only, `vm\usb4\bin-2a893ea`, headless; QMP
48190-48191; a Bulk-Only stick `usb-storage` on root port 3 and a
`usb-uas` + `scsi-hd` disk on root port 4, both on
`vm\u4-asm\base\nvme-fat.qcow2` overlays, a `usb-tablet` on port 2;
the bench package by hand, `INSTALL /console /yes`, exit 3, then the
new `XHCITR.EXE` copied over `C:\XHCI98\XHCITR.EXE`):

- **Windows 2000 SP4** (`vm\t26\win2k-clean.qcow2`, `pc,acpi=off`,
  one vCPU). Before (bench): as the matrix read it, plus the causes
  above. After: the tray menu read "Safely remove QEMU QEMU HARDDISK UAS
  Device (E:)" and "... USB Device (F:)", the tree the same names with
  their letters. The stick stopped from the tray: the balloon, then the
  tray item "Safely remove USB Mass Storage Device (stopped - unplug
  it)" greyed, the tree "[Port 3, USB 3] USB Mass Storage Device
  SuperSpeed [stopped - unplug it]", the details "Safely Remove: not
  now - stopped - unplug it", Code 24, "Configuration: 1". Unplugged:
  "(empty)"; plugged in again: started, "can be stopped", "(F:)" back
  in the tray. The UAS disk stopped from the tray: "[stopped - unplug
  it]", Code 24; the emulated UAS device does not attach when added to
  a running machine (section 14), so its return was read at the cold
  start, with its letter. The stick stopped from Windows' own icon:
  Code 24, "not offered - no disk on it", no mark (not stopped here).
  **Pass.**
- **Windows 7 x86** (`vm\win7.img`, two vCPUs): the bench already named
  "QEMU QEMU HARDDISK USB Device (D:)" and "... UAS Device (F:)"; the
  new tool the same. The stick stopped from the tray: Code 47, "[stopped
  - unplug it]"; plugged in again: started, "(D:)" back in the tray. The
  UAS disk stopped: Code 47, "[stopped - unplug it]". **Pass**, no
  change from the bench.

Not taken here: XP (the same Code 47 path as 7; whether its USB disks
carry the `Class` value was not read, and either way is now handled), 98 SE and ME (their letters come from
`CurrentDriveLetterAssignment`, a path this change does not touch), a
Thunderbolt unit stopped on 2000 (the same `tr_w2k_code24` rule, no
port), and the x64 tool.

## 42-A.27: the hub's job after its root hub's reinstall (2026-10-19)

Branch `p42-hubsettle`, from `3.1.0.0` at `08709480`. The matrix leg of
the bench package on Windows 98 SE (an overlay of the `3.0.0.0` image,
`qemu-xhci` with a `usb-hub` on port 1 and the tablet on port 2) ended
the update over `3.0.0.0` with "Changes were made, but not everything is
done" (exit 4) and a step for "xHCI98 USB Hub: The helper could not
install it: not present now". Its log: the controller's job and a settle;
the root hub's job (`installed`) with no settle after it; at once the
hub's job, whose `DiGetClassDevsEx(DIGCF_PRESENT)` found nothing
(`not-present`); then the pass's settle (4305 ms, devices moved), after
which the hub was back under `xhci98`. The root hub's reinstall restarts
it and takes its hub away for a few seconds; the next start showed
everything current. An engine defect, not the driver's.

### What changed

Design: record 23 revision 14 (section 5.8, "A parent's job and the
devices below it", and the pure-half list of section 10), record 26
draft 9 (section 7), record 27 draft 8 (section 6, step 3); reviewed and
revised. Code (`a0ff9fc6`, `04f9fb36`, `ee886ce5`):

- A parent is a controller or a device of the bus (root hub, hub,
  composite entry). Within a pass, a parent's job that reached its device
  is followed, when another job of the pass comes after it, by a wait of
  up to 15 s for the parent to be present and the settle with no device
  expected (`reappear_settle`); 98 SE and ME in `helper_installs`, NT in
  `install_inf` after a successful call on a bus device.
- A job that finds its device not present after a parent's job earlier in
  the run (the helper's `not-present`; on NT `ERROR_NO_SUCH_DEVINST`)
  waits up to 15 s for it, settles, and is made once more; only then is it
  a step (or, on NT, a failure). Not for Stor98's devices, and not once the
  helper is out of the run.
- A wait there that fails or is cancelled stops the run: `install_inf`
  returns -1 and `run_install_nt` goes to its stopped path; on 98 SE and
  ME `helperStop` ends the passes.
- Pure decisions in `inst_core.c`: `inst_parent_kind`,
  `inst_settle_after_job`, `inst_absent_retry`, with 20 host vectors in
  `test_inst`.

Functions touched: `inst_core.c` (the three above, new);
`inst_eng.c` `nt_more_after` and `reappear_settle` (new), `install_inf`,
`run_install_nt` (its two `install_inf` calls), `helper_install`,
`helper_installs`.

### Builds

| Build | Result |
|---|---|
| `xhciinst\build.cmd` and `selftest.cmd`, `ee886ce5` | passed (the four executables and `XHCIDI.EXE`, their imports and screens) |
| host suites, `ee886ce5` | 2080 checks, 0 failures (`test_inst` 1452) |
| `build-driver.cmd all`, `04f9fb36` (the gated sources unchanged since) | BUILD + GATES PASSED (debug release qemu) |
| `build-driver.cmd all -amd64`, `d8dbd272` | BUILD + GATES PASSED (debug release qemu) |

### The legs (development host A, QEMU, release flavour)

Kit `E:\xhci98-vm\p42-hubsettle` (QMP 48220 to 48229): the bench package
`E:\xhci98-vm\m31\pkg-bench` with `tools\xhciinst` replaced by the
executables of `ee886ce5` (`XHCIDI.EXE` unchanged), `qemu-xhci`
`p2=4,p3=4`, a `usb-hub` on port 1 at power-on, the tablet on port 2.
Logs under `caps\`.

| Step | Result |
|---|---|
| 98 SE, a fresh overlay of `w98-300.qcow2` (`3.0.0.0` installed; the image N1 used), `INSTALL /console /yes` | **pass**: the controller's job and settle; the root hub's job `installed`, then "Waiting for the devices to settle ... [4564 ms, devices moved]"; the hub's job `FOUND 1`, `installed`; all three "updated; the new driver file replaces the running one at the next start"; no steps; exit 3 |
| shut down, cold start, `INSTALL /console /list` | **pass**: controller, root hub and hub on `xhci98`, "current (this release)"; nothing to do, exit 0 |
| ME, the stock image with the hub: `INSTALL /console /yes` (fresh), shut down, cold start; Windows installed the root hub and hub from the copy | exit 3 (controller `installed-restart`) |
| ME, `INSTALL /console /yes /force` (the same ordering: root hub, then hub) | **pass**: the root hub's job, the settle (3031 ms, nothing moved on ME), the hub's job `FOUND 1`, `installed`; no steps; exit 3 |
| ME, shut down, cold start, `INSTALL /console /list` | **pass**: all three `xhci98 3.1.0.0`, current; exit 0 |

The registry-only "PCI Universal Serial Bus" at `DEV_04` the listings
show is an Enum entry of the base images with no devnode, as in N1's
logs; the run does not touch it. Not taken: the window (the console runs
the same engine), NT (no hub ordering leg; the NT rule rests on the code
read and the vectors), ME over `3.0.0.0` (no such image).

## 42-A.29: `XhciFastPollFsLs` among the USB page's "Advanced..." rows (2026-10-19)

Branch `p42-fastpoll`, from `3.1.0.0` at `08709480`. The owner, 2026-10-19:
"XhciFastPollFsLs should be in advanced settings." Design: record 23
revision 15 (section 8.1), record 26 draft 10 (sections 5.4 and 11);
reviewed and revised (clean, then a clean confirmation of `3733302e`).

### What it does

- A new row of `instValues`: an `xhci98.inf` value on every system, never
  asked, so the window shows it behind the USB settings page's
  "Advanced..." and the console sets it with `/set` alone. Allowed 0, 2,
  3 and 4; 1 is not a mode and is refused, as is 5. The default stays the
  INF's `AddReg` line (0); the value is written machine-wide under
  `Services\USB` like the other `xhci98.inf` values.
- The choices: 0 off; 2 a root-port mouse set to 31/62 Hz in hidusbf runs
  at 2000/4000 Hz; 3 the same at 4000/8000 Hz; 4 the rate the client
  chooses, codes 128 to 143. The caution, shown with every value: outside
  the xHCI specification for Low- and Full-Speed devices, read when the
  controller starts so restart after changing it, and with 4 the value
  set and Windows restarted before the filter's code. By the owner's
  instruction it says nothing of value 4's earlier meaning.
- `INST_VALUE`'s choice list is `INST_CHOICES` (5) entries long, so value
  4 has its text; the masks already allowed a gap. The two lookups in
  `inst_core.c` and the two in `xhciwiz.c` (the row's note and
  `row_fill_combo`) take the constant in place of 4; nothing else in
  `xhciwiz.c` changed. `XHCITR` and `XHCICLN` compile `inst_core.c` but
  read no choice text.

### Builds

| Build | Result |
|---|---|
| host suites, `544a9640` | 2080 checks, 0 failures (`test_inst` 1488) |
| `xhciinst\build.cmd` and `selftest.cmd`, `544a9640` | passed (the four executables, their imports, the switch refusals) |
| `build-driver.cmd all`, `544a9640` | BUILD + GATES PASSED (debug release qemu) |
| `build-driver.cmd all -amd64`, `3733302e` | BUILD + GATES PASSED (debug release qemu) |
| `xhcitr`, `xhcicln`, `xhcimark` `build.cmd`, `3733302e` | built by hand (the gated build rebuilds none of them) |
| after the merge of `3.1.0.0` (`cfcef657`), `646762d8`: `xhciinst\build.cmd`, `selftest.cmd`, host suites, `build-driver.cmd all` | passed; 2080 checks, 0 failures (`test_inst` 1515); BUILD + GATES PASSED (debug release qemu) |

`3733302e` changes only the two records' revision notes.

### The leg (development host A, QEMU)

Kit `E:\xhci98-vm\p42-fastpoll` (QMP 48230 to 48239): Windows 98 SE on a
fresh overlay of the gold image (an older xhci98 installed), `qemu-xhci`
with a tablet, the package a copy of the `3.1.0.0` bench package with
`XHCIINST.EXE` and `XHCIWIZ.EXE` of `544a9640`, on the transfer drive.
Cold start only; the guest was powered off at the end, never restarted.

| Step | Result |
|---|---|
| `INSTALL /console /set XhciFastPollFsLs=1` | **pass**: "xhciinst: /set: not an allowed value: XhciFastPollFsLs=1", then the usage; nothing done |
| `INSTALL`, the USB settings page, "Advanced..." | **pass**: the row `XhciFastPollFsLs`, "now: 0 (the default)", beside `XhciSelectiveSuspend`; the note carries the meaning, the four choices and the caution; the combo box offers 0, 2, 3 and 4, no 1 |
| 4 chosen, OK, Next | **pass**: the row "4 (to be written)"; the Summary's "The settings written machine-wide: - XhciFastPollFsLs = 4" |
| Install, Finish with "Restart Windows later" | **pass**: the controller updated; the Finish page lists "XhciFastPollFsLs: set to 4 on the settings page" with its note |
| `regedit /e` of `HKLM\System\CurrentControlSet\Services\USB` | **pass**: `"XhciFastPollFsLs"=dword:00000004` |

Seen, not changed: the combo box's drop-down is as wide as the box, so the
two long choices of 2 and 3 are cut at its edge there; the note above it
carries them whole.

Not taken: the driver reading the machine-wide 4 after a cold start, and
hidusbf codes (42-V.2's leg); Windows 7 and the x64 half.

## 42-A.25: no republication after a resume left a bus unnumbered (2026-10-19)

The matrix reading on Windows ME that morning (the bench zip, two USB4
NVMe enclosures on the emulator's `usb4-generic` host, `tb0` at `00:05`
for port 1 and `tb1` at `00:06` for port 2): after a Stand by and resume
with `enc0` attached, plugging `enc1` into port 2 dropped the volume of
port 1. The log: at the resume the core rebuilt port 1's tunnel
(`cm.nvlink.u4-up=00010002`), and the UP hook's and the seam's exposures
of it found the secondary bus not yet numbered (`cm.nvlink.up.no-bus`),
so only the bridge's tunnel record took it; at `enc1`'s tunnel up the
seam's exposure of port 1's member, naming no tunnel, published for that
record and advanced port 1's entry (`cm.nvlink.up=02000101`) before port
2's own (`01000201`). Stor98, bound since before the sleep, then ended
that disk.

Code on branch `p42-nvres` (from `3.1.0.0` at `4b6d3477`): `0213636d`
(`CmNvlinkPublishUpEx`: only an exposure that itself names a new tunnel
advances an entry already `UP`; one that names none labels what it
publishes with the bridge's recorded tunnel and never advances; host
vector `test_resume_no_bus`), `b860175b` and the merge `d26ccedf`
(records 21 section 7.2 and 22 section 10, with their revision notes).
Reviewed: the first round found nothing. Built at `d26ccedf`, the
current `3.1.0.0` merged in, with every gate, `scripts\build-driver.cmd
all` and `all -amd64`: both pass (`test_cm_nvlink` 473 checks).

**Windows ME** (the guest of the morning's reading copied, the gated
`qemu` `xhcicm.sys` of `0213636d` copied over the installed one, cold
starts only): `enc0` at power-on (`cm.nvlink.up=01000101`), F: compared;
Stand by and resume as the reading did (`cm.nvlink.u4-up=00010002`,
`cm.nvlink.up.no-bus=00000028` and `00000030`); `enc1` plugged 60 s after
the resume: one publication, its own (`cm.nvlink.up=01000201`), none for
port 1; F: and G: both compared clean. Pass. A first attempt with `enc1`
plugged at once after the wake saw no tunnel at all: the connection
manager made no ring access after the plug and the emulator dropped port
2's hot-plug frames ("fifo overflow"); no publication was reached, so it
says nothing of this change; not seen again, unattributed.

**Windows 7 x86** (the guest of the 42-A.6 legs, two enclosures, a
hibernation as its only sleep): the resume's own named exposure advanced
port 1's entry once (`02000101`, the tunnel being new) and `enc1`'s plug
published only its own (`01000201`); the rule of 42-A.25 changes nothing
on this path. But F: (port 1) hung in a read after the hibernate resume:
with this branch's `qemu` build (after `enc1`'s plug), its `release`
build of `0213636d` (before any plug) and the `release` build of the
merge `d26ccedf` (before any plug); and did not hang - read clean before
and after `enc1`'s plug - with the bench zip's `xhcicm.sys` nor with the
`qemu` build of `4a708676` (42-A.6's own). Not attributed: a change to
`xhcicm.sys` between the bench's source and `4b6d3477` is the likeliest
place; owed to a bisection.

## Matrix N1 F3 and F6: SETUPX's OEM Name record and /list's start at login (2026-10-19)

Branch `p42-instsmall`, from `3.1.0.0` at `cfcef657` (`1ed43f0b`, then
`8fa1060d`), with `3.1.0.0` at `94c22687` merged last. Two findings of the
matrix's 98 SE and ME legs (agent N1, `E:\xhci98-vm\m31\N1\RESULT.md`):

- **F3.** The 98 SE and ME uninstall deleted this package's `INF\OTHER`
  copy but left SETUPX's record of it,
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Setup\SetupX\INF\OEM Name`,
  value `C:\WINDOWS\INF\OTHER\Yeo Kheng Mengxhci98.inf` = `xhci98.inf`.
  Now each deletion of an INF copy of the package (`infs_9x`) or of a
  Stor98 copy the run owns (`s98_un_inf_9x`) is followed by
  `oem_name_forget`, which deletes only the values named by that copy's
  path or its short alias (`inst_oem_name_ours`, case aside).
- **F6, second half.** `INSTALL /console /list` after an update with the
  tray tool unticked said "... and started at login." with no Run value:
  the line was the planned action. It now says what the Run value is
  (present, absent, other data or unreadable) and then what a run with
  the same switches would do (`inst_tray_list_text`).

Design: record 23 revision 20 (sections 6.1 and 9.5), record 26 draft 14
(section 9.3 step 4); reviewed and revised. 13 host vectors in
`test_inst`.

| Build | Result |
|---|---|
| `xhciinst\build.cmd` and `selftest.cmd`, the merge | passed |
| host suites, the merge | 0 failures (`test_inst` 1564) |
| `build-driver.cmd all`, the merge | BUILD + GATES PASSED (debug release qemu) |
| `build-driver.cmd all -amd64`, the merge | BUILD + GATES PASSED (debug release qemu) |

The leg (development host A, QEMU, kit `E:\xhci98-vm\p42-hubsettle`,
the 98 SE overlay left by 42-A.27's leg with 3.1.0.0 installed and a hub
on port 1; the `1ed43f0b` executables):

| Step | Result |
|---|---|
| `INSTALL /console /yes /notray` | "The xHCI98 USB Tree no longer starts at login." (exit 4 for the base image's registry-only `DEV_04` controller entry, matrix F5, unrelated) |
| `INSTALL /console /list /log ...` | **pass**: "C:\XHCI98\XHCITR.EXE; it does not start at login now (a run without /notray makes it start at login)." |
| `OEM Name` before the uninstall (`regedit /e`) | `C:\WINDOWS\INF\OTHER\Yeo Kheng Mengxhci98.inf` = `xhci98.inf` beside Windows' own values |
| `INSTALL /console /uninstall /yes` | **pass**: "Deleted C:\WINDOWS\INF\OTHER\Yeo Kheng Mengxhci98.inf (and its .PNF). Removed the setup engine's record of it (SetupX OEM Name)."; exit 3 |
| `OEM Name` after it | **pass**: no value naming this package's copy; every other value (`USBSTOR.INF`, `USBNTMAP.INF`, Windows' own) kept |

Not taken: ME (the same code path), a Stor98 copy's record (no NVMe in
the 9x guests; the alias rule rests on the code read).

## The x64 package's filter INF at the flavour directory's root (2026-10-19)

The owner asked why the x64 package keeps the tunnel-port filter INF in
`nt\` when the files are the same as the root's, and decided, for
`3.1.0.0`, that it should not: Windows 98 and ME never read the x64
package, the driver store's same-directory rule (`SetupCopyOEMInf` error
`0xA1` on 7 SP1 x86, 2026-10-15) is met at the root, and no other INF in an
x64 flavour directory binds a PCI bridge. The x86 package keeps `nt\` with
its own `nt\xhcicm.sys`, because 98 SE and ME read a `$Windows NT$` INF and
would offer the filter for the tunnel bridge (task 39-0.4, record 23
section 4.1).

Branch `p42-x64flat`, from `3.1.0.0` at `4b6d3477`, with `3.1.0.0` merged
in at `470ea34c` (42-A.21), `bc63b037` (42-A.24, 42-A.27, 42-A.28),
`8dd99004` (42-A.22) and last at the head (42-A.24's revision 17 of
record 23, 42-A.25):

| Commit | What |
|---|---|
| `be8e4358` | the change: `package-common.ps1`'s `Get-CmFilterRel` and `Get-CmPublishable` (per architecture, and per version for a published tree: `3.0.0.0`'s x64 directories keep `nt\`); `make-package.ps1` stages `xhcicmf.inf` at the x64 root and `nt\xhcicm.sys` for x86 alone, the amd64 `xhcicm.sys` signed once and copied once; `make-release.ps1`'s copy set, the `nt\` copy comparison (x86 only), the upload assembly's layout rule and its `check-cm-inf.ps1 -PublishedVersion` call, the readme's file list; `check-cm-inf.ps1`'s CM-NO9XBRIDGE package half by `-Arch` (x86 `nt\` as before; amd64 the root, no `nt\`), with self-test layouts for both and for `3.0.0.0`'s x64 tree; `test-package.ps1`'s four-directory case with the connection manager in both layouts and an x64 `nt\` refused; the installer's `inst_cmf_rel`, `inst_cmf_pnf` and `inst_cmf_where` in `inst_core.c` (host vectors in `test_inst`), used for the filter INF's path, the list-less copy cleanup and the two Have Disk texts, x86 unchanged |
| `e23e34c5` | an amd64 output directory staged in the `3.0.0.0` `nt\` layout is replaced by `make-package.ps1` (those three entries only), with a test |
| `654a7b10`, `f399fe0e` | the documents: AGENTS.md, README.md, `build-and-test.md` (packaging; the by-hand install: Have Disk to the main folder on x64, `nt\` on x86), records 19, 21 and 23, `source-files.md`, `legal-provenance.md`, `releases/README.md`, the release notes (the `3.1.0.0` line) and the acceptance test's file-list row |
| `41361afd` | records 21 and 23 name the revision (record 23 revision 21 after the merges, 17 being 42-A.24's; record 21's status paragraph) |
| `470ea34c`, `bc63b037`, `8dd99004`, the head | `3.1.0.0` merged in; record 23 section 10's list keeps both new entries, and its status line every revision |

Reviewed and revised: the review of `be8e4358` found the replacement of an
old amd64 output directory (fixed in `e23e34c5`); the fix and the merge
came back clean; the documents gave four wording points, taken in
`f399fe0e`; the three merges came back clean.

Builds at `470ea34c` (development host A), x86 then amd64, one after the
other, before the legs: `build-driver.cmd all` and `all -amd64` each **BUILD + GATES
PASSED** (host suites `test_inst` 1443 checks, 0 failures; the CM INF gate's
self-tests 122 cases; `test-package.ps1` with the new cases; the installer
self-test). `xhciinst\build.cmd` built the four installer programs for the
legs. `make-package.ps1 -Flavor release`, both architectures, Stor98
`0.4.0.0` bundled, unsigned (nothing was signed).

The staged trees against the bench cut's
(`E:\xhci98-vm\m31\pkg-bench\xhci98-3.1.0.0`, from `be9b9bf6`):

| Directory | Bench cut | This branch |
|---|---|---|
| `release-x86` | 16 files, `nt\xhcicmf.inf` and `nt\xhcicm.sys` | the same 16 names; `nt\xhcicm.sys` the same bytes as `xhcicm.sys`; `nt\xhcicmf.inf` the same bytes as the bench's |
| `release-x64` | 15 files, `nt\xhcicmf.inf` and `nt\xhcicm.sys` | 14 files: `xhcicmf.inf` at the root (the bytes of `src\cm\xhcicmf-amd64.inf`), no `nt\`; every other name the same |

The legs, QEMU only (kit `E:\xhci98-vm\p42-x64flat`, QMP 48170 to 48173,
emulator `vm\usb4\bin-2a893ea`, the `tb3` tree: root bridge `1B36:0001`,
four `8086:15C0` switch ports, the NHI `8086:15BF`, the switch's xHCI
`8086:15C1`, and `qemu-xhci`), on the packages staged at `470ea34c` with
the installer programs built from it, unsigned; every x64 boot chose
"Disable Driver Signature Enforcement" from the boot menu. The guests were
clean (no xhci98, `xhcicm` or Stor98).

| Leg | Result |
|---|---|
| 7 SP1 x86, `INSTALL /console /yes /thunderbolt` (regression) | **pass**: `SetupCopyOEMInf("C:\Windows\XHCI98\D2BF7BD7\nt\xhcicmf.inf", "...\nt", SPOST_PATH, 0)`, staged as `oem5.inf`; the filter put on `8086:15C0` (four ports) and `1B36:0001` from `nt\xhcicmf.inf`; the NHI on `xhcicm`; exit 3; after the restart every port `pci` with upper filter `xhcicm` from `oem5.inf`, problem 0, and `/list` "current (this release)" |
| 7 SP1 x64, `INSTALL /console /yes /thunderbolt` | **pass**: the plan read `E:\PKG31\release-x64` and named `xhcicmf.inf` for the five ports; `SetupCopyOEMInf("C:\Windows\XHCI98\195E4F69\xhcicmf.inf", "C:\Windows\XHCI98\195E4F69", SPOST_PATH, 0)`, staged as `oem5.inf`, no error and no `0xA1`; the filter on the five ports and `xhcicm` on the NHI, all started; exit 3; after the restart the five ports upper `xhcicm` from `oem5.inf`, problem 0, `xhci98`, `xhcicm` and `xhcimark` running, `/list` current |
| 7 SP1 x64, `INSTALL /console /uninstall` after it | **pass**: the filter taken off the five ports, the four INFs (`oem2` to `oem5`), the services, the binaries, the mark service and the copies removed; after the restart `sc` 1060 for each service, `pnputil -e` Microsoft's two alone, the five ports back on `machine.inf` with no upper filter, `usbd.sys` and `usbui.dll` kept |
| 7 SP1 x64, by hand after the uninstall: a `15C0` port, Update Driver, Let me pick, Have Disk on `release-x64` itself | **pass**: the list offered "xHCI98 Thunderbolt PCI Bridge" (Windows' own bridge alone before Have Disk); installed, and after the restart that port `pci` with upper filter `xhcicm`, problem 0, `xhcicm` running |
| 7 SP1 x64, `INSTALL /console /yes /cmonly` on a clean guest (42-A.21) | **pass**: `xhcicm.inf` and `xhcicmf.inf` staged from the copy's root (`SetupCopyOEMInf(... "\195E4F69\xhcicmf.inf", "...\195E4F69", ...)`, `oem3.inf`), nothing else; the NHI and the five ports done; after the restart `xhcicm` and `xhcimark` running, no `xhci98` |
| Vista SP2 x64, `INSTALL /console /yes /thunderbolt` | **pass** on the third run: the first two refused with exit 4 and changed nothing, Vista's own Found New Hardware windows still open; then `xhcicmf.inf` staged from the copy's root as `oem6.inf`, the filter on the five ports, all started; after the restart upper `xhcicm` on the five ports, problem 0 |

Seen beside the legs, not from this change: QEMU froze six times on the 7
x64 guest with the whole package (the process at no CPU, QMP silent) -
four cold boots of one installed overlay, at the NHI's first control-ring
reads, and two fresh installs, right after the two xHCI controllers'
installs and before `xhcicm`'s - on both emulator builds tried and with
single-threaded TCG. None on the connection-manager-only 7 x64 guest, on
Vista x64 or on 7 x86. Two full installs and their warm restarts went
through; a cold start of 7 x64 with the full package installed is owed.
Read afterwards (2026-10-19, the 42-A.25 agent): the frozen R2 overlay,
its `xhcicm.sys` unchanged, cold-booted twice to the desktop on the same
command line with `-display sdl` replaced by `-display none -vnc`, the
connection manager's NHI accesses in the trace (1,116 and 1,139; the
frozen run's trace has none). The same wedge hit two SDL-window 7 x86
guests that day and went with the window. So the freezes are QEMU's SDL
front end on development host A, not the driver; guests run headless
since. Why they clustered at the first ring reads is not known.
The frozen overlays are kept in the kit (`w7x64\disk-*-frozen.qcow2`).

After the legs `3.1.0.0` was merged in twice more. At `bc63b037` both
gated builds passed (`test_inst` 1470 checks, 0 failures); at `8dd99004`,
the branch's head, `build-driver.cmd all` and `all -amd64`
each **BUILD + GATES PASSED** (`test_inst` 1556 checks, 0 failures). At
the last merge, `9aab6759` (record 23's revision renumbered 21, 17
being 42-A.24's), both again **BUILD + GATES PASSED** (`test_inst` 1562
checks, 0 failures; `test-package.ps1` with the layout cases), and its
review came back clean. The legs were not repeated on those merges, which brought no
change to where the filter INF is read.

## 42-A.30: Stor98 built in-tree, one version, one date, one signature (2026-10-19)

Branch `p42-s98ver` from `3.1.0.0` at `08709480`, with `3.1.0.0` merged in
at `7e1ab583` (`cfcef657`). The owner's decisions of the day: every binary
and INF of the package carries one version and one date, Stor98's
included; Stor98 is built from `src\stor98` by this repository's build, not
bundled from a pinned release; one key signs every x64 driver, `stor98.sys`
included; any fix before shipping is a recut of `3.1.0.0`, dated
`2026-10-19`.

### What it does

- **Version.** `src\stor98\src\stor98ver.h` includes `src\xhci_version.h`
  and gives `nvme2k.rc` `XHCI_VER_CSV` and `XHCI_VER_STR`: `stor98.mpd`
  and both `stor98.sys` are `3.1.0.0`. Both Stor98 INFs carry
  `DriverVer=10/19/2026,3.1.0.0`, held to the header by the new
  `scripts\inf-gate\check-stor98-inf.ps1` (self-tested by six mutants)
  beside the other INF gates. Stor98 logs and reports no release version
  of its own (the version resource was its only one). Its interface
  versions (`STOR98LS_VERSION` 4 and the rest) are unchanged, and
  `check-stor98-header.ps1` still holds `src\cm\stor98cm.h` to the master.
- **Flavour marker.** `nvme2k.c` carries exactly one of
  `STOR98_FLAVOUR_FREE`, `_CHECKED` or `_DBGPORT`, read in `DriverEntry`
  so the linker keeps it.
- **The gated run.** `build-driver.cmd` runs Stor98's unit suite on the
  x86 run, builds Stor98 free once per run for its architecture (it
  deletes only that architecture's images first), and gates each image in
  every flavour: `scripts\check-stor98-build.ps1` (version, the free
  marker, machine, imports against the new
  `scripts\import-gate\stor98-imports.allow`; Stor98 had no import gate of
  its own), the gated record (`stor98.sys`, and `stor98.mpd` on x86) and
  the source stamp (`source-stamp.ps1 -Driver stor98 / stor98mpd`, whose
  SOURCES reader now also takes a list that ends the file).
  `build-all.cmd` lost its separate Stor98 step.
- **The packager.** `make-package.ps1` bundles that build by default
  (`-NoStor98` leaves it out; `-Stor98Root` is for its self-tests),
  re-checks it before staging (`Resolve-Stor98Build`), holds it to the
  gated record under `-SignOnly`, and with `-TestSignPfx` signs the amd64
  `stor98.sys` in the same step and with the same key as the other x64
  drivers. The four pins, the SHA256SUMS handling and `stor98-test.cer`
  are gone; `src\stor98\docs\package-readme.txt` is the bundled
  `stor98-readme.txt`. `make-release.ps1`: the release legs publish
  Stor98 with the project's files into `releases\<ver>\release-*`, each
  image held to the release version and, on x64, to the cut's signature;
  the debug legs pass `-NoStor98`; Stor98's INFs join the one-date rule;
  `-UploadSetOnly` refuses `3.0.0.0`, whose pinned Stor98 was in its upload
  set only. Stor98's own `make-release.ps1`, `sign-x64.ps1`,
  `make-testcert.ps1` and `testsign-common.ps1` are removed.
- **The installer and XHCITR.** XHCIINST/XHCIWIZ read the bundled
  version from `stor98.inf` and compare four-part versions, so an
  installed `0.4.0.0` (or `0.3.0.x`) ranks below `3.1.0.0` and is
  updated; no code change was needed. New `test_inst` vectors: `0.4.0.0`
  and `0.3.0.1` updated, `3.1.0.0` current, `3.2.0.0` refused without
  `/force`, PreExisting deciding removal whatever the version. The
  test-signing note now says `stor98.sys` is signed with the other
  drivers. XHCITR's report lists the installed `stor98.sys` /
  `stor98.mpd` with its file version (it had no Stor98 row).
- **Docs.** AGENTS.md, README.md, records 19 (revision 2), 23 (revision
  19) and 26 (draft 13), build-and-test.md, source-files.md,
  legal-provenance.md, the release notes, `releases\README.md` and
  Stor98's own README, AGENTS, BUILD, TESTING, releases README and a
  `3.1.0.0` history entry.

Commits: `ce48f465` (installer, XHCITR, vectors), `cf6ca042` (build,
packager, version), `36a9b701` (docs), `514b5bcf` (review fixes),
`7e1ab583` (merge of `3.1.0.0`).

### Review

Round 1 on `ce48f465..36a9b701`: three P1 (the Stor98 stamp's SOURCES
reader, the amd64 run deleting the x86 images, a stray backslash in
`make-release.ps1`) and two P2 (the manifest reader refusing
`stor98.mpd`, `-UploadSetOnly` dropping `3.0.0.0`'s Stor98), fixed in
`514b5bcf`. Round 2 on `514b5bcf`: no findings. Round 3 on the merge
`7e1ab583`: no findings.

### Builds

| Build | Result |
|---|---|
| `test-package.ps1`, `cf6ca042` | 1098 checks passed |
| `build-driver.cmd both`, `7e1ab583` | BUILD + GATES PASSED (debug release): host tests passed (`test_inst` 1470 checks), Stor98 unit tests all passed, Stor98 INF gate, Stor98 gate x86 and win9x in both flavours |
| `build-driver.cmd both -amd64`, `7e1ab583` | BUILD + GATES PASSED (debug release), Stor98 gate amd64 in both flavours |
| gated records | x86 and amd64, release and debug, commit `7e1ab583`, `dirty=0` |
| `make-release.ps1 -Arch x86 -SkipQualtool -TestLabel "42-A.30, Stor98 built in-tree"` into `out\testrel` | passed; `release-x86\` carries Stor98, `debug-x86\` none |
| `make-package.ps1 -Flavor release -Arch amd64`, unsigned | passed |

Every binary's file version and every INF's DriverVer, read from the
staged trees (`out\testrel\3.1.0.0` and `out\pkg-s98-release-amd64`):

| File | x86 (release-x86, debug-x86) | amd64 |
|---|---|---|
| `xhci98.sys`, `xhciuas.sys`, `xhcicm.sys`, `nt\xhcicm.sys` | 3.1.0.0 | 3.1.0.0 |
| `xhcihlpr.sys` | 3.1.0.0 | - |
| `stor98.mpd` | 3.1.0.0 (release-x86) | - |
| `stor98.sys` | 3.1.0.0 (release-x86) | 3.1.0.0 |
| `xhci98.inf`, `xhciuas.inf`, `xhcicm.inf`, `nt\xhcicmf.inf` | 10/19/2026,3.1.0.0 | 10/19/2026,3.1.0.0 |
| `stor98.inf` | 10/19/2026,3.1.0.0 | 10/19/2026,3.1.0.0 |
| `XHCIWIZ.EXE`, `XHCIWZ64.EXE`, `XHCITR.EXE`, `XHCITR64.EXE` | 3.1.0.0 | |

`txtsetup.oem` names no driver version. The console tools (XHCIINST,
XHCICLN, XHCIMARK, XHCIDI and their 64-bit twins) carry no version
resource, as before; they print `XHCI_VER_STR`. No version resource
carries a build date or time.

The staged Stor98 against `0.4.0.0`: built from `src\stor98` (the stor98
repository's `4253b58`, 82 commits after the `0.4.0.0` release commit
`d2e6056`; 34 files of `src`, `inf` and `include` changed, +1463/-437:
the audit of 2026-10-18's fixes, FUA, MODE SENSE, the find and recovery
fixes, the 9x generation rule). `stor98.mpd` 51296 bytes (`0.4.0.0`
48237), x86 `stor98.sys` 41678 (39403), amd64 `stor98.sys` 49664 (47064);
imports: `ScsiPortConvertUlongToPhysicalAddress` dropped on all three,
`NTOSKRNL.EXE!MmSystemRangeStart` added on both `stor98.sys`. SHA-256:
`stor98.mpd` `1d2bf9f5...9e9ce4`, x86 `stor98.sys` `fe620cd4...776691`,
amd64 `stor98.sys` `87f02518...`. The `stor98-spd` bounce fix is not in
it.

### The legs

Not run. The kit was prepared in `E:\xhci98-vm\p42-s98ver` (98 SE fresh
with the enclosure, 2000 fresh with the Alpine Ridge enclosure, and 98 SE
with `3.0.0.0` and Stor98 `0.4.0.0` installed for the update, its guest
read as `0.4.0.0` with the NVMe volume working), and the package above
was staged into it, but starting the VMs was refused by the session's
permission check. All three legs are owed: a fresh install with a file
copy and compare on the NVMe on 98 SE and on 2000, and the update from
`0.4.0.0` to `3.1.0.0`.

### The merges of the evening and the rebuild

`3.1.0.0` was merged in twice more: at `f5678586` (`94c22687`; conflicts
in `make-release.ps1`'s readme paragraph, the release notes, this file
and records 23 and 26, both sides kept) and at `234884a3` (`9cf98652`,
which brings Stor98's 9x bounce fix of 42-A.33 and the 98 SE / ME
`OEM Name` cleanup; Stor98's `3.1.0.0` history entry now names the
bounce fix). This task's record revisions are record 23 revision 19 and
record 26 draft 13 (42-A.26 took 18 and 12; the `OEM Name` cleanup took
20 and 14). The merge review found one P2: the bundled
`stor98-readme.txt`, `README.md` and the release notes still said
`/stor98` alone, which the installer now refuses without `/thunderbolt`;
fixed in `6ae57798`, then a confirmation round and the review of the last
merge found nothing.

| Build | Result |
|---|---|
| `build-driver.cmd both`, `234884a3` | BUILD + GATES PASSED (debug release): packager self-tests 1098 checks, host tests passed (`test_inst` 1562 checks), Stor98 unit tests all passed (the bounce fix's `sg9x_ut` with them), Stor98 gate win9x and x86 in both flavours |
| `build-driver.cmd both -amd64`, `234884a3` | BUILD + GATES PASSED (debug release): packager self-tests 1098 checks, host tests passed (`test_inst` 1575 checks), Stor98 gate amd64 in both flavours |
| `make-release.ps1 -Arch x86 -SkipQualtool -TestLabel ...` into `out\testrel2`, `7e84f8ef` (before the last merge) | passed; every binary `3.1.0.0`, every INF `10/19/2026,3.1.0.0` |
| `make-package.ps1 -Flavor release -Arch amd64`, unsigned | passed at `234884a3`; every binary `3.1.0.0`, every INF `10/19/2026,3.1.0.0` |

The Stor98 images of the last merge, the bounce fix in, pass the same
gate: version `3.1.0.0`, `STOR98_FLAVOUR_FREE`, 11, 19 and 18 imports,
none new.

The legs were allowed by the owner in the evening, but starting the
guests was refused by the session's permission check again, so they are
still owed; the kit `E:\xhci98-vm\p42-s98ver` now starts its guests
headless (`-display none`, VNC on its own ports).

A third merge, at `2f7dd861` (`200d841b`: the x64 package's filter INF
at the flavour directory's root, 42-A.23), kept Stor98 in the flat x64
layout (the upload set's and the release leg's lists take the
connection manager's files from `Get-CmPublishable` and Stor98's beside
them); its review found nothing. Rebuilt there: `build-driver.cmd both`
and `both -amd64` BUILD + GATES PASSED (packager self-tests 1113 checks,
`test_inst` 1586 checks, Stor98 unit tests passed), gated records of
both architectures at `2f7dd861`, `dirty=0`; `make-package.ps1
-Flavor release` for x86 and amd64 (unsigned) passed, every binary
`3.1.0.0` and every INF `10/19/2026,3.1.0.0`, the amd64 package's
`xhcicmf.inf` at its root. `3.1.0.0`'s roadmap-only `53619ce4` was
merged after it.

## 42-A.19's licence housekeeping: nvme2k 1.0.0.2 read, the `a18-*` branches (2026-10-19)

Documentation and comments only; no driver code changed.

- **nvme2k 1.0.0.2's licence**, read in the upstream history: a clone of
  `https://github.com/techomancer/nvme2k` (`main` at `0c41727`, the
  1.2.0.0 commit Stor98's reference copy is taken from) kept beside the
  emulator on development host A. Tag `1.0.0.2` is commit
  `cbbadf041d186d084c55d240b3b4d10fb4cc1f07` (2025-11-13). Its `LICENSE`,
  the same blob since the first commit, is the BSD-3-Clause text with the
  template's unfilled copyright line `Copyright (c) <year>, <copyright holder>`;
  its `README.md` names the 3-Clause BSD License; its sources carry no
  header. Commit `eb31419` (2025-12-02) filled in "Copyright (c) 2025,
  Dominik Behr" and changed nothing else. Stor98's `NOTICE` and
  `legal-provenance.md` now say so instead of "not checked".
- **`src/stor98/src/nvme2k.def` and `makefile`**, both derived from
  nvme2k, carry the full BSD-3-Clause conditions and disclaimer in their
  header, as the other nvme2k-derived files of `src/` do, in place of the
  pointer to `LICENSE`.
- **The six `a18-*` track branches** in the separate Stor98 repository,
  each listed by `git branch --merged` from `audit-2026-10-18` (`4253b58`),
  deleted with `git branch -d` (local only; no remote touched). Tips, to
  restore with `git branch <name> <sha>`: `a18-build`
  `41e5e97a3894744f26efb05a3a65deca343e1acf`, `a18-docs`
  `6383ef55a605f05471cf0274b5b11a54af49eaba`, `a18-engine`
  `5751ba69f30b0a0b614d985d2ce6f2c58970f278`, `a18-p9`
  `f9755aee7a27725e2a0edfb0d8c1e1231669f777`, `a18-scsi`
  `8f5430eb5d54fdf7d3e333e3a1cbe07e398add30`, `a18-tests`
  `b41c9b71de32ac099f1486e7dcb3c28d4930de50`.

## 42-A.31: SETUP.EXE, the installer's entry point for people (2026-10-19)

Branch `p42-batlaunch` from `3.1.0.0` at `08709480`. The owner asked
first that `INSTALL.BAT`, double-clicked, start the installer's window
and close at once, so that no console sits behind the wizard; then, the
same afternoon, for a `SETUP.EXE` at the package root instead ("setup.exe
by tonight"), with `INSTALL.BAT` kept, unchanged, for the command line.
The detached `INSTALL.BAT` was not committed.

### What it adds

- **`SETUP.EXE`** (`xhciinst\setup.c`, `setup.rc`, `setup.manifest`;
  record 26 draft 15 section 2 and W17, record 23 revision 22): a 32-bit
  windows-subsystem program, MSVC 6.0, the C runtime static,
  `/SUBSYSTEM:WINDOWS,4.0`, the xHCI98 USB Tree's icon and the version
  resource of `src\xhci_version.h`. It finds its own directory, picks
  `tools\xhciinst\XHCIWZ64.EXE` on 64-bit Windows (`IsWow64Process`
  looked up at run time) and `XHCIWIZ.EXE` otherwise, starts it with
  `ShellExecuteExA` (`open`, its own arguments after its name, the
  package root as the working directory, `SEE_MASK_FLAG_DDEWAIT` so the
  start is finished before it exits) and returns 0, or 1 after a message
  box naming the problem (the window missing, its own path unreadable,
  the start failed, the elevation declined). The manifest asks for
  `asInvoker` and declares Vista to 10; an empty popup of no size at the
  screen's centre owns the elevation prompt and the message box.
- **The gates**: `xhciinst\build.cmd` builds it with the 32-bit pair;
  `selftest.cmd` runs its `/selftest` (13 checks: the command-line tail,
  `/selftest` recognised with the blank `cmd.exe` leaves before a
  redirection, the paths for a folder with a space, a drive's root and a
  share, the refusals) and `check-imports.ps1`'s new `setup` profile
  (KERNEL32, USER32 and SHELL32 alone, the windows subsystem, an embedded
  `asInvoker` and no elevated level, Windows 7's `supportedOS` entry).
- **The packager**: `make-release.ps1` requires it, holds it to the
  install tool's staleness rule, stages it at the version root beside
  `INSTALL.BAT`, lists it in the readme's file list and its linkage in
  `tools\xhciinst\NOTICE.TXT`; `test-package.ps1` holds the staging, the
  staleness rule and the readme's first instruction.
- **The words**: the readme's section 4 begins "THE EASY WAY: SETUP.EXE.
  To install or update, double-click SETUP.EXE; to uninstall, use Remove
  in Add/Remove Programs (or SETUP /uninstall). INSTALL.BAT is for the
  command line and scripts (INSTALL /console ...); run with no arguments
  it opens the same window." The window started with no argument installs
  or updates; it has no uninstall page of its own, so the owner's "To
  install, update or uninstall, double-click SETUP.EXE" became this.
  `tools\xhciinst\readme.txt`, `INSTALL.BAT`'s header, `README.md`'s
  install section (now the window's pages, not the console's questions),
  a line in "What `3.1.0.0` changes", `source-files.md` and
  `legal-provenance.md` follow.
- **The exit code**: `INSTALL.BAT` keeps waiting for the window and
  passing its code on, and `INSTALL /console` is unchanged. `SETUP.EXE`
  does not wait: the window's result is its Finish page and the log, and
  a script runs `INSTALL /console`.

### What the legs changed

Three things were read in the Windows 7 guests and fixed before the last
legs (`9f526670`, `a56f2818`, `7fcdbe67`, `7995a30e`):

- With the elevation declined, the message box opened behind the
  Explorer window `SETUP.EXE` was started from (the taskbar button
  flashed), first with no flags and then with `MB_TOPMOST |
  MB_SETFOREGROUND` as well: the declined prompt handed the foreground
  back to Explorer. The owner window, made the foreground while
  `SETUP.EXE` still had the right to, and passed as the prompt's
  `hwnd`, fixed it.
- After the window was cancelled, Windows 7's Program Compatibility
  Assistant asked "This program might not have installed correctly"
  about `SETUP.EXE` (installer detection by name). The manifest's
  `supportedOS` section exempts it; not seen again on 7 x86 or x64.
- With the owner at 0,0 the elevation prompt opened in the screen's
  corner; the owner now sits at the centre.

### Review

Reviewed in six rounds, each on the commits before it: the shell start
finished before exit (`SEE_MASK_FLAG_DDEWAIT`, `277dd147`); the uninstall
wording and `README.md`'s console prompts (`7b843d4d`); then no findings
on the documents, the message box, the manifest, the owner window, its
place, and the merge of `3.1.0.0` (`7950d900`), where 42-A.27 held record
23 revision 14 and record 26 draft 9 and 42-A.29 takes 15 and 10, so this
task's text was revision 16 and draft 11; 42-A.22 then took those, and it
was revision 17 and draft 12 after the second merge; after the third
(42-A.24 holding revision 17, 18 to 21 and drafts 12 to 14 reserved for
other branches) it is revision 22 and draft 15.

### Builds

Both gated builds on the merged tree, `7950d900`: `build-driver.cmd all`
(x86) and `all -amd64`, BUILD + GATES PASSED, the installer's self-test
with `SETUP.EXE`'s 13 checks and its `setup` import profile among them
(`xhciinst\build.cmd` and `xhcimark\build.cmd` run by hand first: the
gated build rebuilds neither). The packager's self-tests passed again
(1150 checks) after the text fixes below.

The TEST cut: `make-release.ps1 -Flavor release -Arch x86 -TestLabel
"SETUP.EXE, task 42-A.31" -ReleasesDir out\blrel -SkipQualtool
-SkipUploadSet` (no Stor98; the bench cut's `history.md` copied beside
it). The first two tries refused `tools\xhciinst\NOTICE.TXT`: the
sentence that gained `SETUP.EXE` ran past 78 columns, which no gate
renders (`80693c4c`, `f177637f`); the readme's file list then said
`SETUP.EXE` uninstalls too (`d78eb68f`). The cut at `d78eb68f` holds
`SETUP.EXE` (`a4b3c922...`) and `INSTALL.BAT` at its root, its readme's
section 4 opening with the instruction above.

### The legs (development host A, QEMU)

Kit `E:\xhci98-vm\p42-batlaunch` (QMP 48240 to 48249; screenshots in
`shots\`), copied from `p42-int`'s. The package: the bench package
(`E:\xhci98-vm\m31\pkg-bench`, checked against its `SHA256SUMS-bench.txt`)
with this branch's `SETUP.EXE` and `INSTALL.BAT` at its root, given to
each guest as a read-only FAT drive. Every start was from Explorer, the
file selected and opened with Enter (Explorer's default verb, as a
double-click): the guests have no pointer before xhci98 is installed.
Nothing was installed; every window was cancelled on its Welcome page.

- **Windows 98 SE** (`win98-clean.qcow2` overlay, `w98c`): `SETUP.EXE`
  showed the USB Tree's icon in Explorer; opened, the window's Welcome
  page appeared ("Windows 98 SE (4.10.2222 A), 32-bit", package `D:\`,
  `release-x86`) with no MS-DOS box, the taskbar holding Explorer and
  Welcome alone; Cancel closed it (`bl-98-setup`, `bl-98b-setup`, and on
  the final `SETUP.EXE` `bl-98c-setup`, `bl-98c-cancel`). **Pass.** A
  Ctrl+Alt+Del task list taken with the window open (`bl-98-tasklist`)
  froze that guest's display; it was powered off and started cold, and
  the leg taken again.
- **Windows ME** (`winme-stock-base.qcow2` overlay): the same, "Windows
  ME (4.90.3000), 32-bit", no MS-DOS box, cancelled at Welcome
  (`bl-me-setup`, `bl-me-cancel`; final `SETUP.EXE` `bl-me2-setup`,
  `bl-me2-cancel`). **Pass.**
- **Windows XP SP3** (`winxp.img` overlay, signed in as an
  administrator): the window opened with no console; `tasklist` listed
  `XHCIWIZ.EXE` and no `SETUP.EXE` (`bl-xp-setup`, `bl-xp-tasklist3`);
  cancelled at Welcome. Then, in a command prompt, `INSTALL /console
  /list` from the package's top directory: the console tool's plan,
  "No changes were made.", "Exit code 0. The log is (none).", and the
  batch file's `%errorlevel%` 0 (`bl-xp-console`): the console path is
  unchanged. **Pass.**
- **Windows 7 SP1 x86** (`win7.img` overlay, UAC at its default): one
  elevation prompt, naming `XHCIWIZ.EXE` (not `SETUP.EXE`), centred
  (`bl-w7d-uac`); No: `SETUP.EXE`'s message in front of Explorer
  (`bl-w7d-uacno`); run again, Yes: the window, "Windows 7 (6.1.7601
  Service Pack 1), 32-bit", `release-x86` (`bl-w7d-wiz`); `tasklist`
  listed `XHCIWIZ.EXE` and no `SETUP.EXE` (`bl-w7-tasklist2`); cancelled,
  and no compatibility question followed (`bl-w7d-cancel`). Taken on the
  final `SETUP.EXE`; the earlier builds' two defects above were read
  here (`bl-w7-uacno`, `bl-w7b-uacno`, `bl-w7-cancel`). **Pass.**
- **Windows 7 SP1 x64** (`w7x64-ts` overlay): the prompt named
  `XHCIWZ64.EXE` (`bl-x64b-uac`); No: the message in front
  (`bl-x64b-uacno`); Yes: the window, "Windows 7 (6.1.7601 Service Pack
  1), x64", `release-x64` (`bl-x64b-wiz`); `tasklist` listed
  `XHCIWZ64.EXE` alone, no `XHCIWIZ.EXE` and no `SETUP.EXE`
  (`bl-x64b-tasklist`); cancelled, no compatibility question
  (`bl-x64b-cancel`). Taken on the build before the centred owner
  (`7fcdbe67`), whose prompt opened in the screen's corner. **Pass.**

`SETUP.EXE` of the final legs: `309466a0...79e8` (`7995a30e`); the
window's executables are the bench package's (`XHCIWIZ.EXE`
`ad09907e...`, `XHCIWZ64.EXE` `d47ce891...`). The cut of this branch
rebuilds all of them from the merged tree.

The cut package itself (`out\blrel\3.1.0.0`, x86 only) on fresh starts
of the same overlays: **Windows 7 x86** one prompt for `XHCIWIZ.EXE`,
the window, cancelled, no compatibility question (`bl-cut-w7-uac`,
`bl-cut-w7-wiz`, `bl-cut-w7-cancel`); **Windows 98 SE** the window with
no MS-DOS box, cancelled (`bl-cut-98-setup`, `bl-cut-98-cancel`).
**Pass.**


After the second merge of `3.1.0.0` (`49d35844`: 42-A.22 and 42-A.29,
reviewed with no findings), `xhciinst\build.cmd` and `xhcimark\build.cmd`
were run again and both gated builds passed once more at `49d35844`
(x86, then amd64); the x86 TEST cut was taken again there (`SETUP.EXE`
`f7e9b102...`). No leg was taken on that last cut: `SETUP.EXE`'s source
is unchanged since `7995a30e`.

## 42-A.26: the bundled Stor98 on tunnel drives only (2026-10-19)

Worker p42-s98tun, branch `p42-s98tun` from `3.1.0.0` at `4b6d3477`,
with `3.1.0.0` merged in four times (`c598a252`, 42-A.21's connection
manager alone; `87403775`, the 42-A.24 and 42-A.28 work; `b8238676`, 42-A.27;
`54bb23c2`, 42-A.29). The owner's decision of
2026-10-19: the installer's bundled Stor98 goes only on NVMe controllers
behind a Thunderbolt or USB4 tunnel, never on internal ones; a user with
an internal NVMe drive installs Stor98 by hand. The Components page's
layout is the window branch's (p42-wizgap), untouched here; this branch
is the engine's and the console's side.

What was done (record 23 revision 18, section 4.4; record 26 draft 12,
sections 5.3, 6.3, 6.4 and 9.3):

- **The place of a drive** (`inst_s98_place`): from the connection
  manager's confirmed reading, the walk `/devices` and `/eject` take
  (`tb_read_all`, NT location, 98 SE and ME PCI path), read once per
  reading of the devices under the lock `XHCITR` shares. Behind a tunnel:
  a verified tunnel port above it. Internal: a whole reading, every
  manager found confirmed, no verified or unverified Thunderbolt port
  above it. Anything else is unknown, never either.
- **The gate** (`inst_s98_eligible`), at the end of every plan, so after
  every scan, settle and barrier: install, update, repair, `/force` and
  `/restage` all held to it; eligible, skip-internal, skip-unknown,
  needs-TB. A drive left is named with its reason on screen, in the log
  and on the window's found page, and gets no printed 98 SE and ME steps;
  an unknown place still unknown at the end of the run is work left
  (exit 4). NT's refusal of a hardware id shared with a device the run
  leaves stays (one call reaches every device of the id), with its own
  sentence.
- **Stor98 needs the Thunderbolt component**: the engine keeps it out of
  a run without it (`s98_set`, from both the Stor98 and the Thunderbolt
  choice), and the console says so and does not ask.
- **The shared file, the decision** (`inst_s98_share`): every Stor98
  device shares one `stor98.sys` or `stor98.mpd` and one service's
  settings. Where a device on Stor98 is not a known tunnel drive (an
  internal one installed by hand, one of unknown place, one not present
  that the record does not name; on 98 SE and ME every driver key naming
  `stor98.mpd`), the conservative choice: at another installed release
  than the bundled one, **no Stor98 change at all** in the run (no
  staging, no file in the 98 SE and ME install directory, no install,
  no update, no setting), said on screen and in the log with the readme's
  update by hand; at the same release, tunnel drives are installed and no
  setting is written. A run whose copy was made held stays held. The
  internal drive's devnode is never updated or reconfigured.
- **Wording**: the Components sentence (`INST_PT_S98_T`) and the
  console's: "For NVMe drives behind a Thunderbolt or USB4 tunnel; the
  installer selects tunnel drives only." The release notes and the
  generated readme add that Stor98's INF matches `PCI\CC_010802`, so
  Windows may still offer it for a later driverless internal NVMe drive.
- **The uninstall** (the owner's coordinator, from the ME matrix leg N3
  of the same day): with Stor98 brought by this package's installer
  (`PreExisting` clear), devices Windows bound to Stor98 after the run
  (an enclosure first plugged after the restart, its wizard taking the
  staged INF) are adopted into the record with the one INF copy their
  driver key names, when each is behind a tunnel or not present; an
  internal or unknown one keeps Stor98 and is named. An INF copy that
  appeared outside a Stor98 step of a 98 SE or ME install is recorded
  when its active text names `stor98.mpd` and `PreExisting` is clear
  (`inst_s98_adoptable`, `inst_s98_stray_inf_ours`,
  `inst_inf_names_file`).

Commits: `7b077075` (the gate, the place, the share, the wording);
`d008870f` (reviewed and revised; the uninstall's adoption); `76bd0d31`
(records 23 and 26, the release notes, the readme template); `3f9876b0`,
`e6ec24cf` (reviewed and revised); `ab4b0458` (the records again);
`952c47df` (the 98 SE baseline below, and no immediate NT retry of a
Stor98 call after 42-A.27's wait); `061d9ca1` (record 26). Reviewed and
revised in six rounds: four to no findings on `e6ec24cf`, a delta round
on the merge `b8238676` (one finding, fixed in `952c47df`) and a
confirmation round on `952c47df` with no findings. Two points were kept as they were, with their reasons: the
staged INF and the 98 SE and ME install directory keep `stor98.inf`
while Stor98 is in the run and not held, because a tunnel drive that
appears after the restart must find it (the release notes' sentence
covers the internal case); and at the same release the INF's own service
section writes the values that release's INF wrote already.

Host suite: `test_inst` 1476 checks, 0 failures (eligible, skip-internal,
skip-unknown, needs-TB; the place; the share; the adoption; the stray
copy; the INF word test; the 98 SE `LinkState` key; the Components
sentence).

The legs, in QEMU (the `usb4-emu` build `bin-2a893ea`), kit and
evidence in `E:\xhci98-vm\p42-s98tun` (QMP 48200 to 48202; `logs\`,
`shots\`): each guest with the emulator's `tb3-alpine-ridge` host, its
tunnel bridge `tb0` and the enclosure `enc0` (an NVMe behind the
tunnel, hot-plugged after the desktop), and a second QEMU `nvme` on the
root bus (the internal drive), both at `1B36:0010` with one subsystem
id. Each guest was prepared with the bench package
(`xhci98-3.1.0.0-bench.zip`, `INSTALL /console /yes /thunderbolt`, so
`xhcicm` installed and running and no Stor98), both NVMe drives without
a driver; the package for the legs is that bench package with this
branch's installer in `tools\xhciinst\`.

| Leg | Installer | Result |
|---|---|---|
| 98 SE, enclosure plugged: `INSTALL /console /yes /thunderbolt /stor98`, cold start, uninstall | `e6ec24cf` | install **pass**; uninstall **fail**: "Stor98 stays: it was installed before this package's installer first ran" - the baseline had read `Services\stor98`, which `xhcicm.sys` makes on 98 SE for the link-state locator (`LinkState` alone, exported from the untouched snapshot), as Stor98 there before; fixed in `952c47df` |
| the same | `952c47df` | **pass**: the reading "61 devnodes, 0 not read whole, changing 0, 1 connection managers, all confirmed 1"; the tunnel NVMe (`...\082800`) "behind a tunnel (below a verified port 1)", installed by the helper and recorded; the root-bus NVMe (`...\BUS_00&DEV_09&FUNC_00`) "internal", "LEFT: an internal drive, not behind a Thunderbolt or USB4 tunnel: the installer leaves it as it is (Stor98 goes on tunnel drives only)", on screen and in the log; "found before 0, PreExisting 0"; exit 3. After the cold start Stor98 under SCSI controllers on the tunnel drive (Code 29: the emulator's no-interrupt case of 42-A.15, the same before any install), the internal drive still without a driver. The uninstall deleted the `INF\OTHER` copy and `stor98.mpd` and removed the record; afterwards no `STOR98.MPD`, `INF\OTHER` empty, no `Software\xhci98` |
| 98 SE, enclosure first plugged after the restart: install with no enclosure, cold start, the drive bound to Stor98 from `C:\XHCI98` by hand (Update Driver; the snapshot's devnode already existed, so no New Hardware wizard), uninstall | `e6ec24cf` | install **pass** (exit 4: a stale registry-only `1B36:000D` entry's steps, not Stor98); uninstall **fail** ("Stor98 stays: a device is still bound to it", the same baseline cause) |
| the same | `952c47df` | **pass**: Stor98 staged into `C:\XHCI98`, nothing installed, the internal drive left; at the uninstall "...\082800: bound to Stor98 after this installer's run (behind a tunnel): it goes with Stor98", "INF\OTHER\Yeo Kheng Mengstor98.inf adopted with its device"; the device removed, the copy and `stor98.mpd` deleted, the record removed; nothing of Stor98 left but the key below |
| XP SP3, enclosure plugged, `INSTALL /console /yes /thunderbolt /stor98` | `e6ec24cf` | **pass**: the internal NVMe "internal", LEFT; the tunnel NVMe "behind a tunnel"; then, the two sharing one hardware id, NT's refusal: "... is shared with ...\3&267A616A&0&48, which this run leaves (left: an internal drive (Stor98 goes on tunnel drives)); neither is changed. Install Stor98 on the tunnel drive in Device Manager ..."; neither drive got a driver; exit 4 |
| 7 x86, the same | `e6ec24cf` | **pass**: the same readings, the same refusal, neither drive changed; exit 4 |

Not read, or left: the Windows 2000 leg (XP was taken); the window's
pages (the Components layout is the window branch's); the shared-file
hold and its same-release case in a guest (host vectors and review
only); an XP or 7 tunnel drive actually installed (QEMU's NVMe gives
both drives one hardware id, so NT's refusal is what a guest can show).
Two observations for the owner: on XP and 7 `stor98.inf` is staged
although neither drive got it this run (as decided: a tunnel drive that
appears later needs it; Windows may offer it for the internal drive, as
the release notes say); and on 98 SE `Services\stor98` keeps `LinkState`
(`xhcicm`'s) and Stor98's runtime `StorInterrupt 1.1.0` value after the
uninstall, the leftover the 42-A.2 legs listed.

Builds: the gated build, x86 `all` then `all -amd64`, on the final
merge `54bb23c2`: both **passed** (BUILD + GATES PASSED, debug, release
and qemu, each architecture). The legs ran this branch's installer as
built at `e6ec24cf` and `952c47df`; the later merges brought 42-A.27's
and 42-A.29's installer changes, not this task's code.

**After the last merge** (`473323ae`, 3.1.0.0's 42-A.22, 42-A.24 and
42-A.25): Stor98's needs-Thunderbolt rule is one rule, the window's
`inst_s98_box_open`, which the engine's `s98_set` applies; the console's
duplicate sentence went (42-A.22's refusal of `/stor98` without
`/thunderbolt` or `/cmonly` stands). 42-A.26 is record 23 revision 18
and record 26 draft 12. 42-A.24's step 6a removes Stor98's
CriticalDeviceDatabase entries only once its service is gone, so an
adoption refused for an internal drive keeps them with the service. The
integration delta was reviewed with no findings. `test_inst` 1568 checks,
0 failures; the gated build, x86 `all` then `all -amd64`, **passed** on
`473323ae`. One more 98 SE leg on the merged installer, from the
prepared snapshot, enclosure plugged after the desktop, `INSTALL /console
/yes /thunderbolt /stor98`: **pass** - the tunnel NVMe behind a tunnel and
on Stor98, the internal NVMe LEFT as internal, "found before 0,
PreExisting 0", exit 3; the uninstall then deleted the `INF\OTHER` copy
and `stor98.mpd` (exit 3); `logs\N1-98-473323ae.log`.

## The 3.1.0.0 fix round: Stor98 as a file on 98 SE and ME, the Thunderbolt and Stor98 boxes, the Stor98 record, the `XhciCmMode` 0 warning (2026-10-19)

Worker p42-fix1, branch `p42-fix1` from `3.1.0.0` at `bd0d14dd`. The
release held on the night of 2026-10-19 for these, the owner's
decisions of that night (record 23 revision 23, record 26 draft 16,
record 27 draft 9).

What was done:

- **98 SE and ME: Stor98 updated as a file.** The update over `3.0.0.0`
  and Stor98 `0.4.0.0` with a tunnel enclosure attached and mounted
  stopped 98 SE with "fatal exception 0E at 0028:FF09E842" every time,
  when the helper had Windows' device installer replace the running
  `stor98.mpd`; with the enclosure absent Stor98 was held ("Stor98 is
  LEFT ... another release") and stayed `0.4.0.0`. Now a device Stor98
  runs is never reinstalled on those systems, and never sent to Update
  Driver by the printed steps; an older `IOSUBSYS\stor98.mpd` under a
  free shared-file rule (`inst_s98_file_update`) is written beside the
  old one as `STOR98.TMP`, checked, and swapped through `WININIT.INI`'s
  `[rename]` with 8.3 names, the INF copy each Stor98 driver key names
  is made the bundled `stor98.inf` (and again by any later run while the
  file is the bundled release), the install directory's copy is the
  package's, and the run asks for a shut down and a start. A Stor98
  entry with no present devnode counts on those systems as a drive gone
  with its tunnel (`inst_s98_known_tunnel`), so the update runs with the
  enclosure absent. One Summary box and one console question, "Please
  disconnect all external NVMe drives before proceeding" (Enter and
  `/yes` N), replace the per-drive boxes; unconfirmed, Stor98 is left
  out of the run whole (exit 4). A Stor98 drive the run does not touch
  may be unplugged between the plan and Install. The uninstall takes
  back a queued swap with the file.
- **The helper that did not finish** (`inst_di_unfinished`): an
  unfinished install with steps (exit 4) that say to shut down, start and
  run the installer again, never "Nothing to do: this release runs it".
- **The Thunderbolt 3 / USB4 box** "(experimental)", never ticked
  unasked (the console's Enter and `/yes` N, `/thunderbolt` explicit),
  its note naming the controller found or saying none was; **Stor98's
  box ticked with it**, also when the box is locked on, and untickable;
  the console's `/thunderbolt` implies Stor98 (Enter Y) unless the new
  `/nostor98`.
- **The Stor98 record**: `PreExisting` only with the run's first Stor98
  change, `Version` only when the run installed or updated Stor98; the
  uninstall removes `Software\xhci98\Stor98`, and `Software\xhci98` when
  empty, also when Stor98 stays.
- **`XhciCmMode` 0**: "Thunderbolt 3 and USB4 will be turned off
  (XhciCmMode 0)" on the Summary and in the console before the
  confirmation.

Commits: `6a061ddf` (the code), `bb40965b` (the records, the release
notes, README, the installer readme, the release readme template),
`f0114d4b`, `5ebd97b0` and `73644472` (reviewed and revised),
`3ccf37f7` (the self-test's scripted answers). Reviewed and revised in
three rounds and a confirmation round. Kept as decided: an absent Stor98
entry on 98 SE and ME is taken for a tunnel drive (the coordinator's
"whether or not a drive is present"); an internal drive on a Stor98
installed by hand that is absent at the update finds the bundled release
when it comes back (record 23 section 4.4).

Host suite: `test_inst` 1621 checks, 0 failures (the file update's
decision, the known tunnel drive, the unfinished helper, `/nostor98`,
the Stor98 decision with Enter Y, the Thunderbolt decision with Enter N,
the `XhciCmMode` 0 warning, the Thunderbolt note and the box's text);
`XHCIINST /selftest` passes (every screen within 79 columns).

The legs, in QEMU (the `usb4-emu` build `bin-2a893ea`), on 98 SE, kit
and evidence in `E:\xhci98-vm\p42-fix1` (QMP 48280 and 48282; `logs\`,
`shots\`), each an overlay of the frozen `p42-s98ver\l3prep` (98 SE with
`3.0.0.0` and Stor98 `0.4.0.0`, bound by hand to the `usb4-generic`
enclosure's NVMe, `F:`), cold starts only. The package: `3.1.0.0`'s
`release-x86` with this branch's installer as built at `f0114d4b`.
`INSTALL /console /thunderbolt /defaults /nopause`, Stor98's question
Enter (Y), the disconnect question Y.

| Leg | Result |
|---|---|
| f1a: the enclosure attached at boot and during the run | **pass**: "Thunderbolt 3 / USB4 support (experimental)", "A Thunderbolt 3 / USB4 controller was found: xHCI98 USB4 Host Router."; the plan "the installed stor98.mpd is an older release ... replaces it ... as a file"; "other bindings 0 ... share 0, file update 1", the tunnel NVMe "not reinstalled: the file update covers it"; no fatal exception; `WININIT.INI` `[rename]` `C:\WINDOWS\SYSTEM\IOSUBSYS\STOR98.MPD=C:\WINDOWS\SYSTEM\IOSUBSYS\STOR98.TMP` beside the setup engine's `xhci98.sys` entry; `INF\OTHER\Yeo Kheng Mengstor98.inf` refreshed (3,981 to 4,098 bytes); "Changes were made. Please shut down and start Windows again.", exit 3. After the shut down and a start with the enclosure attached: `STOR98.MPD` 52,152 bytes, `3.1.0.0`, the package's SHA-256; a 4 MB file copied to `F:` and compared with no differences |
| f1b: the enclosure absent during the run | **pass**: the same plan and file update with no drive present ("other bindings 0 ... file update 1"); the record `PreExisting` 1 (Stor98 there before, by hand) and `Version` `3.1.0.0`; exit 3. After the shut down and a start without the enclosure, `STOR98.MPD` `3.1.0.0` (`0.4.0.0` before) and the `[rename]` done; the enclosure then plugged: its NVMe bound with no wizard, `F:` written and compared with no differences |

Not read: the window's pages in a guest (the Components and Summary
boxes, host vectors and review only); Windows ME; the declined update in
a guest; the NT routes (unchanged, `test_inst` and review only). The
legs ran the installer of `f0114d4b`; the later rounds changed the
declined path, the cancel, the record's first write and the INF
refresh's retry, not the confirmed path the legs read.
