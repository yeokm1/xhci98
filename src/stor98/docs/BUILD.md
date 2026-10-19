<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# Building Stor98

One source tree (`src/`) builds three drivers:

| Target | Output | Toolchain | Environment |
|---|---|---|---|
| Windows 98 SE / ME | `stor98.mpd` | Windows 2000 DDK + MSVC 6.0, Windows 98 DDK `SCSI.H`/`SRB.H` | `setenv <ddk> free w2k x86`, `NVME2K_W9X` |
| Windows 2000 - 7, x86 | `stor98.sys` | Windows 2000 DDK + MSVC 6.0 | `setenv <ddk> free w2k x86` |
| XP x64 / Vista / 7, amd64 | `stor98.sys` | WDK 7.1 (7600.16385.1) | `setenv <wdk> fre x64 WNET no_oacr` |

The 9x image is the NT4-ABI SCSI miniport that Windows 9x's SCSI layer loads, built the way
SweetLow built nvme9x (see `src/README-nvme9x.txt`): Windows 2000 DDK and MSVC 6.0, with the
Windows 98 DDK's `SCSI.H` and `SRB.H` ahead of the Windows 2000 ones, and `NVME2K_W9X`
defined (which implies `NVME2K_NT4`). The amd64 image targets NT 5.2 (`WNET`), so one binary
serves XP x64, Server 2003 x64, Vista x64 and 7 x64.

## The `tools\` folder

Everything the build needs lives in `tools\` at the repository root. It is git-ignored:
Microsoft kits, headers, libraries and binaries are never committed. Nothing is installed
machine-wide and the build does not depend on any other checkout.

```
tools\
  MSVC600\          MSVC 6.0: VC98\ (BIN, INCLUDE, LIB) and Common\MSDev98\ (MSPDB60.DLL)
  ntddk\            Windows 2000 DDK, with bin\ddkvars.bat pointing at ..\..\MSVC600
  WinDDK71\         WDK 7.1 7600.16385.1, unpacked (bin\setenv.bat, inc\, lib\wnet\amd64)
  w98ddk\inc\       SCSI.H and SRB.H from the Windows 98 (or NT4) DDK
