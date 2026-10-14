# Design Record 23 - The Install, Upgrade and Uninstall Tool

Status: revision 1, 2026-10-15, before code. Written for the task group
the owner added to Phase 40 of `roadmap-usb4.md` on 2026-10-15: the
`3.0.0.0` package gains an install and upgrade script, `INSTALL.BAT`, in
front of a console tool of this project's own, `XHCIINST.EXE` (x86) and
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

In scope, per system:

| | Windows 2000, XP, XP x64, Vista, 7 (x86 and x64) | Windows 98 SE, ME |
|---|---|---|
| Install and update on present devices | every driver of the package: the controller, then the root hub, hubs and composite entries, `xhciuas.sys`, `xhcicm.sys` on the NHI and the tunnel-port filter (section 5) | staged where the wizard searches; the clicks printed (section 6) |
| Pre-staging for devices not present | every INF of the package (`SetupCopyOEMInf`) | the same staging |
| Registry values (section 8) | asked, written to the driver key | asked where the driver key exists |
| Uninstall (section 9) | devnodes, the filter name, the OEM INFs, the services, the files | best effort with documented mechanisms, then the manual steps printed |
| A dry run, `/list` | everything above, nothing changed | the same |

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
It chooses the executable and the flavour directory and passes every
argument on:

- `%OS%` other than `Windows_NT`: Windows 98 SE or ME; `XHCIINST.EXE`,
  `release-x86`. `COMMAND.COM` has no `%~dp0`, so the batch file runs the
  tool by its path relative to the current directory, which is the batch
  file's own when it is started from Explorer, and the tool checks its own
  location (section 4).
- `Windows_NT`, `PROCESSOR_ARCHITECTURE` or `PROCESSOR_ARCHITEW6432` equal
  to `AMD64`: `XHCIIN64.EXE`, `release-x64`.
- `Windows_NT` otherwise: `XHCIINST.EXE`, `release-x86`.
- `/debug` among the arguments: the `debug-` directory instead, which the
  tool says is for troubleshooting only.

The batch file is ASCII with CRLF endings (`.gitattributes` holds `*.BAT`
to CRLF; MS-DOS 7.1 loses `goto` labels in LF files), and `make-release.ps1`
stages it from `xhciinst\INSTALL.BAT`.

## 3. Toolchain and builds

`XHCICLN`'s, exactly (design record 13 section 10.13.8, decision D7 there):

