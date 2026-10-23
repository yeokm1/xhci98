# Design Record 27 - The Windows 98 SE and ME Device Install

Status: draft 13, 2026-10-23 (the owner's decision of that day, record
26 section 5.0's draft 39: the job file's optional `SECTION` line, with
which the helper installs `xhci98.inf`'s update section, one no model
names, through a driver node `DiCreateDriverNode` makes, sections 4.1 and
5.2; one import more, section 7); draft 12, 2026-10-22 (roadmap-usb4 task 43.5's finding: a
storage unit is taken only with no driver, the "Disk drive" rebind
retired, and the Bulk-Only step admitted only with `USBNTMAP.INF`
findable beside `USBSTOR.INF`, record 26 section 6.5.5; steps 3a and 4a);
draft 11, 2026-10-21 (roadmap-usb4 task 43.42: the installer,
not the helper, places the pack's two storage INFs in `%windir%\INF`
on 98 SE, record 26 section 6.5.11; sections 2 and 8 say where that
stands against the driver index; the helper unchanged); draft 10, 2026-10-20 (roadmap-usb4 task 43.4: on Windows 98 SE
the USB storage drivers of record 26 section 6.5 - Bulk-Only devices
and storage units installed by NUSB 3.3's `USBSTOR.INF` and
`USBNTMAP.INF` from the install directory, two steps of section 6, and
the recorded storage devices removed first, section 9; sections 1.2,
4.1, 8 and 12 to match; the helper itself unchanged); draft 9, 2026-10-19 (the fix round of that night: a helper that
did not finish is an unfinished install with steps, exit 4, never a
failure of the device and never "nothing to do", sections 4.1 and 4.2;
and the installer never runs a job on a device Stor98 runs, record 26
section 6.3 draft 16); draft 8, 2026-10-19 (task 42-A.27, section 6: the settle after a
parent's job
within a pass, and a device a parent's job hid tried once more, record 23
section 5.8 revision 14, from the 98 SE update leg with an external hub);
draft 7, 2026-10-19, revised from the 98 SE smoke readings of
task 42-A.1 (`runs/run-42.md`) with the owner's two decisions of
2026-10-18: the job's operands in a job file, since a 16-bit program's
command tail holds 126 characters (section 4.1); the helper's own choice
of the best-ranked node when the dispatcher answers `DIF_SELECTBESTCOMPATDRV`
with `ERR_DI_DO_DEFAULT` (section 5.2); with them what a cancelled
hardware wizard does to its devnode (sections 5.1 and 6) and what
`LDID_SRCPATH` does not do to the Windows CD's prompt (section 5.3);
draft 6, 2026-10-18, reviewed and revised (the settle-binding
leg); draft 5, 2026-10-18, reviewed and revised (the ownership leg's
existing-copy case); draft 4, 2026-10-18, reviewed and revised (the
probe as the test of the process handle, the timeout per job kind, the
two-versions leg's exception); draft 3, 2026-10-18, reviewed and
revised (the bounded
end of a helper whose process cannot be seen to exit, the class per job,
ME as a hypothesis the probe tests); draft 2, 2026-10-18, reviewed and
revised the same day (every
action through the class-installer dispatcher, the INF's class from
`DiGetINFClass`, an explicit global removal of entries present or not,
the version carried on the command line and a probe first, one wait
loop, a gate before every pass, the Watcom discovery and header
closure, the import library's spellings and ordinals); draft 1,
2026-10-18, before code. Written for `roadmap-usb4.md`
Phase 42 task 42-0.2 (planned as `DI9X.1` until the owner opened the phase
on 2026-10-18), for release `3.1.0.0`; the code is task 42-A.1, the
installer that calls it is design record 26 (task 42-0.1), the legs are
42-V.1. The owner's decision of 2026-10-18 (the decisions table, "Phase
42, the open choices"): the helper is built with Open Watcom 2.0 against
the Windows 98 SE DDK's `SETUPX.H` and `SETUPX.LIB`. Section 13 lists the
decisions taken in writing it, each the orchestrator's for the owner to
confirm unless it names the owner; section 14 what is open.

The owner's rule binds every line of it (record 23 section 6.1): nothing
undocumented. What "documented" means here is section 2.

## 1. What it is for

### 1.1 The problem

On Windows 2000 and later the install tool puts a driver on a device with
`UpdateDriverForPlugAndPlayDevices` (record 23 section 5.5). Windows 98 SE
and ME have no such routine: `newdev.dll` is NT's, and their device
installation is the 16-bit setup engine `SETUPX.DLL` behind the hardware
wizards. Record 23 section 6 therefore stages the package where the
wizards look (`DevicePath` and the local copy) and prints the clicks that
remain, one numbered list per device, and the owner's run on a real
machine (2026-10-16) shaped that list as well as a list can be shaped. It
is still a list: the user installs the controller by hand, restarts,
installs the root hub by hand on an update, and on `/force` or for a UAS
disk walks a Have Disk dialog. Task 42-A.1 puts the driver on the device
from the tool, "the driver put on the controller, its root hub and the
Thunderbolt controller by the tool, in place of the printed Update Driver
steps", and this record says how, with what, and what it may not do.

### 1.2 Scope

- A 16-bit helper program, `XHCIDI.EXE`, that the 32-bit installer starts
  for one device at a time, which calls the setup engine's documented
  device-installer routines to install a given INF's driver on a given
  devnode, or to remove a devnode, and reports what the engine answered
  (sections 4 and 5).
- The devices: the xHCI controller, its root hub, the hubs and composite
  entries the bus presents, a UAS device, the Thunderbolt or USB4 host
  interface (`xhcicm.inf`), and, when the user ticks it, the bundled
  Stor98's NVMe devices (record 26 section 6.3); on Windows 98 SE, when
  the user ticks the USB storage drivers (record 26 section 6.5, draft
  10), a Bulk-Only device with no driver and a storage unit with none
  (draft 12: no longer one at Windows' generic "Disk drive"), by the
  pack's own INFs from the install directory; each a present devnode (section 6).
- The removal of a devnode, present or not, for the uninstall (section 9).
- The toolchain and its place in the build and the cut (section 3), its
  provenance rows (section 3.4), the helper's interface to the installer
  (section 4), what the engine's own bookkeeping then holds (section 8),
  and the legs (section 12).

### 1.3 Not in scope

- Any system but Windows 98 SE (4.10.2222) and ME (4.90.3000): the helper
  refuses to run elsewhere, and the installer never starts it on NT.
- The hardware wizard's own pages, the class installer's dialogs and the
  copy engine's prompts: Windows' own, owned by the installer's window
  (section 4.3), never replaced or suppressed beyond what a documented
  flag does.
- For an install, a device that is not present (an `Enum` entry with no
  devnode, record 23 section 6.3): it stays listed apart, with what to do
  in either case. The uninstall removes such entries (section 9).
- A device another vendor's driver runs: listed and left (record 23
  decision D3).
- The silent install (`XhciSilentInstall`), which is the driver's and the
  user's (record 23 section 8.4): the helper neither needs it nor sets it.

## 2. What is documented, and what the helper may use

The Windows 98 SE DDK (`tools\win98ddk-extracted\`, fetched 2026-10-15,
its identity in `docs\references\README.md`, "Windows 98 DDK") carries
the 16-bit setup engine's public header, `inc\win98\inc16\setupx.h`
(member `INC_WIN98_INC16_SETUPX.H` of `INCS_DDK.CAB`), and its import
library `lib\win98\setupx.lib` (`LIB_WIN98_SETUPX.LIB` of `BINS_DDK`). The
header says of itself "Public include file for Setup services", and its
device-installer half is documented in the header itself: every structure
and flag of the device installer carries a documentation block headed
`EXTERNAL SETUPX DEVICE_INSTALLER` (the DDK's own documentation markup,
from which its reference was generated), naming each field, each flag and
when the engine sets or reads it. The DDK's help file (`HELP_WIN98DDK.CHM`)
carries no page for these routines: its device-installation chapters are
the WDM ones, and the Windows 95 DDK's "Device Installation Functions"
reference, which documented the same interface in prose, is not at hand
(section 14). What the helper rests on is therefore:

1. **The header's documentation blocks** for `DEVICE_INFO`, `DRIVER_NODE`,
   the `DI_*` and `DI_FLAGSEX_*` flags, the `DIF_*` function codes, the
   `ERR_DI_*` errors and the class-install parameter blocks
   (`REMOVEDEVICE_PARAMS` among them). Every flag the helper sets and every
   field it reads is one of those, cited by its block.
2. **The prototypes the header declares** for the routines, with the
   header's own line comments where a block is missing
   (`DiCreateDeviceInfo`'s arguments, `DiGetClassDevs`'s `DIGCF_*` options,
   `DIREG_*`, `DICS_*`), and the import library's public names, each
   bound to an ordinal of module `SETUPX` (the library carries the
   ordinals, not the names, into the image), which cover every routine
   the helper calls: `DiCreateDeviceInfo`, `DiGetClassDevs`,
   `DiGetClassDevsEx`, `DiGetINFClass`, `DiBuildCompatDrvList`,
   `DiSelectBestCompatDrv` (ordinal 524), `DiInstallDevice`,
   `DiRemoveDevice`, `DiCallClassInstaller`, `DiCreateDriverNode`
   (ordinal 309, from draft 13), `DiDestroyDeviceInfoList`,
   `DiDestroyDriverNodeList`, `DiOpenDevRegKey`, the `SUReg*` registry
   routines, `CtlSetLddPath` and `CtlGetLddPath` (most recorded in upper
   case by the 16-bit librarian, `DiSelectBestCompatDrv` and
   `DiGetINFClass` in mixed case; read from the file).
3. **The DDK's own samples**, which show the engine driven exactly this
   way from a 16-bit program: `src\net\ipnew\ipmodem.cpp` and `ipndi.cpp`
   (the network setup's modem and adapter install: `DiCreateDeviceInfo`,
   `DiGetClassDevs` with `DIGCF_ALLCLASSES`, `DiOpenDevRegKey`,
   `DiBuildCompatDrvList`, `lpSelectedDriver = lpCompatDrvList`,
   `DiCallClassInstaller(DIF_INSTALLDEVICE)`, `DiDestroyDeviceInfoList`),
   and `src\plugplay\custom\validate.c` (`DiOpenDevRegKey` with
   `DIREG_DEV`). A sample is not a specification, and the record says so
   where a step rests on one.

Not used, and why: anything the header marks reserved, `BUGBUG`, or "for
use by Setup" (`dwSetupReserved`, `lpReserved`); the driver index
(`DiBuildDriverIndex`, `DiAddSingleInfToDrvIdx`: prototypes with no
documentation block, and a write into the engine's own index, which record
23 section 6.1 already refuses for `INF\OTHER`; draft 11: the pack's two
INFs that record 26 section 6.5.11 has the installer place in
`%windir%\INF` are a file copy of the 32-bit installer, which leaves
the index to the engine as NUSB's own installer does, and no call of
the helper's); `GenInstall` on the INF's
sections by hand (the engine does it inside `DiInstallDevice`, which is
the point); `DiInstallDrvSection` (a class-driver install, not a device's);
and any registry key of the engine's bookkeeping (section 8) written
directly. A reading on the legs that a documented call does not do what
its block says is recorded as such and does not license an undocumented
one; the printed steps of record 23 section 6.4 remain the fallback for
that device (record 26 section 7).

## 3. Toolchain and builds

### 3.1 Why 16-bit

`SETUPX.DLL` is a 16-bit DLL: its routines take far pointers and the
`DEVICE_INFO` of the header, and no 32-bit import of it exists. The DDK's
Win32 `setupapi.dll` of those systems is not the device installer (record
23 section 6.1). A flat thunk, which Microsoft documents for a 16-bit and
a 32-bit DLL of one's own built with its thunk compiler, would put two
more images and a thunk layer into the package to reach the same six
calls, and the undocumented direct thunk entry points were never a
candidate (H2). So the installer, a
32-bit program, starts a 16-bit program, which Windows 98 and ME run in
their system virtual machine beside it, and the two talk through a file
and the process handle (section 4). The helper is small: no window of its
own, no resources but a version string, one job per start.

### 3.2 Open Watcom 2.0, the Win16 target (owner, 2026-10-18)

Open Watcom 2.0 is already a prerequisite of a cut for `XHCIQUAL.EXE`
(`build-and-test.md`, "Prerequisites"), installed to `C:\WATCOM` and
nowhere in the tree, and its install carries the 16-bit compiler and the
Windows 3.x target: `wcc.exe` (the 16-bit compiler), `wlink.exe` and
`wrc.exe` in `binnt64\` or `binnt\` (the owner's install has them in
`binnt64\`),
`h\win\windows.h` and `lib286\win\windows.lib` with the 16-bit C runtime
(`clibs.lib`, `clibm.lib`). The helper is built:

- `wcc` with `-bt=windows` (the Windows 3.x target), `-ml` (the large
  model, because the engine's structures hold far pointers and the header
  declares `FAR` throughout), `-zW` (Windows prologue), `-zu`, `-s`, `-wx`,
  `-zq`, `-oas`; C89, ASCII, CRLF, as everything else here;
- `wlink system windows` with `option stack=16k`, the object files,
  `library` the DDK's `setupx.lib` and Watcom's `windows.lib`; a `.def`
  is not needed, the linker writes the Windows 3.x header;
- the include path: the helper's own directory first, then the DDK's
  `inc\win98\inc16\` for `setupx.h` and `prsht.h` (`setupx.h` includes
  `prsht.h` unconditionally; Watcom's `h\win` has no `prsht.h`, and the
  DDK's includes nothing further, read from the file), then Watcom's
  `h\win` (`windows.h`, `shellapi.h`) and `h` (the C runtime's headers,
  which `xhciqual\build.cmd` points at for the DOS tool). The DDK's
  `inc16\windows.h` is the 98 one; whether the helper compiles against it
  or Watcom's is settled by the build (a conflict with Watcom's runtime
  headers is resolved by include order and recorded in `build.cmd`), and
  the leg reads the result on both systems.
- `xhciinst\build.cmd` builds it as its last step when `%WATCOM%` (or
  `C:\WATCOM`) holds `wcc.exe` under `binnt64\` or `binnt\`, searched in
  `xhciqual\build.cmd`'s order (`BINNT64;BINNT;BINW`), else says the helper
  was not built,
  and `build.cmd x86` includes it; `xhciinst\di\` holds its sources, its
  `makefile` for `wmake` as `xhciqual\makefile` is, and its `build.cmd`
  that sets the environment as `xhciqual\build.cmd` does (the DOS tool
  points `INCLUDE` at `%WATCOM%\H`; this one at the three directories
  above).

**The version** is `src\xhci_version.h`'s, compiled into the helper and
written as the first line of its result file (section 4.2), so an
installer and a helper of different releases refuse each other; the
package carries the helper beside the installer in `tools\xhciinst\`, and
`make-release.ps1` checks its freshness and version as the other tools'.
A 16-bit image has no PE version resource; the check reads the string the
build stamps into it (`XHCIDI <version>` in the image's data, found by the
packager as `scripts\check-flavour-marker.ps1` finds the drivers' marker).

### 3.3 The cut's prerequisites

Open Watcom moves from "used only for the `xhciqual/` DOS qualifier" to
"used for the DOS qualifier and the 98 SE and ME device-install helper"
in `build-and-test.md`'s prerequisites, and the 98 SE DDK's three files
join them: a cut needs `tools\win98ddk-extracted\` to hold `setupx.h`,
`prsht.h` and `setupx.lib` at the SHA-256s `docs\references\README.md`
records, or
`make-release.ps1` refuses with the fetch procedure named. The helper is
not skippable on a real cut (`-SkipDiHelper` exists for a host that cannot
build it, and the directory then says so, as `-SkipQualtool`'s does): a
package without it installs on 98 SE and ME through the printed steps
only, which the release notes would have to say, so a release is cut with
it.

### 3.4 Provenance

`legal-provenance.md` and `docs\references\README.md` gain, the first
two items with this record and the third with the helper's first build:

- a row in `legal-provenance.md` section 2's table for the published
  helper binary, marked not cut yet
  (`releases\<version>\tools\xhciinst\XHCIDI.EXE`): this project's code
  with the Open Watcom 16-bit C runtime statically linked (the Sybase Open
  Watcom Public License, as the qualifier's row), and linked against
  Microsoft's `setupx.lib`, which is an import library (the file's OMF
  records name the module `SETUPX` and its export ordinals and carry no
  code; read from the file), so the image carries import thunks to
  `SETUPX.DLL` of the running system and no Microsoft code;
- in `docs\references\README.md`'s Windows 98 DDK table, the three
  members' sizes and SHA-256s (`INC_WIN98_INC16_SETUPX.H`,
  `INC_WIN98_INC16_PRSHT.H`, `LIB_WIN98_SETUPX.LIB`),
  and a sentence that they are compiled and linked against, not shipped:
  the DDK licence the entry quotes permits the design, development and
  testing of software products with it, and the image contains none of
  its bytes: its import table names the module `SETUPX` and the ordinals
  the library binds, not the library's public symbols;
- a section 2a entry for `XHCIDI.EXE` beside `XHCIQUAL.EXE`'s, recording
  the linkage and the `NOTICE.TXT` `make-release.ps1` generates beside it
  (the Watcom lines from the linker map, as the qualifier's; nothing of
  the DDK's text, since none is reproduced).

The header is read for facts, as the DDK's `configmg.h` was for record 21
(`legal-provenance.md` section 4's row for the configuration manager's
values): a flag value, a structure layout, a routine's name. Nothing of
its text is copied into a tracked file beyond a short verbatim phrase with
its line, as the rule for every third-party document here says.

## 4. The helper's interface to the installer

### 4.1 Starting it

The installer (record 26's engine, on 98 SE and ME only) starts the
helper with `CreateProcessA` from `tools\xhciinst\XHCIDI.EXE` of the
package, or from the install directory's copy for the uninstall (record
26 section 6.2), with one job, written into a **job file** first (owner,
2026-10-18):

```
XHCIDI.EXE <version> <job file>
```

**Why a job file.** A 16-bit program's command line is its MS-DOS command
tail, at most 126 characters: on the 98 SE smoke run of 2026-10-18 (task
42-A.1, `runs/run-42.md`) `CreateProcessA` failed with error 31 for a
helper tail of 127 characters and started it at 126. A PCI device id alone
is 66 characters and a USB one with a serial number can reach
`MAX_DEVICE_ID_LEN` (200), so the job's operands cannot travel on the
command line; only the version and the job file's path do.

**The command line.** Two words separated by one space: `<version>`, the
installer's `XHCI_VER_STR` (at most 31 characters), and `<job file>`, a
drive-rooted path whose every part is 8.3 (record 23 section 9.6's rule,
the characters `inst_name83` takes) of **at most 80 characters**. The
tail is therefore at most 31 + 1 + 80 = 112 characters, 14 under the
bound, whatever the install directory. The installer names the job file
in its install directory (`C:\XHCI98\DIJOB.TXT`, say) when that path fits
in 80 characters, and otherwise in the root of the install directory's
drive (`C:\XHCIDIJ.TXT`); a path longer than 80 characters is refused by
the helper with no result file (exit 3).

**The job file.** Written whole by the installer before the start, read
whole by the helper at its start, and never changed or deleted by the
helper (the installer deletes it after the job, or overwrites it with the
next one). ASCII, at most 2048 bytes, one record a line, each line ending
in CRLF (a bare LF is accepted), each record a key and its operands
separated by one or more spaces or tabs, no operand containing a space.
The records, exactly in this order and each exactly once, keys in upper
case:

```
XHCIDI-JOB 1
JOB <install | remove | probe>
RESULT <result file>
HWND <hwnd>
DEVICE <device id>          install and remove only
INF <inf path>              install only
SECTION <install section>   install only, and optional (draft 13)
FLAGS <flag> [<flag>]       install only, and optional
END
```

(The words after the columns are this record's explanation, not part of
the file.) Nothing but blank lines may follow `END`, and no blank line
may come before it. `XHCIDI-JOB 1` is the format's version: a later
format changes the number. A probe of an installer whose run directory is
`C:\XHCI98` is therefore

```
XHCIDI-JOB 1
JOB probe
RESULT C:\XHCI98\DIRES.TXT
HWND 1048
END
```

and an install

```
XHCIDI-JOB 1
JOB install
RESULT C:\XHCI98\DIRES.TXT
HWND 1048
DEVICE PCI\VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01\BUS_00&DEV_03&FUNC_00
INF C:\XHCI98\XHCI98.INF
FLAGS quiet
END
```

The operands:

- `<version>` (the command line): the installer's `XHCI_VER_STR`. The
  helper compares it with its own before it reads anything of the job but
  the `RESULT` line, and writes `RESULT refused version` on a difference,
  so no job of another release touches a device; and the
  installer runs `probe` once per run, before its first job, which proves
  that the helper starts on this system (its imports resolved by the
  system's `SETUPX.DLL`), its version, the result file's round trip,
  and whether the process handle signals at the helper's exit: the probe
  does nothing but write its file, so a handle still unsignalled 60 s
  after its `END` line means the handle does not signal on this system,
  and the run then uses the helper for no device job at all (every device
  of the plan goes to the printed steps on an install, and an uninstall
  stops before its removals with its recovery state kept), says so, and
  the leg records it (section 12); no device job is ever started while
  the end of the one before it cannot be seen. If the legs find the
  handle does not signal on a target, the record is revised with a
  termination test before the code rests on one, and the printed steps
  remain the route there until then. (The 98 SE smoke run read it
  signalling at the helper's exit, `GetExitCodeProcess` 0.)
- `RESULT <result file>`: a path of at most 255 characters, 8.3 in every
  part (record 23 section 9.6), in the install directory, which the
  installer creates empty before the start and reads after the end; the
  helper truncates and rewrites it, so a stale file is never read as a
  result. The helper looks for this line first, wherever it is in the
  file, because with no usable result path it can report nothing.
- `HWND <hwnd>`: the installer's window handle in decimal (at most 65535,
  no leading zero), or 0 from the console tool. On Windows 98 and ME the
  16-bit and 32-bit window handle spaces are one, so the engine's dialogs
  (the copy engine's "Insert disk", the class installer's questions, the
  signature prompts ME shows) are owned by the installer's window and
  appear in front of it. A 16-bit program may use a handle a 32-bit
  program created on these systems (the smoke run's 844 and 1048 passed
  the helper's `IsWindow`); the leg reads that the dialogs appear owned
  (section 12).
- `DEVICE <device id>`: the devnode's device id as `cfgmgr32.dll` gave it
  to the installer (`PCI\VEN_8086&DEV_...\BUS_00&DEV_14&FUNC_00`,
  `USB\XHCI98_ROOT_HUB\...`), at most `MAX_DEVICE_ID_LEN` (200)
  characters.
- `INF <inf path>`: the INF in the install directory
  (`C:\XHCI98\XHCI98.INF`), at most 255 characters, every part 8.3 and no
  space (the engine's `atDriverPath` is one atom of at most 255
  characters). From draft 10 it may be one of the two storage INFs the
  install directory holds when record 26 section 6.5's row is ticked
  (`C:\XHCI98\USBSTOR.INF`, `C:\XHCI98\USBNTMAP.INF`), unmodified, in
  the same form: the helper treats every INF alike.
- `SECTION <install section>` (draft 13; record 26 section 5.0's draft
  39): the install section to install instead of the one the chosen
  node's model names, from the same INF; 1 to 32 characters, each a
  letter, a digit, `.` or `_`. The installer writes it for the controller
  of `xhci98.inf` alone (`INST_XHCI_UPD_SECTION`, `Xhci.Upd`, the
  Windows 98 SE and ME install with the temporary-name copy row, which no
  model names, so that Windows' own wizard takes `Xhci.Dev`, whose row
  keeps a file already there); the root hub, the hubs, the composite
  parents and every other INF's devices get no `SECTION` line. Section
  5.2 says what the helper does with it.
- `FLAGS`: one or both of `force` (reinstall a current driver: record
  23's `/force` and the Have Disk cases of its section 6.4) and `quiet`
  (section 5.2), lower case, each at most once; the line is left out for
  neither.

The installer waits in one loop: `MsgWaitForMultipleObjects` on the
process handle with a one-second timeout in the window
(`WaitForSingleObject` with the same timeout in the console tool), and at
each tick reads the result file for its `END` line. The outcomes: the
handle signalled and `END` present is a finished job; signalled without
`END` is "the helper did not finish", an unfinished install of that
device with its steps (draft 9; a failure until then); `END`
present with the process still alive is waited for up to 60 s more; a
process still alive after that (the probe having shown the handle does
signal here) ends the helper's part of this run: the job is reported
finished by its file and no further helper job is started. On an
install the remaining devices are listed as steps left with record 23
section 6.4's Update Driver list (record 26 section 7's page) and the
finish says to restart and run the installer again; on an uninstall
the remaining removals are not made, the INF copies, the files, the
records and the entry point stay (record 26 section 9.3's incomplete
uninstall), and the finish says to restart and run the uninstall again,
which resumes from what is left; neither, for as long as the process lives, is a
job under way, which may be a Windows dialog waiting for the user, so that
wait is unbounded and the Progress page says which device the helper is
installing and that a dialog may be waiting behind the window. Whether the
handle `CreateProcessA` returns for a 16-bit program on these systems is
signalled at its exit, and what `GetExitCodeProcess` then says, are
readings of the legs: the result file's `END` line, not the exit code, is
the contract (section 4.2), and a handle that never signals is found by
the probe and keeps the helper out of that run. The helper has no window, so nothing rests on one.

### 4.2 The result file

ASCII, CRLF, one record a line, written in order and flushed, the `END`
line last:

```
XHCIDI 3.1.0.0
JOB install PCI\VEN_8086&DEV_15EC&SUBSYS_...\BUS_00&DEV_0D&FUNC_00 C:\XHCI98\XHCICM.INF
FOUND 1                       the devnode was found in the engine's list
DRIVER XHCICM.INF XhciCm.Install "xHCI98 Thunderbolt 3 Controller" rank 0 date 2026-10-18
CALL DiBuildCompatDrvList 0
CALL DiCallClassInstaller DIF_INSTALLDEVICE 0
FLAGS 00000080                DEVICE_INFO.Flags after the call (DI_NEEDRESTART here)
RESULT installed-restart      one of section 5.4's verdicts
END 8                         the number of lines above it
```

(The words after the columns are this record's explanation, not part of
the file.)

A `CALL` line per engine routine with its `RETERR`; a `RESULT` line with
one verdict word; an `ERROR` line with the engine's error name
(`ERR_DI_*`, from the header's enumeration) and number where a call
failed; `END` with the count. The installer's pure half parses it
(`inst_di_result`, host-tested on every verdict and on a truncated file):
no `END`, a count that differs, a version that is not the installer's, a
`JOB` that is not the one started (the job file's `JOB`, `DEVICE` and
`INF` records, in that order, as section 4.4 says), or an unknown verdict
is "the helper
did not finish", reported as an unfinished install of that device with
the steps page (record 26 section 7, draft 16: the steps flag, exit 4,
and steps that say to shut down, start Windows and run the installer
again, never "nothing to do"). The installer copies every line into its log.

### 4.3 What the helper never does

It never shows a window of its own (its only output is the file), never
writes the registry itself (the engine does, inside its calls), never
touches a devnode other than the one named, never starts a second job,
never retries, and never runs on a system that is not 98 SE or ME (it
reads the Windows version first and writes `RESULT refused` with the
reason), or whose version is not the installer's (section 4.1). A
helper started with a malformed command line or job file writes `RESULT
refused` and the reason, where the job file's `RESULT` line can be read. An installer that finds no helper beside
it (an older or a development package) falls back to the printed steps
of record 23 section 6.4 for every device, saying so once.

### 4.4 The file's details as the code has them (task 42-A.1)

What sections 4.1 to 4.3 leave to the code, as `xhciinst\di\` writes it
(`di_core.c`, host-tested in `test\test_di.c`); nothing above is changed by
it:

- **The order of the checks.** The command line first: two words, the
  job file's path 8.3 and at most 80 characters; then the job file, read
  whole (at most 2048 bytes); then its `RESULT` line, wherever it is. With
  any of those unusable nothing can be written: the helper exits 3 with no
  file. Then the version, before the rest of the job file is read; then
  the job file whole, strictly, in section 4.1's order; then the window
  handle; then the system. `XHCIDI <own version>` is always the first
  line of the result file.
- **The `JOB` line** is `JOB install <device id> <inf path>`, `JOB remove
  <device id>` or `JOB probe`: the job and its operands, never its flags,
  so it compares with the job file's `JOB`, `DEVICE` and `INF` records
  without them. A refusal of the version or of the job has no `JOB` line
  (the job was not read, or not whole); a refusal of the system, and every
  job that ran, has one.
- **The refusals** are `RESULT refused version`, `RESULT refused command
  <what>` (`job-file` for a record missing, out of order, of the wrong
  shape or after `END`, or a format version other than 1; `job`, `hwnd`,
  `device-id`, `inf-path` and `flags` for that record's operand; an `hwnd`
  that is not 0 and names no window is `command hwnd`) and `RESULT
  refused system <VersionNumber>` (the build string of
  `HKLM\Software\Microsoft\Windows\CurrentVersion`, `-` when none was read;
  98 SE is `4.10.2222`, ME `4.90.3000`, and `GetVersion` must also say
  3.95).
- **The probe's verdict** is `RESULT probed`: section 5.4's table has no
  row for it, since the probe calls no engine routine.
- **Three lines beside section 4.2's.** `DEFAULT DIF_SELECTBESTCOMPATDRV
  lowest-rank` follows the `CALL` line of that dispatcher call when it
  answered `ERR_DI_DO_DEFAULT` and the helper chose the node itself
  (section 5.2). `NODE <flags>` (eight hex digits,
  then `force` when the installer asked for it) follows `DRIVER`: the
  chosen node's `DRIVER_NODE.Flags`, so the log shows `DNF_OLDDRIVER` and
  `DNF_CURRENT_DRIVER` beside what was asked. The helper installs the
  chosen node whatever its flags say; whether the engine itself declines a
  same-version install is section 5.2's reading. And a `no-match` is
  preceded by `IDS HardwareID <list>` and `IDS CompatibleIDs <list>`, read
  from the devnode's `Enum` key (section 5.2's "with the ids the helper
  read"). A space or a quote in a bare field is written as `_`.
- **The helper's own failures**, which are no engine error, are written as
  section 5.4's `failed <name> <n>` with the number 0: `XHCIDI_NO_SELECTION`
  (the class installer's choice left no selected driver),
  `XHCIDI_OTHER_INF` (the chosen node's INF is not the one given, which
  `DI_ENUMSINGLEINF` should make impossible) and `XHCIDI_NO_ATOM` (the
  INF's path could not be made an atom). Each has an `ERROR` line with the
  same name before it. An engine error name the header does not enumerate
  is `ERR_UNKNOWN`.
- **Exit codes**, which nothing rests on: 0 a job ran to a verdict, 1
  refused, 2 a line could not be written, 3 no result file.

## 5. The install, call by call

### 5.1 Finding the devnode

`DiGetClassDevsEx(&list, NULL, <enumerator>, hwnd, DIGCF_PRESENT |
DIGCF_ALLCLASSES)`, the enumerator being the device id's first part
(`PCI`, `USB`), builds a list of `DEVICE_INFO`s for the present devices of
that enumerator (the header: `DIGCF_PRESENT`, `DIGCF_ALLCLASSES`; the
network sample uses `DiGetClassDevs` with `DIGCF_ALLCLASSES` and walks the
list). The helper walks it for the one whose `szRegSubkey` is
`Enum\<device id>` (compared without case: the field is "the device's
hardware registry subkey ... usually some place in the \ENUM branch", the
header's block), takes its `dnDevnode`, `hRegKey`, `szRegSubkey`,
`szClassName` and `szDescription`, and builds a `DEVICE_INFO` of its own
for it with `DiCreateDeviceInfo(&lpdi, szDescription, dnDevnode, hRegKey,
szRegSubkey, class, hwnd)`, the class being the INF's for an install
(below) and the entry's own `szClassName` for a removal (section 9,
which has no INF), the sample's way of taking one entry
out of the list (`ipmodem.cpp`: the found device's fields passed to
`DiCreateDeviceInfo`), then destroys the list (`DiDestroyDeviceInfoList`).
Not found: `FOUND 0`, `RESULT not-present`, and the installer treats the
device as record 23 section 6.3's "recorded, not running now".

**A cancelled hardware wizard** (read on the 98 SE smoke run of
2026-10-18): when the user cancels the Found New Hardware wizard of a
device, 98 SE takes the devnode out of the tree for the rest of that boot
(its `Enum` entry stays, class `Unknown`; the wizard opens again at the
next start), so the present list does not hold it and the helper answers
`not-present`. Taking the wizard to its end instead, Next until "Windows
has not installed a driver for this device" and Finish when it finds
none, leaves the devnode present at problem 28 with class `Unknown`, and
the helper installs it (below). The installer therefore asks for the
wizards to be finished, not cancelled (section 6), and a device of the
plan the helper answers `not-present` for is listed as section 5.4 says:
a step that says to restart, finish that device's wizard when it opens,
and run the installer again.

**The class.** A devnode with no driver yet has the class its enumerator
gave it, often none or `Unknown`; the INF says which class its driver
installs. The helper asks the engine for the INF's class with
`DiGetINFClass(<inf path>, INFTYPE_TEXT, class, sizeof class)`, declared
in the header as "API to return the Class name of an INF File", and
passes that class to `DiCreateDeviceInfo` for the devnode's entry, as the
DDK's network sample passes an explicit class name when it creates the
`DEVICE_INFO` for a device it is about to install (`ipndi.cpp`), the
other fields taken from the list's entry. The 98 SE smoke run of
2026-10-18 read this route installing `xhci98.inf` (`DiGetINFClass`
answered `USB`) on the controller listed under `Unknown` at problem 28:
the devnode's `Enum` key then said `Class` `USB` and `Driver` `USB\0002`.
The 42-V.1 legs read it again for every device and on ME; the fallback is
record 23 section 6.4's steps.

### 5.2 Choosing the driver

- `lpdi->atDriverPath = GlobalAddAtom(<inf path>)` and
  `lpdi->Flags |= DI_ENUMSINGLEINF`: "Set if DiBuildCompatDrvList and
  DiBuildClassDrvList should only search the INF file specified by
  atDriverPath" (the header's flag block). So the compatible list is built
  from this package's INF alone, and no other INF's driver can be chosen
  by a rank the helper did not ask for.
- `lpdi->hwndParent = hwnd`; `DI_QUIETINSTALL` with the `quiet` flag
  ("Set if device install API should be as silent as possible using
  default choices wherever possible"), which the installer gives for every
  device so the engine asks nothing it can decide; the copy engine's
  "Insert disk" and the Windows CD prompt for `usbd.sys` are not choices
  and still appear (section 5.3).
- `DiBuildCompatDrvList(lpdi)`: the engine matches the devnode's hardware
  and compatible ids against the INF's models and fills `lpCompatDrvList`,
  one `DRIVER_NODE` per match with its `Rank`, `lpszSectionName`,
  `lpszDescription`, `atInfFileName`, `DriverDate` and `Flags`
  (`DNF_OLDDRIVER` marks the installed driver's node, `DNF_CURRENT_DRIVER`
  the current one; the header's `DRIVER_NODE` block). An empty list is
  `RESULT no-match` with the ids the helper read: the installer's own
  match (record 23 section 5.3's rule, pure) said the INF matches, so a
  disagreement is reported as such, never forced.
- `DiCallClassInstaller(DIF_SELECTBESTCOMPATDRV, lpdi)`: the class
  installer of the device's class is given the choice first. The record
  assumed that the engine's default would make the choice when the class
  installer answers `ERR_DI_DO_DEFAULT` or there is none (the header's
  `DI_NODI_DEFAULTACTION` block: "Set if DiCallClassInstaller should not
  perform any default action if the class installer return
  ERR_DI_DO_DEFAULT, or there is not class installer"). It does not, for
  this function: on the 98 SE smoke run of 2026-10-18 the dispatcher
  returned 506, `ERR_DI_DO_DEFAULT`, for the controller with `xhci98.inf`
  (class `USB`), `DI_NODI_DEFAULTACTION` clear, and selected nothing. The
  block promises no default for any one function, and
  `DiSelectBestCompatDrv` is declared with another shape (it returns a
  `LPDRIVER_NODE` and takes a current node), so this reads as the
  dispatcher having no default for `DIF_SELECTBESTCOMPATDRV`, not as a
  default that failed. **On `ERR_DI_DO_DEFAULT` the helper selects the
  node itself** (owner, 2026-10-18): the node of the best rank in
  `lpCompatDrvList` ("Ranks go from 0 to n, where 0 is the most
  compatible", the `DRIVER_NODE` block), the first of equal ranks, into
  `lpSelectedDriver`, as the DDK's network sample selects from the
  compatible list itself (`lpSelectedDriver = lpCompatDrvList`), and
  writes `DEFAULT DIF_SELECTBESTCOMPATDRV lowest-rank` after the call's
  `CALL` line. With `DI_ENUMSINGLEINF` every node is this package's INF.
  The same run read the install that followed succeed (section 5.3). Any
  answer other than 0 or 506 is the call's failure (`ERR_DI_USER_CANCEL`
  is `cancelled`). `lpSelectedDriver` is then read. The helper checks the chosen node's `atInfFileName` names the given INF
  (with one INF in the search it must) and writes the `DRIVER` line from
  the node. With `force`, a node flagged `DNF_OLDDRIVER` or
  `DNF_CURRENT_DRIVER` is still installed (the engine's "do not upgrade
  the current driver", `ERR_DI_DONT_INSTALL`, is answered by the installer
  having decided already, record 23 section 5.4; the header lists the
  error, and whether `DiInstallDevice` returns it for a same-version
  install without the wizard's "better driver" test is a reading of the
  legs, with the Have Disk steps the fallback).
- **The job's `SECTION`** (draft 13). With a `SECTION` line whose name is
  not the chosen node's `lpszSectionName` (compared without case), the
  helper makes a node for it with `DiCreateDriverNode` (the header's
  declaration: rank, INF type, INF date, the device and driver
  descriptions, the provider and manufacturer names, the INF file and
  the section, and the private data), every field the chosen node's but
  the section, the provider and manufacturer read from the node's atoms;
  sets the new node's `Flags` and `DriverDate` to the chosen node's;
  makes it `lpSelectedDriver`; writes a `CALL DiCreateDriverNode
  <section> <err>` line and a second `DRIVER` line from the new node, and
  checks its INF is the job's again. The node is on no list, so the
  device info's destruction does not free it; the helper ends after the
  job. A failure of the call is `failed` with its error; no node with no
  error is `failed XHCIDI_NO_SECTION_NODE 0`. Read on 98 SE (2026-10-23):
  the controller's update over 3.1.0.0 installed `Xhci.Upd`, and its
  driver key reads `InfSection = Xhci.Upd`, `InfPath`, `ProviderName`,
  `DriverDesc`, `MatchingDeviceId` and `DriverDate` as an install of
  `Xhci.Dev` writes them.

### 5.3 Installing

- The copy source. The 98 SE leg of record 23 (section 6.2 item 3) read
  the wizard taking the INF from the copy and then asking for the "xhci98
  xHCI Host Controller Disk" with "Copy files from" set to the Windows
  source path, not the INF's directory. The engine resolves a disk of
  `[SourceDisksNames]` to a path through its logical disk ids, and
  `LDID_SRCPATH` (1, "source of installation", the header) is the one the
  prompt starts from. Before the install the helper sets it to the INF's
  directory with `CtlSetLddPath(LDID_SRCPATH, dir)` after saving the old
  path with `CtlGetLddPath`, and puts it back after (both routines are the
  header's `Ctl*` logical-disk set, declared with their arguments). Read on
  the 98 SE smoke run of 2026-10-18: the saved path was `D:\WIN98\` (the
  Windows setup source), the package's own `xhci98.sys` was copied from
  `C:\XHCI98` with no prompt for the package's disk, and the Windows CD
  prompt for `usbd.sys` still came, its "Copy files from" set to
  `D:\WIN98`: **`LDID_SRCPATH` does not change the default path of the
  Windows CD's prompt**, which names the Windows CD's own disk. Whether
  the package's files would have prompted without it is read on the legs
  (section 12), and the helper keeps setting and restoring it until then.
  The CD prompt is Windows' own, owned by the installer's window; the
  installer's page before the first job says that Windows may ask for its
  CD and what to type when the path it offers is not the CD's (the CD-ROM
  drive's `\WIN98`, or wherever the CD's `WIN98` directory was copied);
  the files then come from the directory typed, as today.
- `DiCallClassInstaller(DIF_INSTALLDEVICE, lpdi)`, the dispatcher the
  DDK's samples call: the class installer runs first and the engine's
  `DiInstallDevice` is the default action it falls to (`ERR_DI_DO_DEFAULT`,
  or no class installer; `DI_NODI_DEFAULTACTION` clear). The default
  copies the files of the chosen node's section (through its copy engine, asking for the Windows CD for
  `usbd.sys` and `usbui.dll` by `LayoutFile`, as every install here does),
  writes the driver key and the devnode's `Driver` value, and restarts the
  device through the configuration manager, unless `DI_DONOTCALLCONFIGMG`
  is set, which the helper never sets (the network sample sets it because
  its devices are not present yet; ours are, and the restart is what
  makes the driver start without a reboot). `DI_NOFILECOPY` and
  `DI_FORCECOPY` are not set: the engine's version conflict rules decide
  (`suVerConflict` is its own), and a `force` install goes through the
  copy engine as the wizard's Have Disk does.
- After the call: `Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)` ("flags
  returned by DiInstallDevice to indicate need to reboot/restart", the
  header) into the `FLAGS` line and the verdict; `RETERR` 0 is installed,
  `ERR_DI_USER_CANCEL` is cancelled (the user closed a Windows dialog),
  anything else is failed with the error's name.
- `DiDestroyDeviceInfoList(lpdi)`, `GlobalDeleteAtom`, the logical disk
  path restored, `END`.

### 5.4 The verdicts

| `RESULT` | Meaning | The installer |
|---|---|---|
| `installed` | the install call returned 0, no restart flag | record 23 section 5.9's check on the devnode (record 26 section 8) |
| `installed-restart` | 0, with `DI_NEEDRESTART` or `DI_NEEDREBOOT` | "installed, restart pending"; the restart flag |
| `cancelled` | `ERR_DI_USER_CANCEL` | the device's steps page (record 26 section 7), exit 4 |
| `no-match` | an empty compatible list | the steps page, and the ids in the log |
| `not-present` | the devnode is not in the present list (a device whose hardware wizard was cancelled this boot among them, section 5.1) | record 23 section 6.3's "recorded, not running now", and for a device of the plan a finish-page step: restart, finish its wizard, run the installer again (section 6) |
| `failed <ERR_DI_name> <n>` | any other `RETERR`, with the call that returned it | the steps page; a failed controller stops the devices below it, as record 23 section 5.1 does on NT |
| `refused <reason>` | the helper did not start the job (the system, the job file, the version), before any call | a failure of the run's 98 SE and ME route, said once |

## 6. The devices, in order

The installer's plan (record 23 sections 5.4 and 6.3) decides what is
installed; the helper only does what it is told, one devnode at a time,
in this order, and before every pass, the first included, the gate of
step 2: the tree read and settled, and the user's Next on a page that
says to finish any hardware wizard already open (Next until its end, and
Finish where Windows "has not installed a driver"), so a controller whose
own wizard is open when the installer starts is never installed under it.
The page says not to cancel a wizard, because a cancelled one removes its
device until the next start (section 5.1); a device the helper then
answers `not-present` for is a step of the finish page (restart, finish
its wizard when it opens, run the installer again), and the rest of the
plan goes on.

1. **The controller** (`PCI\CC_0C0330` among its ids, `xhci98.inf`). Its
   install starts `xhci98.sys`, which enumerates the root hub as a new
   devnode. On a fresh install nothing of the package is below it yet; on
   an update the root hub, hubs and composite entries keep their devnodes
   (record 13 section 10.13.4: new ids from `2.4.0.0`, new devnodes from
   `2.3.x`).
2. **The settle.** Windows' own hardware detection finds the new devnodes
   and opens its wizards for them, or installs them silently where the
   silent install is on. The installer waits as record 23 section 5.8
   waits on NT, reading the tree through `cfgmgr32.dll` until it stands
   still (the quiet stretch, the bound), and says what to do with the
   wizards (record 26 section 7). It never installs a device while a
   wizard may hold it: Windows 98 has no "installation in progress"
   question to ask, so the quiet tree and the user's word (the page's
   Next) are the gate, and a `DiInstallDevice` that meets the engine's own
   install of the same devnode is reported as that call's error, not
   retried (record 23's three-tries rule is NT's `0x20`; here the second
   try is the next run).
3. **The root hub, the hubs and the composite entries** that still have no
   driver (a wizard finished with none, which leaves the devnode present;
   a cancelled one is `not-present`, section 5.1) or an older one (an update: the 9x leg of
   2026-10-15, where the root hub otherwise stayed at the old release),
   each by `xhci98.inf`, repeated while new targets appear, at most four
   passes as on NT, each pass after a settle. Within a pass a job on the
   root hub, a hub or a composite entry is followed by a settle before the
   next job (its reinstall restarts it and takes the devices below it away
   for seconds: the 98 SE update with an external hub, 2026-10-19), and a
   device the helper answers `not-present` for after a parent's job of the
   run is waited for, the tree settled, and the job made once more before
   it is a step (record 23 section 5.8, revision 14).
3a. **Bulk-Only devices** (draft 10; on 98 SE, only when record 26
   section 6.5's row is ticked and its case allows a copy, and (draft 12)
   only while the install directory holds `USBNTMAP.INF` and the INF
   directories hold no `USBSTOR.INF` without it, record 26 section
   6.5.5): each present
   devnode with a Bulk-Only id (`USB\Class_08&SubClass_02&Prot_50`,
   `SubClass_05`, `SubClass_06`) that no INF of this package matches and
   that has no driver, by `USBSTOR.INF` from the install directory, after
   the passes of step 3 and their settle. The storage files are already
   in place (record 26 section 6.5.4), so the engine's copy writes the
   same bytes.
4. **UAS devices** (`xhciuas.inf`) present, install or update; on 98 SE
   the NUSB mass-storage prerequisite and on ME the first-stick advice
   stay as record 23 section 6.4 says them, on the Found page; on 98 SE
   with the storage row ticked, `USBNTMAP.SYS` is in place before this
   step, so a UAS disk installed here starts with its filter.
4a. **Storage units** (draft 10; on 98 SE, as step 3a): after a settle,
   each present unit (`USBSTOR\GenDisk`, `USBSTOR\GenCdRom` or
   `USBSTOR\GenSfloppy` among its ids) with no driver, by
   `USBNTMAP.INF` from the install directory, "USB Disk" (draft 12: a
   unit bound any way is left; record 26 section 6.5.5); then a
   settle and the read-back, a unit bound but not started given the
   replug step. A unit that appears in a later settle of the run is
   taken the same way.
5. **The Thunderbolt or USB4 host interface** (`xhcicm.inf`) when the
   component is ticked; its own devices (the bridges a tunnel brings up)
   are `xhcicm.sys`'s to number (task 42-A.14 and record 28 for what is
   still open there), not the helper's. Its start may bring tunnelled
   devices up and open their wizards, so the settle of step 2 follows it
   before anything else is installed.
6. **Stor98's NVMe devices**, after that settle, when the component is
   ticked (record 26 section 6.3): those with no driver, and those Stor98
   runs at an older release than the bundled one, by `stor98.inf` from the
   install directory, with the same version rule and read-back as the
   package's own devices.

A USB keyboard and mouse behind the controller stop at step 1 and come
back when the root hub, their hub and their HID function have their
drivers, which on a fresh install means the wizards of step 2 (the input
warning of record 23 section 5.8, on the Found page and the summary) and
on an update from `2.4.0.0` means the controller's restart alone (the
devnodes keep their drivers; read on the 98 SE update leg of record 23,
where only the root hub needed the hand step). The helper cannot shorten
that gap, and the record does not claim it does; what it removes is the
hand step for every device the installer reaches.

## 7. ME

The same calls, bound by ordinal: the import library names each routine's
ordinal in module `SETUPX`, so the helper loads on a system only where
that system's `SETUPX.DLL` exports those ordinals, and one whose does not
refuses to load the image before any call. That ME's `SETUPX.DLL`
exports the same ordinals with the same behaviour is a hypothesis, not a
reading and not anything the 98 SE DDK says (it is the only DDK at hand
and says nothing about ME; ME's own is not here): the `probe` job's
clean run on ME is the first check, and the ME leg reads every row of
section 12 again before the record claims it. `DiCreateDriverNode` (ordinal 309, draft 13) is one more
such ordinal: the `probe` job's clean run on ME proves ME's `SETUPX.DLL`
exports it, and the ME leg of the update section reads its install. The differences record 23 recorded are the wizard's
(the "Select Other Driver" list naming a driver twice, `INF\OTHER`),
which the helper does not open. ME's digital-signature prompt for an
unsigned driver, where ME shows one, is Windows' own dialog owned by the
installer's window.

## 8. What the engine's bookkeeping then holds

The point of going through `DiInstallDevice` is that the result is what
the wizard would have left, so Device Manager, the next Update Driver and
the uninstall see an ordinary install. What that is, read on the 98 SE and
ME legs of record 23 and to be read again under the helper:

- the devnode's `Enum` key: `Driver` naming the class and the driver key
  (`USB\0003`), `Class`, `DeviceDesc` from the INF, `Mfg`, `ConfigFlags`;
- the driver key under `HKLM\System\CurrentControlSet\Services\Class\
  <class>\<nnnn>`: `InfPath` (the engine's copy's name), `InfSection`,
  `ProviderName`, `DriverDesc`, `DriverDate`, `NTMPDriver` for a WDM
  driver (the value record 23 section 6.3 binds a devnode to a group by),
  `DriverVersion` where the INF's `DriverVer` gives one, and the INF's
  `AddReg` values (the `Xhci*` settings at their defaults, record 23
  section 8.4);
- the engine's copy of the INF in `%windir%\INF\OTHER` under the
  provider-prefixed name, and its `OEM Name` record under
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Setup\SetupX\INF\OEM
  Name` (`REGSTR_PATH_INF_OEMNAME`, record 23 section 6.1), which the
  engine writes for an INF installed from outside `%windir%\INF`; whether
  an INF found through `DevicePath` is copied there or indexed in place is
  a reading (record 23's ME leg saw a copy under `INF\OTHER`);
- the files in `%windir%\SYSTEM32\DRIVERS` (`xhci98.sys`, `xhciuas.sys`,
  `xhcicm.sys`) and `SYSTEM` (`usbui.dll`), copied by the engine from the
  install directory and the Windows CD.

The installer reads these after the helper as record 23 section 6.3 reads
them, and its uninstall removes the `INF\OTHER` copies whose provider is
this package's (record 26 section 9.3 step 4) after the devnodes are gone.
Nothing here is written by the helper or the installer directly.

The storage INFs (draft 10) leave the same kind of bookkeeping under
their own provider, Microsoft's: a copy in `INF\OTHER` and its `OEM Name`
value, a driver key of class `USB` (`USBSTOR.INF`, `NTMPDriver`
`USBSTOR.SYS`) or `Storage` (`USBNTMAP.INF`, `DevLoader` `*IOS`,
`PortDriver` `USBMPHLP.PDR`). Their provider is not this package's, so
the rule above never reaches them; record 26 section 6.5.6 records each
copy made for the installer by its exact `OEM Name` value, and section
6.5.8 removes only those. Which name the engine gives the copy is read on
43.5's legs. From draft 11 the same two INFs may also be in
`%windir%\INF` under their own names, placed there by the installer
(record 26 section 6.5.11): a device Windows then installs from one of
them at its first plug is expected to carry the bare name (`USBSTOR.INF`)
as its `InfPath`, with no `INF\OTHER` copy and no `OEM Name` value, which
43.5's first leg, taken again, reads. The helper's own storage jobs still
name the install directory's copy.

## 9. Removal, for the uninstall

A `remove` job (section 4.1: `JOB remove`, `RESULT`, `HWND`, `DEVICE`): the
devnode's entry is taken from `DiGetClassDevsEx` without `DIGCF_PRESENT`
(`DIGCF_ALLCLASSES` alone), so a device of the package that is not
connected now, a UAS disk unplugged or the root hub of a controller that
moved slot, is found by its registry entry as a present one is, which is
what the uninstall needs (record 26 section 9.3: no devnode of the
package, present or not, may remain before the INF copies go). Then a
`REMOVEDEVICE_PARAMS` block with `cbSize`, `dwFlags = DI_REMOVEDEVICE_GLOBAL`
and `dwConfigID` 0, attached through `lpClassInstallParams` with
`DI_CLASSINSTALLPARAMS` set (the header's block for the structure, and the
flag's: "Set if the lpClassInstallParams field points to a class install
parameter block"), and `DiCallClassInstaller(DIF_REMOVE, lpdi)`, the class
installer first and the engine's `DiRemoveDevice` as the default action.
Children first is the installer's order (record 26 section 9.3 step 4):
on 98 SE the storage units and then the Bulk-Only devices the installer
recorded or adopted (record 26 section 6.5.8, draft 10; adopted since
record 26 draft 18), UAS devices,
Stor98's recorded devices, the host interface, hubs and
composite entries, root hubs, controllers. Whether the engine lists an
absent device's entry without `DIGCF_PRESENT`, whether `DIF_REMOVE` on it
deletes its `Enum` and driver keys, and whether the removal of a
controller takes its subtree as Device Manager's Remove does ("it removes
the root hub and everything beneath it", record 23 section 9.4's steps),
are readings of the legs. Until they are read the installer removes each
devnode it knows one by one, reads the tree and the `Enum` branch between
removals (a devnode gone with its parent is not an error), and an entry
still there after its removal, present or not, stops the destructive
steps after it (the INF copies and the files stay) with an incomplete
uninstall (exit 4) and the recovery state record 26 section 9.3 keeps, so
a later run resumes. The verdicts: `removed`, `removed-restart` (the
flags), `cancelled`, `not-found`, `failed <ERR_DI_name> <n>`.

After the removals the engine's bookkeeping of section 8 is what a
removal leaves (read on the legs): the devnode and its `Enum` key gone,
the driver key gone, the files and the `INF\OTHER` copy still there; the
installer
removes the copies and the files as record 26 section 9.3 says, so the
next start's hardware detection finds the controller and nothing to
install it with, which is the machine as it was before the install
(owner, 2026-10-18). `CM_Uninstall_DevNode` of `cfgmgr32.dll`, a stub on
both systems (record 13 section 10.13.8), is not touched.

## 10. The console tool

Uses the helper too (task 42-A.1: "the console tool uses it too"): the
same engine call per device, `hwnd` 0, the engine's dialogs then owned by
the desktop, which the console says before the first call ("a Windows
dialog may open; answer it"). Its 98 SE and ME route keeps the printed
steps for a device the helper could not install, in record 23 section
6.4's form, and the `/list` run says, per device, that the helper would
install it. `/yes` and `/defaults` run the helper too: an unattended 98 SE
install still meets the engine's own prompts where they come (the Windows
CD), which is no different from today's wizard and is said in the readme.

## 11. The pure half and the host suites

In `xhciinst\inst_core.c` (host-tested in `test\test_inst.c`):

- the helper's job file and command line (section 4.1: the records in
  their order, the 80-character bound on the job file's path, the 8.3 and
  length checks on the paths and the id);
- the result file's parser and the verdict table of section 5.4, with
  cases for every verdict, a truncated file, a wrong version, a wrong job,
  an unknown word, an `END` count that differs;
- the devices' order of section 6 and the passes' rule (the NT settle's
  state machine, `inst_settle_*`, reused);
- the steps-page fallback decision (which verdicts send a device to record
  23 section 6.4's list).

In `xhciinst\di\di_core.c`, the helper's own pure part, C89 with no
Windows header and no far pointer, compiled by Watcom into the helper and
by the host suite (`test\test_di.c`) under MSVC: the parser of the
command line and the job file,
the result file's writer (line by line into a caller's buffer), the
`RETERR` to error-name table from the header's enumeration (checked by the
suite against the header's values, read at build time from
`tools\win98ddk-extracted\` where it is present, else from a recorded
copy of the numbers in the suite with the header's SHA-256 beside them),
and the verdict rule. The system part, `di_sys.c`, is the calls of section
5 and nothing else, and it is read, not unit-tested: its proof is the legs.

## 12. The legs (42-V.1)

On the 98 SE and ME guests of `build-and-test.md`, from a package staged
by `make-package.ps1`, each leg recording the helper's result files, the
installer's log, `regedit /e` of the devnode's `Enum` key and its driver
key, `INF\OTHER`'s listing, and Device Manager:

| Leg | Reads |
|---|---|
| F, fresh install | the controller installed by the helper from the Found page's plan with no hand step; the Windows CD prompt for `usbd.sys` owned by the installer's window; the root hub's and hubs' wizards after the controller's restart, and the settle's wait for them; a root-hub wizard finished with no driver then installed by the helper, and a cancelled one answered `not-present` and listed as the restart-and-run-again step (section 5.1); the devnode's class after the install; whether `LDID_SRCPATH` spares the package disk's prompt (the Windows CD's keeps its own default path, section 5.3); the driver key's values of section 8 |
| U24, update over `2.4.0.0` | the controller and the root hub updated by the helper with no hand step (the hand step record 23's 9x leg needed); the keyboard and mouse behind the controller back after the restart of the devices, not of Windows |
| U23, update over `2.3.1.0` (98 SE) | the new devnodes and their wizards; the helper's pass over what a wizard finished with no driver; a cancelled one's `not-present` and its step |
| UAS and Stor98 | a UAS disk installed and `force`-reinstalled (the `ERR_DI_DONT_INSTALL` question); Stor98 ticked on an NVMe device with no driver, installed from the copy, its IOS binding read back; an older Stor98 updated with the shutdown afterwards (record 26 section 6.3); two bundled versions in succession, then the uninstall removing every owned staged copy no device names and leaving a hand-installed Stor98's device with the copy its driver key names, and an `INF\OTHER` copy of a hand install whose device was removed before the run, which stays; an NVMe device Windows binds to Stor98 by itself during the settle after the host interface's install, with no Stor98 helper call, recorded with the copy the engine made for it, and the uninstall removing that copy once unreferenced (record 26 section 6.3) |
| Thunderbolt | the host interface by `xhcicm.inf` on the emulator's Alpine Ridge guest (the 98 SE wizard's "Copy files from" of record 23 section 8.5, now the helper's) |
| UN, uninstall | each devnode removed through the helper in order, an unplugged UAS disk's entry and a moved controller's old entry among them (the removal of an entry that is not present); the subtree removal; the `INF\OTHER` copies and the files deleted by the installer; the restart; the controller back as an unknown device with no wizard finding a driver |
| Probe | the `probe` job on 98 SE and ME: the helper loads (its ordinals resolved), writes its version, and refuses a job with another version before any call |
| ME | every row above on ME, and ME's own prompts |
| Storage (98 SE, draft 10; roadmap-usb4 task 43.5) | a Bulk-Only stick at "Unknown Device" and a unit with no driver installed by the pack's INFs from the install directory, the unit's "USB Disk" and whether it starts without a replug, and a unit already bound any way left as it is (draft 12); the `INF\OTHER` copies and `OEM Name` values the engine makes for them; whether the engine copies over a file of the same bytes, or one in use; the removal of the recorded storage devices before the UAS devices |
| Dialogs and handles | that the engine's dialogs are owned by the installer's window; that the process handle of the 16-bit helper signals at its exit and what its exit code reads; the fallback poll of the result file |

What the 98 SE smoke run of 2026-10-18 read before the legs (task 42-A.1,
`runs/run-42.md`): the probe and the 16-bit process handle signalling at
the helper's exit; the 126-character command tail (section 4.1); a
cancelled wizard's devnode leaving the tree (section 5.1); the install of
`xhci98.inf` on the controller's `Unknown` entry through
`DiCreateDeviceInfo` with the INF's class, with `DI_ENUMSINGLEINF` and the
INF outside `%windir%\INF`; `ERR_DI_DO_DEFAULT` from
`DIF_SELECTBESTCOMPATDRV` (section 5.2); `LDID_SRCPATH` and the Windows
CD's prompt (section 5.3); the engine's copy `YEOKHE~1.INF` under
`INF\OTHER`. Each is read again on the legs with the helper as it now
is, on 98 SE and ME. What the legs settle that nothing here has read: the
other devices' installs and ME's; the listing and removal of an entry
that is not present; `ERR_DI_DONT_INSTALL` on a same-version install;
the subtree removal of `DiRemoveDevice`; whether `LDID_SRCPATH` is what
spares the package disk's prompt; and where the engine copies an INF
found through `DevicePath`.

## 13. Decisions taken in writing this (the orchestrator's, owner to confirm)

- **H1** (owner, 2026-10-18) Open Watcom 2.0's Win16 target against the
  98 SE DDK's `setupx.h` and `setupx.lib`; the large model (section 3.2).
- **H2** A separate 16-bit executable, one job per start, a result file
  and the process handle as the interface; no thunk, no 16-bit DLL loaded
  into the installer (section 4). Not taken: a flat thunk pair
  (documented for a 16-bit and a 32-bit DLL of one's own through the
  thunk compiler, but two more images and a thunk layer for the same six
  calls; the undocumented direct thunk entry points were never a
  candidate), or
  `rundll.exe setupx.dll,InstallHinfSection` (installs an INF section, not
  a driver on a devnode; the DDK's own INFs use it for an uninstall
  section, and record 26's uninstall does not need it).
- **H3** The devnode is taken from `DiGetClassDevsEx`'s list (present
  devices for an install, every entry for a removal) and rebuilt with
  `DiCreateDeviceInfo`, with the INF's class from `DiGetINFClass` for an
  install and the entry's own class for a removal, the DDK sample's way; the helper never fills `DEVICE_INFO` from a device id by
  hand (sections 5.1 and 9).
- **H4** `DI_ENUMSINGLEINF` with the package's INF, `DI_QUIETINSTALL`,
  every action through `DiCallClassInstaller` (`DIF_SELECTBESTCOMPATDRV`,
  `DIF_INSTALLDEVICE`, `DIF_REMOVE`) with the engine's default behind the
  class installer, and where the dispatcher has no default
  (`ERR_DI_DO_DEFAULT` for `DIF_SELECTBESTCOMPATDRV`, read on 98 SE) the
  helper's own choice of the best-ranked node (owner, 2026-10-18); never
  `DI_NOFILECOPY`, `DI_FORCECOPY` or `DI_DONOTCALLCONFIGMG` (sections 5.2,
  5.3 and 9).
- **H5** `LDID_SRCPATH` set to the INF's directory for the install and
  restored after; the package's files came from that directory with no
  prompt on the 98 SE smoke run, and the Windows CD's prompt, which still
  comes with its own default path, is Windows' own, the installer's page
  saying what to type (section 5.3).
- **H6** The installer never installs under a wizard it cannot see: a
  settle and the user's Next gate every pass after the controller (section
  6; record 23 decision D13 on these systems).
- **H7** Removal through `DIF_REMOVE` with an explicit global
  `REMOVEDEVICE_PARAMS`, entries present or not, children first, the
  controller last; an entry left stops the uninstall's destructive steps;
  the INF copies and the files removed by the installer after (section 9).
- **H8** The helper is a cut prerequisite with the qualifier; a package
  without it falls back to the printed steps and the release notes say so
  only for a `-SkipDiHelper` package (section 3.3).
- **H9** The result file is the contract; the exit code and the handle's
  signal are readings; the version travels on the command line and a
  probe runs before the first job (sections 4.1 and 4.2).
- **H10** (owner, 2026-10-18) The job's operands travel in a job file of
  section 4.1's format, its path at most 80 characters on the command
  line beside the version, because a 16-bit program's command tail holds
  126 characters (98 SE smoke run of 2026-10-18). Not taken: the operands
  shortened to fit (a USB device id alone can reach 200 characters), the
  environment (a 16-bit task's is a copy made at its start, with no
  documented way to pass one per job from a 32-bit `CreateProcessA` that
  this record could rest on).
- **H11** (owner, 2026-10-18) A cancelled hardware wizard's device is
  `not-present` until the next start, so the installer asks for wizards
  to be finished, not cancelled, and lists a device left so as a
  restart-and-run-again step (sections 5.1 and 6).
- **H12** (draft 10) The storage devices of record 26 section 6.5 go
  through the same helper and the same calls as the package's own, with
  the pack's unmodified INFs as the job's INF; the helper gains nothing
  for them (no flag, no job kind).

## 14. Open, and the risks

- The Windows 95 DDK's prose reference for the device installer is not at
  hand; the header's documentation blocks and the 98 DDK's samples are
  what the design rests on, and section 2 says which steps rest on a
  sample alone. Fetching that DDK (its identity recorded as the 98 DDK's
  was) would let the record cite the prose; it would not change the calls.
- Whether `DiGetClassDevsEx` lists a devnode under `Unknown` with the
  class the installer expects, and whether `DiCreateDeviceInfo` from that
  entry installs an INF of another class as the wizard does, is the first
  reading of the legs; the fallback for a device it does not is record 23
  section 6.4's steps, kept in the installer for that reason.
- The 16-bit process handle: if the probe finds it does not signal, the
  helper is not used on that system until the record names a
  termination test (section 4.1), and the printed steps remain the
  route; if `GetExitCodeProcess` reads nothing useful, nothing rests on
  it.
- The 16-bit build's headers (Watcom's against the DDK's `windows.h`) are
  settled by the build; a conflict that cannot be resolved by include
  order is reported before any code rests on it.
- A 16-bit program on ME under a non-administrator account does not
  arise (ME has no accounts in this sense); on 98 SE the user-level
  profiles' restrictions on Device Manager (`NoDevMgrPage`) are the
  user's, not read.

## 15. Files

| File | What |
|---|---|
| `xhciinst\di\xhcidi.c` | the helper's `WinMain`: the version check, the job, the result file |
| `xhciinst\di\di_sys.c` | the engine calls of sections 5 and 9 |
| `xhciinst\di\di_core.c`, `di_core.h` | the pure part: the command line and the job file, the result writer, the error names, the verdicts |
| `xhciinst\di\prsht16.h` | only if the DDK's `inc16\prsht.h` cannot be used as it is: a declaration-only stand-in for the types `setupx.h` names from it, this project's text |
| `xhciinst\di\makefile`, `build.cmd` | the Watcom build, as `xhciqual\`'s |
| `xhciinst\di\stage-ddk.ps1` | the DDK's four files (`setupx.h`, `prsht.h`, the 98 `windows.h`, `setupx.lib`) held to `docs\references\README.md`'s hashes and staged into `obj\` (task 42-A.1) |
| `xhciinst\di\check-imports.ps1`, `xhcidi-imports.allow`, `test-gate.ps1`, `selftest.cmd` | the NE import gate of section 7 and its refusals, and the version stamp of section 3.2 (task 42-A.1) |
| `xhciinst\di\test\ditest.c`, `build.cmd` | `DITEST.EXE`, the legs' test driver, which starts the helper as section 4.1's installer does, without the window (task 42-A.1; never packaged) |
| `xhciinst\inst_core.c`, `inst_eng.c` | the installer's side: the job file and the command line, the result parser, the order, the fallback |
| `test\test_di.c`, `test\test_inst.c` | the host suites |
| `scripts\package\make-release.ps1`, `test-package.ps1` | the helper staged in `tools\xhciinst\`, its freshness, version and `NOTICE.TXT`; `-SkipDiHelper` |
| `docs\contributing\legal-provenance.md`, `docs\references\README.md`, `build-and-test.md` | the rows of section 3.4 and the prerequisite of 3.3 |