```

The repository path must not contain a space: the DDKs' `setenv.bat` takes its root unquoted.

In the xhci98 repository, where Stor98 is `src\stor98\` (since 2026-10-19), there is no
`tools\` here: `scripts\build.cmd`, `tests\unit\build.cmd`, `tests\cm9x\build.cmd`,
`tests\cmnt\build.cmd` then use xhci98's own `tools\`, two levels
up, which holds the same kits (its `tools\w98ddk\inc` the two headers below). `STOR98_TOOLS`
in the environment names any other root; the layout under it is the one above.

### Where each kit comes from

| Folder | Source | Notes |
|---|---|---|
| `tools\MSVC600` | MSVC 6.0 (Visual C++ 6.0, `cl` 12.00.8804), e.g. `MSVC600.zip` from [itsmattkc/MSVC600](https://github.com/itsmattkc/MSVC600) (sha256 `a4c46a05039cc8f512043728c8132a830a04ffd68d6f038df100413ae6c21061`) or a Visual Studio 6.0 CD | Unzip so that `tools\MSVC600\VC98\BIN\CL.EXE` exists. No setup program is run; `cl.exe` works in place once `Common\MSDev98\Bin` is on `PATH`, which `ddkvars.bat` does. |
| `tools\ntddk` | Windows 2000 DDK, `WIN2KDDK.EXE` (sha256 `e81728f182a278fc749abe32a366ae6726f15ca5f2c9e43912672707a4df2213`), e.g. from [KunYi/WDK_DDKArchive](https://github.com/KunYi/WDK_DDKArchive/releases/tag/Win2K_DDK) | A cabinet self-extractor; extract the cabinets into `tools\ntddk` (`bin\setenv.bat`, `inc\`, `libfre\`, `libchk\`). `setenv.bat` calls `bin\ddkvars.bat` when it exists instead of looking for a registry-installed MSVC; create it as shown below. |
| `tools\WinDDK71` | WDK 7.1, `GRMWDK_EN_7600_1.ISO` (649,877,504 bytes, sha256 `5edc723b50ea28a070cad361dd0927df402b7a861a036bbcf11d27ebba77657d`), Microsoft download KB 11800 | Unpack the ISO's payload MSIs with `msiexec /a <msi> TARGETDIR=...` (administrative install: lays out files, registers nothing). Only needed for amd64. |
| `tools\w98ddk\inc` | `SCSI.H` and `SRB.H` from the Windows 98 DDK (`98DDK.EXE`), the Windows 98 SE DDK, or the Windows NT4 DDK | Only these two files. The copies used so far came with SweetLow's `nvme2k-src.zip` (sha256 `809d57e6...b7` and `4b3c3305...96`; in full, `SCSI.H` 36,501 bytes `809d57e6756de21524930eb4d17840ab3047c7e1773da634ceb73a81ff8d39b7`, `SRB.H` 21,056 bytes `4b3c3305686f2593ceb471ae69f1d690de5d833c8f2047859d735b21affd8396`). Only needed for the 9x target. |

`tools\ntddk\bin\ddkvars.bat` (local, not part of the DDK):

```bat
@echo off
rem Point the Windows 2000 DDK at the MSVC 6.0 in tools\MSVC600.
for %%I in ("%~dp0..\..\MSVC600\VC98") do set "MSVCDIR=%%~fI"
for %%I in ("%~dp0..\..\MSVC600\Common\MSDev98") do set "MSDEVDIR=%%~fI"
set "Path=%MSVCDIR%\BIN;%MSDEVDIR%\BIN;%Path%"
set "INCLUDE=%MSVCDIR%\INCLUDE"
set "LIB=%MSVCDIR%\LIB"
```

A machine that already has these kits unpacked for another project can copy the folders
across as they are; the paths inside are relative.

## Building

From a Command Prompt (or PowerShell) at the repository root:

```
scripts\build.cmd                 :: all targets, free
scripts\build.cmd amd64           :: one target: 9x | x86 | amd64 | all
scripts\build.cmd all both        :: free and checked
```

Each build runs `build -cZ` in `src\` inside its own `setenv.bat` environment and puts its
objects in `src\obj<alt>\<arch>\` (`objfre9x\i386`, `objfre\i386`, `objfre\amd64`, and the
`chk` equivalents). The images and their `.pdb` files are then copied to:

```
out\free\win9x\stor98.mpd     out\checked\win9x\stor98.mpd
out\free\x86\stor98.sys       out\checked\x86\stor98.sys
out\free\amd64\stor98.sys     out\checked\amd64\stor98.sys
```

These build folders are not the package layout: xhci98's `make-package.ps1` puts
`win9x\stor98.mpd`, `x86\stor98.sys` and `inf\stor98-x86.inf` (as `stor98.inf`) at the root
of its x86 package and `amd64\stor98.sys` with `inf\stor98-x64.inf` at the root of its amd64
package (see "INFs and version" and "Releases").

`build.cmd` fails if `build.exe` fails, writes a `build*.err`, or produces no image, and prints
any `build*.wrn`. Sources compile at `/W3 /WX`.

A bare `build` from a DDK prompt also works if `STOR98_TARGET` is set to `9x` or `nt` first;
`src\sources` refuses anything else.

The gate G9 and Q-9x prototype layers (`src\g9dbg.c`, `src\q9x.c`;
[gates/G9.md](gates/G9.md), [gates/Q-9x.md](gates/Q-9x.md)) are kept for reference only. They
hook into the request path that the engine replaced and are not built: `STOR98_G9` or
`STOR98_Q9X` in the environment makes `src\sources` fail the build. The test-only
instrumentation of the product code is `STOR98_DBGPORT=1` (debug-port stage codes and
debug-console log lines, images in `out\<flavor>-dbgport\`; [TESTING.md](TESTING.md),
"Debug-port stage codes"). These images are never shipped. The 9x CM stand-in and its
helper are built separately with `tests\cm9x\build.cmd` (`out\cm9x\cm9x.sys`, `cm9xctl.exe`).

## INFs and version

Two INFs, one per release folder, each shipped as `stor98.inf`:

| Source | Release | Signature | Sections |
|---|---|---|---|
| `inf\stor98-x86.inf` | `x86\stor98.inf`, next to `stor98.mpd` and `stor98.sys` | `$CHICAGO$` | undecorated models `PCI\CC_010802`; Windows 98 SE / ME: `[Stor98_Inst]` copies `stor98.mpd` (`[Stor98_Files_9x]`) to DIRID 12 (`SYSTEM\IOSUBSYS`) and sets `DevLoader=*IOS`, `PortDriver=stor98.mpd`; Windows 2000 to 7: `[Stor98_Inst.NTx86]` copies `stor98.sys` (`[Stor98_Files_NT]`) to DIRID 12 (`system32\drivers`), `[Stor98_Inst.NTx86.Services]` adds the service |
| `inf\stor98-x64.inf` | `x64\stor98.inf`, next to the amd64 `stor98.sys` | `$Windows NT$` | `NTamd64` models only; `[Stor98_Inst.NTamd64]` and `.Services` |

Windows 9x reads only undecorated sections and NT prefers the `.NTx86` ones, so each system
copies only its own image; both files are listed in `[SourceDisksFiles]`. 64-bit Windows
ignores undecorated models, so it does not offer the x86 INF, and 32-bit Windows finds no
models in the x64 INF. The unpublished builds 0.1.0.0 and 0.1.1.0 (under the former name) had a
separate 9x INF in `win9x\` and one NT INF (undecorated, `NTx86` and `NTamd64` models) in
both `x86\` and `amd64\`, each shipped under the former name with the `.inf` extension.

**The version is xhci98's** since xhci98 3.1.0.0 (owner, 2026-10-19; xhci98 roadmap-usb4
task 42-A.30): `src\stor98ver.h` includes xhci98's `src\xhci_version.h` (two directories
up, `#define` lines only) and gives `nvme2k.rc` its `XHCI_VER_CSV` and `XHCI_VER_STR`, so
every image carries the xhci98 package's version; each INF's `DriverVer` carries the
package's version and date (`XHCI_DRIVERVER_DATE`) as a literal, which xhci98's
`scripts\inf-gate\check-stor98-inf.ps1` checks on every gated build. Stor98 is therefore
built only inside the xhci98 tree. The interface versions in `include\stor98cm.h`
(`STOR98CM_VERSION`, `STOR98LS_VERSION` and the others) are protocol versions and are not
this. Each image also carries one flavour marker string, `STOR98_FLAVOUR_FREE`,
`STOR98_FLAVOUR_CHECKED` or `STOR98_FLAVOUR_DBGPORT` (`src\nvme2k.c`), which `DriverEntry`
reads so the linker keeps it; xhci98's gated build ships only the free one.