- One C89 tool source plus one pure source shared with a host suite
  (section 10), in `xhciinst\` at the repository root beside `xhcicln\`.
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
  `newdev.dll` and `cfgmgr32.dll` are loaded with `LoadLibrary` and every
  routine looked up with `GetProcAddress`, so the image loads on every
  target, including Windows 98, where `newdev.dll` has no such routine;
  a routine missing that a step needs refuses that step, says which
  routine, and never substitutes another (section 7). `xhciinst\selftest.cmd`
  holds the import table to those two names (`link -dump -imports`), the
  tool's import gate, as section 11 says.
- **A manifest** asking for elevation (`requireAdministrator`), embedded
  as resource 1 of type 24 through `xhciinst.rc`, as `xhcicln.manifest` is;
  98, ME, 2000 and XP ignore it. On 2000 and XP the tool checks for
  administrator rights itself (section 7).
- **The version** is `src\xhci_version.h`'s `XHCI_VER_STR`, printed first
  on every run and compared with the package's `DriverVer` (section 4);
  `make-release.ps1` refuses a stale or mis-versioned tool as it does for
  `XHCICLN`.
- **The port switchover's gate** is the driver's own: the tool compiles
  `src\xhci_psw.c`, the pure half, and asks `XhciPswGate` whether a present
  controller is one the switchover applies to (section 8.2), so the list
  of Intel ids exists once.
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
2. the flavour directory named (`release-x86` and the rest) exists under
   that root, and its name's architecture is the tool's own;
3. every file of section 4.1's required rows is there, a regular file, not
   empty, and every file each INF names in `[SourceDisksFiles]` (and its
   decorated forms) is there too;
4. each INF's `[Version]` `DriverVer` version field equals the tool's
   `XHCI_VER_STR`, and all four agree - an INF from another release in the
   directory is refused by name;
5. each INF's `[Manufacturer]` line is the tool's architecture's: the x86
   INFs carry an undecorated models section or `NTx86` decorations and no
   `NTamd64`, the x64 INFs `NTamd64` decorations and no undecorated
   section (the INF gate's `PATH-NO9X` and `PATH-MFGDEC` rules, read again
   here because a user can mix directories);
6. every `.sys` is a PE image whose machine field is the tool's (`0x014C`
   for x86, `0x8664` for x64).

### 4.3 Mixing architectures

Refused, exit 1, each with the right command named: the 32-bit tool on
64-bit Windows (WOW64; section 3); the x64 tool on 32-bit Windows (it does
not start there: Windows reports it is not a valid application, which
`INSTALL.BAT` never invites, since it picks by `PROCESSOR_ARCHITECTURE`);
an x64 flavour directory given to the 32-bit tool or the reverse
(section 4.2 items 2, 5 and 6).

## 5. The NT route: Windows 2000, XP, XP x64, Vista and 7

### 5.1 Order of operations

1. Refusals (section 4), rights (section 7.2), the routines (section 7.3).
2. Read the system: every devnode, present or not (section 5.3); the
   driver key of each one bound to this package; test-signing mode on
   Vista x64 and 7 x64 (section 5.10).
3. Plan: for each device its action (section 5.4); print the plan. `/list`
   stops here, exit 0.
4. Ask the registry questions (section 8), then ask once whether to go on
   (`Y`; anything else, or end of input, cancels with exit 1 and changes
   nothing). `/yes` skips the confirmation; `/defaults` and `/set` answer
   the registry questions without asking.
5. Pre-stage every INF (section 5.6), detecting the Vista and 7 trap
   (section 5.7) as it goes.
6. Update the controllers (`xhci98.inf`).
7. Wait for the system to finish installing what the update made appear
   (`CMP_WaitNoPendingInstallEvents`, 60 seconds), and read the devnodes
   again: after an update from `2.3.x` the root hub, hubs and composite
   devices are new devnodes (record 13 section 10.13.4).
8. Update or install the root hub, hubs and composite entries, then
   `xhciuas.sys`'s devices, then the NHI (`xhcicm.inf`), then the
   tunnel-port filter (`xhcicmf.inf`).
9. Write the registry answers (section 8.4).
10. Read the devnodes a last time and report each device: before, action,
    result, after (version, problem code, started).
11. Report the restart (section 5.9), and on a run that changed anything
    ask whether to run `XHCICLN` (section 5.11).

Steps 6 to 8 stop at the first failure that leaves the order meaningless
(a controller update failed), report what was done and exit 2; a failure
on one hub or one UAS disk is reported and the rest go on, exit 2 at the
end.

### 5.2 The models section per system

Each INF's hardware ids are the model lines of the models section the
running system reads, chosen by the rule the engines apply to the
`[Manufacturer]` line's decorations:

- **Windows 2000** reads the undecorated section only: decorations on
  `[Manufacturer]` are an XP addition, which is why `xhci98.inf` shares
  `[XhciModels]` between 98 and 2000 (record 21 section 3.3). An INF whose
  only sections are decorated (the x64 INFs) has nothing for 2000, which
  section 4.2 has already refused on that system.
- **XP onward**: of the decorations whose architecture is the system's
  (`NTx86`, `NTamd64`, or `NT` with none) and whose version, when given
  (`.6.0`), is at most the system's, the most specific - the highest
  version, and with equal versions the one naming the architecture; none
  matching, the undecorated section.

The rule is pure (section 10) and is checked against the four INFs of both
architectures in the host suite: XP and 2000 get `[XhciModels]`,
`[XhciModels.NTx86.6.0]` goes to Vista and 7 x86, XP x64 gets
`.NTamd64`, Vista x64 and 7 x64 `.NTamd64.6.0`.

The tool reads the INFs with a parser of its own (section 10): `[Version]`,
`[Manufacturer]`, the models sections, `[SourceDisksFiles]` and, for
section 8, the `AddReg` lines. It needs no `setupapi.dll`, so the same
code reads the package on Windows 98.

### 5.3 Reading the devices

The device tree is read through `cfgmgr32.dll` on every system, the walk
`XHCISNAP` makes (`CM_Locate_DevNode`, `CM_Get_Child`, `CM_Get_Sibling`,
`CM_Get_Device_ID`, `CM_Get_DevNode_Status`,
`CM_Get_DevNode_Registry_Property`), for each devnode: its instance id,
`HardwareID` and `CompatibleIDs`, `Service`, `Driver`, `UpperFilters`,
`DeviceDesc`, its status and problem code. Non-present devnodes, which the
uninstall needs (section 9), come from `SetupDiGetClassDevs` with
`DIGCF_ALLCLASSES` and no `DIGCF_PRESENT`. A driver key's values
(`DriverVersion`, `DriverDate`, `InfPath`, `ProviderName`) are read from
`HKLM\SYSTEM\CurrentControlSet\Control\Class\<Driver>`.

A device is **matched** by an INF when one of its hardware or compatible
ids equals one of the INF's model ids for the running system, compared
without case.

### 5.4 The action per device

| The matched device | Action |
|---|---|
| No driver: no `Service`, or problem `CM_PROB_NOT_CONFIGURED` (1), `CM_PROB_FAILED_INSTALL` (28) or `CM_PROB_REINSTALL` (18) | install |
| Bound to this INF's service (`xhci98`, `xhciuas`, `xhcicm`), driver key `DriverVersion` older than the package | update |
| The same, equal to the package's | current, nothing done; `/force` reinstalls |
| The same, newer than the package | refused for that device and said; `/force` installs the older release (going back) |
| A tunnel port (`xhcicmf.inf`) bound to `pci` without `xhcicm` in `UpperFilters` | install the filter (the INF keeps `pci.sys` through `machine.inf`'s section) |
| A tunnel port with `xhcicm` in `UpperFilters` | as for an own service |
| Bound to any other service | foreign: listed, left (decision D3) |

The controller's model id is the class id `PCI\CC_0C0330`, so every xHCI
controller in the machine is matched; one running another vendor's driver
is foreign and left. The same holds for a UAS device another driver holds.
An xHCI controller with no driver is installed: it is what the package is
for.

A driver key's version is compared field by field as four numbers. A key
with no `DriverVersion` (a `1.x` install, or a hand-edited key) is older.

### 5.5 Installing: `UpdateDriverForPlugAndPlayDevices`

`newdev.dll`'s `UpdateDriverForPlugAndPlayDevicesA(NULL, hwid, inf,
INSTALLFLAG_FORCE, &reboot)`, documented from Windows 2000, installs the
best driver of the given INF on every present device whose hardware or
compatible ids include `hwid`. Three properties of it decide how it is
called:

- **`INSTALLFLAG_FORCE` always.** Without it the routine installs only a
  driver it ranks better than the installed one, and Windows 2000 does not
  rank an unsigned driver's date (the release notes' "use Have Disk"); a
  reinstall of the same version (`/force`) needs it as well. The decision
  whether to install at all is section 5.4's, made before the call.
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
  reports (`ERROR_AUTHENTICODE_*`, `ERROR_NO_CATALOG_FOR_OEM_INF` and the
  rest by number).

### 5.6 Pre-staging: `SetupCopyOEMInf`

Every INF of the package is copied into the system's INF store with
`SetupCopyOEMInfA(inf, packageDir, SPOST_PATH, SP_COPY_NOOVERWRITE, ...)`
before any device is touched, so a device that appears later - a UAS disk
plugged next week, a hub, the root hub after an update from `2.3.x`, an
NHI that appears only with a dock - finds its driver with no question
about where. `SP_COPY_NOOVERWRITE` returning `ERROR_FILE_EXISTS` with the
existing `oemNN.inf`'s name means an identical INF is already there; that
is the input of section 5.7.

Where the files then come from differs, and the release notes say so:
on Vista and 7 the whole package goes into the driver store and a later
install takes its files from there; on 2000 and XP the store holds the INF
and its `.pnf`, which records the package directory as the source, so a
later install reads the files from where the package was when the tool ran
- a removable disk then has to be in its drive. The tool prints, on 2000
and XP, "keep this package where it is, or copy it to the hard disk and
run INSTALL.BAT again from there", and does not copy the package itself
(decision D7).

### 5.7 The Vista and 7 trap: an identical INF in the driver store

On Vista and 7, when the store already holds an INF byte-identical to the
package's, Windows installs from the stored package, with its files, so a
build whose INF did not change keeps the old `.sys` (README, "Upgrading
the driver"). The tool detects it per INF:

1. `SetupCopyOEMInf` with `SP_COPY_NOOVERWRITE` fails with
   `ERROR_FILE_EXISTS` and names the stored `oemNN.inf`;
2. `SetupGetInfDriverStoreLocationA` (Vista onward) gives the stored
   package's directory;
3. each file of the INF's `[SourceDisksFiles]` is compared byte for byte
   with the package's.

All equal: nothing to do, the store holds this package. Any difference is
the trap. Without `/restage` the tool installs nothing from that INF,
reports "the driver store holds a different build under an identical INF;
run INSTALL.BAT /restage to replace it" and ends with exit 4. With
`/restage` (decision D5) it removes the stored package first,
`SetupUninstallOEMInfA(oemNN.inf, SUOI_FORCEDELETE, NULL)`, then stages
and installs as usual; the devices still on the old package keep running
its files until their update, which follows at once. Released packages
never meet the trap - each release has its own `DriverVer` - so this is a
development-build route and is said to be.

On 2000 and XP `ERROR_FILE_EXISTS` is no trap: the install reads its files
from the INF's own directory, the package (read on the legs; section 12).

### 5.8 After the controller

From `2.4.0.0` onward the root hub, hubs and composite devices keep their
ids, so step 8 updates them as section 5.4 says: each is bound to `xhci98`
with an older driver key, which is what the release notes' "then on xHCI98
USB 3.x Root Hub" does by hand. From `2.3.x` they are new devnodes after
the controller's update (record 13 section 10.13.4):

- **XP and XP x64** show Found New Hardware for each, and the unsigned
  question. The tool calls `CMP_WaitNoPendingInstallEvents` and, on its
  timeout (the wizard waits for the user), installs the devnodes still
  without a driver itself (section 5.4, "install"). Whether Windows' wizard
  then closes, stays and installs again, or conflicts is read on the legs
  (section 12); the release notes' advice to have a keyboard and mouse
  that do not go through this driver stands either way.
- **32-bit Vista and 7** keep the earlier release running until the
  restart the helper asks for: the new devnodes appear after it. The tool
  reports exit 3 with "restart, then run INSTALL.BAT again", and the
  second run finds them installed (Windows installs them from the staged
  package) or installs them.
- **2000, Vista x64 and 7 x64** install them at once from the staged INF;
  step 8 finds them current.

`XHCI98\` entries left behind by the update are `XHCICLN`'s (section 5.11).

### 5.9 Restart

A restart is needed when `UpdateDriverForPlugAndPlayDevices` sets its
`bRebootRequired`, when a device ends at `CM_PROB_NEED_RESTART` (14), or
when a removal or deletion is deferred to the next start (section 9).
The tool never restarts the machine: it says so last, and the exit code
is 3. On 32-bit Vista and 7 the first install of `xhcihlpr.sys` asks for
it (release notes, "Installing", step 3), and the tool says that the
controller stays at Code 28 (7) or Code 1 (Vista) until then.

### 5.10 Signatures and test-signing mode

On Vista x64 and 7 x64 the x64 driver loads only in test-signing mode or
on an F8 start with signature enforcement disabled (AGENTS.md; design
record 19). The tool reads
`HKLM\SYSTEM\CurrentControlSet\Control\SystemStartOptions`, the boot
options the loader was started with, and looks for the word
`TESTSIGNING`. Absent, it says before the confirmation that the driver will
not load (Code 52 on 7, Code 39 on Vista) until `bcdedit /set testsigning
on` and a restart, or an F8 start, and prints the README section's steps;
it goes on if the user confirms, because an F8 start is a legitimate way to
run. It never runs `bcdedit`. Whether that value carries the word on these
two systems is read on the legs (section 12).

### 5.11 `XHCICLN` at the end

When the run changed anything and `XHCI98\` devnodes exist (the walk of
section 5.3 sees them), the tool asks, last, "Run XHCICLN to list and
remove the old entries an update from 2.3.x left? [y/N]"; only `y` runs
it. `/yes` does not answer this question (never by default); `/clean`
answers it yes. It starts `..\xhcicln\XHCICLN.EXE` (`XHCICL64.EXE` from the
x64 tool) with `-remove` in the same console and waits; `XHCICLN` asks its
own question and keeps every refusal of record 13 section 10.13.8, so a run
before the restart it needs removes nothing and says why. Its exit code is
reported beside the tool's own and does not change it.

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
  installer searches for INFs. It is the route the tool takes.

### 6.2 The route

1. Copy the flavour directory's files - the INFs, the `.sys` files - to
   `%windir%\XHCI98`, a directory of this project's own, replacing what an
   earlier run left there. A fixed local copy, because `DevicePath` names a
   directory for good and the package may be on a CD or a floppy.
2. Append `;%windir%\XHCI98` (expanded) to `DevicePath` if it is not
   already in the list, compared without case; never reorder or remove
   another entry.
3. The device installer then finds the package's INFs in a directory it
   searches by itself. An INF found there names its files relative to its
   own directory, so they come from `%windir%\XHCI98` with no prompt.
   Whether the 98 SE and ME wizards search a `DevicePath` entry other than
   `INF` without a box ticked, whether the driver database is rebuilt for
   it, and where the files are copied from are read on the legs before the
   route is documented as working (section 12); if a wizard does not
   search it, the tool's printed steps name "Specify a location" with
   `%windir%\XHCI98` typed once, which still needs no package media.
4. The OS's own files the INF asks for through `LayoutFile=layout.inf`
   (`usbd.sys`, `usbui.dll`; and `hidclass.sys` for the first HID device)
   come from the Windows CD as today; the tool says so.

### 6.3 What it prints

For each xHCI controller (`PCI\CC_0C0330` among its ids, from the
configuration manager's tree through `cfgmgr32.dll`, as `XHCISNAP` reads it
on these systems):

- **no driver**: Device Manager, the controller under "Other devices",
  Properties, Driver, Update Driver, Next, "Search for the best driver",
  Next with no box ticked, Next, Finish; Windows 98 SE may ask for its CD;
  restart when asked, and on ME restart anyway;
- **an older `xhci98.sys`**: the same Update Driver route on "xHCI98 USB
  3.x eXtensible Host Controller", "Search for a better driver than the
  one your device is using now", no box ticked; restart afterwards
  although Windows does not ask (release notes, "Updating from 2.y.y.y");
  and from `2.3.x` the keyboard-and-mouse paragraph of "From 2.3.x or
  earlier";
- **current**: nothing; the root hub's Driver tab may show the earlier
  date until it is updated the same way;
- **another vendor's driver**: left (decision D3).

The first UAS disk and the first NHI then find `xhciuas.inf` and
`xhcicm.inf` in the same directory. The run ends with exit 4: staged,
steps left for the user.

### 6.4 After the restart

Running `INSTALL.BAT` again after the restart reports each device as
section 6.3 sees it and asks the registry questions for the controllers
now bound to `xhci98` (section 8). It does nothing else on these systems.

## 7. Modes, switches, rights, the routines, exit codes, the log

### 7.1 Switches

| Switch | Meaning |
|---|---|
| (none) | install or update, with the plan shown and one confirmation |
| `/list` | the plan, the registry questions that would be asked and their current values, the uninstall plan with `/uninstall`; nothing changed, nothing asked, exit 0 (or 1 on a refusal) |
| `/yes` | no confirmation; never answers `XHCICLN`'s question or the uninstall warning's typed word (section 9.1) |
| `/defaults` | ask no registry question, keep every current value (the INF's default on a new install) |
| `/set Name=Value` | answer one registry question, repeatable; validated against the value table (section 8.1); applies even where the question would not be asked |
| `/force` | reinstall a current device, and install over a newer one |
| `/restage` | Vista and 7: replace a stored package the trap holds (section 5.7) |
| `/clean` | answer the `XHCICLN` question yes |
| `/uninstall` | section 9 |
| `/debug` | `INSTALL.BAT` only: the `debug-` flavour directory |
| `/nopause` | do not wait for Enter before closing |
| `/log path` | write the log there |

An unknown switch, `/set` with an unknown name or a value outside its
allowed set, or a contradiction (`/defaults` with `/set`, `/list` with
`/yes`) is refused with exit 1 before anything is read. The tool waits for
Enter before it exits, because Explorer and the Vista and 7 elevation
prompt open its console window and close it at exit; `/nopause` is for
scripts and the legs.

### 7.2 Rights

On Vista and 7 the manifest asks for elevation. On 2000 and XP the tool
opens the service control manager with `SC_MANAGER_ALL_ACCESS`; a failure
with `ERROR_ACCESS_DENIED` refuses (exit 1) with "run it as an
administrator". `/list` runs without rights and says what it could not
read.

### 7.3 The routines

| Routine | DLL | Systems | Step |
|---|---|---|---|
| `UpdateDriverForPlugAndPlayDevicesA` | `newdev.dll` | 2000 onward | 5.5 |
| `SetupCopyOEMInfA` | `setupapi.dll` | 2000 onward (and resolved on 98 for the record) | 5.6 |
| `SetupUninstallOEMInfA` | `setupapi.dll` | XP onward, where it resolves (section 9.2) | 5.7, 9.2 |
| `SetupGetInfDriverStoreLocationA` | `setupapi.dll` | Vista onward | 5.7 |
| `SetupDiGetClassDevsA`, `SetupDiEnumDeviceInfo`, `SetupDiGetDeviceInstanceIdA`, `SetupDiGetDeviceRegistryPropertyA`, `SetupDiSetDeviceRegistryPropertyA`, `SetupDiSetClassInstallParamsA`, `SetupDiCallClassInstaller`, `SetupDiGetDeviceInstallParamsA`, `SetupDiOpenDevRegKey`, `SetupDiDestroyDeviceInfoList` | `setupapi.dll` | 2000 onward | 5.3, 8.4, 9 |
| `CM_Locate_DevNodeA`, `CM_Get_Child`, `CM_Get_Sibling`, `CM_Get_Device_IDA`, `CM_Get_DevNode_Status`, `CM_Get_DevNode_Registry_PropertyA` | `cfgmgr32.dll` | all | 5.3, 6.3 |
| `CMP_WaitNoPendingInstallEvents` | `cfgmgr32.dll` (`setupapi.dll` on 2000) | 2000 onward | 5.1 |
| `IsWow64Process` | `kernel32.dll` | XP SP2 onward, absent is not WOW64 | 3 |

Every one is looked up at run time. `/list` prints which resolved, which
is how the legs read them on each system (runtime, through this tool's own
call; `legal-provenance.md` section 4 gains a row when a reading rests on
one).

### 7.4 Exit codes

| Code | Meaning |
|---|---|
| 0 | done: everything current or installed, no restart; or `/list` |
| 1 | refused before any change: location, architecture, rights, a routine missing, a bad switch, cancelled |
| 2 | stopped or failed part-way: the log and the report say what was done |
| 3 | done, restart needed (and, where said, run `INSTALL.BAT` again after it) |
| 4 | done as far as the tool can: steps remain for the user - Windows 98 SE and ME always; on NT a device left by the trap (5.7) or an id shared with a foreign device (5.5) |

When two apply, the larger code wins, except that 2 beats 3 and 4 (a
failure is said first). `INSTALL.BAT` passes the code on.

### 7.5 The log

Every run appends to `XHCIINST.LOG` at the root of the system drive
(`%SystemDrive%\` on NT, the drive of the Windows directory on 98 and ME;
decision D8), or where `/log` says: a header with the tool's version, the
system's version, build and service pack, the architecture and WOW64, the
package path and flavour, the switches, the local time; then every line
the console shows, every routine that did not resolve, every call made
with its arguments and result (`GetLastError` in hex), every registry
value written with its old and new value, and the exit code. The path is
printed first and last, and the release notes ask for the file in a field
report. A log that cannot be opened is said and does not stop the run.

## 8. Registry values (owner, 2026-10-15)

### 8.1 The curated set

The tool asks about a short list of the values the INFs write, chosen as
the ones a user plausibly wants to change after reading README "Tuning"
and the release notes' "Registry settings" - a behaviour switch with a
user-visible effect, off or on by default for a reason the user can judge
- and none of the rest:

| Value | INF | Asked when | Allowed (asked) | Allowed (`/set`) | Meaning shown |
|---|---|---|---|---|---|
| `XhciIntelPortSwitch` | `xhci98.inf` | a present controller passes the driver's `XhciPswGate` (Intel 7-, 8- and 9-series, C610) | 0, 1 | 0, 1, 2 | route the shared connectors from EHCI to this driver |
| `XhciInterruptMode` | `xhci98.inf` | any xHCI controller | 0, 1, 2; 3 on Vista and 7 only | 0 to 3 | automatic, line only, polling always, message only |
| `XhciPollFineTimer` | `xhci98.inf` | 2000 onward | 0, 1 | 0, 1 | a finer system clock while polling |
| `XhciSelectiveSuspend` | `xhci98.inf` | any xHCI controller | 0, 1 | 0, 1 | put idle devices to sleep |
| `XhciForceBulkOnly` | `xhci98.inf` | any xHCI controller | 0, 1 | 0, 1 | Bulk-Only instead of UAS for every disk |
| `XhciCmMode` | `xhcicm.inf` | a present NHI `xhcicm.inf` matches | 0, 1 | 0 to 3 | the connection manager off or automatic (2 and 3 are the record 21 overrides, `/set` only) |
| `XhciCmApprove` | `xhcicm.inf` | as `XhciCmMode` | 0, 1 | 0, 1 | approve Thunderbolt devices automatically |
| `XhciCmSafeEject` | `xhcicm.inf` | as `XhciCmMode` | 0, 1 | 0, 1 | offer Safely Remove for the dock's devices |

Not asked: the log values (`SNAPON.BAT` and `SNAPOFF.BAT` own them, with a
restart the user is told about), the fourteen Linux quirk values (record
20: their defaults are the point), the poll periods, the moderation
interval, `XhciMaxTransferKB`, `XhciWideMap`, `XhciFirstEnum*`,
`XhciIntelIrqRoute` (a repair that is on by default and turned off only to
diagnose), `XhciTolerance`, `XhciIntervalCap`, `XhciAvgTrbEsit`,
`XhciAccept096`, `XhciUsb2HubSpeedFallback`, and `XhciSilentInstall`,
which needs its class list beside it (`tools\silent\`'s batch files set
both). Any of them stays settable by hand, as today.

Each value's default is taken from its INF's `AddReg` line for the running
system's install section, not from the table; a value the INF does not
write is not asked, and the host suite fails if a table row names a value
an INF of `src\` does not write (section 10). The `XhciCm*` rows follow
`xhcicm.inf` as task 39-A.1 writes it, and the allowed sets are checked
against record 21 section 8 when that section is final.

### 8.2 Applicability (owner, 2026-10-15)

A question is shown only when the hardware or the system it affects is
there; `/set` may still set any value:

- **Hardware**, from the PCI ids of present devices (the walk of section
  5.3 on NT; on 98 and ME the same walk through `cfgmgr32.dll`, with the
  `PCI\VEN_vvvv&DEV_dddd` part of each id): `XhciIntelPortSwitch` when
  `XhciPswGate((dev << 16) | ven)` is 1 for a present xHCI controller -
  the driver's own list, compiled in from `src\xhci_psw.c` (section 3);
  the `XhciCm*` values when a present device is matched by `xhcicm.inf`'s
  models for the system (section 5.2).
- **System**: `XhciPollFineTimer` from Windows 2000 on (98 and ME read it
  and do nothing); `XhciInterruptMode`'s value 3 on Vista and 7 only (on
  every other system it fails the start).
- **A driver key to write**: the values are asked only for devices that
  will be bound to the value's driver after the run - installed or updated
  by it, or already current. On 98 and ME that is only a controller
  already running `xhci98.sys` (section 6.4).

The applicability rule is a pure function of the value, the system and the
present ids, and is host-tested row by row (section 10).

### 8.3 The question

Per value, once, for every device it applies to:

```
XhciSelectiveSuspend - put idle devices to sleep
  now: 0 (xHCI98 USB 3.x eXtensible Host Controller, PCI\VEN_1B36&DEV_000D)
  default: 0     allowed: 0, 1
  new value [Enter keeps 0]:
