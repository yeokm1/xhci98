# Design Record 23 - The Install, Upgrade and Uninstall Tool

Status: revision 4, 2026-10-15, reviewed and revised three times the same day;
the code is written against it (task 40.6). Written for the task group the
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
<package>\INSTALL.BAT              the one entry point
<package>\release-x86\ ...         unchanged: the drivers and their INFs
<package>\release-x64\ ...
<package>\debug-x86\, debug-x64\
<package>\tools\xhciinst\XHCIINST.EXE   32-bit
<package>\tools\xhciinst\XHCIIN64.EXE   x64
<package>\tools\xhciinst\readme.txt, NOTICE.TXT
<package>\tools\xhcicln\ ...       unchanged
```

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
  (`link -dump -imports`), the tool's import gate (section 11).
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

## 4. The package as the tool sees it, and the refusals

### 4.1 The driver table

The tool knows the package's drivers by file, never by hardware id:

| INF | Driver | Service | Systems | Present in the package |
|---|---|---|---|---|
| `xhci98.inf` | `xhci98.sys` (and `xhcihlpr.sys` in x86) | `xhci98` (`xhcihlpr`) | all | required |
| `xhciuas.inf` | `xhciuas.sys` | `xhciuas` | all | required |
| `xhcicm.inf` | `xhcicm.sys` | `xhcicm` | all | the set of three, all or none |
| `xhcicmf.inf` | `xhcicm.sys` as an upper filter | `xhcicm` | NT only (`$Windows NT$`) | the set of three |

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
   decorated forms) is there too;
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
or 98 SE is refused too: it is not a target of this package.

### 4.4 The local copy

Every run that changes anything first copies the flavour directory's files
to a directory of this project's own under `%windir%\XHCI98\`, and stages
and installs from that copy (decision D7). Two reasons: on Windows 2000 and
XP a device found later takes its files from where the INF was staged from,
which must not be a CD, a floppy or a USB stick behind the controller being
updated; and on 98 SE and ME the directory is what `DevicePath` names
(section 6).

**The copies are immutable.** Each is named for its content -
`<version>-<flavour directory>-<tag>`, the tag an FNV-1a hash of every
file's name and bytes - holds an `XHCIINST.LST` naming the files the tool
put there, and is never rewritten: a run whose package is already there
checks the files are byte-identical and uses them, and a run of another
package makes another directory. So an older package run for one driver
cannot replace the files a newer installed package's INF still names, and
a run that changes only a registry value replaces nothing. The copies are
kept while anything may name them, and only the uninstall removes them:
the files each list names, then each directory and `%windir%\XHCI98` only
if empty.

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
4. Ask the registry questions (section 8), then ask once whether to go on
   (`Y`; anything else, or end of input, cancels with exit 1 and changes
   nothing). `/yes` skips the confirmation; `/defaults` and `/set` answer
   the registry questions without asking.
5. Ask Windows whether an installation is in progress
   (`CMP_WaitNoPendingInstallEvents` with no wait, Microsoft's "Checking
   for In-Progress Installations"): only "none" goes on; "pending" or a
   failed question stops the run with nothing changed (exit 4). Then copy
   the package locally (section 4.4) and pre-stage every INF from the copy
   whose service group is not refused (sections 5.4 and 5.6); with
   `/restage`, replace a stored package the trap holds (section 5.7).
6. Update the controllers (`xhci98.inf`).
7. Wait for the system to finish installing what the update made appear
   (`CMP_WaitNoPendingInstallEvents`, 60 seconds), and read the devnodes
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
11. Report the restart (section 5.9), and offer `XHCICLN` (section 5.11).

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

- **`INSTALLFLAG_FORCE` always.** Without it the routine installs only a
  driver it ranks better than the installed one, and Windows 2000 does not
  rank an unsigned driver's date (the release notes' "use Have Disk"); a
  reinstall of the same version (`/force`, `/restage`) needs it as well.
  The decision whether to install at all is section 5.4's, made before the
  call.
- **The id is the device's own first hardware id**, its most specific,
  never the INF's model id: the controller's model id `PCI\CC_0C0330`
  would reach every xHCI controller, a foreign one included. Before each
  call the tool checks that every present device carrying that id among
  its ids is one this step was going to install or update; if not - two
  identical controllers, one with another vendor's driver - the call is
  not made for that id and the devices are reported (exit 4).
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
the trap. Without `/restage` the tool installs nothing from that INF,
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
   it. A later run reads it first: an unreadable or malformed file refuses
   the run (exit 1); a file naming another package's copy refuses it too,
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
5. every listed device is reinstalled. A `DEV` line goes only when its
   device is present, its driver key no longer names the removed INF, and
   it runs the package's version with no problem code; the `INF` line goes
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
most four passes): before each pass `CMP_WaitNoPendingInstallEvents` (60
seconds) must answer "none" and the devices are read and planned again,
with every safeguard of sections 5.4 and 5.5 applied afresh; a timeout or
a failed question stops the run with "finish or cancel the windows Windows
shows, then run INSTALL.BAT again" (exit 4), and the next run continues
from what it finds - a root hub whose wizard was cancelled is a device
with no driver, installed then.

- **XP and XP x64** show Found New Hardware for each, and the unsigned
  question. `CMP_WaitNoPendingInstallEvents` then times out, because the
  wizard waits for the user. **A timeout, or a failed question, stops the
  run** (exit 4) with
  "finish or cancel the Found New Hardware windows, then run INSTALL.BAT
  again": an installation in progress is not the tool's to compete with
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
Anything else is reported per device:

| After | Meaning | Exit |
|---|---|---|
| problem 14 (`CM_PROB_NEED_RESTART`), or the routine's `bRebootRequired` | restart needed | 3 |
| 32-bit Vista and 7 after the first `xhcihlpr.sys` install: Code 28 (7) or Code 1 (Vista) with a restart asked for | restart needed (release notes, "Installing", step 3) | 3 |
| Vista x64 and 7 x64, Code 52 or 39, test-signing not confirmed (section 5.10) | steps for the user: test-signing mode or F8 | 4 |
| any other problem code, a missing root hub under a started controller, or the old version | failed | 2 |

The flags are kept apart - failed, restart, steps for the user - and the
exit code is derived at the end (section 7.4); a later failure never
erases an earlier restart. A registry value written to a running device
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
start does not set it - followed by the README section's instructions
(`bcdedit /set testsigning on` and a restart, or F8 at every start), and
the note that otherwise the controller shows Code 52 on 7 or Code 39 on
Vista. The run goes on if the user confirms. The tool never runs
`bcdedit`. What the value holds in test-signing mode and on an F8 start is
read on the legs (section 12).

### 5.11 `XHCICLN` at the end

Whenever `XHCI98\` devnodes exist (the walk of section 5.3 sees them) -
after a run that changed something, and also after a run that changed
nothing, such as the second run after the restart, when the cleanup first
becomes possible - the tool asks, last, "Run XHCICLN to list and remove
the old entries an update from 2.3.x left? [y/N]"; only `y` runs it.
`/yes` does not answer this question (never by default); `/clean` answers
it yes. It starts `..\xhcicln\XHCICLN.EXE` (`XHCICL64.EXE` from the x64
tool) with `-remove` in the same console and waits; `XHCICLN` asks its own
question and keeps every refusal of record 13 section 10.13.8, so a run
before the restart it needs removes nothing and says why. Its exit code is
reported; its 3 (a restart pending) adds a restart to this tool's result,
its 2 adds a failure, and its 1 (refused) changes nothing.

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
  into either by hand is driving the engine's own bookkeeping. Not used.
- **`DevicePath`** is the documented knob:
  `HKLM\Software\Microsoft\Windows\CurrentVersion`, value `DevicePath`,
  "default search path for .INFs" (`REGSTR_VAL_DEVICEPATH`, the Windows 98
  DDK's `regstr.h`), a `;`-separated list of directories the device
  installer searches for INFs. It is the route the tool takes. The header's
  comment establishes the value's meaning, not how the wizards search it,
  cache it or take files from it: those are readings (section 6.2 item 3).

### 6.2 The route

1. The local copy (section 4.4): this package's own directory under
   `%windir%\XHCI98\`, which holds the INFs - the parent holds none, and no
   recursive search is assumed.
2. `DevicePath` names that directory: every entry under
   `%windir%\XHCI98\` (an earlier run's copy, which the tool itself added)
   is taken out of the list and this copy's directory appended, compared
   without case; no other entry is reordered or removed, and the value
   keeps its type. So the wizards search this release's copy alone; the
   earlier copies stay on disk, where an INF Windows installed from one may
   still look for its files, until the uninstall.
3. The device installer then finds the package's INFs in a directory it
   searches by itself, and an INF found there takes its files from its own
   directory. **Unread until the legs**: whether the 98 SE and ME wizards
   search a `DevicePath` entry other than `INF` with no box ticked,
   whether their driver database is rebuilt for it, and where the files
   are copied from (section 12). Until those readings, the printed steps
   name both routes: the search with no box ticked, and "Specify a
   location" with the copy's directory typed once (the tool prints it in
   full), which needs no package
   media either.
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
`xhciuas.sys`, `xhcicm.sys`), and its version is the driver key's
`DriverVersion` where the install wrote one; where it did not, the group's
installed binary's file version stands for it (section 5.4: one binary
per service), and the `DriverDate` is only shown. Neither readable: the
device is unknown and only listed. A failed walk refuses the run.

### 6.4 What it prints

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
  and from `2.3.x` the keyboard-and-mouse paragraph of "From 2.3.x or
  earlier", printed before the confirmation as on NT (section 5.8);
- **current**: nothing; the root hub's Driver tab may show the earlier
  date until it is updated the same way;
- **another vendor's driver**: left (decision D3).

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
the same directory. The run ends with exit 4: staged, steps left for the
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
| `/yes` | no confirmation, and with `/uninstall` no typed word (section 9.1); never answers the `XHCICLN` question |
| `/defaults` | ask no registry question, keep every current value (the INF's default on a new install) |
| `/set Name=Value` | answer one registry question, repeatable; validated against the value table and the system (section 8.1); applies even where the question would not be asked |
| `/force` | reinstall a current device, and install over a newer group (section 5.4) |
| `/restage` | Vista and 7: replace a stored package the trap holds (section 5.7) |
| `/clean` | answer the `XHCICLN` question yes |
| `/uninstall` | section 9 |
| `/debug` | `INSTALL.BAT` only: the `debug-` flavour directory |
| `/nopause` | do not wait for Enter before closing |
| `/log path` | write the log there |

An unknown switch, `/set` with an unknown name or a value outside its
allowed set, or a contradiction (`/defaults` with `/set`, `/list` with
`/yes`, `/clean` or `/restage`, `/uninstall` with an install switch) is
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
read.

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
| 0 | done: everything current or installed, no restart; or `/list` |
| 1 | refused before any change: location, architecture, rights, a routine missing, a bad switch, a partial device list, cancelled |
| 2 | failed: a step failed or a result was wrong (section 5.9); the log and the report say what was done |
| 3 | done, restart needed (and, where said, run `INSTALL.BAT` again after it) |
| 4 | done as far as the tool can: steps remain for the user - Windows 98 SE and ME always; on NT a group refused as newer or unknown, a device left by the trap (5.7), an id shared with a foreign device (5.5), a wizard to finish (5.8), test-signing to turn on (5.9) |

Failed, restart and steps are three flags; the code is 2 when failed is
set, else 4 when steps is set, else 3 when restart is set, else 0. The
console says every flag that is set, so a 2 or a 4 that also needs a
restart says so. `INSTALL.BAT` passes the code on.

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
| `XhciPollFineTimer` | `xhci98.inf`, the controller's | 2000 onward | 0, 1 | 0, 1 | a finer system clock while polling |
| `XhciSelectiveSuspend` | `xhci98.inf`, the controller's | any xHCI controller | 0, 1 | 0, 1 | put idle devices to sleep (off by default; read on the release notes' terms) |
| `XhciForceBulkOnly` | `xhci98.inf`, the controller's | any xHCI controller | 0, 1 | 0, 1 | Bulk-Only instead of UAS for a disk that offers both, from its next connection; a UAS-only disk is not affected |
| `XhciCmMode` | `xhcicm.inf`, the NHI's | a present NHI `xhcicm.inf` matches | 0, 1 | 0 to 3 | the connection manager off or automatic (2 and 3 are the record 21 overrides, `/set` only) |
| `XhciCmApprove` | `xhcicm.inf`, the NHI's | as `XhciCmMode` | 0, 1 | 0, 1 | approve Thunderbolt devices automatically |
| `XhciCmSafeEject` | `xhcicm.inf`, the NHI's | as `XhciCmMode` | 0, 1 | 0, 1 | offer Safely Remove for the dock's devices |

Not asked: the log values (`SNAPON.BAT` and `SNAPOFF.BAT` own them, with a
restart the user is told about), the fourteen Linux quirk values (record
20: their defaults are the point), the poll periods, the moderation
interval, `XhciMaxTransferKB`, `XhciWideMap`, `XhciFirstEnum*`,
`XhciIntelIrqRoute` (a repair that is on by default and turned off only to
diagnose), `XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`,
`XhciAccept096`, `XhciUsb2HubSpeedFallback`, and `XhciSilentInstall`,
which needs its class list beside it (`tools\silent\`'s batch files set
both). Any of them stays settable by hand, as today.

Each value's default is taken from its INF's `AddReg` line, not from the
table; a value the INF does not write is not asked, and the host suite
fails if a table row names a value an INF of `src\` does not write, or
writes at a default the row does not offer (section 10). The `XhciCm*`
rows follow `xhcicm.inf` as task 39-A.1 writes it, and the allowed sets
are checked against record 21 section 8 when that section is final.

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
- **System**: `XhciPollFineTimer` from Windows 2000 on (98 and ME read it
  and do nothing); `XhciInterruptMode`'s value 3 on Vista and 7 only.
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
XhciSelectiveSuspend - put idle USB devices to sleep (0 off, 1 on)
  now: 0 (xHCI98 USB 3.x eXtensible Host Controller)
  default: 0     allowed: 0, 1
  new value [Enter keeps the value now]:
```

