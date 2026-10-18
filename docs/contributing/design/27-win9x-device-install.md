# Design Record 27 - The Windows 98 SE and ME Device Install

Status: draft 1, 2026-10-18, before code. Written for `roadmap-usb4.md`
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
  Stor98's NVMe devices (record 26 section 6.3); each a present devnode
  (section 6).
- The removal of a devnode, for the uninstall (section 9).
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
- A devnode that is not present (an `Enum` entry with no devnode, record
  23 section 6.3): it stays listed apart, with what to do in either case.
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
   `DIREG_*`, `DICS_*`), and the import library's export list, which names
   every routine the helper calls (`DICREATEDEVICEINFO`, `DIGETCLASSDEVS`,
   `DIGETCLASSDEVSEX`, `DIBUILDCOMPATDRVLIST`, `DISELECTBESTCOMPATDRV`,
   `DIINSTALLDEVICE`, `DIREMOVEDEVICE`, `DICALLCLASSINSTALLER`,
   `DIDESTROYDEVICEINFOLIST`, `DIDESTROYDRIVERNODELIST`, `DIOPENDEVREGKEY`,
   the `SUReg*` registry routines, `CtlSetLddPath` and `CtlGetLddPath`).
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
23 section 6.1 already refuses for `INF\OTHER`); `GenInstall` on the INF's
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
23 section 6.1), and a thunk from a 32-bit program into a 16-bit DLL is
the undocumented route this project does not take. So the installer, a
32-bit program, starts a 16-bit program, which Windows 98 and ME run in
their system virtual machine beside it, and the two talk through a file
and the process handle (section 4). The helper is small: no window of its
own, no resources but a version string, one job per start.

### 3.2 Open Watcom 2.0, the Win16 target (owner, 2026-10-18)

