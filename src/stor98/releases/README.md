<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# Released builds

Each subdirectory is one version Stor98 released on its own, named for the version in its
INFs' `DriverVer` (`0.3.0.1/`, `0.4.0.0/`). Earlier releases were removed from this folder
and remain in the git history of the Stor98 repository.

**Since xhci98 3.1.0.0 Stor98 has no releases of its own** (owner, 2026-10-19; xhci98
roadmap-usb4 task 42-A.30). It is built from this source by xhci98's gated build
(`scripts\build-driver.cmd` in the xhci98 tree), carries the xhci98 package's version and
date (`src\stor98ver.h` takes them from xhci98's `src\xhci_version.h`; the INFs'
`DriverVer` is held to the same by xhci98's `scripts\inf-gate\check-stor98-inf.ps1`), and
is published in the xhci98 release's `release-x86\` and `release-x64\` directories, its x64
`stor98.sys` signed with the other x64 drivers by xhci98's key. `0.4.0.0` was the last
release made here, so the next version is `3.1.0.0`, and an installed `0.4.0.0` is older
than it. No new folder is added here; [history.md](history.md) keeps the release history,
its newest entry the version xhci98 ships. The release script, the signing scripts and
Stor98's own test certificate were removed with that change.

## Rules for the folders here

1. **Write-once.** Both versions were uploaded; their directories are never edited again:
   not to fix a typo, not to regenerate one file, not to correct a hash. Two different byte
   states answering to one version name cannot be told apart by the person holding one of
   them.
2. **Stored byte-exact** (`.gitattributes`: `-text`), so the hashes in `SHA256SUMS.txt` hold
   on every checkout.
3. **History.** `history.md` keeps one entry per version, newest first, written for the
   person installing the driver, in plain ASCII within 78 columns.

## Layout (0.3.0.1 and 0.4.0.0)

```
releases\<version>\
  readme.txt          install guide and release history (plain text, 78 columns, CRLF)
  LICENSE             BSD-3-Clause, every copyright holder
  NOTICE.TXT          provenance of each part
  SHA256SUMS.txt      SHA-256 of every other file, "<hash>  <path>", LF line ends
  x86\stor98.inf      one $CHICAGO$ INF for every 32-bit target (inf\stor98-x86.inf)
  x86\stor98.mpd      Windows 98 SE / ME
  x86\stor98.sys      Windows 2000 SP4, XP, Vista, 7 (32-bit)
  x64\stor98.inf      inf\stor98-x64.inf
  x64\stor98.sys      Windows XP x64, Vista x64, 7 x64 - test-signed (SHA-1, no timestamp)
  x64\stor98-test.cer public half of the certificate x64\stor98.sys was signed with
```

`x86\stor98.inf` installs `stor98.mpd` on Windows 98 SE and ME (its undecorated install
section: `IOSUBSYS`, `DevLoader` `*IOS`, `PortDriver`) and `stor98.sys` as the demand-start
miniport service on Windows 2000 to 7 (its `.NTx86` sections); each system copies only its
own image. `x64\stor98.inf` has only `NTamd64` models, so 32-bit Windows does not offer it,
and 64-bit Windows does not offer the x86 INF. All binaries are the free (release) build.
Only the x64 image was signed, with Stor98's own test certificate, which signs nothing any
more.

## What users of the x64 driver do

Since 3.1.0.0 the x64 `stor98.sys` is signed with the same test certificate as xhci98's own
x64 drivers, so the steps are xhci98's, and the package's `stor98-readme.txt` says so; in
short:

- **Vista x64 and Windows 7 x64**: once, in an elevated Command Prompt,
  `bcdedit /set testsigning on`, then restart. "Test Mode" shows on the desktop and the
  drivers load at every start. For a single start without test mode: F8, Disable Driver
  Signature Enforcement. With BitLocker on the system drive, suspend it before changing the
  boot setting.
- The service is demand-start, so without test mode the controller shows Code 52 and
  Windows starts normally.
- **XP x64, Server 2003 x64 and all 32-bit Windows** do not check driver signatures and
  need none of this.