`now` is the value in each device's driver key, or "not set" (the
install's `AddReg` then writes the default), or "new install: the default".
A value with a caution shows it under the question, and `/set` prints it
when it sets the value the caution is about: `XhciIntelPortSwitch` 2 writes
registers of unknown meaning on an Intel controller without the switch
registers (release notes, "Value 2"); `XhciSelectiveSuspend` 1 has not been
read on a real machine, "leave it off unless you are testing it";
`XhciForceBulkOnly` is read as each device is connected, and on Windows
2000 and later a disk already installed keeps its driver until it is
uninstalled in Device Manager and plugged in again; `XhciInterruptMode` 1
and 3 refuse a controller Windows gives no line or no message.
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

## 9. Uninstall (owner, 2026-10-15)

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
   INFs' own `DefaultInstall` sections make through directory id 17. Where
   `SetupUninstallOEMInfA` resolves (Windows XP onward, Microsoft's
   documentation; the legs read which systems have it), it removes each
   `oemNN.inf`, flags 0, so a package still referenced is refused and
   reported rather than forced; on Vista and 7 that also removes the
   driver-store package. Otherwise - Windows 2000, and every plain copy -
   Microsoft's documented manual route (KB 813449): first every devnode,
   present or not, has its driver key's `InfPath` read, and an INF any of
   them names, or any `InfPath` that cannot be read, is kept and stops
   the run; then the `.pnf`, then the `.inf`.
