# Design Record 23 - The Install, Upgrade and Uninstall Tool

Status: revision 37, 2026-10-23 (roadmap-usb4 task 43.42's
simplification, record 26 draft 35: the x64 package refused without
`xhcicm.cat` beside the connection manager, section 5.10; the console
no longer asks `XhciInterruptMode` and `XhciForceBulkOnly`, section 8;
Stor98's Enter default from one per-system table, section 8.5; on 98 SE
and ME `xhciuas.sys` put in `SYSTEM32\DRIVERS` with the UAS driver,
section 6; decision D20);
revision 36, 2026-10-23 (roadmap-usb4 task 43.46, design record
30 section 7.2: `/s98vsd` and `/nos98vsd`, `S98VSD.VXD` with Stor98 on
98 SE and ME, the switch table; recorded at the batch 7 merge);
revision 35, 2026-10-22 (roadmap-usb4 task 43.27, record 25
section 7.7: `/faster` and `/nofaster`, the computer's choice of
XHCITR's "can run faster" notice, section 9.5 and the switch table);
revision 34, 2026-10-22 (the catalog refusal's text asks for the
complete package with its `.cat` files, no longer a start with
enforcement disabled, and a guest reading of Secure Boot's state is owed,
not promised before the release; section 5.10); revision 33, 2026-10-22 (the owner's decisions of that day on
43.33's reading: the x64 package carries a test-signed catalog per INF,
which the tool copies with its INF, and the refusal under Secure Boot is
taken from Microsoft's documented policy; sections 5.10 and 5.12, design
record 19 section 3a); revision 32, 2026-10-22 (roadmap-usb4 task 43.42, the owner:
the storage row does not preselect XhciSilentInstall, section 8.4);
revision 31, 2026-10-22 (roadmap-usb4 task 43.43, the owner: on
8 and 8.1 a package without the connection manager warned, not refused,
section 4.5); revision 30, 2026-10-22 (roadmap-usb4 task 43.22, section
5.10: on 64-bit Windows 8 and 8.1 the connection manager installed only
from a test-signed package in test-signing mode, Windows making it a
boot driver; the catalog refusal explained; the Secure Boot note for
32-bit 8 and 8.1; Secure Boot's state read and shown there, record 26
section 5.3.2; keyed on the system's kind, so 10 takes them);
revision 29, 2026-10-22 (roadmap-usb4 task 43.42, option A,
record 26 draft 24 section 6.5.12: with the 98 SE storage row the
Bulk-Only SilentClasses entries added and recorded, and XhciSilentInstall
preselected 1 machine-wide where none is set - the console's question
replaced by that preselection, section 8.3; the uninstall removes the
entries it added, section 9); revision 28, 2026-10-22 (roadmap-usb4 tasks 43.34 to 43.36, the
owner's decisions of 2026-10-20: Windows 10, NT 10.0 below build 22000,
admitted beside 8 and 8.1, Windows Server 2016 to 2022 as 10 with no
Server check, 11 still refused; sections 4.3, 4.5 and 5.10; record 26
draft 23 section 5.3.2); revision 27, 2026-10-22 (roadmap-usb4 task 43.51; the owner's decision of that day to keep
NUSB apart from the non-free driver pack: the seven storage files are read
from the download's own `NUSB33\W98SE\`, beside the version root, no longer
`NONFREE\STORAGE\NUSB33\W98SE\`; section 4.4, record 26 draft 22); revision 26, 2026-10-21 (roadmap-usb4 task 43.43, record 26
draft 20 section 5.3.1: every component on every system, the defaults as
they are, a warning where one does not apply; sections 4.5 and 7.1, and
decision D18 amended); revision 25, 2026-10-21 (roadmap-usb4 task 43.42, record 26
draft 19 section 6.5.11: on Windows 98 SE the pack's two storage INFs
placed in `%windir%\INF` itself, as NUSB's installer places them, so a
new USB disk installs with no wizard, recorded and removed by the
uninstall; section 6.1; and two texts of section 6.4's steps); revision 24, 2026-10-20 (roadmap-usb4 task 43.4, record 26
draft 17 section 6.5: on Windows 98 SE the USB storage drivers, NUSB
3.3's seven files from the download's `NONFREE\`, copied where their
INFs send them and put in the package's copy when the row is ticked and
nothing present is another's, the storage devices installed by the
helper, the uninstall removing what it placed; sections 4.4, 6.4, 7.1,
7.4, 9.4 and 13); revision 23, 2026-10-19 (the fix round of that night, owner:
on 98 SE and ME the bundled Stor98 is updated as a file through
`WININIT.INI`, never by reinstalling a running device, section 4.4 and
record 26 draft 16; Stor98 chosen with the Thunderbolt 3 / USB4
support, `/nostor98`, section 7.1; that support experimental and never
ticked or answered Y unasked, section 8.5; no Stor98 install record
without a Stor98 change; the `XhciCmMode` 0 warning, section 8.3);
revision 22, 2026-10-19 (`SETUP.EXE` for people, `INSTALL.BAT`
for the command line; see below); revision 21, 2026-10-19 (the x64 package's filter INF at the
flavour directory's root; see below); revision 20, 2026-10-19 (the 98 SE
and ME uninstall removes SETUPX's `OEM Name` record of each INF copy it
deletes, section 6.1, and section 9.5's `/list` line reads the Run value;
the matrix's 98 SE and ME legs of that day); revision 19, 2026-10-19 (task 42-A.30: the bundled Stor98 is
built with the package at its version, no pins); revision 18, 2026-10-19
(task 42-A.26: the bundled Stor98 on tunnel drives only; see below);
revision 17, 2026-10-19 (task 42-A.24: the
CriticalDeviceDatabase service entries and the mark service following
`XhciCmSafeEject`, sections 5.1 and 9.2); revision 16, 2026-10-19 (task 42-A.22, record 26 draft 11: Stor98
part of the Thunderbolt support, the UAS driver a component of its own;
sections 4.4, 7.1 and 8.1); revision 15, 2026-10-19 (`XhciFastPollFsLs`
settable; see below); revision 14, 2026-10-19 (task 42-A.27: a
parent's job and the devices below it, sections 5.8 and 10); revision
13, 2026-10-19 (the connection manager alone, and
Windows 8 and 8.1 for it; see below); revision 12, 2026-10-19 (the mark
service and the machine-wide key); revision 11, 2026-10-18 (the window,
the helper, the uninstall
from Add/Remove Programs; see below); revision 10, 2026-10-16 (the
console, the questions and the
install directory; see below); revision 9, 2026-10-16 (the USB tree
tool); revision 8, 2026-10-15, reviewed and revised six times the same day;
the code is written against it (task 40.5) and was reviewed and revised
against it the same day. Written for the task group the
owner added to Phase 40 of `roadmap-usb4.md` on 2026-10-15: the `3.0.0.0`
package gains an install and upgrade script, `INSTALL.BAT`, in front of a
console tool of this project's own, `XHCIINST.EXE` (x86) and
`XHCIIN64.EXE` (x64), built in-tree the way `XHCICLN.EXE` and
`XHCICL64.EXE` are (design record 13 section 10.13.8). The same day the
owner widened the scope to an uninstall mode and to asking the user about a
curated set of the drivers' registry values, each asked only where the
hardware it affects is present. It ships in `3.0.0.0` (the no-cuts rule).
Section 13 lists the decisions taken in writing it, each an orchestrator
decision for the owner to confirm; section 14 what is open.

Revision 9, 2026-10-16 (roadmap-usb4.md task 41-A.6): the tool also
installs, starts at login and removes the xHCI98 USB Tree, `XHCITR.EXE` /
`XHCITR64.EXE` (design record 25 section 8): section 9.5, the `/notray`
switch in section 7.1, the packaging in section 11. The `/devices` and
`/eject` lock (record 24 section 3.3) is now opened before it is created,
and created with a security an unelevated `XHCITR` can use, and a
maintenance mutex keeps two runs from working on the tool at once
(section 9.5).

Revision 10, 2026-10-16 (the owner, after running the tool on a real
machine under Windows 98): every line the tool prints is at most 79
columns, through a word wrap all its output passes (section 7.6); every
question lists its choices one a line, each with its meaning, and ends
"Enter for [x]", x being what Enter gives (sections 8.3 and 8.5, and the
confirmation, the uninstall's word and the eject's questions); the
xHCI98 tools go to an install directory the user names, `C:\XHCI98` on
the system's drive by default, with `/to <path>` to give it unasked,
recorded in the registry for the update and the uninstall, and on 98 SE
and ME the package copy `DevicePath` names goes there too (section 9.6;
it replaces `%windir%\XHCI98` in sections 4.4, 6.2, 9.4 and 9.5); and on
98 SE and ME the steps are a short numbered list per device, followed by
an offer to open Device Manager (section 6.4). The owner then decided, the
same day, that on 98 SE and ME the package copy goes straight into the
install directory with no subfolder (section 4.4), and that Enter at the
confirmation goes on ("Enter for [Y]", section 5.1 step 4).

Revision 11, 2026-10-18 (roadmap-usb4.md tasks 42-A.2 and 42-A.3, design
record 26 section 12; record 27 section 10): a second front end, the
window `XHCIWIZ.EXE` / `XHCIWZ64.EXE`, over one engine, and the uninstall
from Add/Remove Programs. What changes here, section by section:

- **The engine** (sections 3, 10 and 15): the system half moves from
  `xhciinst.c` to `xhciinst\inst_eng.c` behind the front-end table of
  record 26 section 3.2; `xhciinst.c` is the console front end. Every rule
  of this record stays the engine's or the pure half's, and the split
  itself leaves the console's screens as they were (the bullets below
  change some of their content).
- **Section 2**: `INSTALL.BAT` opens the window; `/console` as its first
  word runs the console tool with the rest (on 98 SE and ME the batch file
  drops the word, seven arguments after it; on NT the console tool takes
  it). The package's `tools\xhciinst\` holds the window pair beside the
  console pair, and record 27's helper `XHCIDI.EXE` for 98 SE and ME
  (staged by the helper's own task, 42-A.1; a package without it keeps
  the printed steps).
- **Section 3**: `build.cmd` builds the four executables (the window with
  the static multithreaded runtime, `/SUBSYSTEM:WINDOWS,4.0` for
  `XHCIWIZ.EXE` and `5.02` for `XHCIWZ64.EXE`; its import gate the seven
  modules of record 26 section 4); the helper has its own build (record
  27 section 3).
- **Section 4.2**: the package-layout refusal holds for an install and an
  update; the uninstall needs no package (W12).
- **Section 5.5**: the routine's `hwndParent` is the front end's window
  (the window's sheet; NULL on the console, as before).
- **Sections 6.1, 6.2 and 6.4**: on 98 SE and ME each device of the plan is
  installed by record 27's helper after the copy and `DevicePath`, in its
  order (the controllers; after the settle the root hub, hubs and
  composite entries, at most four passes; UAS devices; the host
  interface), each pass behind the gate (the tree settled, and the user's
  word that no hardware wizard is open; `/yes` asks no word), each device
  read back afterwards (record 26 section 8). Section 6.4's printed steps
  stay for a device the helper could not install (with a line of its own
  for one gone after its install), and in their old form - the controller
  and an updated root hub, the wizards doing the rest - when the package
  has no helper or its probe fails. "Never drives
  `SETUPX`" above now reads: never through anything but record 27's
  documented calls.
- **Section 7.1**: `/from <dir>` (the uninstall's relaunch), `/console`
  (the console's no-op word) and `/debug` (the window's own choice of the
  `debug-` directory); the window refuses the console's switches (record
  26 section 10).
- **Sections 8.1, 8.3 and 8.4**: an answer is written machine-wide, to
  `HKLM\System\CurrentControlSet\Services\USB` (task 42-A.17), and only
  where it differs from the value in force (the machine-wide one, else
  each device's own); the question shows the machine-wide value beside
  each device's own; a per-controller value is left as it is. The table
  gains the connection manager's waits and log values (ranges) and
  `XhciCmBridgeNumbering` (98 SE and ME, written by no INF), all never
  asked, set with `/set` or the window's Advanced dialog.
- **Section 9**: every install and update writes the Add/Remove Programs
  entry (record 26 section 9.1) once the installer's own files are in the
  install directory (record 26 section 6.2); an uninstall started from the
  install directory relaunches a temporary copy with `/from` (W6); 98 SE
  and ME remove each package entry through the helper, children first,
  behind the settle and the gate, an entry left stopping the destructive
  steps, a fresh reading finding none before `DevicePath`, the copy, the
  INF copies and the driver files go (record 26 section 9.3 step 4); where
  the helper cannot run, nothing goes: the devices are removed in Device
  Manager by hand and the uninstall run again, which, finding none, goes
  on; the machine-wide values the table names are deleted; the tools, the
  installer's files (the window last), the records, the entry and the
  directory go last, only when nothing is left for a later run, and an
  uninstall that stopped, or could not remove one of these, keeps its
  entry, named "(incomplete: run again)", with its program and records
  (W14).
- **Not in this revision**: the bundled Stor98 component (record 26
  sections 5.6, 6.3 and 6.4) and the safe-removal mark service (record 21
  section 7.4 revision 3, task 42-A.5); sections 4.4 and 9.2 stay as they
  were for them. The mark service is revision 12's.

Revision 12, 2026-10-19 (roadmap-usb4.md task 42-A.5's installer hooks and
task 42-A.17's installer part; record 21 section 7.4 revision 3, "The
installer"; record 26 section 12): the safe-removal mark service
`xhcimark` and the marks it writes, and the machine-wide key.

- **Section 3**: the package carries `tools\xhcimark\` (`XHCIMARK.EXE`,
  `XHCIMK64.EXE`, built by `xhcimark\build.cmd`); the tool copies one of
  them, never runs it. `CreateServiceA`, `ChangeServiceConfigA`,
  `StartServiceA` and `ControlService` are looked up at run time like the
  other routines; the import table stays `KERNEL32` and `ADVAPI32`.
- **Section 5.1**: a step 12 at the end of every install and update run,
  whatever its verdict (record 21 section 7.4, decision M13): with the
  `xhcicm` service registered and the Thunderbolt component in the run,
  on 2000 to 7, the mark service installed or updated and started; with
  no `xhcicm` service registered, step 0 of section 9.2 run, so no
  `LowerFilters` value names `xhcicm` once the run ends (a filter with no
  service stops the device at its next start, Code 39).
- **Section 8.4**: the machine-wide key is made where it is absent at
  every install and update, a value is read there by the drivers' rule
  (present in any type wins, a zero-length `REG_NONE` is absent; a value
  that is not a `DWORD` wins with the driver's own fallback, which is not
  always the INF's default, so it is shown as unreadable and a value
  chosen for it is always written over it), and every write goes there,
  never to a controller's own key.
- **`/eject`** (record 24 section 7.2; record 21 section 7.4, decision
  M16): on NT, after its own successful `CM_Request_Device_EjectA` (not
  the query-and-remove fallback), the tool reads the NHI's marks answer
  (`IOCTL_XHCICM_MARKS`) for the unit's one located entry with a pending
  departure, and writes `XhciCmEjectConfirm` (`REG_BINARY`, 28 bytes,
  `cm_query.h`'s `CM_QC_*`) into the NHI's driver key while that value is
  absent or empty, under the eject lock it already holds
  (`Global\XhciinstTbAsk`): the driver then tears the tunnel down, which on
  Windows 2000 nothing else confirms.
- **Section 9.2**: step 0 before the filter hosts, and the inventory read
  again before step 5.
- **Section 9.5**: the mark service's copy in `system32`.
- **Section 13**: D16.

Revision 21, 2026-10-19 (roadmap-usb4.md task 42-A.23, the owner's
decision of that day): the x64 package keeps the tunnel-port filter INF at
the flavour directory's root, beside `xhcicm.inf` and its one
`xhcicm.sys`, with no `nt\` and no second copy of the binary; the x86
package keeps `nt\` as before. The place is a pure function of the
architecture (`inst_cmf_rel`, with `inst_cmf_pnf` and `inst_cmf_where`):
section 4.1's table, section 4.4's listing, section 8.5's file set, and
section 10's list.

Revision 13, 2026-10-19 (roadmap-usb4.md task 42-A.21; record 26 draft 8;
record 21 section 3.5). The owner, 2026-10-19: "The installer should give
people the option not to install xhci98 but just the CM. Reason is that in
case some Win 8 users which have native USB3 but not USB4 or TB may want
it". What changes here:

- **Section 4.5, new**: the connection manager alone. On 2000 to 7 the
  USB drivers (`xhci98.sys`, `xhciuas.sys` and on x86 `xhcihlpr.sys`)
  can be left out of a run that carries the connection manager
  (`/cmonly`, or the window's box unticked); on Windows 8 and 8.1 that
  is the only run there is; on 98 SE and ME the USB drivers stay
  required. A run without them plans, stages and installs only the
  Thunderbolt group, asks no USB value, and leaves an installed
  `xhci98.sys` exactly as it is.
- **Section 4.3**: Windows 8 and 8.1 (NT 6.2 and 6.3) are admitted for
  the connection manager alone; 10 and later stay refused, as before. The
  version is read through `ntdll.dll`'s `RtlGetVersion` on NT, because
  `GetVersionEx` in a program with no compatibility manifest answers 6.2
  on 8.1, 10 and 11 alike.
- **Section 5.10**: the note reads the connection manager's binary when
  the USB drivers are not in the run, and on 8 and 8.1 x64 adds that
  test-signing mode can be turned on only with Secure Boot off.
- **Section 7.1**: `/cmonly`.
- **Section 8.5**: with the USB drivers out of the run the Thunderbolt
  support is in it unasked: it is all the run installs.
- **Section 9.1**: the warning names the connection manager alone when
  the `xhci98` service is not registered and `xhcicm`'s is (a machine
  where only the connection manager was installed); the typed word stays.
- **Sections 9.5 and 5.1 step 12**: the mark service on 8 and 8.1 too.
- **Section 13**: D17 to D19.

Revision 15, 2026-10-19 (roadmap-usb4.md task 42-A.29; the owner, for
`3.1.0.0`: "XhciFastPollFsLs
should be in advanced settings"). What changes here:

- **Section 8.1**: `XhciFastPollFsLs` joins the table, never asked,
  settable with `/set` and the window's "Advanced..." for 0, 2, 3 and 4
  (1 is not a mode), written machine-wide like the other `xhci98.inf`
  values (section 8.4). A choice list now names values 0 to 4, so the
  table's choice texts reach value 4.

Revision 17, 2026-10-19 (roadmap-usb4.md task 42-A.24; the owner,
2026-10-19, for the mark service and `XhciCmSafeEject`). What changes here:

- **Section 9.2 step 0**: the CriticalDeviceDatabase's `LowerFilters`
  and `UpperFilters` copies of `xhcicm`, every entry, first and read again
  at the end (record 21 section 7.4).
- **Section 9.2 step 6a, new**: every CriticalDeviceDatabase entry whose
  `Service` names a service of the package that is gone is deleted.
- **Section 5.1 step 12**: the mark service follows the `XhciCmSafeEject`
  in force after the run; step 6a's scrub runs at the end of every NT
  install and update run too.

Revision 18, 2026-10-19 (roadmap-usb4.md task 42-A.26; the owner's
decision of 2026-10-19): the bundled Stor98 is installed on NVMe drives
behind a Thunderbolt or USB4 tunnel only.

- **Section 4.4**: the paragraph on the bundled Stor98 now says what is
  copied, staged and installed, and where: tunnel drives only, with the
  Thunderbolt component, the place read from the connection manager's
  confirmed reading; the shared file and service held when a Stor98 the
  installer did not put on a tunnel drive exists.
- **Sections 5.4 and 6.2**: the plan's verdict for a device `stor98.inf`
  matches passes the gate of section 4.4 after every reading, so an
  install, an update, a repair, `/force` and `/restage` are all held to
  it; section 5.5's refusal of a hardware id shared with a device the run
  leaves stands, with its own sentence for a drive the gate leaves.
- **Record 26 sections 5.3, 6.3, 6.4 and 9.3** carry the rest: the
  Components page's sentence, the uninstall's adoption of tunnel drives
  Windows bound after the run.

Revision 22, 2026-10-19 (roadmap-usb4.md task 42-A.31; record 26 draft 15
section 2 and W17). The owner, 2026-10-19: "setup.exe by tonight". The
package root gains `SETUP.EXE`, the program a person double-clicks: it
starts the installer's window and exits, with no console window behind
it. `INSTALL.BAT` stays, its behaviour unchanged, for the command line
and scripts. This supersedes decision D1's one entry point (the owner's
of 2026-10-15, kept by record 26 W2 on 2026-10-18). What changes here:

- **Section 2**: the layout gains `SETUP.EXE` at the root, and the
  paragraph on the one batch file says who uses which. `README.md` and
  the release readme name two ways only, `SETUP.EXE` or Device Manager
  by hand; `INSTALL.BAT` and the console are documented in
  `tools\xhciinst\readme.txt` alone (record 26 section 2).
- **Section 7.4**: `SETUP.EXE` does not pass the code on.
- **Sections 13 and 15**: D1 revised; the files.

The tool never ships `devcon` or any other Microsoft file, never drives
Windows 98's 16-bit setup engine (`SETUPX`) through anything undocumented,
and never changes a boot setting.

## 1. What it is for, and what it is not

### 1.1 The problem

Installing this package is Device Manager work today, one device at a
time, and the clicks differ per system (README "Install", release notes
"Installing"). Updating is worse, because each system has its own trap,
all of them recorded in the release notes' "Updating from 2.y.y.y":

- **Windows 2000** keeps the old driver when Update Driver is left to
  search: it ignores the dates of unsigned drivers, so Have Disk is the
  only route, and the same is true of a reinstall after the update.
- **Vista and 7** keep the old `xhci98.sys` when the new INF is identical
  to the installed one (the same `DriverVer`): Have Disk reuses the package
  already in the driver store.
- **The root hub** keeps showing the old version until it is updated
  separately; from `2.3.x` the root hub, every hub and every composite
  device are new devnodes instead and install by themselves (design record
  13 section 10.13.4).
- **32-bit Vista and 7** keep the earlier release running until a restart,
  for the helper `xhcihlpr.sys`.
- **Windows 98 SE and ME** need a restart Windows does not ask for.
- **xhciuas.sys** with the same `DriverVer` is not taken by a search.

A user who meets none of these by reading first gets a machine running the
old driver with the new one's name on the box. The tool does on the NT
systems what those paragraphs tell the user to do by hand, and says what it
did; on Windows 98 SE and ME, where no documented interface installs a
driver on a device, it puts the package where the hardware wizard looks by
itself and prints the clicks that remain.

### 1.2 Scope

| | Windows 2000, XP, XP x64, Vista, 7 (x86 and x64) | Windows 98 SE, ME |
|---|---|---|
| Install and update on present devices | every driver of the package: the controller, then the root hub, hubs and composite entries, `xhciuas.sys`, `xhcicm.sys` on the NHI and the tunnel-port filter (section 5) | staged where the wizard searches; the clicks printed (section 6) |
| Pre-staging for devices not present | every INF of the package (`SetupCopyOEMInf`) | the same staging |
| Registry values (section 8) | asked, written to the driver key | asked where the driver key exists |
| Uninstall (section 9) | devnodes, the filter name, the OEM INFs, the services, the files, each gated on the step before | what the tool placed; the manual steps printed |
| A dry run, `/list` | everything above read, nothing changed | the same |

Out of scope: a Windows Setup (text-mode or F6) path, which stays
`txtsetup.oem`'s; the qualifier and the snapshot reader, which keep their
own batch files; any device bound to another vendor's driver, which the
tool lists and leaves (decision D3); the boot configuration, test-signing
mode included, which the tool reads and reports and never changes
(decision D6); and a removal of the OS's own `usbd.sys` and `usbui.dll`
(section 9.3).

### 1.3 Why a tool of this project's own

`devcon` would do half of the NT work, and it is a Microsoft sample binary
the project may not ship (AGENTS.md, "Third-Party Material and
Provenance"; `legal-provenance.md` section 5). Windows has the documented
routines a driver package's own installer is meant to call - `newdev.dll`'s
`UpdateDriverForPlugAndPlayDevices` and `setupapi.dll`'s `SetupCopyOEMInf`,
`SetupUninstallOEMInf` and the `SetupDi` family - and the tool is a thin
program around them, in the shape `XHCICLN` already has.

## 2. Where it lives in the package (decision D1)

```
<package>\SETUP.EXE                for people: starts the window (revision 22)
<package>\INSTALL.BAT              the command line's entry point
<package>\release-x86\ ...         unchanged: the drivers and their INFs
<package>\release-x64\ ...
<package>\debug-x86\, debug-x64\
<package>\tools\xhciinst\XHCIINST.EXE   32-bit
<package>\tools\xhciinst\XHCIIN64.EXE   x64
<package>\tools\xhciinst\readme.txt, NOTICE.TXT
<package>\tools\xhcicln\ ...       unchanged
```

Since `3.0.0.0` the release download's `release-x86\` and `release-x64\`
may also carry Stor98, the NVMe driver (`build-and-test.md`, "The bundled
Stor98 build"): at the directory's root, every name starting with
`stor98`. In `3.0.0.0` it was a pinned Stor98 release (`0.4.0.0`) copied
unchanged; since `3.1.0.0` (roadmap-usb4 task 42-A.30) it is built from
`src\stor98` with the drivers, carries the package's version and date, and
is published in `releases\<version>\` like them. The
tool does not install it (section 4.4).

`INSTALL.BAT` sits at the package root, beside `readme.txt`, because it is
the one thing a user is told to run and must be found without reading.
The executables sit in `tools\xhciinst\`, beside `tools\xhcicln\`, because
that is where this package keeps its programs: `make-release.ps1`'s tool
list and `Assert-ToolsLayout`, the 8.3 directory names, the per-tool
`readme.txt` and `NOTICE.TXT` all exist for that layout, and the flavour
directories stay what they are, the media Have Disk and the F6 floppy are
pointed at - a floppy built from `release-x86\` must not grow two
executables. The tool finds the flavour directory from its own location,
two levels up, so the batch file passes only a name.

There is one batch file, not an `UNINSTALL.BAT` beside it: the uninstall
is `INSTALL.BAT /uninstall`. A second batch file at the root is one
double-click from removing the driver of the only keyboard on an
xHCI-only machine; behind a switch it is one typed word and a confirmation
(section 9.1).

Since revision 22 a person is not sent to `INSTALL.BAT` at all: the
readme's first instruction is "To install or update, double-click
SETUP.EXE; to uninstall, use Remove in Add/Remove Programs (or SETUP
/uninstall)", and `SETUP.EXE` starts the window (record 26
section 2). `INSTALL.BAT` is for the command line and scripts
(`INSTALL /console ...`); with no argument it still opens the same
window. `SETUP.EXE` is a program, not a second batch file, and does
nothing an `INSTALL` with the same arguments would not: the uninstall
stays behind `/uninstall`.

`INSTALL.BAT` runs under Windows 98's `COMMAND.COM` and under `cmd.exe`.
It chooses the executable and the flavour directory and passes the
arguments on:

- `%OS%` other than `Windows_NT`: Windows 98 SE or ME; `XHCIINST.EXE`,
  `release-x86`. `COMMAND.COM` has no `%~dp0` and no `%*`, so the batch
  file runs the tool by its path relative to the current directory, which
  is the batch file's own when it is started from Explorer, passes `%1` to
  `%9`, and the tool checks its own location (section 4). `COMMAND.COM`
  splits an argument at `=`, so `/set Name=Value` arrives as two
  arguments; the tool joins them (section 7.1). A tenth argument would be
  lost silently, so the batch file refuses any command line that uses
  `%9` at all, and names `TOOLS\XHCIINST\XHCIINST.EXE /dir release-x86`
  as the way to pass more.
- `Windows_NT`, `PROCESSOR_ARCHITECTURE` or `PROCESSOR_ARCHITEW6432` equal
  to `AMD64`: `XHCIIN64.EXE`, `release-x64`, the arguments passed with
  `%*`.
- `Windows_NT` otherwise: `XHCIINST.EXE`, `release-x86`.
- `/debug` among the arguments: the `debug-` directory instead, which the
  tool says is for troubleshooting only.

The batch file is ASCII with CRLF endings (`.gitattributes` holds `*.BAT`
to CRLF; MS-DOS 7.1 loses `goto` labels in LF files), and `make-release.ps1`
stages it from `xhciinst\INSTALL.BAT`.

## 3. Toolchain and builds

`XHCICLN`'s, exactly (design record 13 section 10.13.8, decision D7 there):

- The tool's sources are in `xhciinst\` at the repository root beside
  `xhcicln\`: `xhciinst.c`, the system half, and `inst_core.c`, the pure
  half shared with a host suite (section 10).
- `xhciinst\build.cmd`: `XHCIINST.EXE` with the in-repo MSVC 6.0
  (`tools\MSVC600`), `/W3 /WX /O2`, the C runtime linked statically
  (`LIBC`, no `/MD`), so it starts on 98 SE, ME, 2000, XP and 32-bit Vista
  and 7; `XHCIIN64.EXE` from the same sources with WDK 7.1's amd64
  compiler (`tools\WinDDK71`), `/MT`, subsystem 5.02, for XP x64, Vista x64
  and 7 x64. `build.cmd x86` builds the first alone.
- **A native x64 build by design**: Microsoft documents that a 32-bit
  process on 64-bit Windows cannot run device installation
  (`SetupDiCallClassInstaller` and the routines built on it fail with
  `ERROR_IN_WOW64`). The 32-bit build under WOW64 (`IsWow64Process`,
  resolved at run time) refuses with exit 1 and names `XHCIIN64.EXE`;
  `INSTALL.BAT` never gets there.
- **Imports**: `KERNEL32` and `ADVAPI32` alone. `setupapi.dll`,
  `newdev.dll`, `cfgmgr32.dll` and `version.dll` are loaded with
  `LoadLibrary` and every routine looked up with `GetProcAddress`, so the
  image loads on every target, including Windows 98, where `newdev.dll`
  has no such routine; a routine missing that a step needs refuses that
  step, says which routine, and never substitutes another (section 7.3).
  `xhciinst\selftest.cmd` holds the import table to those two names
  (`xhciinst\check-imports.ps1`, which reads the PE import directory
  itself: every module name, whatever its characters, and no delay-load
  table), the tool's import gate (section 11).
- **A manifest** asking for elevation (`requireAdministrator`), embedded
  as resource 1 of type 24 through `xhciinst.rc`, as `xhcicln.manifest` is;
  98, ME, 2000 and XP ignore it, and on Vista and 7 every run asks, `/list`
  included. On 2000 and XP the tool checks for administrator rights itself
  (section 7.2).
- **The version** is `src\xhci_version.h`'s `XHCI_VER_STR`, printed first
  on every run and compared with the package's (section 4);
  `make-release.ps1` refuses a stale or mis-versioned tool as it does for
  `XHCICLN`.
- **The port switchover's gate** is the driver's own: the tool compiles
  `src\xhci_psw.c`, the pure half, as the host suites compile it, and asks
  `XhciPswGate` whether a present controller is one the switchover applies
  to (section 8.2), so the list of Intel ids exists once.
- **Provenance**: the executables carry statically linked runtimes, MSVC
  6.0's and WDK 7.1's, as `XHCICLN`'s do; `legal-provenance.md` section 2a
  gains their rows and `NOTICE.TXT` records the linkage. `XHCIIN64.EXE` is a
  user-mode program, not a driver: the packager's test signing (design
  record 19) does not apply to it.
- **The mark service's binaries** (revision 12; record 21 section 7.4,
  revision 3): `XHCIMARK.EXE` and `XHCIMK64.EXE`, built by
  `xhcimark\build.cmd` and staged under the package's `tools\xhcimark\`;
  the tool copies the one of its own architecture into `system32`
  (section 9.5) and never runs it. The service routines it needs beyond
  those it already imports (`CreateServiceA`, `ChangeServiceConfigA`,
  `StartServiceA`, `ControlService`) are looked up at run time, so the
  import table stays `KERNEL32` and `ADVAPI32`.

## 4. The package as the tool sees it, and the refusals

### 4.1 The driver table

The tool knows the package's drivers by file, never by hardware id:

| INF | Driver | Service | Systems | Present in the package |
|---|---|---|---|---|
| `xhci98.inf` | `xhci98.sys` (and `xhcihlpr.sys` in x86) | `xhci98` (`xhcihlpr`) | all | required |
| `xhciuas.inf` | `xhciuas.sys` | `xhciuas` | all | required |
| `xhcicm.inf` | `xhcicm.sys` | `xhcicm` | all | the set of three, all or none |
| `nt\xhcicmf.inf` (x86), `xhcicmf.inf` (x64) | `xhcicm.sys` as an upper filter | `xhcicm` | NT only (`$Windows NT$`) - in the x86 package in the flavour directory's `nt\` subdirectory, because Windows 98 and ME read a `$Windows NT$` INF and would offer the filter for the tunnel bridge (task 39-0.4, `runs/run-39.md`); read and copied on NT only, never on 98 or ME; beside its own copy of `xhcicm.sys`, `nt\xhcicm.sys`, the same bytes, because Vista's and 7's driver store refuses a source outside the INF's directory (`SetupCopyOEMInf` error `0xA1`, read on 7 SP1 x86). In the x64 package, from `3.1.0.0`, at the flavour directory's root beside `xhcicm.inf` and the one `xhcicm.sys`, with no `nt\`: 98 and ME never read it, the same-directory rule is met at the root, and no other INF there binds a PCI bridge. `inst_cmf_rel` (section 10) gives the place per architecture | the set of four on x86 and of three on x64 (on 98 and ME, `xhcicm.inf` and `xhcicm.sys`) |

The hardware ids come from the INFs themselves (section 5.2): a model line
added to an INF reaches the tool with no change to it. A package without
the three `xhcicm` files is a development package (`make-package.ps1`
stages them only when it builds them); the tool then says that the
package carries no Thunderbolt or USB4 driver and goes on. One or two of
the three present is refused.

### 4.2 "Unpacked correctly"

Before anything is read from the system, the tool refuses (exit 1) unless:

1. its own path (`GetModuleFileName`) ends in `\tools\xhciinst\<its name>`,
   so the package root is two levels up - a tool run from a ZIP folder
   view, which Explorer extracts alone to a temporary directory, fails
   here and says "unzip the whole package first";
2. the flavour directory named (`release-x86` and the rest) is a plain
   name, exists under that root, and its name's architecture is the
   tool's own;
3. every file of section 4.1's required rows is there, a regular file, not
   empty, and every file each INF names in `[SourceDisksFiles]` (and its
   decorated forms) is there too - found relative to that INF's own
   directory by its disk's `[SourceDisksNames]` path and its own
   subdirectory, as the setup engines resolve it, and refused if the path
   leaves the flavour directory;
4. each INF's `[Version]` `DriverVer` version field equals the tool's
   `XHCI_VER_STR`, and all of them agree - an INF from another release in
   the directory is refused by name;
5. each INF's `[Manufacturer]` lines are the tool's architecture's: the
   x86 INFs carry an undecorated models section and only `NTx86`
   decorations, the x64 INFs only `NTamd64` decorations and no undecorated
   section (the INF gate's `PATH-NO9X` and `PATH-MFGDEC` rules, read again
   here because a user can mix directories); a decoration the tool does
   not know is refused, not guessed;
6. every `.sys` is a PE image whose machine field is the tool's (`0x014C`
   for x86, `0x8664` for x64), whose file version resource (`version.dll`)
   equals the package version - an older `.sys` beside a current INF is
   refused - and, for the images that carry one (`xhci98.sys`,
   `xhciuas.sys`), whose `XHCI98_FLAVOUR_*` marker is exactly one and is the
   directory's flavour (`RELEASE` in `release-`, `DEBUG` in `debug-`): the
   marker `scripts\check-flavour-marker.ps1` reads at every build, so a
   release and a debug binary swapped between directories, or a `qemu`
   build, is refused.

### 4.3 Mixing architectures

Refused, exit 1, each with the right command named: the 32-bit tool on
64-bit Windows (WOW64; section 3); the x64 tool on 32-bit Windows (it does
not start there: Windows reports it is not a valid application, which
`INSTALL.BAT` never invites, since it picks by `PROCESSOR_ARCHITECTURE`);
an x64 flavour directory given to the 32-bit tool or the reverse
(section 4.2 items 2, 5 and 6). A Windows newer than 7 or older than 2000
or 98 SE is refused too: it is not a target of this package. The one
exception (revision 13) is Windows 8 and 8.1, NT 6.2 and 6.3, admitted for
the connection manager alone (section 4.5); 10 and later stay refused
(until revision 28, which admits 10 below build 22000 beside them; below).

On NT the version is read through `ntdll.dll`'s `RtlGetVersion`, looked
up at run time, and `GetVersionExA` is the fallback where the routine is
missing (2000 and XP report their own version through either). Windows
8.1 and later answer `GetVersionEx` with 6.2 in a program whose manifest
names no supported system, which the tool's do not, so before revision 13
the refusal of 10 and 11 rested on that 6.2 being above 6.1; with 6.2
admitted that reading cannot tell 8 from 11. A `GetVersionExA` answer of
6.2 or above with no `RtlGetVersion` to read is refused for that reason.
The rule is pure (`inst_os_kind`, section 10): 98 SE (4.10, build 2222
or later) and ME (4.90) and NT 5.0 to 6.1 are full targets, NT 6.2 and 6.3
the connection manager's alone, anything else refused.

**Windows 10 (revision 28; roadmap-usb4 task 43.34, the owner's decision
of 2026-10-20).** NT 10.0 with a build below 22000 is admitted as 8 and
8.1 are, the same kind (`INST_OS_CMONLY`, which from this revision means
"8, 8.1 and 10"): every client release from 1507 (build 10240) to 22H2
(19045) and the LTSC releases. Windows 11 also reports 10.0, from build
22000, and stays refused: it has a USB4 connection manager of its own.
The two are told apart by the build alone, the low word of
`RtlGetVersion`'s `dwBuildNumber` (the reading above; 10 has the
routine, so the `GetVersionExA` answer, which stops at 6.2 for a program
whose manifest does not name 10, never decides it). Windows Server 2016
(14393), 2019 (17763) and 2022 (20348) report 10.0 below 22000 and are
admitted as 10, untested: the tool makes no Server check (`wProductType`
is not read), by the owner's decision. Any other NT 10.x version, and
any major version above 10, is refused. The name follows the build
(`inst_os_name`, which takes it from this revision): "Windows 10" below
22000, a Server build included, and "Windows 11" from it. Host vectors:
10240, 14393, 17763, 19045, 20348 and 21999 admitted; 22000 and 26100
refused; 10.1 and 11.0 refused. Only 22H2 is to be read (roadmap-usb4
task 43.37), and the release notes say which build was.

### 4.4 The local copy

Every run that changes anything first copies the flavour directory's files
to a directory of this project's own - on 2000 to 7 under
`%windir%\XHCI98\`, on 98 SE and ME the install directory itself (section
9.6, revision 10; below) - and stages and installs from that copy
(decision D7). Two reasons: on Windows 2000 and
XP a device found later takes its files from where the INF was staged from,
which must not be a CD, a floppy or a USB stick behind the controller being
updated; and on 98 SE and ME the directory is what `DevicePath` names
(section 6).

**98 SE and ME since revision 10: the install directory itself**
(owner, 2026-10-16: no subfolder; `DevicePath` names `C:\XHCI98` and the
steps say "type C:\XHCI98"). What the immutable subfolders below gave is
kept by the order of an update (`make_flat_copy`): the new files are
staged and checked in `XHCIINST.NEW` below the directory, with a list
whose first line is `#TAG <content tag>`; `DevicePath` stops naming the
directory; the current list is renamed `XHCIINST.OLD` and its files
deleted (each listed INF with its `.PNF`); the staged files are moved in;
the new list is put in place last; and only then does `DevicePath` name
the directory again. So no wizard searches a mixture of two packages,
and a run cut short leaves the directory off `DevicePath`, every file of
the package in it named by one of the three lists, for the next run to
finish. A package file's name that is in the directory and on none of
the lists is the user's: the copy refuses, and nothing the user put
there is overwritten or deleted. A copy whose tag and files are this
package's is left as it is. The uninstall deletes what the lists name,
and the tool, and nothing else.

**On 2000 to 7 the copies are immutable.** Each is named for its content - eight hex
digits, an FNV-1a hash of every file's relative name and bytes (an 8.3
name, which 98 SE's "Copy files from" box takes as typed, section 6.2) -
holds an `XHCIINST.LST` naming the files the tool
put there, and is never rewritten. A copy is built in `<name>.NEW` and
published by renaming it only once every file and the list's closing `END`
line are written, so a failed copy never passes for a finished one; a
directory whose list lacks the `END` line (an interrupted run) is removed
and built again; one whose list cannot be read refuses the run (it is
never removed on a guess), and a listing of the package that does not
reach its end never publishes a copy. A finished copy is never rewritten:
a run whose package is already there
checks the files are byte-identical and uses them, and a run of another
package makes another directory. So an older package run for one driver
cannot replace the files a newer installed package's INF still names, and
a run that changes only a registry value replaces nothing. The copies are
kept while anything may name them, and only the uninstall removes them:
the files each list names, then each directory and `%windir%\XHCI98`
(and the install directory, section 9.6) only if empty. With each listed INF goes its `.PNF`, which the device installer
of 98 SE writes beside an INF it reads in place (the 98 SE uninstall leg,
2026-10-15: without it the directory could not be removed, and the
reinstall refused); a copy whose list is already gone loses only the
`.PNF`s of this package's INFs, never another file.

**The bundled Stor98 driver: tunnel drives only** (owner, 2026-10-19;
revision 18). The listing takes the flavour directory's files and `nt\` (the x64
package has none),
never another subdirectory, and it leaves out every file whose name
starts with `stor98` (`inst_bundled_file`) unless Stor98 is in the run,
because a copy of `stor98.inf` on `DevicePath` or in the driver store
would have Windows offer Stor98 when nobody chose it. Stor98 is in the run only when
it is chosen (its box, the console's question, `/stor98`) **and** the
Thunderbolt component is, which Stor98 is part of (revision 16,
`inst_s98_box_open`, the window's rule for its box, which the engine's
`s98_set` applies too): the engine keeps it out otherwise, and the
console says so and asks nothing. Since revision 23 (owner, 2026-10-19,
replacing "never unasked" of 2026-10-18) it is chosen with that
component: its box is ticked whenever the Thunderbolt box is, and can be
unticked; the console's question answers Y on Enter and under `/yes`,
and `/nostor98` leaves it out (section 7.1, `inst_s98_decide`). The
Thunderbolt component itself is never chosen unasked (section 8.5).

In the run, the tool installs, updates and reconfigures Stor98 only on
an NVMe drive behind a Thunderbolt or USB4 tunnel. Where a drive sits is
read once per reading of the devices from the connection manager's
confirmed reading, the walk of record 24 sections 3.3 and 4 that
`/devices` and `/eject` take (`tb_read_all`: the status record asked and
echoed, its tunnel ports matched by bus and address on NT and by PCI path
on 98 SE and ME), under the lock `XHCITR` shares
(`inst_s98_place`):

- **behind a tunnel**: a verified tunnel port above the drive;
- **internal**: no verified port above it, in a reading that is whole
  (every devnode read, no record moved), with at least one connection
  manager confirmed and every one found confirmed, and no Thunderbolt or
  USB4 port by its id, nor one carrying the tunnel-port filter, above it;
- **unknown**: anything else - the drive not in the walk, a record that
  moved, a reading not whole, no manager or one unconfirmed, an
  unverified port above it. Unknown is never taken for internal and never
  for a tunnel.

After every plan (`plan`, which runs after every reading, so after each
settle and barrier too), each present device `stor98.inf` matches is
eligible only behind a tunnel with the Thunderbolt component in the run
(`inst_s98_eligible`); an internal drive is left ("skip-internal"), one
of unknown place is left and is work left for a later run when it is
still unknown at the end ("skip-unknown", exit 4). Whatever the plan
decided for it - an install, an update, a repair of a missing file,
`/force`, `/restage` - a device the gate leaves is not run, gets no
printed steps on 98 SE and ME, and is named on screen and in the log with
its reason (the plan's `LEFT:` line, a line when a later reading changes
it, the window's found page). On NT one call reaches every device of a
hardware id (section 5.5), so a tunnel drive that shares its id with a
drive the gate leaves is refused too, with that sentence.

**The shared file and service.** Every Stor98 device runs one
`stor98.sys` (2000 to 7) or `stor98.mpd` (98 SE and ME) and reads one
service's settings, so a change made for a tunnel drive reaches an
internal drive on Stor98 as well. A device bound to Stor98 counts as
another when it is not a known tunnel drive (`inst_s98_known_tunnel`):
present and not behind a tunnel, or, on NT, not present and not named
by the install record (an unreadable record names none); on 98 SE and
ME every driver key naming `stor98.mpd` is read for it, present or not,
and since revision 23 one with no present devnode is a drive gone with
its tunnel, not another: an internal drive there is always present, and
the uninstall's adoption reads a drive that is not present the same way
(record 26 section 6.3). So on those systems an enclosure that is not
plugged in no longer holds Stor98 at its old release for ever (the 98 SE
update of 2026-10-19 left `stor98.mpd` at `0.4.0.0` with the enclosure
absent; the owner's coordinator asked that the update run whether or
not a drive is present). What that accepts: an internal drive on a
Stor98 installed by hand that is absent at the update - taken out, or
off in the firmware - is not seen, and finds the bundled release when
it comes back. With another, the
conservative choice is taken (`inst_s98_share`): when the installed file
is another release than the bundled one, or cannot be read, the run
makes **no Stor98 change at all** - `stor98.inf` is not staged, no
`stor98` file goes into the 98 SE and ME install directory, no device is
installed or updated and no setting is written - and says so, naming
Stor98's readme for an update by hand; when it is the bundled release,
tunnel drives are installed (the file stays what it is) but no setting
is written, and that is said too. A run whose copy was made held stays
held to its end, even when a later reading would lift the hold, since
its copy has no Stor98. On 98 SE and ME the driver key of an unplugged
drive is matched to the record through its `Enum\PCI` entry. On NT
`stor98.inf` is not staged either, and no `/restage` reaches it, while
another build of it is in the driver store and another Stor98 exists.
The devnode of an internal drive on Stor98 is never updated or
reconfigured by the tool.

**98 SE and ME: the file, never a running device** (revision 23; owner,
2026-10-19). Replacing a running `stor98.mpd` through Windows' device
installer stopped 98 SE with "fatal exception 0E at 0028:FF09E842",
every time, in the update over `3.0.0.0` and Stor98 `0.4.0.0` with a
tunnel enclosure attached and mounted (the helper had run
`DIF_SELECTBESTCOMPATDRV` and `CtlSetLddPath`). So on those systems a
device Stor98 runs is never installed again, whatever the plan says of
it (an update, a repair, `/force`). When the installed
`IOSUBSYS\stor98.mpd` is an older release than the bundled one and the
shared-file rule leaves the file free (`inst_s98_file_update`), the run
replaces the file instead, whether or not a drive is present: the new
`stor98.mpd` is written beside the old one as `STOR98.TMP` and compared
with the package's, and its swap is asked in `WININIT.INI`'s `[rename]`
section with 8.3 names, the mechanism the setup engine uses for a
running `xhci98.sys` (read back by `wininit_pending`); the INF copy each
Stor98 driver key names, in `INF` or `INF\OTHER`, is made the bundled
`stor98.inf` (`s98_inf_refresh_9x`), and the install directory's copy
is the package's through the flat copy. The run then asks for a shut
down and a start, not a restart. A drive first plugged after that start
binds to the new file by itself. The update waits for the user's word,
one box on the Summary page and one question in the console, both
"Please disconnect all external NVMe drives before proceeding" (Enter
and `/yes` give N); without it Stor98 is left for a later run, out of
this one whole - no copy of its files, no job, no setting, no record
(exit 4). A Stor98 drive no helper job touches may be unplugged between
the plan and Install, as the confirmation asks: the comparison of the
plan with the devices read again leaves it out. The INF copies are made
the bundled one's again by every later run while the file is the
bundled release, so a refresh cut short is finished then.
A device with no driver behind a tunnel is still installed by the
helper. NT is unchanged: SetupAPI updates Stor98 there.

What remains, said in the release notes and the package's readme:
`stor98.inf` matches every NVMe controller (`PCI\CC_010802`), and it is
staged on NT and in the install directory on 98 SE and ME so that a
tunnel drive that appears later - the usual case, since the tunnel may
come up only after the restart - installs, so Windows may still offer
Stor98 for an internal NVMe drive that has no driver. The tool never
selects one.

**The USB storage drivers on 98 SE (revision 24).** When record 26
section 6.5's row is ticked and its case admits the same-bytes route,
the copy of 98 SE also takes the seven files of the download's
`NUSB33\W98SE\` (`package_files`, from that folder),
listed as the package's own files are, and only once the five binaries
are in place where their INFs send them; otherwise it takes none of
them, and when an earlier run had put them there (the record's
`Source`) the directory is taken off `DevicePath` before the copy is
tried (record 26 section 6.5.4).

**The UAS driver left out (revision 16).** Under `/nouas`, or its
unticked box in the window (record 26 section 5.3), `xhciuas.inf` and
`xhciuas.sys` are left out of the 98 SE and ME copy, since a copy on
`DevicePath` would have the hardware wizard install it unasked; on NT
`xhciuas.inf` is not staged. An `xhciuas` already installed stays as it
is and gets no group verdict (`group_verdicts`), and an unfinished
`/restage` that includes it is refused while it is left out.

### 4.5 The connection manager alone (revision 13)

The owner, 2026-10-19: a user whose Windows already has a USB 3 driver
(Windows 8 and 8.1 carry Microsoft's own, and on 2000 to 7 a controller
vendor's may run) may want the Thunderbolt 3 / USB4 connection manager
without this package's USB drivers. What the connection manager needs of
the USB drivers is record 21 section 3.5: nothing, on NT; the devices a
tunnel brings are driven by whatever driver Windows binds to them.

**Which systems** (`inst_usb_decide`, pure, host-tested case by case):

| System | The USB drivers | `/cmonly` |
|---|---|---|
| 98 SE, ME | required, as before | refused: "the connection manager alone is not offered on Windows 98 SE and ME" |
| 2000, XP, XP x64, Vista, 7 | in the run unless left out; can be left out only when the package carries the connection manager | leaves them out; refused when the package carries no connection manager |
| 8, 8.1 | never in the run, never offered | accepted, and changes nothing |

(Revision 26, record 26 draft 20 section 5.3.1, roadmap-usb4 task 43.43,
the owner's decision of 2026-10-21: every component on every system,
the defaults as they are, a warning where one does not apply.) The table
becomes, with `inst_usb_decide` refusing nothing but a `/cmonly` with no
connection manager in the package and `/usb` with `/cmonly` (revision 31,
owner 2026-10-22: a package without the connection manager on 8 and 8.1
is no longer refused; the run warns, record 26 section 5.3.1 case 5, and
goes on with the USB drivers' box and the tools):

| System | The USB drivers | `/cmonly` |
|---|---|---|
| 98 SE, ME | in the run unless left out; can be left out when the package carries the connection manager, with the warning of record 26 section 5.3.1 case 2 (the connection manager alone, untested there) | leaves them out, with that warning |
| 2000, XP, XP x64, Vista, 7 | as before | as before |
| 8, 8.1 | left out unless `/usb` (or the window's box) puts them in, with case 3a's warning (untested there); installed as on Vista and 7 | accepted; leaves them out, as before |

The paragraph below, which keeps them required on 98 SE and ME and never
offered on 8 and 8.1, stands as the reason for the warnings' words
rather than as a refusal.

98 SE and ME keep them required because those systems have no USB 3
driver of their own: a machine with the connection manager and no
`xhci98.sys` has every xHCI controller dark, the dock's controller behind
a tunnel included, and on those two systems the connection manager's
re-enumeration counts on `xhci98.sys` starting on a controller a tunnel
adds (record 21 section 3.1). On 8 and 8.1 the USB drivers are not
offered: this package's USB drivers have never been read on those
systems, Windows' own `usbxhci.sys` runs every xHCI controller there, and
a second driver for the same class id is what the plan would then
propose. A package without the connection manager's files is refused on
8 and 8.1 ("this package carries no Thunderbolt or USB4 driver: there is
nothing to install on Windows 8 or 8.1").

**A run without the USB drivers** is the Thunderbolt question's N
(section 8.5) applied to the other groups: `xhci98.inf` and `xhciuas.inf`
are left out of the run - not planned, not staged, no verdict on their
groups, no USB value asked (a `/set` of an `Xhci*` value of those INFs is
refused, as one the run cannot apply) - and the Thunderbolt support is
in the run unasked (section 8.5). The package check (section 4.2) is
unchanged: the package must still be whole. The local copy is the whole
package, as before, so a later run with the USB drivers ticked uses the
same copy; on NT nothing searches that copy, so the INFs it holds and
does not stage are offered to no device. An `xhci98.sys` or `xhciuas.sys`
already installed is neither updated nor removed: its devices are not
part of the run, its group gets no verdict, and the plan says "The xHCI98
USB drivers already installed stay as they are (INSTALL.BAT /uninstall
removes the whole package)". An unfinished `/restage` that owes
`xhci98.inf` or `xhciuas.inf` is refused under `/cmonly` with "run it
again with the USB drivers to finish it", as section 8.5 refuses one that
owes the Thunderbolt INFs under N. The bundled Stor98 is not offered on 8
and 8.1 (it is not one of its targets; the decision is the owner's); on
2000 to 7 it stays what record 26 section 6.3 says, whichever drivers are
in the run.

**From one to the other.** A machine with the connection manager alone
gets the USB drivers from a later run with them in it, which installs
them as a fresh install (a controller another vendor's driver runs is
foreign and left, decision D3). The way back, from the whole package to
the connection manager alone, is the uninstall (section 9) followed by a
run with `/cmonly`: a run that leaves the USB drivers out never removes
them, because removing the controller's driver on an xHCI-only machine
takes its USB keyboard and mouse, which only the uninstall's warning and
typed word may do (D17). One Add/Remove Programs entry covers both
(record 26 section 9.1), its name following what the machine has.

**On Windows 8 and 8.1** the NT route of section 5 runs for the
Thunderbolt group as on Vista and 7: the INFs' `.NTx86.6.0` and
`.NTamd64.6.0` models sections (the highest decoration not above 6.2,
section 5.2), the driver store and its trap (5.7, from 6.0 on), the
filter's forced install (5.5), the mark service (9.5) and the test-signing
note on x64 (5.10). None of it has been read on 8 or 8.1: no guest of
either exists here. The x64 connection manager loads there only as on
Vista x64 and 7 x64 - test-signing mode, which 8 and 8.1 let a user turn
on only with Secure Boot off in the firmware setup, or a start with
driver signature enforcement disabled (Advanced startup, Startup
Settings) - because 64-bit Windows 8 enforces kernel-mode signatures and
the package's x64 binaries carry the project's test signature at most
(design record 19). The installer never signs anything and never changes
a boot setting (D6).

**On Windows 10** (revision 28; roadmap-usb4 tasks 43.34 and 43.35, 43.22's
and 43.29's work for 8 and 8.1 reused) what this section says of 8 and
8.1 holds as it stands, the 8 and 8.1 rows of both tables included: the
connection manager installed; the USB drivers offered unticked, Windows'
own USB 3 driver staying the default, ticked only by the user (`/usb` or
the window's box) with record 26 section 5.3.1's case 3a warning, a
controller whose service is Windows' own `USBXHCI` then installed over
and the uninstall removing its devnode and rescanning so that Windows
binds `usbxhci.sys` again (unread until roadmap-usb4 task 43.37); the
bundled Stor98 offered unticked under the Thunderbolt box with case 3b's
warning, 10's own `stornvme` on a tunnel drive installed over when it is
ticked, as on 8.1; the NUSB pack's row taking case 1; and a package
without the connection manager handled as on 8 and 8.1. The NT route is
8's: the INFs' `.NTx86.6.0` and `.NTamd64.6.0` models sections, the
highest decoration not above 10.0 (`inst_models_section`, host vectors
on 10.0), so no INF changes; the driver store and its trap (5.7); the
filter's forced install (5.5); the mark service (9.5; `inst_mk_system`
admits 10.0, 11 being refused before it is asked); and the signature note
(5.10). None of it has been read on 10. Three things are 10's own and
wait on roadmap-usb4 task 43.33's readings. (1) Intel's Thunderbolt
driver: 10 carries no Thunderbolt or USB4 driver of its own, but
computer makers ship Intel's on the NHI; until 43.33 (c) reads which ids
and service it binds and the owner decides whether the installer leaves
it alone, asks, or replaces it, an NHI it runs is what any other
vendor's device is (decision D3): foreign and left, and the uninstall,
having taken nothing, gives nothing back. (2) What `xhci98.sys` and its
INFs need on 10 beyond what 8 and 8.1 get (43.33 (a)), and the box's
note on what the user gains and loses, which until then is 8's
sentence. (3) What SCSIport on 10 asks of Stor98, read with 43.23's
reading for 8.1.

## 5. The NT route: Windows 2000, XP, XP x64, Vista and 7

### 5.1 Order of operations

1. Refusals (section 4), rights (section 7.2), the routines (section 7.3).
2. Read the system: every devnode, present or not (section 5.3); the
   driver key of each one bound to this package; the installed binaries'
   versions (section 5.4); the driver store for the Vista and 7 trap
   (section 5.7, read without staging); test-signing mode on Vista x64
   and 7 x64 (section 5.10).
3. Plan: for each device its action and for each service group its
   verdict (section 5.4); print the plan, the warnings (section 5.8 for
   the input warning) and the questions that would be asked. `/list`
   stops here, exit 0, having written nothing (section 7.1).
4. Ask the registry questions (section 8) and the install directory
   (section 9.6), then ask once whether to go on (`Y`, or Enter since the
   owner's decision of 2026-10-16, revision 10; `N`, or end of input,
   cancels with exit 1 and changes nothing; another answer is asked
   again). `/yes` skips the confirmation
   and asks no registry question either - a run nobody answers keeps every
   value `/set` does not name, as `/defaults` does (decided 2026-10-15
   from the 98 SE leg, where `/set ... /yes` still asked); `/defaults` and
   `/set` answer the registry questions without asking.
5. Ask Windows whether an installation is in progress
   (`CMP_WaitNoPendingInstallEvents` with no wait, Microsoft's "Checking
   for In-Progress Installations") and let one finish: the settle of
   section 5.8, bounded at a minute; still "pending" then, devices that
   never stand still, or a failed question, stop the run with nothing
   changed (exit 4). Then - every time, since Windows may have changed a
   device while the question was open - the devices, the driver store's
   trap (section 5.7) and `/restage`'s precondition are read again and
   planned again; if they no longer match the plan the user confirmed
   (each device of the package with its driver, version, problem, filters,
   action and decision, and every device, present or not, naming a trapped
   INF), the run stops, nothing changed, with "run INSTALL.BAT again to see
   the new plan" (exit 4). The uninstall waits before it reads the devices
   at all. The
   message also says to restart first when no wizard is open: on Windows
   2000 a cancelled Found New Hardware wizard stays pending until the next
   start (read on the 2000 leg, 2026-10-15: a windowless `rundll32.exe`,
   the answer "pending" at every run). Then copy
   the package locally (section 4.4) and pre-stage every INF from the copy
   whose service group is not refused (sections 5.4 and 5.6); with
   `/restage`, replace a stored package the trap holds (section 5.7).
6. Update the controllers (`xhci98.inf`).
7. Wait for the system to finish installing what the update made appear
   (the settle of section 5.8, up to 10 minutes), and read the devnodes
   again: after an update from `2.3.x` the root hub, hubs and composite
   devices are new devnodes (record 13 section 10.13.4). A wait that times
   out stops the run here (section 5.8).
8. Update or install the root hub, hubs and composite entries, then
   `xhciuas.sys`'s devices, then the NHI (`xhcicm.inf`), then the
   tunnel-port filter (`xhcicmf.inf`).
9. Write the registry answers (section 8.4).
10. Read the devnodes a last time and check each changed device's result
    (section 5.9): bound to this package's service at the package version,
    started, or a stated reason why not.
11. Report the restart (section 5.9), and, where old `XHCI98\` entries
    exist, one line naming `tools\xhcicln\` (section 5.11).
12. **The marks' invariant** (revision 12; record 21 section 7.4, decision
    M13), at the end of every run that got this far or stopped on the
    way, whatever its verdict: a `LowerFilters` value naming `xhcicm`
    exists only while the `xhcicm` service is registered and safe eject
    is on. Safe eject is the `XhciCmSafeEject` in force once the run's own
    writes are done, by task 42-A.17's precedence (revision 17; the owner,
    2026-10-19): the machine-wide value when present (one that is not a
    `DWORD` reads as the driver's default 1), else the value in the
    driver key of every present device whose service is `xhcicm`, read
    whatever this run plans for it, else the default 1; it is off only
    when the value in force is 0 for every such device
    (`inst_mk_safe_eject`). With the `xhcicm` service registered, safe
    eject on and the Thunderbolt component in the run, on Windows 2000 to
    8.1 (8 and 8.1 from revision 13), the mark service is installed or
    updated and started (section 9.5); with it registered, safe eject on
    and the component not in the run, nothing of the marks changes; with
    it registered and safe eject off, or with no `xhcicm` service (an
    update that ended without it, a name left on a machine where the
    service was removed by hand), section 9.2's step 0 runs, so every
    such name is removed, the CriticalDeviceDatabase's copies with them,
    and the mark service, if any, deleted; with `xhcicm` registered and
    safe eject off, the database's `UpperFilters` copies stay, being the
    tunnel-port filter's and not a mark's (read on 7: an install with
    `XhciCmSafeEject=0` had taken the filter INF's out before this rule),
    while with no `xhcicm` service and at the uninstall both values go. So a later
    `INSTALL /console /set XhciCmSafeEject=0` removes the service and its
    marks at the end of that run, and `=1` installs it again at the end
    of a run that carries the Thunderbolt component. A step 0 that fails
    is a failure of the run, said with "restart Windows and run
    INSTALL.BAT again". A state of the `xhcicm` service that cannot be
    read changes nothing. Then, on NT, step 6a of section 9.2 runs (an
    update's INF retires `xhcimsi` on Vista and 7), its failure a failure
    of the run.

Steps 6 to 8 stop at the first failure that leaves the order meaningless
(a controller update failed), report what was done and exit 2; a failure
on one hub or one UAS disk is reported and the rest go on, exit 2 at the
end.

### 5.2 The models section per system

Each INF's hardware ids are the model lines of the models section the
running system reads, chosen by the rule the engines apply to the
`[Manufacturer]` line's decorations (Microsoft's "INF Manufacturer
Section"):

- **Windows 2000** reads the undecorated section only: decorations on
  `[Manufacturer]` are an XP addition, which is why `xhci98.inf` shares
  `[XhciModels]` between 98 and 2000 (record 21 section 3.3). An INF whose
  only sections are decorated (the x64 INFs) has nothing for 2000, which
  section 4.2 has already refused on that system.
- **XP onward**: of the decorations whose architecture is the system's
  (`NTx86`, `NTamd64`, or `NT` with none) and whose version, when given
  (`.6.0`), is at most the system's, the most specific - the highest
  version, and with equal versions the one naming the architecture; none
  matching, the undecorated section. The product-type and suite fields a
  decoration may carry after the version are not used by this package's
  INFs; a decoration carrying them is refused by section 4.2 item 5.

The rule is pure (section 10) and is checked against the INFs of both
architectures in the host suite: XP and 2000 get `[XhciModels]`,
`[XhciModels.NTx86.6.0]` goes to Vista and 7 x86, XP x64 gets
`.NTamd64`, Vista x64 and 7 x64 `.NTamd64.6.0`.

The tool reads the INFs with a parser of its own (section 10): `[Version]`,
`[Manufacturer]`, the models sections, `[SourceDisksFiles]` and, for
section 8, the `AddReg` lines. It needs no `setupapi.dll`, so the same
code reads the package on Windows 98.

### 5.3 Reading the devices

On NT every devnode, present or not, comes from `SetupDiGetClassDevs` with
`DIGCF_ALLCLASSES` and no `DIGCF_PRESENT`, walked to `ERROR_NO_MORE_ITEMS`,
for its instance id and `DeviceDesc`. Every property that decides
anything - `HardwareID`, `CompatibleIDs`, `Service`, `Driver`,
`UpperFilters` - is read from the devnode's own key under
`HKLM\SYSTEM\CurrentControlSet\Enum\<instance id>`, not through
`SetupDiGetDeviceRegistryProperty`, whose `ERROR_INVALID_DATA` means
"absent **or** invalid" (Microsoft's documentation). Each has three
answers, as `XHCICLN`'s reads do: read - the type expected (`REG_SZ` or
`REG_MULTI_SZ`), fitting the buffer, and terminated by the tool whatever
was stored; absent - `ERROR_FILE_NOT_FOUND` for the value, the one answer
that proves it; or unreadable - the key not opening, another type, data
too long. Presence has three too: `CM_Get_DevNode_Status` answering
is present, `CR_NO_SUCH_DEVINST` is absent, anything else is unknown. A
failure to build or walk the set, more devnodes than the tool holds, an
instance id that cannot be read, or any unreadable property or unknown
presence refuses the run (exit 1), naming the devnode: the plan is never
made on a partial picture, and an absent `Service` is only trusted when
it was read as absent. On 98 and ME the tree comes from `cfgmgr32.dll`,
the walk `XHCISNAP` makes (section 6.3).

A driver key's values (`DriverVersion`, `DriverDate`, `InfPath` - the
uninstall's reference check, section 9.2 -
`ProviderName`, and on 98 and ME `NTMPDriver`) are read from
`HKLM\SYSTEM\CurrentControlSet\Control\Class\<Driver>` on NT and
`HKLM\System\CurrentControlSet\Services\Class\<Driver>` on 98 and ME. Each
read has three answers: the value, absent (the key opened and the value
is not there), or unreadable (the key did not open, or the value is not a
string, or does not parse); only the first two are used for a decision.

A device is **matched** by an INF when one of its hardware or compatible
ids equals one of the INF's model ids for the running system, compared
without case.

### 5.4 The action per device, and the verdict per service group

**Ownership first.** A device bound to another vendor's service is foreign
whatever its problem code: it is listed and left (decision D3), and no
problem code turns it into an install. Then:

| The matched device | Action |
|---|---|
| No `Service` value at all | install |
| Bound to this INF's service (`xhci98`, `xhciuas`, `xhcicm`), driver key `DriverVersion` older than the package, or absent | update (a problem code 1, 18 or 28 on it too: it is reinstalled) |
| The same, equal to the package's | current, nothing done; `/force` reinstalls |
| The same, newer than the package | refused (section below); `/force` installs the older release (going back) |
| The same, `DriverVersion` unreadable | unknown: refused, said, `/force` does not override it |
| A tunnel port (`xhcicmf.inf`) bound to `pci` without `xhcicm` in `UpperFilters` | install the filter (the INF keeps `pci.sys` through `machine.inf`'s section) |
| A tunnel port with `xhcicm` in `UpperFilters` | as for an own service |
| Bound to any other service | foreign: listed, left |

The controller's model id is the class id `PCI\CC_0C0330`, so every xHCI
controller in the machine is matched; one running another vendor's driver
is foreign and left. The same holds for a UAS device another driver holds.

**A missing or older payload is repaired.** When the group's installed
binary is missing or older than the package's - or, for `xhci98` on 32-bit
NT, `xhcihlpr.sys` is - while a device's driver key says "current", that
device is reinstalled from its INF (for `xhci98`, its controllers: their
install sections are the ones that copy and register both files), so the
INF puts the files back. The cases "current driver key, missing file" and
"current driver key, older file" are legs (section 12).

**The verdict per service group.** The devnodes of one service share one
binary: every devnode bound to `xhci98` - each controller, root hub, hub
and composite entry - runs the one loaded `xhci98.sys`, and the release
notes record two controllers whose driver keys showed different versions
while both ran the same image ("From 2.3.x or earlier", "Two
controllers"). So replacing the file for one device replaces it for all,
and the decision is made per group (`xhci98`, `xhciuas`, `xhcicm` with its
NHIs and filtered ports):

- the group's installed binary, `%windir%\system32\drivers\<service>.sys`
  (and for `xhci98` on 32-bit NT also `xhcihlpr.sys`, which the
  controller's INF installs), has its file version read (`version.dll`):
  a file that is not there is "not installed" - a fresh install, or a file
  to put back - and never refuses; a file whose version cannot be read is
  unknown;
- the group is **refused** when any of its devnodes, present or not, is
  newer or unknown, or the installed binary is newer or unknown: nothing of
  that group is installed, and the group's devices are listed with the
  reason. `/force` lifts a "newer" refusal for the whole group, after the
  plan names every devnode it reaches; it never lifts an "unknown";
- otherwise each device's own action stands.

### 5.5 Installing: `UpdateDriverForPlugAndPlayDevices`

`newdev.dll`'s `UpdateDriverForPlugAndPlayDevicesA(NULL, hwid, inf,
INSTALLFLAG_FORCE, &reboot)`, documented from Windows 2000, installs the
best driver of the given INF on every present device whose hardware or
compatible ids include `hwid`. Three properties of it decide how it is
called:

- **`INSTALLFLAG_FORCE`, with one exception.** Without it the routine
  installs only a driver it ranks better than the installed one, and
  Windows 2000 does not rank an unsigned driver's date (the release notes'
  "use Have Disk"); a reinstall of the same version (`/force`, `/restage`)
  needs it as well. The decision whether to install at all is section
  5.4's, made before the call. The exception (owner, 2026-10-15): the
  tunnel-port filter's first install on Windows 2000 is unforced, the
  route task 39-0.4 read there (2000 takes the unsigned filter INF over
  `machine.inf` by itself; XP and 7 rank it below and need the force).
- **The id is the device's own first hardware id**, its most specific,
  never the INF's model id: the controller's model id `PCI\CC_0C0330`
  would reach every xHCI controller, a foreign one included. Before each
  call the tool checks that every present device carrying that id among
  its ids is one this step was going to install or update; if not - two
  identical controllers, one with another vendor's driver - the call is
  not made for that id and the devices are reported (exit 4), with the
  way on (`/force` where the device is this package's and `/force` would
  include it, Device Manager otherwise). One exception: the ports of one
  Thunderbolt switch share one id, and a healthy port that already
  carries this release's filter and needs nothing does not block the call
  that gives the filter to its siblings; that call may reach it again
  with the same driver; under Windows 2000's unforced call it is not
  counted as installed by it, while a forced call reinstalls it and it is
  checked like the other targets (read
  on the 2000 leg, 2026-10-15, where the downstream ports were otherwise
  never filtered).
- **`hwndParent` NULL**: the signature question (XP's "has not passed
  Windows Logo testing", Vista's and 7's "cannot verify the publisher") is
  Windows' own dialog, answered by the user; the tool never changes the
  driver-signing policy. A refusal there comes back as an error the tool
  reports by number.

### 5.6 Pre-staging: `SetupCopyOEMInf`

Every INF of the package whose service group is not refused (section 5.4)
is copied into the system's INF store with
`SetupCopyOEMInfA(inf, localCopy, SPOST_PATH, 0, ...)` from the local copy
(section 4.4) before any device is touched, so a device that appears later
- a UAS disk plugged next week, a hub, the root hub after an update from
`2.3.x`, an NHI that appears only with a dock - finds its driver with no
question about where. The call is made without `SP_COPY_NOOVERWRITE`, so
an INF already present with identical contents takes the routine's own
existing-INF path (Microsoft's `SetupCopyOEMInf` remarks) with the local
copy as the source location, the intent being that an INF staged earlier
from a removable disk then names the local copy. That this refresh happens
on 2000 and XP is not assumed: it is read on the legs (section 12, leg M) -
the package is moved and its medium removed, and a device plugged
afterwards must install with no prompt; if it does not, the release notes
say to keep the original medium at hand.

On Vista and 7 the whole package goes into the driver store and a later
install takes its files from there; on 2000 and XP the store holds the INF
and its `.pnf`, which records the local copy as the source.

### 5.7 The Vista and 7 trap: an identical INF in the driver store

On Vista and 7, when the store already holds an INF byte-identical to the
package's, Windows installs from the stored package, with its files, so a
build whose INF did not change keeps the old `.sys` (README, "Upgrading
the driver"). The tool detects it per INF, before staging and in `/list`
too, without changing anything:

1. each `%windir%\inf\oem*.inf` whose bytes equal the package INF's is a
   stored copy;
2. `SetupGetInfDriverStoreLocationA` (Vista onward) on it gives the stored
   INF's full path, and the directory part is the stored package;
3. each file of the INF's `[SourceDisksFiles]` is compared byte for byte
   with the package's.

All equal: nothing to do, the store holds this package. Any difference is
the trap. "No trap" is a verified answer only: a store that cannot be
inspected - the routine missing, a location query failing, an `oem*.inf`
or the directory that cannot be read, an unusable source entry - leaves
the INFs it may concern uninstalled, says so and ends with exit 4, like
the trap, but `/restage` does not replace what it could not read. Without `/restage` the tool installs nothing from that INF,
reports "the driver store holds a different build under an identical INF;
run INSTALL.BAT /restage to replace it" and ends with exit 4. With
`/restage` (decision D5), after the plan has named every devnode, present
or not, whose driver key's `InfPath` is that `oemNN.inf`, and the
confirmation:

0. **The precondition**: every devnode, present or not, whose driver key's
   `InfPath` names that `oemNN.inf` must be present, so each can be moved
   to the new package in this run, and every `InfPath` must be readable;
   otherwise `/restage` refuses that INF ("connect it first") and the trap
   stays reported (exit 4). Forcing the package out from under a device
   that cannot be reinstalled now is never done;
1. the local copy is in place (section 4.4), so the replacement's source
   cannot vanish;
2. the work owed is written down first, flushed and closed before anything
   is removed: `%windir%\XHCI98\RESTAGE.LST` holds `PKG` and the local
   copy's directory name (the package identity), `INF` with the INF's slot
   and the removed `oemNN.inf`, and one `DEV` line per devnode that named
   it, and a closing `END` line with the number of `INF` and `DEV` lines.
   Every update is written to `RESTAGE.TMP`, flushed to the disk and moved
   over `RESTAGE.LST` in one step (`MoveFileEx`, replace, write-through),
   so an interrupted write leaves the previous complete journal. A later
   run reads it first: an unreadable, malformed or incomplete file (no
   `END` line, counts that differ, a line after it, a `DEV` or `NEW` of no
   recorded `INF`) refuses the run (exit 1); a file naming another package's copy refuses it too,
   with that package named ("run that package's INSTALL.BAT"); the same
   package's run reinstalls every `DEV` device present, whatever its
   `DriverVersion` says and whether or not the trap is still detectable;
3. `SetupUninstallOEMInfA(oemNN.inf, SUOI_FORCEDELETE, NULL)`, which
   removes the package even while devices reference it (Microsoft's
   documentation; that is why the plan names them first);
4. the package is staged again from the local copy; if that fails, the
   run stops (exit 2) with the instruction to run `INSTALL.BAT` again,
   which stages from the same local copy and, by the work file, reinstalls
   - the devices keep running the files already in `system32\drivers`
   meanwhile;
5. the replacement is checked before any device is moved to it: its
   stored package (`SetupGetInfDriverStoreLocationA` on the name staging
   returned) must hold exactly this package's files, and that name is then
   journalled as `NEW` - it may be the removed `oemNN.inf`'s own name, which
   Windows does not promise never to reuse. A mismatch stops the run with
   the journal kept;
6. every listed device is reinstalled. A `DEV` line goes only when its
   device was reinstalled by this run with a call that succeeded (an
   unchanged device can read the same name and version), is present, its
   driver key names the verified replacement (the
   `NEW` name, whatever it is), and it runs the package's version with no
   problem code; the `INF` line goes
   when none of its devices is left; the file goes when nothing is. A
   listed device absent at a later run - unplugged after the precondition
   held - keeps its line, is reported, and sets the steps flag
   ("connect it and run INSTALL.BAT"): completion is never claimed for a
   device that was not seen moved.

Released packages never meet the trap - each release has its own
`DriverVer` - so this is a development-build route and is said to be. On
2000 and XP an identical INF is no trap: the install reads its files from
the INF's own source, the local copy (read on the legs).

### 5.8 After the controller, and the input warning

**The warning, before the confirmation.** When the plan changes a
controller, the tool prints before it asks: the USB keyboard and mouse on
the controller stop for a moment while it restarts; from `2.3.x` or
earlier on XP, XP x64 and 98 SE they do not work until the questions
Windows asks for the new root hub and hubs are answered (release notes,
"From 2.3.x or earlier"), so a keyboard and mouse that do not go through
this driver - PS/2, a laptop's built-in ones, a USB controller another
driver runs - are needed first. The tool cannot tell which input devices
are behind the controller, so the warning is unconditional when a
controller changes.

From `2.4.0.0` onward the root hub, hubs and composite devices keep their
ids, so step 8 updates them as section 5.4 says: each is bound to `xhci98`
with an older driver key, which is what the release notes' "then on xHCI98
USB 3.x Root Hub" does by hand. From `2.3.x` they are new devnodes after
the controller's update (record 13 section 10.13.4):

**Every dependent phase has the same barrier.** Installing a root hub or a
hub shows the hubs behind it, a UAS disk may show children, an NHI shows
its ports. So step 8 runs per INF, repeated while new targets appear (at
most four passes): before each pass the system must settle, and the
devices are read and planned again, with every safeguard of sections 5.4
and 5.5 applied afresh; a timeout or a failed question stops the run with
"finish the windows Windows shows (Next through Finish, even when they
find no driver; never cancel one), then run INSTALL.BAT again" (exit 4),
and the next run continues from what it finds - a root hub whose wizard
was finished with no driver is a device with no driver, installed then
(on 98 SE a cancelled wizard takes the devnode away until the next start:
record 27 section 5.1).

**The settle** (read on the 2000 leg, 2026-10-15: the controller updated
from `2.3.1.0`, a single `CMP_WaitNoPendingInstallEvents` answered "none"
before the new root hub had appeared; the tool's install call for it then
met Windows' own Found New Hardware install of the same devnode, failed
with `0x20` (`ERROR_SHARING_VIOLATION`), Windows' install ended at Code 28
and the keyboard behind it stopped; a second run installed it). The tool
asks `CMP_WaitNoPendingInstallEvents` with no wait every half second and
reads a signature of the devnodes below this package's controllers (ids,
started, problem code - the problem number taken only when the status
reports one, as `CM_Get_DevNode_Status` specifies). A reading that fails
anywhere (an id, a child or a sibling other than "none"), or a walk cut
short by its bounds (a subtree deeper than eight levels, more than 2048
devnodes), is not a reading: it restarts the quiet stretch. It goes on only after three seconds with
nothing pending and the signature read whole and unchanged - at the bound
too: devices still moving there are "unsteady", which stops the run like
a timeout - and not while a device is expected:
a controller this run updated is started but shows no root hub of this
driver yet, or a device of the bus (`USB\XHCI98_...`) has Code 1 or 18
(not configured yet) - each waited for at most 20 seconds. While Windows
has an install pending the tool waits (10 minutes at the barrier, a
minute before the first change) and, after 20 seconds, says once that
Windows has opened its own Found New Hardware window, perhaps behind the
tool's, and to answer it. A device Windows installs that way is found
current at the next reading and not touched again. An install call that
still meets Windows' own install (`0x20`) is no failure the first two
times: the device stays eligible, and after the next barrier's settle the
call is made again if the device still needs it, controllers included;
the third such answer is a failure. One call reaches every device of its
id, so it is made again only while every device it reaches has tries
left, and a call is not made at all - before anything is noted - while a
device of its id has spent its tries, so a member that appears later
cannot buy an earlier one another try. The pure half holds the settle's
state machine and the retry rule (section 10). Completion is only declared after a last
barrier and a fresh plan: a device the plan would still install - one the
last allowed pass exposed - is listed as "not finished", the steps flag is
set and the run ends with exit 4 and "run INSTALL.BAT again".

**A parent's job and the devices below it** (revision 14, task 42-A.27;
read on the 98
SE update over `3.0.0.0` with an external hub, 2026-10-19: the root hub's
reinstall restarted it, its hub was gone for about four seconds, and the
hub's job, made at once, found it not present and left a step for a device
the next start showed current). A parent is a controller or a device of
the bus (root hub, hub, composite entry): its job may restart the devices
below it. Within a pass, a parent's job that reached its device (on NT a
call that succeeded; on 98 SE and ME any helper verdict but `not-present`
and `refused`) is followed, when another job of the same pass comes after
it, by a wait of its own: up to 15 seconds for the parent to be present
again, then the settle above with no device expected (the quiet stretch
alone, inside the 10-minute bound). And a job that finds its device not
present (the helper's `not-present`; on NT `ERROR_NO_SUCH_DEVINST` from
the call) after a parent's job earlier in the run waits up to 15 seconds
for the device to come back, settles, and is made once more; only then is
the device "not present" (a step) or the call a failure. A device that is
really gone costs that one wait and no more. A wait there that fails or is
cancelled stops the run as the barrier's does. The controllers' pass needs
no wait of its own (one controller's job does not touch another, and the
next pass begins with the barrier), and the later passes' devices begin
behind the barrier too. The pure half holds the three decisions
(`inst_parent_kind`, `inst_settle_after_job`, `inst_absent_retry`).

- **XP and XP x64** show Found New Hardware for each, and the unsigned
  question; Windows 2000 can too (the leg above). The settle waits while
  the wizard waits for the user, and says so; answered, the run goes on
  and finds the devices current. **A timeout, or a failed question, stops
  the run** (exit 4) with
  "finish the Found New Hardware windows (Next through Finish, never
  Cancel), then run INSTALL.BAT again": an installation in progress is not
  the tool's to compete with
  (Microsoft's "Checking for In-Progress Installations"). The second run
  finds the new devnodes installed by the wizard, or without a driver if
  it was cancelled, and installs those.
- **32-bit Vista and 7** keep the earlier release running until the
  restart the helper asks for: the new devnodes appear after it. The tool
  reports exit 3 with "restart, then run INSTALL.BAT again", and the
  second run finds them installed (Windows installs them from the staged
  package) or installs them.
- **2000, Vista x64 and 7 x64** install them at once from the staged INF;
  step 8 finds them current.

### 5.9 Results and restart

Each device a step changed is read again at the end and checked by its
role: a controller, hub, UAS disk or NHI must be bound to the group's
service; a tunnel port must keep its function driver (`pci`), carry
`xhcicm` in `UpperFilters` and name this package's filter INF in its
driver key's `InfPath`; each must have the package's `DriverVersion`, be
started and have no problem code. The devices the run did not change are
read too: one of this package's devices with a problem code is reported -
"disabled in Device Manager" for Code 22, which is no failure, and
otherwise "already had problem code N" with the steps flag set, so a
current controller already at Code 10 is never reported as a success.
A device gone after its install, or left with a problem code, is a
failure everywhere but in one case: on Windows 2000 a port's first filter
install (the unforced call) took the Thunderbolt switch's other devices
away and left the upstream port at Code 12 until the restart, all started
after it (the 2000 leg, 2026-10-15). The owner's decision (2026-10-15):
the filter goes on both the upstream and the downstream ports, and the
run asks for that restart. So when such a call succeeded in the run, a
device of the connection manager's INFs that is gone, or at Code 12, is
reported as waiting for the restart - not a failure - and the tool says
once that the restart brings them back and to run INSTALL.BAT again after
it: the restart flag is set, and the run ends with the restart line and
exit 3 unless another device set the failed or steps flag (section 7.4's
order). The same holds for such a device this run did not change. Any
other problem code stays what it is elsewhere: on a device the run
changed a failure (exit 2, with the restart advice when the hint was
given), on one it did not change "already had problem code N" (steps,
exit 4). The release notes carry
the same reading for a filter installed by hand from `nt\`.
Anything else is reported per device:

| After | Meaning | Exit |
|---|---|---|
| problem 14 (`CM_PROB_NEED_RESTART`), or the routine's `bRebootRequired` | restart needed | 3 |
| 32-bit Vista and 7 after the first `xhcihlpr.sys` install: Code 28 (7) or Code 1 (Vista) with a restart asked for | restart needed (release notes, "Installing", step 3) | 3 |
| Vista x64 and 7 x64, Code 52 or 39, test-signing not confirmed (section 5.10) | steps for the user: test-signing mode or F8 | 4 |
| any other problem code, a missing root hub under a started controller, or the old version | failed | 2 |

The flags are kept apart - failed, restart, steps for the user - and the
exit code is derived at the end (section 7.4); a later failure never
erases an earlier restart. The results print each device's state from one
rule: a problem code is printed as such and never beside "started", and
an empty field is never printed (the 2000 leg read "no driver, started,
problem code 28" and "(problem 28, )"). A registry value written to a running device
(section 8.4) adds "restart, or disable and enable the device". The tool
never restarts the machine; it says so last.

### 5.10 Signatures and test-signing mode

On Vista x64 and 7 x64 the x64 driver loads only in test-signing mode or
on an F8 start with signature enforcement disabled (AGENTS.md; design
record 19). The tool reads
`HKLM\SYSTEM\CurrentControlSet\Control\SystemStartOptions`, the boot
options the loader was started with, and looks for the word
`TESTSIGNING`. Present: "test-signing mode is on". Absent or unreadable:
"test-signing mode could not be confirmed" - not a definite negative,
since the value is a reading and not a documented contract, and an F8
start does not set it - followed by the instructions of the package's
`readme.txt` section "WINDOWS VISTA X64 AND WINDOWS 7 X64: TEST-SIGNING
MODE", which it names by that exact heading (`bcdedit /set testsigning on`
and a restart, or F8 at every start), and the note that otherwise the
controller shows Code 52 on 7 or Code 39 on Vista. The note calls the
drivers test-signed only when they are: the tool reads the x64
`xhci98.sys`'s security directory - inside the optional header, with a
size, pointing to a certificate table inside the file (structure only, no
cryptographic check). A package built without the project's key - a
clone's - carries none, and the note then says so and that only an F8
start loads it (test-signing mode is not enough); a directory that cannot
be read, or points outside the file, gives a note that names both cases.
In either case test-signing mode never counts as confirmed.
The run goes on if the user confirms. The tool never runs
`bcdedit`. What the value holds in test-signing mode and on an F8 start is
read on the legs (section 12).

Revision 13 (section 4.5): in a run without the USB drivers the signature
read is the x64 `xhcicm.sys`'s and the note names the connection manager
(its device, the NHI, is then the one that shows the code), and on
Windows 8 and 8.1 x64 the note adds that test-signing mode can be turned
on only with Secure Boot off in the computer's firmware setup, and that
the other way is Advanced startup's Startup Settings, "Disable driver
signature enforcement", at every start. The same reading of
`SystemStartOptions` is made; it is unread on 8 and 8.1.

Revision 28 (roadmap-usb4 task 43.36; the owner, 2026-10-20: test-signing,
documented by the installer, Secure Boot off, no Microsoft attestation
for now): on Windows 10 x64 the note is 8 and 8.1 x64's, in whichever
run, the USB drivers' or the connection manager's alone - the x64
drivers load only in test-signing mode, which 10 lets a user turn on
only with Secure Boot off in the computer's firmware setup, or on a start
with driver signature enforcement disabled (Advanced startup,
Troubleshoot, Startup Settings, at every start); the tool never runs
`bcdedit` and never changes a firmware setting. From 10 1607 with Secure
Boot on, only drivers signed through Microsoft's portal load, cross-signed
ones excepted on a machine upgraded from an earlier release or with a
certificate issued before 2015-07-29 (Microsoft's "Driver Signing Policy"
page, read 2026-10-20; roadmap-usb4 task 43.33 (d)); the project's test
signature is none of these, so the project's test signature is not enough there whatever the
boot options say: Secure Boot off is the condition, as the note says.
The note's sentence names 10 beside 8 and 8.1, and the test-signing
check after the install (a controller or NHI at Code 52 or 39 with
test-signing mode not confirmed) covers 10 as it covers 6.x. Two parts
are not decided here: whether 32-bit 10 with Secure Boot on refuses the
unsigned x86 drivers (43.33 (d); 43.22 reads the same for 8 and 8.1, and
if it does the x86 note 43.22 gives 8 and 8.1 covers 10 too), and
whether memory integrity (hypervisor-protected code integrity) refuses
these drivers when on, which the note names only if 43.33 (d) finds it
does. Whether the tool reads Secure Boot's state to say which case
applies is record 26 section 5.3.2's design.

Revision 30 (roadmap-usb4 task 43.22, 2026-10-22; `runs/run-43.md`,
"43.22"). Read on a Windows 8 Pro x64 guest (QEMU, SeaBIOS, so no Secure
Boot; test-signing mode on; the emulator's Thunderbolt 3 NHI) with the
published `3.1.0.0` package and its `INSTALL /console /cmonly /yes`, two
facts revision 13 did not have, both **runtime** (Windows' own setup log
and service configuration, read in the guest):

1. **No catalog, no install.** In test-signing mode `SetupCopyOEMInf`
   refused `xhcicm.inf` with `0xE000022F` (`setupapi.dev.log`: "Driver
   package does not contain a catalog file, and Code Integrity is in Test
   Signing mode"), and the run ended "No changes were made", exit 2. On
   Vista and 7 the same package installs after Windows' "can't verify the
   publisher" prompt (record 19 section 7); Windows 8 x64 accepts a package
   with no catalog only on a start with driver signature enforcement
   disabled (Advanced startup, Troubleshoot, Advanced options, Startup
   Settings, Restart, 7), where the same run showed the prompt for each of
   the two INFs ("Driver package does not contain a catalog file, but user
   wants to install anyway") and ended with the NHI started on `xhcicm`,
   the tunnel port filtered and `xhcimark` running, exit 3.
2. **The connection manager becomes a boot driver.** Installing
   `xhcicmf.inf` on the bridge, whose function driver `pci` is boot start,
   made the setup engine rewrite `xhcicm`'s start type
   (`setupapi.dev.log`: "Updated service 'xhcicm' to be boot start"; `sc
   qc xhcicm`: `BOOT_START`, the INF's `StartType=3` notwithstanding). The
   next start, with test-signing mode turned off, did not reach the desktop:
   Windows went to "Attempting repairs" (Automatic Repair) and stayed
   there through two launches of the guest, over an hour; the way back
   from there (a start with enforcement disabled) is unread. 32-bit 8 and
   64-bit 8.1 made `xhcicm` boot start the same way (8.1 x64 then started in
   test-signing mode with the NHI on `xhcicm`).

So on 8 and 8.1 x64 the tool now, in place of revision 13's note:

- **refuses the connection manager** - in a run without the USB drivers
  and in one with them alike - unless the x64 `xhcicm.sys` carries a
  signature and `SystemStartOptions` holds `TESTSIGNING`
  (`inst_cm_boot_gate`, pure, host-tested): "REFUSED: on this 64-bit
  Windows the connection manager is installed only in test-signing mode,
  from a test-signed package ...", exit 1, nothing changed. An install
  outside test-signing mode, the case revision 13 let through with a note,
  leaves a machine that stops in Automatic Repair at its next start.
  `SystemStartOptions` on a start with enforcement disabled and test-signing
  on reads `TESTSIGNING NOEXECUTE=OPTIN DISABLE_INTEGRITY_CHECKS`, so the
  install route of item 1 passes the gate;
- when it goes on, **warns** that the connection manager is a boot driver
  there: keep test-signing mode on and Secure Boot off while it is
  installed, and from Automatic Repair the way back is a start with
  enforcement disabled, then test-signing mode on again or the uninstall;
- after a staging refused with `0xE000022F` on 8 or 8.1 x64, **says what
  it means** (`inst_stage_error_text`). Revision 30's text named item 1's
  route for the catalog-less package of the time: keep test-signing mode
  on, start once with enforcement disabled, run the installer again.
  Since revision 33 the package carries a catalog per INF, so revision
  34's text says that Windows found no signed catalog for the INF and
  asks for the complete package, with its `.cat` files, to be restored
  and the installer run again; the start with enforcement disabled is
  gone from it, since the tool installs its own package, which carries
  the catalogs.

The three decisions (`inst_cm_boot_gate`, `inst_stage_error_text`,
`inst_sig_note_kind`) key on the kind `inst_os_kind` gives
(`INST_OS_CMONLY`, the connection-manager-alone systems of section 4.3),
not on the version, and their texts name no version ("this Windows"): a
system admitted to that kind later - Windows 10 below build 22000, tasks
43.34 to 43.36 - takes the gate, the catalog text and the 32-bit Secure
Boot note with it. Whether 10 makes `xhcicm` boot start and refuses a
package with no catalog in test-signing mode was unread here (the same
setup engine and code integrity, so likely); 10 x64's refusal of the
catalog-less package was read since (section 5.12), and whether 10 makes
`xhcicm` boot start is still unread.

The installer still signs nothing and changes no boot setting (D6). A
catalog would remove item 1's detour (a test-signed catalog is accepted
in test-signing mode); it is design record 19's decision not to ship one,
so it is a question for the owner, not a change made here (the
owner's answer: revision 33 below). Item 2 is not
cured by a catalog: a boot-start driver must load at every start, so on 8
and 8.1 x64 the connection manager needs test-signing mode for as long as
it is installed, whatever the package carries.

**32-bit Windows 8 and 8.1** (revision 30): Microsoft's "Driver Signing
Policy" page (learn.microsoft.com, `kernel-mode-code-signing-policy--windows-vista-and-later-`,
`ms.date` 2024-08-19, read 2026-10-22) gives, under "Signing requirements
by version", the column "Windows 8, Windows 8.1, Windows 10, versions
1507, 1511 with Secure Boot on" the architectures "64-bit, 32-bit", and the
column "Windows Vista, Windows 7; Windows 8+ with Secure Boot off" "64-bit
only, no signature required for 32-bit". The x86 drivers are never signed
(AGENTS.md), so with Secure Boot on a 32-bit 8 or 8.1 loads none of them,
and test-signing mode does not help an unsigned binary. The note of
section 5.10 therefore covers the x86 package there: on 32-bit 8 and 8.1
the tool prints the Secure Boot note (`inst_sig_note_kind`,
`inst_sig_x86_sb_text`) - Secure Boot on refuses the unsigned drivers;
turn it off, or start with enforcement disabled at every start; a computer
that starts from a legacy BIOS has no Secure Boot - and after the install
a device at Code 52 is reported as refused for that reason. Secure Boot on
itself is unread here: every guest of this project starts from SeaBIOS.
The same table names 10 1507 and 1511 in that column, and from 10 1607
with Secure Boot on only portal-signed drivers load (revision 28 above),
so 32-bit 10 below build 22000, of the same kind, gets the same note: the
part revision 28 left to 43.33 (d) and to this reading.

**Secure Boot's state** (revision 30; owner, 2026-10-22, accepting record
26 section 5.3.2 as `p43-34` designed it). On the connection-manager-alone
kind of system (`inst_sb_applies`: 8, 8.1 and 10 below build 22000) the
tool reads `HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot\State`,
`UEFISecureBootEnabled`, logs it, and shows it in the note; it never
changes it. `inst_sb_state`: a `REG_DWORD` 1 is on, 0 off, anything else
or nothing not known. On x64 (`inst_sb_text`) on says the test-signed
drivers do not load and test-signing mode cannot be turned on until
Secure Boot is turned off in the firmware setup; off says test-signing
mode can be turned on (`bcdedit`); not known adds nothing to the note. On
32-bit 8 and 8.1 on and off replace the Secure Boot note above by the
case that applies (refused until it is turned off; the drivers load as on
7), not known keeps the note naming both. With Secure Boot read as on,
`TESTSIGNING` in `SystemStartOptions` never counts as confirmed
(`inst_ts_confirmed`), so on 8 and 8.1 x64 the gate above refuses the
connection manager with the steps that begin with turning Secure Boot
off, and after an install a Code 52 is reported as the note's case.
Host vectors: each state on each system, and the state through the gate.
The value is unread on any guest here (all SeaBIOS, where it is absent or
0, both of which read as not known or off), and its reading with Secure
Boot on is owed: task 43.32's OVMF guest was not built. The refusal with
Secure Boot on does not wait on that reading: since revision 33 it is
taken from Microsoft's documented policy (section 5.12, "Settled").

Revision 33 (the owner, 2026-10-22, on section 5.12's measured refusal):
from `3.2.0.0` every INF of the x64 package names a catalog of its own
(`xhci98.cat`, `xhciuas.cat`, `xhcicm.cat`, `xhcicmf.cat`, `stor98.cat`),
made and signed with the project test certificate by the packager alone
(design record 19 section 3a), because Windows 8, 8.1 and 10 x64 refuse a
package with no catalog in test-signing mode, with `0xE000022F` and no
prompt. The tool needs no change of route for it: its local copy takes
every file of the flavour directory but those it leaves out by component,
so each catalog lands beside its INF, and the setup engine imports it
with the INF into the driver store. The connection manager's two catalogs
are left out with its INFs on a No to the Thunderbolt question, and
`stor98.cat` with Stor98's files when it is not ticked (its name starts
with `stor98`, `inst_bundled_file`). The x86 package carries none. The
note's text is unchanged: test-signing mode, Secure Boot off.

### 5.11 `XHCICLN` stays the user's (owner, 2026-10-15)

The tool never runs `XHCICLN`: no question, no switch, no call path.
`XHCICLN` stays a separate tool the user runs by hand (record 13 section
10.13.8). Whenever `XHCI98\` devnodes exist (the walk of section 5.3 sees
them) - left by an update from `2.3.x` or earlier - the tool prints one
line last: nothing needs them removed, and `tools\xhcicln\` lists and
removes them if the user wants. The line changes no exit code. The
uninstall (section 9) does not run `XHCICLN` either: it removes the old
bus's devnodes itself, through the same `DIF_REMOVE` route, as part of
its set.

### 5.12 Windows 10: the signature policy and the version (task 43.33 (d) and (e), 2026-10-22; a reading)

A reading for roadmap-usb4 task 43.33 (d) and (e), before 43.34 to 43.36
write the Windows 10 route; no code changed. Labels as design record 13
section 6.14's: **measured** (in a Windows 10 22H2 guest of task 43.32,
10.0.19045.2965, legacy BIOS, so no Secure Boot), **static**,
**documentation** (Microsoft's pages, read 2026-10-22) and
**inference**.

**The signature policy (documentation).** Microsoft's "Driver Signing
Policy" page (learn.microsoft.com, "Kernel-mode code signing policy
(Windows Vista and later)", ms.date 2024-08-19):

- "Starting with Windows 10, version 1607, Windows will not load any new
  kernel-mode drivers which are not signed by the Dev Portal", with
  cross-signed drivers still permitted where "The PC was upgraded from an
  earlier release of Windows", "Secure Boot is off in the BIOS", or the
  driver was signed "with an end-entity certificate issued prior to July
  29th 2015". The page on the deprecated certificates (ms.date
  2025-07-11): "Most cross-certificates expired in July 2021" and "All
  cross-signed trusted root certificates are now expired", so no new
  cross-signature can be made.
- Its table's "Architectures" row: Vista, 7, and "Windows 8+ with Secure
  Boot off": "64-bit only, no signature required for 32-bit"; every
  Windows 8, 8.1 and 10 column "with Secure Boot on": "64-bit, 32-bit".
  **So a 32-bit Windows 10 with Secure Boot on enforces kernel-mode
  signatures as 64-bit does, and with Secure Boot off requires none.**
  The same reading answers 43.22's question for 8 and 8.1 x86 (the page
  puts 8, 8.1 and 10 1507 and 1511 in one column).
- Test-signing mode: "The TESTSIGNING boot configuration option" page
  (ms.date 2025-04-16) answers a `bcdedit /set testsigning on` refused
  with "The value is protected by Secure Boot policy" by "disable Secure
  Boot", as on 8 (section 5.10's revision 13 note).
- **Memory integrity** (hypervisor-protected code integrity): the
  Windows hypervisor "is only supported on 64-bit IA processors with
  virtualization extensions" (OEM VBS page), so **32-bit 10 has none**
  (inference from that page). On 64-bit it refuses drivers that are not
  compatible - "Don't use sections that are both writable and
  executable", executable pool, a section alignment not a multiple of
  0x1000 (HLK "Driver compatibility with memory integrity" page, which
  adds that an `INIT` section marked writable and executable is
  tolerated: "In Windows 10, however, the W will automatically be
  stripped"); and "if you have Memory Integrity / HVCI ... enabled, you
  must test-sign the binary ... An unsigned binary isn't supported" (the
  TESTSIGNING page). Static, this project's own `3.1.0.0` x64 binaries
  (`xhci98.sys`, `xhcicm.sys`, `xhciuas.sys`, `stor98.sys`): section
  alignment 0x1000; `.text` executable and not writable; `INIT` writable
  and executable (the tolerated case); every pool allocation through
  `ExAllocatePoolWithTag` with a pool type the code chooses, which the
  compatibility test flags where it is the executable `NonPagedPool`.
  Whether 10 x64 with memory integrity on loads them is unread (no guest
  here runs the hypervisor under TCG); since memory integrity is turned
  on only beside Secure Boot on the machines that ship it (OEM VBS page:
  "Secure Boot must be enabled on devices leveraging VBS"), and the x64
  drivers need test-signing mode, which needs Secure Boot off, the case
  arises only on a machine where the user turned memory integrity on by
  hand with Secure Boot off.

**What the guests read (measured, 2026-10-22).** The installer's NT
route by hand with the published `3.1.0.0` package (record 13 section
6.14.5):

- 10 x64 in test-signing mode: `UpdateDriverForPlugAndPlayDevicesW`
  with `INSTALLFLAG_FORCE` returns `0xE000022F`; the setup log says
  "Driver package does not contain a catalog file, and Code Integrity is
  in Test Signing mode" and the package is never staged. Vista and 7 x64
  prompted for the same package (record 19). **So on 10 x64 the package
  needs a catalog signed with the test certificate**; the section 5.10
  test-signing note alone is not enough.
- 10 x86 (no Secure Boot): the same call shows "Windows can't verify the
  publisher of this driver software", and "Install this driver software
  anyway" goes on: the package is staged unsigned ("Signer Score =
  0x80000000"), a restart is asked, and after it `xhci98.sys` runs the
  controller (record 13 section 6.14.5). So 10 x86 without Secure Boot
  takes the package as Vista and 7 x86 did, with the prompt; with Secure
  Boot on it would refuse the unsigned x86 drivers by the policy above
  (unread: no 32-bit UEFI guest here).

**The version (documentation; static on the guests).** Windows 10 and
Windows 11 both report NT 10.0, and `GetVersionEx` reports 6.2 to an
application not manifested for 8.1 or 10 ("Operating system version"
page); `RtlGetVersion` is the kernel's and is not shimmed (inference from
its page: "the kernel-mode equivalent of the user-mode GetVersionEx"),
which is why 43.34 tells 10 from 11 by `dwBuildNumber` below 22000
("Version 21H2 (OS build 22000)", Windows 11 release information). The
guests' `RtlGetVersion` reads 10.0.19045 (measured, `rtlgetversion.txt`
of the run sheet's reading). The range the owner admitted (2026-10-20)
and what Microsoft's lifecycle pages say of it:

| Release | Build | End of support |
|---|---|---|
| 10 1507 (and 2015 LTSB) | 10240 | LTSB 2025-10-14 |
| 10 1607 (2016 LTSB), Server 2016 | 14393 | LTSB 2026-10-13; Server 2027-01-12 |
| 10 1809 (2019 LTSC), Server 2019 | 17763 | 2029-01-09 |
| 10 21H2 (2021 LTSC) | 19044 | 2027-01-12 (IoT Enterprise 2032-01-13) |
| 10 22H2, the last release of 10 | 19045 | 2025-10-14 ("will be the final version of Windows 10") |
| Server 2022 | 20348 | 2031-10-14 |
| 11 21H2 | 22000 | refused (43.34) |

Only 22H2 is read here, and the release notes say so (43.36).

**Settled by the owner, 2026-10-22** (revision 33):

- **The catalog.** The x64 package carries a catalog per INF signed with
  the test certificate (design record 19 section 3a; section 5.10's
  revision 33). A package without one is refused on 10 x64 as measured
  above (`0xE000022F`, "Driver package does not contain a catalog file");
  with one, the package installs in test-signing mode with Secure Boot
  off, after the publisher prompt (measured on the 10 x64 and 8.1 x64
  guests with the packager's catalogs signed by a guest-made throwaway
  certificate, run sheet 43, "43.36 the x64 catalog"; the owner-signed
  package's leg is owed). The x86 package stays without one: 10 x86 takes it with the
  prompt (measured above).
- **Secure Boot on.** The refusal of a test-signed driver with Secure
  Boot on is taken from Microsoft's documented policy above ("Driver
  Signing Policy"; "The TESTSIGNING boot configuration option": test-signing
  cannot be turned on while Secure Boot is on) rather than read on a guest:
  the OVMF guest with Microsoft's keys enrolled (43.32's last clause) is
  not built, and 43.32 is closed on that ground. 43.37's "on the OVMF
  guest with Secure Boot on the note's text and the load refused as it
  says" is therefore a documentation reading, not a leg.

## 6. The Windows 98 SE and ME route

### 6.1 What exists on these systems, and what is not used

- **No routine installs a driver on a device.** `UpdateDriverForPlugAndPlayDevices`
  is NT's `newdev.dll`; Windows 98's device installation is the 16-bit
  `SETUPX` engine behind the hardware wizards, whose interface this
  project does not drive (the owner's rule: nothing undocumented).
- **`SetupCopyOEMInf`.** The Windows 98 DDK's `setupapi.h` declares it, so
  the Win32 `setupapi.dll` of those systems may export it; but that DLL is
  not the device installer, and nothing documented says the 16-bit engine
  reads what it copies. Not used. The tool reports whether the routine
  resolves on each system, for the record (section 12).
- **`INF\OTHER`** is where the 16-bit engine itself keeps an OEM INF it
  has installed from, and records its original name under
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Setup\SetupX\INF\OEM Name`
  (`REGSTR_PATH_INF_OEMNAME`, the Windows 98 DDK's `regstr.h`). Writing
  into either by hand is driving the engine's own bookkeeping. Not used
  to install. (Revision 25) The one exception is the 98 SE storage row's
  (record 26 section 6.5.11, the owner's decision of 2026-10-20): NUSB
  3.3's `USBSTOR.INF` and `USBNTMAP.INF` placed in `%windir%\INF` itself
  under their own names, never over a file there and never in
  `INF\OTHER`, as NUSB's own installer places them, with nothing written
  to the engine's index or its `OEM Name` key. (Revision 20) The uninstall that deletes one of this
  package's copies there (record 26 section 9.3 step 4, Stor98's owned
  copies too) deletes the `OEM Name` value named by that copy's path, or
  by its short alias, and no other value (`inst_oem_name_ours`): read on
  the 98 SE and ME uninstall legs of 2026-10-19, which left
  `C:\WINDOWS\INF\OTHER\Yeo Kheng Mengxhci98.inf` = `xhci98.inf` naming
  a deleted file.
- **`DevicePath`** is the documented knob:
  `HKLM\Software\Microsoft\Windows\CurrentVersion`, value `DevicePath`,
  "default search path for .INFs" (`REGSTR_VAL_DEVICEPATH`, the Windows 98
  DDK's `regstr.h`), a `;`-separated list of directories the device
  installer searches for INFs. It is the route the tool takes. The header's
  comment establishes the value's meaning, not how the wizards search it,
  cache it or take files from it: those are readings (section 6.2 item 3).

### 6.2 The route

1. The local copy (section 4.4): the install directory itself (section
   9.6; a `<hash>` directory under `%windir%\XHCI98\` before revision
   10), which holds the INFs; no recursive search is assumed. On 98 and ME it never takes the `nt\`
   subdirectory nor a filter INF wherever it lies, so the wizards cannot
   offer the filter for a bridge (39-0.4).
2. `DevicePath` names that directory: every entry under
   `%windir%\XHCI98\` (an earlier build's copy, which the tool itself
   added), the install directory and the one an earlier install recorded,
   and every `<hash>` copy directly in either (`<dir>\<eight hex digits>`,
   `inst_path_remove_copies`: another entry under a directory the user
   chose is kept) is taken out of the list and the install directory
   appended, compared without case; no other entry is reordered or
   removed, and the value keeps its type. So the wizards search this
   release's copy alone. The earlier layouts' `<hash>` copies are deleted
   once `DevicePath` no longer names them (`hashed_cleanup`; in a
   directory the user chose, only a `<hash>` directory holding this tool's
   list).
3. The device installer then finds the package's INFs in a directory it
   searches by itself. **Read on 98 SE** (2026-10-15, `runs/run-40.md`):
   after `DevicePath` named the copy, the Add New Hardware wizard that
   came back at the next start already named the device "xHCI98 USB 3.x
   eXtensible Host Controller", and "Search for the best driver" with no
   box ticked offered the copy's `XHCI98.INF` (by its short path) - the
   search works with no typing. **The files do not come from there**: the
   engine then asked for the "xhci98 xHCI Host Controller Disk" with
   "Copy files from" set to the Windows source path (`D:\WIN98` on the
   guest), not the INF's directory, and took the files once the copy's
   directory was typed into that box; then it asked for the Windows CD for
   `usbd.sys`, as a Have Disk install does. So the printed steps name that
   question and the folder to type, which is the install directory, whose
   every part is an 8.3 name with no spaces (section 9.6), typed as
   printed. A wizard cancelled
   before the tool ran does not come back when Device Manager is
   refreshed; it comes back at the next start. ME is read on its own leg.
4. The OS's own files the INF asks for through `LayoutFile=layout.inf`
   (`usbd.sys`, `usbui.dll`; and `hidclass.sys` for the first HID device)
   come from the Windows CD as today; the tool says so.

### 6.3 Identifying the devices

The tree comes from `cfgmgr32.dll` (`CM_Locate_DevNode`, `CM_Get_Child`,
`CM_Get_Sibling`, `CM_Get_Device_ID`, `CM_Get_DevNode_Status`,
`CM_Get_DevNode_Registry_Property` for the hardware and compatible ids,
the driver key name and the description), the source `XHCISNAP` reads on
these systems (`xhcisnap\README.md`, "Where the device readings come
from"). A WDM driver is bound on 98 and ME by its driver key's
`NTMPDriver` value, not by a `Service` value: a devnode belongs to a group
of section 5.4 when that value names the group's `.sys` (`xhci98.sys`,
`xhciuas.sys`, `xhcicm.sys`), its hardware and compatible ids are read
from its `HKLM\Enum\<device id>` key, where 98 SE keeps each list as one
comma-separated string (read on the guest; the configuration manager's
answer did not match there), and its version is the driver key's
`DriverVersion` where the install wrote one; where it did not, the group's
installed binary's file version stands for it (section 5.4: one binary
per service), and the `DriverDate` is only shown. Neither readable: the
device is unknown and only listed. A failed walk refuses the run. A
controller whose New Hardware wizard was cancelled has no devnode until
the next start, only its `HKLM\Enum\PCI` entry (read on 98 SE: nothing in
`HKEY_DYN_DATA\Config Manager\Enum`), so an `Enum\PCI` entry of an xHCI
controller with no driver and no devnode is listed apart, as "recorded, not
running now", with what to do if Device Manager shows it. An old entry of
a controller that moved slot looks the same and needs nothing (read on the
98 SE and ME legs: the controller moved from `DEV_04` to `DEV_03` after the
first start, and every later run planned the old entry as an install). A
hardware id cannot tell that old slot from a second, identical controller
whose wizard was cancelled, so (orchestrator decision 2026-10-15, owner to
confirm) such an entry - one whose first hardware id a present controller
with a driver carries - stays listed with what to do in either case and
stays a step (exit 4), but does not make the confirmation speak of an
install; the run on an otherwise current machine changes nothing and says
"No changes were made."

### 6.4 What it prints

**The form** (owner, 2026-10-16, revision 10): the tool cannot put the
driver on the device here, so the hand step is made as easy as it can be.
"What to do now" names the folder to type once, then gives each device
that needs a step - the controller, its root hub on an update, a
Thunderbolt controller when its INF was copied, a UAS disk - a short
numbered list, one action a line, each line at most 79 columns, the
folder spelled out where it is typed ("Specify a location", "Copy files
from", Have Disk). The facts below are unchanged; only their form is.
The list ends with "run INSTALL.BAT again" to check the result and set
the options. Then, when at least one device needs Update Driver or Have
Disk and the run is interactive (not `/yes`, `/defaults`, `/list` or
`/nopause`), the tool asks "Open Device Manager now?", Y and N on lines
of their own, "Enter for [Y]"; Y starts `control.exe sysdm.cpl,,1`
(System Properties at its Device Manager tab) with `CreateProcessA` and
does not wait for it.

For each xHCI controller (`PCI\CC_0C0330` among its ids):

- **no driver**: Device Manager, the controller under "Other devices",
  Properties, Driver, Update Driver, Next, "Search for the best driver",
  Next with no box ticked, Next, Finish; if the wizard finds nothing,
  "Specify a location" and the local directory; Windows 98 SE may ask for
  its CD; restart when asked, and on ME restart anyway;
- **an older `xhci98.sys`**: the same Update Driver route on "xHCI98 USB
  3.x eXtensible Host Controller", "Search for a better driver than the
  one your device is using now", no box ticked; restart afterwards
  although Windows does not ask (release notes, "Updating from 2.y.y.y");
  then the same route on "xHCI98 USB 3.x Root Hub", listed as a step of
  its own (the 9x leg, 2026-10-15: without it the root hub stayed at the
  old release); and, only when the installed `xhci98.sys` is from `2.3.x`
  or earlier and only on 98 SE, the keyboard-and-mouse paragraph of "From
  2.3.x or earlier", printed before the confirmation as on NT (section
  5.8) and as a line of the controller's step;
- **current**: nothing;
- **another vendor's driver**: left (decision D3).

An `Enum\PCI` entry of a controller with no driver and no devnode is
listed as "recorded, but not running now", with both readings it can have:
a cancelled wizard's controller (section 6.3) to install, or an old slot's
entry (the 9x leg, 2026-10-15: the controller moved from `DEV_04` to
`DEV_03` after the first start) that needs nothing. The tool does not
guess between them: two identical controllers share a hardware id, so the
id alone proves neither.

For each UAS device and NHI present with no driver, the install route of
the first item. For each one already bound to an older release of this
package, the same Update Driver route on its entry; and because a search
keeps an `xhciuas.sys` with the same `DriverVer` (release notes, "Updating
xhciuas.sys over an older one"), the Have Disk route for an older UAS
disk, and for any current device when `/force` asks for a reinstall:
"Display a list of all the drivers in a specific location", Have Disk,
the local directory. The storage prerequisite stays as the README says: on 98 SE,
NUSB's mass-storage component for any disk, UAS included. The first UAS
disk and the first NHI found later find `xhciuas.inf` and `xhcicm.inf` in
the same directory. A device of the package that needs a step - an
install, an update, a refused version, an entry of section 6.3 that may be
a cancelled wizard's controller - ends the run with exit 4 (staged, steps
left for the user); with none (every device current), the run
changes nothing where the copy and `DevicePath` are already in place and
ends "No changes were made." with exit 0, or, where it wrote a setting,
with the restart line and exit 3 (section 7.4). The confirmation names
what will happen: the copy and `DevicePath`, the changed settings, or a
check of the copy and `DevicePath`; with an entry of section 6.3 left as
a step, it claims neither that every device is current nor that none
needs a driver, and points at the entry listed apart. The steps also say, for a UAS disk's
Have Disk, that Windows asks for the "xhci98 xHCI Host Controller Disk"
(the same folder), and on ME, for the root hub, that a "Select Other
Driver" list may name the driver twice (one copy under `INF\OTHER`),
either being the same driver (both read on the legs).

(Revision 24) On 98 SE with the USB storage drivers' row ticked, the run
copies `USBNTMAP.SYS` itself before any device job, so the step below
is printed only where the row is not ticked or its case copies nothing;
a UAS entry already at Code 2 then starts once Windows is shut down and
started again, which the final line says (section 7.4, record 26
section 6.5.5).

(Revision 25, record 26 section 7) The storage prerequisite's sentence
after the steps ("On Windows 98 SE any disk, UAS included, needs ...")
is printed only on 98 SE and only where the storage drivers are not
taken care of on this machine (the run put the seven in place, or found
all of them installed), and names the installer's row beside NUSB's
component. Where the reading finds no xHCI controller at all, "No device
of this package is present" is followed on 98 SE and ME by the steps
for a controller whose New Hardware wizard was cancelled at this start:
restart, finish the wizard (the install directory where it asks for the
xHCI98 driver disk, the Windows CD or its setup files' folder where it
asks for a Windows file), run the installer again.

A UAS entry at Code 2 with no `USBNTMAP.SYS` in `System32\Drivers` (the
upper filter `xhciuas.inf` names) gets a step of its own, in the plan and
in the steps, in place of "Nothing" when it is current: on ME, plug in any
ordinary stick once (ME copies its own storage files from its cabinets
when its first Bulk-Only device installs) and then unplug and replug the
disk; on 98 SE, NUSB's mass-storage component, the disk starting at the
next boot (release notes, "Known limitations", the ME entry, and
"Windows 98 SE and ME: the stock install, and storage"). Code 2 with the
file present has another cause and gets no such step. On ME with no
`USBNTMAP.SYS` yet, the steps end with the same advice before any UAS
disk is plugged in (the ME leg of 2026-10-15: a UAS disk as the first
storage device installed silently from the staged INF and sat at Code 2,
through a restart, as the release notes say). The run ends with exit 4: staged, steps left for the
user.

### 6.5 After the restart

Running `INSTALL.BAT` again after the restart reports each device as
section 6.4 sees it and asks the registry questions for the controllers
and NHIs now bound to this package (section 8). It does nothing else on
these systems.

## 7. Modes, switches, rights, the routines, exit codes, the log

### 7.1 Switches

| Switch | Meaning |
|---|---|
| (none) | install or update, with the plan shown and one confirmation |
| `/list` | the plan, the store trap, the questions that would be asked and their current values, and with `/uninstall` the uninstall plan; reads only: no confirmation, no question, no local copy, no staging, no log unless `/log` names one, no pause; exit 0 (or 1 on a refusal) |
| `/yes` | no confirmation, no registry question (every value `/set` does not name is kept), and with `/uninstall` no typed word (section 9.1) |
| `/defaults` | ask no registry question, keep every current value (the INF's default on a new install) |
| `/set Name=Value` | answer one registry question, repeatable; validated against the value table and the system (section 8.1); applies even where the question would not be asked |
| `/force` | reinstall a current device, and install over a newer group (section 5.4) |
| `/thunderbolt` | include the Thunderbolt 3 / USB4 support without asking (section 8.5) |
| `/cmonly` | (revision 13) the connection manager alone: the USB drivers left out of the run, the Thunderbolt support in it unasked (section 4.5); refused on 98 SE and ME, with `/uninstall`, `/devices` and `/eject`, and where the package carries no connection manager; on 8 and 8.1 every install run is one |
| `/stor98` | include the bundled Stor98 without asking (record 26 section 6.4); refused without `/thunderbolt` or `/cmonly`, since Stor98 is part of the Thunderbolt 3 / USB4 support (owner, 2026-10-19), and with `/uninstall`, `/devices` and `/eject`; never offered on 8 and 8.1. Since revision 23 it is what `/thunderbolt /yes` does anyway |
| `/nostor98` | (revision 23) leave the bundled Stor98 out of a run that has the Thunderbolt support. The console follows the window: Stor98 is chosen with the Thunderbolt support (owner, 2026-10-19), so `/thunderbolt` alone implies Stor98 - asked with Enter for Y, unasked under `/yes` - unless `/nostor98` is given; `/thunderbolt` itself stays explicit (section 8.5). Refused with `/stor98`, `/uninstall`, `/devices` and `/eject` |
| `/to <path>` | the install directory (section 9.6) without asking; refused with `/uninstall`, `/devices` and `/eject`, and checked as a typed answer is (a path that fails refuses the run, exit 1) |
| `/nouas` | leave the UAS disk driver out of the run (owner, 2026-10-19; record 26 section 5.3): `xhciuas.inf` neither staged nor installed, nor copied to `DevicePath` on 98 SE and ME, and an `xhciuas` already installed left as it is; refused with `/uninstall`, `/devices` and `/eject` |
| `/storage`, `/nostorage` | (revision 24) Windows 98 SE: install the USB storage drivers of record 26 section 6.5 without asking, or leave them out; `/storage` refused on any other system, where the download does not carry them or carries other bytes, and with `/nostorage`; both refused with `/uninstall`, `/devices` and `/eject`; `/nostorage` changes nothing on another system |
| `/usb` | (revision 26, proposal) Windows 8 and 8.1: put the USB drivers in the run, which there are otherwise left out, with record 26 section 5.3.1 case 3a's warning; elsewhere it changes nothing; refused with `/cmonly`, `/uninstall`, `/devices` and `/eject`. Revision 26 also lifts `/cmonly`'s refusal on 98 SE and ME (case 2's warning), `/storage`'s on any system but 98 SE (case 1: the copy to the tools folder), and `/stor98`'s "never offered on 8 and 8.1" (case 3b), each warning printed before the go-ahead question and printed and logged under `/yes` |
| `/notray` | install without the Run value that starts the xHCI98 USB Tree at login, and delete one an earlier run wrote (section 9.5); refused with `/uninstall`, `/devices` and `/eject` |
| `/s98vsd`, `/nos98vsd` | (revision 36) preselect `S98VSD.VXD`, Stor98's large-transfer layer on 98 SE and ME, ticked or unticked (design record 30 section 7.2); without either it follows Stor98 (ticked whenever Stor98 is, the owner, 2026-10-22), and the console asks after Stor98's question, Enter and `/yes` giving Y. `/s98vsd` needs Stor98 in the run (`/stor98`, or `/thunderbolt` or `/cmonly` without `/nostor98`); refused together, with `/uninstall`, `/devices` and `/eject`, and both on every system but 98 SE and ME |
| `/faster`, `/nofaster` | (revision 35) the computer's choice of XHCITR's "can run faster" notice, on or off (section 9.5 item 7; record 25 section 7.7); refused together, and with `/uninstall`, `/devices` and `/eject` |
| `/restage` | Vista and 7: replace a stored package the trap holds (section 5.7) |
| `/uninstall` | section 9 |
| `/debug` | `INSTALL.BAT` only: the `debug-` flavour directory |
| `/nopause` | do not wait for Enter before closing |
| `/log path` | write the log there |
| `/devices` | the connected Thunderbolt and USB4 devices and the Windows devices behind them; reads only (design record 24) |
| `/eject [N]` | remove one of them safely (design record 24) |

An unknown switch, `/set` with an unknown name or a value outside its
allowed set, or a contradiction (`/defaults` with `/set`, `/list` with
`/yes` or `/restage`, `/uninstall` with an install switch) is
refused with exit 1 before anything is read. `/set Name Value` (two
arguments, as `COMMAND.COM` delivers it) is joined back. Every run but
`/list` waits for Enter before it exits, because Explorer and the Vista
and 7 elevation prompt open its console window and close it at exit;
`/nopause` is for scripts and the legs.

### 7.2 Rights

On Vista and 7 the manifest asks for elevation, for every run. On 2000 and
XP the tool opens the service control manager with
`SC_MANAGER_ALL_ACCESS`; a failure refuses (exit 1) with "run it as an
administrator". `/list` runs without that check and says what it could not
read, and so do `/devices` and `/eject`, which report what each call
answers (design record 24 section 7.2).

### 7.3 The routines

| Routine | DLL | Systems | Step |
|---|---|---|---|
| `UpdateDriverForPlugAndPlayDevicesA` | `newdev.dll` | 2000 onward | 5.5 |
| `SetupCopyOEMInfA` | `setupapi.dll` | 2000 onward (and resolved on 98 for the record) | 5.6 |
| `SetupUninstallOEMInfA` | `setupapi.dll` | XP onward (Microsoft's documentation), where it resolves | 5.7, 9.2 |
| `SetupGetInfDriverStoreLocationA` | `setupapi.dll` | Vista onward | 5.7 |
| `SetupDiGetClassDevsA`, `SetupDiEnumDeviceInfo`, `SetupDiGetDeviceInstanceIdA`, `SetupDiGetDeviceRegistryPropertyA`, `SetupDiSetDeviceRegistryPropertyA`, `SetupDiSetClassInstallParamsA`, `SetupDiCallClassInstaller`, `SetupDiGetDeviceInstallParamsA`, `SetupDiDestroyDeviceInfoList` | `setupapi.dll` | 2000 onward | 5.3, 9 |
| `CM_Locate_DevNodeA`, `CM_Get_Child`, `CM_Get_Sibling`, `CM_Get_Device_IDA`, `CM_Get_DevNode_Status`, `CM_Get_DevNode_Registry_PropertyA` | `cfgmgr32.dll` | all | 5.3, 6.3 |
| `CMP_WaitNoPendingInstallEvents` | `cfgmgr32.dll` (where `XHCICLN` resolves it, read on 2000 by task 38.8's legs), else `setupapi.dll` | 2000 onward | 5.1 |
| `GetFileVersionInfoSizeA`, `GetFileVersionInfoA`, `VerQueryValueA` | `version.dll` | all | 4.2, 5.4 |
| `SetupDiBuildDriverInfoList`, `SetupDiEnumDriverInfoA`, `SetupDiGetDriverInfoDetailA`, `SetupDiGetDriverInstallParamsA`, `SetupDiSetSelectedDriverA`, `SetupDiDestroyDriverInfoList`, `SetupDiSetDeviceInstallParamsA` | `setupapi.dll` | 2000 onward | 9.2 |
| `IsWow64Process` | `kernel32.dll` | XP SP2 onward, absent is not WOW64 | 3 |

Every one is looked up at run time, and the log records where each
resolved. `/list` prints which resolved, which is how the legs read them
on each system (runtime, through this tool's own call; `legal-provenance.md`
section 4 gains a row when a reading rests on one).

### 7.4 Exit codes

| Code | Meaning |
|---|---|
| 0 | done, nothing needed a change; or `/list` |
| 1 | refused before any change: location, architecture, rights, a routine missing, a bad switch, a partial device list, cancelled |
| 2 | failed: a step failed or a result was wrong (section 5.9); the log and the report say what was done |
| 3 | done, something changed: restart needed (and, where said, run `INSTALL.BAT` again after it) |
| 4 | done as far as the tool can: steps remain for the user - on Windows 98 SE and ME whenever a device needs a step (section 6.4); on NT a group refused as newer or unknown, a device left by the trap (5.7), an id shared with a foreign device (5.5), a wizard to finish (5.8), test-signing to turn on (5.9) |

Failed, restart and steps are three flags; the code is 2 when failed is
set, else 4 when steps is set, else 3 when restart is set, else 0. The
console says every flag that is set, so a 2 or a 4 that also needs a
restart says so. `INSTALL.BAT` passes the code on; `SETUP.EXE` does not
wait for the window and returns 0 once it is started (revision 22,
record 26 section 2).

**The last line before the exit code** (owner, 2026-10-15): when the run
changed anything - a driver installed, updated or removed, a filter added
or removed, an INF staged or removed, the local copy or `DevicePath`
written, a registry value written (the silent install included) - and
finished, "Changes were made. Please restart Windows now.", on every
system, whether or not Windows itself asked for a restart; when it changed
nothing (`/list`, every answer kept, every INF staged already byte for
byte, a value already as asked, a refusal) "No changes were made." The
line and the exit code agree (orchestrator decision 2026-10-15, owner to
confirm; it replaces "the exit code is not changed by it"): a finished run
that asks for the restart exits 3, so 0 means nothing needed a change. A
run that changed something but stopped with steps left (2 or 4) does not
also ask for a restart beside "run INSTALL.BAT again": it ends "Changes
were made, but not everything is done: do what is said above first, then
restart Windows.", its code unchanged (a change under a refusal's code is
reported as 2). The 2000 leg read the restart line on an unchanged rerun
and beside a stop; the rule lives in the pure half (section 10).
(Revision 24) On 98 SE the line takes record 26 section 5.9's shutdown
form ("shut down and start Windows again") also when the run copied the
USB storage drivers' `USBNTMAP.SYS` with a UAS entry already at Code 2,
which starts only after a fresh start (record 26 section 6.5.5); the
codes are unchanged. The USB storage drivers' copies count as a change,
and as a device change for the confirmation's words.

**The device modes are the one exception** (design record 24 section 8):
`/devices` changes nothing and ends "No changes were made.", exit 0; an
eject that Windows accepted and finished changes the device's state but
never needs a restart, so it ends with the eject's own last line ("You
can now unplug the device." or its qualified forms) and exit 0; a refusal
before or by the call ends "No changes were made.", exit 1; a veto the
same, exit 4; an uncertain result (a removal that failed after an accepted
query) ends with record 24's failure text, exit 2 - record 24 section
8.2's outcome table. The pure half's final-line rule takes the mode.

**The confirmation says what will happen**: "Install and stage the package
now" when a device changes; "Write the changed settings now (no device
needs a new driver)" when only option values change; otherwise that every
device is current and no setting changes, and the staged INFs are checked
(any missing staged).

### 7.5 The log

Every run but `/list` appends to `XHCIINST.LOG` at the root of the system
drive (`%SystemDrive%\` on NT, the drive of the Windows directory on 98
and ME; decision D8), or where `/log` says: a header with the tool's
version, the system's version, build and service pack, the architecture,
the package path and flavour, the switches, the local time; then every
line the console shows, where every routine resolved, every call made with
its arguments and result (`GetLastError` in hex), every registry value
written with its old and new value, and the exit code. The path is
printed first and last, and the release notes ask for the file in a field
report. A log that cannot be opened is said and does not stop the run.

### 7.6 The console (owner, 2026-10-16; revision 10)

Every line the tool prints in an interactive or listing run is at most 79
columns: a line of 80 wraps an extra blank line on the 98 and NT consoles.
The fixed text is written to that width, and everything printed passes a
streaming word wrap (`inst_wrap_*`) that breaks a line too long - one made
long by a device's name, an id or a path - at its last space, hung under
its text: under the text after a marker (`- `, `1. `, `[1] `, `NOTE: `),
under the text after a key followed by two spaces (a choice, a switch),
else two columns in; a word longer than a line breaks after a backslash.
The log gets the same lines.

Output redirected (addendum, 2026-10-16): with standard output going to a
file or a pipe (`INSTALL /eject > EJ.TXT`) while the answers come from the
keyboard, the questions - which device, "Eject it now?", the install's
confirmation - and the closing "Press Enter" used to land in the file
alone, and the run sat at a blank console waiting for a key, which looked
like a hang (read on 98 SE). Now, when standard output is not the console
and standard input and standard error are, everything printed also goes to
standard error, so the screen shows the run as before and the file gets
the same text (`inst_out_mirror`, host-tested). A handle is the console
when its console mode can be read. With answers from a file, or standard
error redirected too, nothing changes.

## 8. Registry values (owner, 2026-10-15)

### 8.1 The curated set

The tool asks about a short list of the values the INFs write, chosen as
the ones a user plausibly wants to change after reading README "Tuning"
and the release notes' "Registry settings" - a behaviour switch with a
user-visible effect, off or on by default for a reason the user can judge
- and none of the rest:

| Value | INF, section | Asked when | Allowed (asked) | Allowed (`/set`) | Meaning shown |
|---|---|---|---|---|---|
| `XhciIntelPortSwitch` | `xhci98.inf`, the controller's | a present controller passes the driver's `XhciPswGate` (Intel 7-, 8- and 9-series, C610) | 0, 1 | 0, 1, 2 (2 is the README's "at your own risk" setting for other Intel controllers) | route the shared connectors from the EHCI controller to this driver |
| `XhciInterruptMode` | `xhci98.inf`, the controller's | any xHCI controller | 0, 1, 2; 3 on Vista and 7 only | 0, 1, 2; 3 on Vista and 7 only (it fails the start elsewhere) | automatic, line only, polling always, message only |
| `XhciPollFineTimer` | `xhci98.inf`, the controller's | never (owner, 2026-10-15) | - | 0, 1 | a finer system clock while polling |
| `XhciSelectiveSuspend` | `xhci98.inf`, the controller's | never (owner, 2026-10-15) | - | 0, 1 | put idle devices to sleep (off by default; read on the release notes' terms) |
| `XhciSilentInstall` | `xhci98.inf`, the controller's | 98 SE and ME with an xHCI controller (owner, 2026-10-15); never on 2000 and later, where the window shows it greyed (record 26 section 5.4) | 0, 1 | 0, 1 on 98 and ME; refused on NT | install hubs, boot keyboards and mice and Bulk-Only storage with no Add New Hardware wizard; on is `tools\silent\silent-on.bat`'s effect, off `silent-off.bat`'s (section 8.4) |
| `XhciForceBulkOnly` | `xhci98.inf`, the controller's | any xHCI controller | 0, 1 | 0, 1 | Bulk-Only instead of UAS for a disk that offers both, from its next connection; a UAS-only disk is not affected |
| `XhciFastPollFsLs` | `xhci98.inf`, the controller's | never (owner, 2026-10-19; the window's "Advanced...") | - | 0, 2, 3, 4 (1 is not a mode) | poll a mouse faster than 1000 Hz, with SweetLow's hidusbf: off (0); a root-port mouse set to 31/62 Hz in hidusbf at 2000/4000 Hz (2) or 4000/8000 Hz (3); the rate the client chooses, codes 128 to 143 (4). Its caution: outside the xHCI specification for Low and Full Speed, read at the controller's start so restart after changing it, and with 4 the value set and Windows restarted before the filter's code |
| `XhciCmMode` | `xhcicm.inf`, the NHI's | a present NHI `xhcicm.inf` matches | 0, 1 | 0 to 3 | the connection manager off or automatic (2 and 3 are the record 21 overrides, `/set` only) |
| `XhciCmApprove` | `xhcicm.inf`, the NHI's | as `XhciCmMode` | 0, 1 | 0, 1 | approve Thunderbolt devices automatically |
| `XhciCmSafeEject` | `xhcicm.inf`, the NHI's | as `XhciCmMode` | 0, 1 | 0, 1 | offer Safely Remove for the dock's devices |
| `XhciCmReenum` | `xhcicm.inf`, the NHI's | never (owner, 2026-10-15) | - | 0, 1 | make Windows look for devices after each tunnel change |
| `XhciCmUsb3Tunnel` | `xhcicm.inf`, the NHI's | never (owner, 2026-10-15) | - | 0, 1 | USB4: build USB3 tunnels |
| `XhciCmLaneBonding` | `xhcicm.inf`, the NHI's | never (owner, 2026-10-15) | - | 0, 1 | USB4: bond the two lanes of a link |
| `XhciCmPreOsCleanup` | `xhcicm.inf`, the NHI's | never (owner, 2026-10-15) | - | 0, 1, 2 | USB4: tunnels found at start kept (0), cleaned where they do not verify (1), or all cleaned (2) |

Not asked: the log values (`SNAPON.BAT` and `SNAPOFF.BAT` own them, with a
restart the user is told about), the fourteen Linux quirk values (record
20: their defaults are the point), the poll periods, the moderation
interval, `XhciMaxTransferKB`, `XhciWideMap`, `XhciFirstEnum*`,
`XhciIntelIrqRoute` (a repair that is on by default and turned off only to
diagnose), `XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`,
`XhciAccept096` and `XhciUsb2HubSpeedFallback`; and, by the owner's
decision of 2026-10-15, `XhciPollFineTimer` and `XhciSelectiveSuspend`,
which stay settable with `/set Name=Value` only, their INF defaults
unchanged; and, by the owner's decision of the same day, `XhciCmReenum`
(its 0 exists to diagnose a machine where the re-enumeration misbehaves),
`XhciCmUsb3Tunnel` and `XhciCmLaneBonding` (USB4 diagnostics whose
defaults are the point) and `XhciCmPreOsCleanup` (an override of design
record 22 section 9's tunnel preservation), settable with `/set
Name=Value` only; and, by the owner's decision of 2026-10-19,
`XhciFastPollFsLs` (tuning for hidusbf, outside the specification at Low
and Full Speed), settable with `/set Name=Value` and the window's
"Advanced..." only. Any of them stays settable by hand, as today.

Each value's default is taken from its INF's `AddReg` line, not from the
table; a value the INF does not write is not asked, and the host suite
fails if a table row names a value an INF of `src\` does not write, or
writes at a default the row does not offer (section 10). The `XhciCm*`
rows follow `xhcicm.inf` as task 39-A.1 writes it; their allowed sets were
checked on 2026-10-15 against `cm_vals.c`'s rule table, record 21 section
8 and record 22 section 12, and match (design record 24 section 11).

### 8.2 Applicability (owner, 2026-10-15)

A question is shown only when the hardware or the system it affects is
there; `/set` may still set any value, within the system's allowed set:

- **Hardware**, from the PCI ids of present devices (the walk of section
  5.3 on NT; on 98 and ME the walk of section 6.3, with the
  `PCI\VEN_vvvv&DEV_dddd` part of each id): `XhciIntelPortSwitch` when
  `XhciPswGate((dev << 16) | ven)` is 1 for a present xHCI controller -
  the driver's own list, compiled in from `src\xhci_psw.c` (section 3);
  the `XhciCm*` values when a present device is matched by `xhcicm.inf`'s
  models for the system (section 5.2).
- **System**: `XhciInterruptMode`'s value 3 on Vista and 7 only;
  `XhciSilentInstall` on 98 SE and ME only (the driver never reads it on
  2000 and later, and `/set` refuses it there).
- **The configuration owner**: an `xhci98.inf` value belongs to the
  controllers' driver keys - the controller install sections are the ones
  that write it, and a root hub, hub or composite entry bound to the same
  service is not a target; an `xhcicm.inf` value belongs to the NHIs'
  keys, never a filtered port's. A target is a device that will be bound
  to the value's driver after the run - installed or updated by it, or
  already current or newer. On 98 and ME that is only a device already
  bound (section 6.5).

The applicability rule is a pure function of the value, the system and the
present ids, and is host-tested row by row (section 10).

### 8.3 The question

Per value, once, for every device it applies to:

```
XhciForceBulkOnly - Bulk-Only instead of UAS for a disk that offers both
  now: 0 (xHCI98 USB 3.x eXtensible Host Controller)
  allowed:
    0  UAS where the disk offers it
    1  Bulk-Only
  default: 0
  NOTE: read as each device is connected; ...
  Enter for [0]:
```

(Owner, 2026-10-16, revision 10: each choice on its own line with its
meaning, which the value table carries per value, from the release notes
and `docs/using/tuning.md`; a value whose choices have no meaning recorded
shows `allowed: 0 to 255` for a range, or the list. The last line says
what Enter gives: the value set now, which on a new install is the
default; "default: 1 (set now: 0)" when they differ, and "Enter for [each
keeps its own]" when two devices differ. A device's long name goes on a
line of its own under the value. `inst_value_question` renders it, and
the host suite checks every row against the width.)

`now` is the value in each device's driver key, or "not set" (the
install's `AddReg` then writes the default), or "new install: the default".
A value with a caution shows it under the question, and `/set` prints it
when it sets the value the caution is about. The tool's text is practical
guidance only: it never speaks of virtual machines, emulators, or what has
or has not been tested or read (owner, 2026-10-15). `XhciIntelPortSwitch`
2 forces the switch writes on an Intel controller without the switch
registers, "use it only if the release notes' "Value 2" section applies to
your machine"; `XhciSelectiveSuspend` 1 (set with `/set` only): "leave it
off unless a device needs it; if a device stops responding after idling,
set it back to 0"; `XhciCmMode` 2 and 3 force one connection manager,
"use them only on the release notes' advice"; `XhciSilentInstall`, before
its question every time: restart Windows after changing it (roadmap-usb4
task 43.6, the owner, 2026-10-19: no warning about the listed classes'
drivers, since the user knows what is being plugged in; until then it
also said to turn it on only once each listed class's drivers had been
installed at least once); `XhciForceBulkOnly` is read as each device is connected, and on Windows
2000 and later a disk already installed keeps its driver until it is
uninstalled in Device Manager and plugged in again; `XhciInterruptMode` 1
and 3 refuse a controller Windows gives no line or no message.
**Revision 23 (owner, 2026-10-19):** a run that will write `XhciCmMode`
0 says so before its confirmation, "*** Thunderbolt 3 and USB4 will be
turned off (XhciCmMode 0). ***", and the window's Summary carries the
same line at the head of its list (record 26 section 5.7;
`inst_cm_mode_off`, `instCmOffText`): on real hardware that night an
accidental mouse-wheel change set it to 0 and turned Thunderbolt off
with nothing said.
Two controllers with different values are listed apart and asked once: the
answer goes to both. Enter keeps each device's value. An answer outside the
allowed set is asked again; end of input keeps every value.

### 8.4 The write, and when it takes effect

After the installs (section 5.1 step 9), each changed value is written as a
`REG_DWORD` into the driver key the INF's `HKR` lines write - the device's
driver (software) key, the key its `Driver` value names, read again after
the installs: `HKLM\SYSTEM\CurrentControlSet\Control\Class\<Driver>` on NT
and `HKLM\System\CurrentControlSet\Services\Class\<Driver>` on 98 and ME -
and only a value the user answered with a value different from the current
one, or named with `/set`. The INFs' don't-overwrite flag (`0x00010003`) is
the install's convention and the tool keeps it: nothing but an explicit
answer changes a value, and the tool writes after the install so the
install's defaults cannot overwrite the answer. Every write is logged with
its old and new value.

**A value is read at the driver's start**, and an install starts the
driver at once with the INF's defaults. So an answer applies from the next
start, and the tool says so and sets the restart flag (section 5.9). For a
new NHI that matters: until that restart the connection manager runs with
`xhcicm.inf`'s defaults (`XhciCmMode` 1, `XhciCmApprove` 1), and devices
connected meanwhile are approved. The question for those two values says
so on a new install, before the confirmation, and suggests disconnecting
the dock until the restart when the answer is 0 (decision D12).
`XhciForceBulkOnly` is read as each device is enumerated instead
(release notes, "XhciForceBulkOnly"), with the caution of section 8.3.

**The silent install** (owner, 2026-10-15) does exactly what the
package's `tools\silent\` scripts do, with their files as the one source:
on (an answer of 1, or `/set XhciSilentInstall=1`, on 98 SE and ME) first
merges the package's own `tools\silent\SILENT.REG` into
`HKLM\System\CurrentControlSet\Services\USB\SilentClasses` and reads
every value back, and only then writes the switch at 1 on each
controller's driver key, as `silent-on.bat` does; a list that cannot be
read or does not read back leaves the switch unchanged (exit 2). Off (an
explicit 0 when the switch is on) writes the switch at 0 and leaves the
list, as `silent-off.bat` does. Enter keeps the state; the default answer
is off. (Revision 29, record 26 section 6.5.12) The 98 SE storage row
adds its own Bulk-Only entries to the list under its record and does
not turn the switch on: it stays the user's, as above.

### 8.5 The Thunderbolt 3 / USB4 question (owner, 2026-10-15)

Before the plan is shown, after the first reading of the devices, the tool
asks "Install Thunderbolt 3 / USB4 support?", on every system, whenever the
package carries the Thunderbolt driver (`xhcicm.inf`, `nt\xhcicmf.inf`
or on x64 `xhcicmf.inf`, `xhcicm.sys`). The decision is a pure function (`inst_tb_decide`,
host-tested case by case):

| Situation | Asked | Enter, and `/yes` |
|---|---|---|
| a device `xhcicm.inf` matches is present (section 8.2's detection) | yes, the controller named | N (revision 23; Y until then) |
| none is present | yes - some laptops show their controller only while a device is plugged in, or only with a BIOS setting (the P14s with Assist on and pre-boot support off) | N |
| `/thunderbolt` | no | Y, present or not |
| the USB drivers out of the run (`/cmonly`, Windows 8 and 8.1; revision 13, section 4.5) | no | Y, present or not |
| `/list` | no; the question and its default are shown | the default |
| the package carries no Thunderbolt driver | no | - |

**Revision 23 (owner, 2026-10-19):** the support is labelled
"(experimental)" in the question's heading and on the window's box, and
is never chosen unasked: Enter and `/yes` give N whether or not a
controller is found, and `/thunderbolt` (or the window's box) is the
way in. The heading's next line says whether a controller was found,
"A Thunderbolt 3 / USB4 controller was found: <name>", or that none was
and why that can be. The connection manager alone (`/cmonly`, 8 and
8.1) is unchanged: there the support is all the run installs. An update
over an install that has `xhcicm` therefore leaves it out unless asked,
and N keeps the installed Thunderbolt driver as it is (below).

The text says, in plain terms, that a device plugged into that port is then
approved automatically and can reach the computer's memory, names
`XhciCmApprove`, and points to `tools\xhciinst\readme.txt`, which is in the
package and says what the approval means and how to turn it off;
where a Thunderbolt driver is already installed it adds that N keeps it as
it is.

- **Y** with a controller present: the run as before - its devices planned
  and installed (the NHI, and the tunnel-port filter on NT), the INFs
  staged, the `XhciCm*` questions asked.
- **Y** with none present: on 2000 to 7 `xhcicm.inf` and the filter INF
  are staged with the rest (section 5.6), so Windows installs the NHI by
  itself when the controller appears; the filter, which the tunnel ports
  take only by an install the tool forces on XP and 7 (record 21 section
  3.3), comes from running `INSTALL.BAT` again then, which the run says.
  On 98 SE and ME `xhcicm.inf` stays in the local copy `DevicePath` names
  (section 6.2), where the hardware wizard finds it; as for the
  controller, the files then come from the copy's directory typed under
  "Copy files from", and the run's closing note names that directory.
  **Read on 98 SE** (2026-10-15): a Y with no controller, then the
  controller at the next start - the wizard offered `XHCICM.INF` from the
  copy with no box ticked, asked for the "xhci98 xHCI Host Controller
  Disk" at `D:\WIN98`, and installed the host interface once the copy's
  directory was typed.
- **N**: the three files are left out of this run - not planned, not
  staged in the driver store, not copied into the local copy (so no wizard
  and no store offers them), and no `XhciCm*` question asked. A
  Thunderbolt driver already installed is neither removed nor updated:
  its devices are simply not part of the run, and its group gets no
  verdict (a newer or unreadable xhcicm makes no step of a run that did
  not include it). The 98 and ME steps then name only the UAS disk as
  finding its driver in the copy.
- The local copy's name follows its content, so the two answers make two
  copies; an unfinished `/restage` recorded under either is the same
  package's, and one that still owes the Thunderbolt INFs is refused
  under N with "answer Y (or use /thunderbolt) to finish it". "The
  Thunderbolt driver is staged" is printed only when this run staged
  `xhcicm.inf`.


## 9. The uninstall

### 9.1 The warning

`INSTALL.BAT /uninstall` prints first, before anything is read:

```
*** This removes the xhci98 drivers from Windows. ***
On a machine whose only USB controller is xHCI, the USB keyboard and mouse
stop working when the controller's driver is removed, and stay dead until
another driver is installed. Have a PS/2 keyboard, a laptop's built-in one,
or a USB controller another driver runs, before you go on.
Type REMOVE and press Enter to go on; anything else cancels.
```

Where the `xhci98` service is read as not registered and `xhcicm`'s as
registered (revision 13: a machine that has the connection manager alone;
read before anything else, and on NT only), the first lines read
instead:

```
*** This removes the xHCI98 Thunderbolt 3 / USB4 connection manager. ***
Devices connected through a Thunderbolt or USB4 port stop working when it
is removed. Close every program that uses one, and save your work, before
you go on.
```

and the question "Remove the connection manager?"; the typed word is the
same. Any other reading, an unreadable one included, gives the first
text.

Only `REMOVE` goes on. With no console answer (end of input) the tool
refuses unless `/yes` is given, which is for the legs and scripts; `/yes`
is the one way past the word, and the tool says it was used. `/list`
prints the warning and the plan and asks nothing.

### 9.2 NT: what is removed, in order, and what stops it

The set is every devnode, present or not, bound to `xhci98`, `xhciuas` or
`xhcicm`, and every devnode whose device id is one of the bus's own forms
(`USB\XHCI98_ROOT_HUB` and the other three bases; `XHCI98\ROOT_HUB` and
the three old forms, record 13 section 10.13.8's rule 1). A devnode that
carries `xhcicm` in its `UpperFilters`, or whose function driver is
another's (`pci`) while its driver key's `InfPath` names this package's
filter INF - a tunnel port, its filter half-removed by an interrupted run
included - is **not** in that set: it is a filter host, and it is never
removed.

Before the first change the tool asks whether an installation is in
progress, as the install does (section 5.1 step 5); anything but "none"
stops with nothing changed. Each phase must then finish cleanly before the
next starts; a failure or a deferred result stops the destructive phases
after it, reports what was done, and tells the user to restart and run
`INSTALL.BAT /uninstall` again, which resumes from what is left.

0. **The safe-removal marks** (revision 12; record 21 section 7.4,
   decision M14), before anything of `xhcicm` goes. The mark service
   `xhcimark`, if registered, is stopped (`ControlService`,
   `SERVICE_STOPPED` within 30 s); the marks' write mutex
   `Global\Xhci98MarkWrite` is taken (opened, or created with the shared
   security of section 9.5's locks); every PCI devnode, present or not
   (`SetupDiGetClassDevsA` with the `PCI` enumerator and
   `DIGCF_ALLCLASSES`, no `DIGCF_PRESENT`), has its `SPDRP_LOWERFILTERS`
   read, and `xhcicm` is removed from each value that names it, the other
   names kept in order and the value deleted with a zero length when
   nothing else is in it, each read back; and every entry of
   `HKLM\SYSTEM\CurrentControlSet\Control\CriticalDeviceDatabase` has
   `xhcicm` taken out of its `LowerFilters` and its `UpperFilters` the
   same way, every entry and not only the ids the filter INF names (the
   copies Windows makes there of a boot-critical device's own filter
   values when it installs it: a marked unit's `LowerFilters` into
   `pci#cc_0604` or `pci#cc_010802`, the filter INF's `UpperFilters` into
   the tunnel port's id - read on 2000 on 2026-10-19, then on XP and 7 x86
   for a marked bridge's driver installed again; roadmap-usb4 42-A.24 -
   which would otherwise give every new bridge or NVMe controller, a
   boot disk among them, a filter that no longer exists), a key that cannot
   be read whole or a removal that does not read back failing step 0 as a
   device's does. Then the service's `Marks`
   records go, then its `system32` copy (one in use renamed to
   `<name>.DEL` and that name deleted at the restart, so a later install
   of the same name is not deleted with it; an earlier `.DEL` deleted or
   its deletion asked again), and only then the service itself
   (`DeleteService`). A stop that times out, a listing
   that does not reach its end, a value that cannot be read, a removal
   that does not read back, or a copy that can be neither deleted nor
   scheduled for deletion fails step 0: the run stops before step 1 with
   "steps left", keeping `xhcicm`'s service and file and the mark
   service's registration (and, when the marks themselves were not all
   removed, its records; a copy that fails after them leaves the records
   already deleted), and the next run resumes from what is left. The
   database is taken first, as the source new devices take the name from,
   and once every value is out both the database and the devices are read
   once more before the records and the service go, since Windows takes no
   lock of the installer's and a device found meanwhile may have taken the
   name from a copy. Step 0 needs neither a registered `xhcicm` nor a
   record, so the uninstall of a 3.1.0.0 package cleans what a test build
   left; `XHCICLN` only lists such values and points at it. The same step runs at the end of any install or update that
   leaves no `xhcicm` service (section 5.1 step 12).
1. **The filter hosts.** For each: `xhcicm` is removed from its
   `UpperFilters` multi-string and the rest kept in order
   (`SetupDiSetDeviceRegistryPropertyA(SPDRP_UPPERFILTERS)`; a value left
   empty is deleted with the same call and a zero length - the whole value
   is never removed while another name is in it). Then the port, if
   present, is given back to a driver that is not this package's **by the
   documented per-device route**, not an ID-wide call: the device's
   install parameters get `DI_FLAGSEX_ALLOWEXCLUDEDDRVS`
   (`SetupDiGetDeviceInstallParamsA`, `SetupDiSetDeviceInstallParamsA`),
   because Windows' own bridge node may be an excluded driver; its
   compatible driver list is built (`SetupDiBuildDriverInfoList`,
   `SPDIT_COMPATDRIVER`); the best-ranked node
   (`SetupDiGetDriverInstallParamsA`'s `Rank`) whose INF
   (`SetupDiGetDriverInfoDetailA`) is not this package's - Windows' own
   `machine.inf` node, or a vendor's - is selected
   (`SetupDiSetSelectedDriverA`) and installed
   (`SetupDiCallClassInstaller(DIF_INSTALLDEVICE)`), and the restart flags
   are read after it (unreadable stops). **The postconditions**, read back
   from the devnode's key and its driver key: the same function driver as
   before; `UpperFilters` equal to the edited value - the other filters
   kept, and set back if the reinstall changed them (a restart then); and
   an `InfPath` that is readable and not this package's. No such node, a
   failure, or a postcondition not met stops the run before step 2. A port
   not present cannot be
   reinstalled: its filter name is removed and the run stops (exit 4)
   with "connect the dock and run INSTALL.BAT /uninstall again" - the
   port is still a filter host by its `InfPath`, so the next run finds it.
   This is unread in any guest here (no Thunderbolt port in QEMU; section
   12) and is held to these gates.
2. **The devnodes**, children before parents (UAS devices, composite and
   hub entries, root hubs, NHIs, controllers last), each with
   `SetupDiCallClassInstaller(DIF_REMOVE)` and `DI_REMOVEDEVICE_GLOBAL`,
   the route Device Manager's Uninstall and `XHCICLN` take;
   `DI_NEEDREBOOT` or `DI_NEEDRESTART` after it is "removed, restart
   pending". Device and function devnodes of other drivers beneath (a
   mouse's `hidusb`, a stick's `usbstor`) are not this package's and are
   left: they go non-present with their parent. A failed removal, or a
   restart pending, stops the run before step 3.
3. **The check.** The devnodes are read again (section 5.3). Any devnode,
   present or not, still bound to one of the three services, still in the
   set, or still a filter host stops the run before step 4. A present one
   means Windows found the hardware again and installed this package's
   driver on it: the run stops (exit 4) and says to remove that device in
   Device Manager (on Vista and 7 with "Delete the driver software"
   ticked) and run the uninstall again; the tool does not loop.
4. **The INFs**: every INF in `%windir%\inf` whose `[Version]` `Provider`
   is this package's and whose install sections name one of the three
   services (`AddService`) or files (`NTMPDriver`), read with the tool's
   own parser - the staged `oemNN.inf` copies, and the plain copies the
   INFs' own `DefaultInstall` sections make through directory id 17. An
   INF that cannot be read, or a listing of `%windir%\inf` that does not
   reach its end, leaves ownership unestablished and stops the run before
   step 5. Where
   `SetupUninstallOEMInfA` resolves (Windows XP onward, Microsoft's
   documentation; the legs read which systems have it), it removes each
   `oemNN.inf`, flags 0, so a package still referenced is refused and
   reported rather than forced; on Vista and 7 that also removes the
   driver-store package. Otherwise - Windows 2000, and every plain copy -
   Microsoft's documented manual route (KB 813449): first every devnode,
   present or not, has its driver key's `InfPath` read, and an INF any of
   them names, or any `InfPath` that cannot be read, is kept and stops
   the run; then the `.pnf`, then the `.inf`.
5. **The services** - first the inventory of step 0 read once more, a
   name left (or a listing that cannot be read) stopping the run before
   `xhcicm`'s service and file go - then `xhci98`, `xhciuas`, `xhcicm`, `xhcihlpr` and the
   `2.3.x` `xhcimsi`, each with `DeleteService` (advapi32), the handle
   closed at once. Microsoft's documentation: the service is deleted once
   it has stopped **and** every handle to it is closed - for a running
   driver, at the next start. The tool reports the state it read (running
   or stopped) beside "marked for deletion", and a running one sets the
   restart flag.
6. **The files** in `%windir%\system32\drivers`: `xhci98.sys`,
   `xhciuas.sys`, `xhcicm.sys`, `xhcihlpr.sys`, `xhcimsi.sys`, each only once
   step 5 has shown its service deleted or marked. A file in use is
   scheduled with `MoveFileEx(..., MOVEFILE_DELAY_UNTIL_REBOOT)` and
   reported as pending. A service whose deletion failed keeps its file.
   Then the local copies and the `/restage` work file (section 4.4).
6a. **The CriticalDeviceDatabase's service entries** (revision 17): when
   Windows installs a boot-critical device it writes an entry under
   `HKLM\SYSTEM\CurrentControlSet\Control\CriticalDeviceDatabase` for
   its ids and class with its `Service`; left after the uninstall, the
   next install of that device starts from a service that no longer
   exists (read on 2000: the NHI came back at Code 19 through
   `pci#cc_0c0340` and `pci#ven_8086&dev_15bf` naming `xhcicm`; the same
   entries were left on XP and 7). Every entry whose `Service` names, case
   ignored, a service of the package that this run removed or marked for
   deletion, or that the service manager reads absent or marked now
   (`xhci98`, `xhciuas`, `xhcicm`, `xhcihlpr`, `xhcimsi`, and `stor98`,
   which is gone only when Stor98's own removal took its service, so a
   Stor98 that was there before the package or that a device still uses
   keeps its entries), is deleted with `RegDeleteKeyA` and read back gone
   (`inst_cddb_service_gone`). An entry naming another driver's service
   is never touched. An entry or a `Service` value that cannot be read,
   or a deletion that does not read back, fails the step as step 6's
   failures do; `/list` names the entries it would delete. Read on 2000
   in 42-A.24's leg: the uninstall deleted the two entries naming
   `xhcicm`, kept `pci#cc_010802` naming the Stor98 a drive still used,
   and after the restart the NHI came back with no driver (Code 1, as
   before the package was first installed, the Found New Hardware wizard
   finding none), not Code 19. `xhci98` and `xhciuas` left no entry on
   that 2000 disk.

### 9.3 What it never removes

The OS's own `usbd.sys` and `usbui.dll`: the install had Windows copy them
from its own source because nothing on an xHCI-only machine had placed them
(`legal-provenance.md` section 5), they are Windows' files, other USB
drivers import `usbd.sys`, and `usbui.dll` draws every USB property page.
Device and function devnodes of other drivers (above). A filter host's
devnode (above). Any value, file or devnode the walk cannot attribute to
this package. Another release's package is removed only through the INF
rule of step 4, which matches every release of this project by provider and
by service or file.

### 9.4 98 SE and ME: best effort, then the manual steps

No documented interface removes a devnode there: `CM_Uninstall_DevNode`
is a stub on both (record 13 section 10.13.8, read statically from each
system's `CFGMGR32.DLL`). So the tool:

1. removes what it owns: its entries in `DevicePath`, then - only once
   that write succeeded, so no search path is left naming a removed
   source - the files the package copy's `XHCIINST.LST` names in the
   install directory (section 9.6) and those each earlier build's copy
   names under `%windir%\XHCI98`, and each directory if it is then
   empty (a failed `DevicePath` update keeps the copies and ends with
   exit 2);
2. lists the INF files in `INF` and `INF\OTHER` whose `Provider` is this
   package's and that install one of its drivers (the rule of section 9.2
   step 4) - the device installer's own copies, and the plain copies the
   INFs' `DefaultInstall` sections make - and the driver files present;
3. prints the steps, dependants first: in Device Manager remove "xHCI98
   USB Attached SCSI Storage" and an "xHCI98 Thunderbolt 2 Controller",
   "xHCI98 Thunderbolt 3 Controller" or "xHCI98 USB4 Host Router" if
   there is one,
   then "xHCI98 USB 3.x eXtensible Host Controller" (which removes the root
   hub and everything beneath it); then delete the listed INF files, so
   the restart cannot find the driver again; then restart, and
   cancel the New Hardware wizard if it offers the controller; then
   delete the driver files listed and restart once more.

Whether the wizard offers the controller again after step 3's restart, and
what it finds, is read on the legs. Exit 4.

(Revision 24) On 98 SE the uninstall also removes the USB storage
drivers this installer placed, as record 26 section 6.5.8 says: its
recorded storage devices, and those Windows bound from its copy after
the run (adopted first, record 26 draft 18), before this package's own,
then, after the
package's INF copies and driver files, the INF copies the setup engine
made for it, (revision 25) the two INFs it placed in `%windir%\INF`
itself with their `.PNF` files, and the files it copied, each only by
its record and the reference checks, none while NUSB's
`Uninstall\USB20U` is present.

### 9.5 The xHCI98 USB Tree (`XHCITR`)

Design record 25 section 8 is the source; this is how the tool does it.
The tool's file (`XHCITR.EXE` on 32-bit Windows, 98 SE and ME included;
`XHCITR64.EXE` on x64), its `.TMP`, `.BAK` (98 SE and ME, during a
replacement) and `.DEL` (NT, an uninstall's copy in use) forms and the
Run value `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`, `xHCI98
USB Tree` = `"<path>" /tray`, are this tool's own, outside every copy's
`XHCIINST.LST`. The file sits in the install directory (section 9.6;
`%windir%\XHCI98\` itself, beside the local copies, before revision 10). The tool is replaced at once or not at all:
nothing is left pending for a restart on 98 SE or ME, no replacement is
deferred on NT, and `WININIT.INI` is never edited.

- **One at a time.** Everything below but `/list` runs under the
  maintenance mutex `Global\XhciinstMaint` (`XhciinstMaint` on 98 SE and
  ME), opened or else created with the shared security (below); a second
  XHCIINST waits up to 60 s, then leaves the tool as it is and fails that
  part of its run. The versions are read once it is held.
- **Leftovers first**, before any version is read: a `.TMP` is deleted; a
  `.BAK` with no tool beside it is an interrupted replacement and is
  renamed back - if that fails, the tool's part of the run stops there
  with steps left (exit 4) - and a `.BAK` beside the tool is deleted
  (`inst_tray_leftover`); on NT a `.DEL` that no longer runs is deleted,
  and one still in use has its deletion at the restart asked again (a
  restart is then asked for; a failure is reported, with steps left). A
  leftover that cannot be deleted is reported, with steps left.
- **Install and update.** Every run that passes its confirmation, on both
  routes, after the drivers (NT: after the results and `/restage`'s work
  file; 98 SE and ME: after the registry values, before the printed
  steps), and under `/list` as one line of what would be done. (Revision
  20) That line says what the Run value is now - present, absent, or
  holding other data or unreadable - and then what a run with the same switches would
  do to it (`inst_tray_list_text`); it never says "started at login" with
  no Run value (the 98 SE leg of 2026-10-19, after an update with the
  tool unticked).
  1. A package without `tools\xhcitr\<name>` (an older or a development
     one) is said in one line, and nothing else of the tool is touched.
  2. The file is copied when it is missing, or when the package's version
     is read and is newer than the installed file's, an unreadable one
     counting as older (`inst_tray_copy_needed`); otherwise it is left.
  3. The new copy is written as `.TMP` and compared with the package's.
     Running copies are then closed: XHCIINST alone creates the
     manual-reset quit event `Global\Xhci98TrQuit` (`Xhci98TrQuit` on 98
     SE and ME; every user may wait on it, SYSTEM and the Administrators
     may do everything), sets it, posts `WM_CLOSE` to the `Xhci98TrTray`
     window of this session (`FindWindowA` and `PostMessageA` looked up
     in `user32.dll` at run time), and waits up to 15 s until the file
     opens for writing with no sharing. The event is closed when the
     tool's part of the run ends, so it exists only then; `XHCITR` only
     opens it. No process is ever ended.
  4. NT replaces the tool with `MoveFileExA` and replace existing; 98 SE
     and ME rename the tool `.BAK`, `.TMP` to the tool, and delete
     `.BAK`, renaming `.BAK` back when the second rename fails. Every
     result is checked.
  5. A tool still in use after the 15 s is left as it is, the `.TMP`
     deleted, and the run says to close the xHCI98 USB Tree (or restart
     Windows) and run INSTALL again to update it; it ends with steps left
     (exit 4), the drivers' part of the run unaffected.
  6. The Run value is written when the file is in place and the value is
     not already the one wanted; `/notray` writes none and deletes an
     existing one (`inst_tray_run_action`). A deletion that fails, as
     against a value that is not there, is reported and fails the run
     (exit 2).
  7. (Revision 35) `/faster` and `/nofaster` write the DWORD
     `TrayFasterNotice` under `HKLM\Software\xhci98` (the install
     directory's key, section 9.6) as 1 and 0: the computer's choice of
     XHCITR's "can run faster" notice (record 25 section 7.7), which a
     user's own choice in XHCITR overrides. Neither switch writes
     nothing, and the value stays as it is; the console asks no question
     for it. Written when the tool's file is in place, whatever the Run
     value; a write that fails is reported and fails the run (exit 2);
     `/list` says what would be written (`inst_fast_list_text`).
- **Uninstall.** As the first step of the removal of the install
  directory and `%windir%\XHCI98` (both routes, after everything else;
  under `/list` one line each; section 9.6),
  under the maintenance mutex: the Run value is deleted (a failure is
  reported and fails the run), the leftovers are handled as above, the
  running copies are closed as above and the file is deleted. A file
  still in use is, on NT, renamed `.DEL` (a running image can be renamed
  on its volume) and that name deleted at the restart (`MoveFileExA`,
  delay until reboot), and the then-empty `%windir%\XHCI98` is scheduled
  for removal too; the next install or uninstall deletes a leftover
  `.DEL`. On 98 SE and ME it is left and reported, with steps left: it no
  longer starts at login, and the next uninstall deletes it. (Revision
  35) `TrayFasterNotice` goes with the install directory's record
  (section 9.6), its deletion checked as `InstallDir`'s is.
- **Exit codes.** The tool's steps left and failures join the run's: on
  98 SE and ME an install ends with steps left when a device or the tool
  leaves one, and an uninstall, whose manual steps are always left, ends
  with exit 4 unless something failed (exit 2), as on NT.
- **The security of the shared objects.** `/devices` and `/eject` take
  `Global\XhciinstTbAsk` (`XhciinstTbAsk` on 98 SE and ME), which
  `XHCITR`'s eject takes too. It and the maintenance mutex are opened
  first with `SYNCHRONIZE` and `MUTEX_MODIFY_STATE`, else created with
  every user (the World SID) allowed those and SYSTEM and the
  Administrators everything, a lost race opening the winner's;
  `WAIT_ABANDONED` counts as taken. The eject lock refuses after 10 s.
  The security routines (`AllocateAndInitializeSid`, `InitializeAcl`,
  `AddAccessAllowedAce`, `InitializeSecurityDescriptor`,
  `SetSecurityDescriptorDacl`, `FreeSid`) are looked up in
  `advapi32.dll` at run time on NT only, so the import table and 98 SE's
  advapi32 are untouched.

**The safe-removal mark service** (revision 12; record 21 section 7.4,
revision 3, decision M8), Windows 2000 to 7, and 8 and 8.1 from
revision 13 (section 4.5), never 98 SE or ME. Its executable is never
in the install directory: a program the service controller starts as
`LocalSystem` sits where only administrators and the system can replace
it. The tool copies the package's `tools\xhcimark\XHCIMARK.EXE` (32-bit
Windows) or `XHCIMK64.EXE` (x64, into the 64-bit `system32`) to
`%SystemRoot%\system32\<name>.TMP`, compares it byte for byte, moves it
over `<name>.EXE` with `MoveFileExA(MOVEFILE_REPLACE_EXISTING)` (the
`.TMP` route above, applied to that directory) and compares the result
again: a copy that is not the package's is never registered. A running
service is stopped first (`SERVICE_STOPPED` within 30 s, or the service is
left as it was and the run fails) and started again after. A new service
is `CreateServiceA` `xhcimark`, "xHCI98 Thunderbolt safe-removal mark
service", `SERVICE_WIN32_OWN_PROCESS`, `SERVICE_AUTO_START`, the
`LocalSystem` account, with the quoted full path; an existing one has the
same set again with `ChangeServiceConfigA`. The tool makes
`system32\LogFiles\xhci98\` (the directory's default rights) for the
service's log, then starts it. The uninstall's step 0 (section 9.2) stops
and deletes it with its copy; the move of the install directory does not
concern it.

### 9.6 The install directory (owner, 2026-10-16; revision 10)

The owner, having run the tool on a real machine: the user names the
install directory, or it is `C:\XHCI98`.

- **What goes there**: the USB tree tool (section 9.5), and on 98 SE and
  ME the package's drivers themselves, in the directory with no subfolder
  (section 4.4), which `DevicePath` names and the user types into the
  hardware wizards. On 2000 to 7 the local copies stay in
  `%windir%\XHCI98`: Windows 2000 and XP take a device's files later from
  where its INF was staged, so a copy that moved would break the next
  device's install. The question lists what goes there, one line each.
- **The question**, after the registry values and before the
  confirmation: "Install directory for the xHCI98 tools", a line for each
  thing that goes there, where they are now (an update), on 98 SE and ME
  the naming rule, and "Enter for [<default>]". NT asks it only when the
  package carries the tool. An answer that fails the checks below is
  said, and asked again; end of input takes the default.
- **The default**: the directory an earlier install recorded; else an
  earlier build's `%windir%\XHCI98` when its tool is there (or, on 98 SE
  and ME, a copy with its `XHCIINST.LST`); else `C:\XHCI98` on the
  system's drive - `%SystemDrive%` on NT, the Windows directory's drive
  on 98 SE and ME (`inst_dir_default`) - shown resolved.
- **`/to <path>`** gives it unasked (the name avoids `/dir`, which names
  the package's flavour directory). `/yes` and `/defaults` take the
  default unasked. `/list` shows the question and what Enter would give.
- **The checks** (`inst_dir_check`, host-tested, then the system's):
  `X:\name[\name...]`, not a drive's root, no `" * ? < > | ;` (the `;` is
  `DevicePath`'s separator) and no second colon, no `.` or `..` part and
  no part ending in a space or a dot, at most 100 characters; not inside
  the Windows directory, except exactly the earlier build's
  `%windir%\XHCI98` kept where it is; not inside the package directory;
  on 98 SE and ME every part an 8.3 name with no spaces, because the
  wizards' "Copy files from" and `DevicePath` take it as typed. Then the
  drive must be a local hard disk (`GetDriveTypeA`, `DRIVE_FIXED`) and the
  path a directory or absent below one. It is made, parents first, when
  something is first put there.
- **Checked as typed** whatever its source: `/to`, an answer, and the
  default too, so that an unpacked package in `C:\XHCI98` refuses the
  default under `/yes` ("name another with /to") rather than install
  into the package.
- **The record**: `HKLM\Software\xhci98`, value `InstallDir` (`REG_SZ`),
  written once what goes there is in place - the tool, or on 98 SE and ME
  the copy and `DevicePath`. The uninstall deletes the value, and the key
  when nothing else is in it (98 SE's `RegDeleteKey` would take the rest
  with it); a tool left running on 98 SE or ME keeps the record, so the
  next uninstall finds it.
- **A move** (another directory than the earlier one, which is how the
  earlier builds' `%windir%\XHCI98` is left on an update): before
  anything is copied, the tool running from the old directory is closed
  as in section 9.5; one that does not close keeps everything where it is
  this time (said, steps left, exit 4), so the tools and the copies are
  never split between two directories. Before anything moves the move is
  recorded: the old directory as `MoveFrom` beside `InstallDir`, then the
  new one as `InstallDir` (`begin_move`; a registry that takes neither
  stops the move with nothing changed), so whatever stops it part-way,
  the next install finishes it and the uninstall cleans both. Then the
  tool is put in the new directory (section 9.5's `.TMP` route) - the old
  directory's tool where it is newer than the package's, after its own
  leftovers are put right - the Run value names it, and only then is the
  old copy deleted - one in use is, on NT, renamed `.DEL` and deleted at
  the restart, and on 98 SE and ME left with a step. A move that leaves
  nothing behind deletes `MoveFrom`; another move is refused while one is
  outstanding. Two names of one directory are one directory, not a move:
  the same text, the same short path (`GetShortPathNameA`), or a probe
  file of a new name made in one and found in the other (a junction, a
  substituted drive), never under `/list`. A directory inside, or around,
  the one the tools are in now is refused, and the Windows and package
  checks are made on the short forms too. On 98 SE and ME the new copy is made and
  `DevicePath` updated first, and the old directory's copies are removed
  only once `DevicePath` no longer names them; the old directory goes if
  it is then empty. No replacement is deferred and `WININIT.INI` is never
  edited, as in section 9.5.
- **The uninstall** removes the tool from the install directory, from
  an earlier build's `%windir%\XHCI98` and from a `MoveFrom` directory,
  the package copy (what its lists name) and the `<hash>` copies (in a
  directory the user chose only those holding this tool's list), so a
  directory the user chose keeps what else is in it; then the
  directories when empty, and the record.
- XHCITR does not care where it lives: it names no directory, and the
  maintenance lock, the quit event and the tray window's class are
  unchanged.

## 10. The pure half and the host suite

`xhciinst\inst_core.c` (with `inst_core.h`) holds everything that is a
function of its inputs, C89 with no Windows header, so `test\test_inst.c`
links it in `test\run-host-tests.cmd` under `/Za` like every other suite,
and the tool links the same file:

- the INF parser: sections, lines, `key = value`, comma fields, quotes,
  comments, `%strings%` expanded from `[Strings]` for `Provider`;
- `DriverVer` and four-part version parsing and comparison;
- section 5.2's models-section choice and section 4.2 item 5's
  architecture rule;
- id matching against hardware and compatible id multi-strings;
- section 5.4's action table (ownership first, unknown refused) and its
  group verdict, and section 9.2's devnode selection and order;
- the PE machine read, the embedded-signature test (section 5.10) and the
  flavour marker search over a file buffer;
- the `DevicePath` append and removal (section 6.2);
- the `UpperFilters` edit (section 9.2 step 1);
- `SystemStartOptions`' test-signing word;
- the switch parser, `/set`'s validation and section 7.4's exit code,
  last line and confirmation wording;
- section 5.8's settle state machine, busy-retry rule and (revision 14)
  parent rule, section 5.9's
  device state text, and section 5.6's "staged already" test;
- the registry value table, its applicability (section 8.2) and its
  answer parser;
- (revision 10) the console's word wrap and width, the questions' text
  (`inst_value_question`, `inst_yesno_question`, `inst_dir_question`),
  the install directory's checks and default, and `DevicePath`'s removal
  of a chosen directory's copies;
- (revision 13) the system's kind (`inst_os_kind`: a full target, the
  connection manager's alone, refused) and its name, the USB drivers'
  decision (`inst_usb_decide`, section 4.5), the Add/Remove Programs name
  and the uninstall warning by what is registered, and the models
  section chosen for 6.2 and 6.3.
- (3.1.0.0) the filter INF's place per architecture (`inst_cmf_rel`:
  `nt\xhcicmf.inf` on x86, `xhcicmf.inf` at the flavour directory's root
  on x64), its `.PNF` (`inst_cmf_pnf`) and the place the Have Disk text
  names (`inst_cmf_where`).

The suite reads `src\xhci98.inf`, `src\xhci98-amd64.inf`, the two UAS INFs
and, once task 39-A.1 has merged, the `src\cm\` INFs, and checks that each
value-table row is written by its INF at a default the row offers, and
that section 5.2's choice gives each system its section.
`xhciinst\selftest.cmd` runs the executables' `/selftest` (a short set of
the same checks, so the shipped binary's linkage is exercised, and since
revision 10 every screen of an interactive and a listing run rendered
through the tool's own functions with made-up devices, once with names
and paths as long as they come in practice, when no line may need the
wrap, and once at their longest, when no line may be wider than 79
columns), their switch refusals (`/frobnicate`, `/set Bogus=1`, `/list
/yes`, `/uninstall /to`), and the import table; `build-driver.cmd` runs it whenever the executables have been
built, as it runs `XHCICLN`'s.

## 11. Gates, packaging and documents

- **build-driver.cmd**: a step after `XHCICLN`'s, "xhciinst self-test",
  when `xhciinst\XHCIINST.EXE` exists; the host suite runs with the others.
- **The import gate for the tool**: `xhciinst\selftest.cmd` refuses an
  import table naming any DLL but `KERNEL32.dll` and `ADVAPI32.dll`, on
  both executables. The kernel-mode import gate does not apply (user
  mode).
- **The charset gate** covers `xhciinst\` as it covers every tracked file;
  `INSTALL.BAT` is CRLF by `.gitattributes`.
- **make-release.ps1**: stages `tools\xhciinst\` with both executables,
  `readme.txt` from `xhciinst\README.TXT` and a generated `NOTICE.TXT`, and
  `INSTALL.BAT` at the root, with the freshness check (each executable
  newer than every source beside it), the version check
  (`XHCIINST_VERSION` expanding `XHCI_VER_STR`), and a `-SkipInstallTool`
  switch for a host that cannot build it, all as for `XHCICLN`;
  `$toolNames` and `Assert-ToolsLayout` name the directory, and the
  top-level layout check admits `INSTALL.BAT`. `make-package.ps1` is
  unchanged: it stages flavour directories, which the tool is not part of.
- **test-package.ps1**: the mutation cases `XHCICLN` has, for the new tool.
- **The USB tree tool** (revision 9): `make-release.ps1` stages
  `tools\xhcitr\` with `XHCITR.EXE`, `XHCITR64.EXE`, `readme.txt` from
  `xhcitr\README.TXT` and a generated `NOTICE.TXT`, with the same
  freshness and version checks (`XHCITR_VERSION` expanding
  `XHCI_VER_STR`, and each EXE's numeric file version, which XHCIINST
  compares, equal to the release's) and a `-SkipTrayTool` switch; `$toolNames` names the
  directory; `test-package.ps1` holds those lines and the layout.
- **Documents**: README "Installation Steps", "The package" and "Upgrading
  the driver"; the release notes' "Installing", "Updating from 2.y.y.y",
  "Registry settings" (the tool as a second way to set the curated
  values) and a new "Uninstalling"; the readme template in
  `make-release.ps1`; `docs/README.md`, `source-files.md`,
  `build-and-test.md` (the build and the self-test), `legal-provenance.md`
  section 2a, AGENTS.md's tool-executable sentence; the release
  acceptance test's layout rows.

## 12. The legs, and the reading plan per system

Run on development host A's QEMU guests with overlays only, under the
admission and lock rules, from a package staged by `make-package.ps1`
(x64 unsigned when the signing key's password is not available, then on
F8 starts), recorded in `docs/contributing/runs/run-40.md`.

Every leg records: the tool's console output and log (out through COM1),
the prompts in order, `/list` before and after, the devices' driver keys
(`reg query`, or `regedit /e` on 2000, 98 and ME), a byte compare of each
installed `.sys` with the package's, and Device Manager.

| Leg | Systems | What it reads |
|---|---|---|
| F, fresh install | 98 SE, ME, 2000, XP, XP x64, Vista x86, Vista x64, 7 x86, 7 x64 | NT: one run installs the controller and root hub; the routines resolved; the restart reported (32-bit Vista and 7); a UAS disk plugged after the run installs from the staged INF with no question about where. 98 and ME: `DevicePath` written; the wizard finds the controller with no box ticked, or by the typed path the tool printed; the files come from the copy's directory under `%windir%\XHCI98\`; the CD prompt for `usbd.sys` (98 SE); a UAS disk plugged later finds `xhciuas.inf` |
| U24, update over `2.4.0.0` | the nine | NT: the controller, root hub, hubs and composite entries updated, each driver key's version the new one, every `.sys` equal to the package's; 2000 with no Have Disk; the Vista and 7 trap - this branch's INFs are byte-identical to `2.4.0.0`'s until the `3.0.0.0` restamp, so a leg with the unrestamped package reads the trap's detection and `/restage`, and a leg with a restamped package the normal update; 98 and ME: the update wizard finds the new driver by search |
| U23, update over `2.3.1.0` | 98 SE, 2000, XP, XP x64, 7 x64 | the new devnodes of record 13 section 10.13.4; on XP and XP x64 the wizard stop and the second run (section 5.8); on 7 x64 installed by step 8 or by Windows; the one line naming `tools\xhcicln\` after the restart, on a run that changes nothing, and no `XHCICLN` run |
| UN, uninstall and reinstall | 98 SE, ME, 2000, XP, XP x64, Vista x86, Vista x64, 7 x86, 7 x64 | the warning and the word; every devnode, the OEM INFs (2000: the manual route; XP onward: `SetupUninstallOEMInf`, which systems resolve it), the services and files gone or pending; the gate after a pending removal and the resumed second run; a restart; the controller back as an unknown device; `INSTALL.BAT` installing it again. 98 SE and ME: the steps printed and followed |
| R, registry questions | 98 SE (after the install and restart), 2000 | each applicable question shown with now, default and allowed; a non-applicable one not shown (no Intel switchover controller in QEMU, no NHI); an answer written to the controller's driver key and not to the root hub's, Enter keeping, `/set` and `/defaults` |
| L, `/list` and the refusals | every leg's first run | nothing changed (a registry and file listing before and after, no log written); the ZIP-folder refusal, a mixed architecture, a mis-versioned INF, a stale `.sys`, a swapped flavour |
| P, payload repair | XP, 7 x86 | a current installation whose `xhci98.sys` is deleted, then (separately) replaced by the earlier release's, and on 7 x86 `xhcihlpr.sys` deleted: `INSTALL.BAT` reinstalls the controller, the files compare equal to the package's afterwards, and the restart it reports is honoured (section 5.4) |
| M, moved source | 2000, XP | an installation made the old way first - Have Disk from a CD image for the controller **and** for a UAS disk, so the staged `xhciuas.inf`'s `.pnf` names the CD - then the tool run on the identical package, then the CD image removed, then a UAS disk never seen before plugged: it must install with no prompt (section 5.6). XP x64 is not covered by this leg, and the release notes say so |

What each system's legs settle that nothing here has read:

- **98 SE and ME**: whether the wizard searches a `DevicePath` entry other
  than `INF` and copies the files from it (section 6.2); whether
  `SetupCopyOEMInfA` resolves (for the record); the uninstall steps and
  whether the restart offers the controller again.
- **2000**: that `INSTALLFLAG_FORCE` takes the update the search refuses;
  the `.pnf` route of section 9.2; where `CMP_WaitNoPendingInstallEvents`
  resolves.
- **XP and XP x64**: the wizard stop and the second run (section 5.8);
  that an identical INF is no trap there (5.7); the source refresh (5.6;
  XP only, leg M);
  `SetupUninstallOEMInf` on XP SP3 and XP x64 SP2.
- **Vista and 7**: the trap's detection and `/restage`; the restart on
  32-bit; `SystemStartOptions`' `TESTSIGNING` on the x64 guests in
  test-signing mode and on an F8 start.

Not read in these guests, and owed to the Phase 39 and 40 benches: the
Thunderbolt half. No NHI or tunnel port exists in QEMU here, so
`xhcicm.inf` and `xhcicmf.inf` are pre-staged and their questions not
shown; the install on an NHI and a tunnel port, the filter removal of
section 9.2 step 1 and a failed filter removal are read on `usb4-emu`'s
guests or the P14s. Also owed: two controllers of different versions
(the group verdict), an interrupted `/restage` and its resumption by the
work file, a refused package removal, a disconnected tunnel port during
the uninstall, and runs that combine a failure, a restart and steps for
the user.

## 13. Decisions taken in writing this (orchestrator decisions, owner to confirm)

- **D1** The executables in `tools\xhciinst\`, `INSTALL.BAT` at the root;
  one batch file, the uninstall behind `/uninstall` (section 2).
  Revision 22 (the owner, 2026-10-19): `SETUP.EXE` beside it at the root
  is what a person runs, `INSTALL.BAT` the command line's and scripts'
  (record 26 W17); still one batch file.
- **D2** Windows 98 SE and ME through `DevicePath` and the local copy in
  in this package's own directory under `%windir%\XHCI98\`, which
  `DevicePath` names in place of an earlier copy's, never `INF\OTHER` or
  `SETUPX` (section 6). (Revision 24) On 98 SE the same route carries the
  USB storage drivers' INFs when record 26 section 6.5's row is ticked.
- **D3** A device bound to another vendor's driver is listed and left,
  including an xHCI controller, whatever its problem code; Device Manager
  remains the way to replace a vendor driver (section 5.4). Amended by
  revision 26 (record 26 section 5.3.1 cases 3a and 3b): on Windows 8 and
  8.1 alone, with the box ticked by the user, Windows' own in-box drivers
  `USBXHCI` (a controller) and `stornvme` (a drive behind a tunnel, for
  Stor98) are not another vendor's, and are installed over; every other
  vendor's driver is left as before.
- **D4** A group running a newer release is not downgraded without
  `/force`, and an unknown version is never overridden (section 5.4).
- **D5** The Vista and 7 trap is reported by default and replaced only with
  `/restage`, which reinstalls the group's devices (section 5.7).
- **D6** Test-signing mode is read and reported, never changed
  (section 5.10).
- **D7** Every changing run copies the package to an immutable directory
  of its own content under `%windir%\XHCI98\` and installs from there, on
  every system; only the uninstall removes the copies (section 4.4).
- **D8** The log at the root of the system drive, appended; not written by
  `/list` (section 7.5).
- **D9** The curated registry set of section 8.1, asked only where it
  applies, written only on an explicit answer.
- **D10** (owner, 2026-10-15) The tool never runs `XHCICLN`; where old
  entries exist it prints one line naming `tools\xhcicln\` (section 5.11).
- **D11** The uninstall leaves the OS's `usbd.sys` and `usbui.dll`, other
  drivers' devnodes and every filter host's devnode, gates each phase on
  the one before, uses `SetupUninstallOEMInf` without force, and on
  Windows 2000 deletes the `.pnf` and then the `.inf` (section 9).
- **D12** A registry answer applies from the next start of the driver; a
  new NHI's first start runs with the INF's defaults until then, and the
  question says so (section 8.4).
- **D13** An installation in progress - before the first change, or after
  the controller's update - is waited for (the settle, bounded) and stops
  the run if it does not end; the tool never installs under a wizard, and
  a call that meets Windows' own install is made again only after it has
  ended and only if still needed (sections 5.1 and 5.8).
- **D15** The last line and the exit code agree: a finished run that
  changed something asks for the restart and exits 3 (section 7.4).
- **D14** A filter host is given back to a driver that is not this
  package's by the per-device driver-node route, never by an ID-wide
  call; a disconnected one stops the uninstall until it is connected
  (section 9.2).
- **D16** (revision 12) The marks are kept by the state a run ends in,
  not its verdict (record 21 section 7.4, decisions M13 and M14): the
  mark service is installed only from this package's `tools\xhcimark\`
  into `system32`, only with `xhcicm` registered and the Thunderbolt
  component in the run, on 2000 to 7; every name is removed, step 0
  first, whenever no `xhcicm` service is left; and the Advanced dialog
  does not yet offer the service's `Restart` (it is set by hand under
  `Services\xhcimark\Parameters`).
- **D17** (revision 13; the owner's request of 2026-10-19, the choices
  below the orchestrator's) The connection manager alone: offered on 2000
  to 7 by `/cmonly` or the window's box, the only install on 8 and 8.1,
  refused on 98 SE and ME; a run without the USB drivers never updates
  or removes an installed `xhci98.sys`, and the way back from the whole
  package is the uninstall and a run with `/cmonly` (section 4.5). Not
  taken: removing the USB drivers in an install run that leaves them out
  (it would take an xHCI-only machine's keyboard without the uninstall's
  warning).
- **D18** (revision 13) Windows 8 and 8.1 admitted for the connection
  manager alone, read through `RtlGetVersion`; 10 and later refused as
  before; the bundled Stor98 not offered on 8 and 8.1 (section 4.5).
  Amended by revision 26 (roadmap-usb4 task 43.43, the owner's decision
  of 2026-10-21): on 8 and 8.1 the USB drivers and Stor98 are offered,
  unticked by default, untested there until roadmap tasks 43.31 and
  43.24 pass, each with record 26 section 5.3.1's warning; this takes
  tasks 43.29's and 43.23's installer rows.
- **D19** (revision 13) The mark service on 8 and 8.1 as on 2000 to 7
  (section 9.5).
- **D20** (revision 37; the owner, 2026-10-22 and 2026-10-23; record 26
  section 5.0, draft 35) Four changes of the tool with the window's
  simplification. (a) The 64-bit tool refuses, before any change, a
  package carrying the connection manager's three files without
  `xhcicm.cat` beside them (`inst_cm_cat_refusal`); a package without
  the connection manager needs no catalog of it, and the x86 tool is
  unchanged. (b) `XhciInterruptMode` and `XhciForceBulkOnly` are no
  longer asked (their `Asked` mask 0: Advanced rows of the window);
  `/set` sets them as before. (c) Stor98's question takes its Enter
  default from `inst_s98_box_default`, one line per system, [Y] on every
  system for now, 8 and 8.1 included (the `is8` exception and its
  warning removed). (d) On 98 SE and ME, with the USB drivers and the UAS
  driver in the run, `xhciuas.sys` is put in `%windir%\SYSTEM32\DRIVERS`
  after the helper's installs, so the New Hardware wizard of a UAS disk
  plugged in later finds it and asks for no disk (the 98 SE reading of
  2026-10-23, record 26 section 5.0); the uninstall's 9x file list
  already removes it.

## 14. Open, and the risks

- The 98 SE and ME route rests on `DevicePath` being searched by the
  wizards; until the legs read it, section 6.2's typed path is what the
  release notes promise.
- The source refresh of an identical INF on 2000 and XP (section 5.6) is
  read on the legs, not assumed.
- `xhcicmf.inf` is a draft pending task 39-0.4 (record 21 section 3.3): the
  tool follows whatever model lines it ships with, and the filter install
  and removal on a tunnel port are not read in any guest here.
- The `XhciCm*` allowed sets follow task 39-A.1's INF and record 21
  section 8 as they stand on 2026-10-15.
- `SystemStartOptions` as the test-signing signal is a reading to take,
  not a documented contract; the tool's note is advice, never a refusal.
- Windows 8 and 8.1 (revision 13) have no guest here: the connection
  manager's install, its filter's (whose INF includes `machine.inf`'s
  `PCI_DRV` sections, read on 2000, XP and 7 only), the driver's imports
  (the import gate holds the x86 driver to Windows 98's evidence and
  Windows 2000's exports and the amd64 driver to NT 5.2's; Vista and 7
  are read in guests; no export table of 8 is in the tree; record 21
  section 3.5) and the mark service are unread there.

## 15. Files

| File | What |
|---|---|
| `xhciinst\xhciinst.c` | the tool: the system half |
| `xhciinst\inst_core.c`, `inst_core.h` | the pure half (section 10) |
| `xhciinst\xhciinst.rc`, `xhciinst.manifest` | the elevation manifest |
| `xhciinst\build.cmd`, `selftest.cmd`, `check-imports.ps1` | the two builds; the self-test and the import check |
| `xhciinst\INSTALL.BAT` | staged at the package root |
| `xhciinst\setup.c`, `setup.rc`, `setup.manifest` | `SETUP.EXE`, staged at the package root (revision 22; record 26 section 2) |
| `xhciinst\README.TXT` | staged as `tools\xhciinst\readme.txt` |
| `test\test_inst.c` | the host suite |
| `scripts\build-driver.cmd`, `scripts\package\make-release.ps1`, `scripts\package\test-package.ps1`, `test\run-host-tests.cmd`, `.gitignore` | wiring |