Open Watcom 2.0 is already a prerequisite of a cut for `XHCIQUAL.EXE`
(`build-and-test.md`, "Prerequisites"), installed to `C:\WATCOM` and
nowhere in the tree, and its install carries the 16-bit compiler and the
Windows 3.x target: `binnt\wcc.exe` (16-bit), `wlink.exe`, `wrc.exe`,
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
  `inc\win98\inc16\` (for `setupx.h`, and for `prsht.h` and `shellapi.h`,
  which `setupx.h` includes and which Watcom's `h\win` lacks or declares
  differently), then Watcom's `h\win`. The DDK's 16-bit headers are the
  ones the engine was built against, which is the point of using them;
  which of Watcom's and the DDK's `windows.h` the helper compiles against
  is settled by the build (the DDK's `inc16\windows.h` is the 98 one; a
  conflict with Watcom's runtime headers is resolved by including the
  DDK's and recorded in `build.cmd`), and the leg reads the result on both
  systems.
- `xhciinst\build.cmd` builds it as its last step when `%WATCOM%` (or
  `C:\WATCOM`) holds `binnt\wcc.exe`, else says the helper was not built,
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
in `build-and-test.md`'s prerequisites, and the 98 SE DDK's two files
join them: a cut needs `tools\win98ddk-extracted\` to hold `setupx.h` and
`setupx.lib` at the SHA-256s `docs\references\README.md` records, or
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
- in `docs\references\README.md`'s Windows 98 DDK table, the two members'
  sizes and SHA-256s (`INC_WIN98_INC16_SETUPX.H`, `LIB_WIN98_SETUPX.LIB`),
  and a sentence that they are compiled and linked against, not shipped:
  the DDK licence the entry quotes permits the design, development and
  testing of software products with it, and the image contains none of
  its bytes but the import names;
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
26 section 6.2), with one job on the command line:

```
XHCIDI.EXE install <result file> <hwnd> <device id> <inf path> [<flags>]
XHCIDI.EXE remove  <result file> <hwnd> <device id>
XHCIDI.EXE probe   <result file> <hwnd>
```

- `<result file>`: a path in the install directory (8.3 parts, record 23
  section 9.6) the installer creates empty before the start and reads
  after the end; the helper truncates and rewrites it, so a stale file is
  never read as a result.
- `<hwnd>`: the installer's window handle in decimal, or 0 from the
  console tool. On Windows 98 and ME the 16-bit and 32-bit window handle
  spaces are one, so the engine's dialogs (the copy engine's "Insert
  disk", the class installer's questions, the signature prompts ME shows)
  are owned by the installer's window and appear in front of it. A 16-bit
  program may use a handle a 32-bit program created on these systems; the
  leg reads that the dialogs appear owned (section 12).
- `<device id>`: the devnode's device id as `cfgmgr32.dll` gave it to the
  installer (`PCI\VEN_8086&DEV_...\BUS_00&DEV_14&FUNC_00`,
  `USB\XHCI98_ROOT_HUB\...`), at most `MAX_DEVICE_ID_LEN` characters.
- `<inf path>`: the INF in the install directory (`C:\XHCI98\XHCI98.INF`),
  every part 8.3 and no space (record 23 section 9.6's rule makes the
  command line safe for the 16-bit engine, whose `atDriverPath` is one
  atom of at most 255 characters).
- `<flags>`: `force` (reinstall a current driver: record 23's `/force`
  and the Have Disk cases of its section 6.4), `quiet` (section 5.2).

The installer waits on the process handle with the window's message loop
alive (`MsgWaitForMultipleObjects` in the window; `WaitForSingleObject` in
the console tool), unbounded while the helper's process lives, because the
engine's dialogs wait for the user; the Progress page says which device
the helper is installing and that a Windows dialog may be waiting behind
the window. Whether the handle `CreateProcessA` returns for a 16-bit
program on these systems is signalled at its exit, and what
`GetExitCodeProcess` then says, are readings of the legs: the result
file's closing line, not the exit code, is the contract (section 4.2),
and a handle that never signals is bounded by the file's `END` line being
polled every second once the helper's window class is gone from the
desktop (read on the legs; section 14).

### 4.2 The result file

ASCII, CRLF, one record a line, written in order and flushed, the `END`
line last:

```
XHCIDI 3.1.0.0
JOB install PCI\VEN_8086&DEV_15EC&SUBSYS_...\BUS_00&DEV_0D&FUNC_00 C:\XHCI98\XHCICM.INF
FOUND 1                       the devnode was found in the engine's list
DRIVER XHCICM.INF XhciCm.Install "xHCI98 Thunderbolt 3 Controller" rank 0 date 2026-10-18
CALL DiBuildCompatDrvList 0
CALL DiInstallDevice 0
FLAGS 00000080                DEVICE_INFO.Flags after the call (DI_NEEDRESTART here)
RESULT installed-restart      one of section 5.4's verdicts
END 7                         the number of lines above it
```

A `CALL` line per engine routine with its `RETERR`; a `RESULT` line with
one verdict word; an `ERROR` line with the engine's error name
(`ERR_DI_*`, from the header's enumeration) and number where a call
failed; `END` with the count. The installer's pure half parses it
(`inst_di_result`, host-tested on every verdict and on a truncated file):
no `END`, a count that differs, a version that is not the installer's, a
`JOB` that is not the one started, or an unknown verdict is "the helper
did not finish", reported as a failure of that device with the steps page
(record 26 section 7). The installer copies every line into its log.

### 4.3 What the helper never does

It never shows a window of its own (its only output is the file), never
writes the registry itself (the engine does, inside its calls), never
touches a devnode other than the one named, never starts a second job,
never retries, and never runs on a system that is not 98 SE or ME (it
reads the Windows version first and writes `RESULT refused` with the
reason). A helper started with a malformed command line writes
`RESULT refused` and the reason. An installer that finds no helper beside
it (an older or a development package) falls back to the printed steps
of record 23 section 6.4 for every device, saying so once.

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
szRegSubkey, szClassName, hwnd)`, the sample's way of taking one entry
out of the list (`ipmodem.cpp`: the found device's fields passed to
`DiCreateDeviceInfo`), then destroys the list (`DiDestroyDeviceInfoList`).
Not found: `FOUND 0`, `RESULT not-present`, and the installer treats the
device as record 23 section 6.3's "recorded, not running now".