5. **The services** `xhci98`, `xhciuas`, `xhcicm`, `xhcihlpr` and the
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

1. removes what it owns: its entries in `DevicePath`, the files each
   copy's `XHCIINST.LST` names under `%windir%\XHCI98`, and each directory
   if it is then empty;
2. lists the INF files in `INF` and `INF\OTHER` whose `Provider` is this
   package's and that install one of its drivers (the rule of section 9.2
   step 4) - the device installer's own copies, and the plain copies the
   INFs' `DefaultInstall` sections make - and the driver files present;
3. prints the steps, dependants first: in Device Manager remove "xHCI98
   USB Attached SCSI Storage" and a Thunderbolt entry if there is one,
   then "xHCI98 USB 3.x eXtensible Host Controller" (which removes the root
   hub and everything beneath it); then delete the listed INF files, so
   the restart cannot find the driver again; then restart, and
   cancel the New Hardware wizard if it offers the controller; then
   delete the driver files listed and restart once more.

Whether the wizard offers the controller again after step 3's restart, and
what it finds, is read on the legs. Exit 4.

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
- the PE machine read and the flavour marker search over a file buffer;
- the `DevicePath` append and removal (section 6.2);
- the `UpperFilters` edit (section 9.2 step 1);
- `SystemStartOptions`' test-signing word;
- the switch parser, `/set`'s validation and section 7.4's exit code;
- the registry value table, its applicability (section 8.2) and its
  answer parser.

