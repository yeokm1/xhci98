<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright (c) 2026, Yeo Kheng Meng -->

# Released builds

Each subdirectory is one released version of Stor98, named for the version in its INFs'
`DriverVer` (`0.3.0.1/`, ...). Earlier releases were removed from this folder and remain
in the git history. The newest entry in [history.md](history.md) names the current release.

## Rules

1. **Written by the script, never by hand.** A version directory is produced only by
   `scripts\make-release.ps1 -Finalize`, from a stage that `scripts\make-release.ps1 -Stage`
   built and signed with the maintainer's key (`-TestSignPfx`). Nobody copies, edits or
   adds files here directly.
2. **Write-once once published.** After a version has been uploaded or given to anyone, its
   directory is never edited again: not to fix a typo, not to regenerate one file, not to
   correct a hash. If something published under a number is wrong, cut the next version and
   say in `history.md` what changed. Two different byte states answering to one version name
   cannot be told apart by the person holding one of them.
3. **Re-cut with `-Force` only before upload.** A version that exists here but has not been
   uploaded has not spent its number; a finding against it may be fixed by re-staging with
   `-Stage -Force`, re-signing and `-Finalize -Force` under the same number. Without
   `-Force` both refuse a version that exists here. Once uploaded, rule 2 is absolute.
4. **One `history.md` entry per version**, `## <version> - <YYYY-MM-DD>`, written for the
   person installing the driver. Its date must equal the INFs' `DriverVer` date and the version
   must equal `src\stor98ver.h`; the script refuses otherwise. The generated `readme.txt`
   embeds the history from that entry down, so the entry is written before `-Stage`, and
   editing it after a cut means re-cutting. An entry newer than the newest directory here is
   a cut in progress (staged, awaiting signature); the entries below it stay as the release
   history of the removed directories. The entries for the builds from before the rename to
   Stor98 (0.1.0.0 and 0.1.1.0) were removed at the rename; the `readme.txt` of 0.3.0.1
   and the git history still have them.
5. **Generated files are outputs.** `readme.txt`, `LICENSE`, `NOTICE.TXT` and
   `SHA256SUMS.txt` come from the script; change the script or the source file, then cut.
6. **Commit at finalize.** `releases\<version>\` is committed after `-Finalize`, never from
   a stage. Everything it is built from, its `history.md` entry included, is committed
   before `-Stage`: the script stages only a tree without changes, records the commit in
   the stage manifest and names it in `readme.txt`, and `-Finalize` refuses if HEAD or the
   tree has moved since. Files here are stored byte-exact
   (`.gitattributes`: `-text`), so the hashes in `SHA256SUMS.txt` hold on every checkout.

## Layout

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
  x64\stor98-test.cer public half of the certificate x64\stor98.sys is signed with
```

`x86\stor98.inf` installs `stor98.mpd` on Windows 98 SE and ME (its undecorated install
section: `IOSUBSYS`, `DevLoader` `*IOS`, `PortDriver`) and `stor98.sys` as the demand-start
miniport service on Windows 2000 to 7 (its `.NTx86` sections); each system copies only its
own image. `x64\stor98.inf` has only `NTamd64` models, so 32-bit Windows does not offer it,
and 64-bit Windows does not offer the x86 INF. The two folders match the `release-x86` /
`release-x64` split of the xhci98 packages that bundle Stor98. All binaries are the free
(release) build. Only the x64 image is signed; the 32-bit images never are. How and why:
[docs/BUILD.md](../docs/BUILD.md), "Test signing".

## Cutting a release

Once, before the first signed cut, the maintainer makes the project test certificate and
records its thumbprint (docs/BUILD.md, "Test signing"):

```
powershell -ExecutionPolicy Bypass -File scripts\make-testcert.ps1
```

Then, for each version:

1. Set the version in `src\stor98ver.h` and the `DriverVer` (date and version) of
   `inf\stor98-x86.inf` and `inf\stor98-x64.inf`; write the `history.md` entry; commit.
2. Stage and sign (builds every target, then signs the x64 image with the project test
   certificate; the .pfx password is asked for without echo):

   ```
   powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Stage -TestSignPfx scripts\local\testsign\stor98-test.pfx
   ```

   This writes `out\stage\<version>\` (with `x64\stor98-test.cer`), the unsigned x64
   reference image `out\stage\<version>.x64-unsigned.sys`, the stage manifest
   `out\stage\<version>.stage.json` and symbols in `out\stage\<version>-symbols\` (`9x\`,
   `x86\`, `x64\`). The key
   is made once with `scripts\make-testcert.ps1` (docs/BUILD.md, "Test signing"); only the
   maintainer holds it, and agents never sign a release or create or import a real
   certificate. Delete the key-container files SignTool leaves, which the script names.
3. Alternative to `-TestSignPfx`, for another certificate (for example xhci98's test
   certificate, so test machines trust one signer): stage without it, then sign the stage.
   The layout is the same.

   ```
   powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Stage
   powershell -ExecutionPolicy Bypass -File scripts\sign-x64.ps1 -Pfx <file.pfx>
   powershell -ExecutionPolicy Bypass -File scripts\sign-x64.ps1 -Thumbprint <sha1>
   ```

   Options: `-TimestampUrl <url>`, `-CrossCert <file>` (production kernel signing; verified
   with `SignTool verify /kp`, otherwise `/pa`).
4. Finalize:

   ```
   powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1 -Finalize
   ```

   It refuses unless the staged x64 image carries an Authenticode signature (SHA-1,
   vouching for this image), `x64\stor98-test.cer` is the signer's certificate, the image
   is the unsigned reference plus that signature and nothing else, every other staged file
   is unchanged since `-Stage`, and the stage holds exactly the files of the layout above.
   It also refuses unless HEAD is the commit the stage was built from, the work tree has no
   changes, and `readme.txt`, the INFs, `LICENSE` and `NOTICE.TXT` made again from the tree
   equal the staged ones.
   It then copies the stage here and writes `SHA256SUMS.txt`.
5. Commit `releases\<version>\`, then upload.

To install-test a tree without cutting anything, `make-release.ps1 -Stage -Test` writes an
unsigned stage in the same layout to `out\stage\<version>-test\`, with `readme.txt` headed
TEST BUILD. It needs no `history.md` entry and no commit (its `readme.txt` says when the tree
had changes), is never signed and cannot be finalized. `-Stage -Test -NoBuild` stages the
images already in `out\free\`; a release stage always builds.

## What users of the x64 driver do

The generated `readme.txt` tells them; in short:

- **Vista x64 and Windows 7 x64**: once, in an elevated Command Prompt,
  `bcdedit /set testsigning on`, then restart. "Test Mode" shows on the desktop and the
  driver loads at every start, and installing it in test mode starts it at once, without a
  restart. To undo: uninstall Stor98 or `sc config stor98 start= disabled` first, then
  `bcdedit /set testsigning off` and a restart. For a single start without test mode: F8,
  Disable Driver Signature Enforcement. With BitLocker on the system drive, suspend it
  before changing the boot setting.
- The service is demand-start, so without test mode the controller shows Code 52 and
  Windows starts normally.
- Setup still says it cannot verify the publisher (the package has no catalog): "Install
  this driver software anyway".
- Importing `x64\stor98-test.cer` into Trusted Root
  Certification Authorities and Trusted Publishers is optional and not needed; it does not
  remove that prompt.
- **XP x64, Server 2003 x64 and all 32-bit Windows** do not check driver signatures and
  need none of this.