The NT sections of both INFs install the `stor98` service as **demand-start** (`StartType=3`,
`ErrorControl=1`, group `SCSI Miniport`), for data disks only: Plug and Play loads the
miniport when it enumerates an NVMe controller, on Windows 2000, XP, XP x64, Vista and 7
alike, and a driver that Windows refuses only leaves that device stopped (Code 52 on Vista
x64 and 7 x64). Upstream, and the 0.1.0.0 pre-release build, used boot-start (`StartType=0`). Windows 7 x64
treats a boot-start image that fails code integrity as a failed start even when the system
disk is on another controller: Startup Repair runs and System Restore removes the driver
(seen with 0.1.0.0 after `bcdedit /set testsigning off`). Booting from an NVMe disk needs
`StartType=0` and, on Vista x64 and 7 x64, a signature those systems trust or test-signing
left on permanently; no release supports it yet. The Windows 9x sections have no service
start type and are unaffected.

## Releases

Since xhci98 3.1.0.0 Stor98 has no releases of its own: xhci98's gated build
(`scripts\build-driver.cmd` there) builds the free images of each architecture from this
source, after the unit tests on its x86 run, and gates them in every flavour - the version,
the flavour marker and the import allowlist (xhci98's `scripts\check-stor98-build.ps1` and
`scripts\import-gate\stor98-imports.allow`), a source stamp beside each image, and the
gated record its signing step reads. xhci98's `make-package.ps1` bundles that build in each
package, with `LICENSE`, `NOTICE` and [package-readme.txt](package-readme.txt) as
`stor98-LICENSE.txt`, `stor98-NOTICE.txt` and `stor98-readme.txt`, and xhci98's
`make-release.ps1` publishes it in the release's `release-x86\` and `release-x64\`
(xhci98's `docs\contributing\build-and-test.md`, "Building Stor98" and "The bundled Stor98
build"). The releases made here up to `0.4.0.0` stay in `releases\` as history
([releases/README.md](../releases/README.md)); the release script that made them was
removed.

## Test signing

Vista x64 and Windows 7 x64 refuse a kernel-mode driver without a signature they accept;
XP x64, Server 2003 x64 and every 32-bit Windows do not check. In test-signing mode
(`bcdedit /set testsigning on`) those two load a driver signed by any certificate, trusted
or not, at every start.

**Since xhci98 3.1.0.0 one signature signs every driver of the package** (owner,
2026-10-19): the amd64 `stor98.sys` is signed by xhci98's `make-package.ps1 -TestSignPfx`,
in the same step and with the same key as `xhci98.sys`, `xhciuas.sys` and `xhcicm.sys` -
an embedded Authenticode signature, SHA-1 file digest, no timestamp, no catalog, the INFs
unchanged - and `xhci98-test.cer` beside them is that certificate's public half (xhci98's
design record 19). The 32-bit and Windows 9x images are never signed. Stor98's own test
certificate (`CN=stor98 project test signing (not trusted)`), which signed the amd64 image
of `0.3.0.0` to `0.4.0.0` with `stor98-test.cer` beside it, signs nothing any more, and its
scripts (`make-testcert.ps1`, `sign-x64.ps1`, `testsign-common.ps1`) were removed. Windows 7
SP1 needs the SHA-1 certificate signature and file digest ([TESTING.md](TESTING.md), "Windows
7 x64 notes"). This is not Microsoft signing and nothing may say it is.

What users do on Vista x64 and Windows 7 x64 is xhci98's (its readme and release notes):
once, `bcdedit /set testsigning on` in an elevated Command Prompt and a restart, or F8,
Disable Driver Signature Enforcement, for one start. Without either the controller shows
Code 52 and Windows still starts (the service is demand-start).
