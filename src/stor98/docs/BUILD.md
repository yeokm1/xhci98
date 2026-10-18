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
`tests\cmnt\build.cmd` and the signing scripts then use xhci98's own `tools\`, two levels
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

These build folders are not the release layout: `make-release.ps1` puts `win9x\stor98.mpd`
and `x86\stor98.sys` together in the release's `x86\` and `amd64\stor98.sys` in its `x64\`
(see "INFs and version").

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
both `x86\` and `amd64\`, each shipped under the former name with the `.inf` extension. The version lives in `src\stor98ver.h` (version resource) and in each INF's
`DriverVer`; `scripts\make-release.ps1` checks that they agree with each other and with
`releases\history.md`.

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

`scripts\make-release.ps1 -Stage -TestSignPfx <file.pfx>` builds and stages a release in
`out\stage\<version>\` (`x86\` and `x64\`), signing the x64 image with the project test
certificate as it stages; then `scripts\make-release.ps1 -Finalize` copies it to
`releases\<version>\`. `-Stage -Test` writes an unsigned stage for install tests to
`out\stage\<version>-test\`, which is never finalized. The rules and the exact commands are
in [releases/README.md](../releases/README.md).

## Test signing

The procedure is the one xhci98 uses for its x64 drivers, written again for Stor98.

### Why, and what is signed

Vista x64 and Windows 7 x64 refuse a kernel-mode driver without a signature they accept;
XP x64, Server 2003 x64 and every 32-bit Windows do not check. In test-signing mode
(`bcdedit /set testsigning on`) those two load a driver signed by any certificate, trusted
or not, at every start. Without a signature the only route is F8, Disable Driver Signature
Enforcement, which lasts one start. So the released `x64\stor98.sys` (`amd64\` up to 0.1.1.0) carries:

- an **embedded Authenticode signature** by the Stor98 project test certificate. There
  is no catalog, so the kernel checks the image's embedded signature when Plug and Play
  loads the demand-start service (`inf\stor98-x64.inf`);
- **SHA-1 file digest** (`/fd sha1`): Vista verifies SHA-1 only, and Windows 7 verifies
  SHA-256 only after a later update a user may not have;
- **no timestamp**: the cut stays offline, and the same bytes signed with the same key give
  the same bytes, so a re-cut is reproducible;
- **no page hashes** (`/nph`), **no catalog**, and the INFs unchanged. A catalog would only
  govern the install-time "can't verify the publisher" prompt, which therefore stays.

The x86 and Windows 9x images are **never signed**: nothing there checks, and a signature
would change bytes every 32-bit reading was taken on. The signed image differs from the
build only in the PE checksum, the certificate-table directory entry, up to seven bytes of
zero padding and the appended `WIN_CERTIFICATE`; the scripts check exactly that.

This is not Microsoft signing and nothing may say it is. The certificate is self-signed,
trusted nowhere, and does not make the driver load on Windows in its ordinary mode.

### The certificate (once, by the maintainer)

```
powershell -ExecutionPolicy Bypass -File scripts\make-testcert.ps1
```

The rename to Stor98 changed the expected subject and file names: a certificate made
before it (its subject and .pfx name carry the former name) is refused by `-TestSignPfx`. Make a new one with this script, or sign a stage with the old
one through `scripts\sign-x64.ps1 -Pfx <file>`, which accepts any certificate
([RENAME-STOR98.md](RENAME-STOR98.md), "For the maintainer").

It runs WDK 7.1's MakeCert and pvk2pfx from `tools\WinDDK71\bin\x86` with guards: it
refuses to overwrite an existing key, refuses to write into a folder of the repository git
does not ignore (the default, `scripts\local\testsign\`, is ignored as a whole), asks for
the .pfx password twice without echo, deletes the intermediate .pvk, and prints the
certificate's facts and SHA-1 thumbprint. The raw commands are:

```
cd scripts\local\testsign
set WDK=<repo>\tools\WinDDK71\bin\x86
"%WDK%\MakeCert.exe" -r -pe -n "CN=stor98 project test signing (not trusted)" -a sha1 -len 2048 -eku 1.3.6.1.5.5.7.3.3 -b 01/01/2000 -e 12/31/2039 -sv stor98-test.pvk stor98-test.cer
"%WDK%\pvk2pfx.exe" -pvk stor98-test.pvk -spc stor98-test.cer -pfx stor98-test.pfx -po <pfx password>
```

`-r` self-signed; `-pe` exportable key, which pvk2pfx needs; `-a sha1` a certificate
signature Vista can check; `-len 2048` RSA 2048; `-eku 1.3.6.1.5.5.7.3.3` Code Signing,
which SignTool requires; `-b 01/01/2000` already valid on a machine whose clock is behind;
`-e 12/31/2039` long, since there is no timestamp. MakeCert and pvk2pfx ask for the .pvk
password in dialogs. pvk2pfx takes the .pfx password only on its command line (so does
SignTool at every cut), where local processes that read command lines can briefly see it.

| File | What | Where |
|---|---|---|
| `stor98-test.pfx` | private key and certificate | `scripts\local\testsign\`, git-ignored. Never committed, packaged, printed or copied into notes. |
| `stor98-test.cer` | public half | the same folder; a copy is staged next to every signed `x64\stor98.sys` |
| .pfx password | | typed at a prompt that does not echo, or `STOR98_TESTSIGN_PW` in the process environment; never stored |

If the repository is in a synchronised folder (OneDrive, Dropbox, ...), a key under
`scripts\local\testsign\` is synchronised too (git-ignored is not local-only). `make-testcert.ps1 -OutDir <folder>` writes
outside the repository instead, and `-TestSignPfx` takes any path.

After making it, record the printed thumbprint in `scripts\testsign-common.ps1`
(`$Stor98TestSignThumbprint`) and commit that: from then on `-TestSignPfx` accepts that
certificate and no other. While it is empty, any .pfx with the project subject is accepted.

### Cutting with the signature

```
powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Stage -TestSignPfx scripts\local\testsign\stor98-test.pfx
powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Finalize
```

`-Stage -TestSignPfx` opens the .pfx before building (a wrong password costs a prompt, not
a build) and refuses one whose subject is not `CN=stor98 project test signing (not
trusted)` or which has no private key. After staging it signs a copy of the amd64 image
(`SignTool sign /f ... /fd sha1 /nph`), checks the copy (signer thumbprint, one PKCS #7
signer with SHA-1 as both digests, the vouched digest equal to the image's, no timestamp,
and the build's bytes plus the signature and nothing else), puts it in place and writes
`x64\stor98-test.cer`. Only an amd64 PE32+ image is ever signed. `-Finalize` refuses an
unsigned amd64 image, an `stor98-test.cer` that is not the signer's certificate, and a
signed image that is not the staged build plus a signature. It does not check who the
signer is, and it accepts a timestamp: any certificate that `stor98-test.cer` matches
passes, so that `scripts\sign-x64.ps1` can sign with another certificate (and
`-TimestampUrl`). The project certificate and "no timestamp" are enforced only where
`-TestSignPfx` signs; SHA-1 as both digests is required on both routes.

**SignTool leaves key containers.** SignTool `/f` imports the .pfx's private key into a
CryptoAPI key container under `%APPDATA%\Microsoft\Crypto\RSA\<user SID>\` and leaves it
there: a copy of the private key outside the key folder. The scripts compare that folder
before and after signing and print each new file as a ready `Remove-Item -LiteralPath ...
-Force` line; they do not delete them. Delete the files named. They are hidden system
files; list the newest with:

```
Get-ChildItem "$env:APPDATA\Microsoft\Crypto\RSA" -Recurse -Force -File | Sort-Object LastWriteTime -Descending | Select-Object -First 5 FullName, LastWriteTime
```

### Another certificate: `scripts\sign-x64.ps1`

`make-release.ps1 -Stage` without `-TestSignPfx` stages the amd64 image unsigned, and
`scripts\sign-x64.ps1` signs that stage with any certificate, through the same code and to
the same layout (the signer's public half is staged as `x64\stor98-test.cer`):

```
powershell -ExecutionPolicy Bypass -File scripts\sign-x64.ps1 -Pfx <file.pfx>
powershell -ExecutionPolicy Bypass -File scripts\sign-x64.ps1 -Thumbprint <sha1>
```

An option, not the default: sign with xhci98's existing test certificate
(`-Pfx <xhci98 checkout>\scripts\local\testsign\xhci98-test.pfx`), so a test machine has
one certificate to trust for both drivers. The released `stor98-test.cer` is then
xhci98's certificate. With the same certificate the two routes give byte-identical stages.

### Checking a signed image

```
tools\WinDDK71\bin\x86\SignTool.exe verify /pa /v releases\<version>\x64\stor98.sys
```

prints `Hash of file (sha1)`, the chain with the signer's SHA-1 hash, `File is not
timestamped.`, and then fails with "terminated in a root certificate which is not trusted".
That failure is expected for a test certificate; what is checked is the thumbprint and
`sha1`.

### What users do

On Vista x64 and Windows 7 x64, once: an elevated Command Prompt, `bcdedit /set
testsigning on`, restart. "Test Mode" then shows on the desktop and the driver loads at
every start. `bcdedit /enum {current}` shows `testsigning Yes`; `bcdedit /set testsigning
off` and a restart undo it. F8, Disable Driver Signature Enforcement, still works for one
start without test mode. With BitLocker on the system drive, suspend it before changing the
boot setting. Without test-signing mode the driver is refused and the controller shows Code
52; Windows still starts. To leave test-signing mode, uninstall Stor98 or run `sc config
stor98 start= disabled` first, then `bcdedit /set testsigning off` and restart. With 0.1.0.0
(boot-start) installed, turning test-signing off makes Windows 7 x64 fail to start
(Startup Repair, then System Restore removes the driver); recovery is F8, Disable Driver
Signature Enforcement, or test-signing on again. With test-signing on, `pnputil -i -a
stor98.inf` (or Device Manager) starts the driver at once, without a restart. Setup still
says it cannot verify the publisher (no catalog): "Install this
driver software anyway". Importing `stor98-test.cer` into Trusted Root Certification
Authorities and Trusted Publishers is optional and not needed: test-signing mode loads the
driver without it, and it does not remove the install prompt. XP x64, Server 2003 x64 and
32-bit Windows need none of this. The release `readme.txt` carries the same steps.