```

`now` is the value in each device's driver key, or "not set" (the
install's `AddReg` then writes the default), or "new install: the default".
Two controllers with different values are listed apart and asked once: the
answer goes to both. Enter keeps each device's value. An answer outside the
allowed set is asked again; end of input keeps every value.

### 8.4 The write

After the installs (section 5.1 step 9), each changed value is written as a
`REG_DWORD` into the driver key the INF's `HKR` lines write - the device's
driver (software) key, opened with `SetupDiOpenDevRegKey(...,
DIREG_DRV, KEY_SET_VALUE)` on NT and as
`HKLM\System\CurrentControlSet\Services\Class\<Driver>` on 98 and ME - and
only a value the user answered with a value different from the current one,
or named with `/set`. The INFs' don't-overwrite flag (`0x00010003`) is the
install's convention and the tool keeps it: nothing but an explicit answer
changes a value, and the tool writes after the install so the install's
defaults cannot overwrite the answer. Every write is logged with its old
and new value, and the tool says the value takes effect when the driver
next starts (a restart, or a disable and enable of the device).

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
is the only way past the word, and the tool says it was used.

### 9.2 NT: what is removed, in order

The set is every devnode, present or not, bound to `xhci98`, `xhciuas` or
`xhcicm`, every devnode whose device id is one of the bus's own forms
(`USB\XHCI98_ROOT_HUB` and the other three bases; `XHCI98\ROOT_HUB` and
the three old forms, record 13 section 10.13.8's rule 1, taken from
`XHCICLN`'s classification), and every devnode whose `UpperFilters` holds
`xhcicm`. Then:

1. **The filter name.** For each tunnel port, `xhcicm` is removed from its
   `UpperFilters` multi-string and the rest kept in order
   (`SetupDiSetDeviceRegistryPropertyA(SPDRP_UPPERFILTERS)`); a value left
   empty is deleted with the same call and a zero length. The port's driver
   stays `pci.sys`. The change takes effect when the port restarts:
   `DIF_PROPERTYCHANGE` with `DICS_PROPCHANGE`, or a restart reported. The
   whole value is never removed while another name is in it.
2. **The devnodes**, children before parents (UAS devices, composite and
   hub entries, root hubs, NHIs, controllers last), each with
   `SetupDiCallClassInstaller(DIF_REMOVE)` and `DI_REMOVEDEVICE_GLOBAL`,
   the route Device Manager's Uninstall and `XHCICLN` take;
   `DI_NEEDREBOOT` or `DI_NEEDRESTART` after it is "removed, restart
   pending". Device and function devnodes of other drivers beneath (a
   mouse's `hidusb`, a stick's `usbstor`) are not this package's and are
   left: they go non-present with their parent.
3. **The OEM INFs**: every `oemNN.inf` in `%windir%\inf` whose `[Version]`
   `Provider` is this package's and whose install sections name one of the
   three services, read with the tool's own parser. Where
   `SetupUninstallOEMInfA` resolves (XP SP2 and later, the legs read which
   systems have it), it removes each, flags 0, so a package still in use
   is refused and reported rather than forced; on Vista and 7 that also
   removes the driver-store package. Where it does not (Windows 2000), the
   documented manual route: delete `oemNN.inf` and its `oemNN.pnf`
   (Microsoft's driver-package uninstall guidance for systems without the
   routine).
4. **The services** `xhci98`, `xhciuas`, `xhcicm`, `xhcihlpr` and the
   `2.3.x` `xhcimsi`, each with `DeleteService` (advapi32); a running or
   boot-start service is marked for deletion and removed at the next start,
   reported as a pending restart.
5. **The files** in `%windir%\system32\drivers`: `xhci98.sys`,
   `xhciuas.sys`, `xhcicm.sys`, `xhcihlpr.sys`, `xhcimsi.sys`. A file in use
   is scheduled with `MoveFileEx(..., MOVEFILE_DELAY_UNTIL_REBOOT)` and
   reported as pending.

The order is the one Vista needs (an INF a device still uses cannot be
uninstalled without forcing) and the one that keeps 2000 and XP from
reinstalling the driver if Windows rescans between steps: the INFs go
before anything could ask for a rescan, and the tool asks for none.

### 9.3 What it never removes

The OS's own `usbd.sys` and `usbui.dll`: the install had Windows copy them
from its own source because nothing on an xHCI-only machine had placed them
(`legal-provenance.md` section 5), they are Windows' files, other USB
drivers import `usbd.sys`, and `usbui.dll` draws every USB property page.
Device and function devnodes of other drivers (above). Any value or
devnode the walk cannot attribute to this package. Another release's
package is removed only through the INF rule of 9.2 step 3, which matches
every release of this project by provider and service.

### 9.4 98 SE and ME: best effort, then the manual steps

No documented interface removes a devnode there: `CM_Uninstall_DevNode`
is a stub on both (record 13 section 10.13.8, read statically from each
system's `CFGMGR32.DLL`). So the tool:

1. removes what it owns: `%windir%\XHCI98` and its entry in `DevicePath`;
2. lists the `INF\OTHER` files whose `Provider` is this package's, and the
   driver files present in `%windir%\system32\drivers`;
3. prints the steps: Device Manager, "xHCI98 USB 3.x eXtensible Host
   Controller", Remove (which removes the root hub and everything beneath
   it), then the same for "xHCI98 USB Attached SCSI Storage" and the NHI
   if present; restart; then delete the listed `INF\OTHER` files and the
   driver files; restart. It deletes neither itself: both are the device
   installer's records while a devnode still names them.

Exit 4.

### 9.5 Restart

Reported as in section 5.9, exit 3, the pending items named.

## 10. The pure half and the host suite

`xhciinst\inst_core.c` (with `inst_core.h`) holds everything that is a
function of its inputs, C89 with no Windows header, so `test\test_inst.c`
links it in `test\run-host-tests.cmd` under `/Za` like every other suite,
and the tool links the same file:

- the INF parser: sections, lines, `key = value`, comma fields, quotes,
  comments, `%strings%` expanded from `[Strings]` for `Provider`;
- `DriverVer` and four-part version parsing and comparison;
- section 5.2's models-section choice;
- id matching against hardware and compatible id multi-strings;
- section 5.4's action table and section 9.2's devnode selection;
- the PE machine read from a header buffer;
- the `DevicePath` append (section 6.2);
- the `UpperFilters` edit (section 9.2 step 1);
- `SystemStartOptions`' test-signing word;
- the switch parser, `/set`'s validation and section 7.4's combination
  of exit codes;
- the registry value table, its applicability (section 8.2) and its
  answer parser.

The suite reads `src\xhci98.inf`, `src\xhci98-amd64.inf`, the two UAS INFs
and, once task 39-A.1 has merged, the four `src\cm\` INFs, and checks that
each value-table row is written by its INF with the default the table
expects and that section 5.2's choice gives each system its section.
`xhciinst\selftest.cmd` runs the executables' switch refusals
(`/frobnicate`, `/set Bogus=1`, `/list /yes`), the import table, and a
`/list` against a fixture package directory built from `src\`, which
`build-driver.cmd` runs whenever the executables have been built, as it
runs `XHCICLN`'s.

## 11. Gates, packaging and documents

- **build-driver.cmd**: a step after `XHCICLN`'s, "xhciinst self-test",
  when `xhciinst\XHCIINST.EXE` exists; the host suite runs with the others.
- **The import gate for the tool**: `xhciinst\selftest.cmd` refuses an
  import table naming any DLL but `KERNEL32.dll` and `ADVAPI32.dll`, on
  both executables; it is the same claim `XHCICLN`'s `NOTICE.TXT` makes, now
  enforced. The kernel-mode import gate does not apply (user mode).
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
| F, fresh install | 98 SE, ME, 2000, XP, XP x64, Vista x86, Vista x64, 7 x86, 7 x64 | NT: one run installs the controller and root hub; the routines resolved; the restart reported (32-bit Vista and 7); a UAS disk plugged after the run installs from the staged INF with no question about where. 98 and ME: `DevicePath` written; the wizard finds the controller with no box ticked; the files come from `%windir%\XHCI98`; the CD prompt for `usbd.sys` (98 SE); a UAS disk plugged later finds `xhciuas.inf` |
| U24, update over `2.4.0.0` | the nine | NT: the controller, root hub, hubs and composite entries updated, each driver key's version the new one, every `.sys` equal to the package's; 2000 with no Have Disk; the Vista and 7 trap - this branch's INFs are byte-identical to `2.4.0.0`'s until the `3.0.0.0` restamp, so a leg with the unrestamped package reads the trap's detection and `/restage`, and a leg with a restamped package the normal update; 98 and ME: the update wizard finds the new driver by search |
| U23, update over `2.3.1.0` | 98 SE, 2000, 7 x64 (at least) | the new devnodes of record 13 section 10.13.4; on 7 x64 installed by step 8 or by Windows; the `XHCICLN` question and its run after the restart |
| UN, uninstall and reinstall | 98 SE, 2000, XP, 7 x86, 7 x64 | the warning and the word; every devnode, the filter name, the OEM INFs (2000: the manual route; XP onward: `SetupUninstallOEMInf`, which systems resolve it), the services and files gone or pending; a restart; the controller back as an unknown device; `INSTALL.BAT` installing it again. 98 SE: the steps printed and followed |
| R, registry questions | 98 SE (after the install and restart), 2000 | each applicable question shown with now, default and allowed; a non-applicable one not shown (no Intel switchover controller in QEMU, no NHI); an answer written to the driver key, Enter keeping, `/set` and `/defaults` |
| L, `/list` and the refusals | every leg's first run | nothing changed (a registry and file listing before and after); the ZIP-folder refusal, a mixed architecture, a mis-versioned INF |

What each system's legs settle that nothing here has read:

- **98 SE and ME**: whether the wizard searches a `DevicePath` entry other
  than `INF` and copies the files from it (section 6.2); whether
  `SetupCopyOEMInfA` resolves (for the record); the uninstall steps.
- **2000**: that `INSTALLFLAG_FORCE` takes the update the search refuses;
  the `.pnf` route of section 9.2; `CMP_WaitNoPendingInstallEvents` in
  `setupapi.dll` or `cfgmgr32.dll`.
- **XP and XP x64**: what the Found New Hardware wizard does when the tool
  installs the new devnode under it (section 5.8); that `ERROR_FILE_EXISTS`
  is no trap there (5.7); `SetupUninstallOEMInf` on XP SP3 and XP x64 SP2.
- **Vista and 7**: the trap's detection and `/restage`; the restart on
  32-bit; `SystemStartOptions`' `TESTSIGNING` on the x64 guests in
  test-signing mode and on an F8 start.

The legs read the Thunderbolt half only as far as QEMU allows: no NHI or
tunnel port exists in these guests, so `xhcicm.inf` and `xhcicmf.inf` are
pre-staged and their questions not shown, and the install on an NHI is
read on `usb4-emu`'s guests or the P14s with the Phase 39 work.

## 13. Decisions taken in writing this (orchestrator decisions, owner to confirm)

- **D1** The executables in `tools\xhciinst\`, `INSTALL.BAT` at the root;
  one batch file, the uninstall behind `/uninstall` (section 2).
- **D2** Windows 98 SE and ME through `DevicePath` and a local copy in
  `%windir%\XHCI98`, never `INF\OTHER` or `SETUPX` (section 6).
- **D3** A device bound to another vendor's driver is listed and left,
  including an xHCI controller; Device Manager remains the way to replace
  a vendor driver (section 5.4).
- **D4** A device running a newer release is not downgraded without
  `/force` (section 5.4).
- **D5** The Vista and 7 trap is reported by default and replaced only with
  `/restage` (section 5.7).
- **D6** Test-signing mode is read and reported, never changed
  (section 5.10).
- **D7** On 2000 and XP the package is staged from where it is, not copied
  to the disk (section 5.6).
- **D8** The log at the root of the system drive, appended (section 7.5).
- **D9** The curated registry set of section 8.1, asked only where it
  applies, written only on an explicit answer.
- **D10** `XHCICLN` is offered last, asked, never by default (section
  5.11).
- **D11** The uninstall leaves the OS's `usbd.sys` and `usbui.dll` and
  other drivers' devnodes, uses `SetupUninstallOEMInf` without force, and
  on Windows 2000 deletes the `oemNN.inf` and `.pnf` (section 9).

## 14. Open, and the risks

- The 98 SE and ME route rests on `DevicePath` being searched by the
  wizards; until the legs read it, section 6.2's fallback (a typed path)
  is what the release notes may promise.
- The XP wizard's behaviour when the tool installs under it (section 5.8)
  is unread.
- `xhcicmf.inf` is a draft pending task 39-0.4 (record 21 section 3.3): the
  tool follows whatever model lines it ships with, and the filter install
  on a tunnel port is not read in any guest here.
- The `XhciCm*` allowed sets follow task 39-A.1's INF and record 21
  section 8 as they stand on 2026-10-15.
- `SystemStartOptions` as the test-signing signal is a reading to take,
  not a documented contract; the tool's warning is advice, never a
  refusal.

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