**The class.** A devnode with no driver yet has the class the enumerator
gave it (the controller's PCI class, or none); the INF's install gives it
the INF's. `DiCreateDeviceInfo` takes the class name the list reported, and
`DiInstallDevice` writes the INF's `Class` into the devnode as every
install does; the helper does not set `szClassName` from the INF itself
(the sample passes the list's class, and the engine's block for the field
says what it holds, not that the caller changes it). Whether an INF of
class `USB` installs on a devnode listed under `Unknown` through this
route exactly as the wizard installs it is the first thing the 98 SE leg
reads (section 12); the wizard does the same thing with the same
structure, so the record expects it to.

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
- `lpdi->lpSelectedDriver = DiSelectBestCompatDrv(lpdi, NULL)`: the
  engine's own choice among the matches ("Given list of drivers returns the
  newest" is `DiPickBestDriver`'s comment; `DiSelectBestCompatDrv` is what
  the wizard calls, and the class installer may be consulted by it). The
  helper checks the chosen node's `atInfFileName` names the given INF
  (with one INF in the search it must) and writes the `DRIVER` line from
  the node. With `force`, a node flagged `DNF_OLDDRIVER` or
  `DNF_CURRENT_DRIVER` is still installed (the engine's "do not upgrade
  the current driver", `ERR_DI_DONT_INSTALL`, is answered by the installer
  having decided already, record 23 section 5.4; the header lists the
  error, and whether `DiInstallDevice` returns it for a same-version
  install without the wizard's "better driver" test is a reading of the
  legs, with the Have Disk steps the fallback).

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
  header's `Ctl*` logical-disk set, declared with their arguments; their
  effect on the prompt is the leg's reading, section 12). If the prompt
  still comes, it is Windows' own, owned by the installer's window, and
  the page says what to type (record 26 section 7); the files then come
  from the directory typed, as today.
- `DiInstallDevice(lpdi)`: the engine copies the files of the chosen
  node's section (through its copy engine, asking for the Windows CD for
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
| `installed` | `DiInstallDevice` returned 0, no restart flag | record 23 section 5.9's check on the devnode (record 26 section 8) |
| `installed-restart` | 0, with `DI_NEEDRESTART` or `DI_NEEDREBOOT` | "installed, restart pending"; the restart flag |
| `cancelled` | `ERR_DI_USER_CANCEL` | the device's steps page (record 26 section 7), exit 4 |
| `no-match` | an empty compatible list | the steps page, and the ids in the log |
| `not-present` | the devnode is not in the present list | record 23 section 6.3's "recorded, not running now" |
| `failed <ERR_DI_name> <n>` | any other `RETERR`, with the call that returned it | the steps page; a failed controller stops the devices below it, as record 23 section 5.1 does on NT |
| `refused <reason>` | the helper did not start the job (the system, the command line, the version) | a failure of the run's 98 SE and ME route, said once |

## 6. The devices, in order

The installer's plan (record 23 sections 5.4 and 6.3) decides what is
installed; the helper only does what it is told, one devnode at a time,
in this order:

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
   driver (a cancelled wizard) or an older one (an update: the 9x leg of
   2026-10-15, where the root hub otherwise stayed at the old release),
   each by `xhci98.inf`, repeated while new targets appear, at most four
   passes as on NT, each pass after a settle.
4. **UAS devices** (`xhciuas.inf`) present, install or update; on 98 SE
   the NUSB mass-storage prerequisite and on ME the first-stick advice
   stay as record 23 section 6.4 says them, on the Found page.
5. **The Thunderbolt or USB4 host interface** (`xhcicm.inf`) when the
   component is ticked; its own devices (the bridges a tunnel brings up)
   are `xhcicm.sys`'s to number (task 42-A.14 and record 28 for what is
   still open there), not the helper's.