The suite reads `src\xhci98.inf`, `src\xhci98-amd64.inf`, the two UAS INFs
and, once task 39-A.1 has merged, the `src\cm\` INFs, and checks that each
value-table row is written by its INF at a default the row offers, and
that section 5.2's choice gives each system its section.
`xhciinst\selftest.cmd` runs the executables' `/selftest` (a short set of
the same checks, so the shipped binary's linkage is exercised), their
switch refusals (`/frobnicate`, `/set Bogus=1`, `/list /yes`), and the
import table; `build-driver.cmd` runs it whenever the executables have been
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
| U23, update over `2.3.1.0` | 98 SE, 2000, XP, XP x64, 7 x64 | the new devnodes of record 13 section 10.13.4; on XP and XP x64 the wizard stop and the second run (section 5.8); on 7 x64 installed by step 8 or by Windows; the `XHCICLN` question and its run after the restart, on a run that changes nothing |
| UN, uninstall and reinstall | 98 SE, ME, 2000, XP, XP x64, Vista x86, Vista x64, 7 x86, 7 x64 | the warning and the word; every devnode, the OEM INFs (2000: the manual route; XP onward: `SetupUninstallOEMInf`, which systems resolve it), the services and files gone or pending; the gate after a pending removal and the resumed second run; a restart; the controller back as an unknown device; `INSTALL.BAT` installing it again. 98 SE and ME: the steps printed and followed |
| R, registry questions | 98 SE (after the install and restart), 2000 | each applicable question shown with now, default and allowed; a non-applicable one not shown (no Intel switchover controller in QEMU, no NHI); an answer written to the controller's driver key and not to the root hub's, Enter keeping, `/set` and `/defaults` |
| L, `/list` and the refusals | every leg's first run | nothing changed (a registry and file listing before and after, no log written); the ZIP-folder refusal, a mixed architecture, a mis-versioned INF, a stale `.sys`, a swapped flavour |
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
- **D2** Windows 98 SE and ME through `DevicePath` and the local copy in
  in this package's own directory under `%windir%\XHCI98\`, which
  `DevicePath` names in place of an earlier copy's, never `INF\OTHER` or
  `SETUPX` (section 6).
- **D3** A device bound to another vendor's driver is listed and left,
  including an xHCI controller, whatever its problem code; Device Manager
  remains the way to replace a vendor driver (section 5.4).
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
- **D10** `XHCICLN` is offered last whenever old entries exist, asked,
  never by default (section 5.11).
- **D11** The uninstall leaves the OS's `usbd.sys` and `usbui.dll`, other
  drivers' devnodes and every filter host's devnode, gates each phase on
  the one before, uses `SetupUninstallOEMInf` without force, and on
  Windows 2000 deletes the `.pnf` and then the `.inf` (section 9).
- **D12** A registry answer applies from the next start of the driver; a
  new NHI's first start runs with the INF's defaults until then, and the
  question says so (section 8.4).
- **D13** An installation in progress - before the first change, or after
  the controller's update - stops the run; the tool never installs under
  a wizard (sections 5.1 and 5.8).
- **D14** A filter host is given back to a driver that is not this
  package's by the per-device driver-node route, never by an ID-wide
  call; a disconnected one stops the uninstall until it is connected
  (section 9.2).

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

## 15. Files

| File | What |
|---|---|
| `xhciinst\xhciinst.c` | the tool: the system half |
| `xhciinst\inst_core.c`, `inst_core.h` | the pure half (section 10) |
| `xhciinst\xhciinst.rc`, `xhciinst.manifest` | the elevation manifest |
| `xhciinst\build.cmd`, `selftest.cmd` | the two builds; the self-test and the import check |
| `xhciinst\INSTALL.BAT` | staged at the package root |
| `xhciinst\README.TXT` | staged as `tools\xhciinst\readme.txt` |
| `test\test_inst.c` | the host suite |
| `scripts\build-driver.cmd`, `scripts\package\make-release.ps1`, `scripts\package\test-package.ps1`, `test\run-host-tests.cmd`, `.gitignore` | wiring |
