# Build and Test Guide

## Prerequisites

### Host Machine (Windows)
- Windows 11 x64 is the current host development machine
- Visual C++ 6.0 unpacked from `tools/MSVC600.zip` into `tools/MSVC600`
- Windows 2000 DDK unpacked from `tools/WIN2KDDK.EXE` into `tools/ntddk`
- Open Watcom 2.0, installed to `C:\WATCOM`, used only for the `xhciqual/`
  DOS qualifier and never for the driver
- QEMU, installed normally on the host

Neither driver toolchain is installed machine-wide. Both live inside the
repository, are used in place, and write no registry key: MSVC 6.0 runs from
`tools\MSVC600\VC98\BIN`, and the DDK is reconstructed into `tools\ntddk`. Every
script that compiles, links, or dumps a binary derives those paths from its own
location, so a clone builds wherever it is unpacked and the whole project is
self-contained apart from QEMU and Open Watcom. `DDKROOT` and `MSVC6` override
each half for a host that has one installed elsewhere.

Open Watcom 2.0 is the exception: it is a real host install. The DOS qualifier
is a 32-bit protected-mode DOS binary with the DOS/32A extender embedded as its
EXE stub. Neither MSVC 6.0 nor the DDK can produce that, so `xhciqual/` is built
by a different compiler from everything else in this repository.

- Install it to `C:\WATCOM`. `xhciqual\build.cmd` defaults to that path and
  `xhciqual\SETENV.BAT` hard-codes it; set `WATCOM` in the environment first to
  build from anywhere else.
- Build with `xhciqual\build.cmd`, run from the `xhciqual\` directory. It
  points `INCLUDE` at `%WATCOM%\H` (the DOS headers) because `SETENV.BAT`
  sets up the NT headers for the host-side unit tests, and a DOS build against
  those fails in ways that read as source errors.
- You only need it to touch the qualifier. `scripts\build-driver.cmd`,
  `make-package.ps1` and the host driver suite never invoke it. The one place it
  becomes mandatory is cutting a release: `make-release.ps1` publishes
  `XHCIQUAL.EXE` beside the driver and refuses a binary older than any `.c`,
  `.h` or `.asm` next to it, so a stale qualifier stops the release. `-SkipQualtool`
  publishes without it and the resulting directory is incomplete and says so.
- `xhciqual\README.md` has the tool's own build, test and usage documentation.

Two consequences worth knowing before they bite:

- A repository path containing a space is a problem for the DDK half only.
  `setenv.bat` takes its base directory verbatim and cannot be quoted, so a
  space splits it and every derived path is wrong. `build-driver.cmd` falls back
  to the 8.3 short name and refuses with an explanation when the volume does not
  generate one (8dot3 creation is off on many non-system volumes). Cloning to a
  path without a space avoids the question.
- `tools/` is git-ignored, so a fresh clone has the scripts but not the
  toolchain. The two setup scripts below are the whole of the difference.

Both targets need the same thing, a `usbport.sys` for the miniport to register
with, but they get it from different places, and the two prerequisites are
mutually exclusive.

### Target A: Win98 SE (guest or real machine)
- NUSB 3.3 installed. It provides the Windows 2000-derived `usbport.sys` + `usbhub20.sys` USB 2.0 stack that `xhci98.sys` (a `usbport` miniport) binds to. See "Installing the usbport USB 2.0 Stack (NUSB)" below. Without it there is no `usbport.sys` for the miniport to register with. Installing NUSB places those files unconditionally, with no EHCI controller required (`_NUSB.INF` `[DefaultInstall]` copies them; confirmed on real xHCI-only hardware). An emulated `-device usb-ehci` in QEMU is optional, useful only as a live miniport reference.
- `usbd.sys` present in `C:\WINDOWS\SYSTEM32\DRIVERS`. NUSB does not ship it and Win98 installs it only with the USB 1.1 stack, yet `usbhub20.sys` imports it. See Phase 2a task 6 in `docs/contributing/roadmap.md`.

### Target B: Windows 2000 SP4 (guest or real machine)
- Service Pack 4 installed, or the standalone USB 2.0 update KB319973. This provides the same stack, natively: `usbport.sys`/`usbhub20.sys`/`usbehci.sys` are Microsoft's own builds here.
- Do not install NUSB on Win2000. It is a 9x package; the native stack is already the thing NUSB back-ports.
- Win2000 installs the USB files on demand, per detected controller. A machine that has never had a USB controller attached has no usbport stack on disk. See "Windows 2000 SP4 Target VM" for the staging procedure and the `usbd.sys` bugcheck it avoids.

## Automated Phase 1 Host Setup

Use the Phase 1 PowerShell helpers from a normal Windows PowerShell prompt.

From a fresh clone, the shortest path to a built driver is three commands. The
first two are one-time, and neither needs elevation:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-msvc6.ps1         # -> tools\MSVC600
powershell -ExecutionPolicy Bypass -File scripts\install-w2kddk-cabs.ps1  # -> tools\ntddk
scripts\build-driver.cmd both
```

They expect `tools\MSVC600.zip` and `tools\WIN2KDDK.EXE` to be present; the
download links are in `README.md` under "Toolchain", and `tools/` is git-ignored
because neither archive is this project's to redistribute.

Run all setup helpers (both target VMs):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-all.ps1 `
  -Win98Iso D:\iso\win98se.iso -Win2KIso D:\isos\win2ksp4.ISO -CreateDisk
```

Or run only one component:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup-msvc6.ps1
powershell -ExecutionPolicy Bypass -File scripts\setup-w2kddk.ps1
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu.ps1 -Win98Iso D:\iso\win98se.iso -CreateDisk
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win2k.ps1 -Win2KIso D:\isos\win2ksp4.ISO -CreateDisk
```

Setup scripts:

| Script | Purpose |
|---|---|
| `scripts\setup-msvc6.ps1` | Extracts `tools\MSVC600.zip` in place to `tools\MSVC600`, where it is used from; no install. `-RunInstaller` launches the legacy setup program, which is never needed |
| `scripts\setup-w2kddk.ps1` | Validates `tools\WIN2KDDK.EXE` and writes the DDK build wrappers into `scripts\local\`; `-RunInstaller` launches the GUI setup, which is never needed |
| `scripts\install-w2kddk-cabs.ps1` | Unpacks the DDK build environment into `tools\ntddk` directly from the `WIN2KDDK.EXE` payload CABs: no GUI installer, no registry, nothing under `C:\`. `-DdkPath` puts it elsewhere (see notes below) |
| `scripts\setup-qemu.ps1` | Checks/configures the Win98 SE (Phase 2a) QEMU launchers; use `-Install` to try Winget QEMU install; use `-CreateDisk` for VM images |
| `scripts\test-qemu-launchers.ps1` | Generates all three VMs' launchers against stand-in QEMU files and verifies per-boot debug-console log rotation plus the SMP default/fallback flags; run by `build-driver.cmd` |
| `scripts\setup-qemu-win2k.ps1` | Same for the Win2000 SP4 (Phase 2b) VM, the second first-class target. Monitor port 55556, and it also stages `usbd.sys` (`-Win2KUsbdSys`) |
| `scripts\setup-qemu-winxp.ps1` | The Windows XP SP3 guest of roadmap Phase 19 (`vm\winxp.img`, monitor 55559, transfer drive `vm\xferxp`): WHPX with `kernel-irqchip=off`, ACPI on, no companion EHCI unless the run launcher is given `ehci` as its second argument; see "Windows XP target VM" |
| `scripts\setup-qemu-win2k-smp.ps1` | The Phase 2d SMP stress VM (`vm\win2k-smp.img`, monitor 55557). Defaults to the checkpoint-proven `whpx,kernel-irqchip=off` rung; `-Accel`/`-AcpiOff`/`-Smp`/`-MemoryMb` select another Phase 2d task-2 rung so each is a regenerated launcher, not a hand-edited copy |
| `scripts\check-smp-parallelism.ps1` | Host-side Phase 2d checkpoint check against the running 2d VM: a complete one-to-one vCPU/`thread_id` mapping from `info cpus`, plus a process affinity mask allowing 2+ logical processors. Guest-side "MP kernel landed" checks do not distinguish those host conditions; this script does. Run-time, so not part of `build-driver.cmd`; `-SelfTest` needs no VM |
| `scripts\setup-all.ps1` | Runs MSVC, DDK, and both Phase 2a/2b QEMU setups; use `-RunInstallers` for MSVC/DDK and `-InstallQemu` for QEMU. Pass `-Win2KIso` or the Win2000 half is skipped with a warning |
| `scripts\package\extract-usbd-sources.ps1` | Stages reference copies of each target's own `usbd.sys` (and Win98 SE's `usbhub.sys`, an import-gate precedent binary) from that OS's install media into the git-ignored `tools\`, then authenticates them. Packaged by nothing since 1.0.0.1; see "The files the OS supplies" |
| `scripts\package\make-package.ps1` | Assembles the install media both targets are installed from (`out\pkg-<flavor>\`), staging against the layout `check-inf.ps1 -EmitMediaLayout` derives, and gates it |
| `scripts\package\test-package.ps1` | The packager's regression tests; stand-ins only, run by `build-driver.cmd` |
| `scripts\import-gate\test-flavour-rules.ps1` | Regression tests for the import allowlist's three-flavour `FLAVORS` grammar, on synthetic allowlists, including the row that keeps `HAL.dll!WRITE_PORT_UCHAR` out of every published binary. Run by `build-driver.cmd` |

The Phase 2d SMP Win2000 VM has its own script and is not part of
`setup-all.ps1`: it is a stress rig rather than part of a fresh host's baseline
setup, and its accelerator has to be probed per host before it can be trusted.

The legacy MSVC and DDK GUI installers exist and can still require interactive
UI and elevation on Windows 11 x64. Neither is required, and the project does
not use either. The verified route is GUI-free and needs no machine-wide
install:

1. `scripts\setup-msvc6.ps1` extracts `tools\MSVC600.zip` in place. The
   archive's own root directory is `MSVC600\`, so the toolchain lands at
   `tools\MSVC600` and the compiler is used straight out of
   `tools\MSVC600\VC98\BIN` (cl.exe 12.00.8804 works run-in-place once
   `Common\MSDev98\Bin`, which holds `MSPDB60.DLL`, is also on `PATH`; no
   registry install needed).
2. `scripts\install-w2kddk-cabs.ps1` places the DDK's build tools, headers,
   libraries, and the toaster sample into `tools\ntddk` by parsing the component
   INFs inside `WIN2KDDK.EXE` (a Wextract cabinet readable by Windows bsdtar),
   and writes `tools\ntddk\bin\ddkvars.bat` pointing `setenv.bat` at the
   extracted MSVC (`setenv.bat` prefers `ddkvars.bat` over a registry MSVC).
   Those two paths are written relative to `%~dp0`, so the hookup follows the
   repository. An absolute path breaks on a rename or a second host, and the
   symptom is `'nmake.exe' is not recognized` with the DDK tree, the MSVC tree
   and `nmake.exe` all present.
3. `scripts\setup-w2kddk.ps1` (re)writes the wrappers, which likewise derive
   both the repository root and `DDKROOT` from their own location;
   `scripts\local\verify-ddk-toaster.cmd` builds the toaster sample.

`scripts\install-w2kddk-cabs.ps1 -DdkPath <path>` still installs the DDK
anywhere, and `build-driver.cmd` honours `DDKROOT`; when the target is on
another volume the generated `ddkvars.bat` falls back to absolute paths, because
a relative one does not exist. Both are for a host that wants a shared DDK. The
in-repo default is what the checkpoints are run against.

Observed quirks of this route (all benign or handled by the scripts):

- `setenv.bat` prints `Installation of MSVC not detected!!!` from its
  `vccheck` registry probe; harmless, since `ddkvars.bat` supplies the paths.
- The DDK's own `mofcomp.exe` fails on modern Windows
  (`CoCreateInstance ... 0x8007007f`); `verify-ddk-toaster.cmd` generates
  `toaster.bmf` with the OS `mofcomp` (`C:\Windows\System32\wbem\mofcomp.exe
  -B:` accepts the same syntax) before running `build`.
- This DDK ships toaster at `tools\ntddk\src\general\toaster` (per
  `NGEN_DDK.inf`), not `tools\ntddk\src\wdm\toaster`.
- `build.exe` logs two `invalid include statement: importlib(STDOLE_TLB)`
  warnings while scanning the MSVC include tree (`exdisp.odl`/`vidsvr.odl`);
  they do not affect driver builds.

Useful generated wrappers:

| File | Purpose |
|---|---|
| `scripts\local\ddk-debug.cmd` | Open a debug Win2K DDK build prompt rooted at this repo |
| `scripts\local\ddk-release.cmd` | Open a release Win2K DDK build prompt rooted at this repo |
| `scripts\local\verify-ddk-toaster.cmd` | Build the DDK toaster function sample |
| `scripts\local\qemu-win98-install.cmd` | Start Win98 setup from the configured ISO |
| `scripts\local\qemu-win98-run.cmd` | Boot the installed Win98 VM with xHCI present |
| `scripts\local\qemu-win98-usb-test.cmd` | Boot with QEMU USB keyboard/mouse attached to xHCI |
| `scripts\local\qemu-win98-net-storage-test.cmd` | Boot with USB Ethernet + USB mass storage attached to xHCI (Phase 8 bulk path) |
| `scripts\local\qemu-win2k-install.cmd` | Start Win2000 SP4 setup from the configured ISO |
| `scripts\local\qemu-win2k-prepare-usbd.cmd` | Controller-free boot used to stage `usbd.sys` before EHCI is attached (see "Windows 2000 SP4 Target VM") |
| `scripts\local\qemu-win2k-run.cmd` | Boot the installed Win2000 VM with xHCI present |

The generated wrappers and VM images are local machine artifacts and are ignored by git.

### Win2K DDK Installation Notes
Run `scripts\install-w2kddk-cabs.ps1` (no GUI, nothing under `C:\`; the
verified route above). Running `WIN2KDDK.EXE` itself works too, but then the
install path is whatever its dialog was told and `DDKROOT` has to name it. What
lands under `tools\ntddk` either way:
- Build tools (`build.exe`, `nmake.exe`, plus `link.exe`, `rc.exe`, `ml.exe`,
  and `mspdb50.dll` for its own linker; only `cl.exe` comes from MSVC)
- Kernel headers (`inc\`, where `wdm.h` sits rather than `inc\ddk\`, and
  `inc\ddk\`)
- Kernel import libraries (`libchk\i386\` and `libfre\i386\`)
- Sample drivers (`tools\ntddk\src\`, which is not this repository's
  `src\`; toaster is at `tools\ntddk\src\general\toaster`)
- Build environment batch files (`bin\setenv.bat`)

Source comments and docs across this repository cite DDK headers as
`C:\NTDDK\inc\...`, which was the install path until the toolchain moved into
the repository. Those are provenance citations naming a file within the DDK,
not paths anything reads at build time; read them as `<DDK>\inc\...`.

The DDK build environment uses its own compiler (from MSVC), so you do not need to launch the MSVC IDE separately for building drivers. MSVC 6.0 provides `cl.exe`, which the DDK build system uses.

## Setting Up the Build Environment

`scripts\build-driver.cmd` does this for you and is the only supported way to
produce a deployable binary (it runs the gates). To do it by hand, open a
Command Prompt at the repository root and run:
```
tools\ntddk\bin\setenv.bat %CD%\tools\ntddk free w2k x86
```

Parameters:
- `%CD%\tools\ntddk` - DDK installation path. It must be absolute and
  unquoted: `setenv.bat` assigns `BASEDIR` verbatim, so a relative path breaks
  the moment the build `cd`s elsewhere, and a quoted one puts literal quotes
  into every derived path
- `free` - the DDK's own word for what this project calls the release build. Its `checked` is this project's debug build. These two words appear here because `setenv.bat` takes them literally; everywhere else the project says release and debug.
- `w2k` - target OS (Windows 2000). This controls which headers/libs are used.
- `x86` - architecture.

For a debug build (recommended during development):
```
tools\ntddk\bin\setenv.bat %CD%\tools\ntddk checked w2k x86
```

That is the DDK's checked environment, which `build-driver.cmd` maps both `debug` and `qemu` onto. What it buys this driver is narrower than the DDK's general description: both flavours compile `/Oxs` and differ by `/Oy-` against `/Oy`, so a checked build keeps frame pointers rather than dropping optimisation. Every `XHCI_C_ASSERT` in this driver is compile-time and fires in every flavour, and the verbose per-line output belongs to `qemu` alone, not `debug`.

## Build Files

The DDK uses a `sources` file (no extension) instead of a Makefile. Alongside it is a one-line `makefile` that includes the DDK's master makefile.

`src/makefile`:
```
!INCLUDE $(NTMAKEENV)\makefile.def
```

`src/sources` has this shape (`SOURCES` grows as files are added):
```
TARGETNAME=xhci98
TARGETTYPE=DRIVER
TARGETPATH=obj

INCLUDES=$(BASEDIR)\inc;$(BASEDIR)\inc\ddk

SOURCES=xhci_mem.c       \
        xhci_dispatch.c  \
        xhci_pci.c       \
        xhci_init.c      \
        xhci_ring.c      \
        xhci_port.c      \
        xhci_slot.c      \
        xhci_xfer.c      \
        xhci_dbg.c

TARGETLIBS=.\usbport.lib

LINKER_FLAGS=-merge:.rdata=.text
```

`build` appends the flavour to `TARGETPATH`, so the debug output lands in `src\objchk\i386\` and the release output in `src\objfre\i386\`, not `src\obj\`.

`SOURCES` also carries one non-C entry, `xhci98.rc`, the file version resource. The DDK's build engine runs `rc.exe` on a `.rc` in `SOURCES` like any other input. It adds no import (a resource is data in its own `.rsrc` section rather than code), so the post-link allowlist is unaffected, and it does not interact with `LINKER_FLAGS=-merge:.rdata=.text`, which merges `.rdata` and says nothing about `.rsrc`. Both were checked with the import gate; see "Versioning the driver" below.

### Versioning the driver

The version and the release date are edited in one place, `src\xhci_version.h`. It declares three macros: `XHCI_VER_CSV` (the four-part number as four integers, which is what a `VERSIONINFO` resource takes), `XHCI_VER_STR` (the same number as a string, which is what everything else takes) and `XHCI_DRIVERVER_DATE` (the release date, in the `MM/DD/YYYY` form `DriverVer` takes). The file is `#define` lines and comments only, because three toolchains include it: the DDK's `rc.exe`, MSVC 6.0's `cl.exe`, and Open Watcom's `wcc386`.

Four sites include it and cannot disagree: `FILEVERSION`, `PRODUCTVERSION`, `VALUE "FileVersion"` and `VALUE "ProductVersion"` in `src\xhci98.rc`. Two more include it across a directory boundary, by a path relative to the including file (which is what a quoted `#include` means to all three compilers): `TOOL_VERSION` in `xhciqual\qual.h` and `XHCISNAP_VERSION` in `xhcisnap\xhcisnap.c`. Both tools are published inside a release directory (`releases\<version>\xhciqual\`, `releases\<version>\xhcisnap\`) and print their number into every log a user saves and sends back, so a tool answering with a number that is not on the box ties a bug report to an artifact that does not exist.

The seventh site cannot include anything. An INF is a data file Windows setup reads, not a compiled one, so `src\xhci98.inf`'s `DriverVer` keeps a literal and is checked against the header instead. Three gates cover what is left:

- `scripts\inf-gate\check-inf.ps1`, which every build runs, reads `src\xhci_version.h` as the authority and refuses four different disagreements: the header's two forms of the number differing from each other (nothing in any toolchain would notice, since `rc.exe` wants the integers and every other consumer wants the string), the header's version differing from `DriverVer`'s, the header's date differing from `DriverVer`'s, and a version literal appearing in `src\xhci98.rc` at all, or the include being dropped. That last rule is what keeps the header an authority rather than a suggestion.
- `scripts\package\make-package.ps1` compares the built binary's own `FileVersion` with the `DriverVer` in the INF staged beside it. That is the copy a source-side gate cannot see and the only one an installed machine reads: bumping the source without rebuilding produces a package that installs cleanly, is accepted by Windows 2000 as an upgrade, and reports the older build for ever afterwards.
- `scripts\package\make-release.ps1` refuses a cut whose declared version is not the release's, whose tool sources have stopped expanding the shared macro, or whose staged `XHCIQUAL.EXE`/`XHCISNAP.EXE` is older than `src\xhci_version.h` as well as older than its own sources. That last input is the one non-obvious consequence of the single source: a bump no longer touches anything in `xhciqual\` or `xhcisnap\`, so without the header in that set, bumping the version and shipping yesterday's binary would pass every check in the script.

The header spells the number out twice because `FILEVERSION` takes four comma-separated integers and cannot be given a string, and deriving one form from the other needs preprocessor stringizing that this era's `rc.exe` does not handle reliably. So both are written and the INF gate compares them. Two adjacent lines a gate reads beat one clever line no toolchain agrees about.

One trap is worth knowing before anyone improves on this. An earlier attempt at a macro factored only the string form, so the two strings were tied to each other and left free to drift from the numbers. Worse, it made the INF gate's cross-check vacuous, because that gate searched the `.rc` for the version text and the `#define` line satisfied the search whatever the `VALUE` entries said. The single-source header is only safe because the gate changed with it: it reads the header, not the file it is checking, and it refuses a literal in the `.rc` outright. A gate that can be satisfied by the definition it is checking against is not a gate.

The build stamps are not in the header. `XHCIQUAL` and `XHCISNAP` each print a `built <date> <time>` line from their compiler's `__DATE__`/`__TIME__`. That is a different fact from the version and must stay one: between cuts the version does not move and the tools are rebuilt many times a day, so the stamp is what tells a guest's stale copy from the one just staged. The driver has no such banner in a release build (`DriverEntry (built ...)` is inside `XHCI_DBG_TEXT` and compiles out), so a release binary is identified by its version resource and its SHA-256; a byte compare cannot stand in for either (task 13-L.4 in the roadmap records why).

A cut therefore has two toolchains as prerequisites beyond the DDK: Open Watcom for `XHCIQUAL.EXE` and the in-repo MSVC 6.0 for `XHCISNAP.EXE`. Neither is skippable on a real cut. `-SkipQualtool` and `-SkipSnapTool` exist for a host that cannot build one, and a release cut with the second publishes a read channel nobody can open.

The scheme is one four-part version per released package, bumped in the last field, with the date set to the release date and never moving backwards within a series. It is a package version rather than a build counter: the deploy loop overwrites `xhci98.sys` in place and never re-runs the INF (see "Deploying a build into the Win98 VM"), so a per-build number would churn with no observer.

The major version says whether this is a final release. It is `1`: task 14.2 cut `1.0.0.0`, the first one, and every package before it was a `0.x` pre-release that carried no claim of being finished. Those directories are gone and `releases\history.md` holds one entry. The current number is in `src\xhci_version.h` and is not repeated here.

The numbering has been restarted once, at the project owner's direction: an earlier `1.0.0.x` series of development builds was removed from `releases\` and the version restarted at `0.0.0.1`, which is where the `0.x` pre-releases came from. Two consequences outlive it:

- A machine can be carrying a package this repository no longer publishes, and a `1.0.0.x` development build ranks at or above today's `1.0.0.0` to the Windows 2000 setup engine, which will then decline the release as not-better. The remedy is the one task 11-V.3 established: uninstall, and delete the cached `%SystemRoot%\inf\oemN.inf` and `.pnf`. Do not choose a version to beat it. Nothing here was ever uploaded, so the only machines that can be in this state are this project's own.
- `scripts\package\make-11v-media.ps1` takes its baseline from a git commit rather than from `releases\`. `-BaselineVersion` defaults to the version cut before the current one, `-BaselineCommit` is a commit the caller names (the last one whose `src\xhci98.inf` reads that version), and the script refuses when the baseline is not older than the INF's. Both move together at each bump, and a rewritten history moves the commit again. The check runs ahead of the first build, so the refusal is immediate.

  Two of its other checks a caller should know exist: the staged baseline binary is not trusted by directory name (its own version resource is checked against both the expected baseline and the current version through `Test-DriverVersionMatches`, so a stale binary under `old-<baseline>-debug` cannot satisfy the prerequisite silently), and `New-DatedInf`, which rewrites the INF's date for the upgrade experiment, refuses a non-ASCII byte, an LF-only source or a mixed-EOL source rather than claiming a byte-faithful rewrite it would have silently transformed.

The prose copies are not gated, and a cut has to bump them by hand. `docs/using/release-notes.md`'s opening line states the version the file describes. Nothing reads it, so nothing catches it, and it has sat stale across cuts before. Bump it in the same change as the header, and prefer pointing at `src\xhci_version.h` to restating a number anywhere a statement does not need one; `releases\README.md` stopped naming the current version for this reason.

A version is only free until it is published. The single source and its three gates keep every statement of the number agreeing with the others; what they cannot check is whether the number they agree on has already been spent on bytes someone else is running. That check is against `releases\`, by eye, whenever the tree diverges from the last release. It was missed once, when development carried on under a number already published and two different binaries both answered to it.

On Windows 2000 that number is not cosmetic. The setup engine ranks candidate drivers by date and then version, so a package whose `DriverVer` does not exceed the installed one is declined as not better and the upgrade-over-an-older-build path (task 11-V.3) cannot pass. Microsoft's own reference INF says so: SP4's `[EHCI.NT]` carries `DriverVer=2/15/2003,5.1.2600.1` with the comment `; Date and Ver should be > any preSP4 QFE`. NUSB's 9x-half `USB2.INF` carries a `DriverVer` too (`09/26/2003,4.90.3000.10`), so keeping the line follows both reference INFs rather than a guess about the 16-bit engine.

Changing the display strings is not observable on an existing devnode. Both setup engines cache `DriverDesc` in the device's software key at install time, so an already-installed machine keeps the old string across a reboot, and the in-place `.sys` overwrite does not exercise the INF at all. The observation is a fresh install, or an uninstall-and-reinstall, on each target. On Win98 that install is also a bugcheck; see "Do not disable the controller in Device Manager" below for how to sequence it so the price is paid once.

Where each target surfaces these fields was measured on the 2a VM (batch 8-V), and the answer differs from the Win2000 mapping in a way that matters:

| Written in | Win98 SE shows it as |
|---|---|
| `Provider=` | General tab Manufacturer, and Driver tab Provider |
| `DriverVer` date half | Driver tab Date (`8-8-2026`) |
| `DriverVer` version half | nowhere |
| `.rc` `VALUE "FileVersion"` | Driver File Details -> File version |
| `.rc` `VALUE "LegalCopyright"` | Driver File Details -> Copyright |

The identity strings are split in two. `[Version]`'s `Provider=` and the `[Manufacturer]` key carry different strings:

| INF | Value | Surfaces as |
|---|---|---|
| `[Version] Provider=%Provider%` | `Yeo Kheng Meng` | Driver tab Driver Provider (both targets); stored as `ProviderName` |
| `[Manufacturer] %Mfg%=` | `xHCI98 Project` | General tab Manufacturer; stored as `Mfg` |

They are answers to different questions. `Provider` is who supplied the driver, which is a person, and is what `src\xhci98.rc`'s `LegalCopyright` already said, so `CompanyName` moved with it. The manufacturer is who made the device as this driver reports it, and this driver binds by class code, so the silicon is Intel's or AMD's or Renesas's: no string here is literally true of the hardware, and the project name is the least misleading of the available answers.

The table above records `Provider=` as feeding the Manufacturer field as well as the Provider one. That row was measured when a single string filled both slots, so the two could not be distinguished; a fresh install now separates them, and the row is worth re-taking rather than trusted.

Observations recorded in this repository before the split show `xhci98 Project`, and those readings are left as they were taken: a measurement is a record of what was on screen, not a statement about what the tree currently says. That includes `docs/contributing/runs/run-11v.md`'s stage B tables and the batch 8-V boxes in `docs/contributing/roadmap.md`. An already-installed machine keeps the cached strings until the devnode is reinstalled, so the first target to show either new one is a fresh install rather than an upgrade.

One tooling consequence to check for whenever a string in `src\xhci98.inf` changes: `scripts\inf-gate\test-inf-checks.ps1` builds its high-byte mutation by matching the quoted-value form by pattern. An earlier version spelled out `xhci98 Project`, so a rename turned that `Replace` into a no-op and left the test asserting that a rule fires on a file it never mutated. It failed rather than passed (`Assert-RuleFires` demands a nonzero exit and an unmodified INF gives zero), but the failure read as "the FILE-ENCODING rule stopped working" and pointed at the gate's encoding check, when what had happened was that one line in the test stopped editing the file. A self-test that names a value it does not own has an expiry date.

Two consequences. The version resource is not decorative on Win98: the 16-bit engine does render an NT-style `VERSIONINFO` out of a `.sys`, and it is the only place on this target the four-part version appears at all. So when asking a Win98 user which build they are running, say "Driver File Details", not "the Driver tab": the Driver tab shows only a date, and two packages released on the same day are indistinguishable there. Two development builds carrying the same `08/08/2026` date and different version fields both ran on this VM within the hour.

On Windows 2000 the halves are the other way round, and one of them is missing. This remains an open question. Measured on the 2b VM the same day: Driver Provider `xhci98 Project`, Driver Version showing the four-part version the package carried, Driver Date `Not available`, Digital Signer `Not digitally signed`. So each target surfaces one half of `DriverVer` and a different half, and Win2000 drops the date entirely.

The control rules out the obvious answer: Microsoft's own `Intel(r) 82801DB/DBM USB Enhanced Host Controller` on the same machine shows `Driver Date: 15-Feb-03`, from SP4's `USB.INF` `DriverVer=2/15/2003,5.1.2600.1`. This build populates the field, so "Not available" is about our package rather than about Windows 2000. Two further facts narrow it: our `DriverVer` line clearly parsed (the version half is displayed), and the date value itself is not absurd (Win98's engine accepted the same `08/08/2026` and rendered `8-8-2026`).

Of the three candidates, batch 11-V's stage B eliminated one:

- The date value: ruled out. Stage B5 installed `PKG2003`, byte-identical to the package beside it except that `DriverVer` reads `02/15/2003`, the date Microsoft's entry renders correctly, and it behaved the same. Whatever Windows 2000 objects to, it is not the year 2026.
- Signed vs unsigned: still open. Microsoft's entry is signed by the Windows 2000 Publisher and ours is not. If Win2000 takes Driver Date from the package's catalog rather than from `DriverVer`, every observation above fits, and it would be unfixable here, since code signing is out of scope.
- Zero-padding: still open. Ours is `02/15/2003` against Microsoft's `2/15/2003`. Separating this from signing needs an unpadded package, which this repository's own INF-gate rule `^\d{2}/\d{2}/\d{4}` refuses to produce, so the remaining step needs a narrow, deliberate exception rather than another run.

This is not cosmetic. `DriverVer`'s date is the first key Windows 2000 ranks candidate drivers by; if it is never recorded, that ranking cannot be using it. Stage B showed the practical consequence directly (the package could not upgrade itself on Windows 2000, declined four ways) and found the remedy: delete the cached `%SystemRoot%\inf\oemN.inf` and its `.pnf`. Task 11-V.3 covered that half and is closed. What is left is separating signing from padding, which is task 12.4; it needs no machine beyond a 2b guest.

`Driver File Details` also lists `xhci98.tmp` beside `xhci98.sys`, `NTKERN.vxd` and `USBD.SYS`. That is the `[Xhci.CopyFiles]` temporary name; Win98 copies through it correctly and then leaves it behind and registers it. Cosmetic, and not a reason to drop the third field: without it a replace over the loaded binary fails outright. Confirm which binary is live from the trace's `DriverEntry (built ...)` line rather than from that list.

Code signing is out of scope and is not a missing field. `xhci98.sys` has no Authenticode signature, so the Digital Signer property in file properties is empty on both targets. Neither Windows 98 SE nor Windows 2000 SP4 enforces driver signing for an unsigned INF install (both warn at most), and a WHQL or self-signed Authenticode path is a project of its own. `docs/using/release-notes.md` says the same under "What this is not".

`usbport.lib` is an import library for the private `usbport.sys` exports; the Win2K DDK ships no import library for them. It is a build artifact, not a checked-in file (`*.lib` is git-ignored): once MSVC 6.0 is set up, run `scripts\make-usbport-lib.cmd` before the first `build` in a fresh clone. No extracted `usbport.sys` is required: the tracked exact-name manifest was verified against all three recorded shipping binaries.

Do not use plain `lib /def:` for this (verified with the MSVC 6.0 toolchain): the usbport exports are `__stdcall` functions exported by undecorated name, and `lib /def:` with plain names emits cdecl-style `_USBPORT_GetHciMn` symbols. A miniport that prototypes them correctly as `NTAPI` references `_USBPORT_GetHciMn@0` / `_USBPORT_RegisterUSBPortDriver@12` and fails to link; re-declaring them cdecl to force the link would corrupt the stack at the 3-argument registration call (caller and callee would both pop the arguments). Use the stub-DLL method instead:

1. `dumpbin /exports usbport.sys` (record alongside the ABI record; `DllUnload` is also exported but is not imported).
2. Write stub bodies with the real stdcall signatures (`usbport-stub.c`: `USBPORT_GetHciMn(void)`, `USBPORT_RegisterUSBPortDriver(ptr, ULONG, ptr)`, bodies never executed) and a plain-name `.def` (`LIBRARY USBPORT.SYS` + the two export names).
3. `cl /nologo /c usbport-stub.c`, then `link /nologo /DLL /NOENTRY /NODEFAULTLIB /machine:ix86 /def:usbport-stub.def usbport-stub.obj /out:usbport-stub.sys /implib:usbport.lib`. `LNK4070` ("`/OUT:USBPORT.SYS` directive in .EXP differs from output filename") is expected and wanted: the `.def`'s `LIBRARY` name is what gets baked into the import library.
4. Check the result: `dumpbin /linkermember:1 usbport.lib` must show `_USBPORT_GetHciMn@0` / `_USBPORT_RegisterUSBPortDriver@12`, and the archive members must carry `DLL name : USBPORT.SYS`.
5. Link-test it: compile an NTAPI-prototyped caller and link it against the lib, then confirm the image's import descriptor names `USBPORT.SYS` and both symbols. Steps 1-4 can all pass on a lib that a real miniport still cannot link against; this is the step that catches it, and it costs one throwaway object file.

Where `dumpbin` is named here and in the static disassembly passes, `link -dump` from `tools\ntddk\bin` is the same COFF dumper (the DDK ships it), so no Visual Studio install is needed to repeat them; the root-hub disassembly pass in Phase 5 was done with `link -dump -disasm`.

`scripts\make-usbport-lib.cmd` does all five and writes `src\usbport.lib`. Its inputs (`scripts\usbport-lib\usbport-stub.{c,def}`, `linktest.c`, `usbport-imports.expected`, and the exact-name verifier) are tracked; the lib is a git-ignored artifact regenerated per clone. The script always requires the DEF to match the tracked manifest exactly.

For the export names it also takes the strongest evidence available: argument 1 if you pass a `usbport.sys` path, otherwise the first of the known `tools\{nusb,win2ksp4,winxpsp3}-extracted\` binaries that this working copy happens to have, otherwise the manifest alone. Matching is exact and case-sensitive (`USBPORT_GetHciMnEx` does not satisfy `USBPORT_GetHciMn`, which a substring `findstr` would have accepted). The last line of output names which of the three ran, so the weakest form is never silent; the reference is only ever read, so it cannot change the generated lib.

`usbport-imports.expected` is the single source for the `USBPORT.SYS` half of the driver's import surface. The post-link import gate below reads it rather than restating the names.

The library is staged under a unique temporary name in `src` and moved to `src\usbport.lib` only after the decoration, module, and NTAPI link checks pass. A failed run therefore cannot replace the last verified library with a partial or invalid artifact.

## Building

Use `scripts\build-driver.cmd`. It is non-interactive and runs the DDK
build plus every gate a binary must pass before it goes near a VM:

```
scripts\build-driver.cmd            REM both SHIPPING flavours (default)
scripts\build-driver.cmd debug
scripts\build-driver.cmd release
scripts\build-driver.cmd qemu       REM the emulator-only third flavour
scripts\build-driver.cmd all        REM all three - what a release cut gates
```

There are three flavours, and only two of them are ever published. `release`
and `debug` are the shipping pair and are what `-Flavor`, `out\pkg-*`,
`releases\<version>\` and "both flavours" mean throughout these documents.
`qemu` is `debug` plus the port-`0xE9` trace mirror and the
`HAL.dll!WRITE_PORT_UCHAR` import that carries it; it builds into
`src\objchk_qemu\i386\` with `buildchk_qemu.*` logs, and it **must never be
published**.

That import was the sole delta between the two binaries of the development
package whose debug build gave the E460 a `Code 2`; why that build
failed is not established (defect 2b). The import allowlist's one
`qemu required` row is what makes a published binary carrying it impossible.
`src\sources` refuses any `BUILD_ALT_DIR` outside the three with an `!ERROR`.
`docs/contributing/design/08-build-flavours-and-the-log-channel.md` section 5 is
the design record.

`both` and `all` are different words. `both` is the two shipping flavours and
stays the wrapper's default, because widening it would build a never-published
binary on every ordinary run; `all` is the three and is what a release cut
uses, so every flavour is gated even though only two are staged. In the import
allowlist's `FLAVORS` column the word is `all` and `both` is refused, which is
a different rule in a different file; see "Post-link import-compatibility
gate" below.

The DDK calls `release` and `debug` free and checked, and those words survive
only where it requires them: `setenv.bat`'s flavour argument, the
`src\objfre` / `src\objchk` trees, and the `buildfre` / `buildchk` logs.
`build-driver.cmd` translates once, in `:buildflavor`, which is also where
`qemu` is mapped onto checked with its own `BUILD_ALT_DIR`, since `debug` and
`qemu` are both checked builds and would otherwise collide in one output tree.
Do not reintroduce the DDK vocabulary anywhere else: "free" reads as free of
charge to a user, so `releases\` never says it.

In order: validate the flavour word and the optional `-NoTargetEvidence`,
generate `src\usbport.lib` if it is missing, run the import gate's
authenticated-baseline regression tests and its flavour-rules tests, run the
INF gate's self-tests and then the gate on `src\xhci98.inf`, run the packager
self-tests and the QEMU launcher self-tests, run `test\run-host-tests.cmd`,
`build` each requested flavour in its own child `cmd` (`setenv.bat` is not
idempotent across flavours), fail on a build error or on the presence of
`src\build{chk,fre,chk_qemu}.err`, then run the post-link import gate and the
flavour-marker check on each linked binary. Exit codes: 0 built and gated, 1
failure, 2 host tests inconclusive (a blocked exe launch, see "Smart App
Control" below, so just run it again).

The host suite runs before the DDK builds: it compiles the same pure-core
files in seconds, so a bad carve, ring or PORTSC constant should not cost two
full builds first. Both it and the import gate run again in
`scripts\package\make-package.ps1`, because that is the step that produces
media a VM is installed from and a binary can reach `src\obj*` without having
been built by this wrapper.

`scripts\local\ddk-debug.cmd` / `ddk-release.cmd` still open an interactive DDK
prompt for one-off experiments, where `cd src && build` works as before. A
binary built that way has not been through the gates; do not deploy one.

Run `scripts\build-driver.cmd`, not only the host suite, before calling a
batch done. The host suite compiles the same files without the DDK headers,
and there is a class of break it cannot see: at one point `xhciDevRingless`'s
parameter had been named `except`, which the DDK headers define as `__except`,
so the DDK build had not compiled for a whole batch while the host suite stayed
green throughout.

Two `BUILD_ALT_DIR` facts the wrapper's `:buildflavor` depends on, verified
against this DDK's own `build.exe`: the value may be at most 10 characters
(`build.exe` says so itself; `chk_qemu` fits and yields `src\objchk_qemu\i386\`
and `buildchk_qemu.log`), and it must be overridden after `setenv.bat`, so that
`SDK_LIB_PATH` still points at `libchk` and the link resolves.

`build` appends its own flavour word to `TARGETPATH`, so output is
`src\objchk\i386\xhci98.sys` (the debug build), `src\objfre\i386\xhci98.sys`
(the release one) or `src\objchk_qemu\i386\xhci98.sys` (the emulator-only one),
never `src\obj\`. Build errors are logged to `buildchk.log`/`buildchk.err` (or
`buildfre.*`, or `buildchk_qemu.*`) in `src\`.

`objchk_qemu` contains the substring `objchk`, so the import gate infers a
flavour by matching the object directory's full name (`objfre`, `objchk` or
`objchk_qemu`), and anything else is a refusal rather than a guess. A
substring test would gate a qemu binary as `debug` and let through the one
import the third flavour exists to exclude.

Useful flags when driving `build` by hand: `-c` clean first, `-e` show
environment, `-f` force rebuild, `-Z` do not scan for dependencies (`build -cZ`
is what the wrapper uses).

### Post-link import-compatibility gate (Phase 3 onward)

An unresolved import prevents `xhci98.sys` from reaching `DriverEntry`. On
Win98 the symptom can be only a Device Manager yellow bang, indistinguishable
from a bad INF. Treat this as a property of the linked image rather than of the
source.

Implemented as `scripts\import-gate\check-imports.ps1`, which
`scripts\build-driver.cmd` runs after every link. It can also be run by hand on
any binary:

```
powershell -ExecutionPolicy Bypass -File scripts\import-gate\check-imports.ps1
powershell -File scripts\import-gate\check-imports.ps1 -Image out\xhci98.sys -Flavor debug
```

With no `-Image` it checks whichever of `src\objfre\i386\xhci98.sys`,
`src\objchk\i386\xhci98.sys` and `src\objchk_qemu\i386\xhci98.sys` exist and
infers the flavour from the path.

Tracked inputs:

| File | Role |
|---|---|
| `scripts\import-gate\xhci98-imports.allow` | The committed decision: allowed module/symbol pairs per build flavour, plus a `[deny]` section of names that must never appear, each with its diagnosed reason |
| `scripts\usbport-lib\usbport-imports.expected` | The `USBPORT.SYS` rows, read rather than restated; it is already the single source the `.def` and the import library come from |
| `scripts\import-gate\win2k-baselines.expected` | Every SP4 kernel/HAL source image the targets can run, with its provider, version, length, and SHA-256 |
| `scripts\import-gate\win98-evidence.list` | The Win98 evidence sources, `[precedent]` binaries and the `[nametable]` file, each with why it counts and its version, length, and SHA-256 |

Target files the evidence steps resolve against live under `tools\` (git-ignored);
`scripts\import-gate\extract-target-baselines.ps1` recreates the Win2000 kernels
and HALs and `ntkern.vxd` from the install media. The Win98/NUSB precedent
binaries are staged separately.

The allowlist records module/symbol pairs rather than bare names because the
module is part of the fact. When MMIO was added in Phase 4, the release profile
grew from two usbport exports to `NTOSKRNL.EXE!READ_REGISTER_ULONG`,
`NTOSKRNL.EXE!WRITE_REGISTER_ULONG` and `HAL.DLL!KeStallExecutionProcessor`,
the same pairs NUSB's own `USBEHCI.SYS` imports on Win98, including that the
two register accessors come from `NTOSKRNL.EXE` and not `HAL.DLL`. A name-only
list would pass a binary that resolved them against the wrong module. The
release profile was a strict subset of the shipping miniport's until the
command engine's spin lock was added; the allowlist records that decision and
the rule that replaced it, that every pair carries Win98 evidence of its own.

The import list is a decision, not a build-flag accident. Phase 3's linked
release build imported `USBPORT.SYS` and nothing else (no NTOSKRNL, no HAL),
and it stayed that way on purpose: the registration packet is zeroed by an
explicit word loop rather than `memset`/`RtlZeroMemory`, and there is no
`ExAllocatePoolWithTag`, so a compiler intrinsic or a runtime helper cannot
add an import nobody chose. The trace channel's imports are in the allowlist
by flavour: `NTOSKRNL.EXE!DbgPrint` and `HAL.DLL!KeGetCurrentIrql` are
required in every flavour, and `HAL.DLL!WRITE_PORT_UCHAR` is required in the
qemu flavour only and refused in the two that ship. Keep it that way when
adding code: a new import is a gate change first.

Identity is checked before any file is used, on both halves. The rules differ
only in how absence is treated: a wholly absent Win2000 set is a warning
because allowlist enforcement can still run, but once any SP4 baseline is
present a missing variant or a version/length/hash mismatch is a failure. Win98
evidence files may be absent (they contribute nothing and the run says so), yet a
present file that is not the recorded build is a failure too, because the gate
prints those files by name as its reason for believing an import resolves.
`-NtkernPath` overrides where that file is read from, never what it must be.

Every identity failure lists the remedies, and only the first of them needs the
install media: re-stage with `extract-target-baselines.ps1 -Force`, or delete
the complete list of manifest-owned kernel/HAL outputs printed by the error
(which returns that half of the gate to its "not present" warning), or pass
`-NoTargetEvidence` for allowlist-only enforcement. Do not clear
`tools\win2ksp4-extracted\`: it also holds USBPORT/USBEHCI binaries and
disassembly evidence unrelated to this baseline. A host without the ISOs is
therefore never stuck, and `-NoTargetEvidence` does not read the manifests at
all.

`scripts\import-gate\test-evidence-manifests.ps1` regresses both manifests with
synthetic temporary files (complete, missing (mandatory vs optional), wrong
length, and same-length tampering), and `build-driver.cmd` runs it before it
builds anything.

Pairs, not names. The PE import descriptor names the provider, so
`ntoskrnl.exe!DbgPrint` and `HAL.dll!DbgPrint` are different imports and only
one of them resolves; the gate reports a provider change as its own failure.
Module names are compared case-insensitively because the recorded spelling
varies: this build's linker writes `ntoskrnl.exe` and `HAL.dll` where the
shipping NUSB miniport carries `NTOSKRNL.EXE` and `HAL.DLL`. Symbol names are
compared exactly. Do not assume only `NTOSKRNL.EXE` and `USBPORT.SYS` are
valid: the measured NUSB `usbehci.sys` imports `KeStallExecutionProcessor` from
`HAL.DLL`.

What the gate does, in order:

1. Enforcement, always. Every imported pair must be in the allowlist for
   the flavour being checked; pairs marked `required` must be present; denied
   symbols are reported with their specific cause. An import by ordinal is
   refused outright, since a name is what the three usbport lineages agree on.
   This half needs nothing but the tracked files, so it runs on any host.
2. The Win2000 half, host-side and authoritative. Every `ntoskrnl.exe`
   import must appear in both SP4 kernel images (`ntoskrnl.exe` and the
   `ntkrnlmp.exe` used by the Phase 2d SMP kernel); every `hal.dll` import must
   appear in all eight HAL images on the media. That includes the
   Standard-PC and MPS variants used by the VMs, the ACPI variants real hardware
   can select, and `halborg.dll`, the one kernel HAL the media ships
   uncompressed, which a `HAL*.DL_` enumeration silently misses (it targets the
   SGI Visual Workstation; it is covered so the completeness claim rests on the
   media rather than on an argued exception).

   Before reading an export table,
   the gate authenticates the exact version, length, and SHA-256 from
   `win2k-baselines.expected`. Win2000 has real kernel/HAL files on disk and
   these are what its loader resolves against, so this is the check Oney's
   `DEPENDS` recipe (p.439) would perform, done before deploy instead of after.
   It is also the only thing that catches an XP-era API copied in from a
   sample.
3. The Win98 half, evidence rather than proof. Nothing host-side can be
   authoritative: Win98's NT-style export tables are built at init by
   `NTKERN.VXD` rather than read from a file, so there is no export table to
   resolve against and `dumpbin /exports` of `NTKERN.VXD` is not a substitute.
   Two positive-only sources are reported per import:
   - precedent: a driver in `win98-evidence.list` imports the same pair.
     Win98 SE's own `usbd.sys`/`usbhub.sys` (4.10.2222) and the NUSB set
     installed on the 2a VM qualify; NUSB's `USBPORT.SYS` and `USBEHCI.SYS`
     are the strongest, being the exact modules this miniport plugs into.
   - `ntkern.vxd` name table: the symbol appears as a NUL-delimited
     string in the file that builds those tables. Absence means nothing:
     `KeGetCurrentIrql` is not in it, yet NUSB's `usbport.sys` imports that
     pair from `HAL.DLL` on Win98. Never treat a missing name as a failure.

   The authoritative Win98 check remains the load itself on the 2a VM.
   Production services are shared by both flavours, so both failing never
   rules those shared imports out.

A wholly absent evidence source is a warning, never a silent pass; a
partial or unauthenticated Win2000 baseline, or a present-but-unrecognized Win98
evidence file, is a failure. The run prints which sources ran. The gate
has been exercised against broken inputs made for the purpose (a driver importing
`ExAllocatePoolWithTag` and an unlisted `KeQuerySystemTime`, a debug binary
declared as free, an allowlist naming the wrong provider, and a baseline that
does not export the symbols) to confirm each failure path fires.

Two toolchain notes for anyone extending this: MSVC 6.0's `dumpbin` needs
`Common\MSDev98\Bin` on `PATH` for `MSPDB60.DLL` or `/exports` exits 53 with no
output at all, and it does not know `/nologo` (harmless `LNK4044`).

This gate also catches the local Win2K DDK's confirmed
`ExAllocatePool` -> `ExAllocatePoolWithTag` macro expansion; see
`docs/usb-xhci-info/win98-wdm.md`.

## Test Environment Setup

### Option A: VirtualBox

VirtualBox supports XHCI via its "USB 3.0 Controller" option (requires VirtualBox Extension Pack). The Phase 1 setup scripts only generate QEMU launchers (Option B); VirtualBox is a manual alternative.

1. Create a VM:
   - OS type: Other Windows (32-bit) or Windows 98
   - RAM: 128-256 MB
   - Storage: IDE (Win98 does not support AHCI)

2. Enable USB 3.0 controller:
   - Settings -> USB -> USB 3.0 (xHCI) Controller
   - This presents a VirtualBox XHCI controller (PCI device) to the guest

3. Install Windows 98 SE from ISO.

4. To transfer driver files to the guest: attach a USB storage device to the host and pass it through, or mount a shared folder if Win98 guest additions are available (limited support), or use a floppy/ISO image.

5. A USB keyboard/mouse through the XHCI controller requires our driver to work first. Workaround: enable PS/2 mouse/keyboard emulation in VirtualBox settings, or use a PS/2 KVM. Many VMs have PS/2 emulation enabled by default.

### Option B: QEMU

QEMU supports XHCI via the `qemu-xhci` or `nec-usb-xhci` device type. Use `qemu-xhci` by default. QEMU's USB documentation recommends `qemu-xhci` for guests with xHCI support, and it avoids presenting a NEC/Renesas PCI ID that could accidentally trigger real-hardware NEC quirks in this driver.

QEMU USB documentation: `https://www.qemu.org/docs/master/system/devices/usb.html`

Example QEMU invocation for Win98 SE installation and Phase 2a testing. The
project uses `qemu-system-x86_64` throughout: a 32-bit guest runs identically
under it, and it is the scoop package's only WHPX-capable binary.
```
qemu-system-x86_64 \
  -machine pc \
  -cpu pentium3 \
  -m 256 \
  -drive file=win98.img,format=qcow2,if=ide \
  -device qemu-xhci,id=xhci \
  -cdrom win98se.iso \
  -boot d \
  -action reboot=reset -no-shutdown
```

**Install Win98 with `setup /p j`. A plain ISO install does not enumerate PCI
under QEMU** (observed on QEMU 7.0 and 11.0; see `docs/contributing/lessons.md`).
Win98's legacy PnP-BIOS enumerator does not initialize against QEMU/SeaBIOS: it
lands with "Plug and Play BIOS" Code 24 and no "PCI bus" node, so nothing on
PCI (xHCI, NIC, even the IDE/VGA) is seen and Windows uses generic "Standard"
drivers. `setup /p j` forces the ACPI HAL, which enumerates PCI via ACPI
instead. Keep QEMU ACPI on (the default; do not pass `acpi=off`). Procedure:

1. Boot the CD, choose "Start computer with CD-ROM support" (drops to `A:\>`,
   CD is usually `D:`).
2. `fdisk` -> enable large-disk support -> create + activate a Primary DOS
   Partition -> reboot (back to CD-ROM support).
3. `format c:` -> `D:\setup.exe /p j` -> run the GUI wizard to the desktop.
4. Verify in Device Manager connection view that a "PCI bus" node now
   exists and the xHCI shows as an unrecognized "PCI Universal Serial Bus"
   (Code 28) under Other devices.

`-action reboot=reset -no-shutdown` are important: this QEMU build otherwise
exits on each guest reboot, and Setup reboots several times. Do not
`system_reset` from the monitor during Setup's hardware-detection/first-boot
phase; it wedges Win98 on the splash. (The alternative to `/p j` is the
"PCI bus method": plain install, then Device Manager -> System devices -> Plug and
Play BIOS -> Update Driver -> Show All Hardware -> PCI Bus. `/p j` is simpler.)

Use `nec-usb-xhci` only when the aim is to test QEMU's NEC/Renesas-flavored emulation:
```
  -device nec-usb-xhci,id=xhci
```

The setup scripts default to `qemu-xhci`. To generate launchers for another model:
```
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu.ps1 -XhciDevice nec-usb-xhci
```

The default PC machine provides PS/2 keyboard and mouse, which should remain the primary input path until `xhci98.sys` works.

After Win98 is installed, boot the installed VM without the CD: drop `-cdrom`
and `-boot d`, and use `-boot c` to boot the HDD (this is what the generated
`scripts\local\qemu-win98-run.cmd` runs):
```
qemu-system-x86_64 \
  -machine pc \
  -cpu pentium3 \
  -m 256 \
  -drive file=win98.img,format=qcow2,if=ide \
  -device qemu-xhci,id=xhci \
  -boot c \
  -action reboot=reset -no-shutdown \
  -monitor tcp:127.0.0.1:55555,server=on,wait=off
```

NUSB's `usbport.sys`/`usbhub20.sys` are placed by the NUSB installer itself (its `_NUSB.INF` `[DefaultInstall]` copies them to `SYSTEM32\DRIVERS` unconditionally), so no EHCI is needed; confirmed on real xHCI-only hardware. Verify with `dir C:\WINDOWS\SYSTEM32\DRIVERS\usbport.sys`. Optionally, add `-device usb-ehci,id=ehci` alongside `-device qemu-xhci,id=xhci` if you want a live `usbehci`-on-`usbport` miniport to observe as a reference (see "Installing the usbport USB 2.0 Stack (NUSB)" and `docs/contributing/lessons.md`, "The NUSB installer places the usbport stack unconditionally"); it is not required.

For port-event testing, attach USB devices explicitly to the XHCI bus:
```
  -device usb-kbd,bus=xhci.0,port=1 \
  -device usb-mouse,bus=xhci.0,port=2
```

How QEMU places devices on USB2 vs USB3 logical ports (observed with the
Phase 0 qualifier): `qemu-xhci` defaults to 4 USB2 + 4 USB3 logical ports, and
each device attaches by the speed it advertises. SuperSpeed-capable models
(`usb-storage`, `usb-bot`) land on USB3 ports; HS/FS models (`usb-kbd`,
`usb-mouse`, `usb-tablet`, `usb-hub`, `usb-net`, `usb-audio`) land on USB2
ports. Practical consequences:

- `port=N` uses QEMU's own numbering, which does not map 1:1 onto the
  Supported-Protocol port numbers the driver sees, and a device cannot be
  pinned to a port of the wrong speed class (`usb port N ... not found`).
  When mixing device types, omit `port=` and let QEMU place them.
- To exercise a mass-storage device on a managed USB2 port (what a real
  USB3 stick looks like to this USB2-only driver), remove the USB3 ports:
  `-device qemu-xhci,p3=0,...`. `usb-storage` then falls back to
  High-Speed on a USB2 port. (`p2=N`/`p3=N` set the port counts.)
- With `p3=0` QEMU still advertises a USB3 Supported Protocol capability
  claiming zero ports, so code walking the capability list must tolerate a
  zero-port protocol range.
- When the root ports run out, QEMU auto-inserts a `usb-hub` and puts
  overflow devices behind it.

Headless guest automation without a guest agent (established by the
Phase 0 matrix runner, `xhciqual/test/run-qemu-matrix.ps1`):
for a DOS guest in VGA text mode, expose the QEMU monitor with `-monitor
tcp:127.0.0.1:PORT,server=on,wait=off`, type commands with `sendkey`, scrape
the 80x25 char/attribute buffer with `pmemsave 0xb8000 4000 file`, and
capture program output with `-serial file:...` plus the program's own
serial mirroring. Cold-boot a fresh QEMU process per test case for
determinism.

The monitor and `sendkey` approach also works for the Win98 VMs, but the
`0xb8000` scrape does not capture the graphical Win98 desktop. Use
`screendump file.ppm` plus image comparison/OCR when visible GUI state must
be checked, and prefer `-serial file:...` or `-debugcon file:...` for
machine-readable test output. For companion-era controllers, the QEMU
device names are `usb-ehci` and `pci-ohci` (UHCI variants exist as
`piix3-usb-uhci` and friends).

#### Who drives the GUI: hand it to the operator

Whenever a VM is launched for any purpose, hand the GUI back. An agent
driving a Windows GUI blind (`sendkey`, screendump, crop, re-read, repeat)
takes minutes per dialog and burns a round trip on every wrong guess about
focus, accelerator keys or keyboard layout. The operator at the console does the
same clicks in seconds. This was measured repeatedly during the Phase 4
checkpoint runs, and it is the standing preference for this project.

So the division of labour is:

- The operator drives everything inside the guest window: wizards, Device
  Manager, Driver Verifier, file copies, shutdown.
- The agent keeps the host side: launching the VM, the QEMU monitor
  (`device_add`/`device_del`, `info` commands, `trace-event`, `screendump`),
  reading and archiving the debugcon trace, and reconciling counters.

When a VM is launched, write the operator explicit numbered GUI steps before
waiting on them: which window, which item, which button, and what a correct
result looks like. Name the observation each step is meant to produce, because a
step whose result nobody reads is how a checkpoint clause ends up "satisfied" by
inference (`lessons.md`, "hot-plug operations are not hot-plug events").
Screenshot to confirm state when it is cheap; never assume a dialog is where it
was left.

Two traps that make blind driving worse than it looks:

- The guest keyboard layout is not necessarily US. The 2d Win2000 guest is
  US-Dvorak, so `sendkey e` types `.` and a path comes out as garbage.
  Diagnose by sending `a`..`m` into any text field: `axje.uidchtnm` is Dvorak.
  Non-character keys (`ret`, `tab`, `spc`, `backspace`, arrows, `alt-`) are
  unaffected, so navigation works even when text does not.
- Alt accelerators do not reliably toggle a focused checkbox through
  `sendkey`. Tab to the control and use `spc`, then screendump to confirm,
  rather than trusting the keystroke landed.

To pass a USB storage device from the host:
```
  -device usb-storage,bus=xhci.0,port=3,drive=usb0 \
  -drive if=none,id=usb0,file=files.img,format=raw
```

For the Phase 8 bulk path, the generated `qemu-win98-net-storage-test.cmd` attaches an emulated USB Ethernet adapter and a USB mass-storage device to the xHCI bus:
```
  -netdev user,id=usbnet0 \
  -device usb-net,netdev=usbnet0,bus=xhci.0,port=1 \
  -drive if=none,id=usbdisk0,file=usbdata.img,format=raw \
  -device usb-storage,drive=usbdisk0,bus=xhci.0,port=2
```
`usb-storage` exercises the bulk transfer path end to end only if a USB mass-storage function driver binds, and the two targets differ here. Base Win98 has no Microsoft generic USB mass-storage class driver; in the normal project VM it comes from NUSB (or a vendor driver). Win2000 SP4 has one in the box. So the same `usb-storage` device can prove the bulk path on the 2b VM while proving nothing on 2a if NUSB's driver is absent; check which side is missing before reading it as an xHCI fault. `usb-net` likewise only proves the bulk path if a function driver binds to QEMU's emulated NIC on the target under test; see "QEMU coverage limits" below.

QEMU XHCI emulation is generally more reliable for driver development than VirtualBox. QEMU also supports GDB-based kernel debugging (`-s -S` flags).

### Getting files into the guest (the deploy loop)

Every driver iteration is: build on the host -> get `xhci98.sys` into the VM ->
install/replace -> reboot -> observe. Options for the "get it in" step, most
convenient first:

1. VVFAT, a host directory as an immutable FAT backing store. QEMU can
   synthesize a FAT16 volume from a host directory at boot. An IDE hard disk
   frontend expects writable media, so place a temporary snapshot above the
   read-only VVFAT node:

   ```
   -drive "file=fat:..\deploy,format=raw,if=ide,index=1,snapshot=on"
   ```

   The guest sees a second hard disk containing whatever was in the directory
   when the VM started (copy the freshly built `xhci98.sys` there before each
   boot). Keep the directory small (FAT16 limits; a few MB of driver files is
   fine). This is the recommended deploy path: no image mounting on the host at
   all. The guest may write to the apparent disk, but those writes go to a
   throwaway qcow2 overlay under `%TEMP%` and disappear when QEMU exits.

   **Do not substitute `fat:rw:`.** That selects VVFAT's beta write-back mode,
   which can modify the host directory. Bare read-only VVFAT attached directly
   as an IDE disk fails under QEMU 11 with `Block node is read only`; the
   per-drive `snapshot=on` is what supplies writable guest media without making
   the host directory writable. This exact form was verified against QEMU
   11.0.50. Copy an installer to `C:` before running it; don't execute
   directly off the VVFAT drive.

2. Floppy image (`vm\transfer.img` from `setup-qemu.ps1`): write into it
   on the host with mtools (`mcopy -i vm\transfer.img xhci98.sys ::/`) if
   mtools is installed. Windows cannot natively mount raw floppy images, so
   without mtools prefer option 1 or 3. Also the only easy guest-to-host
   path (copy to `A:` in the guest, read the image with mtools/7-Zip on the
   host), useful for pulling logs out.

3. ISO: build a small ISO on the host (`oscdimg` from the Windows ADK, or
   `mkisofs`) and attach it with `-cdrom`. Win98 reads CDs natively.
   Read-only by nature; good for larger payloads like NUSB itself.

### VM snapshots - iterate without fear

The setup scripts create qcow2 images, which support named snapshots (VM must
be powered off):

```
qemu-img snapshot -c post-nusb vm\win98.img    # create
qemu-img snapshot -l vm\win98.img              # list
qemu-img snapshot -a post-nusb vm\win98.img    # revert (apply)
qemu-img snapshot -d post-nusb vm\win98.img    # delete
```

Take a snapshot immediately after the Phase 2a checkpoint passes (OS + NUSB
installed, transfer path proven) and another right before each first-install
of a new driver build. Reverting a snapshot is by far the fastest recovery
from a boot-crashing driver. `-snapshot` on the QEMU command line gives a
throwaway boot (all writes discarded on exit), useful for risky experiments,
but remember nothing is saved, including files you copied in.

The two Win98 snapshots on `vm\win98.img` are not interchangeable.
`post-nusb` is the Phase 2a checkpoint state without `usbd.sys`. It is kept
because it is the only state that can test the INF's own `usbd.sys` delivery;
on a guest that already has the file, that install path is never exercised.
`phase2a-usbd-ok` is the same plus the `usbd.sys` fix from Phase 2a task 6, and
is the baseline every later Win98 phase starts from. Revert to `post-nusb` only
when the question is whether the package carries the file.

### QEMU monitor - hot-plug USB devices without rebooting

Phase 4+ needs plug/unplug events on demand (Port Status Change testing, the
Phase 11 20x replug cycle). Use the QEMU monitor: press Ctrl-Alt-2 in the GUI
window for the monitor console (Ctrl-Alt-1 returns to the guest display), or
expose it with `-monitor telnet:127.0.0.1:5555,server,nowait` and connect with
`telnet 127.0.0.1 5555`. Useful commands:

```
info usb                                      # devices on the USB buses
info pci                                      # confirm the xHCI PCI function
device_add usb-kbd,bus=xhci.0,port=1,id=kbd1  # hot-plug
device_del kbd1                               # hot-unplug (by id)
system_reset                                  # hard reboot the guest
```

A device behind a hub is addressed as `bus=xhci.0,port=<rootport>.<hubport>`.
A hub is not a bus, and `bus=hub1.0` is refused with `Bus 'hub1.0' not found`.
`scripts\local\hub7bv0.ps1` once carried the wrong form through two runs
because its `Send-Mon` discarded the monitor's reply, so the stage failed
silently and looked like a stage that worked; every monitor command in that
script now reads and prints its reply and exits nonzero on `Error:`. Name root
and hub ports explicitly rather than leaving QEMU to assign them (`info usb`
then reports `Port 2`, `2.1`, `2.2`, `2.2.1` for a two-tier tree), and note
that a duplicate `device_add` answers `Error: Duplicate device ID`.

For scripted/repeated cycles (e.g. 20x replug), drive the same commands over
QMP: add `-qmp tcp:127.0.0.1:4444,server,nowait` and send the JSON forms of
`device_add`/`device_del`.

Never fire a state-changing monitor command without reading its reply, and confirm the effect rather than the command. Measured on 2b: `scripts\local\unplug-8v.ps1` wrote `device_del` and closed the socket 150 ms later without reading anything back. It printed a confident

```
FIRED at 15:54:42.161
  READ commands before the pull : 1,759  (+376 since baseline)
```

while the device was still attached; `info usb` still listed `Device 1.1, Port 1, ... ID: stor`. HMP prints nothing on success, so "no output" and "an error nobody read" look identical down the wire. What caught it was the driver's own counters disagreeing with the script and agreeing with the device: no `SlotsDisabled`, no `DevicesTornDown`, and submitted == completed with nothing cancelled, the signature of a teardown that never happened. Two rules: read the reply, and then confirm the state you were trying to reach. A pull is only a pull if `info usb` no longer lists the device. `unplug-8v.ps1` now does both and exits 3 if the device is still there.

A guest's file cache can absorb an entire I/O test while the guest reports performing it. Measured on 2b: a read loop over a 24 MB working set on a 256 MB Win2000 guest completed all its copies and reported reading 192 MB, while `usb_msd_cmd_submit` moved zero; the device saw 8.8 MB in the whole boot. The cache was then measured to saturate at about 170 MB on that guest, from the per-file cost jumping ~140 -> ~260 SCSI commands once the copy source began being re-read.

So a read clause needs a working set larger than the guest's cache; issuing many `copy` commands is not the same thing, and a medium smaller than the guest's RAM cannot support one at all (`vm\usb8v-smp.img` is 256 MB against the SMP VM's `-m 512`). Writes are unaffected, since they must be flushed, so gating an unplug on `-Direction Write` works at any medium size. Same shape as the `removable=on` trap: every layer reports health while nothing reaches the device.

When proving traffic was in flight at an unplug, tag-match the cancelled command rather than counting a window. QEMU's trace carries the CBW direction and length on both the submit and the cancel, so one grep settles direction unambiguously:

```
usb_msd_cmd_submit lun 0, tag 0xaf680e70, flags 0x00000080, len 10, data-len 65536
usb_msd_cmd_cancel tag 0xaf680e70
```

`flags 0x80` = data-IN, 10-byte CDB, 64 KB: a `READ(10)`. A window count is an aggregate over whatever the guest happened to be doing; this is the transfer that was cancelled.

`sendkey` names keys by their US-QWERTY label. The 2b and 2d guests are US-Dvorak, but 2a is not. Injected text therefore arrives transposed on the Win2000 guests: `echo` lands as `.jdr`. `scripts\local\sendtext.ps1` carries the inverse map and defaults to `-Layout dvorak`, which is right for 2b and 2d and wrong for 2a: the Windows 98 guest is plain US-QWERTY, measured by typing `dir a:` into its DOS box with `-Layout qwerty` and reading it back off a screendump.

So the default is a per-guest hazard in both directions. Pass `-Layout qwerty` for 2a, and establish the layout on any guest not on this list rather than assuming it: type a command without Enter, screenshot it, then send Enter. It costs one screendump.

Two further console traps measured the same day: `Ctrl+C` does not kill an interactive `cmd.exe` `for` loop (it skips iterations and the loop resumes; close the window instead), and keystrokes injected into a busy console land in type-ahead and are echoed at the prompt looking as though they ran.

`screendump` writes a binary P6 PPM whatever extension you give it, so a file named `.png` from the monitor is not one. `scripts\local\ppm2png.ps1` converts it and `scripts\local\shot.ps1` does dump-and-convert in one step.

**Do not re-attach a storage device to a different root port after unplugging it mid-write. On Win98 it wedges the machine, and it is not this driver.** Measured: copy a large file to a `usb-storage`, pull it with `device_del` while transfers are in flight, take Win98's recoverable `Disk Write Error`, then `device_add` it on another port, and the guest stops. Screen byte-identical over 12 s, unclickable, CPU still executing in ring-0.

The control reproduces it with `-device usb-ehci` and `bus=ehci.0`, that is with NUSB's own `usbehci.sys` carrying every transfer and this driver measurably idle (`usb_msd_cmd_submit` 652 against `usb_xhci_xfer_start` 0, `slot_enable` 0, `port_reset` 0). Same exoneration shape as `0028:C00312EE` below. It does not happen on Win2000, where the identical sequence re-enumerates cleanly (`SlotsEnabled` 1 -> 2, two more `RH_SetFeaturePortReset` calls). So on Win98 replug onto the same port, and if a different port is unavoidable, expect to reboot.

One caveat: the two wedges are not proven to be one bug. In the xHCI case this driver's `HealthPolls` kept climbing (3,019 -> 3,266 while it was watched) while everything usbport drove froze (`RhPortStatusQueries` stuck at 244, `RhChangesCleared` at 24, `PortEventChanges` at 4), whereas in the EHCI control `HealthPolls` itself is frozen at 58. That poll runs off `UsbPortRequestAsyncCallback`, so a frozen count means usbport's timer machinery stopped too. The control's failure is deeper, and claiming an identical mechanism would be claiming more than was shown.

One replug observation is unexplained. On the 2d SMP guest (write-unplug cycles) the fourth attach of a boot enumerated normally but its volume never mounted: `dir f:\` hung. The driver answered every command the guest issued, all `status 0`, then the guest stopped asking; nothing was outstanding, `DevicesStalledOut`, `EndpointHalts` and the refusal counters all read 0, `HealthPolls` was still climbing, and QEMU's block layer was idle on every drive including C:. A reboot cleared it and the same medium mounted at once.

Two explanations were formed and both refuted by later cycles: "a cancelled in-flight command wedges the next mount" (the attaches after cycles 4 and 5 mounted fine) and "attach count accumulates removal state" (attach 3 of the next boot was fine, and the wedge was attach 4 of the first). So: one occurrence in seven attaches, cause unknown. If it recurs, the discriminator is the EHCI control the 8-V launcher already carries. The state at the wedge was captured as `counters-at-wedge.txt`, `blockstats-at-wedge.txt` and `trace-tail-at-wedge.txt` under `vm\8v-2d-smp\` (discarded 2026-08-30; what they showed is above).

A `device_del` on a storage device takes its block backend with it. `-drive
if=none,id=usbdisk,...` has auto-delete semantics, so after unplugging a
`usb-storage` that referenced it, the same `device_add` fails with `Property
'usb-storage.drive' can't find value 'usbdisk'`. That reads like a typo and is
not one. Re-create the backend from the monitor first:

```
drive_add 0 if=none,id=usbdisk2,file=<abs path>,format=raw
device_add usb-storage,drive=usbdisk2,bus=xhci.0,removable=on,id=stor2
```

This matters for any replug loop over a storage device. Phase 11's 20x
unplug/replug matrix and Phase 10's harness both need a fresh backend id per
cycle, or every cycle after the first fails at the host and never reaches the
driver. HID devices have no equivalent problem.

#### `sendkey` and `mouse_move` drive the USB HID devices, not only PS/2

Measured on all three guests. With a `usb-kbd` attached and bound,
`sendkey <k>` produces exactly two interrupt completions (down + up) on the
xHCI interrupt endpoint, and `mouse_move dx dy` produces one per call on an
attached `usb-mouse`; four seconds of idle produces none. So HID traffic can be
generated entirely from the monitor, which both proves the keyboard works end
to end without typing into the guest, and gives a keep-alive, which the next
trap makes mandatory.

#### A replug onto an idle-suspended controller is invisible, and fails silently

Win98 idle-suspends the controller within about half a second of the last
transfer. A `device_add` while it is halted is seen by nothing: `PORTSC` sits
at CCS=1 / PED=0 / Polling with the connect change already acknowledged, and
nothing above ever issues the port reset. An attached-but-silent HID device
does not prevent it.

The first ten-cycle attempt lost 8 of 10 cycles this way and looked like a
driver defect; the counters simply did not move. The tell is `restore-state
failures` climbing (one per suspend/resume pair) across a run that should have
none.

Read that counter as "the controller resumed N times", not as "N stages were
lost". On QEMU it climbs once per resume whether or not anything was missed,
because QEMU fails every CSS/CRS restore (each reads `USBSTS=00000401`, `SRE`
set) and the driver's reinitialize path is what runs instead; one 2a boot read
4, one per pre-hub resume, with all ten hot-plug stages landing. So on a run
with traffic, the evidence that nothing was lost is that every stage landed,
and the counter only says how many idle windows there were. On 2b it reads 0,
because native usbport never idle-suspends the controller.

This is fixed (roadmap task 11-V.6, and `docs/using/release-notes.md`,
the `DisableSelectiveSuspend` entry under "Known limitations", which
documents the setting rather than the defect). Both install paths write
`HKLM\System\CurrentControlSet\Services\USB\DisableSelectiveSuspend = 1`
(the Windows 98 path since task 11-V.6, the NT path since 1.0.0.2, when the
Windows XP guest showed XP's usbport idling the controller about thirty
seconds after start), which stops NUSB's usbport idling the controller at
all: `SuspendController` never fires, `USBCMD` reads `0x00000005`, and a
hot-plugged device enumerates with no Refresh. Everything below describes
the behaviour without that value, which is what a guest installed from the
batch 11-V baseline media, or any image predating this INF (including the
working 2a and 2b images), still does. Set the value by hand on such a
guest, or install current media, before reading an idle hot-plug as a
defect.

The fix is a setting and not driver code, and the reason also says what a
future wake path would have to overcome. The differential came out the awkward
way: NUSB's own `usbehci.sys`, under the same `usbport.sys` on the same idle
guest, is woken by a connect, so the stack was exonerated and this driver was
owed an explanation.

The explanation is architectural. `usbehci.sys` masks and
halts on suspend as we do and then re-arms one interrupt across the halt
(`USBINTR |= 4`, Port Change Detect; measured live at `USBINTR = 0x00000004`
on a halted EHCI and read out of the binary at VA `0x13C92`). There is no xHCI
equivalent to re-arm: the spec gates Port Status Change Event generation on
`HCHalted = '0'` (p.294) and says that `EINT`/`PCD` "do not generate an
interrupt" (p.365), so a halted xHC has no interrupt source a driver could
leave enabled.

The one route the architecture does offer is a PCI PME# wake,
and `qemu-xhci` exposes no PCI Power Management capability at all (capability
chain `@0x90 -> 0x11` MSI-X, next `0x00`), so it cannot be built against or
tested in this vehicle. It is carried to Phase 13.

Two measurements from the same session belong beside the test-harness advice
below. usbport does not call the miniport at all while it holds the controller
suspended (`CheckCallbacks`, the entry counter above every gate, frozen equal
to `HealthPolls` across 90 s), so there is no callback a poll could ride. And
nothing writes a PCI power register at suspend time: the idle "suspend" is a
software halt with the device left in D0, so a Refresh recovers it
cleanly and nothing is lost while it sleeps.

The test-harness fix is to carry both phases of each cycle with monitor
traffic: `sendkey` during the attached phase (which also posts the read the
unplug must tear down), `mouse_move` on a second, permanently attached device
during the detached phase. `scripts\local\cycle7av.ps1` does this and held
`restore-state failures` flat for a whole ten-cycle run. Win2000 does not need
it (both guests there enumerated a hot-plugged HID device with no GUI action at
all), but the keep-alive is harmless, so use one script for all three.

#### Two operator traps around HID attach

- Attaching a `usb-mouse` steals the host pointer. QEMU makes the newest
  pointing device current, so the guest stops responding to the mouse until it
  has a working USB HID driver, which is what you are trying to install.
  `info mice` shows the active one (`*`); `mouse_set <index of QEMU PS/2
  Mouse>` gives the pointer back. Set it back to the USB index only when you
  want `mouse_move` to generate bus traffic.
- Win98 needs one Device Manager -> Refresh per boot to resume the
  idle-suspended controller before it will see a monitor-attached device,
  unless `DisableSelectiveSuspend` is set as described above. Once HID traffic
  flows the controller stays awake and everything else can be driven from the
  monitor.
- Win98 has no HID driver on disk, so the first attach of each device class
  runs the Add New Hardware wizard and asks for the CD. Attach the Win98 SE ISO
  as a CD-ROM drive in the launcher for a guest that has not bound HID before;
  it can be dropped once the files are on disk. Win2000 needs nothing; it bound
  both devices with no wizard and no GUI action.

### QEMU coverage limits

QEMU is the right default for Phases 3-7 (enumeration, HID, basic transfers), but it has a hard ceiling that real-hardware testing must cover.

Host controller models. QEMU emulates only two xHCI host models: `qemu-xhci` (generic, spec-clean, no vendor PCI ID) and `nec-usb-xhci` (NEC/Renesas-flavored PCI ID). Neither reproduces the real-silicon deviations Linux's `xhci-pci.c` and `pci-quirks.c` catalogue:

- `nec-usb-xhci` advertises a NEC ID but involves no firmware, so the Renesas uPD720201/202 driver firmware-upload path cannot be exercised in QEMU (QEMU emulates neither that chip nor the uPD720200's on-card SPI flash).
- Spurious-success (FL1000/VL800), Intel compliance-mode lockup, the ASM1042 64 KB bulk limit, AMD PLL re-lock, and BIOS/UEFI handoff contention are all absent from QEMU's emulation.

The residual-length and quirk-handling code paths therefore can only be validated against physical controllers. `nec-usb-xhci` is still useful as a negative test: it confirms the driver does not misfire NEC quirks against a controller that advertises the ID but lacks the bug.

Hub trees have two further ceilings, both measured and neither obvious from the command line.

- QEMU refuses to build a hub chain more than five hubs deep: a `device_add usb-hub` whose upstream port already sits five hubs down answers `Error: usb hub chain too deep`. That is xHCI's own Route String ceiling, so a device behind five chained hubs (tier 5, `route 0x11111`) is the deepest topology this vehicle can present and it is a legal one. A driver's too-deep refusal therefore cannot be exercised on QEMU at all; `topology: behind-hub refused - too deep` reads 0 by construction, not by passing. Check a claim like that against a guestless paused QEMU (`-S -display none -device qemu-xhci` plus a monitor) before spending a boot on it; the whole chain took ten seconds to falsify that way.
- Attaching a `usb-kbd` or `usb-mouse` takes the host's input away from PS/2 immediately, before the guest has installed it, which can lock you out of the guest.

  Measured on the 2a VM: `device_add usb-mouse` behind a hub made `info mice` report `* Mouse #5: QEMU HID Mouse` as current, and `device_add usb-kbd` likewise took the key events, while Win98's Add New Hardware Wizard for those very devices was still open and modal. The wizard needs input to dismiss; the input was routed to the device the wizard was installing. The pointer is recoverable with `mouse_set <n>` (pick the `QEMU PS/2 Mouse` line from `info mice`), but there is no `keyboard_set`; the only way back is `device_del` on the `usb-kbd`.

  So dismiss every hardware wizard before adding the next HID device, and do not leave a `usb-kbd` attached while you still need to type in the guest. The same routing rule (QEMU sends keyboard input to the most recently added keyboard) means a `usb-kbd` with no guest driver silently swallows every keystroke and the guest reads as hung; when a run only needs a High-Speed HID on the bus, attach a `usb-tablet` instead. This is a QEMU input-routing trap and says nothing about the driver; it cannot occur on bare metal, where the keyboard is physical.
- An attached hub keeps Win98's controller out of idle suspend. The trap documented above ("A replug onto an idle-suspended controller is invisible") applies to the first attach of a boot and then stops applying: a hub's interrupt pipe is polled continuously, so once one is enumerated the bus never goes idle. One 2a boot suspended four times, all before the first hub, and zero times across nine subsequent hot-plug stages. Useful in both directions: it makes hub churn easy to drive, and it means a run that needs an idle-suspend window (roadmap rider B1) cannot take it on a boot that has a hub attached.

Four clauses the emulated bus cannot present at all, each measured rather than assumed, so that nobody spends a boot on them again:

- A disconnect during a data transfer is not producible. `transfers cancelled` stayed 0 across monitor-driven unplug gaps from 8 ms to 450 ms, because an emulated bus completes control transfers effectively instantaneously and there is no window to hit. Only the mid-command-chain half (a device removed after Enable Slot and before SET_ADDRESS) can be driven, and it unwound cleanly on 2b. The cancel path's evidence for the in-flight case is the host vectors; interrupt endpoints (Phase 7a onward) do have traffic genuinely in flight between events.
- Low Speed is unreachable. No QEMU peripheral model declares it: `usb-mouse`, `usb-tablet`, `usb-wacom-tablet`, `u2f-emulated` and `usb-kbd,usb_version=1` attached together all report 12 Mb/s under `info usb`, and `usb_version=0` is refused outright (`Invalid usb version 0 for usb hid device`). That measurement struck the LS leg from the VM checkpoint. LS therefore survives host-side, in the `test_ctx` vectors (a slot context built with `XHCI_SPEED_LOW`, EP0 `MaxPacketSize` 8, the LS MPS0 correction), until a bare-metal run.
- Alternate-interface change and reset-pipe-after-stall are unavailable: no QEMU HID device has a second interface setting, and nothing in one produces a STALL on demand, so Reset Endpoint never runs and `endpoint resets` stays 0. This is distinct from the abort path, which is exercised.
- The two `TT pairs disagreeing with usbport` readings are a matched pair, and neither alone is evidence. A nonzero reading is QEMU-only: it is the phantom-translator case, usbport claiming a TT for a hub that physically has none, which no real hub can produce; the negative control. A zero reading with the DW2 pair naming the real translator is metal-only: there is no TT in QEMU for the graph and usbport to agree about. The QEMU row is the metal row's control, so both are printed.

Peripheral (function) drivers. QEMU emulates `usb-kbd`, `usb-mouse`, `usb-storage`, `usb-net`, `usb-audio`, and `usb-hub`. Note `usb-hub` is a USB 1.1 (Full-Speed) hub: it exercises Route String tiers and FS hub paths, but it cannot stand in for a High-Speed hub, so transaction-translator paths (FS/LS behind a HS hub, single- vs multi-TT; roadmap task 7b-A.3 / task 7b-M.1, `docs/contributing/design/02-hub-topology-route-string.md`) need `usb-host` passthrough of a physical HS hub or real hardware.

That passthrough rung is measured shut for hubs and must not be scheduled against: on a Windows QEMU host, libusb does not enumerate hub-class devices at all, so `info usbhost` lists every non-hub device and none of the hubs. That reading stands on its own and is what shuts the rung. A second observation recorded alongside it, that `device_add usb-host` naming a hub's VID/PID silently produces an unbound stub rather than an error, was partly measuring something else: on this build the VID/PID matcher stubs out for every device, hub or not (see below).

The stub itself is only readable as a refusal with a negative control. An impossible `vendorid=0xdead,productid=0xbeef` yields the identical `Device 0.0, Speed 1.5 Mb/s, Product USB Host Device` line, so without it a refusal reads as a success. The obstacle for hubs is not the predicted one: taking the hub from the Windows hub driver is not the problem, because libusb offers nothing to take it to. The downstream devices that do pass through easily arrive on the guest's root ports and exercise no topology at all.

These devices exercise the host-controller transfer paths only if a function driver binds to them on the target under test (see "Target Class Devices"). HID should bind with each target's existing stack; on Win98, `usb-storage` needs NUSB's mass-storage support or another storage driver because the base OS has no generic USB mass-storage class driver, while Win2000 supplies `usbstor.sys`. QEMU's emulated `usb-net` NIC is RNDIS (`0525:a4a2`) and neither target ships an RNDIS host driver (the Phase 8 Ethernet validation ran on `usb-host` passthrough of a physical ASIX AX88772, `0b95:7720`, for that reason), so for the Ethernet and audio tests prefer `-device usb-host,...` passthrough of a physical adapter with a known-working driver on the target under test, or real hardware.

Bridging emulation and real hardware. `-device usb-host,hostbus=N,hostaddr=M` passes a real USB peripheral from the host through to the guest's xHCI bus. This runs the driver against genuine peripheral adapters while still inside the VM, a useful step between pure emulation and bare-metal testing. It does not help with host-controller quirks, which depend on the emulated/physical xHCI chip, not the peripheral.

Use `hostbus`/`hostaddr`, never `vendorid`/`productid`. Measured on host `MINIS-W11P-YKM`, scoop QEMU 11.0.0, against an ASIX AX88772 (`0b95:7720`) that `info usbhost` was listing at that very moment as `Bus 1, Addr 7, Speed 480 Mb/s`:

| Form | Result |
|---|---|
| `vendorid=0x0b95,productid=0x7720` | attaches the stub, no error printed |
| `vendorid=2965,productid=30496` (same IDs, decimal) | attaches the stub, no error printed |
| `hostbus=1,hostaddr=7` | `Error: failed to open host usb device 1:7` |
| `hostbus=1,hostaddr=99` (no such device) | `Error: failed to find host usb device 1:99` |

So on this build the VID/PID matcher never matches and never complains; it is indistinguishable from a successful attach until you look at the speed. The bus/addr form is the only one that both works and diagnoses, and its two failures mean different things. "failed to open" is a device libusb found and could not take (a Windows function driver is holding it; rebind it to WinUSB with Zadig). "failed to find" is a stale bus/addr, which is routine because `hostaddr` is libusb's device address and changes on every replug. Resolve it fresh per attach out of `info usbhost` rather than writing a number into a launcher; `scripts\local\netattach-8v2.ps1` does that and then checks the result against the stub signature.

Whatever form is used, verify the claim landed. A refused claim still produces an `info usb` line. A genuine attach shows the device's real speed and product string (`Speed 480 Mb/s, Product AX88772 ...`, just as an emulated device shows `Product QEMU USB MSD`); the refusal shows `Speed 1.5 Mb/s, Product USB Host Device`. Reading `device_add` returning quietly as success is how an hour gets spent measuring an empty shell.

Passthrough of a multi-interface function is shut on a Windows host, and USB Audio is always that shape. QEMU intercepts `SET_CONFIGURATION` rather than forwarding it: `usb_host_set_config` releases the interfaces, calls `libusb_set_configuration` only when `bNumConfigurations != 1`, then calls `usb_host_claim_interfaces` and stalls the request if it cannot claim every interface of the active configuration. QEMU leaves that loop early only when the number of successful claims reaches `bNumInterfaces`, so a trace walking all sixteen `usb_host_claim_interface ... if 0..15` lines and ending `usb_host_req_emulated status -3` (`USB_RET_STALL`) is itself the proof it never got them all.

For the device measured, `bNumConfigurations = 1` (read host-side with `scripts\local\lsusb-desc.py`, a ctypes binding of QEMU's own `libusb-1.0.dll` that never calls `libusb_open`), so only the claim branch was ever reachable.

On the host, `Get-PnpDevice` shows the `usbccgp` parent with one child devnode per function, and the interfaces that claim are the ones with a devnode of their own; interfaces grouped into one function by the device's Interface Association Descriptor have none and refuse `-12 [NOT_SUPPORTED]`, so no Zadig rebind can help, since the grouping is the device's. "libusb can claim only an interface with its own devnode" is an inference; what is measured is the per-interface result and the devnode set. Scope: shut for any device whose active configuration contains an IAD-grouped multi-interface function (AudioControl plus AudioStreaming is always one), not for composites whose functions are all single-interface. The ASIX adapter above is the single-function case, not a counter-example.

### Windows 2000 SP4 Target VM (second first-class target; also the differential)

A second QEMU VM running Windows 2000 SP4 is the project's second first-class
target, not a lab instrument. From Phase 3 onward every checkpoint must be
observed here as well as on the Win98 VM, and a failure seen only here is a
defect to fix. It also happens to be the best differential available, because
`usbport.sys` is native on Win2000. If the same `xhci98.sys` binary fails on
Win98 but works here, suspect NUSB's back-ported `usbport`, the INF, or the
Win98 loader gate first. If it fails on both, suspect the miniport itself. If
it fails only here, suspect preemption, IRQL and Verifier findings, and then
native-usbport differences.

Setup mirrors the Win98 QEMU VM with two differences:

1. Use the same `qemu-xhci` device (the GDB stub `-s -S` and PS/2 input apply
   identically). Create a separate disk image, e.g. `vm\win2k.img`.
2. Install Service Pack 4 (or the standalone USB 2.0 update, KB319973) so the
   native `usbport.sys` + `usbehci.sys` + `usbhub20.sys` stack is present. Do
   not install NUSB; the usbport stack is native to Win2000.

Win2000 installs USB files on demand, per detected controller. A VM installed
with no USB controller attached has no usbport stack on disk at all. Before the
run launcher attaches EHCI, stage the ISO's own `USBD.SYS` and use the generated
controller-free preparation launcher:

```
7z e win2ksp4.ISO -o<dir> I386\USBD.SY_
expand <dir>\USBD.SY_ <dir>\USBD.SYS        (5.00.2195.6658, 20688 bytes)
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win2k.ps1 \
  -Win2KIso D:\isos\win2ksp4.ISO -Win2KUsbdSys <dir>\USBD.SYS
scripts\local\qemu-win2k-prepare-usbd.cmd
copy X:\USBD.SYS C:\WINNT\system32\drivers\ (in the guest; X: = QEMU VVFAT)
```

Shut down the preparation boot after confirming the copy, then start
`qemu-win2k-run.cmd`. It attaches `usb-ehci` (which makes native
`usbport.sys`/`usbehci.sys`/`usbhub20.sys` land and load) and `qemu-xhci`.
Without the preparation step, PnP's install is incomplete: it omits
`usbd.sys`, which `usbhub20.sys` imports, and the next boot bugchecks
`STOP: c000026c ... usbhub20.sys ... 0xc0000034`; Safe Mode bugchecks too.
Full write-up in `docs/contributing/lessons.md`, "`usbhub20.sys` bugchecks Win2000".

The VVFAT host directory is immutable because both generated launchers use a
read-only VVFAT node with `snapshot=on`. Guest writes disappear with the
temporary overlay; use a raw image when a guest-to-host path is needed.

Three launcher facts about this VM, each measured:

- `system_powerdown` does nothing on 2b. The machine type is `pc,acpi=off`,
  so there is no ACPI power button for Windows 2000 to see: the VM stays
  `running` and no teardown callback fires. A graceful 2b shutdown must be
  driven from the guest GUI. 2a's `-machine pc` does have it, and the 2a
  teardown readings were collected that way (batch 7b-V).
- A floppy can be inserted live without a reboot. The 2b launcher used in
  batch 13-L had no floppy drive, so the `copy C:\SNAP.TXT A:` guest-to-host
  trick that works on 2a failed; `change floppy0 <path>` on the monitor
  inserted `vm\transfer.img` into the (empty) controller at once and cost no
  boot. The launcher now carries `-drive if=floppy`, but `scripts\local\` is
  git-ignored, so a launcher fix is host-local and has to be re-made on every
  other host. This project has hit that trap on four of five hosts.
- Device Manager's "Driver Version" identifies the install, not the running
  binary. It is the INF's `DriverVer` as written at install time, and a
  `.sys` swap never re-runs the INF. After a swap it can name `0.0.0.1` while
  the tree is at `0.0.0.5`, in a place that looks more authoritative than a
  file date.

After install, record the native `usbport.sys` version and compare it with the
build NUSB installs on Win98 (see "Installing the usbport USB 2.0 Stack
(NUSB)"). Both share the Win2000 SP4 lineage, so the
`USBPORT_REGISTRATION_PACKET` version should match and a single miniport binary
should load on both; confirm this the first time the spike runs on each.

Recorded: native is `5.00.2195.6681` against NUSB's `5.00.2195.5652`. They are
different builds with an identical export set and ordinals, and their
registration version gates are semantically identical (a `Version >= 100` /
`>= 200` range test, not a magic-number comparison; in the 2195.x builds
`0x10000001` belongs to an unrelated USBUSER opcode, and `USBPORT_GetHciMn`
returns `0x57324B30` on both primary targets). A single binary is therefore
expected to serve both; see `docs/usb-xhci-info/usbport-miniport-interface.md`,
"Target ABI record".

The xHCI device (`PCI\CC_0C0330`) shows unrecognised here
too until this project's INF is installed. Native `usbport`/`usbehci` bind only
EHCI, so the xHCI appears as "Universal Serial Bus (USB) Controller" under
Other devices with Code 1.

Win2000 also has full WDM 1.10 and real kernel debugging (WinDbg/KD over
serial), which makes it a more comfortable place to confirm an ambiguous result
than Win98. Keep the iteration loop on Win98+NUSB, where the integration risk
lives and a break is most likely, but Win2000 is a target in its own right, so
no phase closes on a Win98-only observation. In particular the lifecycle, power
and locking behaviour Win98 cannot exercise at all (no preemption, no real spin
locks, no `IRP_MN_SURPRISE_REMOVAL`, `Po*` calls that are no-ops) is unmeasured
until it has run here. See `docs/usb-xhci-info/win98-wdm.md`, "Windows 2000 as
a co-primary target".

#### Run Driver Verifier here (there is no Win98 equivalent)

Source: Oney ch.3.5.4 (p.80-82) and ch.4.5.4 (p.110), a secondary source. Oney
also warns that Verifier options evolve and can interact; use the settings this
Win2000 SP4 installation offers, not a later OS's checklist copied verbatim.

Win98/Me is single-CPU and normally does not preempt nonpaged PASSIVE_LEVEL
WDM code unless it blocks or faults, so some synchronization defects are less
likely to appear there. Exercise `xhci98.sys` under Verifier on Win2000 from
Phase 3 onward:

- Special Pool and Pool Tracking catch CPU-side pool overruns and leaks. They
  do not guard usbport's common buffer against hardware DMA overruns.
- Force IRQL Checking catches pageable access at raised IRQL. The driver is
  intentionally nonpaged, but the check still guards future drift.
- Deadlock Detection is not available on Win2000; Microsoft added that
  Verifier option in Windows XP ([Microsoft's Driver Verifier version
  history](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier--what-s-new)).
  Do not make it part of a Win2000 checkpoint. Use the Phase 2d SMP stress run
  to exercise real contention and the static synchronization review against
  `docs/usb-xhci-info/usbport-miniport-abi.md` section 7 to check lock ordering.
- Low Resources Simulation, if offered, exercises allocation failure paths;
  Oney's described implementation begins failures about seven minutes after
  boot.
- DMA Checking, if offered, primarily verifies DMA-adapter API use. Under
  Option A those calls belong to `usbport.sys`, so enabling it for
  `xhci98.sys` does not validate TRB contents or make the common buffer
  bounds-safe. It becomes directly relevant to the Option-B allocation path.

Verifier failures are strong evidence of a real defect, but a Win2000-only
failure is not automatically proof: the native usbport build and WDM behavior
also differ from NUSB. Preserve the exact failing configuration and isolate
the axis.

The current QEMU Win2000 installation uses the Standard-PC uniprocessor HAL
because APIC mode hangs under this host's QEMU/TCG combination
(`docs/contributing/lessons.md`, the Standard-PC HAL entry). It can expose
preemption, IRQL, pool and Verifier failures, but it cannot expose simultaneous
cross-CPU ISR/DPC or DPC re-entry races. Those need the separate
multiprocessor Win2000 target, the WHPX-accelerated SMP VM described in
"Windows 2000 SMP Stress VM (Phase 2d)" below, with the static synchronization
review against `docs/usb-xhci-info/usbport-miniport-abi.md` section 7 as the
fallback if 2d is blocked. Adding `-smp 2` to this already installed VM does
not replace its HAL.

Once the 2d VM exists, prefer it for Verifier runs: same
native SP4 stack, plus real spinlock contention. Win2000 Verifier still has no
Deadlock Detection; the SMP stress run and static lock-order review remain
separate, required checks.

### Windows ME target VM (`2e`)

Windows ME support was asked for by the project owner on 2026-09-02, after
the SweetLow-stack work, and the same evening a Windows ME guest
(`vm\winme.img`, target `2e`) was installed and the driver observed on it
under SweetLow's stack: registration, `StartController`, the root-hub
callbacks, then a HID mouse, a mass-storage device and a composite (audio)
device, all bound. What tier that makes Windows ME is the owner's decision
(roadmap task 18.4), and no document names it as supported until that is
taken. What follows is what was established statically from the owner's
Windows ME OEM CD image (the `win9x\` directory's `PRECOPY1.CAB`, read with
7-Zip; nothing executed), the recipe, and what the run showed.

Why it is expected to be close. Windows ME is the same 16-bit setup engine
and the same VxD-hosted WDM model as Windows 98 SE, one WDM revision newer
(1.05 against 1.0; `docs/usb-xhci-info/win98-wdm.md`), so the undecorated
half of `src/xhci98.inf` is the half it reads. Nothing suggests Windows ME
dropped an export Windows 98 SE had, but the import gate holds no Windows
ME evidence, so the load itself is the first thing to observe. A USB 2.0
stack has to be installed on Windows ME as on Windows 98 SE: the Windows ME
CD carries none (its `layout.inf` names the USB 1.1 stack only, `uhcd.sys`,
`openhci.sys`, `usbd.sys` and `usbhub.sys`, and no `usbport.sys`,
`usbehci.sys` or `usbhub20.sys`). Both stacks the driver runs under on
Windows 98 say they cover it: Microsoft's own `USB2.INF` that NUSB ships and
SweetLow's edit of it both carry `; For Windows 98SE and Windows ME`. The
owner decided on 2026-09-02 that Windows ME runs SweetLow's stack only
(`usb20_win9x.zip`, unpacked in `tools\sweetlow-extracted` and staged as
`vm\SWEETLOW`): NUSB is a Windows 98 SE package, and the Microsoft
`USB2.INF` it carries is not tried on Windows ME.

What the CD says (static, file level):

- Its `USB.INF` binds `PCI\CC_0C0300`, `PCI\CC_0C0310` and vendor-qualified
  `CC_0C03` entries only, so `PCI\CC_0C0330` is unclaimed there exactly as it
  is on the other two targets.
- `layout.inf` places `usbd.sys` (22,928 bytes) and `usbhub.sys` (41,904
  bytes) on disk 2, `BASE2.CAB`, with `usbccgp.sys` beside them; `ntkern.vxd`
  is on disk 20. Its own `USB.INF` and `HIDDEV.INF` carry
  `LayoutFile=Layout.inf, Layout1.inf, Layout2.inf`, so the route release
  1.0.0.1 takes on Windows 98 (`LayoutFile=layout.inf` in `src/xhci98.inf`,
  the OS supplying `usbd.sys` and `usbhub.sys` from its own source) is the
  route that OS's own INFs use.
- `USB\COMPOSITE` binds to `Composite.Dev`, whose `AddReg` is
  `CommonClassParent.AddReg`: Windows ME's composite parent is `usbccgp.sys`,
  not `usbhub.sys`. The `usbhub.sys` copy the Windows 98 install path makes
  is therefore expected to be inert there, as it is under SweetLow's stack,
  and harmless (flag 16 never replaces a file).

The recipe, as run on 2026-09-02 (deviations under "What the run showed"):

1. Create the image and install the OS by hand. `scripts\setup-qemu.ps1
   -WinMeIso <path> -Win98Iso <path> -CreateDisk` creates `vm\winme.img` and
   writes `scripts\local\qemu-winme-install.cmd`, which boots the Windows 98
   SE CD's floppy for `fdisk` and `format c:` (the Windows ME CD's own FORMAT
   never writes a sector under QEMU; see "What the run showed") with the
   Windows ME CD as the second CD-ROM, `E:`, for `E:\WIN9X\SETUP.EXE /p j`;
   the `/p j` (ACPI HAL) rule is the same as Windows 98's. Install the OS
   from the CD, then SweetLow's USB 2.0 stack (right-click Install on
   `SWEETLOW\USB2.INF` from the transfer drive, `prepare-image.ps1 -Target 2e
   -Boot -Xfer -XferAdd vm\SWEETLOW`, then a Start-menu shutdown and a
   relaunch rather than the restart it offers).
2. Add the `2e` target to your `matrix.config.psd1` from `config.sample.psd1`,
   with `Cd` pointing at the Windows ME CD image.
3. `powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2e -Boot
   -Xfer -XferPackage`, then install the driver from the transfer drive as
   for Windows 98. The prep script treats `2e` as the Windows 98 family
   (`Like = '2a'`), attaches the target's own CD, and stages the whole qemu
   package because of `-XferPackage`; `PrepareOnly = $true` keeps the target
   out of both matrix runs until it has rows of its own.
4. What to read: `DriverEntry`, `USBPORT_GetHciMn`, `StartController` and the
   `RH_*` callbacks in `out\phase10\prep-2e-debugcon.log`, then `-Status`
   with the keep-alive pointer bound (`endpoints opened` 1), then HID and
   storage by hand. Whether the copy phase asks for the Windows ME CD for
   `usbd.sys` and `usbhub.sys` is the first reading to take.

What the run showed (2026-09-02, scoop QEMU 11.0.0, `-machine pc`, the owner
at the console, the host side through `prepare-image.ps1`):

- The Windows ME CD is El Torito bootable and its boot menu defaults into
  Setup without `/p j`; F3 exits to the prompt. After `fdisk`, the CD's own
  `D:\WIN9X\FORMAT C:` printed "0 percent completed" and never wrote a
  sector (`info blockstats`: `wr_operations=0` for minutes; the CPU looping
  in real mode with interrupts off through a code segment that read back as
  all zeros). The cause was not chased. The format that worked was Windows
  98 SE's: boot the Windows 98 SE CD's floppy ("Boot from CD-ROM", then
  "Start computer with CD-ROM support", F3 out of its Setup) with the
  Windows ME ISO attached as the second CD-ROM (`-drive
  file=Win98SE.iso,media=cdrom,index=2 -drive file="Windows Me OEM
  Full.iso",media=cdrom,index=3 -boot once=d`), `format c:` from `D:\WIN98`
  (about thirty seconds for 4 GB), then `E:\WIN9X\SETUP.EXE /p j`.
  `setup-qemu.ps1 -WinMeIso` with `-Win98Iso` writes the launcher that way.
- Windows ME OEM Setup copies its CAB set to the hard disk before
  extracting (the CD was read once, about 167 MB, then only the hard disk
  moved, in 512-byte BIOS operations), so the copy bar sits at 10 to 25
  percent for long minutes while progressing. Neither the SweetLow
  `USB2.INF` install nor the driver install later asked for the CD, which
  is consistent with that on-disk copy; it was not confirmed with a `dir`.
- Every restart the guest initiated wedged at the Windows ME logo exactly
  as `lessons.md` records for Windows 98 ("the guest reboot after a driver
  install wedges at the splash"), and the mechanism is now known: after the
  warm reset the local APIC's LINT0 reads masked (`info lapic`: `LVT0
  0x00010000`), so the 8259's IRQ0, pending and unmasked there, never
  reaches the CPU and IO.SYS waits on the BIOS tick at `0040:006C` forever.
  A cold launch shows `LVT0 0x00000700 ExtINT`. So every restart of this
  guest is a Start-menu shutdown and a relaunch; Setup's own restarts
  included.
- Snapshots on `vm\winme.img`: `winme-clean-install` (the OS to the
  desktop, first login done, nothing else), `winme-stock-stack-driver-attempt`
  (the package installed on the stock USB 1.1 stack: the install went
  through with no CD prompt and the controller shows Code 2, "The
  NTKERN.VXD device loader(s) for this device could not load the device
  driver", with the debug console at 0 bytes, `usbport.sys` being absent),
  and the SweetLow-stack driver state after that, built from
  `winme-clean-install`.
- Under SweetLow's stack, from a cold start with the package installed:
  `DriverEntry`, `USBPORT_GetHciMn=10000001`, `USBPORT_RegisterUSBPortDriver
  status=0`, `StartController` (8 USB2-only ports, all managed), the
  `RH_*` family, and `-Status` reading `devices addressed` 1 / `endpoints
  opened` 1 for the keep-alive mouse. `-Attach storage` bound with no wizard
  (`devices addressed` 2, `endpoints opened` 3, "USB Mass Storage Device");
  a hot-plugged `usb-audio` (`device_add usb-audio,id=prep_audio,bus=xhci.0,
  audiodev=prepaud` on the prep monitor) ran the Add New Hardware Wizard
  from the CD and bound as "Composite Device" under Universal Serial Bus
  controllers, Windows ME's own `usbccgp` parent, with "USB Audio Device"
  under Sound, video and game controllers; the controller and "USB 2.0 Root
  Hub" clean, no refusal counter moved.

What Windows ME becomes (a third first-class target with the full checkpoint
tax `AGENTS.md` describes, or a supported-in-VM target stated the way
Windows 2000's status is stated) is the owner's decision, roadmap task 18.4,
open at the time of writing; until it is taken no document names Windows ME
as supported.

### Windows XP target VM (roadmap Phase 19)

The owner asked on the morning of 2026-09-03 whether Windows XP could be
supported, and installed a 32-bit Windows XP Professional SP3 guest by hand
the same afternoon (`vm\winxp.img`, 8 GB, snapshot `winxp-clean-install`
taken at 14:11 with the OS at the desktop and nothing else on it). Nothing
had ever been run on XP itself; `win98-wdm.md` ("What about Windows XP?")
kept it best-effort for cost, with the static registration gate looking
compatible. What tier XP becomes is the owner's decision, roadmap task 19.6,
and no document names it as supported until that is taken. What follows is
the recipe, what the first afternoon measured, and what the measurement
changed: the two INF fixes release 1.0.0.2 carries ("The files the OS
supplies" below, and the `DisableSelectiveSuspend` block in
`src/xhci98.inf`).

The recipe. `scripts\setup-qemu-winxp.ps1` writes both launchers into
`scripts\local`; the hand-written ones that took the first readings are
what it reproduces.

1. `scripts\setup-qemu-winxp.ps1 -WinXpIso <path> -CreateDisk`, then
   `scripts\local\qemu-winxp-install.cmd` and install the OS by hand (the
   owner did; the VL media asks for a volume licence key). The machine is
   `-machine pc` (ACPI on), `-accel whpx,kernel-irqchip=off`, `-cpu
   pentium3`, 512 MB, `-vga std`, `-boot d` every boot (the XP CD's "Press
   any key" falls through to the hard disk, which is what Setup's own
   reboots need). WHPX, not TCG: XP Setup selects the ACPI APIC HAL, and on
   this project's hosts TCG storms the APIC-clock ISR on that HAL
   (`lessons.md`, "The vector-0xD1 storm is the accelerator"); under WHPX
   the whole install ran with no storm. Probe WHPX on a new host first: a
   bare `-M pc -accel whpx,kernel-irqchip=off -display none -m 64` that is
   still alive after a few seconds is the yes. Shut down from the Start
   menu and snapshot: `qemu-img snapshot -c winxp-clean-install
   vm\winxp.img`.
2. Stage the package: `make-package.ps1 -Flavor qemu -OutDir vm\xferxp`.
   Only the qemu flavour writes the port-0xE9 trace the launcher captures.
3. `scripts\local\qemu-winxp-run.cmd <tag>`: the same machine plus
   `qemu-xhci`, the VVFAT transfer drive (`E:` in the guest; the CD, when
   attached, is `D:`), `isa-debugcon` at 0xE9 into `vm\winxp-debugcon.log`
   (rotated per boot like the other guests' logs), and the QEMU xhci trace
   into `vm\winxp-qemu-trace.<tag>.log`. No companion EHCI unless `ehci` is
   the second argument (below). No USB device is boot-attached
   (`lessons.md`, "QEMU 11.1.0-rc2 parks a Windows 98 boot in SeaBIOS's SMM
   handler"); hot-plug from the monitor on port 55559 with `device_add
   usb-mouse,id=m1,bus=xhci.0` and no `port=` (`qemu-xhci` refuses a port
   number and picks a USB2 port itself).
4. Install the driver from the transfer drive: Device Manager, the
   unrecognised "Universal Serial Bus (USB) Controller", Update Driver,
   Have Disk, `E:\`. The unsigned-driver warning on 32-bit XP is a prompt
   ("Continue Anyway"), not a blocker. The owner drove every wizard so far.
5. What to read, the same list as the Windows ME recipe above:
   `DriverEntry`, `USBPORT_GetHciMn` (expect `10000001`, the XP lineage;
   both known values are accepted since Phase 3),
   `USBPORT_RegisterUSBPortDriver status`, `StartController`, the `RH_*`
   family; then a hot-plugged mouse, `usb-storage`, and Device Manager
   disable, re-enable, remove and rescan (roadmap task 19.3, still owed).

What the first afternoon showed (2026-09-03, the owner at the console; the
`first`, `ehci` and `dss` trace tags in `vm\`):

- **The 1.0.0.1 package, xHCI only: Code 39, debug console 0 bytes.** The
  image, read on the host (`qemu-img convert -O raw`, then 7-Zip straight
  through the MBR and NTFS; `Mount-DiskImage` needs elevation and refuses a
  sparse VHD), had `xhci98.sys` in `system32\drivers`, XP's own 4,736-byte
  `usbd.sys` beside it (the INF's own `LayoutFile` row placed it), and no
  `usbport.sys` or `usbhub.sys` anywhere, `dllcache` included;
  `setupapi.log` ends `CM_PROB_DRIVER_FAILED_LOAD (0x27)`. Its earlier
  "Install failed, attempting to restore original files" is the copy-only
  pass, not the failure. XP places `usbport.sys` from
  `Driver Cache\i386\sp3.cab` only when a USB controller's install pulls
  it, and this guest never had one. The same `layout.inf` table says the
  same of Windows 2000, whose every vehicle here carried an EHCI; the
  1.0.0.2 INF has the NT path copy the file itself ("The files the OS
  supplies").
- **With a companion EHCI (`qemu-winxp-run.cmd ehci ehci`), the same
  binary ran.** `DriverEntry`, `USBPORT_GetHciMn=10000001`,
  `USBPORT_RegisterUSBPortDriver status=00000000`, `StartController`
  (`MaxPorts=8`: 4 USB2 companion ports managed and powered, 4 USB3 ports
  left unpowered, `MaxSlotsEn=0x20`), the No Op self-test matched with
  completion code 1, `RH_GetRootHubData` reporting 4 managed ports, the OS
  installing "USB Root Hub" from its cache with no prompt (XP's own
  `usbport.inf` binds `USB\ROOT_HUB20` to `usbhub.sys` and copies
  `usbhub.sys` and `usbd.sys` with it), Device Manager "working properly".
- **About thirty seconds after start with nothing attached, usbport called
  `SuspendController` and the driver halted the xHC**; a `usb-mouse`
  hot-plugged after that was invisible. That is the reading the SweetLow
  record predicted for an XP-lineage usbport without
  `DisableSelectiveSuspend` (`usbport-miniport-interface.md`, "The SweetLow
  rebuild"); the 1.0.0.1 INF's NT half deliberately omitted the value.
- **With `Services\USB\DisableSelectiveSuspend=1` set by hand** (the key
  did not exist on the stock install; the owner created it in Registry
  Editor, shut down, cold relaunch as `dss`): no `SuspendController` in two
  minutes, then the hot-plugged mouse bound: port status change on port 5,
  slot enabled, `SET_ADDRESS` intercepted, `devices addressed` 1,
  `endpoints opened` 1, "USB Human Interface Device", interrupt transfers
  submitted and completed while the pointer was driven, no refusal or
  failure counter moved. Both "USB Root Hub" entries present. This is
  roadmap task 19.2's observation, before the INF change; the 1.0.0.2 INF
  writes the value on the NT path, and the `p194` run below repeated the
  reading from a package install on the clean snapshot with no EHCI.
- **The 1.0.0.2 INF from a package install, xHCI only, on the clean
  snapshot (`p194`, later the same day, the owner driving the wizard on a
  host with no XP ISO): the driver loaded on that boot.** Have Disk from
  `E:\` finished with "installed and ready to use", no CD prompt and no
  reboot asked; `DriverEntry` through the `RH_*` family as in the EHCI run
  above, "USB Root Hub" installed by XP's own `usbport.inf` with no prompt,
  the controller and hub clean in Device Manager. `dir usb*.sys` in
  `system32\drivers` afterwards: `usbport.sys` (143,872 bytes) and
  `usbhub.sys` (59,520) carrying the SP3 cab's 14-Apr-08 stamp, XP's own
  4,736-byte `usbd.sys`, and the in-box `usbcamd.sys`, `usbcamd2.sys`,
  `usbintel.sys` and `usb8023.sys` that Setup places on every install; the
  two the 1.0.0.1 guest lacked are the two the `[Xhci.CopyNT]` rows
  brought from `Driver Cache\i386`. `Services\USB\DisableSelectiveSuspend`
  present in Registry Editor with nothing hand-set. No `SuspendController`
  in two minutes idle, then the hot-plugged mouse bound exactly as in the
  `dss` run (port 5, `SET_ADDRESS` intercepted, `devices addressed` 1,
  `endpoints opened` 1, "USB Human Interface Device", interrupt transfers
  completing while the pointer was driven), every refusal and failure
  counter at zero. This is roadmap task 19.4, the reading release 1.0.0.2
  claims; 19.1 and 19.2 closed on it.

Not done on XP: real hardware (nothing in the fleet runs it), mass storage,
a composite device, and the disable, enable, remove and rescan sequence;
roadmap Phase 19 holds each as a task.

### Windows 2000 SMP Stress VM (Phase 2d)

A third VM, separate from the 2b differential VM: Windows 2000 SP4 with two
vCPUs and a multiprocessor HAL, accelerated by WHPX (Windows Hypervisor
Platform). Roadmap Phase 2d holds the task list and checkpoint, and roadmap
Phase 2c the prerequisite migration of the existing estate onto the x86_64
binary; this section holds the environment knowledge. It is built and
checkpoint-passing. The observed configuration is recorded below, and the
evidence is in `docs/contributing/lessons.md`, "Phase 2d".

Why it exists: a uniprocessor NT kernel structurally hides SMP bugs.
`KeAcquireSpinLock` on a UP kernel only raises IRQL and never spins, and two
DISPATCH_LEVEL paths never run simultaneously. A missing interior lock between
the miniport's submit path and its `InterruptDpc` path (which usbport runs
under different locks; `docs/usb-xhci-info/usbport-miniport-abi.md` section 7)
cannot show up on Win98 (uniprocessor forever) or on the 2b VM (Standard-PC UP
HAL). Only an MP kernel with real contended spinlocks can surface it.

This VM is a race detector, not a dev-loop accelerator. Primary development
stays on Win98+NUSB (2a) and differential isolation stays on 2b. Do not expect
a speed benefit either: booting this VM's own image with only the accelerator
changed measured about 50 s to desktop under WHPX and about 52 s under plain
TCG, indistinguishable in that boot trial (`docs/contributing/lessons.md`,
"Phase 2d"). The second CPU is the entire value of this VM. TCG can boot the
installed image, but it has not been validated as Phase 2d's concurrent race
detector; the VM stays on its checkpointed WHPX rung.

Host prerequisites, one-time (roadmap Phase 2c tasks 1-2; both gates must pass
before touching any guest):

1. Windows Hypervisor Platform enabled: elevated PowerShell,
   `Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform`,
   reboot. Side effect to be aware of: the Hyper-V hypervisor now loads at
   boot, so other host virtualization software (VirtualBox etc.) runs in its
   slower Hyper-V-compatible mode. Verify: `systeminfo` reports "A hypervisor
   has been detected."
2. The WHPX-capable binary, which is already installed. In the scoop QEMU
   11.0.0 package, WHPX is compiled into `qemu-system-x86_64.exe` only
   (`-accel help` -> `tcg`, `whpx`, verified); the retired
   `qemu-system-i386.exe` that 2a/2b were originally validated on is TCG-only.
   A 32-bit Win2000 guest runs identically under the x86_64 system emulator, so
   everything moved to `qemu-system-x86_64.exe` and no new QEMU install is
   needed. The owner's decision is that everything stays on the single scoop
   install; the qemu.org Windows installer comes from the same weilnetz
   upstream, so switching would add version skew, not provenance.

   Use the
   console `.exe`, never the `...x86_64w.exe` variant: the `w` builds detach
   from the console and break the monitor/harness conventions. Verify:
   `& "$env:USERPROFILE\scoop\apps\qemu\current\qemu-system-x86_64.exe"
   -accel help` lists `whpx`.

   Roadmap Phase 2c (completed) migrated the xhciqual harnesses and the 2a/2b
   VMs onto this binary: binary swap first under TCG with guest-visible flags
   unchanged and each component's checkpoint state re-verified, then
   per-component WHPX trials from launcher copies, adopting WHPX only where
   re-verification passed. Outcome: the xhciqual passes 29/29 under WHPX
   (proven optional) but all committed launchers and gates stay on TCG, because
   Win98 2a and Win2000 2b both fail to boot usably under WHPX (see
   `docs/contributing/lessons.md`, the Phase 2c WHPX trials). The harness
   `-Qemu` defaults now resolve `qemu-system-x86_64.exe` from PATH, so the
   matrix runs with no `-Qemu` argument (the old default pointed at a
   nonexistent `C:\Program Files\qemu`).

Flag contrast with the 2b VM. The two VMs sit on opposite sides of the APIC
decision on purpose, and neither's flags may drift toward the other's:

| | 2b differential VM | 2d SMP VM |
|---|---|---|
| Binary | scoop `qemu-system-x86_64.exe` (migrated from the i386 binary in Phase 2c, TCG kept, flags unchanged) | scoop `qemu-system-x86_64.exe` (the package's only WHPX-enabled target; runs 32-bit guests identically) |
| Accelerator | TCG (installed HAL validated under TCG; the Phase 2c WHPX trial failed, 2b will not boot usably under WHPX, so it stays on TCG) | `-accel whpx,kernel-irqchip=off` (plain `-accel whpx` is not merely slow, it fails to create the partition at all on the host 2d was built on) |
| Machine | `-machine pc,acpi=off` | `-machine pc` (ACPI on) |
| CPU | `-cpu pentium3,-apic` (APIC removed) | `-cpu pentium3` (APIC present) |
| SMP | (none, 1 vCPU) | `-smp 2` (Win2000 Pro caps at 2 CPUs) |
| RAM | `-m 256` | `-m 512` (Verifier Special Pool headroom) |
| HAL Setup picks | Standard PC (uniprocessor) | ACPI Multiprocessor PC |
| Monitor port | 55556 | 55557 |
| Disk | `vm\win2k.img` | `vm\win2k-smp.img` |

The 2b VM removes the APIC because the TCG APIC clock (IDT vector 0xD1) storms
and livelocks Setup (`docs/contributing/lessons.md`, the Standard-PC HAL
entry). Every Win2000 multiprocessor HAL requires the local APIC, so 2d
restores it and uses the WHPX execution path. The storm was absent there:
Setup walked straight past "Setup is starting Windows 2000" and picked the ACPI
Multiprocessor PC HAL unaided.

The rung 2d had to run on narrows one part of the result. `kernel-irqchip=off`
means QEMU emulates the APIC in userspace, as it does under TCG. So an
in-hypervisor irqchip is not required to give this guest an APIC, and its
absence did not prevent this install from completing.

The cause of the storm remains unresolved. An installed-boot A/B was run: the
same `vm\win2k-smp.img`, host, QEMU 11.0.50 and
`-machine pc -cpu pentium3 -smp 2 -m 512`, regenerated with `-Accel tcg`.
Windows 2000 booted to the desktop under TCG with the ACPI Multiprocessor PC
HAL, APIC present and two vCPUs. That proves WHPX is not required for this
installed-boot workload on the current host and build. It does not falsify an
execution-rate explanation for the original Setup storm, because that run also
differed in workload, host, QEMU version and binary target. Do not assign the
original cause without a controlled Setup comparison.

Nor does boot success validate TCG for Phase 2d's race-detection role, though
one of the reasons to doubt it has been measured away. On this host `info
cpus` reports two distinct `thread_id`s for plain `-accel tcg` as well as for
`tcg,thread=multi`, so MTTCG is already the default and the A/B did run one
host thread per vCPU. What is still missing is concurrent driver load under
either accelerator; none has been run yet, on WHPX or TCG. Keep using the
checkpointed WHPX rung.

The check for separate host-vCPU threads: `-accel tcg,thread=single` puts both
vCPUs on one host thread (`info cpus` shows the same `thread_id` twice). QEMU
still round-robin interleaves the guest vCPUs, so some interleaving-dependent
races can fire, but simultaneous execution is gone and race coverage is weaker.
The probe did not boot Windows; because the virtual CPU topology is unchanged,
the guest-visible checks below are expected to remain satisfied, but that is an
inference rather than an observation.

Distinct `thread_id`s in `info cpus`
directly establish separate host-vCPU threads, a prerequisite for simultaneous
execution; they do not prove that the host scheduled those threads
concurrently. Record them at each checkpoint and re-check whenever the
accelerator string changes or the VM moves host. For the Phase 2d checkpoint,
on `whpx,kernel-irqchip=off`, they were 22176 and 8772, distinct.

The host process's effective affinity supplies the other prerequisite; the
machine-wide processor count alone is insufficient because Windows can give a
process a narrower inherited affinity mask. A paused, no-disk probe using QEMU
11.0.50 and the checkpoint machine/accelerator/CPU/SMP flags recorded 8 system
logical processors, QEMU process affinity `0xFF`, and 8 allowed logical
processors. This establishes capacity for overlap in that launch environment,
not actual overlap, which still needs a contended workload.

Run both host-side checks from one script, with the 2d VM running:

```
powershell -ExecutionPolicy Bypass -File scripts\check-smp-parallelism.ps1
```

`scripts/check-smp-parallelism.ps1` finds the QEMU process by its monitor port
(55557), reads the configured `-smp` count, requires a complete one-to-one
vCPU/`thread_id` mapping from `info cpus`, reads the process affinity mask, and
exits non-zero if either prerequisite fails. It is a run-time check: it needs a
live VM, so unlike the import/INF/launcher gates it cannot join
`build-driver.cmd`. Run it at each 2d checkpoint and after an accelerator
change or a host move. `-SelfTest` exercises both set-bit counting and complete
mapping validation with no VM, including shared, partial, duplicate-output and
conflicting mappings. It also verifies that the single-thread TCG hint appears
for a complete shared mapping but stays suppressed when the monitor reply is
incomplete.

Verified against live QEMU 11.0.50 probes, against the prompt-framed
implementation that shipped rather than the earlier fixed-window one:

| Probe | Result |
|---|---|
| `whpx,kernel-irqchip=off`, `-smp 2` | pass: distinct `thread_id`s `16012, 21572`, affinity `0xFF`, 8 of 8 allowed |
| `tcg,thread=multi`, `-smp 4` | pass: all four mapped, `22020, 18644, 19556, 12748` |
| `tcg,thread=single`, `-smp 2` | exit 1, full diagnostic, no spurious re-read |
| no VM running | exit 1 with a clean "start the 2d VM first" message |

`tcg,thread=single` is a host condition the guest-side topology checks cannot
distinguish. The failure names which `thread_id` was shared by which CPUs,
prints the observed mapping, and points at the accelerator string:

```
ERROR: Incomplete or shared vCPU thread mapping: vCPUs share a host thread: thread_id 3776 shared by CPU #0, #1
  observed mapping : CPU #0=3776, CPU #1=3776
  All vCPUs on one host thread is what -accel tcg,thread=single does; check the accelerator string in the launcher.
```

Prompt framing also roughly halved the run: a passing check takes about 625 ms
against about 1268 ms under the previous completeness-driven read, because it
returns when the prompt arrives instead of waiting out a window. That the
timing moved at all is the useful part: it confirms the prompt pattern matches
what QEMU emits, rather than the check merely not crashing.

`-SelfTest` asserts that wording, not just the pass/fail verdict, and checks
that the accelerator hint is emitted only after every configured vCPU has a
conflict-free mapping. That prevents an incomplete monitor reply from being
misreported as proof that all vCPUs share one host thread.

The monitor read is prompt-framed and progress-sensitive, with one retry. It
first synchronizes on the banner's final `(qemu)` prompt, sends `info cpus`,
and reads through the next final line-start prompt before parsing. A
valid-looking prefix therefore cannot hide a later unexpected CPU or
conflicting duplicate. Each received chunk resets a 3-second idle timeout,
while a 10-second hard limit still bounds a stalled or endlessly trickling
peer. These finite limits can still fail an exceptionally stalled host, but
they cannot turn a partial reply into a pass; the script reports the thread
model as unverified after the retry. The retry does not fire on a shared-thread
mapping: that reply is complete, just shared, so a genuine `thread=single`
still fails on the first read.

`-SelfTest` feeds the prompt reader a complete-looking prefix followed by a late
unexpected CPU, then exercises incomplete-then-complete and complete-shared
retry sequences through an injected reader. That covers the response boundary
and retry control flow without requiring a live monitor socket.

The `kernel-irqchip=off` rung was forced, not chosen, for a reason the ladder
never anticipated. Plain `-accel whpx` never reaches guest code on the host 2d
was built on:

```
-accel whpx: WHPX: Failed to enable nested virtualization, hr=80370302
-accel whpx: failed to initialize whpx: Invalid argument
```

That is partition creation failing, not the guest, and it reproduces with a
bare `-M pc -accel whpx -display none -m 64`, independent of `-smp` and
`-cpu`. `kernel-irqchip=off` is what makes WHPX usable here. Probe
`-accel whpx` with a throwaway 60-second launch before putting it in any
launcher on a new host; per-host WHPX behaviour has differed every time this
project has looked (`docs/contributing/lessons.md`, the WHPX trial entries).

So `whpx,kernel-irqchip=off` is the generator's default, not a retreat
position: a new host starts there, and plain `whpx` sits below it as an
opportunistic probe rather than above it as a rung. Adopting plain `whpx`
where it does initialise is legitimate, but it means redoing the install and
the MP/USB verifications on that configuration. It is not a flag to flip on an
installed system.

The one real fallback remains documented for a host where the default also
fails. If Setup hangs at "Setup is starting Windows 2000", confirm it is the
same storm by sampling EIP over the monitor (a tiny EIP range inside an
interrupt prologue), then try the MTTCG+MPS-HAL experiment on the same x86_64
binary (`-accel tcg,thread=multi`, `acpi=off`, APIC kept -> "MPS
Multiprocessor PC"), then declare the phase BLOCKED and fall back to static
review.

`scripts/setup-qemu-win2k-smp.ps1` takes `-Accel` and `-AcpiOff`, so
each rung is a regenerated launcher that records its own configuration in
comments, never a hand-edited copy. The launcher self-tests assert that a
rung's accelerator, machine flags and CPU count reach all three launchers,
because the HAL is fixed at install time and an install/run mismatch leaves an
installed system that will not boot.

Everything else is inherited from the 2b procedure unchanged (see "Windows
2000 SP4 Target VM" above): the SP4-integrated ISO and product key; the
mandatory controller-free preparation boot to place `USBD.SYS`
`5.00.2195.6658` in `C:\WINNT\system32\drivers` before any launcher attaches a
USB controller (else the `c000026c`/`0xc0000034` bugcheck with no Safe-Mode
escape); the run launcher's `usb-ehci` (loads the native usbport stack) plus
`qemu-xhci` (stays unclaimed, cancel Found New Hardware); the read-only VVFAT +
`snapshot=on` transfer disk (host to guest only, shared `vm\xfer` directory);
identical flags on install and run launchers; and a shut-down
`qemu-img snapshot -c phase2d-clean vm\win2k-smp.img` once the checkpoint
passes.

Tooling comes from `scripts/setup-qemu-win2k-smp.ps1` (cloned from
`setup-qemu-win2k.ps1`), which generates
`scripts\local\qemu-win2k-smp-{install,prepare-usbd,run}.cmd`.

Proving the SMP kernel landed, all three in the guest (a 2-vCPU QEMU command
line proves nothing about what Setup installed):

- Device Manager -> Computer reads "ACPI Multiprocessor PC" (or "MPS
  Multiprocessor PC" via the TCG fallback rung). Any "Uniprocessor" or
  "Standard PC" string means reinstall. The HAL is fixed at install time; do
  not attempt an in-place HAL swap.
- Task Manager -> Performance shows two CPU history graphs.
- `C:\WINNT\system32\ntoskrnl.exe` Properties -> Version -> Original Filename
  = `ntkrnlmp.exe`.

All three are guest-side and do not establish separate host-vCPU threads. At
every checkpoint, also run
`powershell -ExecutionPolicy Bypass -File scripts\check-smp-parallelism.ps1`
against the running VM, which records a different `thread_id` per CPU from
`info cpus` and verifies the QEMU process's affinity mask allows at least two
logical processors. Re-run it after either the accelerator string or the host
changes. The `thread=single` probe did not boot Windows, so its unchanged
guest-visible results are expected from the unchanged virtual topology rather
than recorded as observed.

Verify the `USBD.SYS` copy landed before attaching a USB controller:
`dir c:\winnt\system32\drivers\usbd.sys` should report 20,688 bytes. A copy
done through the shell that silently goes elsewhere costs a whole install-boot
cycle, and its symptom on this VM was not 2b's bugcheck but a healthy EHCI with
a yellow-banged "USB 2.0 Root Hub" under it. Do not answer that with a restart:
the restart is what produces the unescapable `c000026c`. Shut down, boot the
controller-free preparation launcher, and check the directory
(`docs/contributing/lessons.md`, "Phase 2d").

Observed state at the Phase 2d checkpoint, so later runs have a baseline: EHCI
`8086:24cd` at bus 0 device 3 IRQ 11 and xHCI `1b36:000d` at bus 0 device 4
IRQ 11 BAR0 `0xfebf0000`; "Intel(r) 82801DB/DBM USB Enhanced Host Controller"
+ "USB 2.0 Root Hub" healthy; the xHCI unclaimed under Other devices with Code
1; native `usbport.sys` 5.00.2195.6681, identical to the 2b record; VVFAT as
`E:`; snapshots `phase2d-usbd-ok` and `phase2d-clean`.

One extra device appears here that 2b structurally cannot show: an Unknown
device whose Resources tab gives the single memory range `FED00000-FED003FF`.
That is the HPET, advertised by QEMU's `pc` machine through ACPI, which
Windows 2000 predates and has no driver for. 2b hides it by running
`acpi=off`; 2d must run ACPI on to get a multiprocessor HAL. Expected and
harmless (Win2000 times off the PIT/RTC regardless), and recorded here so it
is not re-diagnosed.

How to use it, Phase 4 onward: run each phase-checkpoint driver build here
with Driver Verifier enabled (previous subsection) and, from Phase 6,
concurrent transfer load. Interpret results with the fourth differential axis
(`docs/contributing/failure-diagnosis.md`). Reproduces only here, not on 2b:
suspect a cross-CPU race (interior lock coverage, ring enqueue/dequeue state,
DPC vs submit path) before anything else. Reproduces on both Win2000 VMs but
not Win98: the ordinary 2b preemption/IRQL/native-usbport analysis applies.

### Windows 2000 ACPI VM (`vm\win2k-acpi.img`) - built, and it does NOT deliver sleep

A fourth VM: uniprocessor Windows 2000 SP4 installed with a real ACPI HAL
(`ACPI Uniprocessor PC`, chosen by Setup), built on host `fw-w11p-ykm` to make
`ResumeController` execute on Windows 2000. That is the one miniport lifecycle
callback that has never run there, because 2b runs the Standard-PC HAL with
`acpi=off` and has no D-state machinery at all.

Read this before rebuilding or reusing it: the VM works, and the goal it was
built for is unreachable on it. The full investigation is
`docs/contributing/lessons.md`, "On host `fw-w11p-ykm`". The short form:

- The HAL is not the blocker; the display adapter is, and the two available
  choices fail in opposite ways. `-vga std` leaves a yellow-banged
  `Video Controller (VGA Compatible)` with no in-box driver, so Windows offers
  no Stand by and no Hibernate at all. `-vga cirrus` does get Stand by
  offered, then vetoes it at transition time ("the device driver for the
  'Cirrus Logic 5446 Compatible Graphics Adapter' device is preventing the
  machine from entering standby"). The veto aborts at `QUERY_POWER` before any
  `SET_POWER`, so the controller never reaches D3 and no suspend/resume pair is
  produced.
- Device selective suspend is closed too, on a control: neither our controller
  nor Microsoft's own EHCI has a Power Management tab, so it is a property of
  the machine and not of our INF.
- The DSDT (at `0x0ffe0040`, 8598 bytes) defines `_S3_/_S4_/_S5_` and no
  `_S1_`, so a Stand by here would be S3, the deep state that needs the display
  driver to restore, so cirrus vetoes.
- `ResumeController` on Win2000 was therefore owed to bare metal, alongside the
  Low-Speed leg. Do not spend another session on QEMU configurations for it.
  No bare-metal Windows 2000 vehicle ever existed, so the clause is published
  as a limitation rather than pending (roadmap Phase 13).

What it is still good for: it is a clean ACPI-HAL Win2000 target, and the
driver starts on it correctly (4 managed ports, both port-map passes agreeing,
No Op self-test matched, all failure counters zero). It also produced the first
`cb SuspendController` ever seen on Windows 2000, on an ordinary shutdown, in
the Win98 `Suspend -> DisableInterrupts -> Stop` shape.

The working machine definition (monitor port 55558, chosen so it can run
alongside 2a/55555, 2b/55556 and 2d/55557):

```
-machine pc                      ACPI ON - the whole point
-accel whpx,kernel-irqchip=off   TCG storms this guest on 11.0.0; WHPX does not
-cpu pentium3                    local APIC PRESENT (masking it hangs Setup)
-m 256
-vga std                         (see above - cirrus is the alternative, both blocked)
-drive file=vm\win2k-acpi.img,format=qcow2,if=ide
-monitor tcp:127.0.0.1:55558,server=on,wait=off
```

Snapshots on the image: `text-setup-done` (pre-GUI Setup),
`acpi-installed-clean` (working install, pre-USBD.SYS), `post-prep-boot`
(pre-driver-install).

Launchers are hand-written in `scripts\local\` (gitignored but inside the
OneDrive tree, so they sync between hosts). The repo convention is that
launchers are generated by `scripts\setup-qemu-*.ps1`; a
`setup-qemu-win2k-acpi.ps1` was never written because the VM did not earn
further use.

| File | Purpose |
|---|---|
| `qemu-win2k-acpi-install.cmd` | ACPI + APIC masked off: refuted (Setup hangs in the kernel idle loop), kept for the record |
| `qemu-win2k-acpi-apic-install.cmd` | ACPI + APIC under TCG: refuted (the vector-0xD1 storm), kept for the record |
| `qemu-win2k-acpi-whpx-install.cmd` | The working installer |
| `qemu-win2k-acpi-whpx-continue.cmd` | Resumes a part-installed image (`-boot c`, CD attached) |
| `qemu-win2k-acpi-prepare-usbd.cmd` | The no-USB-controller prep boot (see the trap below) |
| `qemu-win2k-acpi-run.cmd` | EHCI + xHCI + debugcon + trace: the test VM |
| `qemu-win2k-acpi-cirrus-run.cmd` | Same, with `-vga cirrus`: the standby experiment |

Two host-specific traps in those launchers, both of which cost time:

- The QEMU path is hard-coded. It happens to be identical on `minis-w11p-ykm`
  and `fw-w11p-ykm`, so it has not bitten yet, but it is the standing trap
  when moving hosts.
- The ISO path differs per host: `D:\isos\win2ksp4.ISO` on `minis-w11p-ykm`,
  `D:\isos\win2ksp4-retail.ISO` on `fw-w11p-ykm`.
  `qemu-win2k-acpi-whpx-continue.cmd` probes a candidate list instead of
  hard-coding one; do the same in any new launcher.

The trap that would cost the VM: `usbhub20.sys` imports `USBD.SYS`, Windows
2000 never places that file itself on an xHCI-only machine, and attaching EHCI
before it exists makes the next boot bugcheck `c000026c` / `0xc0000034`
(Phase 2b paid for this once already). So the installer carries no USB
controller at all, and the bring-up order is:

1. Prep boot: `qemu-win2k-acpi-prepare-usbd.cmd` (no USB controller). Copy
   `USBD.SYS` from the VVFAT disk (volume `QEMU VVFAT`, any drive letter) to
   `C:\WINNT\system32\drivers\USBD.SYS`. Shut down cleanly.
2. Install boot: `qemu-win2k-acpi-run.cmd`. Device Manager -> the
   unrecognised USB controller (`PCI\CC_0C0330`, Code 1) -> Update Driver ->
   Have Disk -> `xhci98.inf` from `out\pkg-qemu\` (staged on the VVFAT share
   at `vm\xfer\pkg\`). Accept the unsigned-driver warning. Confirm
   `DriverEntry (built ...)` and a clean start in `vm\win2kacpi-debugcon.log`.
   That confirmation reads the port-`0xE9` log, and only the `qemu` flavour
   writes to it: a `debug` install here starts fine and leaves the log empty,
   which is indistinguishable from a driver that never reached `DriverEntry`.
3. Verify the HAL: Device Manager -> Computer must read an ACPI HAL. If it
   reads "Standard PC", Setup did not choose the ACPI HAL and the install has
   to be redone. The HAL cannot be swapped afterwards: doing so on an installed
   image bugchecks `STOP 0x7B` / `0xC0000034`, because an ACPI HAL
   re-enumerates the disk controller under the ACPI namespace and a non-ACPI
   install has no boot-start ACPI enumerator.

### QEMU monitor helpers (`scripts\local\`)

All are host-agnostic and are candidates for promotion into `scripts\` proper.
The launchers beside them are not: they hard-code the host's QEMU path and
media paths, so a repo synced across machines needs them regenerated
(`scripts\setup-qemu*.ps1 -QemuBinDir`) or the one path line patched. Observed
paths so far: `C:\Program Files\qemu` on this development host, a scoop prefix
on `minis-w11p-ykm` and `FW-W11P-YKM`.

- `qmon.ps1`: `-Port <n> -Command "<monitor command>"`. Prompt-framed read
  with an idle timeout and a hard limit, and it strips QEMU's
  character-by-character echo. This is how `info registers`, `info qtree`,
  `pmemsave`, `device_add` and `screendump` are issued in an automated loop.

  Always go through the script file; never inline the socket code on a
  command line. `New-Object System.Net.Sockets.TcpClient` followed by a
  `$stream.Write` of an ASCII command is the textbook reverse-shell shape, and
  Microsoft Defender scores it as `Win32/ClickFix.CCJ!MTB` when it appears as
  a `powershell.exe -Command` argument. The failure mode is not a warning: the
  process spawn itself is blocked, so the caller sees
  `EPERM: operation not permitted, uv_spawn ... powershell.exe` and nothing at
  all about Defender, which reads like a broken shell rather than a blocked
  one.

  Observed on this development host during a 2b run; the identical logic
  in `qmon.ps1` / `readcounters.ps1` / `hub7bv0.ps1` runs unimpeded, because a
  `-File` invocation of a script on disk is not the pattern being matched. If
  a new monitor interaction is needed, add a parameter to one of those scripts
  rather than pasting a socket into a shell.
- `ppm2png.ps1`: `-In <ppm> -Out <png>`. QEMU's `screendump` writes a binary
  PPM whatever extension you give it, which no image viewer in this toolchain
  reads.
- `offsets.c` + `readcounters.ps1`: read the miniport extension's counters
  out of a live guest, by offset, over `x/Nwx`. See the next section for why
  the trace alone stops being enough from Phase 7a onwards.
- `cycle7av.ps1`: N unplug/replug cycles with the monitor traffic keep-alive
  described under "QEMU monitor - hot-plug USB devices". `pump.ps1` holds HID
  traffic for a fixed window so a GUI-driven abort or reset lands on an
  endpoint that really has a read in flight. `replug.ps1` is the older,
  keep-alive-free version and is kept only for guests that do not idle-suspend.
- `counters.py`: last value of every counter in a debugcon log. Subject to
  the print-budget limit below, so prefer `readcounters.ps1` for anything a
  HID device touches.

### Reading counters out of a live guest (Phase 7a onwards)

`XHCI_DBG_VALUE_CHANGED` caps each site at `XHCI_DBG_VALUE_LIMIT` (32) prints.
That was sized for enumeration-only phases; continuous HID traffic exhausts it
within seconds, after which the trace is silent about the values a run exists
to read. Half an hour was lost reading that silence as "no traffic", and a
full ten-cycle run showed `isr count` frozen at 41 while the guest was
demonstrably busy.

Read the fields themselves instead. The tracked tools are the vm-matrix ones
(`scripts\vm-matrix\gen-offsets.ps1` and `lib\counters.ps1`, driven by
`run-matrix.ps1`; see "The automated VM device matrix (Phase 10)" below, which
is the procedure a fresh clone can follow):

```
powershell -File scripts\vm-matrix\gen-offsets.ps1
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -ValidateOnly
```

`scripts\local\regen-offsets.cmd` and `scripts\local\readcounters.ps1` were
the originals and the vm-matrix pair superseded them. They are host-local:
`scripts\local\` is git-ignored per-host tooling and a fresh clone does not
have them, so nothing committed may depend on them. The notes below are about
the offset table itself and apply to either.

- The extension VA is the `a=` field of any `cb <name>` trace line.
- Cross-check `offsets.txt`'s `SIZEOF` against the trace's
  `MiniPortExtensionSize`. Equality is the only thing that says the host
  layout is the deployed one. This is not theoretical: at one point
  `offsets.c` did not compile at all (it named `EventsConsumed`, which is
  `EventsTotal`), so the committed `offsets.txt` was a stale layout from
  before batch 7a-V added two fields, and every reading taken through it would
  have been silently shifted.
- Regenerate `offsets.txt` after any change to `XHCI_EXTENSION`, not just
  after a rebuild.
- Use the script, not a hand-typed `cl`. Both reasons were measured when
  batch 8-A grew the extension by 1,928 bytes:
  - `cl.exe` needs `Common\MSDev98\Bin` on `PATH` as well as `VC98\BIN`; that
    is where `MSPDB60.DLL` lives. Without it `cl` exits `0xC0000135`
    (`STATUS_DLL_NOT_FOUND`) having printed nothing at all: no diagnostic, no
    `.obj`, no `.exe`. `test\run-host-tests.cmd` sets both directories, which
    is why the host suite builds while a bare command line did not.
  - A failed compile leaves the previous `offsets.exe` in place, and running
    it rewrites `offsets.txt` with numbers that look entirely plausible and are
    for the wrong layout. That is the stale-offset hazard reached through the
    regeneration meant to prevent it: the counters had shifted by 1,920-1,928
    bytes and `SIZEOF` still read the old 47,848.
  - So the script must, in this order: delete `offsets.exe`, verify it is
    gone, compile, verify the `.exe` now exists, run it into a temporary file,
    check that file contains a `SIZEOF` line, and only then move it over
    `offsets.txt`. Two of those steps look redundant and are not. The delete
    has to be checked as well as attempted: a locked or read-only file makes
    `del` fail, after which "the exe exists" is satisfied by the stale one and
    the silent-compiler hazard is reproduced. And the output must go to a
    temporary file, because `> offsets.txt` truncates it the instant the
    command starts; a generator that then crashes leaves an empty table behind
    while the script reports that nothing was regenerated, destroying the last
    trustworthy reading on the failure path. Any step failing is fatal and
    must leave the existing `offsets.txt` untouched.
  - A layout change does not shift every field by the same amount. Whether a
    given counter moved depends on where the new fields were inserted relative
    to it: batch 8-A moved everything before its two new counters by 1,920
    bytes and everything after them by 1,928. Never adjust a stale reading by
    a delta; regenerate the table and read the offset. The arithmetic behind
    those two numbers recurs: there are 160 `XHCI_TRANSFER_QUEUE`s in an
    extension (`Ep0Queue` plus four endpoint records, times 32 slots), so one
    `ULONG` added to the queue costs 640 bytes before padding. Batch 8-A's
    three queue ULONGs were the 1,920, its two controller counters the extra
    8, and batch 8-V's one per-queue `MidTd` counter took the extension from
    49,776 to 50,420, i.e. 644.
  - Pinning "the same binary on both targets" without a hash. Windows 98
    reports 0 for the driver's `image size`, so the binary's own size cannot
    corroborate identity across targets. What can is three independently
    derived structure sizes agreeing exactly on both: the registration
    `packet size` (`0x13C`), `MiniPortExtensionSize` and
    `MiniPortResourcesSize` as the trace prints them (one Phase 10 run read
    `0x113D0` and `0x64000` for the build it ran). That pins the source
    revision and is written as strong evidence rather than a byte-identical
    proof.
  - Regenerating proves the layout is current; it does not prove the table is
    complete. The tracked generator derives its row set from the driver's
    `XHCI_DBG_VALUE_CHANGED("<label>", ext-><Field>)` sites, so a counter with
    no such site, or one whose label is wrapped across two string literals, or
    whose subscript is a macro call, is simply absent from `offsets.txt`, with
    a correct `SIZEOF`, correct offsets for every row that is present, and no
    diagnostic of any kind. A hand-kept roster had the same failure one step
    earlier, measured: batch 8-A added `TransfersRefusedRingFull` and
    `EndpointRetriesAsked` for its backpressure latch, the question asked of
    that table was whether its values were stale, and nobody checked its row
    set, so the two counters batch 8-V's headline clause reads were missing
    from it until the run was already under way. The check is per
    run: before booting, confirm every counter this run's clauses name is a
    row in `offsets.txt`. When adding a counter, give it a print site the
    generator can match (a single-literal label and either a scalar or dotted
    field or an array element subscripted by one bare constant name), then
    regenerate and confirm the row is there. The generator refuses a
    regeneration that loses a row, which catches a site that stopped matching
    but not one that never existed. A row set assembled path by path is only
    as complete as whoever assembled it.

#### Which channel is the mechanism, and how it was decided

The tension was that `scripts\local\` is gitignored, so `offsets.c` and
`readcounters.ps1` are in no commit and exist only on whichever host last
edited them, while committed source and committed docs named them as the
mechanism for reading counters out of a run. A git-only clone had the
procedure and not the tooling, and neither works on the bare-metal targets
Phase 13 requires, where there is no QEMU monitor at all.

Decided: the traced build's `CheckController` block is the mechanism, and the
monitor reader is a per-host precision tool. An audit found 77 extension
counters incremented and printed in neither flavour, so the reader was carrying
them by default rather than by choice. Batch 7b gave every one of them a
change-gated site in `xhciCheckController` (`src/xhci_dispatch.c`), the same
mechanism the roughly 100 counters already there use. Nothing is readable only
through the reader.

Two qualifications on which build carries that block. Every `XHCI_DBG_*` site,
which is all of `xhciCheckController`'s printing, compiles only into the `qemu`
flavour; the shipping `debug` build prints nothing as it runs. And per-line
`DbgPrint` under DebugView bugchecks Win98 metal across three device classes
(roadmap task 12.2), so that route is banned on the machine it would have
served. On bare metal the counter route is `XHCISNAP`, shipping in every
flavour since `0.0.0.6`, and the snapshot `.BIN` decodes against
`offsets.txt` exactly as the live reader's dump does. The decision itself,
counters over a per-host script, is unchanged; only the channel that carries
it differs by vehicle.

So `scripts\local\` keeps the repo's standing policy, host-local tooling stays
out of history, and the two channels have distinct jobs:

| Question | Channel |
|---|---|
| Did this family move at all, in a VM | the `CheckController` block in a `qemu` build |
| Did this family move at all, on bare metal | `XHCISNAP -dump`: the `.BIN` decoded against `offsets.txt`, or the `.TXT` for the note ring. There is no live channel on Win98 metal |
| The exact value during continuous traffic, after a site's 32 prints are spent | `readcounters.ps1` against a live QEMU guest |
| A per-row value that is a number rather than an event (the topology pair table, the device records) | `readcounters.ps1` or an `XHCISNAP` `.BIN`; a table cannot be spent through 32 lines |

The remaining obligation is a regeneration, not a rebuild: `offsets.c` must be
re-run after any change to `XHCI_EXTENSION`, and `offsets.txt`'s `SIZEOF`
cross-checked against the trace's `MiniPortExtensionSize` before any reading is
trusted.

Note `pmemsave` (physical) versus `memsave` (virtual): reading ACPI tables or
anything else by physical address needs the former; the latter fails with
`Invalid addr` on an address that is perfectly valid physically.

### Driving the monitor from the host: two traps that do not look like traps

Drive the QEMU monitor from a `.ps1` file, never from an inline
`powershell -Command`. Microsoft Defender classifies an inline command line
that builds a `System.Net.Sockets.TcpClient`, connects and pumps bytes as
`Trojan:Win32/ClickFix`, the canonical shape of a reverse shell, and blocks the
process from starting. What the harness reports is
`EPERM: uv_spawn powershell.exe`, which reads as a transient glitch and is not
one. The detection resource is a `CmdLine:`, so nothing on disk is quarantined
and no file is altered; `Get-MpThreat` / `Get-MpThreatDetection` show the
actual record.

The same code inside a script file runs normally, so
`scripts\local\netattach-8v2.ps1`, `unplug-8v.ps1`, `readcounters.ps1` and
`hub7bv.ps1` have never tripped it while ad-hoc one-liners have, twice, in two
different batches. Do not respond by weakening the scanner: the file-based
form is the project's existing pattern and needs no exclusion.

Any host-side reader of a file QEMU still holds open must pass
`FileShare::ReadWrite`. This is already stated for the disk image and applies
equally to the trace and debug-console logs. `[IO.File]::ReadLines` and
`Get-Content` without the share mode fail with "being used by another
process"; open explicitly instead:

```powershell
$fs = [IO.File]::Open($path,'Open','Read','ReadWrite')
$rd = New-Object IO.StreamReader($fs)
```

Two more, found while building the device-matrix probe, which are about what
the monitor sends back rather than how it is reached:

- The HMP monitor echoes every prefix of a command on one line. Once the
  cursor escapes are stripped, `info usb` comes back as
  `iininfinfoinfo info uinfo usinfo usb`. A reply filter has to drop lines that
  end with the command, not lines that equal it; before the fix, 3 KB of echo
  had gone into the probe's report column.
- QEMU cascades devices onto a hub you attached yourself once the root ports
  run out. Attaching the whole device population at once exhausted the 15 USB
  2.0 root ports of the default `qemu-xhci`, and the remainder appeared at
  `Port 6.1` and `Port 6.2`, behind the `usb-hub` the same probe had just
  attached. A behind-a-Full-Speed-hub speed would then be recorded as a
  root-port speed, silently, for whichever models sorted last. The fix is one
  device on the bus at a time, deleted and waited for, with a non-root port as
  its own reported outcome; raising the port count until it happens not to
  bite is not a fix.

### Option C: Real Hardware

For real hardware testing, the machine needs:

- An xHCI-only USB chipset (most machines from about 2012 onward; Intel
  7-series and later usually).
- One of the two targets installed, Win98 SE or Windows 2000 SP4. A dual-boot
  machine can serve both, and Phase 13's checkpoint allows one machine to
  satisfy both.
  - Win98 SE may require slipstream drivers for SATA/AHCI if the storage
    controller isn't detected; set the BIOS to IDE/Compatibility mode where
    available.
  - Win2000 SP4 has the same AHCI problem and no F6 floppy on most modern
    machines, so IDE/Compatibility mode is usually the practical answer there
    too. Integrate SP4 into the install media (or install SP4 immediately) so
    the native usbport stack is present. Measured, it is worse than an AHCI
    problem: Windows 2000 Setup bugchecks during installation on the ThinkPad
    E460 (Skylake, 2016) and the ThinkPad P14s Gen 1 (Comet Lake, 2020). The
    cause was not investigated on either. Both are xHCI-only Skylake-and-later
    laptops, which leaves "no EHCI" and "modern storage path" as uncontrolled
    variables that have not been excluded, so do not write either of them, or
    an era gap, into the record as the cause. Two machines is where this
    project stopped, by the stop rule the Windows 2000 batch carried, and
    there is no further candidate: a new attempt is a machine acquisition and
    a different kind of decision. Treat a Windows 2000 install on a machine of
    this class as expected-unavailable rather than proven-impossible. What was
    never captured is the bugcheck code, its parameters and the stage Setup
    reached, on either machine. Photograph the screen before rebooting if
    there is ever another attempt.
- A way to transfer the driver: burn to CD, pull the disk and write it on the
  modern host, or use a secondary machine with USB 2.0 for file transfer
  before the driver works. A USB-to-serial adapter is no help here: it needs a
  working USB stack to carry files to a machine that has no working USB
  stack, and no candidate machine has a serial port at the other end either.
  This driver has no serial channel on metal, in either direction; see
  "Getting a trace off a bare-metal machine", which also scopes that statement
  (QEMU's virtual COM1 is still how the Phase 0 DOS qualifier reports out of a
  headless guest).

Phase 13 wanted at least one real machine confirmed per target OS. The
bare-metal risks (BIOS handoff, INTx routing, XUSB2PR) are shared, but the two
stacks above the miniport are not the same code, so a bare-metal pass on one
target is not evidence about the other. That clause is half-met and was closed
that way on the phase's published-limitation branch: Windows 98 SE on the
E460, and no Windows 2000 machine at all. See "Per-target-OS coverage" below
for the table. The rule outlives the closure: nothing measured on Windows 98
metal says anything about Windows 2000.

What the first bare-metal run reached, and why only an xHCI-only machine could
have taken it (batch 7b-M, E460, Windows 98 SE). The driver bound on
`8086:9D2F`; High-, Full- and Low-Speed devices worked on root ports; three
hubs formed one daisy-chained tree; and a mouse plus a Low-Speed keyboard
worked behind a real High-Speed hub. So real split transactions ran through a
real transaction translator, which no VM can present.

That proves the split
transactions work, not that every TT/`MTT`/`TTT` field was programmed as
intended; the numbers are a different claim and were never read. The run could
take the Win98 half only, because that is the only target installed on either
fleet machine. A machine with EHCI of its own would not substitute even with
Windows 2000 on it: a hub tree there demonstrates less, and it is not the
deployment population.

Recommended approach: develop and iterate in the VM; test on real hardware
after VM testing confirms basic operation.

### Bootstrapping xHCI-only machines (no EHCI)

The xHCI-only fleet machines (the E460 and the P14s; the B650M was one of
them and has since left the project, see "Available Test Hardware") have a
chicken-and-egg problem: until `xhci98.sys` works there is no Windows-side USB
at all. Two facts make them workable anyway.

Note what the distinction is. The supported difference between an easy
bootstrap machine and these is EHCI, not input. Whether the ThinkPads have a
PS/2 path was never measured (their internal keyboards are, if anything,
likely i8042-attached through the EC, as laptop keyboards of this era commonly
are, but that is unverified either way), and the B650M's owner reports it has
a PS/2 port. If input on a specific machine matters for a specific run,
measure it on that machine.

"xHCI-only" is a bootstrap description, not a requirement. No clause of Phase
13 ever needed the absence of EHCI; they need a real xHCI controller with a
target OS on top of it, and the one inherited clause that looked otherwise
("at least one real hardware machine confirmed working per target OS") says
per target OS, not per machine class. The phrase entered the record because
these machines have no Windows-side USB until this driver runs, and was then
read back as a requirement it never was. A machine with EHCI would have been
the easier vehicle for a Windows 2000 install (working USB, and so a
file-transfer path, before `xhci98.sys` does anything); none was available to
this project under that OS.

- BIOS legacy-USB (SMM) keyboard/mouse emulation usually keeps working under
  Win9x until a driver claims the controller. The firmware emulates an 8042
  keyboard through SMM, which survives into protected mode as long as no OS
  driver performs the xHCI BIOS handoff. So a USB keyboard typically works for
  the Win98 install and on the desktop before `xhci98.sys` is bound, and dies
  the moment the driver's handoff runs. Verify per machine (it is
  firmware-specific), keep "Legacy USB Support" enabled in BIOS, and expect to
  lose input if the driver initializes but then fails: plan every test session
  so a hard reboot is an acceptable recovery.
- The Win98 Startup Menu and Safe Mode keep working input. The boot menu runs
  in real mode (BIOS emulation fully active), and Safe Mode does not load PnP
  drivers, so `xhci98.sys` never runs its handoff there. The `BootMenu=1`
  recovery path from "Recovering from a driver that crashes at boot" works on
  these machines too.

The Windows 2000 half of this is hypothetical, and the paragraph below is kept
for the reasoning rather than for the plan. Windows 2000 SP4 Setup bugchecks
during installation on both the E460 and the P14s Gen 1 (project owner), so
there is no Windows 2000 install to bootstrap on either ThinkPad, and the
B650M is no longer available. Two machines, two Intel generations, one
outcome, and no cause established on either; do not upgrade it to an era gap,
which is an inference the evidence invites and does not support. No Windows
2000-on-metal candidate remains, so see Phase 13's checkpoint in `roadmap.md`.
Everything above still stands for a Windows 98 install on these machines,
which is what they run.

Both facts would carry over to a Win2000 SP4 install on the same machines,
with one substitution: its recovery path is the F8 menu (Last Known Good, then
Safe Mode), and the F8 menu likewise runs before any driver claims the
controller, so SMM keyboard emulation is still alive there. The Recovery
Console rung matters more on these machines than in a VM: it is the only rung
that still works after a fault severe enough to bugcheck Safe Mode, and unlike
the VM there is no snapshot to fall back to. Boot it from the SP4 CD, or
pre-install it with `winnt32 /cmdcons` while USB still works, so a bad build
never costs a reinstall. Test-boot F8 -> Safe Mode once per machine before the
first driver install and confirm the keyboard responds there; firmware
legacy-USB behaviour is per-machine, and finding out afterwards is expensive.

File transfer without working USB: pull the SATA disk and stage files with a
USB-SATA adapter on the modern host (or in another retro machine), or burn a
CD where the machine has an optical drive. Pre-stage generously before each
on-site session: NUSB, several driver builds, the INF, and any tools. Every
forgotten file is another disk swap. Per-machine BIOS/CSM/storage setup
records for this fleet live in the maintainer's `retro-configs` repository.

Installing NUSB on these machines does place the usbport stack; no EHCI is
needed. NUSB's setup INF (`_NUSB.INF` `[DefaultInstall]`) copies
`usbport.sys`/`usbhub20.sys`/`usbehci.sys` into `SYSTEM32\DRIVERS`
unconditionally at install time, independent of any EHCI device. Confirmed on
real xHCI-only hardware (an earlier claim to the contrary was a
wrong-directory false negative; see `docs/contributing/lessons.md`). So on a
full-NUSB baseline the stack is present and `xhci98.inf` (binding
`PCI\CC_0C0330`) can rely on it.

Bundling `usbport.sys` + `usbhub20.sys` in `xhci98.inf`'s own `CopyFiles`
would be an optional, defensive measure for a non-NUSB host, not a hard
requirement, and the decision is not to. The media already carries a
per-target `usbd.sys` for a reason the gate checks by hash, and adding two more
third-party binaries under the same `CopyFiles` would double that surface for
a host this project has never been asked to support: a machine with an
xHCI-only chipset, no NUSB, and Windows 98. Installing NUSB is the documented
prerequisite. If a real such host ever turns up, this is the option to reach
for; it is written down here for that, not as an open task.

What NUSB 3.3 does not ship, and the composite-device gap it leaves. The
archive's complete driver list is `USBPORT.SYS`, `USBHUB20.SYS`, `USBEHCI.SYS`,
`USBSTOR.SYS`, `USBAUTH.SYS`, `USBNTMAP.SYS`, `USBU2A.SYS`, `NTMAP.SYS` and the
1394 set (`1394BUS.SYS`, `OHCI1394.SYS`, `SBP2PORT.SYS`). There is no composite
generic-parent driver, and no `usbd.sys` either (so `xhci98.inf`
has the OS supply that one through `LayoutFile`).

Measured on the E460: a multi-interface HID device came up as `USB Composite
Device` with Code 2 ("the NTKERN.VXD device loader(s) for this device could not
load the device driver"), on a root port as well as behind a hub. That pair of
readings excludes a hub-specific path defect and nothing more; bench session 1
later had two devices failing identically in both places for a reason inside
this driver (`test-equipment.md`, "USB Audio", the `bMaxPacketSize0` table). What placed this
particular fault in the stack is the absent `usbhub.sys`: the remedy on the
E460, the X61 control, and the guest reverse-rename reproduction below.
Single-interface HID devices bind and work normally, including behind hubs.

The remedy was not a Windows 2000 file. Windows 98 SE's own `usbhub.sys` is
that OS's composite parent, and Windows 98 places it only when Setup detects a
USB controller it recognises, so an xHCI-only machine never receives it and
the devnode reads `Code 2`: matched, and the file was not there. The install
now has Windows supply it on the Windows 98 path, the way it gets `usbd.sys`
(release 1.0.0.1; the 1.0.0.0 media carried both under other names).

Task 13-E.1 closed on that remedy without an SE CD install ever being run;
`run-13e.md`'s "Finding 1 - RESOLVED" has the reading, including the reverse
rename that reproduced `Code 2` on the 2a guest. The method that found it was
derivation from the binaries, the way `usbd.sys` was handled, rather than
copying a file and seeing whether the bang goes away: the `usbd.sys` precedent
shows a Win2000-derived file can work here, and also shows the wrong build
loads rather than failing.

One QEMU observation is worth keeping beside that. USB Audio devices are
essentially always composite (an audio-control interface plus one or more
streaming interfaces), and QEMU can surface one: `usb-audio` has an
AudioControl interface plus one or more AudioStreaming interfaces, and
`multi=on` adds two more streaming alternates. On the 2a VM it binds with no
yellow bang (`USB Composite Device` and `USB Audio Device` both appear) and
`xhci98.sys` carried a complete isochronous transfer for it.

But that guest is
not a plain NUSB install: the same session had attached the Windows 98 SE CD
and installed Win98's native USB stack to satisfy `uhcd.sys` for an audio
control controller (the Device Manager tree shows `Intel 82371SB PCI to USB
Universal Host Controller` beside the bound audio device), and every
successful composite bind observed there happened after that install. No
composite device has ever been seen binding on NUSB alone, in any batch,
because every other QEMU device model is single-interface. That confound is
why the missing file could not be isolated from this guest: the whole stack
went in at once.

### Recommended USB Host Chips for Comprehensive Testing

QEMU cannot exercise the quirk-handling code (see "QEMU coverage limits"), so
comprehensive coverage means putting the driver in front of physical
controllers. The deployment target for this project is xHCI-only machines,
especially laptops: the whole reason the driver exists is that those have no
EHCI/OHCI/UHCI fallback, so Windows itself has no USB until this driver runs.
(Whether they also lack PS/2 is unmeasured on the fleet and was false for the
B650M; "no EHCI" is the part that is both true and sufficient.) That target
drives which controllers matter.

Modern laptops do not use discrete USB host chips. A laptop's xHCI is always
integrated into the chipset or SoC: Intel (PCI vendor `8086`) or AMD (PCI
vendor `1022`). The standalone controllers (NEC/Renesas, ASMedia, Fresco
Logic, VIA, Etron) were popular as USB 3.0 add-in cards and as secondary
controllers on desktop motherboards in the 2010-2015 era, but they never
appear as the primary controller in a laptop. For this project they are a
desktop-bench convenience for hitting specific quirks, not deployment targets,
so they belong at the bottom of the list.

#### The Win98-bootable window bounds the realistic laptop set

A laptop must be able to boot Win98 at all before its xHCI matters:

- Legacy BIOS / CSM is required to boot Win98. Widely removed from about 2020
  onward, so Meteor Lake and similar bleeding-edge laptops are out regardless
  of their USB.
- SATA storage: Win98 has no NVMe support, and many post-2018 laptops are
  NVMe-only.
- RAM cap: Win98 needs `MaxFileCache`/memory-limit workarounds above about
  512 MB to 1 GB.

This brackets the realistic target to roughly 2012-2018 Intel laptops, which
keeps "comprehensive" small and mostly single-vendor.

The bracket is a buying heuristic, not a bound, and the fleet contains a
counterexample. Windows 98 SE is installed on the ThinkPad P14s Gen 1 (Comet
Lake, 2020; project owner), which is outside the window on date. So read the
three bullets above as the properties that decide it (CSM, a non-NVMe storage
path, and a RAM workaround) and check them per machine rather than checking
the year. What the counterexample does not do is widen the xHCI coverage
argument: two clean Intel generations were already covered, and a third clean
one adds confirmation rather than reach.

#### Tier 1 - laptop deployment targets (buy these first)

| Controller | How to get it | Why it matters | Maps to phase |
|---|---|---|---|
| Intel 7/8-series (Panther Point / Lynx Point, Ivy Bridge / Haswell, ~2012-2014) | An era laptop of that generation | First widespread integrated xHCI; the quirky Intel generation: BEI mishandling (and compliance-mode lockup, relevant only to confirm USB3 ports stay unpowered), plus the `XUSB2PR` port mux. No machine of this class is in the fleet and none has ever been tested, so everything this repository says about the mux is read from datasheets rather than measured. | Phase 7-8 |
| Intel 100/200-series (Skylake / Kaby Lake, ~2015-2017) | Era laptop (e.g. ThinkPad T460/T470, X260/X270) | The cleaner modern Intel baseline; PME-stuck and halt-in-flight quirks. Represents what most surviving xHCI-only laptops run. | Phase 7-8 |

Two Intel laptops, one quirky generation and one clean, cover the overwhelming
majority of the real deployment population. Mobile parts use `-LP`/`-M` PCI
IDs that differ from the desktop IDs; cross-reference the Linux `xhci-pci.c`
table for the exact mobile IDs.

#### Tier 2 - second-vendor and add-in coverage (optional)

| Controller | How to get it | Why it matters | Maps to phase |
|---|---|---|---|
| AMD mobile (Kaveri / Carrizo APU `1022:7814`, or Ryzen mobile) | Era AMD laptop | Second integrated vendor; PLL re-lock on power events and isoch scheduling quirks. AMD's USB IP is partly ASMedia-derived. Less common in the retro scene and harder to boot Win98 on. | Phase 13 |
| NEC uPD720200 (`1033:0194`) + Renesas uPD720201/202 (`1912:0014`/`0015`) | PCIe add-in card (desktop bench) | ROM-less 720201/202 cards are the only test vehicle for the driver firmware-upload path; the 720200 boots from on-card SPI flash (no upload) and covers plain NEC-vendor behavior. Not found in laptops. | Phase 6-8 |
| ASMedia ASM1142/ASM2142 (clean) + ASM1042 (`1B21:1042`, 64 KB bulk limit) | PCIe add-in card (desktop bench) | Clean baseline plus the bulk-chunking quirk. ASMedia behavior also surfaces indirectly under AMD integrated USB. | Phase 3-8 |

#### Tier 3 - quirk completeness on a desktop bench (only if chasing specific bugs)

| Controller | How to get it | Why it matters | Maps to phase |
|---|---|---|---|
| Fresco Logic FL1000 (`1D5C:1000`) | PCIe add-in card | Spurious success; validates residual-length computation. | Phase 7-8 |
| VIA Labs VL805/VL806 (`2109:0812` / `2109:0813`) | PCIe add-in card | Common, generally clean; VL800 early revisions share the spurious-success bug. | Phase 7-8 |
| Etron EJ168 (`1B6F:7023`) | PCIe add-in card | Known-flaky budget controller; confirms graceful degradation rather than hang. | Phase 13 (stability) |

Practical notes:

- For the laptop target, prioritize two Intel laptops (one 7/8-series, one
  100/200-series). That is the bulk of "comprehensive" for this project.
- The discrete add-in cards (Tier 2-3) only make sense on a desktop test bench
  with free PCIe slots, and only to exercise quirks your laptops will never
  trigger (NEC firmware upload, FL1000/VL800 spurious success, ASM1042 64 KB
  bulk). Skip them unless you are specifically validating that code path.
- A laptop with Thunderbolt/USB4 exposes an extra xHCI for USB tunneling
  alongside the native PCH xHCI; that path is more complex and out of scope
  (and such laptops usually cannot boot Win98 anyway).
- When buying NEC/Renesas cards, check the chip marking and whether an SPI
  flash chip is fitted: the uPD720200 always boots from on-card flash (no
  driver-upload path), while ROM-less uPD720201/202 cards are the ones that
  exercise the driver firmware upload (Linux `xhci-pci-renesas.c` is the only
  open implementation of it, and this driver has none).

### Available Test Hardware

The current physical test fleet, and what each machine is for. The fleet is
two machines: the E460 and the P14s Gen 1, two Intel xHCI-only laptops,
Windows 98 only, single-controller. A third, an AMD desktop, left the project;
its row is kept because clauses lost their vehicle with it.

The key axis is whether the platform still has an EHCI controller. Intel
removed EHCI starting with Skylake (100-series), and modern AMD is xHCI-only,
so on these machines a fresh Win98 install has no Windows-side USB stack until
`xhci98.sys` works. No machine in this fleet is an exception, so the
bootstrap section is not optional reading. Two precision notes, both of which
have caused a wrong claim to be written elsewhere: whether these machines
have PS/2 was never measured (and was false for the B650M); and "no
Windows-side USB stack" is not the same as "no USB works", because BIOS
legacy-USB SMM emulation typically keeps a USB keyboard and mouse alive until
a driver claims the controller, which is how these machines get installed at
all. See "Bootstrapping xHCI-only machines".

| Machine | PCH / SoC | USB 2.0 EHCI? | xHCI quirk class | Role |
|---|---|---|---|---|
| ThinkPad E460 | Intel Skylake / Sunrise Point-LP (100-series) | no, removed | Clean | xHCI-only deployment validation, and the project's primary bench machine. Windows 98 only; Win2000 Setup bugchecks here. Machine state after batch 13-L: it carries `L3DBG.SYS`, the DEBUG candidate (82,811 bytes, sha256 `84708F2C...`), with the channel switched off (`XhciLogVerbosity` 0, `XhciLogDebugView` 0), at the project owner's direction. A later session must not assume this machine carries a release build; the acceptance run installs from scratch |
| ThinkPad P14s Gen 1 (Intel) | Intel Comet Lake (400-series) | no, removed | Clean | xHCI-only deployment validation (newer Intel gen). Windows 98 only; Win2000 Setup bugchecks here too. Windows 98 SE is installed on it (project owner): it postdates the 2012-2018 window above and boots anyway. The working configuration is the owner's `retro-configs` record for this machine, not anything derived here |
| B650M desktop | AMD Zen 4 (Raphael SoC + Promontory 21 chipset USB) | no, xHCI-only | Clean; chipset USB is ASMedia-derived | No longer available to this project. It was to be the xHCI-only AMD validation machine and it never ran the qualifier or the driver, so no AMD silicon has ever been tested and none remains that could. It was also the last candidate for the multi-controller console reading, its ASMedia-derived chipset USB plausibly being a second xHCI function |

#### Controller identity and qualification status

Each machine's controller as the Phase 0 qualifier reported it. The
authoritative record per machine is its `xhciqual/results/<machine>-<date>/`
directory: raw logs, the exact tool binary, and a write-up. This table is the
index to them, and it is here because "what hardware is in the fleet" and
"which silicon produced this reading" are the same question asked twice.

| Machine | xHCI PCI ID | Subsystem | EHCI functions | Capability facts (the `FACT` line) | Phase 0 verdict |
|---|---|---|---|---|---|
| E460 | `8086:9D2F` rev 21 | `17AA:5048` | none | HCIVERSION 0100, slots 64, ports 18 (1-12 USB2 managed, 13-18 USB3 unmanaged), scratchpad 34, 8 interrupters, IRQ 11 pin A | QUALIFIED, xhciqual v0.9, `xhciqual/results/e460-2026-07-25/` |
| P14s Gen 1 | `8086:02ED` rev 00 | `17AA:22B1` | none | HCIVERSION 0110, slots 64, ports 18 (same managed/unmanaged split), scratchpad 34, 8 interrupters, IRQ 11 pin A | QUALIFIED, xhciqual v0.9, `xhciqual/results/p14s-gen1-2026-07-25/` |
| B650M | not read | - | none (xHCI-only) | - | never run, and now never will be; machine no longer available |

Three things that table does not say on its face:

- Internal devices occupy controller ports and cannot be unplugged. E460:
  ports 6, 7 and 8 (Bluetooth radio, camera, fingerprint reader). P14s Gen 1:
  ports 6, 8, 9 and 10 (touchscreen, camera, fingerprint reader, Bluetooth).
  So "keep external test devices disconnected" means external ones only, and
  an empty-controller pass on either machine is not actually empty; neither
  machine can report `C6 SKIP`.
- The E460 and the P14s report bit-identical PM words (`PME_Support: D3hot,
  D3cold` and `NoSoftRst=1`), each independently cross-checked against
  `lspci` on the machine itself. That is what validated the qualifier's PM
  decoder across two Intel generations, and it is why the PME-stuck quirk
  class applies equally to both under Windows 2000.
- DOS `IRQ 11` and Linux `pin A routed to IRQ 123` (E460) / `IRQ 125` (P14s)
  are the same pin, seen through the 8259 and through the IOAPIC. It is not a
  disagreement, and it is the concrete form of the cross-target caveat every
  qualifier verdict carries.

Coverage assessment. Clean modern Intel silicon is covered at two
generations, and nothing else is:

- No AMD xHCI silicon has ever run this qualifier or this driver, and none
  remains that could.
- No Intel 7/8-series (Panther Point / Lynx Point) part is in the fleet, so
  the quirky-Intel class is uncovered: the compliance-mode lockup path is
  unexercised, and so is the `XUSB2PR` port mux. See "Never set BEI" and the
  `XUSB2PR` section in `docs/usb-xhci-info/xhci-programming.md`, both of which
  are written from datasheets and from Linux rather than from a measurement
  taken here. (BEI itself is not a coverage gap of that class: Linux applies
  `XHCI_AVOID_BEI` to every Intel controller, so both fleet machines carry it,
  and this driver never sets the bit in any case.)
- No machine in the fleet has EHCI, so there is no file-transfer safety net on
  any of them and no multi-controller machine for the qualifier's overflow
  case.
- The older AMD PLL/isoch quirks remain uncovered, as they always were.

Per-target-OS coverage is a separate axis, and it was assigned by measurement
rather than by choice. The table above records silicon coverage; Phase 13
additionally needs at least one bare-metal machine per target OS (Win98 SE and
Win2000 SP4). The Phase 0 qualifier's verdict is about the controller, not
the OS, so a QUALIFIED machine is a candidate for either in principle (see
`docs/contributing/design/01-hardware-qualification-tool.md` section 10). In
practice:

| Target OS | Machine | State |
|---|---|---|
| Win98 SE | E460 | Installed and running this driver since batch 7b-M. Covered. |
| Win98 SE | P14s Gen 1 | Installed (project owner). Second clean-Intel generation on metal, and the fleet's second Windows 98 vehicle. Not yet recorded here: whether NUSB is installed on it and whether `xhci98.sys` has ever been bound there; the install is the only thing reported. |
| Win98 SE | B650M (machine gone) | Was possible and never attempted. |
| Win2000 SP4 | E460, P14s Gen 1 | Setup bugchecks during installation on both (project owner). Cause not investigated on either; no bugcheck code or Setup stage was captured, so the limitation can say what was tried and not what failed. Not worth retrying as a settings problem. Whether this generalises to the era is an inference from two machines, not a measurement, and both are xHCI-only Skylake-and-later laptops, so xHCI-only-ness and the storage path are uncontrolled variables that have not been excluded. |
| Win2000 SP4 | B650M (machine gone); no candidate remains | Was not a candidate on that inference (Zen 4 is newer than both machines that failed) and was never independently tried. No machine in this project can attempt Windows 2000 at all, which is what put Phase 13 on its published-limitation branch. |

So the "one machine per target OS" clause is half-met and will stay that way.
No vehicle in this project can run Windows 2000 on real hardware, and none is
being sought; roadmap Phase 13's checkpoint carries that branch explicitly and
resolves it to a published limitation rather than to an open item. Every
Windows 2000 result in this repository is therefore a virtual-machine result.
Record whatever is installed here and in the maintainer's `retro-configs`
repository.

### The bench rig - two connectors, one tree, and nothing moves

This is the authoritative plug plan for every Phase 13 session, and for the
characterisation run that precedes them (roadmap batch `13-H`). Its purpose is
that a hub or a device characterised on the modern Windows host is
characterised in the arrangement it will be used in, and that the same
arrangement is physically possible on both fleet laptops, so a reading taken on
one machine can be compared with a reading taken on another, and with the
host's.

#### What each machine actually offers

The counts and sides below are from vendor documentation (sources at the end
of this section); the connector-to-controller-port mappings are this project's
own measurements and exist for four connectors.

| Machine | USB-A, by side | Served by xHCI? | Connector -> controller port | Stays out of the rig |
|---|---|---|---|---|
| E460 | left 1 (the Always On USB 3.0); right 2 (USB 3.0) | all three; this machine has no EHCI | all three mapped: left = xHCI port 3 (measured twice, the second time with the same drive), right screen-side = port 1, right user-side = port 2. Nothing external is unmapped; ports 4, 5 and 9-12 are simply unused | internal devices sit on ports 6, 7 and 8 and cannot be unplugged. Port 6 is an `8087:0A2B` Intel Bluetooth radio, absent from every stage of the first qualification run and present in every stage of the second, cause unestablished |
| P14s Gen 1 | left 1 (USB 3.2 Gen 1); right 1 (the Always On) | both; no EHCI | left connector = xHCI port 4 (measured). The right-hand one is unmapped; candidates 1, 2, 3, 5, 7, 11 and 12. Because it is the Always On port it is self-identifying and needs no label, but its port number is still unmeasured: it is a first-five-minutes step in the P14s's own stage E0, comes free if that machine is ever taken, and gates nothing, because the P14s owns no clause | four internal devices on ports 6, 8, 9 and 10; the three USB-C connectors (2x Thunderbolt 3, 1x 3.2 Gen 1) are out of the rig entirely, since a Type-C connector's USB 2.0 half lands on a controller port nothing has mapped |

The budget is two connectors, because the P14s Gen 1 offers exactly two
USB-A, so the rig uses two and no more. That is what makes one plan fit every
machine. Four consequences of the table are worth stating outright:

- The E460 is the only machine with a spare, its third USB-A, and it is a
  named one: the right-hand user-side socket, port 2. Use it for a staging
  flash drive if you want one, never as a third rig position. A rig that is
  two positions on two machines and three on the third is not one rig.
- Position T on the E460 is the right-hand screen-side socket, controller
  port 1, and it is physically labelled (settled at bench session 1; the two
  right-hand sockets look identical). The screen-side one was chosen for a
  mechanical reason rather than a measured one: T holds the hub and its PSU
  cable for a whole session, the front-edge socket is where hands and cables
  are, and a knocked hub latches `DeviceFailedEnumeration` on the port and
  everything behind it until a physical disconnect, which re-running a
  diagnostic then re-reports as a stale verdict. Both right-hand sockets were
  mapped in the same session, so the label is a convenience rather than the
  only identification: if it falls off, port 1 is the screen-side one.
- Both E460 rig positions are stickered, D as well as T, even though D never
  needed one (it is the only left-hand socket). The reason to do it anyway is
  what it makes the third socket: on that machine the unlabelled connector
  is, by exclusion, the spare. The hub cannot end up in the staging socket by
  mistake, which is a live risk there because the two right-hand sockets are
  identical and adjacent.
- The Always On port is on opposite sides of the two xHCI-only machines, left
  on the E460 and right on the P14s, so it is the mapped connector on one and
  the unmapped one on the other. Nothing in this project depends on Always On
  wiring today; it is recorded so that a difference between those two
  machines has somewhere to be looked up before it gets attributed to silicon.

#### The two positions

| Position | Which connector | What lives there |
|---|---|---|
| D (direct) | the mapped left-hand connector on each machine: E460 left (port 3), P14s left (port 4) | one device at a time, on a root port: the composite device, the Low-Speed HID, the audio device, a flash drive, the storage enclosure, and the `0B95:7720` Ethernet adapter |
| T (tree) | the other xHCI connector: E460 right-hand screen-side (port 1, labelled), P14s the second USB-A | the hub under test, always, and it never moves. Hubs are swapped here; children do not move when a hub does |

Position D is the mapped one on purpose: a port number in a qualifier log or a
driver trace is interpretable there on both machines, and nowhere else yet.
Position T's port number is known on the E460 (port 1) and remains unknown on
the P14s. Map it on arrival by moving the flash drive into it and reading the
port back, which costs one run and makes every later tree result citable. That
is how the E460's was taken (`xhciqual/results/e460-2026-08-22/`), and there
all three external connectors were mapped in the same session because each
extra socket costs one cold boot and removes a question permanently. The P14s
needs the run but not a label: it has only one right-hand USB-A, so position T
is self-identifying there and only its number is missing.

#### Fixed hub-port assignment

Children keep the same hub port number in the characterisation run and in
every bench session, so that a route string, a hub port in a trace, and the
host's characterisation output all refer to the same physical thing.

| Hub port | Device | Why it is there |
|---|---|---|
| H1 | the Low-Speed or Full-Speed HID | the leaf that forces a split transaction through the translator: the subject of task 13-E.2 and of the `MTT`/`TTT` numbers that never found a vehicle |
| H2 | the USB 2.0 flash drive | an ordinary High-Speed child, and the one whose presence is visible at a glance (a drive letter) |
| H3 | the composite audio device | task 13-E.3 clause 2, and its Windows 2000 counterpart: the same device behind a High-Speed hub |
| H4 | the downstream hub | the second tier: the spare High-Speed hub `05E3:0608` (with `1A40:0101` the swap partner at T, the spare is the only unit left for this position). A Full-Speed 1.1 hub would have been the stronger occupant, but none is held and its clause was published as untested ground before the trip (roadmap device-table row 5) |

If the hub has fewer than four ports, drop from the bottom: H4 first (the tier
case is lost), then H3 (audio-behind-a-hub is lost, and must then be taken by
moving the audio device into H2's place with the flash drive removed). Never
renumber to fill a gap. An empty H2 with a device in H3 is a valid rig, and a
device that moved between sessions is a result nobody can compare.

#### A hub socket is not a hub port, and the two must be mapped

Everything above assumes a hub's sockets are equivalent and that they are
numbered the way they are arranged. Both assumptions were measured on roadmap
batch `13-H`, and one of them is false. The two hubs this project holds are
the worked example:

| Hub | What it is | Sockets |
|---|---|---|
| `1A40:0201` (multi-TT, the standing position-T hub) | plain USB 2.0, no SuperSpeed half, one devnode reporting `Ports=7`, measured at both ends | seven, all one tier behind one TT. Numbering runs opposite to the physical order, and which end is which is measured: the cable end is logical port 7 and the far end is port 1 (see the map below) |
| `05E3:0608` (single-TT, the spare; it was the swap partner until `1A40:0101` was characterised, see the table below) | plain USB 2.0, one enclosure containing two cascaded chips | seven, and they are not equivalent. Chip 1 (`7&64daed6`) spends port 1 on the internal link and offers ports 2-4; chip 2 (`8&29230d8d`) offers all four. Sockets 1-3 from the cable end are tier 1, sockets 4-7 are tier 2 |

The hubs characterised by batch 13-H, all on the modern Windows host. Three
multi-TT and three single-TT, and neither VID:PID nor `TTT` predicts which:

| Hub | In the bag? | `bcdUSB` | TT class | `TTT` | Structure |
|---|---|---|---|---|---|
| `1A40:0201` | yes, position T | 0200 | multi-TT | 0 | 7 sockets, one chip, barrel jack (run self-powered) |
| `1A40:0101` | yes, the swap partner | 0200 | single-TT | 3 | 4 ports, one chip, no SuperSpeed half |
| `05E3:0608` | yes, position H4 | 0200 | single-TT | 3 | 7 sockets, two cascaded chips (3 + 4) |
| `05E3:0610` + `05E3:0612` | spare | 0210 / 0300 | multi-TT | 3 | USB 3.0 unit |
| `05E3:0610` + `05E3:0625` | no, standing setup | 0210 / 0320 | multi-TT | 3 | USB 3.0 unit, on a root port |
| `05E3:0610` + `05E3:0626` | no, carries the host's keyboard and mouse | 0210 / 0320 | single-TT | 3 | USB 3.0 unit, behind the one above |

`1A40:0201` <-> `1A40:0101` is the swap for task 13-E.2, and it is a genuine
one-variable change: same vendor, adjacent product IDs, both plain USB 2.0
with no SuperSpeed half, both a single chip at one tier, and 4 ports is
exactly H1-H4. `05E3:0608` was the swap partner until `1A40:0101` was
characterised; it stays useful as a ready-made two-tier topology, which is a
test article in its own right.

Three `05E3:0610` units exist and two of them are multi-TT while one is
single-TT: identical strings, no serials. That is the labelling case in its
strongest form, and the last two rows of the table are the demonstration; one
sits directly behind the other.

What a healthy USB 3.0 hub unit looks like here, and what it should look like
at the bench. A USB 3.x hub unit enumerates as two devnodes at the same port
number on the two parallel trees: its High-Speed half on the parent's USB 2.0
port and its SuperSpeed half on the parent's SuperSpeed port. `05E3:0610` +
`05E3:0612` both appeared at port 4 of their respective parents. Under
`xhci98.sys` the SuperSpeed half should be absent, because this driver leaves
USB 3.x root ports unpowered by design. That pair of readings is the control
for the fallback observation the rules below ask for, and the difference
between them is the observation.

Do not plan to identify a hub by its SuperSpeed half. The three `05E3:0610`
units' SuperSpeed halves have different PIDs (`0612`, `0625`, `0626`), so on a
modern host the SuperSpeed devnode does tell them apart, and on the target it
never enumerates at all. The one field that would sort them is guaranteed
absent on the machine where sorting them matters. Label the hubs physically.

A descriptor read is a property of the device and the speed it enumerated at.
Measured on two drives, each read both ways: on a SuperSpeed root port they
declare `bcdUSB` 0320 and carry 1024-byte bulk endpoints; behind a USB 2.0 hub
the same units declare 0210 and carry 512. One of them, `090C:2320`,
additionally offers a UAS alternate setting (proto `0x62`) on the SuperSpeed
path and a BOT-only alternate behind the hub; the other, `0781:55AB`, is
BOT-only both ways and has no UAS at all. So the speed changes the descriptor
on two of two specimens, while UAS being speed-gated is one of two.

"Characterise
it in the rig it will be used in" is therefore not only about negotiated
speed: the configuration descriptor itself changes, and a property read in the
wrong arrangement can be absent rather than merely different. Two consequences
to carry: a device characterised on a modern host's SuperSpeed port has not
been characterised for this driver, and `hub-characterise.ps1`'s endpoint
sizes are the reliable signal for which path a USB 3.x device took.

`hub-characterise.ps1` cannot report SuperSpeed as such, and says so in its
own output. `USB_NODE_CONNECTION_INFORMATION_EX` returns `UsbHighSpeed` for a
SuperSpeed device; the V2 request that separates them
(`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`, `0x22045C`) returns
`ERROR_INVALID_PARAMETER` from this script's handle at every buffer size and
`Length` tried, where an unimplemented control code returns
`ERROR_NOT_SUPPORTED`, so the code exists and wants something the handle does
not have.

The script therefore annotates any device declaring `bcdUSB >= 3.00`
that reads "High" rather than guessing, and the endpoint sizes and alternate
settings are what to read instead. The two cases it cannot separate are
opposites, operating at SuperSpeed versus having fallen back to the USB 2.0
companion path, and the second is the position-D observation the rules below
ask for.

Three rules follow, and they apply to any hub that joins the rig later:

- Map the sockets before assigning H1-H4, and label the hub with the logical
  numbers. The logical port is what appears in a route string, in a driver
  trace, and in `hub-characterise.ps1`; the moulded numbers on the case are
  not evidence of anything. On `1A40:0201` the two disagree completely.
- Check whether the enclosure is one hub or several. A "7-port hub" is
  usually two 4-port chips in one shell, and then which socket a child sits
  in decides its tier, its route string, and which transaction translator
  serves it, which is the property task 13-E.2 and the `MTT`/`TTT` numbers
  exist to read. The give-away is a second hub devnode appearing behind the
  first; `hub-characterise.ps1` prints each child hub's own entry path for
  this.
- A failed enumeration is owed a physical reseat before it is recorded as a
  finding. `DeviceFailedEnumeration` is latched on the port until a
  disconnect/reconnect event, so re-running any diagnostic re-reports the
  stale verdict however good the device is by then. A known-good drive read
  that status on one socket and enumerated cleanly after being pulled and
  pushed back in. At a bench, on a machine running this driver, a device that
  does not appear is the reading most likely to be misattributed to the
  miniport.

How a map is settled. All three maps are taken and are below; this is the
procedure that took them, and any hub joining the rig later goes through it.
`hub-characterise.ps1 -Walk` watches every port of every hub and prints each
arrival with a running number, so a map is taken in one pass:

```
powershell -ExecutionPolicy Bypass -File scripts\hub-characterise.ps1 -Walk -Hub 1A40:0201
```

Walk one device (a flash drive is ideal; its arrival is unambiguous) down the
sockets in physical order, starting from an end of the case you can name, and
leave it in each socket until its arrival prints. Arrival `#3` is the third
socket walked into and its line names the logical port that socket is. Ctrl+C
ends the run and reprints the arrivals in order, which is the map. `-Hub`
takes a `VID:PID` or any substring of the interface path, and may be omitted;
then every hub is watched, root hubs included, which is also how a laptop's
physical connectors get mapped to root port numbers without one cold boot per
socket.

Check the arrival count before recording anything: it must equal the number
of sockets you walked. A walk that ends with more numbers than sockets, or
fewer, is a failed run and its ordering cannot be trusted; re-walk it rather
than reasoning about which number belongs to which socket. `-Seconds N`
bounds a run instead of Ctrl+C, and the summary prints either way, so a walk
can be driven by someone who cannot see the console; then dwell about eight
seconds in each socket, since the arrival prints are what the operator would
otherwise be waiting on.

That check is what the first real walk failed (`1A40:0201`, the first time the
mode's transition branches ever fired). The arrival test counted every
non-empty signature, and a plug is seen as empty -> `** DeviceEnumerating **`
-> the settled identity, so sockets were numbered twice: twelve numbers for
seven sockets, and not even uniformly, because two of the seven transients
fell between polls. The map was recovered anyway, but only because this hub's
ports happen to run in a monotone sequence; on a hub with scrambled numbering
the summary would have been unreadable and nothing in it would have said so.

An arrival is now strictly the empty -> occupied edge, and a change between
two occupied signatures prints as a settle (`=`, no number) that amends the
arrival it belongs to, so the summary names the device rather than the
transient. A second walk of the same hub after the fix printed seven arrivals
for seven sockets and reproduced the first walk's map.

The general form is worth keeping: the first real use of a mode is its first
test, and the cheap guard is an invariant the run can be checked against
afterwards, here one arrival per socket walked.

Why a mode and not one report per socket: the report cannot answer this
question at all. Which socket is logical port 3 is a fact about the plastic,
and only the operator, hub in hand, can supply it. What the walk changes is
where the physical order comes from. It is the order the readings were taken
in, rather than a note written beside them, which is the half that went
missing before. Settling the three hubs in the bag the other way is eighteen
runs and eighteen by-hand correlations.

| Hub | What is recorded today | Map |
|---|---|---|
| `1A40:0201` (position T, multi-TT) | the full map, walked; see below | taken |
| `1A40:0101` (the swap partner, single-TT) | the full map, walked; see below | taken |
| `05E3:0608` (position H4) | the full map, walked, and it confirms the tier split recorded by a different method | taken |

`1A40:0201`, the map. Walked twice with a `0781:5408` flash drive, the second
time after the arrival-counting fix above; the two runs agree, and the second
printed seven arrivals for seven sockets.

| Socket, counting from the cable end | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Logical port | 7 | 6 | 5 | 4 | 3 | 2 | 1 |

The consequence for the rig, and the reason the map was owed: H1-H4 are logical
ports 1-4, so on this hub they are the four sockets furthest from the cable,
and they run backwards. The socket at the far end is H1, and H4 is the fourth
one back towards the cable. The three sockets nearest the cable (logical ports
7, 6 and 5) are unused by the fixed assignment. Load this hub from the far end
rather than from the cable end, and label its sockets with the logical numbers
before the next bench trip.

`1A40:0101`, the map. Same drive, same method, four arrivals for four
sockets:

| Socket, counting from the cable end | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Logical port | 4 | 3 | 2 | 1 |

Both `1A40` units number backwards from the cable end, and that is what makes
the position-T swap physically comparable. On each hub H1 is the socket
furthest from the cable and H1 -> H4 runs back towards it, so the same
physical loading order gives the same hub-port assignment on both units; the
7-port hub simply leaves its three cable-end sockets (ports 7, 6, 5) unused.
Task 13-E.2's swap therefore needs no re-plugging pattern of its own: move
each child to the matching socket counted from the far end and the port
numbers follow. That is the property stage E3's per-child readings need in
order to be comparable across the swap, and before these two walks it was
unknown. The two hubs could as easily have numbered in opposite directions,
which would have made the naive swap silently reverse H1 and H4.

`05E3:0608` was then walked too and agrees, within each of its chips. That is
three units of two makes, all numbering backwards from the cable end. Three is
a pattern worth expecting and is not a rule about hubs: it is still cheaper to
walk a new hub than to assume it, and the walk is what would catch the
exception. Nothing in the rig may rest on the direction being assumed for a
unit nobody has walked.

`05E3:0608`, the map. Two devnodes, so a socket is named by chip and port,
and the walk is the only reading here that has ever had to interleave two
hubs. Seven arrivals for seven sockets. Chip A is the upstream chip, the one
the host sees, and spends its port 1 on the internal link to chip B, which is
why it offers only three sockets:

| Socket, counting from the cable end | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Chip | A | A | A | B | B | B | B |
| Logical port | 4 | 3 | 2 | 4 | 3 | 2 | 1 |
| Tier | 1 | 1 | 1 | 2 | 2 | 2 | 2 |

This confirms the tier split recorded earlier by a different method (reading
the report per socket), which said sockets 1-3 are chip 1 ports 2-4 and
sockets 4-7 are chip 2 ports 1-4. Two methods, one answer. What is new is
which socket is which port inside a tier, the half that was missing and the
half `H1`-`H4` needs. The instance strings differ from the batch 13-H record
(`7&64daed6`/`8&29230d8d` then, `7&12528fef&0&3`/`8&19c19979&0&1` now)
because an instance encodes the host port the enclosure is plugged into. That
is not a discrepancy, and it is also why an instance string must never be
used to identify a hub unit across sessions.

Sockets 4-7 are one hub deeper than sockets 1-3, so a child there has a route
string one hop longer. It is not behind two transaction translators: a split
transaction is handled by the TT of the hub the Low-/Full-Speed device is
directly attached to, and chip A runs at high speed on both sides of that
path, so its TT is not in it at all. What the split buys is a second TT to
spread children across. Both chips are single-TT, so sockets 1-3 share chip
A's one translator and sockets 4-7 share chip B's. For task 13-E.2 and the
`MTT`/`TTT` numbers that makes this enclosure a way to put two Low-/Full-Speed
children on different translators inside one unit, which no single-chip hub in
the bag can do, and it is the reason it is useful at H4 at all.

Stage E3 was gated on all three maps being settled, and they are. Its
readings are per-child and must be comparable across the position-T hub swap,
so a child whose port number is a guess makes the pair of trees incomparable.
Batch 13-E's first bench session was blocked twice on this question and
answered it with "counting from the cable end" as a stand-in, sufficient for
asking whether anything works behind a hub and insufficient for E3
(`run-13e.md`, "Two socket maps that do not exist, and were needed twice").
What the maps remove is the guess, not the labelling: the sockets are still
unmarked plastic, so label the three hubs with their logical port numbers
before the bag goes to a bench, or the map is a document at a desk and the
operator is back to counting.

#### Four rules that decide what a plug means

- The USB 3.0 flash drive belongs in H2, not in position D. Behind a USB 2.0
  hub it is an ordinary High-Speed device, which is what task 13-E.2 and the
  TT numbers want. In position D on a blue connector it exercises something
  else entirely: this driver leaves USB 3.x root ports unpowered, so the
  device falls back to the USB 2.0 companion port. That fallback is worth
  observing once, on purpose, in position D, and recording as its own
  observation rather than as a hub result.
- Prefer a self-powered hub, and use the same hub the same way on the host. A
  bus-powered hub with a storage enclosure behind it can brown out, and a
  brown-out changes the negotiated speed, which would poison the very reading
  the characterisation exists to produce. If the hub is bus-powered, the
  enclosure goes in position D and never behind it.
- Swapping hubs at position T is the single-TT -> multi-TT replacement case.
  Task 13-E.2 asks for that operation, so the rig makes it the normal way to
  change hubs rather than an extra setup.
- On a machine that has EHCI, a device in a connector wired to it says nothing
  about this driver. It will enumerate and work, under Microsoft's driver, and
  prove nothing. That is the cheapest way to waste a bench session, and it is
  written here for a future machine: no fleet machine has EHCI today.

#### Where the port complements come from

Vendor documentation, because the count and side of each connector is a fact
about the machine rather than about this project and there is no reason to
measure what the manufacturer publishes:

- E460: *User Guide, ThinkPad E460 and E465*
  (`https://download.lenovo.com/pccbbs/mobiles_pdf/e460_e465_ug_en.pdf`),
  right-side view: "USB 3.0 connectors, HDMI connector, Ethernet connector, ac
  power connector, Lenovo OneLink connector"; left-side view: "Security-lock
  slot, Fan louvers, Always On USB connector (USB 3.0), Combo audio connector".
  Its interface summary is explicit about the split: "Two USB 3.0 connectors -
  One Always On USB connector (USB 3.0)". PSREF agrees: "Three USB 3.0 (one
  Always On)". No USB 2.0 connector and no Type-C on this machine.
- P14s Gen 1 (Intel): PSREF standard ports: "2x USB 3.2 Gen 1 (one Always
  On)", "2x USB-C 3.1 Gen 2 / Thunderbolt 3", "1x USB-C 3.2 Gen 1". The side is
  from the T14 Gen 1 / P14s Gen 1 Setup Guide, which puts the security-lock
  slot, the Ethernet connector and the Always On USB on the right, so the left
  USB-A is the plain one, which is the connector this project mapped to
  controller port 4.

## Installing the usbport USB 2.0 Stack (NUSB) - Win98 SE only

Win2000 SP4 needs none of this section. Its `usbport.sys` + `usbhub20.sys` +
`usbehci.sys` are Microsoft's own, shipped in SP4 (or KB319973). Do not install
NUSB there; see "Windows 2000 SP4 Target VM" for the on-demand file placement
that does need care on that target.

`xhci98.sys` is a `usbport.sys` miniport, so the target needs `usbport.sys` +
`usbhub20.sys` present before the driver can load. On Win98 these ship in NUSB
(Maximus-Decim Native USB Drivers), originally published via the MSFN forum and
mirrored at `https://www.philscomputerlab.com/windows-98-usb-storage-driver.html`.
A copy of `nusb33e.exe` is kept in `tools/` (git-ignored, like the MSVC/DDK
archives) so Phase 2a does not depend on a live download.

1. Use NUSB 3.3 (`nusb33e.exe` for the English Win98 SE), installed in full,
   as-is (decision by the project owner). Know what it does to the OS: 3.3's
   `_NUSB.INF` unconditionally replaces core files (`user.exe`/`user32.dll`
   4.10.0.2231, `systray.exe`, `explorer.exe`, `ios.vxd`, `hotplug.dll`), but
   with Microsoft Win98 SE QFE hotfix builds (Q242975 lineage), the mildest of
   the options. 3.6 is avoided because it goes further: WinMe-derived files
   plus `sysdm.cpl`, which makes System Properties wrongly report "Windows
   ME".

   NUSB 3.6 does work with this driver, though. Checked 2026-09-01: the 3.6
   package's `USBPORT.SYS`, `USBEHCI.SYS` and `USBHUB20.SYS` are byte-identical
   to 3.3's (hashes match the ABI record), its INFs claim no `CC_0C0330`
   device, and a 2a-fresh clone taken through uninstall-3.3, install-3.6 and a
   reboot ran HID and mass storage cleanly, including formatting and file I/O
   on a hot-plugged disk. What 3.6 changes is the layer above the miniport: it
   places WinMe `usbd.sys` 4.90.3000.1 and `usbhub.sys` 4.90.3002.1 and an XP
   `usbccgp.sys` (KB945436), which pre-empt the no-overwrite copies of
   `usbd.sys` and `usbhub.sys` the install would otherwise make. A VM
   observation only; project guests
   stay on 3.3. (`docs/usb-xhci-info/usbport-miniport-interface.md` section 5
   has the file comparison.)

   The SweetLow stack. A third Windows 98 stack, examined 2026-09-02 after
   issue #1: SweetLow's XP-lineage rebuild (`USBPORT.SYS` 5.1.2600.2180
   "built by: WinDDK", with `USBEHCI.SYS`, `USBHUB20.SYS`, `USBCCGP.SYS`,
   `USBDSTUB.SYS` and his edit of Microsoft's `USB2.INF`), from his
   `usb20_win9x.zip` (`http://sweetlow.orgfree.com/download/usb20_win9x.zip`;
   Windows 98 QuickInstall's driver library,
   `https://github.com/oerg866/win98-driver-lib-base` directory
   `[MBD]_sweetlow_usb2.0`, ships the same binaries). Keep the zip in
   `tools/` and its extraction in `tools/sweetlow-extracted/` (git-ignored;
   the README there records URLs and hashes). It
   registers this driver (GetHciMn `0x10000001`, which the probe already
   accepts), runs HID and mass storage, and, unlike both 5.00.2195 builds,
   survives disable, re-enable, Remove and reinstall on Windows 98 (the
   lessons entry "The Windows 98 teardown bugcheck belongs to the Windows
   2000-lineage usbport"). The guest for it is the `2a-sweetlow` matrix
   target, cloned from the stamped `fresh-2a.img` so the driver is already
   installed, and prepared like this:

   ```powershell
   powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-sweetlow -Clone
   # vm\SWEETLOW\ = the zip's contents (his USB2.INF copies no usbd.sys, so nothing else is needed there)
   powershell -File scripts\vm-matrix\prepare-image.ps1 -Target 2a-sweetlow -Boot -Xfer -XferAdd vm\SWEETLOW
   ```

   The first pass cloned the stamped `fresh-2a.img` (driver already
   installed) and swapped only the stack; the second cloned
   `win98.img @ post-nusb` (no driver, no `usbd.sys`) and installed the
   driver afterwards, which is the order a user takes. Set the target's
   `CloneFrom` accordingly; `config.sample.psd1` carries the first.

   In the guest, from Start, Run: NUSB's own uninstall string,
   `RUNDLL32.EXE C:\WINDOWS\SYSTEM\ADVPACK.DLL,LaunchINFSection C:\WINDOWS\INF\_USB2UN.INF,UNINSTALL`
   (it deletes the three stack files and `USB2.INF`, nothing else, and shows
   no dialog); then
   `rundll32 setupapi,InstallHinfSection DefaultInstall 132 D:\SWEETLOW\USB2.INF`
   (`D:` is the transfer drive; no dialog either); then shut down and relaunch
   `-Boot`, because this guest halts on a reboot. `dir C:\WINDOWS\SYSTEM32\DRIVERS\USB*.SYS`
   in a DOS box shows the swap. The target runs with `Machine = 'pc,smm=off'`
   for the QEMU reason in the lessons entry next to that one. Still VM only,
   no matrix run; 3.3 remains the tested configuration and what the
   acceptance test installs. `DisableSelectiveSuspend` is still needed under
   it, measured the same day with no keep-alive pointer attached: with the
   value present, no `SuspendController` in four idle minutes and a
   hot-plugged keyboard was addressed at once; with the value deleted
   (`regedit /s` of a `"DisableSelectiveSuspend"=-` file, a clean shutdown,
   a relaunch), `SuspendController` fired once shortly after start and a
   keyboard hot-plugged afterwards was never seen (QEMU lists it at the port
   with address 0, the driver's addressed count stays 0). The same behaviour
   as NUSB's build, so the INF's global value stays. One QEMU trap on that
   run: `sendkey` input follows the most recently added keyboard, so a USB
   keyboard hot-plugged onto a suspended controller silently swallows every
   keystroke until `device_del` removes it.

   What the stack needs beside it, measured 2026-09-02 on a guest cloned
   from `win98.img @ post-nusb` (NUSB 3.3's core files, no driver, no
   `usbd.sys`, no `usbhub.sys`), with NUSB's USB 2.0 stack removed and
   SweetLow's `usb20_win9x.zip` (his own `USB2.INF`, the Full variant)
   installed, then this driver installed from a package whose INF had every
   `usbd` and `usbhub` line removed. `usbd.sys` is required: the driver
   registered and started, but the USB 2.0 Root Hub sat at Code 2 ("The
   NTKERN.VXD device loader(s) for this device could not load the device
   driver") with no root-hub callback in the trace, because his
   `usbhub20.sys` imports `USBD.SYS` by name like NUSB's; copying the
   package's `usbd98.sys` to `SYSTEM32\DRIVERS\usbd.sys` by hand and
   rebooting brought the root hub and the mouse up. `USBDSTUB.SYS` is not a
   substitute (his notes call it a helper for the XP SP3 QFE usbccgp, and
   nothing in his INF installs it). `usbhub.sys` is not required: a
   hot-plugged two-interface `usb-audio` enumerated, installed from the CD,
   and appeared as "Composite Device" under Universal Serial Bus
   controllers with "USB Audio Device" beneath, i.e. parented by his
   `usbccgp.sys` through the INF's `USB\COMPOSITE` binding (the Lite INF
   binds `USB\COMPOSITE2` only and was not tried). Under NUSB the same
   device stops at "USB Composite Device", Code 2, without `usbhub.sys`.
   And the package's `usbhub98.sys` copy does not get in his way: from the
   `sweetlow-stack-nodriver` snapshot, the driver installed from the full
   package (so `usbd.sys` and `usbhub.sys` both placed), then the audio
   device plugged once, Windows still chose "Composite Device" (his
   usbccgp, whose INF is the newer of the two claiming `USB\COMPOSITE`)
   rather than USB.INF's "USB Composite Device" on `usbhub.sys`. So the
   package as shipped is right for both lineages: `usbd.sys` needed by
   both, `usbhub.sys` needed by NUSB's and inert under his. One trap on
   the way: Windows 98's `USBAUDIO.VXD` faults (exception 00 at
   `+00002ED4`, the Phase 9 finding) once the audio device streams, which
   a boot-time arrival with a stored assignment does at once; only a first
   arrival lives long enough to read the tree, so the device is plugged once
   per guest and never cycled.

   The full install is used because it is the environment end users of
   `xhci98.sys` will run, and a pre-install VM snapshot makes it reversible.
   (A USB-stack-only alternative, right-click-Install on just `USB2.INF`, was
   considered and rejected for Phase 2a as unrepresentative; it remains a
   useful trick for a minimal repro host.) See the caveat block in
   `docs/usb-xhci-info/usbport-miniport-interface.md` section 5 for what
   `_NUSB.INF` replaces.
2. Install it in the VM or machine and reboot. Take a VM snapshot first (see
   "VM snapshots").
3. Record the exact `usbport.sys` version and file hash it installs
   (`C:\WINDOWS\SYSTEM32\DRIVERS\usbport.sys` -> right-click -> Version, and
   `certutil -hashfile C:\WINDOWS\SYSTEM32\DRIVERS\usbport.sys SHA256` if
   available after copying it out). The miniport's
   `USBPORT_REGISTRATION_PACKET` layout must match this build. Copy the
   version, size, timestamp, SHA256, and `dumpbin /exports` output into the
   ABI record in `docs/usb-xhci-info/usbport-miniport-interface.md` before
   starting Phase 3. (Community note: `usbport.sys` builds newer than
   `5.0.2195.5652` are flaky on 9x; NUSB pins an older one.)
4. To inspect contents without installing, `nusb33e.exe` is a self-extractor;
   open it in 7-Zip to read `USB2.inf` and the file list.

NUSB's `USB2.inf` binds only EHCI (`PCI\CC_0C0320`); it leaves the xHCI device
(`PCI\CC_0C0330`) unclaimed. That binding is this project's INF (below). This
is a device-binding INF, separate from where the driver files come from.

Installing NUSB places `usbport.sys`/`usbhub20.sys` unconditionally, with no
EHCI required. NUSB's own setup INF, `_NUSB.INF` `[DefaultInstall]`, runs
`CopyFiles=...,W98Upd.Copy.Sys32,...` at install time; `[W98Upd.Copy.Sys32]`
(dest = dirid 10 + `SYSTEM32\DRIVERS`) explicitly lists `USBPORT.SYS`,
`USBHUB20.SYS`, and `USBEHCI.SYS`. So the moment NUSB is installed, the stack
is in `C:\WINDOWS\SYSTEM32\DRIVERS\` regardless of which USB controllers are
present. Confirmed on real xHCI-only hardware (Win98 + full NUSB install, no
EHCI/companion controller): the files were correctly placed. `USB2.inf`'s
per-EHCI-device `CopyFiles` is a separate, redundant path; it is not what puts
the files on disk.

- An earlier note here claimed NUSB leaves the stack absent on xHCI-only
  machines until an EHCI triggers the copy. That was a false negative: the
  original Phase 2a VM check looked in `C:\Windows\System\` (the wrong
  directory; Win98 keeps these WDM drivers in `SYSTEM32\DRIVERS`) and saw
  nothing. The files were present all along. See
  `docs/contributing/lessons.md`.
- In QEMU (development): no EHCI is needed to get the usbport stack; a plain
  NUSB install provides it. An emulated EHCI (`-device usb-ehci` alongside
  `-device qemu-xhci`) remains optionally useful, since it gives a live
  `usbehci`-on-`usbport` miniport to observe as a reference while building
  `xhci98.sys`. It is not a prerequisite for anything.
- On the real xHCI-only target (deployment): because the project's baseline
  is a full NUSB install, `usbport.sys`/`usbhub20.sys` are already in
  `SYSTEM32\DRIVERS`, and `xhci98.inf` binding `PCI\CC_0C0330` can rely on
  them being present. Bundling the usbport stack in `xhci98.inf`'s own
  `CopyFiles` would be optional and defensive, useful only for a hypothetical
  host without NUSB, and it was decided against; see "Bootstrapping xHCI-only
  machines" above for the reasoning and for what to reach for if such a host
  ever appears.

## Installing the Driver

One `xhci98.sys` and one `xhci98.inf` serve both targets, but the setup engines
differ (see "Why the INF must carry both install paths"), so the manual
development-install steps differ too.

Both procedures need `xhci98.inf` and `xhci98.sys` in the same directory. The
INF's `[SourceDisksNames]` names `xhci98.sys` as its tag file and its
`CopyFiles` copies the driver from wherever the INF was found; there is no need
to place the `.sys` by hand, and doing so does not substitute for it.

### Manual Installation on Win98 SE (Development)

1. Copy `xhci98.inf` and `xhci98.sys` together into a working directory in the
   VM (the INF copies the driver to `C:\WINDOWS\SYSTEM32\DRIVERS\`, alongside
   the NUSB-installed `usbport.sys`).
2. Open Device Manager, find "Universal Serial Bus (USB) Controller" or "PCI
   Device" with PnP ID `PCI\CC_0C0330`.
3. Right-click -> Update Driver -> specify the directory containing
   `xhci98.inf`.
4. Reboot if prompted.

### Deploying a build into the Win98 VM (the iteration loop)

Measured during the Phase 3 spike; each of these rules cost a wasted VM cycle
to learn.

- Read the `qemu` build's trace over the QEMU debug console, not a debugger.
  The run launcher generated by `scripts\setup-qemu.ps1` carries
  `-chardev file,id=dbgcon,path=vm\win98-debugcon.log` plus
  `-device isa-debugcon,iobase=0xe9`, which is the sink `src\xhci_dbg.c`
  writes every finished line to. Before QEMU starts, the launcher moves a
  non-empty prior trace to `vm\win98-debugcon.previous.log`; the current log
  therefore belongs to exactly one boot, while the immediately preceding
  evidence remains available. A zero-byte current log is itself a result: it
  means that boot's image never reached `DriverEntry`. Do not add
  `append=on`; stale registration lines can otherwise be misattributed to a
  replacement driver.

  An empty current log is not rotated, on purpose. A launch that dies before
  QEMU writes anything (a bad argument, a disk image another QEMU still holds)
  also leaves a zero-byte log, and rotating that would push the last real
  trace out of `.previous.log` and replace it with nothing; two failed
  launches in a row would destroy the evidence the rotation exists to protect.
  QEMU truncates the current log when it opens it, so leaving the empty file
  in place costs nothing. `scripts\test-qemu-launchers.ps1` executes the
  launcher's rotation preamble against real files to hold this.
- Cold-start after installing or replacing the driver. Do not accept the
  reboot Win98 offers. A guest-initiated reboot leaves this VM wedged on the
  splash screen, in real mode, spinning on the BIOS tick at `40:6C` while
  IRQ0 still fires, a wedge that looks like a driver hanging the boot and is
  not one. Shut the guest down, let QEMU exit, relaunch.
- A loaded driver binary can be overwritten in place, on both targets.
  `copy d:\newbuild.sys c:\windows\system32\drivers\xhci98.sys` answers
  `1 file(s) copied` even though the driver is loaded, so a rebuild-and-retest
  cycle is copy + cold restart, with no reinstall and no wizard. (Only the
  binary; an INF change still needs a real install.)

  Measured on Win98 SE and confirmed on Windows 2000 SP4 at
  `C:\WINNT\SYSTEM32\DRIVERS\XHCI98.SYS`, where the target is a mapped image
  held by the memory manager and a sharing violation would have been the
  plausible outcome. It is not: the copy succeeds and the next cold start runs
  the new binary. So the same one-line iteration loop serves 2a and 2b, and
  there is no need to route a driver-only change through Device Manager on
  either.

  Verify by size, in the guest, before shutting down. `dir` the destination
  and compare the byte count with the staged file: it is the only check
  between "I copied it" and the next boot's banner, and it is what a stale
  source defeats. A 2b cycle was lost to that: `vm\xfer\pkg\` still held the
  previous build while `vm\xfer\XHCI98.SYS` beside it had been refreshed, so
  a correct copy from the wrong source produced a banner indistinguishable
  from a copy that never happened. Refresh every staged copy of a binary, not
  the one you happen to be thinking of: `Copy-Item out\pkg-qemu\* vm\xfer\pkg\
  -Force` alongside the root-level file.

  A VM session that reads the trace or
  the `DriverEntry` banner needs the `qemu` flavour, since the port-`0xE9`
  sink is in no other one. Deploy `debug` or `release` when what is under test
  is a shipping binary's behaviour, and expect no trace from either.
- Confirm the deployed binary from the trace's `DriverEntry (built ...)` line
  and `MiniPortExtensionSize`, never from a file date. Staging a package into
  `vm\xfer*\` does not put it in the guest; the copy is manual and per-guest,
  and all three VMs once booted a stale binary first (2a a pre-fix build, 2b
  a Phase 6 build, 2d a Phase 4/5 one). Checking the stamp costs one grep and
  is the difference between testing the fix and testing the defect. The same
  two lines double as the layout check for `readcounters.ps1`.
- Do not disable the controller in Device Manager. Disabling any USB host
  controller devnode on this VM bugchecks it with
  `A fatal exception 0E ... at 0028:C00312EE`, including the shipping
  `usbehci.sys` on an added `-device usb-ehci`. Disabling the USB Root Hub is
  fine. See `docs/contributing/lessons.md`, "Option A works on Win98". This is
  Win98-only: on Win2000 the same binary disables, re-enables and uninstalls
  cleanly, running two more teardown callbacks (`DisableInterrupts`,
  `StopController`) than Win98 ever reaches (`docs/contributing/lessons.md`,
  "The same binary passes on Win2000 SP4"). Test those transitions on the
  Win2000 VM.
- Everything that stops the running driver is that same teardown, and batch
  11-V stage B measured which operations reach it. The full record, with the
  traces and the footprint tables, is `docs/contributing/runs/run-11v.md`
  stage B, and the user-facing statement is `docs/using/release-notes.md`,
  the controller teardown entry under "Known limitations". Measured:
  - Uninstall (Remove) crashes and does not commit. The next boot raises no
    Add New Hardware Wizard, the devnode is back with a root hub under it, and
    the driver key still describes the package. A Win98 uninstall attempted
    the obvious way costs a crash and leaves the install in place.
  - An in-place upgrade crashes too, because it stops the running driver
    first, and it commits half: the file copy has already happened, so the
    new binary loads after the reboot, but the whole registry phase is lost.
    The machine reports the old version while running the new binary, and any
    `AddReg` a new package introduces never arrives. Three independent
    witnesses agreed (`Services\USB` absent, `DriverDate` still the baseline
    file's, the cached INF the new one). The repair is the INF's right-click
    Install (`[DefaultInstall]` touches no device, so it cannot reach the
    teardown), which is what delivers `[Xhci.AddReg.Global]`.
  - Rollback is not a separate case: Win98 has no Roll Back Driver, so a
    rollback is an uninstall plus a reinstall, two teardowns rather than one.
  - The uninstall route that costs no crash is to unload the driver first:
    `ren xhci98.sys xhci98.sav` from an MS-DOS Prompt -> reboot (yellow bang,
    no root hub, 0 bytes of trace: the proof it did not load) -> rename it
    back inside Windows and do not press Refresh -> Remove, which completes
    cleanly. Take the footprint before rebooting, since a reboot lets PnP
    re-detect `PCI\CC_0C0330` and reinstall from the cached INF, which
    contaminates the rows being read. What the uninstall takes is the devnode
    and its driver key: `xhci98.sys`, `usbd.sys`,
    `C:\WINDOWS\INF\OTHER\XHCI98~1.INF` and `Services\USB\
    DisableSelectiveSuspend` all survive it.
- This collides with the one procedure that can observe a description or
  identity change, and the collision is unavoidable. Both setup engines cache
  `DriverDesc` in the device's software key at install time, so a renamed
  `%XhciDesc%` is invisible on an existing devnode and the only way to see it
  is a fresh install or an uninstall-and-reinstall, which on Win98 is the
  controller teardown above. Batch 8-V paid it (`0028:C00312EE`, same
  address, with the trace ending on `RH_DisableIrq` as it always does).

  So on
  Win98, expect the fault and sequence the run to pay it once. Remove the
  devnode and reboot immediately, with the new media already on the transfer
  volume, so the install completes on the way back up; the description is
  written to the software key during that install and survives the crash,
  so the observation is still available afterwards. Do not remove
  the devnode a second time to "check"; read the software key instead. On
  Win2000 none of this applies, since uninstall is clean there, so take the
  identity reading on 2b first when both are available and let it predict
  what 2a should show.
- That bugcheck can leave the image unbootable, and ScanDisk will tell you
  the disk is fine. Measured: the boot after batch 8-V's install bugcheck
  came up with a cascade of `Invalid VxD dynamic link call from VWIN32(01) to
  device "0009"`, one per keypress, each naming the next service number, then
  reached the desktop wallpaper with no shell, no icons and no taskbar. The
  miniport never loaded (debugcon 0 bytes, QEMU trace frozen at the BIOS's
  own port probing), so nothing about it was ours.

  `scandisk c:` under
  Command prompt only then reported no problems found: the FAT is intact and
  the damage is in the VxD/system-file state, which ScanDisk neither sees nor
  repairs. So a clean ScanDisk is not evidence the image is recoverable; it
  is what rules the cheap repair out. Revert to a qcow2 snapshot instead
  (`qemu-img snapshot -a <tag> vm\win98.img`, with QEMU not running; check
  for a live process first, since qemu-img will happily corrupt an image that
  is open). Two operational notes that cost time here:
  - ScanDisk needs `HIMEM.SYS`, so it must be run from Startup Menu option 5,
    "Command prompt only". Option 6, Safe mode command prompt only, skips
    `CONFIG.SYS` and ScanDisk refuses with "there is no extended memory driver
    loaded".
  - Keep snapshots current. The newest tag at the time was `phase4-clean`,
    five phases back, so the revert gave up every install since. A snapshot
    taken after each successful package install costs seconds and is the
    difference between a revert and a rebuild.

To bisect something that can only be changed at build time, `src\sources`
reads `XHCI_EXTRA_DEFINES` from the environment:

```
set XHCI_EXTRA_DEFINES=-DXHCI_PROBE_RESOURCES_SIZE=4096
scripts\build-driver.cmd debug
```

That particular knob overrides the declared `MiniPortResourcesSize` only, and
is how the 372 KB common buffer was ruled out as the cause of the disable
bugcheck. The image carries `XHCI98_PROBE_BUILD_DO_NOT_DEPLOY`, and
`make-package.ps1` refuses that marker before staging anything.

Adding a knob needs no special care. `src/sources` defines
`XHCI_DIAGNOSTIC_BUILD` whenever `XHCI_EXTRA_DEFINES` is nonempty,
`xhci_dispatch.c` emits the marker under that, and `build-driver.cmd` reads
the built image back and fails if the marker is missing. So a diagnostic
binary is refused at package time whatever knob produced it. (Earlier only
`XHCI_PROBE_RESOURCES_SIZE` embedded the marker, and any other define was
warned about at build time and then accepted for packaging. A warning is not a
gate.)

The layout carver still computes the real worst case, so return to a
deployable build explicitly:

```
set XHCI_EXTRA_DEFINES=
scripts\build-driver.cmd debug
```

Clear it with the quotes inside, `set "XHCI_EXTRA_DEFINES="`, if you are
writing it into a `cmd /c "... && ..."` one-liner: `set VAR= && next` assigns
a single space, which is nonempty, so the next build is a diagnostic build
that looks exactly like the clean one you asked for. Measured here.

#### Staging a driver that starts and fails (task 12.3)

This is the one diagnostic build that may be packaged. It exists because
recovery from a driver that installs, loads, and then fails inside
`StartController` had never been staged on either target; a failed load is a
different thing and does not reach this driver's code at all.

```
set XHCI_EXTRA_DEFINES=-DXHCI_FAIL_START_CONTROLLER
scripts\build-driver.cmd debug
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor debug -FailStartArtifact
set "XHCI_EXTRA_DEFINES="
scripts\build-driver.cmd both
```

The package lands in `out\pkg-failstart-debug\`, a different directory from
`out\pkg-debug\` on purpose, so it cannot quietly become the one a VM is
installed from.

What it does on the target. The init sequence runs to the end (the controller
is reset, the rings are built, `R/S` is set, the managed ports are powered and
the No Op self-test is submitted), and then the last step refuses, through the
same exit shape the No Op step's own failure takes: `XhciStopController`,
fail-closed if the stop cannot be proved, `INITIALIZED` dropped, and the
refusal recorded. So the artifact exercises the most start-time cleanup any
refusal in this driver performs. The refusal is visible as `InitStep` 250
(`XHCI_INIT_STEP_FAIL_INJECT`, outside the sequence's range on purpose),
`init.refused.step=000000FA` in the log ring, and `MP_STATUS_HW_ERROR` back to
usbport.

It is the same shape, not the same path. The No Op step stops the controller
only when the submit failed, with nothing outstanding. The injected refusal
runs after a successful submit, so the quiesce abandons a live command.
Internally that is `CommandsAbandoned` = 1 and `NoOpWitnessFired` = 0; both
are correct here and neither is the artifact finding something. Injecting
before the submit would avoid them and was rejected: a start whose command
ring was never exercised is not the most complete start.

Only one of those is readable during the run. `CommandsAbandoned` and
`NoOpWitnessFired` are printed by `CheckController`, which usbport does not
call once `StartController` has failed, and the `cmd.abandoned` log record is
discarded while no sink is enabled. What reaches the debugcon trace is the
direct line `command: abandoned outstanding TRB=<pa>`, emitted where the
command is invalidated, and that is the observation to look for.

What the run printed, on both targets, line for line (from the `vm\12v-runb\`
and `vm\12v-runc\` traces, since discarded; this transcription is the record).
The refusal path prints, in this order:
the init sequence to its end, `No Op command issued at TRB=<pa>` / `No Op
submit status=00000000`, `teardown: ports unpowered=00000004` with `ports that
would not give up power=00000000`, `command: abandoned outstanding TRB=<pa>`,
`quiesce: halted, USBSTS=00000009`, `init REFUSED at step=000000FA`, `init
refusal status=00000000`. The abandoned TRB address equals the No Op submit
address three lines earlier, which pins the exit as the No Op step's shape
after a successful submit rather than that step's own exit. No
`XhciFailClosedDma` and no bugcheck on either target.

A second witness the run got for free. The ordinary shutdown quiesce on both
targets reads `quiesce: halted, USBSTS=00000001`, HCH alone, while the failed
start's reads `00000009`, HCH plus EINT, the pending event from the command
that was abandoned. Same code path, matched control, and it says "a command
was outstanding" independently of the one trace line the run sheet had
planned on.

2b is the clean result. `DriverEntry (built ...)`, so the driver loaded, then
the refusal, printed twice (Windows 2000's usual unload/reload); Device
Manager shows Code 10 on the controller with no USB Root Hub beneath it, and
the machine stays alive. Recovery is the documented route and works:
uninstall, delete the cached `oemN.inf`/`.pnf`, install the real package. The
root hub returns and a mass-storage device enumerates (`slots enabled=1`,
`devices addressed=1`, first speed decode `00010103`, `transfers submitted ==
completed == 0xA5`, no error counter moving).

2a does not survive its own failed start, and that is the run's finding. The
artifact was delivered by file swap, so no install or uninstall path was
re-measured and the teardown bugcheck was not paid. The driver produced the
identical refusal and cleanup trace to 2b's, and then Windows 98 died before
reaching a desktop: `Windows protection error. You need to restart your
computer.`, frozen in ring 0 at `EIP=00000724` (confirmed by two `info
registers` samples), with the startup menu reporting "Windows did not finish
loading on the previous attempt" on the next boot. It is deterministic (four
artifact boots, all identical) and it is not the logging path.

The first two
boots inherited the existing driver key and so also ran a log flush
(`DebugView sink=00000001`, `flush emitted bytes to DebugView=00000694`) that
2b's fresh key never enabled, the only confound between the targets; a
control boot with every `XhciLog*` value deleted reproduced 2b's condition
(`DebugView sink=00000000`, `flush skipped - no sink selected=00000001`) and
froze at the identical address.

Two further controls bound it: Safe mode
reaches a desktop with the artifact in place (the driver does not load; the
debugcon log stays at 0 bytes), and the same image booted normally minutes
earlier with the real driver.

Whether the fault is in this driver's return path or in what NUSB's
`usbport.sys` and NTKERN do with a `StartController` failure is unmeasured;
there is no Microsoft HCD that fails `StartController` to run as the control.
Windows 98's boot log does not help: its tail stops at `Starting Unknown
(HTREE\RESERVED\0)`, which is where its buffer was last flushed and not where
Windows got to, so no module is named and none is inferred. Recovery on 2a
needs Safe mode, since a normal boot never completes, after which the real
`XHCI98.SYS` copies back over the artifact and a cold restart reaches the
desktop with `init complete, USBSTS=00000008` at `init step=00000016`.

Why the exception is safe to have. It is keyed on a second marker,
`XHCI98_FAILSTART_ARTIFACT_TASK_12_3`, emitted only under that define, and the
packager requires both markers plus the switch. A resource-size probe carries
only the do-not-deploy marker and is still refused with the switch present, so
`-FailStartArtifact` cannot be used to talk the gate into staging some other
broken binary. All four combinations are driven by
`scripts\package\test-package.ps1`, which runs on every `build-driver.cmd`.

Before installing it, take a snapshot you can revert to. On Windows 98 an
upgrade or an uninstall of a working driver reaches the teardown bugcheck
(release notes, "Known limitations", the controller teardown entry), so plan the recovery route before spending the
boot, not after.

#### Staging the unpadded-date experiment package (task 12.4)

Windows 2000 records no `DriverDate` for this package, and that is why an upgrade
over an older build is refused. Stage B ruled out the date value; the two
differences from Microsoft's own working entry that remain are that ours is
unsigned and that its date is zero-padded. This package varies the second one
and nothing else:

```
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor debug -UnpaddedDriverVerExperiment
```

It lands in `out\pkg-datefmt-<flavor>\`, and its INF differs from
`src\xhci98.inf` on exactly one line: a `DriverVer` date without its leading
zeros (`8/18/2026` rather than `08/18/2026`). The variant is derived at
staging time, never committed, so there is no second INF in the tree to drift;
`xhci98.rc` is copied beside it so the DriverVer/FILEVERSION cross-check still
runs; and the gate is invoked with `-AllowUnpaddedDriverVer`, which widens
`^\d{2}/\d{2}/\d{4}` to `^\d{1,2}/\d{1,2}/\d{4}` and relaxes nothing else. A
two-digit year, an ISO date or a missing version are refused with the switch
on, as `scripts\inf-gate\test-inf-checks.ps1` asserts. Both the gate and the
packager say out loud that the relaxation was used.

The packager also refuses
the two degenerate inputs: a source whose date is already unpadded (a package
identical to the shipping one is not an experiment) and a source with more
than one `DriverVer` to rewrite. `scripts\package\test-package.ps1` checks the
single-difference property by comparing every line of the derived INF against
its source and failing if the count of differing lines moves from one.

The step that decides whether the run measures anything: delete
`%SystemRoot%\inf\oemN.inf` and its `.pnf` on the guest first. Stage B
measured that cached copy outranking the offered package on five separate
install routes, Have Disk included, so with it in place Setup keeps the
installed driver and the experiment answers a question about caching instead
of about dates.

An unpadded date is Microsoft's own form (`2/15/2003` in SP4's `[EHCI.NT]`),
so what the switch relaxes is a local convention, not the INF format. Keep the
convention for everything else: a two-digit-everywhere date is the one form
neither setup engine can misread.

How to verify the experiment measured anything: the two packages are
byte-identical apart from one INF line, so the `DriverEntry (built ...)` stamp
cannot tell them apart; it only witnesses that a staged package replaced the
incumbent. What pins the run is the cached INF itself.
`findstr /i driverver C:\WINNT\inf\oem*.inf` must read the unpadded
`DriverVer=<m>/<d>/<yyyy>,<version>` during the run, and `InfPath` in the
class key must name that same `oemN.inf`, tying the file to the key the date
is read from. Without both, a mis-picked directory produces an identical "Not
available" off a package that varied nothing.

What the run read (from the `vm\12v-runa\` capture on 2b, since discarded;
this paragraph is the record). The Driver tab still
showed `Driver Date: Not available`, and the driver key
`...\Class\{36FC9E60-C465-11CF-8056-444553540000}\0023` held no `DriverDate`
value at all. Its value list was `DriverDesc`, `DriverVersion`,
`EnIdleEndpointSupport`, `InfPath`, `InfSection`, `InfSectionExt`,
`MatchingDeviceId`, `ProviderName`, `XhciLogDebugView`, `XhciLogFile`, nothing
else. So the blank field is not a rendering quirk: nothing was written,
zero-padding is excluded, and the remaining named difference is that the
package is unsigned (the same tab says `Digital Signer: Not digitally signed`).

Recovery left 2b as it was found: uninstall, cached `oem0.inf`/`.pnf` deleted,
`out\pkg-debug` installed and re-confirmed by a second `findstr` reading the
padded date, root hub back, a mass-storage device enumerated (`slots
enabled=1`, `devices addressed=1`, `SET_ADDRESS interceptions=1`, speed decode
`00010103`, `transfers submitted == completed == 0xAB`, `isr count == claimed
== 0x9C`, `commands issued == completed == 6`, every one of the 80+ error and
failure counters zero).

### Manual Installation on Windows 2000 SP4 (Development)

1. Copy `xhci98.inf` and `xhci98.sys` together into a working directory (the
   INF copies the driver to `%SystemRoot%\system32\drivers\`, i.e.
   `C:\WINNT\system32\drivers\`, alongside the native `usbport.sys`).
2. Device Manager (`devmgmt.msc`) -> the unknown "Universal Serial Bus (USB)
   Controller" under Other devices (`PCI\CC_0C0330`, yellow `?`, Code 1).
3. Right-click -> Properties -> Driver -> Update Driver -> "Display a list..."
   -> Have Disk -> point at `xhci98.inf`.
4. Expect the unsigned-driver warning ("Digital Signature Not Found") and
   click through it; this project does not sign (see below). If the machine's
   driver signing policy is set to Block, no warning appears and the install
   silently fails; check `Control Panel -> System -> Hardware -> Driver
   Signing` first.
5. Reboot if prompted.

Measured during the Phase 3 spike, the same install can be driven straight
from the Found New Hardware Wizard that appears on boot: Next -> "Search for a
suitable driver" -> untick floppy/CD, tick Specify a location -> point at the
staged package directory -> Next through the signature warning. That path
reaches the same `.NTx86` sections as Have Disk.

The Win2000 deploy loop differs from Win98's in three ways worth knowing:

- The run launcher carries its own trace channel. `scripts\setup-qemu-win2k.ps1`
  gives `qemu-win2k-run.cmd` the same `-device isa-debugcon,iobase=0xe9` sink
  and per-boot rotation as the Win98 launcher, writing `vm\win2k-debugcon.log`
  (archiving a non-empty prior trace to `vm\win2k-debugcon.previous.log`). The
  file is separate from the Win98 one on purpose: comparing the two targets is
  the entire point of the Phase 3 gate, and a shared log destroys that.
- Disable / re-enable / uninstall are safe here, unlike Win98. Win2000 also
  unloads the image on disable and reloads it on enable, so a rebuild-retest
  cycle can be driven from Device Manager without a reboot. The
  reload runs `DriverEntry` again, which resets every static counter in the
  trace.
- PnP re-detect (`Action -> Scan for hardware changes`) rebinds the device
  with no wizard once the INF is in `%windir%\inf`.

Two Win2000-only artifacts worth knowing during bring-up:

- `%SystemRoot%\setupapi.log` records every INF section the setup engine
  applied, every file it copied, and every rank decision. When a Win2000
  install "does nothing", read this before theorising; it usually names the
  wrong-section or failed-`CopyFiles` cause outright. Win98 has no equivalent.
- The service the INF creates lives at
  `HKLM\SYSTEM\CurrentControlSet\Services\xhci98`. `Start` there is what the
  Recovery Console's `disable` command edits (see recovery below).

### INF-Based Installation (Option A: usbport miniport)

The INF binds `PCI\CC_0C0330` to `xhci98.sys` as the device's driver; on Win98 via `DevLoader=*ntkern` + `NTMPDriver=xhci98.sys`, the same shape NUSB's `USB2.inf` uses for `usbehci.sys`. `usbport.sys` is not given a service of its own: it loads because `xhci98.sys` imports `USBPORT_RegisterUSBPortDriver` from it (ntkern resolves the import at load). What makes `xhci98.sys` a miniport rather than a monolithic HCD is that registration call at `DriverEntry`, not the registry layout.

This was cross-checked against a shipping miniport on the Win2000 VM, the only one with an EHCI installed: `HKLM\SYSTEM\CurrentControlSet\Services\xhci98` carries `Type=1`, `Start=3`, `ErrorControl=1`, `Group=Base`, `ImagePath=system32\DRIVERS\xhci98.sys`, value-for-value what `Services\usbehci` carries, differing only in the system-assigned `Tag`. The devnode gets `Service=xhci98`, `Class=USB`, `ClassGUID={36FC9E60-C465-11CF-8056-444553540000}`. The 9x `DevLoader`/`NTMPDriver` half has no such reference device on either VM and rests on NUSB's own `USB2.INF`, transcribed below.

#### Verified model: NUSB 3.3 `USB2.INF` EHCI sections

Transcribed from the `USB2.INF` extracted out of the NUSB 3.3
package (`tools/nusb33e.exe` -> `tools/nusb-extracted/USB2.INF`; the file is
Microsoft's USB 2.0 add-on INF, `; Copyright 2001-2003 Microsoft Corporation`,
`DriverVer=09/26/2003,4.90.3000.10`, targeting Win98 SE and WinMe).

Version block and the EHCI device-install sections, verbatim:

```ini
[Version]
signature="$CHICAGO$"
Class=USB
ClassGUID={36FC9E60-C465-11CF-8056-444553540000}
Provider=%Msft%
DriverVer=09/26/2003,4.90.3000.10

[ControlFlags]
ExcludeFromSelect = *

[DestinationDirs]
DefaultDestDir=10
EHCI.CopyFiles         = 10, system32\drivers
HUB20.CopyFiles        = 10, system32\drivers
INF.CopyFiles          = 17

[EHCI]
AddReg=EHCI.AddReg
CopyFiles=EHCI.CopyFiles

[EHCI.AddReg]
HKR,,DevLoader,,*NTKERN
HKR,,NTMPDriver,,usbehci.sys
HKR,,EnumPropPages,,"sysclass.dll,USBControllerPropPage"

[EHCI.CopyFiles]
usbehci.sys
usbport.sys
```

Facts this pins for `src/xhci98.inf`:

- The device's whole registry footprint is three `HKR` values: `DevLoader` =
  `*NTKERN`, `NTMPDriver` = the miniport binary, and a cosmetic
  `EnumPropPages`. There is no `AddService`, and `usbport.sys` appears
  nowhere in the registry; it is copied alongside the miniport in the device's
  `CopyFiles` and loads as an import dependency.
- Destination is dirid 10 + `system32\drivers`, i.e.
  `%windir%\SYSTEM32\DRIVERS`, not `C:\Windows\System\`. Confirmed in the
  Phase 2a VM: after the EHCI-triggered NUSB install, `usbport.sys` +
  `usbehci.sys` + `usbhub20.sys` were present in `C:\WINDOWS\SYSTEM32\DRIVERS\`,
  matching the INF. Hardcode deploy paths to `SYSTEM32\DRIVERS`.
- The root hub PDO that `usbport.sys` creates enumerates with hardware ID
  `USB\ROOT_HUB20` and is bound by the same INF to `usbhub20.sys` (section
  `[ROOTHUB2]`, same `DevLoader=*NTKERN` shape, `NTMPDriver=usbhub20.sys`),
  and `USB\HubClass` likewise to `usbhub20.sys` for external USB 2.0 hubs.
  NUSB installs this INF up front, so the root hub binds without a new INF.
- `[ControlFlags] ExcludeFromSelect = *` keeps the models out of the manual
  picker; installs happen by PnP ID match only.

Hardware IDs `USB2.INF` matches to the `[EHCI]` install (summary; the
per-model `[Strings]` names omitted):

| Manufacturer section | PnP IDs |
|---|---|
| `[Generic]` | `PCI\CC_0C0320` (class-code catch-all, the EHCI analog of this project's `PCI\CC_0C0330`); also `USB\ROOT_HUB20` -> `ROOTHUB2`, `USB\HubClass` -> `Usb2Hub.Dev` |
| `[NEC]` | `PCI\VEN_1033&DEV_00E0` bare and `&REV_01/02/04/05` |
| `[Intel]` | `PCI\VEN_8086&DEV_` `24CD`, `24DD`, `25AD`, `265C`, `27CC`, `283A` |
| `[VIA]` | `PCI\VEN_1106&DEV_3104` bare and `&REV_51/63/82/86/90` |
| `[SIS]` | `PCI\VEN_1039&DEV_7002` |
| `[ALI]` | `PCI\VEN_10B9&DEV_5239` |
| `[NVIDIA]` | `PCI\VEN_10DE&DEV_` `0068`, `0088`, `00D8`, `00E8` |
| `[ATI]` | `PCI\VEN_1002&DEV_` `4345`, `4365`, `4373` (bare and `&REV_80`), `4386` |

`USB2.INF` also has a `[DefaultInstall]` (`CopyFiles=INF.CopyFiles,
EHCI.CopyFiles,HUB20.CopyFiles`) so right-click-Install pre-stages the
binaries and the INF without a device present.

#### Verified model: Windows 2000 SP4 `USB.INF` EHCI service install

Transcribed from `I386\USB.IN_` on the Windows 2000 SP4 retail ISO
(`D:\isos\win2ksp4-retail.ISO`, the same media the import gate's baselines come
from), expanded with `expand.exe`. The file is Microsoft's
`; USB.INF -- This file contains descriptions of all the HCD (USB controller)`,
`DriverVer=06/19/2003,5.00.2195.6717`. Windows 2000 SP4 ships no separate
`usbport.inf`; `USB.INF` is the whole HCD story on that target. The EHCI
sections, verbatim:

```ini
[DestinationDirs]
EHCI.CopyFiles.NT         = 10, system32\drivers

[Generic.Section]
%PCI\CC_0C0320.DeviceDesc%=EHCI,PCI\CC_0C0320

[EHCI.NT]
DriverVer=2/15/2003,5.1.2600.1   ; Date and Ver should be > any preSP4 QFE
AddReg=EHCI.AddReg.NT
CopyFiles=EHCI.CopyFiles.NT,USBUI.CopyFiles.NT,HUB20.CopyFiles.NT
DelFiles=USB.DelFiles.NT

[EHCI.AddReg.NT]
HKR,,EnumPropPages32,,"usbui.dll,USBControllerPropPageProvider"
HKR,,Controller,1,01

[EHCI.CopyFiles.NT]
usbehci.sys
usbport.sys

[EHCI.NT.Services]
AddService = usbehci, 0x00000002, EHCI.AddService

[EHCI.AddService]
DisplayName    = %EHCIMP.SvcDesc%
ServiceType    = 1                  ; SERVICE_KERNEL_DRIVER
StartType      = 3                  ; SERVICE_DEMAND_START
ErrorControl   = 1                  ; SERVICE_ERROR_NORMAL
ServiceBinary  = %12%\usbehci.sys
LoadOrderGroup = Base
```

Facts this pins for the NT half of `src/xhci98.inf`:

- The whole NT load mechanism is `AddService` with flag `0x00000002`
  (`SPSVCINST_ASSOCSERVICE`, what makes the service the device's function
  driver), `ServiceType = 1`, `StartType = 3` (PnP starts it), `ErrorControl
  = 1`, `LoadOrderGroup = Base`, and `ServiceBinary = %12%\<miniport>.sys`.
  No `DevLoader`/`NTMPDriver`; those are 9x-only.
- `%12%` is correct here and nowhere else in the file: the section is
  decorated, so Win98's engine never reads the line (see the dirid-12 trap
  below).
- Microsoft decorates with plain `.NT`, not `.NTx86`. Both work on Win2000;
  `.NTx86` is the more specific match and is what this project uses, since it
  cannot be reached by any non-x86 engine.
- `usbport.sys` again gets no service of its own; it is copied alongside the
  miniport and loads as an import dependency, as on 9x.
- The two `AddReg` values are cosmetic: `EnumPropPages32` names the Device
  Manager property page in `usbui.dll` and `Controller` is that page's
  "I am a host controller" marker (the OHCI and UHCI NT sections carry the
  same pair; the hub sections carry only the prop page).

#### The finished file: `src/xhci98.inf`

Authored from the two reference INFs above; nothing in it is written from
memory. Shape:

| | Section | Contents |
|---|---|---|
| Both | `[Version]` | `$CHICAGO$`, `Class=USB` + the existing USB ClassGUID, `LayoutFile=layout.inf` ("The files the OS supplies" below), `DriverVer` per "Versioning the driver" above (the number moves, so read it out of `src/xhci98.inf` rather than from this row) |
| Both | `[XhciModels]` | `%XhciDesc%=Xhci.Dev,PCI\CC_0C0330`, one class-code entry, the analog of the references' `PCI\CC_0C0320` |
| Win98 | `[Xhci.Dev]` | `AddReg=Xhci.AddReg`, `CopyFiles=Xhci.CopyFiles,Xhci.CopyW98` |
| Win98 | `[Xhci.AddReg]` | `HKR,,DevLoader,,*NTKERN` + `HKR,,NTMPDriver,,xhci98.sys` |
| Win2000 | `[Xhci.Dev.NTx86]` | `AddReg=Xhci.AddReg.NT,Xhci.AddReg.Global` (the second since 1.0.0.2, the NT half of the `DisableSelectiveSuspend` write), `CopyFiles=Xhci.CopyFiles,Xhci.CopyNT` |
| Win2000 | `[Xhci.Dev.NTx86.Services]` | `AddService=xhci98,0x00000002,Xhci.AddService` |
| Win2000 | `[Xhci.AddService]` | `ServiceBinary=%12%\xhci98.sys`, type 1, start 3, error 1, `LoadOrderGroup=Base` |
| Shared | `[Xhci.CopyFiles]` | `xhci98.sys,,xhci98.tmp` -> `10, System32\Drivers` |
| Win98 | `[Xhci.CopyW98]` | `usbd.sys,,,16` and `usbhub.sys,,,16` -> `10, System32\Drivers`, both fetched from the OS's own install source through `LayoutFile` (neither is in `[SourceDisksFiles]`). The second is Windows 98's composite parent; on the NT targets the same name is the OS's own hub driver, and the NT row copies it too. |
| Win2000 | `[Xhci.CopyNT]` | `usbport.sys,,,16`, `usbd.sys,,,16` and `usbhub.sys,,,16` -> `10, System32\Drivers`, from `Driver Cache\i386` through `LayoutFile`. `usbd.sys` alone until 1.0.0.2; an NT install that never had a USB controller has none of the three (the Windows XP guest of 2026-09-03) |
| Both | `[DefaultInstall]` / `[DefaultInstall.NTx86]` | right-click pre-stage; the 9x one also copies the INF to `%17%` |

Four decisions in it depart from the references, each for a reason that would
otherwise cost a debug cycle:

- No `[ControlFlags] ExcludeFromSelect`, which both references set. It only
  hides a model from the manual device-selection list, and the documented
  development install on both targets goes through that list (Win2000 "Have
  Disk", Win98 "Specify a location"). It cannot affect whether `usbport.sys`
  binds, so the risk of it suppressing the install path is all cost and no
  benefit.
- No `EnumPropPages` / `EnumPropPages32` / `Controller`. Those name
  property-page providers in `sysclass.dll` (9x) and `usbui.dll` (NT), files
  placed by USB installs that never ran on an xHCI-only machine. That is the
  same absent-dependency shape as the missing `usbd.sys` Phase 2b tripped over,
  for a purely cosmetic Device Manager tab. Reversible in one line if the tab
  turns out to be wanted.
- `usbport.sys` is not copied on the Windows 98 path, and `usbhub20.sys` on
  neither, unlike both references. On Windows 98 the USB 2.0 stack (NUSB or
  SweetLow's) places `usbport.sys` unconditionally and that OS's `layout.inf`
  has no row for it, so the engine could not resolve one. On the NT targets
  it is the OS's own and is copied since 1.0.0.2 through the same
  `LayoutFile` route as the next section's files; the belief until then,
  that SP4 places it natively, held only because every Windows 2000 vehicle
  had an EHCI (the Windows XP guest of 2026-09-03: Code 39). `usbhub20.sys`
  is the OS's to place: Windows 2000's own `USB.INF` copies it when usbport
  creates the `USB\ROOT_HUB20` PDO, and Windows XP has no such file.
- The `CopyFiles` third field is set (`xhci98.sys,,xhci98.tmp`).
  Reinstalling over a loaded `xhci98.sys` is the normal case in the deploy
  loop, and without a temporary name Win98 has no way to replace a file in use;
  it can leave the previous binary in place, which reads as a code change that
  did nothing.

#### The files the OS supplies: `usbport.sys`, `usbd.sys` and `usbhub.sys`

`usbhub20.sys` imports `USBD.SYS` on both targets and nothing on an xHCI-only
machine ever places that file, so the install has to see to it. The full
diagnosis, a boot-time `c000026c` / `0xc0000034` that names `usbhub20.sys`
rather than the file actually missing, is in `docs/contributing/lessons.md`,
"`usbhub20.sys` bugchecks Win2000". Re-confirmed with `dumpbin`:

- both `usbhub20.sys` builds (NUSB `5.00.2195.6891`, SP4 `5.00.2195.6681`)
  import `ntoskrnl.exe`, `HAL.dll`, `WMILIB.SYS` and `USBD.SYS`;
- they need exactly four `USBD.SYS` symbols:
  `USBD_GetPdoRegistryParameter`, `_USBD_ParseConfigurationDescriptorEx@28`,
  `_USBD_CreateConfigurationRequestEx@8`, `USBD_CalculateUsbBandwidth`;
- `usbd.sys` is a leaf on both targets, importing only `ntoskrnl.exe` and
  `HAL.dll`, so supplying it closes the chain rather than opening a new one.

`usbhub.sys` is the same story on Windows 98: it is that OS's composite
parent, absent for the same reason, and without it every multi-interface
device stops at "USB Composite Device", Code 2 (issue 03; batch 13-E measured
it on the E460). Under SweetLow's stack the parent is his `usbccgp.sys` and
the file is inert. On the NT targets the name belongs to the OS's own hub
driver, and until 1.0.0.2 this section said it was "placed from `driver.cab`
by every install, so the Windows 2000 path must not ask for it". That was
wrong. Both NT targets' `layout.inf` give it the text-mode disposition that
does not copy it at Setup (the table below), the Phase 2d Windows 2000
listing shows `usbhub20.sys` and no `usbhub.sys`, and the Windows XP guest
of 2026-09-03 had none; so the NT path asks for it too, with the same flag.
The composite-parent half of the old reasoning stands: on the NT targets
that role is `usbccgp.sys`'s.

`usbport.sys` is the NT targets' third, and the one that stops the driver
loading at all: `xhci98.sys` imports it, and an unresolved import is Code 39
with nothing on the debug console (the Windows XP guest's first boot, from
the 1.0.0.1 package). Windows 98 differs only because NUSB or SweetLow's
stack places the file unconditionally and its `layout.inf` has no row for
it, so the Windows 98 path does not name it and the gate refuses a path
that does (`OS-ONWIN98`).

What the two NT CDs say, read statically on 2026-09-03 (7-Zip on the ISOs,
`expand` on the `.IN_` files; nothing executed). The last three fields of a
`layout.inf` row are the text-mode Setup disposition: `,4,1,3` is "do not
copy", `,4,0,0` is "copy":

| File | Windows 2000 SP4 `layout.inf` | Windows XP SP3 `layout.inf` |
|---|---|---|
| `usbport.sys` | `= 2,,138288,,,,,4,1,3` (disk 2 = `sp4.cab`) | `= 100,,143872,,,,4_,4,1,3` (disk 100 = the service pack source) |
| `usbhub.sys` | `= 2,,40176,,,,2_,4,1,3` | `= 100,,59520,,,,4_,4,1,3` |
| `usbhub20.sys` | `= 2,,49776,,,,,4,1,3` | no row |
| `usbd.sys` | `= 2,,20688,,,,2_,4,1,3` | `= 1,,4736,,,,4_,4,1,3` |
| `usbehci.sys` | `= 2,,19728,,,,,4,1,3` | `= 100,,30208,,,,4_,4,1,3` |
| `usbcamd.sys`, `usbintel.sys` | `,4,0,0` | `,4,0,0` |

So on both NT targets `usbport.sys`, `usbhub.sys` and `usbd.sys` reach the
disk only when a USB controller's own install pulls them from
`Driver Cache\i386` (`sp4.cab` beside `driver.cab` on Windows 2000, `sp3.cab`
on XP), an xHCI-only machine has no such controller until this package
loads, and this package cannot load without them. The XP guest's disk,
extracted on the host after the 1.0.0.1 install, held `usbcamd.sys`,
`usbintel.sys`, the `usbd.sys` that INF's own row had placed, and no
`usbport.sys` or `usbhub.sys` anywhere, `dllcache` included. `usbhub20.sys`
is deliberately on no path: Windows 2000 SP4's own `USB.INF` binds
`USB\ROOT_HUB20` to it and its `[ROOTHUB2.NT]` section copies it from the
cache when usbport creates that PDO (how every Phase 2 image got the file),
and XP has no such file (its `usbport.inf` binds `ROOT_HUB20` to
`usbhub.sys` and copies `usbhub.sys` and `usbd.sys` with it). The owner's
decision of 2026-09-03 was to leave it out and read the root hub coming up
on clean guests of both NT targets (roadmap tasks 19.4 and 19.5); the gate
refuses a path that names it (`OS-NEVER`).

All three are the operating system's own files, so since release 1.0.0.1
(1.0.0.2 for the NT path's `usbport.sys` and `usbhub.sys`) the INF takes
them from the operating system's own install source rather than carrying
them:

```ini
[Version]
LayoutFile=layout.inf

[SourceDisksFiles]
xhci98.sys=1
xhci98.inf=1

[Xhci.CopyW98]                             ; Win98 reads this
usbd.sys,,,16
usbhub.sys,,,16
[Xhci.CopyNT]                              ; Win2000 and XP prefer this
usbport.sys,,,16
usbd.sys,,,16
usbhub.sys,,,16
```

The mechanism is `LayoutFile`. A `CopyFiles` entry whose file the INF's own
`[SourceDisksFiles]` does not name is resolved through the OS's `layout.inf`
(`C:\WINDOWS\INF\LAYOUT.INF` records `usbd.sys=5`, i.e. `BASE5.CAB`;
`%SystemRoot%\inf\layout.inf` on Windows 2000), and the engine fetches it
from the Windows source path: `C:\WINDOWS\OPTIONS\CABS` when the CABs are on
disk (OEM installs, Windows 98 QuickInstall), otherwise an "Insert Disk"
prompt naming the Windows 98 Second Edition CD-ROM; on the NT targets,
`Driver Cache\i386`, which every install has (`driver.cab` and the service
pack's own cab, `sp4.cab` or `sp3.cab`, beside it), so no prompt. This
is how the OS's own INFs get their files (Windows ME's `USB.INF` spells it
`LayoutFile=Layout.inf, Layout1.inf, Layout2.inf`), and it is what the HID
wizard does on the 2a guests when it takes `hidusb.sys` from `E:\WIN98`.
Each target therefore gets its own OS's build by construction, and the split
the 1.0.0.0 media kept by name (`usbd98.sys` / `usbd2k.sys`, hashed against
a manifest) no longer exists.

Observed on 2026-09-02 (roadmap Phase 17, task 17.1): on the Windows 98
guest running SweetLow's stack, reverted to no driver and no `usbd.sys` or
`usbhub.sys`, the Device Manager install from a package built this way
raised "Insert Disk" naming the Windows 98 Second Edition CD-ROM, not the
xhci98 disk; after the copy and a relaunch the root-hub callbacks followed
`StartController` and the keep-alive mouse was bound. The same evening, on a
fresh clone of `win98.img @ post-nusb` (NUSB 3.3's stack) with the real
1.0.0.1 package: the same CD prompt, the same load and root hub, and a
hot-plugged two-interface `usb-audio` bound as "USB Composite Device" with
"USB Audio Device" beneath it, no Code 2, so `usbhub.sys` arrived by the same
route. And on a fresh clone of `win2k.img @ phase2b-clean` with the same
package, Have Disk installed and started the driver without a reboot, the
root-hub callbacks followed and the mouse was bound; the owner drove the
install and reported no disk prompt, and the file's version on disk was not
read back. Roadmap Phase 17, task 17.1, has the three readings.

What if the target already has the file? Leave it alone: flag `16`.
`COPYFLG_NO_OVERWRITE` is "do not copy if file exists on target"
(`C:\NTDDK\inc\SETUPAPI.H`, the local DDK's own header). Presence is the
entire requirement: an existing `usbd.sys` is by definition that OS's own
build or a newer serviced one, and both builds export all four symbols
`usbhub20.sys` needs. A machine that ever had a USB controller Windows
recognised already has all of them and is asked for nothing. The
alternatives all lose
something:

| Flag | Behaviour | Why not |
|---|---|---|
| none | engine-defined version arbitration | The two setup engines do not arbitrate alike, and the file is the OS's own already |
| `4` `COPYFLG_NOVERSIONCHECK` | overwrite regardless of version | Downgrades a post-SP4 hotfix `usbd.sys` that `usbhub.sys` and every USB client also bind to |
| `32` `COPYFLG_NO_VERSION_DIALOG` | do not copy if target is newer | Still replaces an equal-or-older file for no benefit, and asks for the CD to do it |
| `64` `COPYFLG_OVERWRITE_OLDER_ONLY` | same, by version equality | Same objection |
| `16` `COPYFLG_NO_OVERWRITE` | skip if present | Chosen |

The gate enforces the wiring, because every way of breaking it is silent:
`scripts/inf-gate/check-inf.ps1`'s `OS-*` family (`OS-LAYOUT`, `OS-MEDIA`,
`OS-MISSING`, `OS-ONWIN98`, `OS-NEVER`, `OS-DUP`, `OS-SRCNAME`, `OS-FLAGS`,
`OS-DEST`, `OS-DEFAULT`) requires `LayoutFile=layout.inf`, no Microsoft file
in `[SourceDisksFiles]` under its own or a 1.0.0.0 media name, `usbd.sys`
and `usbhub.sys` on both device-install paths and both right-click paths,
`usbport.sys` on the NT ones and not the Windows 98 ones, `usbhub20.sys` on
none, each under its own name with flag 16 and no overwrite flag to
`10, System32\Drivers`; `PKG-MSFILE` refuses a staged package holding one.
The `SUSP-*` rules (`SUSP-MISSING`, `SUSP-DUP`, `SUSP-VALUE`) require each
of the four install routes, device install and right-click Install on each
target, to write `Services\USB\DisableSelectiveSuspend` once, as a DWORD 1.
`test-inf-checks.ps1` watches each fire.

The package. One flat, 8.3-clean directory serves both targets, and it is
this project's two files:

```
xhci98.inf   xhci98.sys
```

```
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor debug
```

It assembles `out\pkg-<flavor>\` and runs the INF gate against the finished
directory, so a package is never less gated than the binary in it. A copy
taken from `releases\<version>\<flavor>\` is the same two files. The
reference copies of the two `usbd.sys` builds and Windows 98 SE's
`usbhub.sys` are still staged under the git-ignored `tools\` by
`scripts\package\extract-usbd-sources.ps1`, for the import gate's Windows 98
evidence and the Windows 2000 VM setup, and are packaged by nothing.

Where each file goes is not the packager's decision. `[SourceDisksFiles]`
may place a source file in a subdirectory, and a package staged at one path
but gated at another verifies nothing. So `make-package.ps1` asks
`check-inf.ps1 -EmitMediaLayout` for the layout that script's own parse
produces and stages against it, rather than parsing `[SourceDisksFiles]` a
second time. Two parsers would be free to disagree; one cannot. That call
also gates the INF before anything is copied, which is the right order,
since there is nothing to gain from staging a package around an INF that
will be rejected, and it is why `-SkipPackageGate` skips only the
post-staging check.

The binary gates run here too, not only in the build wrapper. Before
anything is staged, `make-package.ps1` runs `test\run-host-tests.cmd` and
`check-imports.ps1` against the `.sys` it is about to package. That is
intended duplication: `scripts\build-driver.cmd` already runs both, but a
binary can reach `src\obj*\i386\` without it (`scripts\local\ddk-debug.cmd`
gives an interactive DDK prompt), and this is the step that produces media a
VM is installed from. Roadmap Phase 4 task 1 requires both gates before
every deploy; running them where the media is assembled makes "gated" a
property of the package rather than of how it happened to be built.

`-SkipBinaryGates`
exists for the packager's own self-tests, which stage text stand-ins; never
pass it for media a VM will see. `-NoTargetEvidence` passes through to the
import gate on a host with no extracted target binaries staged.

`scripts\package\test-package.ps1` holds that to account with stand-in files
(no build, no staged media, no VM; the suite prints its check count when it
passes): a `[SourceDisksFiles]` subdirectory is staged into and not also
flattened, an entry with no source is refused by name, an INF the gate
rejects leaves no output directory behind, a stand-in driver is refused when
the binary gates are left on, and a relative `-OutDir` resolves against the
caller's location.

That last
one is not hypothetical: `[Path]::GetFullPath` resolves against the process
directory, which `Set-Location` does not update, so before the fix a bare
`-OutDir pkg` from a session that had changed directory staged the package
somewhere the caller never named. `scripts\build-driver.cmd` runs this suite
alongside the other self-tests.

`scripts\build-driver.cmd` does not package; it prints the packaging
command on success instead.

One residual, recorded rather than solved: `usbhub20.sys` also imports
`WMILIB.SYS`, which is not on the Win98 SE CD (`layout.inf` has no row for
it) and is not in the NUSB package. Oney (ch.10 notes, p.285) says WMILIB was
missing only from the original Windows 98 retail release, and NUSB is a
working Win98 SE package, so SE resolves it some other way; the `ntkern.vxd`
name-table scan is silent on it, which is no information (that scan has known
false negatives). This is a risk confined to the original Win98 retail
release, which this project does not target, and there is no file on the SE
media for the INF to ask for even if it were.

The keys were not confirmed against a live installed EHCI device when the INF
was authored. Neither VM has an EHCI controller (Phase 2a established none is
needed), so no such device key existed to read, and the INF that would have
created it is the stronger source; it is quoted verbatim above for each
target. The install on each target, and the device key read afterwards, is
where that confirmation lands.

#### Post-authoring gate: `scripts\inf-gate\check-inf.ps1`

Both setup engines fail quietly. Win98 has no log at all, and a Win2000
install that creates no service looks the same in Device Manager as a driver
that loaded and failed. So the parser restrictions below are enforced as a
build-time check rather than trusted to review. `scripts\build-driver.cmd`
runs it on every build, after `scripts\inf-gate\test-inf-checks.ps1`.

That self-test re-runs the gate against broken copies of the real INF (a
`$Windows NT$` signature, a UTF-16 file, LF line endings, a 29-character
section name, a duplicate section, a dirid-12 or wrong driver destination, a
stray `%12%`, missing `SourceDisksNames`, a missing `.NTx86` section, a
missing `.NTx86.Services`, a wrong `AddService` flag, invalid service
type/start/error values, a `ServiceBinary` naming a file no `CopyFiles`
delivers, a removed `DevLoader`, an NT-only INF, an undefined `%string%`, a
non-8.3 name, every way of collapsing the per-target `usbd.sys` split, and
more) plus staged-package cases, and asserts that the specific rule
fails: no build, no VM, no Microsoft binaries, and the suite prints its check
count when it passes.

The package cases use
stand-in files and a stand-in manifest so they run on a host with nothing
staged under the git-ignored `tools\`.

Rule ids are grouped by the failure they prevent:

- `FILE-*`: encoding and line endings.
- `W98-*`: the Win98-parser traps listed below.
- `BOTH-*`: the rules both engines share (signature, class, resolvable
  section cross-references, `DestinationDirs` coverage and driver-directory
  placement, `SourceDisksNames`/`SourceDisksFiles` coverage, defined
  `%strings%`).
- `PATH-*`: the two install paths themselves, including the NT service's
  required type/start/error values.
- `TGT-*`: the per-target `usbd.sys` (each path delivering it from its own
  distinctly-named media file, through its own `CopyFiles` section, with
  `COPYFLG_NO_OVERWRITE` and no version-based overwrite flag, to
  `System32\Drivers`, and, cross-checked against
  `scripts\package\usbd-sources.expected`, from the build that belongs to
  that target).
- `W98-*` also covers the Win98-only `usbhub.sys`: delivered by the Win98
  paths, never by the Windows 2000 ones.
- `VAL-*`: the per-device registry values the miniport reads, present on
  both paths with the required type and default.
- `PKG-*`: the staged-package checks `-PackageDir` runs, below.

`-PackageDir` checks a staged media directory against
`[SourceDisksFiles]` and authenticates each per-target file by SHA-256
(`PKG-*`), at the exact path that file's `[SourceDisksFiles]` entry selects;
a same-named file at the package root must not be able to stand in for a
substituted one in a subdirectory. `-EmitMediaLayout` writes that same
resolved layout out, which is how `scripts\package\make-package.ps1` knows
where to stage each file without parsing `[SourceDisksFiles]` itself.

`-EmitFootprint` emits every file and registry entry one install claims to
place, per install path, with a per-row uninstall verdict derived from the
row's flags: `COPYFLG_*`, `FLG_ADDREG_*` and `SPSVCINST_*`, constants and
descriptions taken from `C:\NTDDK\inc\SETUPAPI.H` rather than from memory.

The verdicts mean less than they look. `remove` means this install wrote what is
there now, not that nothing was there before; a plain unflagged `CopyFiles`
can replace an existing file and an INF cannot say whether it did. `keep` is
a write conditional on what the target already held (`NO_OVERWRITE`,
`REPLACEONLY`, a `NOCLOBBER` / `OVERWRITEONLY` registry row), so this install
may not have written it. `review` is a version-conditional copy
(`NO_VERSION_DIALOG`, `OVERWRITE_OLDER_ONLY`), an `APPEND` or `KEYONLY`
registry row, and any unrecognised flag bit, which is reported rather than
treated as zero. `none` is a row that places nothing (`DELVAL`). A service row expands to everything
the engine writes into the service key (`DisplayName`, `ServiceType`,
`StartType`, `ErrorControl`, `LoadOrderGroup`), not just `ServiceBinary`.

Engine-owned residue (`xhci98.tmp`, `oemN.inf`) is excluded, because a
derivation that mixes in measurements is neither. The output is tracked as
`scripts\inf-gate\expected-footprint.txt` and compared by the gate's own
self-tests, so an INF change that moves the footprint fails the build rather
than being discovered on a guest.

#### Why the INF must carry both install paths

Both Win98 SE and Win2000 SP4 are first-class targets served by one package,
and the two setup engines do not share a loading mechanism. This is the
rationale behind the file above; the gate enforces it as `PATH-W98` and
`PATH-NT`.

| | Win98 SE | Win2000 SP4 |
|---|---|---|
| Section names | Undecorated (`[Xhci.Dev]`); decorated names are ignored outright | `.NTx86`-decorated preferred, most-specific match wins |
| How the driver loads | `HKR,,DevLoader,,*NTKERN` + `HKR,,NTMPDriver,,xhci98.sys` | `[Xhci.Dev.NTx86.Services]` with `AddService=xhci98,0x00000002,Xhci.AddService` and `ServiceBinary=%12%\xhci98.sys` |
| Signature check | None at all | Unsigned-driver warning dialog at install; expect and click through it during bring-up |
| Destination | Spell it `10, System32\Drivers`, never dirid `12`, which resolves to `\Windows\System\Iosubsys` on 9x | `10, System32\Drivers` resolves correctly here too |

One measured wrinkle in the "Section names" row: Windows 2000 records the
undecorated name in the driver key. Batch 13-L's 2b boots read `InfSection =
Xhci.Dev` on both of that guest's driver keys, although the INF carries
`[Xhci.Dev.NTx86]` and the NT engine installs from it. So the undecorated name
is not "the 9x one"; it is what both targets write, and a tool that matched
only the decorated name on the NT path would stop recognising Windows 2000
keys at all. What is measured is the value in the key; the likely mechanism,
that the engine records the section as named in `[XhciModels]` and applies the
decoration while processing it, is an explanation and not a finding.

A single-path INF does not half-work; it produces the phase's worst failure
mode. An INF with only the Win98 sections installs on Win2000 too: Win2000
falls back to the undecorated section, creates no service, and leaves a devnode
whose driver never loads, which looks like a registration failure. An INF with
only `.NTx86` sections leaves Win98 with nothing to install at all.

`Signature="$CHICAGO$"` is correct for both and must not be changed to
`$Windows NT$`; `$CHICAGO$` works on every WDM platform (Oney p.383). Point
both install sections at the same `CopyFiles` section so the file list cannot
drift between targets (the `AddReg` genuinely differs: `DevLoader`/`NTMPDriver`
are 9x-only and mean nothing in a Win2000 device key), and keep every section
name within Win98's 28-character limit.

Digital signing is out of scope for this project: Win98 ignores signatures
entirely, and on Win2000 an unsigned driver installs with a warning rather
than a refusal. Signed drivers do outrank unsigned ones in driver ranking
(Oney p.396), which matters only if a signed INF ever also claims
`PCI\CC_0C0330`; none does today.

#### Win98 INF-parser traps (silent-wrong-answer class)

Source: Oney ch.15 (p.386, 387, 417-419), a 2003 secondary source. The `$CHICAGO$` / `DestinationDirs=10` / `DevLoader`+`NTMPDriver` choices in `src/xhci98.inf` already satisfy these, and `scripts\inf-gate\check-inf.ps1` fails the build if an edit stops satisfying them (`W98-*` rules). This list explains why, so a future edit does not reintroduce a silent failure. Win98 and Win2000 use entirely different setup engines and the single INF must satisfy both.

- Never use directory code `12` in a shared `CopyFiles`. `12` resolves to `\WINNT\System32\Drivers` on Win2000 but `\Windows\System\Iosubsys` on Win98/Me, so it installs the driver to the wrong directory on Win98 with no error. Always spell it `10, System32\Drivers`, which resolves correctly on both (this is why the sections above use `10`).
- 8.3-clean source paths. Win98 setup throws a spurious "can't find the file" dialog when a file or source-path component exceeds 8 characters, even when the file is present. Reference the MSVC6 build output in short-name form in `[SourceDisksFiles]` (Oney's samples use `objchk~1\i386`, not `objchk_wxp_x86\i386`).
- 28-character section-name limit on Win98's parser. Keep every INF section name <= 28 chars.
- Win98 does not merge identically-named sections; the first one wins (Win2000 merges them). Do not rely on additive same-name sections.
- Undecorated vs `.NTx86` sections. Win98/Me ignores decorated section names entirely; Win2000 appends `.NTx86` and prefers the most specific match. Provide undecorated sections for Win98 and `.NTx86` variants for Win2000, both pointing at shared `AddReg` sections. (Decorated `[Strings]` and Unicode INFs are ignored on Win98 too.)
- Redeploy over a loaded driver. When `xhci98.sys` is already loaded, supply a temporary name in the third `CopyFiles` field and Win98 renames it into place on the next reboot (Win2000 generates the temp name automatically). Relevant to the deploy loop: reinstalling over a live `xhci98.sys`.
- Win98 needs a `[ClassInstall]` only to define a new setup class; this project reuses the existing `USB` class (ClassGuid `{36FC9E60-...}`), so no `[ClassInstall]` is required. Win98 also uses no driver signatures, so there is no WHQL friction during bring-up.
- `NTMPDriver` takes an ordered list, and that list is the only load-order control on 9x. `HKR,,NTMPDriver,,"first.sys,xhci98.sys"` makes NTKERN load `first.sys` (and run its `DriverEntry`) before `xhci98.sys`, with no reboot required (Oney p.442). This project's INF names a single driver and should keep it that way; the fact is recorded because it is the delivery mechanism for the export-table stub described in `docs/usb-xhci-info/win98-wdm.md` ("Imports are a silent load-time gate"), which is the last-resort answer if one unavoidable symbol turns out to be missing on the Win98 target.
- No programmatic install path on Win98. `SetupCopyOEMInf` and `UpdateDriverForPlugAndPlayDevices` do not exist there (Oney p.417-418); Win98's setup engine is 16-bit and its class installers/property pages must be 16-bit DLLs. The deploy loop therefore stays file-copy plus Device Manager, and no install automation should be written against the Win2000-era setup APIs and assumed to work on the primary target.

<details>
<summary>Option B fallback INF (monolithic HCD - only if the Phase 3 spike fails)</summary>

```ini
[Install.Services]
AddService=xhci98,0x00000002,ServiceInstall

[ServiceInstall]
DisplayName=%SvcDesc%
ServiceType=1        ; SERVICE_KERNEL_DRIVER
StartType=3          ; SERVICE_DEMAND_START
ErrorControl=1
ServiceBinary=%12%\xhci98.sys

[AddRegistry]
HKR,,DevLoader,,*ntkern
HKR,,NTMPDriver,,xhci98.sys
```

This makes `xhci98.sys` a standalone HCD that must itself create the root hub PDO and handle `IOCTL_INTERNAL_USB_*`, the work Option A delegates to `usbport.sys`.
</details>

### Recovering from a driver that crashes at boot

Once the INF binds the device, both targets load `xhci98.sys` on every boot,
and on both the first recovery step is the same: revert the qcow2 snapshot
taken before the install (see "VM snapshots"). It is the fastest route, and
the reason to always take one. `phase2a-usbd-ok` is the Win98 baseline;
`phase2b-clean` is the Win2000 one. Everything below is for real hardware, or
for a VM whose snapshot is stale.

The two targets diverge after that, and Win2000 is where this bites hardest: it
bugchecks on faults Win98 absorbs (see `docs/usb-xhci-info/win98-wdm.md`, "Win2000 enforces
what Win98 silently forgives").

#### Win98 SE

1. Win98 Startup Menu: press and hold Ctrl (or tap F8) while the VM
   boots. Set `BootMenu=1` under `[Options]` in `C:\MSDOS.SYS` during Phase 2a
   so the menu always appears; it is hard to hit the key window in QEMU.
2. Safe Mode (menu option 3): Safe Mode does not load PnP/PCI drivers, so
   `xhci98.sys` will not load. Open Device Manager, select the xHCI device, and
   Remove it (or point it at no driver), then reboot.
3. Command prompt only (menu option 5): neutralize the binary directly:
   `ren C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS XHCI98.BAD`. Next boot, the device fails to
   start with a yellow `!` instead of crashing the system.
4. To force a clean re-install prompt later, also delete the cached INF copy
   from `C:\WINDOWS\INF\OTHER\` (Win98 stashes third-party INFs there) and
   remove the device in Device Manager; on the next PnP scan Win98 asks for a
   driver again.

Registry pointer for INF debugging: a 9x device's software key lives under
`HKLM\System\CurrentControlSet\Services\Class\USB\000n` (the devnode's
`Driver` value names it). Inspect the NUSB EHCI device's key there to see the
exact `DevLoader`/`NTMPDriver` values the INF must reproduce.

#### Windows 2000 SP4

Win2000 has no equivalent of `MSDOS.SYS BootMenu=1`; the F8 boot menu is
always available (tap it as the "Please select the operating system to start"
line or the OS-loader countdown appears). In escalating order:

1. Last Known Good Configuration (F8 menu). Reverts
   `HKLM\SYSTEM\CurrentControlSet` to the control set from the last boot that
   reached a successful logon, which undoes the service entry the INF just
   created. This is the cheapest fix and it expires the moment you log on
   successfully after the bad install, so use it first.
2. Safe Mode (F8 menu). Loads only the drivers enumerated under
   `HKLM\SYSTEM\CurrentControlSet\Control\SafeBoot\Minimal`, which a
   third-party USB miniport is not in, so `xhci98.sys` stays unloaded. Then
   Device Manager -> the xHCI device -> Disable or Uninstall, or set
   `Services\xhci98\Start` to `4` (disabled) in `regedit`, and reboot.
3. Recovery Console (boot the SP4 CD -> R -> Recovery Console, or a
   pre-installed one from `winnt32 /cmdcons`). Use it when Safe Mode itself
   bugchecks, which it will if the fault is in `DriverEntry` or in the
   registration path, since the usbport stack is what fails:

   ```
   listsvc                     (confirm the service name)
   disable xhci98              (sets Start = SERVICE_DISABLED; it prints the
                                previous value - write it down to restore)
   ren C:\WINNT\system32\drivers\xhci98.sys xhci98.bad
   exit
   ```

   `disable` is the reliable one; renaming the binary alone leaves the service
   entry present and produces a load failure the event log records at every
   boot, which is a useful diagnostic but not a fix.
4. To force a clean re-install prompt later, remove the device in Device
   Manager and delete the cached INF pair (`oemN.inf` + `oemN.pnf`) from
   `%SystemRoot%\INF`. Identify which `oemN` is yours by searching them for
   `CC_0C0330`, and cross-check `%SystemRoot%\setupapi.log`, which names the
   file it created at install time.

A Win2000 bugcheck also leaves evidence Win98 does not; see "Blue Screen
Analysis" below before reverting the snapshot that destroys it.

## Debugging

### Debug Build Output

In a debug build, `DbgPrint` calls (inside `#if DBG` guards) emit to the kernel debugger, if one is attached.

Win9x kernel debugging caveat: WinDbg's KD protocol does not support Win9x. The Win9x-family kernel debugger is `WDEB386.EXE` (ships with the 9x DDK), and it drives over a null-modem serial link. This project does not use it and does not plan to: no fleet machine has an RS-232 port, so on bare metal there is nothing to attach it to, and inside QEMU the GDB stub below is strictly better and needs no guest-side setup. Recorded so that "Win98 has no kernel debugger" is not read into this: it has one; this project has no vehicle for it. In practice, all three rungs actually used are guest-side or host-side:

- The QEMU port-0xE9 debug console (next section) is the primary trace channel for Phases 3-7.
- Sysinternals `DebugView` (which supports Win9x) can capture `DbgPrint`-style output inside the guest without any kernel debugger.
- The QEMU GDB stub (`-s -S`) gives real breakpoints/memory inspection of the guest without any guest-side debugger at all.

### QEMU Debug Console (port 0xE9) - zero-setup trace channel

QEMU (like Bochs) emulates an I/O "debug port" at `0xE9`: any byte the guest
writes to it appears on the host. Launch with:

```
-debugcon file:vm\debugcon.log      (or -debugcon stdio)
```

In the driver, the debug helper (`xhci_dbg.c`) mirrors every message to the
port, character by character:

```c
WRITE_PORT_UCHAR((PUCHAR)0xE9, c);   /* any IRQL; QEMU-only side channel */
```

The mirror gives reliable printf-style tracing in QEMU with no debugger setup
at all, working during early boot and inside the ISR. It is the debug channel
for every phase that runs in a virtual machine, and it belongs to the `qemu`
flavour alone: `XHCI_DBG_LIVE` is defined by `src\sources` for that flavour
and nothing else, and the import allowlist's one `qemu required` row enforces
it. Build `qemu` for anything that reads this channel.

Whether real hardware decodes port 0xE9 is an open question (defect 2b) and
must not be written here as settled. The writes being ignored is not the same
as the binary being safe: `0.0.0.4`'s debug build gave the E460 a `Code 2` and
loaded nothing, with `HAL.dll!WRITE_PORT_UCHAR` as the sole import delta, and
a build without the `0xE9` pair loaded clean. That clean build was itself a
diagnostic one and so also carried the do-not-deploy marker, so it is not the
matched control (`run-13e.md` P6). Whether the cause is the import failing to
resolve or the port being decoded is still open: the two discriminating
binaries are built and their boots not taken. Either way the mirror lives in
no published binary.

This driver has no serial trace channel, on any vehicle (see the end of
"Getting a trace off a bare-metal machine"). A problem that reproduces solely
on hardware is diagnosed from behaviour there, or on Windows 2000 from
DebugView. That statement is about a serial channel for `xhci98.sys`; the
project owner's reasoning is that a machine new enough to be xHCI-only is
unlikely to have a physical port.

It is not a ban on QEMU's
virtual serial port: the Phase 0 DOS qualifier still mirrors its report to
COM1 under `--serial`, and `xhciqual\test\run-qemu-matrix.ps1` still launches
with `-serial file:` and reads its results out of that log. That is a DOS
program's own output in a virtual machine, not a kernel trace channel on
metal, and it is the Phase 0 matrix runner's only way of getting an answer out
of a headless guest.

### Getting a trace off a bare-metal machine

Two different things are covered here, and the distinction matters. The trace
is the driver pushing a line somewhere as it happens. There is no running
trace on Windows 98 bare metal: DebugView bugchecks that machine at real
interrupt rates, there is no serial sink, and a crash takes whatever the
driver was holding down with it. Roadmap task 12.2 closed on that.

The read channel is the other direction, added in `0.0.0.6`. The log does not
have to be pushed at all: it sits in the miniport extension, and
`XHCISNAP.EXE` reaches in through usbport's PassThru escape and reads it out
of the running machine from ring 3. Three facts about it:

- The read channel ships in every flavour, `release` included, because the
  person whose machine misbehaves is running the build they already have.
- `XhciLogFile` and its ring-0 file sink are retired, with
  `ntoskrnl.exe!ZwCreateFile`, `ZwWriteFile` and `ZwClose`. The two surviving
  registry values are `XhciLogVerbosity` (`DWORD`, a 0-4 ladder, `0` and the
  default meaning the driver does not answer the tool at all) and
  `XhciLogDebugView` (`DWORD`, 0). The file did not disappear: `XHCISNAP`
  writes it, in ring 3, at a path the user names.
- It was measured on metal, not only designed: both builds of `0.0.0.6`
  handed their log to `XHCISNAP` on the E460 under Windows 98 SE at stage L3.

The owners are `docs/contributing/design/08-build-flavours-and-the-log-channel.md`
section 13 for the decision and
`docs/contributing/passthru-snapshot-instrument.md` for the route and the wire
format; do not restate either here. Keep the trace-versus-read distinction
exact in anything added below.

The trace channel writes no file, and neither does anything else in the driver.
`xhciDbgEmit` (`src/xhci_dbg.c`) hands each finished line to two sinks: I/O
port `0xE9`, which QEMU writes straight to the host and which real hardware was
assumed to discard (one of defect 2b's two open readings, not a measured fact;
see the previous section), and `DbgPrint`. There is no third, and there will
not be: most trace sites run at DISPATCH_LEVEL or inside the ISR, where file
I/O is illegal. So on metal the trace is captured by a user-mode agent inside
the guest, and the mechanism is `DbgPrint` reaching it.

Beside the trace sits the log ring (`src/xhci_log.c`), a different thing with a
different shape. An earlier ring-0 writer could flush its contents to an
optional `C:\XHCI.LOG`; that writer is gone and the ring is what `XHCISNAP`
reads. What is true of the ring:

- it is a bounded in-memory ring in the miniport extension, appended from any
  IRQL and never written per line;
- it is flushed only from `StopController`, one PASSIVE-level lifecycle
  callback. A Device Manager disable reaches it on Windows 2000 only: on
  Windows 98 disabling any USB host controller devnode bugchecks the machine
  before the teardown completes (see "Do not disable the controller in Device
  Manager" above), so the stop that flushes there is the shutdown, and the
  start that reads the switch is a reboot. `docs/contributing/runs/run-11v.md`
  stage C carries the per-target sequence. The flush measures
  `KeGetCurrentIrql()` and refuses above PASSIVE_LEVEL, counting the refusal
  rather than trusting a static claim about which callbacks are passive;
- it is off unless the driver's own software key says otherwise. The two
  values are `XhciLogVerbosity` (`DWORD`, the 0-4 ladder, `0` by default and
  `0` meaning the driver answers nothing) and `XhciLogDebugView` (`DWORD`, 0),
  both on both install paths and both checked by the INF gate's `VAL-*` rules.
  Each is read once per start through `UsbPortGetMiniportRegistryKeyValue`,
  with no new import, and where a copy that service performs is unclamped, the
  driver terminates the buffer itself and trusts nothing past the first NUL;
- `XhciLogDebugView` emits the ring through `DbgPrint` from the PASSIVE flush
  only, so `ntoskrnl.exe!DbgPrint` is an `all`-flavour row in the
  import allowlist. It is a bulk dump and may never become live mirroring; see
  AGENTS.md's one stated exception to the `#if DBG` rule.

The retired file sink earns a paragraph for what it measured. `XhciLogFile`
was the first untrusted input this driver ever took, and it reached
`ZwCreateFile` on the boot path.

Batch 11-V stage C found that the
NT-namespace form `\??\C:\XHCI.LOG` does not resolve on Windows 98 at all
(`STATUS_OBJECT_PATH_NOT_FOUND`, taken at `StartController` with the machine
up, so the form is the cause), that the two roots `ntkern.vxd` does contain,
`\DosDevices\` and `\SystemRoot\`, refuse a read with `STATUS_NOT_SUPPORTED`,
and that asking `\DosDevices\` for write access never returns and hangs the
boot inside `StartController`. On Windows 2000 the disable route produced a
complete 690-byte file whose contents matched the driver's live counters, while
the shutdown route silently produced nothing, the volume being already
dismounted (`STATUS_TOO_LATE`).

The sink's three `Zw*` imports carried only `ntkern-name` Windows 98
evidence, the weakest tier this project accepts, because nothing in the Win98
precedent set opens a file; every `Zw*` import there is a registry call. All
three imports left the binary with the sink, and
`scripts/import-gate/test-flavour-rules.ps1` asserts their absence. The path a
user names now reaches `fopen` in ring 3 inside `XHCISNAP` and never the
driver. Full matrix in `docs/contributing/runs/run-11v.md` stage C and
`docs/contributing/lessons.md` batch 11-V stage C.

A ring flushed at stop answers "the device does not work" and "the transfer
path is wrong". It answers nothing about a bugcheck, because the flush never
runs, and a bugcheck is when a user most wants a log. The one exception is the
fail-closed DMA teardown, which flushes before it bugchecks.

The rule for any code written against either channel is unchanged: a per-line
file write, or any write from DISPATCH_LEVEL or the ISR, is forbidden. A
bugcheck in the diagnostic path is worse than having no diagnostic path.

That leaves three channels, in the order worth trying:

1. DebugView, capturing kernel output to a file. This is the practical route
   on both targets, subject to the Windows 98 bare-metal ban below: enable
   Capture Kernel, then File -> Log to File so lines land on the local disk as
   they arrive rather than living in a scrollback that a bugcheck takes with
   it. Windows 2000 runs any current build. Win98 needs an old build, since
   Sysinternals dropped Win9x support years ago; stage it before the trip.

   The 9x-capable build used by this project is v4.64 (`Dbgview.exe` dated
   2007-01-08), obtained from the Internet Archive's capture of the
   Sysinternals download, since the live URL now serves a Win7+/Win10+ build:

   ```
   https://web.archive.org/web/20080901000000/http://download.sysinternals.com/Files/DebugView.zip
   ```

   It lives in the git-ignored `tools/DebugView/`. Do not substitute the
   current download; v4.90 and v5.x will not run on Win98 at all.

   **Do not run DebugView on Win98 bare metal while a device is plugged in.
   It bugchecks the machine.** Measured on the E460 across three device
   classes. With Capture Kernel active, plugging any hub raises
   `fatal exception 0E at 0028:C208D79D`; with DebugView not running, the same
   hub on the same connector enumerates and works (an apparent left/right
   connector dependence was a red herring; the A/B showed DebugView was the
   variable, not the port).

   A USB Audio device on a root port, with no hub in
   the machine at all, bugchecked at `0028:C207B26D`, the same region. A
   Low-Speed HID mouse (`046D:C077`, `bInterval`=10, 4-byte interrupt IN) on
   a root port bugchecked at `0028:C20A3F4D`; that is the lowest-interrupt-rate
   stimulus available at the bench. Full account:
   `docs/contributing/runs/run-13e.md`, "Session record - the DebugView ban,
   measured".

   Two further observations from that session. The crash is at the plug, and
   a mouse has no high steady-state rate, so the variable may be the
   enumeration burst rather than the sustained rate; that is a hypothesis and
   is not established. And the traced build has no steady-state trace to
   capture at all: every per-transfer site is budgeted to 32 prints per site
   and spent within seconds of driver load, and the budgets are driver-image
   statics that no start, stop or resume resets. So on Windows 98 metal you
   cannot start a capture before the driver's only dense trace has already
   happened, the same ordering failure that closed the `XhciLogDebugView`
   route on that target, arriving from the other end of the driver's life.

   The likely mechanism is this driver's `DbgPrint` calls from DPC and ISR
   contexts meeting a Win9x VxD capture hook at real-hardware interrupt rates.
   QEMU survives it because it is far slower and more serialised, so
   the VM result below did not predict this.

   The consequence: Win98 bare metal has no live trace channel, and Win2000
   would be the route to counters on real hardware, since it captures
   `DbgPrint` through the native NT path rather than a Win9x VxD hook. Two
   candidate fixes for the Win98 side were considered and neither was
   written: buffer trace and emit only at PASSIVE_LEVEL, or a COM1 sink. Both
   are closed. The first fails on shutdown ordering (Windows closes the
   capture program before it stops the driver, so the ring's bulk dump is
   handed to nothing); the second has no serial port on any candidate
   machine.

   And the Windows 2000 route has no vehicle: Windows 2000
   Setup bugchecks on both fleet machines and no further candidate is
   available to this project, so read it as the only known route, not an
   available one. Roadmap Phase 13's checkpoint took the branch where it does
   not exist at all.

   The Win9x caveat is about the sink, not the version, and in a VM it works.
   Sysinternals' own documentation says that on Windows 95/98/Me DebugView
   captures the VxD services `Out_Debug_String` and `_Debug_Printf_Service`,
   and captures `DbgPrint` only on NT-based systems. This driver calls
   `DbgPrint`, which on Win98 is NTKERN's WDM emulation, so whether it reaches
   DebugView was an open question.

   Settled in the 2a VM, affirmatively: with
   Capture Kernel enabled, DebugView displays this driver's lines
   (`xhci98: PAGESIZE=...`, `MaxSlotsEn=...`, `layout total bytes=...`), and
   each was cross-checked as present in the port-`0xE9` log taken at the same
   moment. Both sinks carry the same content, so NTKERN does route `DbgPrint`
   into the 9x debug service and `xhciDbgEmit`'s second sink is live on Win98.
   The VM was the place to settle it because `0xE9` is an independent oracle
   sitting beside DebugView, and the E460 has no such control. "The sink is
   reachable" and "the sink is safe" are different claims, and the VM only
   established the first.

   A trap measured in the same session: an idle, device-free Win98 bus turns
   the traced build into a trace flood that looks like a hang. With no USB
   device attached at all, Win98 idle-suspends the controller and something
   wakes it again continuously; every resume is a full reinitialisation (~80
   trace lines) because QEMU implements CRS as "set `SRE`" so the state
   restore always fails. Measured: 469 initialisations against 1
   `StartController`, ~6 KB/s of trace, 70,000 lines, and a guest whose GUI
   stopped repainting, reported as "the VM seems to have hung" when it was
   saturated and fully alive (the trace was still growing, and QEMU burned 8 s
   of CPU in 8 s of wall clock).

   Two things make this readable rather than alarming. `cb ResumeController`
   printed only 4 times, because `XHCI_DBG_CB` is budgeted per site, so the
   callback count in a log is a floor and the reinit count is the real
   measure. And attaching a single device ends it instantly: `device_add
   usb-mouse` took the trace rate from 6,253 bytes/s to 0, which is batch
   7b-V's "an attached hub keeps Win98's controller out of idle suspend"
   arriving from the other direction. On real hardware the amplifier may be
   absent (if CSS/CRS works there, a resume is cheap and no storm follows), so
   do not carry this over as a prediction. Carry the mitigation, which is
   free: attach a device early and do not leave a bare bus idling while
   capturing.

2. No running trace. If DebugView is not in place the run yields behaviour
   and Device Manager screenshots as it happens, because the `CheckController`
   block emits through `DbgPrint` like every other trace site and that only
   exists in the `qemu` flavour. Since `0.0.0.6` this is no longer "nothing":
   counters and the stored log are readable afterwards with `XHCISNAP`, which
   is channel 3.

   A kernel debugger over serial is not an option here, by the project
   owner's decision: no fleet machine has an RS-232 port, and a machine new
   enough to be xHCI-only is unlikely to have one, so it would need a dock or
   an add-in card before it needed anything else. On Windows 98 it would not
   help in any case: WinDbg's KD protocol does not support Win9x, and the
   9x-family debugger is `WDEB386.EXE` over a null-modem link, assembly-level,
   a last resort even when the port exists.

   So on Windows 98 bare metal there are no live trace channels: DebugView
   bugchecks the machine at real interrupt rates, and nothing else prints.
   Roadmap task 12.2 closed on that, and the reason is worth carrying here: a
   second hand-over site would have to run at PASSIVE_LEVEL, and on a Windows
   98 machine running this package there is no PASSIVE moment between
   `StartController` and the shutdown. The idle suspend is switched off by
   `DisableSelectiveSuspend` (task 11-V.6), a disable bugchecks the target,
   and `CheckController` is DISPATCH_LEVEL under usbport's `MiniportSpinLock`.

3. The snapshot read. `XHCISNAP` reads the counters and the stored log out of
   the running machine, on demand, through usbport's PassThru escape, shipping
   in every flavour since `0.0.0.6` and measured on the E460 at stage L3. It
   is the only number a Windows 98 metal run can produce, so a bare-metal
   session is no longer "behaviour only".

   The channel is off by default, so the enabling step comes first, before the
   thing you are trying to capture. `XhciLogVerbosity` defaults to `0`, `0` is
   off outright (the driver answers the tool exactly as a build without the
   channel would), and the value is read once per start. The published order
   is four steps and the order matters:

   ```
   XHCISNAP -verbosity 2        (on every xhci98 controller)
   restart
   reproduce the problem
   XHCISNAP -dump -o C:\NAME
   ```

   So a bare-metal session's first action is the command and the restart, and
   the dump is what happens at the end. Taking the dump at the end of a
   session that was never enabled returns what a machine with no channel
   returns. `xhcisnap/README.md` is the reference;
   `docs/contributing/passthru-snapshot-instrument.md` is the as-built record.

Prove the channel before you trust the run. Every trace this project has
relied on came off port `0xE9`; `DbgPrint` capture has never been the channel
a clause was read from on any target, and on Windows 98 bare metal it has no
safe form at all. DebugView has been used here, in the 2a VM, twice over: it
displayed this driver's lines on batch 7b with `0xE9` beside it as the
independent oracle, and stage H's boot had it capturing 707 lines live. Both
were controlled observations of the channel, not runs whose numbers came off
it.

So the first bare-metal session must confirm the `DriverEntry (built ...)`
banner appears in the capture before any clause is read from it. An absent
banner is unreadable rather than negative; that is the anchor rule in
`docs/contributing/failure-diagnosis.md`, "Reading a trace without fooling
yourself".

Then get the file off the machine the same way the driver got on: pull the disk
and read it on the modern host, burn a CD-R, or use Ethernet if that machine
has a working NIC driver under the target OS. DebugView's own remote-agent mode
can stream to another machine's DebugView and skips the copy entirely; it
needs working networking on the retro machine, which is a per-machine question
(`retro-configs`), not an assumption.

A serial (COM1) sink is ruled out, by the project owner. The technical case
for it was sound: the `qemu` build already imports `HAL.DLL!WRITE_PORT_UCHAR`
for the `0xE9` mirror, so a `0x3F8` sink beside it would add no new import.
But the vehicle does not exist and is not going to: a machine new enough to be
xHCI-only is unlikely to have a physical serial port, and this project will
not plan a validation channel around a dock or an add-in card. Do not
re-propose it.

The consequence is that roadmap task 12.2 closed with no trace
channel at all on Windows 98 real hardware, validated behaviourally there,
with no running trace and no capture of a crash. That is a settled outcome;
`docs/using/release-notes.md` publishes it. It is the last word on the trace,
not on the target: `XHCISNAP` reads the counters and the stored log off a
Windows 98 machine on real hardware.

### QEMU xHCI trace events - the host-side hardware oracle

QEMU's emulated xHCI is itself instrumented: it can log every register
access, TRB fetch, doorbell ring, and interrupt assertion the guest driver
causes, with zero guest-side instrumentation. This answers questions no
in-guest log can ("did my doorbell write reach the controller at all?",
"what TRB did the controller actually fetch?", "did it raise INTx?"). For
Phases 4-7 it is the single most powerful debugging instrument, and it works
even when the driver is too broken to print anything.

Enable at launch (append to the run `.cmd`):

```
-trace "usb_xhci_*,file=vm\xhci-trace.log"
```

Patterns can be narrowed (`-trace "usb_xhci_doorbell_*"`), repeated, or
listed: `qemu-system-x86_64 -trace help` prints every event name the installed
build supports.

Do not repeat `-trace` to add patterns; the trace file silently disappears.
`-trace "usb_xhci_port_*,file=vm\t.log" -trace "usb_xhci_irq_intx"` enables both
events and writes them to QEMU's stdout, leaving no `vm\t.log` at all; the
later argument without `file=` takes the output back. Measured on scoop QEMU
11.0.0, and it fails in the direction that looks like "the trace events do not
exist" rather than like a mistake. Put the patterns in a file instead, one per
line, and pass a single argument:

```
-trace "events=vm\xhci-trace-events.txt,file=vm\win98-qemu-trace.<tag>.log"
```

`screendump` writes a binary PPM (`P6`), not a PNG, whatever extension the
filename carries, so an image tool (or a few lines of `System.Drawing`) has to
convert it before it can be viewed. Same measurement date and build.

Event names verified against QEMU master `hw/usb/trace-events` (re-check with
`-trace help` on the installed QEMU; names are stable but not contractual):

| Phase / question | Events to watch |
|---|---|
| Phase 4 init: are my register writes arriving, in the right order? | `usb_xhci_oper_write`, `usb_xhci_runtime_write` (ERSTSZ/ERDP/ERSTBA order!), `usb_xhci_cap_read`, `usb_xhci_reset`, `usb_xhci_run`, `usb_xhci_stop` |
| Phase 4 interrupts: did the controller assert the line? | `usb_xhci_irq_intx` (must appear; `usb_xhci_irq_msi`/`msix` must not, since Win98 is INTx-only), `usb_xhci_queue_event` |
| Phase 4/5 ports: connect/reset plumbing | `usb_xhci_port_reset`, `usb_xhci_port_link`, `usb_xhci_port_notify`, `usb_xhci_port_read`/`_write` |
| Commands: is the command ring being consumed? | `usb_xhci_doorbell_write` (DB 0), `usb_xhci_fetch_trb`, `usb_xhci_queue_event` |
| Phase 6 enumeration: slot lifecycle as the controller sees it | `usb_xhci_slot_enable`, `usb_xhci_slot_address`, `usb_xhci_slot_configure`, `usb_xhci_slot_evaluate`, `usb_xhci_slot_disable` |
| Phase 6/7 endpoints + transfers | `usb_xhci_ep_enable`, `usb_xhci_ep_kick`, `usb_xhci_ep_stop`, `usb_xhci_ep_reset`, `usb_xhci_ep_set_dequeue`, `usb_xhci_ep_state`, `usb_xhci_xfer_start`, `usb_xhci_xfer_success`, `usb_xhci_xfer_error`, `usb_xhci_xfer_nak`, `usb_xhci_xfer_retry` |
| "Am I using something qemu-xhci does not model?" | `usb_xhci_unimplemented`, `usb_xhci_enforced_limit` |

Reading tips:

- `usb_xhci_fetch_trb` prints the TRB physical address and type. Diff it
  against what the driver believes it enqueued to catch cycle-bit and Link-TRB
  bugs directly (the "works for exactly one ring's worth" taxonomy in
  `docs/contributing/failure-diagnosis.md`).
- A `usb_xhci_doorbell_write` with no following `usb_xhci_fetch_trb` means
  the controller rejected the ring state (halted endpoint, bad dequeue, cycle
  mismatch); the bug is in ring programming, not in the transfer content.
- `usb_xhci_queue_event` with no `usb_xhci_irq_intx` means events are being
  generated but the interrupter is masked (IMAN.IE/USBCMD.INTE): the
  poll-vs-interrupt differential, observable from the host side.
- The trace file grows fast; prefer narrowed patterns once past bring-up.

### Blue Screen Analysis

The DDK debug build generates `.sym` and `.map` files alongside the `.sys`.
Both targets need them, but they hand you very different amounts of evidence,
which is the practical reason to reproduce a shared bug on Win2000.

Win98 SE. The screen is all you get. When Win98 blue-screens (exception
0x0E = page fault, 0x06 = invalid opcode, etc.):
- Note the Exception address (EIP value)
- Load the debug build `.sym` file in WinDbg or similar
- Map EIP to source line using the `.map` file from the debug build

There is no crash dump and no stack walk; if the EIP lands outside
`xhci98.sys`, the trail usually ends there. Capture the screen before
rebooting.

Windows 2000 SP4. The STOP screen names a bugcheck code and up to four
parameters, and the system can write a dump:

- Read the STOP code first; it classifies the bug before any symbol work.
  The ones this driver will actually produce:

  | STOP | Name | Usual meaning here |
  |---|---|---|
  | `0x0000000A` | `IRQL_NOT_LESS_OR_EQUAL` | Touched pageable memory or a bad pointer at DISPATCH_LEVEL or above; a callback did something only legal at PASSIVE |
  | `0x000000D1` | `DRIVER_IRQL_NOT_LESS_OR_EQUAL` | Same, attributed to a driver; parameter 4 is the referencing address, map it with the `.map` |
  | `0x00000050` | `PAGE_FAULT_IN_NONPAGED_AREA` | Freed or never-mapped memory; a stale ring/context pointer after teardown |
  | `0x0000007E`/`0x0000008E` | unhandled exception | The Win2000 equivalent of the Win98 exception screen |
  | `0x000000C4` | `DRIVER_VERIFIER_DETECTED_VIOLATION` | Verifier caught it; parameter 1 is the specific violation class. Expect these once Phase 4 turns Verifier on |
  | `0xC000026C` | driver load failure (not a bugcheck code) | A dependency failed to load: the `usbd.sys` trap from Phase 2b, `docs/contributing/lessons.md`, "`usbhub20.sys` bugchecks Win2000" |

- Configure dumps before you need one: Control Panel -> System ->
  Advanced -> Startup and Recovery. A complete dump to
  `%SystemRoot%\MEMORY.DMP` needs a pagefile at least RAM+1 MB on the system
  volume; a small (64 KB) dump lands in `%SystemRoot%\Minidump\` and is
  enough for a stack and a faulting module.
- Analyse on the host: open the dump in WinDbg, point the symbol path at
  the debug build output, and run `!analyze -v`, then `kb` for the stack and
  `lm` to confirm `xhci98` is loaded where you think. This is kernel-aware
  analysis of a saved crash, and nothing on the Win98 side is comparable, which
  is why `docs/contributing/failure-diagnosis.md` says to prefer WinDbg/KD on
  Win2000 when a bug reproduces on both.
- Win2000 also logs driver load failures to the System event log
  (`eventvwr.msc`, source `Service Control Manager`) even when it does not
  bugcheck; the first place to look when `xhci98.sys` simply never runs.

### USB-IF Backwards Compatibility Reference

The USB-IF xHCI backwards compatibility procedure is not tracked in this
repository; it is licensed "FOR INTERNAL USE ONLY". Fetch your own copy from

`https://www.usb.org/sites/default/files/xHCI_Backwards_Compatibility_Testing_v1_7.pdf`

into the git-ignored `docs/references/xhci-backwards-compatibility-testing-v1-7.pdf`;
`docs/references/README.md` carries the SHA-256 to check it against.

It is not a Win98-specific procedure, so do not treat it as a literal pass/fail script for this project. Use it as a reference for building a meaningful validation tree:

- Test direct root-port attachment for Low-Speed, Full-Speed, and High-Speed devices.
- Include both single-TT and multi-TT High-Speed hubs.
- Put Full-Speed and Low-Speed devices behind High-Speed hubs to exercise transaction-translator fields.
- Mix interrupt, bulk, and isochronous devices so event handling and transfer scheduling run concurrently.
- Add multi-tier hub paths only after one-tier downstream hub support is stable.

Suggested practical device set:

| Device type | Why it matters |
|---|---|
| USB keyboard and mouse | Low-risk interrupt traffic and user-visible behavior |
| USB flash drive or card reader | Bulk transfer correctness and short-packet handling |
| USB Ethernet adapter with a working driver on each target OS | Bulk/control traffic under sustained network load |
| Full-Speed audio headset | Isochronous path once implemented |
| Full-Speed printer or vendor device | Control and bulk traffic behind hubs |
| Single-TT and multi-TT HS hubs | Route string and TT scheduling coverage |

### Target Class Devices

This project is a host controller driver, not a USB class-driver bundle. Function drivers come from each target OS's own sources: the base OS, the Win98 USB supplement, NUSB, or vendor packages on Win98; the base OS or vendor packages on Win2000 SP4. USB Ethernet and USB Audio tests require a device that already has a working class or vendor driver on the target being tested. Coverage on one target says nothing about the other, and this is the single most common way a validation result gets misattributed. If a device enumerates but no function driver binds to it, that is not by itself an xHCI driver failure.

For USB Ethernet, prefer an adapter with a known-working vendor driver on the target under test, and test:
- Adapter enumeration and driver binding.
- DHCP or static-IP traffic.
- Sustained ping and file transfer traffic.
- Unplug while traffic is active.
- Operation behind a High-Speed hub after direct attach works.

For USB Audio, prefer a simple Full-Speed USB Audio device first and test:
- Playback with a short WAV file.
- Recording if the device exposes an input path.
- Long playback to catch isochronous underruns.
- Operation behind a High-Speed hub after direct attach works.

#### USB Audio in QEMU: what the vehicle can and cannot show (batch 9-V)

Measured while running task 9-V.1 on both target VMs. Each of these cost a boot
or a wrong conclusion once.

- Windows 98 SE cannot play USB audio in QEMU at all, and it is not this
  driver. `USBAUDIO.VXD` faults after exactly one 10 ms URB: `fatal exception
  00 at 0028:FF0AAA94 in VXD USBAUDIO(01) + 00002ED4` through `xhci98.sys`, and
  `fatal exception 0E at 0028:FF045748` through a UHCI controller driven by
  Windows 98's own USB 1.1 stack with our miniport idle-suspended and every
  isochronous counter at zero. Reproduced across four attempts (one of them a
  ring-0 wedge instead of a bugcheck), with a byte-identical 1,916-byte host
  capture every time audio crossed at all. Do not spend Win98 boots on USB audio
  in QEMU expecting a different answer. This is a statement about the vehicle
  only: at bench session 3 a physical UAC 1.0 device played clean on the E460
  under Windows 98, at a root port and behind a multi-TT hub (`run-13e.md`
  Finding X).
- Every QEMU USB Audio model is Full Speed, so a standalone `-device usb-ehci`
  cannot host one: the attach is refused with `speed mismatch trying to attach
  usb device ... to bus ehci.0`, because a bare EHCI has no companion
  controller. An audio control controller must be UHCI (`piix3-usb-uhci`) or
  OHCI. This is why the 8-V storage controls could use EHCI and the audio ones
  cannot.
- A UHCI/OHCI control on the Win98 VM needs the Windows 98 SE CD attached
  (`-cdrom`). That VM was installed with no USB 1.1 controller, so `uhcd.sys`
  was never copied to it and `C:\WINDOWS\OPTIONS\CABS` does not contain it.
  Without media the boot stops in the Add New Hardware Wizard with device
  initialisation blocked behind the dialog, and the debug console stays at zero
  bytes, which reads like a driver that failed to load. Note also what such a
  control is worth on that target: Win98's 1.1 stack is not a usbport miniport
  (NUSB ships no UHCI or OHCI miniport at all), so it answers the vehicle
  question only. On Windows 2000, where SP4's `usbuhci.sys` binds it, the same
  control is a Microsoft miniport under the same usbport and does exonerate the
  stack.
- There is no High-Speed hub model. `usb-hub` is the only hub this build has
  and it is USB 1.1 Full Speed, which is what batch 7b's `TtPairsDisagreed` was
  already measuring. Any "behind a High-Speed hub" clause is a Phase 13 item,
  for the same reason as the Low-Speed clause.
- No reachable audio device declares `bInterval > 1`. Read off the wire with
  `pcap=` on the device: the plain model declares `bInterval = 1` and the
  `multi=on` model declares it on all three streaming alternates
  (`wMaxPacketSize` 192 / 576 / 768). So the derived-cadence path can be shown
  to work here but never to disagree with the assumption. On metal the Sound
  Blaster X4 declares 3 and 4 and did not bind (`run-13e.md` Finding Y), so the
  path is still exercised at one value everywhere.
- `HCCPARAMS1 = 0x00087001`, so CFC (bit 11) is 0. QEMU's xHC has no
  Contiguous Frame ID Capability, and this driver therefore never names a frame
  on any submit, on either target. `IsoSubmitsWithFrameId` reads 0 in this
  vehicle for that reason alone, so it cannot discriminate anything here.
- A declared `-audiodev dsound` that fails to initialise kills the whole boot;
  QEMU treats it as fatal and does not fall back. It is a host-side condition
  (which endpoint Windows currently calls the default), not a broken launcher:
  the identical command line failed once and started cleanly three times
  minutes later. Remove the line or fix the host default; nothing else depends
  on it.
- Use `wavcapture` to get both oracles at once. Attach the device on `dsound`
  so a human can hear it, then tap the same mixer to a file from the monitor
  (`wavcapture <path> <audiodev> 48000 16 2`). The file proves samples arrived
  and at what rate; the ears prove that what arrived is a clean tone rather
  than distorted or channel-swapped, which no RMS window can see. Both mattered
  in batch 9-V.
- Turn `usb_xhci_xfer_*` tracing off before any audio-quality stage
  (`trace-event usb_xhci_xfer_* off`). An isochronous stream is about 1,000 TDs
  a second and each produces two trace lines; writing them is host work inside
  the very scheduling window the stage is trying to measure.
- A traced build cannot be used to judge audio quality on this vehicle. Its
  output goes out of port 0xE9 one VM exit per character, in the host thread
  that also has to keep the audio sink fed. Batch 9-V heard pronounced stutter
  on the traced build, much less on Microsoft's `usbuhci.sys`, and "about the
  same as the control" on our release build, while all three delivered a
  continuous 120 s stream to the device. Judge quality on the release build or
  on the capture, never on a traced build by ear. (Batch 9-V's traced build was
  the `debug` flavour of the time; today the `qemu` flavour carries the trace
  and `debug` traces nothing.)
- QEMU's `multi=on` (5.1) audio device plays high-pitched, and that is the
  vehicle too: reproduced identically on `usbuhci.sys` with this driver out of
  the path.

### The automated VM device matrix (Phase 10)

`scripts\vm-matrix\` boots each target VM, walks every USB device model the
installed QEMU can present, and emits a diffable per-device report. It is
committed, parameterised, and depends on nothing in `scripts\local\`. Its own
README is `scripts\vm-matrix\README.md`; the verdict design is
`docs\contributing\design\06-device-matrix-verdict.md`.

One-line invocation:

```
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1
```

First time on a host, or after any change to `XHCI_EXTENSION`:

```
copy scripts\vm-matrix\config.sample.psd1 scripts\vm-matrix\matrix.config.psd1     :: then edit the paths
powershell -File scripts\vm-matrix\gen-offsets.ps1
powershell -File scripts\vm-matrix\run-matrix.ps1 -Config scripts\vm-matrix\matrix.config.psd1 -ValidateOnly
```

`-ValidateOnly` boots nothing: it resolves every expectation against the driver's
counters, checks every device model exists in this QEMU build, and checks every
image is present. Run it before spending a boot.

The same runner takes the post-release run (Phase 16, task 16.1) with
`-PostRelease`: guests installed for the occasion, cloned out of the two
pre-driver snapshots and stamped `base-<DriverVer>-qemu` by
`prepare-image.ps1`, each row attached, detached and attached again, and one
report per target under `out\post-release\<DriverVer>\`. The design is
`docs\contributing\design\09-post-release-unattended-run.md` and the commands
are in the harness README's post-release section.

Three things about it that are easy to get wrong:

- The guest must be running the `qemu` build, and the same one the offset
  table was generated from. The harness compares `offsets.txt`'s `SIZEOF`
  against the `MiniPortExtensionSize` the running driver prints and refuses on
  a mismatch, because a stale offset table surfaces as a wrong value and never
  as an error. The flavour matters: the harness reads the driver's identity off
  the port-`0xE9` console, and both the `0xE9` sink and the `cb ...` line that
  carries the extension VA compile only under `qemu`. A `debug` guest boots and
  drives devices and produces no identity line, which surfaces as a
  boot-deadline timeout. Build it with `scripts\build-driver.cmd qemu` and
  `make-package.ps1 -Flavor qemu`; `qemu` is never published, so this is a
  harness prerequisite and not something a user installs.
- It boots with `-snapshot`, so it never writes to the guest images. Anything
  you do inside a guest during a run is discarded; installing a driver is a
  separate, deliberate step with `Snapshot = $false`.
- Windows 98 needs one persisted install pass per device class. Measured in
  Phase 10's matrix: a boot-attached `usb-mouse` enumerates (`devices addressed` = 1,
  `slots enabled` = 1) but `endpoints opened` stays 0 for ten minutes,
  because Win98 raises a modal Add New Hardware Wizard for any class the image
  has not been taught and it blocks the bind indefinitely. The same run measured
  Windows 2000 SP4 claiming the identical device within tens of seconds with no
  intervention. (Two records of that run give "80 s" and "26 s" for the same
  fact, and neither can now be re-attributed to a run. Nothing depends on the
  number: readiness is the guest's own signal, a function driver opening a
  non-default endpoint on the keep-alive, not a timeout.) The harness names the
  wizard in its failure message rather than letting it read as a driver defect,
  which is how it reads otherwise (see "USB Audio in QEMU" above).

  Two more Windows 98 facts the image-preparation pass
  (`scripts\vm-matrix\prepare-image.ps1`) established, neither guessable. A
  HID devnode can persist in a failed state, and Windows 98 then disables the
  port rather than re-enumerating: `usb-kbd/fs` failed with `devices
  addressed +0` and `RH ports disabled` = 1, having raised an Insert Disk
  dialog for a file it could not reach, while the device sat plainly on the bus
  in `info usb`. Remove + Refresh in Device Manager is the fix; Update Driver
  on the failed node keeps the broken configuration.

  A class driver install
  needs a restart before it takes effect, so a prep pass is at least two boots,
  and that is why an existing work copy is never silently re-copied over. One
  class install also covers less than it looks: after the High-Speed keyboard
  was taught, `usb-kbd/hs` passed all eleven expectations on a fresh port
  silently, and the Full-Speed variant still needed its own pass. Every device
  instance the matrix presents must be taught once with the CD attached, and
  the list is the matrix's own row set.

  The wizard also silently disables the idle-suspend pump: `mouse_move` only
  puts data on the wire once a function driver holds the pointer's interrupt
  endpoint, so with the wizard up the pump generates nothing and the next
  hot-plug lands on a suspended controller and is never seen. On Windows 98 the
  keep-alive is therefore attached before the driver starts (`StartController`
  is reached about 8 s into the boot; the idle suspend follows about 0.5 s
  after the last transfer), never after, and never on the QEMU command line,
  which wedges SeaBIOS.

### Testing Checklist

From Phase 3 onward, run every applicable checklist item on both target VMs
with the same `xhci98.sys` binary. Record target-specific exceptions as
coverage gaps; do not substitute one OS's observation for the other.

That rule is stated once, here, and is not repeated per item, so an item that
says nothing about targets still has to pass on both. Where an item does name
a target, it marks something that differs between the two (a prerequisite
binary, an INF path, a function-driver situation), never an exemption.

The boxes are run sheets, not phase status. A box says whether this list has
been walked as written, so a closed phase can still carry unticked lines;
Phases 3 to 8 and 11 all do, and all of them met their checkpoints. The
authority for a phase's status is `docs/contributing/roadmap.md`'s Status entry
for that phase, which names the basis it closed on and what was deferred; the
run sheets, `lessons.md` and the release notes carry the clause-by-clause
readings. Where a phase below carries a tick, that line's outcome is recorded
here as well and the two would otherwise disagree.

Phase 3 (miniport registration spike - go/no-go):
- [ ] Win98 has NUSB `usbport.sys`; Win2000 has the native SP4 `usbport.sys`
- [ ] The same `xhci98.sys` binds through the target-appropriate INF path
- [ ] Each target's `usbport.sys` calls the registration/lifecycle callbacks (DbgPrint)
- [ ] Device Manager shows the xHCI controller bound with no error code
- [ ] If the ABI cannot be matched, switch to the Option B monolithic fallback before continuing

Phase 4 (controller init):
- [ ] Controller registers read correctly (HCIVERSION, HCSPARAMS1)
- [ ] Interrupt fires after controller start
- [ ] Port topology classification logged (USB2/USB3 port counts)
- [ ] Port Status Change events logged on device plug/unplug

Phase 5 (root hub):
- [ ] `usbport.sys` creates the root hub PDO; `usbhub20.sys` loads on it - same filename on both targets, different build (NUSB's on Win98, SP4's on Win2000)
- [ ] Device Manager shows "USB Root Hub" under controller
- [ ] Plug event causes `usbport.sys` to poll port status / run port reset (miniport callbacks fire)

Phase 6 (device enumeration):
- [ ] Device address assigned (Address Device command succeeds)
- [ ] Device descriptor read (GET_DESCRIPTOR control transfer works)
- [ ] Device appears in Device Manager with correct VID/PID

Phase 7 (interrupt transfers + HID validation):
- [ ] USB HID keyboard generates keystrokes
- [ ] USB HID mouse moves the pointer
- [ ] Device survives unplug and replug
- [ ] HID devices work behind a High-Speed hub

Phase 8 (bulk: mass storage + Ethernet):
- [ ] USB flash drive appears in My Computer
- [ ] Files readable from USB flash drive
- [ ] USB Ethernet adapter binds to a known-working driver on each target OS
- [ ] USB Ethernet adapter passes sustained ping or file transfer traffic
- [ ] Flash drive works through a High-Speed hub
- [ ] Unplug during an active read and during an active write completes or cancels every transfer exactly once

Phase 9 (isochronous ABI + USB Audio):
- [x] Task 9-0.1's ABI gate passes on both shipping `usbport.sys` builds, or the isochronous path is published as an explicit Option A limitation, which is a legitimate outcome for this phase and not a failure. Passed statically and confirmed dynamically: `IsoRefusalsMalformed` and `IsoSubmitsWrongType`, the two counters that fire if the recovered block layout and what the running usbport passes disagree, both read 0 across 250,330 packets
- [x] USB Audio device plays audio without underruns. Met on Windows 2000 (heard clean; a 120 s host capture with zero silent windows; 250,330 packets with `IsoPacketErrorsTotal` and `IsoMissedServiceTotal` both 0). Named on Windows 98: that OS's own `USBAUDIO.VXD` faults on this device regardless of host controller; see "USB Audio in QEMU" above
- [x] USB Audio recording works if the device supports input. Not applicable, measured: QEMU's `usb-audio` exposes no input path (`No Recording Devices`, greyed out)
- [ ] USB Audio works behind a High-Speed hub. Met behind a Full-Speed hub on Windows 2000; the High-Speed half is not producible in this vehicle, since `usb-hub` is USB 1.1, and moves to Phase 13

Phase 10 (automated VM device matrix):
- [ ] One command walks every QEMU device model on both target VMs unattended and emits a diffable per-device report
- [ ] The harness reproduces at least two hand-measured results and fails on an expectation broken on purpose

Phase 11 (power, packaging, stress - VM only):
- [ ] 20x unplug/replug cycle without crash or hang
- [ ] Mixed HID + storage + Ethernet or audio traffic runs without lost completions
- [ ] Suspend/resume and the full power lifecycle on both target VMs
- [ ] Package install, upgrade and uninstall

Phases 12 and 13 (the host- and guest-side decisions, and every real-hardware
clause in the project). They are grouped into three groups that fail
independently, split across two phases so that the first group's independence
is structural. Each line names the section of this document that carries its
evidence. Phase 12 carries no batches, so its task ids are plain `12.N`. Phase
13 is cut into batches, so its ids are `<batch>.<n>` (`13-E.2` is the second
task of batch `13-E`); the batches below are `13-E` and `13-H`. A third,
`13-T` (Windows 2000 on metal and the Intel port mux), was removed for want of
any vehicle; what it established is the block below, published in
`docs/using/release-notes.md`.

- Phase 12, closed. The original four tasks closed, task 12.5 reopened the phase, and it closed again the same day on its control:
  - [x] Task 12.2: a Windows 98 trace channel on real hardware, or the statement that there is none. Closed on the statement. There is none, and the last option (a second hand-over site, away from the stop) has no PASSIVE-level moment on that target to fire in
  - [x] Task 12.3: the failed-start rollback artifact, built and gated, then run on both guests (captures `vm\12v-runb\`, `vm\12v-runc\`, transcribed above and discarded). The run found a failure mode nobody had predicted: Windows 98 does not survive its own failed start. "Staging a driver that starts and fails (task 12.3)" above records the trace, the controls and the recovery route
  - [x] Task 12.4: why Windows 2000 records no driver date. The unpadded-date package was built, gated and run on 2b (capture `vm\12v-runa\`, transcribed above and discarded): the unpadded date records no date either, so padding is not the cause. "Staging the unpadded-date experiment package (task 12.4)" above carries the key's value list and the verification step
  - [x] Task 12.1: the `FSC = 0` suspend path, closed on the publish branch as the standby entry in the release notes' "Known limitations". `HCCPARAMS2`/FSC was added to the qualifier's capability dump in the same task, so the remaining gap is a fleet reading, which needs a bare-metal run and comes free with any `XHCIQUAL xhci --probe-only` run on a bench visit; no Phase 13 batch owns it
  - [x] Task 12.5: the 150-hub-pair Windows 98 wedge, added late and closed the same day on its control (captures `vm\12v5-legA\`, `vm\12v5-legB\`, `vm\12v5-legA2\`, transcribed in `lessons.md` and discarded).

        Do not attempt an EHCI arrangement for the control: it is impossible on this vehicle, because `usb-hub` is full-speed, QEMU's standalone `usb-ehci` is high-speed only, and QEMU 11.0.92's `usb-hub` has no `usb_version` property, so the attach is refused outright. The control ran on UHCI instead.

        The churn wedges Windows 98 only when `xhci98.sys` carries it, so the finding is this driver's and is not published as a Windows 98 limitation. An EHCI leg with a high-speed churn device on both sides would test the narrower claim and is unrun. What it hands on is a mechanism hunt with no owner; the evidence directories named here are the record
- Phase 13, batch 13-E (E460, one bench trip, one bag of devices):
  - [x] Real hardware confirmed working under Win98 SE (E460, batch 7b-M)
  - [x] Task 13-E.1: composite devices under NUSB. Closed on a remedy, and the premise of the line was wrong: it is not NUSB, and the SE CD experiment was never needed. Windows 98's composite parent is `usbhub.sys`, which Setup places only when it finds a USB controller it recognises, so an xHCI-only machine never gets one. The package now carries it, Windows 98 path only. `run-13e.md`'s "Finding 1 - RESOLVED" is the record, and the `[Xhci.CopyW98]` row of "The finished file: `src/xhci98.inf`" above is the package side
  - [x] Full-Speed or Low-Speed device works behind both single-TT and multi-TT hubs: task 13-E.2, the behavioural half (the `MTT`/`TTT` numbers need a counter channel and have no vehicle). Closed: stage E3 passed on both trees, the `1A40:0101` <-> `1A40:0201` one-variable swap at position T with the Low-Speed `046D:C077` at H1 and all four other children unchanged either side of it
  - [x] Task 13-E.3: a physical USB audio device plays, on a root port and behind a High-Speed hub, one of them declaring `bInterval > 1`. Closed: clauses 1 and 2 played clean on the E460 (a UAC 1.0 device at a root port, then behind the multi-TT `1A40:0201`), which made the published Windows 98 audio limitation wrong, and the release notes were corrected; clause 3 was read and is unreadable on this target, since the Sound Blaster X4 declares `bInterval` 3 and 4 and never bound an audio alternate, so no Endpoint Context was built from them (`run-13e.md` Findings X and Y)
- Windows 2000 on metal, and the Intel port mux: closed unobserved on the published-limitation branch. No clause below was taken, and none has a vehicle. Batch `13-T` held them and was removed; this block is the record:
  - [ ] A Windows 2000 SP4 install on real hardware. Both fleet machines bugcheck in Setup and no further candidate is available to this project
  - [ ] Real hardware confirmed working under Win2000 SP4: unmet, and unmeetable
  - [ ] The TT/`MTT`/`TTT` numbers read, not merely an absence of yellow bangs. Published as not taken: a Windows 98 route exists (`passthru-snapshot-instrument.md`) and its cost was judged not worth paying
  - [ ] `ResumeController` and the Low-Speed trace half. `ResumeController` is not possible on this fleet; the trace half is not taken
  - [ ] The `XUSB2PR` routing exercised against the driver, and the qualifier on a real multi-controller console. Both not possible on this fleet: no machine has an Intel 7/8-series mux and none has more than one USB controller
- Phase 13, batch 13-H (the modern Windows host; no trip, no target OS):
  - [x] Closed. It owes no clause of the phase; it produces the equipment record the two bench batches are read against. Twelve devices and six hub units characterised, all three buy-or-publish decisions taken with no purchase, and rig positions D and T labelled at the E460. See `docs/contributing/test-equipment.md`, and the "The bench rig" section above in this file for the positions.