6. **Stor98's NVMe devices** with no driver, when the component is ticked
   (record 26 section 6.3), by `stor98.inf` from the install directory.

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

The same calls: ME's `SETUPX.DLL` exports the same names (the import
library is one for both; the 98 SE and ME legs of record 23 read the same
`cfgmgr32.dll` export set, design record 24 section 7.1), and the
differences record 23 recorded are the wizard's (the "Select Other Driver"
list naming a driver twice, `INF\OTHER`), which the helper does not open.
ME's digital-signature prompt for an unsigned driver, where ME shows one,
is Windows' own dialog owned by the installer's window. Read on its own
leg (section 12).

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

## 9. Removal, for the uninstall

`XHCIDI.EXE remove <result file> <hwnd> <device id>`: the devnode found as
in section 5.1 (present devices only; a devnode of the package that is not
present has no device the engine can remove, and its `Enum` entry is the
engine's own record of an absent device, left as Device Manager leaves
one), then `DiRemoveDevice(lpdi)` with the default removal (the header's
`REMOVEDEVICE_PARAMS` block: `DI_REMOVEDEVICE_GLOBAL` removes the device
from every configuration; the helper passes no class-install parameters,
so the default applies, which Device Manager's Remove button also takes).
Children first is the installer's order (record 26 section 9.3 step 4):
UAS devices, Stor98's devices when recorded, the host interface, hubs and
composite entries, root hubs, controllers. `DiRemoveDevice` on a devnode
with children removes the subtree as Device Manager's Remove of the
controller does ("it removes the root hub and everything beneath it",
record 23 section 9.4's steps), so the installer removes the controller
last and reads the tree between removals; a devnode gone with its parent
is not an error. The verdicts: `removed`, `removed-restart` (the flags),
`cancelled`, `not-present`, `failed <ERR_DI_name> <n>`.

After the removals the engine's bookkeeping of section 8 is what Device
Manager's Remove leaves: the devnode and its `Enum` key gone, the driver
key gone, the files and the `INF\OTHER` copy still there; the installer
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

- the helper's command line (the job, the arguments, the 8.3 and length
  checks on the paths and the id);
- the result file's parser and the verdict table of section 5.4, with
  cases for every verdict, a truncated file, a wrong version, a wrong job,
  an unknown word, an `END` count that differs;
- the devices' order of section 6 and the passes' rule (the NT settle's
  state machine, `inst_settle_*`, reused);
- the steps-page fallback decision (which verdicts send a device to record
  23 section 6.4's list).

In `xhciinst\di\di_core.c`, the helper's own pure part, C89 with no
Windows header and no far pointer, compiled by Watcom into the helper and
by the host suite (`test\test_di.c`) under MSVC: the command-line parser,
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
| F, fresh install | the controller installed by the helper from the Found page's plan with no hand step; the Windows CD prompt for `usbd.sys` owned by the installer's window; the root hub's and hubs' wizards after the controller's restart, and the settle's wait for them; a cancelled root-hub wizard then installed by the helper; the devnode's class after the install; whether `LDID_SRCPATH` spares the "Copy files from" prompt; the driver key's values of section 8 |
| U24, update over `2.4.0.0` | the controller and the root hub updated by the helper with no hand step (the hand step record 23's 9x leg needed); the keyboard and mouse behind the controller back after the restart of the devices, not of Windows |
| U23, update over `2.3.1.0` (98 SE) | the new devnodes and their wizards; the helper's pass over what was cancelled |
| UAS and Stor98 | a UAS disk installed and `force`-reinstalled (the `ERR_DI_DONT_INSTALL` question); Stor98 ticked on an NVMe device with no driver, installed from the copy |
| Thunderbolt | the host interface by `xhcicm.inf` on the emulator's Alpine Ridge guest (the 98 SE wizard's "Copy files from" of record 23 section 8.5, now the helper's) |
| UN, uninstall | each devnode removed through the helper in order; the subtree removal; the `INF\OTHER` copies and the files deleted by the installer; the restart; the controller back as an unknown device with no wizard finding a driver |
| ME | every row above on ME, and ME's own prompts |
| Dialogs and handles | that the engine's dialogs are owned by the installer's window; that the process handle of the 16-bit helper signals at its exit and what its exit code reads; the fallback poll of the result file |

What the legs settle that nothing here has read: the devnode's class
through `DiCreateDeviceInfo` from the list's entry; `DI_ENUMSINGLEINF`
with an INF outside `%windir%\INF`; `CtlSetLddPath` and the copy prompt;
`ERR_DI_DONT_INSTALL` on a same-version install; the subtree removal of
`DiRemoveDevice`; the 16-bit process handle's behaviour; and where the
engine copies an INF found through `DevicePath`.

## 13. Decisions taken in writing this (the orchestrator's, owner to confirm)

- **H1** (owner, 2026-10-18) Open Watcom 2.0's Win16 target against the
  98 SE DDK's `setupx.h` and `setupx.lib`; the large model (section 3.2).
- **H2** A separate 16-bit executable, one job per start, a result file
  and the process handle as the interface; no thunk, no 16-bit DLL loaded
  into the installer (section 4). Not taken: a 16-bit DLL called through a
  flat thunk (undocumented from a Win32 program on these systems), or
  `rundll.exe setupx.dll,InstallHinfSection` (installs an INF section, not
  a driver on a devnode; the DDK's own INFs use it for an uninstall
  section, and record 26's uninstall does not need it).
- **H3** The devnode is taken from `DiGetClassDevsEx`'s present list and
  rebuilt with `DiCreateDeviceInfo`, the DDK sample's way; the helper
  never fills `DEVICE_INFO` from a device id by hand (section 5.1).
- **H4** `DI_ENUMSINGLEINF` with the package's INF, `DI_QUIETINSTALL`,
  the engine's own `DiSelectBestCompatDrv`; never `DI_NOFILECOPY`,
  `DI_FORCECOPY` or `DI_DONOTCALLCONFIGMG` (section 5.2, 5.3).
- **H5** `LDID_SRCPATH` set to the INF's directory for the install and
  restored after; the prompt, if it still comes, is Windows' own (section
  5.3).
- **H6** The installer never installs under a wizard it cannot see: a
  settle and the user's Next gate every pass after the controller (section
  6; record 23 decision D13 on these systems).
- **H7** Removal through `DiRemoveDevice`, children first, the controller
  last; the INF copies and the files removed by the installer after
  (section 9).
- **H8** The helper is a cut prerequisite with the qualifier; a package
  without it falls back to the printed steps and the release notes say so
  only for a `-SkipDiHelper` package (section 3.3).
- **H9** The result file is the contract; the exit code and the handle's
  signal are readings (section 4.1, 4.2).

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
- The 16-bit process handle: if it does not signal, the installer's poll
  of the result file is the wait; if `GetExitCodeProcess` reads nothing
  useful, nothing rests on it.
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
| `xhciinst\di\di_core.c`, `di_core.h` | the pure part: the command line, the result writer, the error names, the verdicts |
| `xhciinst\di\prsht16.h` | only if the DDK's `inc16\prsht.h` cannot be used as it is: a declaration-only stand-in for the types `setupx.h` names from it, this project's text |
| `xhciinst\di\makefile`, `build.cmd` | the Watcom build, as `xhciqual\`'s |
| `xhciinst\inst_core.c`, `inst_eng.c` | the installer's side: the command line, the result parser, the order, the fallback |
| `test\test_di.c`, `test\test_inst.c` | the host suites |
| `scripts\package\make-release.ps1`, `test-package.ps1` | the helper staged in `tools\xhciinst\`, its freshness, version and `NOTICE.TXT`; `-SkipDiHelper` |
| `docs\contributing\legal-provenance.md`, `docs\references\README.md`, `build-and-test.md` | the rows of section 3.4 and the prerequisite of 3.3 |
