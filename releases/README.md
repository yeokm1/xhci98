# Released builds

The newest entry in `history.md` names the current release. This file does
not say which version is current, because a sentence saying so goes stale at
every cut and nothing here reads it; the `history.md` entry is written by the
same cut that creates the directory. The one version named below is the first
release, which does not change.

`1.0.0.0` is the first release. The packages cut while the work was going on
were numbered `0.x`, none of them was uploaded or given to anyone, and they
were removed when `1.0.0.0` was cut, so this tree holds one version directory
and `history.md` holds one entry. A `0.x` number carries no claim of being
finished, which is what it was for; nothing is expected to carry one again.

Each subdirectory is one released version of the driver, named for the version
in its `DriverVer` (`1.0.0.0/`, and so on), and each is produced by
`scripts\package\make-release.ps1` rather than assembled by hand.

**A version directory is written once and is never edited afterwards**, not to
fix a typo, not to correct a hash, not to regenerate one file. If what was
published under a number is wrong, cut the next version through
`make-release.ps1` and say in `history.md` what changed. A directory edited in
place leaves two different byte states answering to one version name, and
nothing a user holds says which one they have.

The one qualification is a version nobody holds. Until the first upload, a
finding against the current version re-cuts it under the same number with
`-Force` and says so in its `history.md` entry, because a number that was
never given to anyone has not been spent; `1.0.0.0` was re-cut that way on
2026-08-30. Once a version has been uploaded, the rule above is absolute.

That rule binds the generated files as much as the binaries. `readme.txt`,
`LICENSE` and `NOTICE.TXT` are outputs of `make-release.ps1`, so the way to
change any of them is to change the generator and cut a version.

Each version's `readme.txt` is a standalone install and usage guide: check the
machine, complete the media, install per target, use it, diagnose it, plus a
registry reference and the release history. It assumes the reader has the
directory and nothing else. The same script generates it, and its procedure is
transcribed from `docs/using/release-notes.md`; when that file changes, the
template near the end of `make-release.ps1` has to change with it.

`xhciqual/` carries the DOS qualifier and only its read-only path. The numbered
`.BAT` wrappers stay in the repository, because they drive the staged active
tests that take ownership of the controller and reset it. What a user needs is
the one command that answers "will this driver work here", and the generated
`readme.txt` is built around that command, including that it must be run from
real DOS rather than a DOS box inside Windows. That is a correctness
requirement rather than a preference: the tool needs identity-mapped memory
and direct hardware access. `make-release.ps1` refuses to publish a
`XHCIQUAL.EXE` older than any `.c`, `.h` or `.asm` beside it.

The wrappers are withheld; knowledge of them is not. The published `readme.txt`
carries a `COMMAND LINE` section listing every option the executable accepts,
split into the read-only ones and the ones that take the controller over, with
the safety rules attached to the second group. Documenting one command and
leaving the rest to `--help` would hide nothing, since the flags are in the
binary either way; it would only mean a user first meets `--full` without the
warning beside it. The section is generated from the template in
`make-release.ps1`, and `xhciqual/main.c`'s own `print_usage`/`print_help` are
the authority for what the flags are, so a flag added there needs a line added
here.

`history.md` is the one hand-written piece. `make-release.ps1` refuses to
publish a version with no entry in it and embeds the file into each release, so
every published directory carries the history up to and including itself, and
no version can ship that a user cannot tell apart from the one before it.

```
releases/
  README.md              this file (maintainer-facing, stays markdown)
  history.md             the changelog - hand-written, one entry per version
  1.0.0.0/
    readme.txt           generated - the standalone install and usage guide
    LICENSE              copied from the repository root; the readme cites it
    release/
      xhci98.inf
      xhci98.sys         the build to install
    debug/
      xhci98.inf
      xhci98.sys         the same driver, for diagnosis only - it carries no
                         per-line trace either; that is the qemu flavour's
    xhciqual/
      XHCIQUAL.EXE       the DOS "will this machine work" checker
      XHCIQUAL.MAP       turns a crash address back into a source location
      readme.txt         generated - how to run it, and the verdicts
      NOTICE.TXT         generated - the third-party notices XHCIQUAL.EXE
                         carries, because it is linked as a DOS/32A executable
                         and statically includes that extender and the Open
                         Watcom C runtime
    xhcisnap/
      XHCISNAP.EXE       the Windows tool that reads the driver's own log off
                         a running machine and writes a report to paste into
                         a bug report - on Windows 98 it is the only way to
                         get anything out at all
      readme.txt         generated - the four steps, and why step 1 is not
                         optional
      NOTICE.TXT         generated - the third-party notices XHCISNAP.EXE
                         carries, because MSVC 6.0's C runtime is statically
                         linked into it
```

The upload set is this tree zipped, and since 1.0.0.1 adds nothing to it;
see "What actually gets uploaded" below.

That is the schema a release cut today has: the generated readme sends the
reader to a licence, so the directory has to carry one, and the two tools are
the published files that are not only this project's code, so each carries the
notices for what is linked into it.

Read the block above as what a cut produces today. Because a version directory
is written once, a directory cut before a piece of that schema existed keeps
the shape it was cut with, and the way to tell is to look in it rather than
here.

The whole `xhciqual/` block is absent under `-SkipQualtool` and the whole
`xhcisnap/` block under `-SkipSnapTool`, each one's `NOTICE.TXT` with it.
Neither switch belongs in a real cut: a release cut with `-SkipSnapTool`
publishes a driver whose read channel nobody can open, which on Windows 98 is
the whole of what a user could have sent back.

The published files are `readme.txt`, not `README.md`. They are read on the
target machine, in Windows 98's Notepad or DOS `EDIT`, where a `.md` file is
neither rendered nor associated with anything and markdown syntax is just
noise. Plain text, 78 columns, CRLF, pure ASCII. This file and `history.md`
stay markdown: they are read here, in the repository.

## These directories are complete install media, since 1.0.0.1

`src\xhci98.inf` names two files in `[SourceDisksFiles]`, and both are this
project's own work: `xhci98.sys` and `xhci98.inf`. A version directory
carries exactly those two per flavour, and so does the release download.

Three files the driver depends on are not on the media, because they are
the operating system's own: `usbd.sys` (the USB 2.0 root hub imports it on
both targets), `usbhub.sys` (Windows 98's composite parent and the NT
targets' hub driver) and, on the NT targets, `usbport.sys` (the stack
itself; on Windows 98 NUSB or SweetLow's package places it). Nothing on an
xHCI-only machine ever placed them, so the INF names `LayoutFile=layout.inf`
and the Windows setup engine copies each from the OS's own install source,
the CABs on the hard disk or the Windows 98 CD, and the NT targets'
`Driver Cache\i386`, never overwriting a file already there. On an xHCI-only
Windows 98 machine the install therefore asks for the Windows 98 SE CD; the
generated `readme.txt` says so in its section 3. Until 1.0.1.0 the NT path
copied `usbd.sys` only, and an NT install that had never seen a USB
controller had no `usbport.sys` for the driver to load against (Code 39,
measured on a Windows XP guest on 2026-09-03).

That is a change. From `0.0.0.4` to `1.0.0.0` the download carried three
Microsoft files the tracked tree did not, `usbd98.sys`, `usbd2k.sys` and
`usbhub98.sys` (the two `usbd.sys` builds and Windows 98 SE's `usbhub.sys`,
renamed so that each install path reached only its own), by a decision
`docs/contributing/legal-provenance.md` section 5 records and, since
2026-09-02, records as withdrawn before any upload. The INF gate now refuses
an INF or a package that names a Microsoft file (`OS-MEDIA`, `PKG-MSFILE`),
so the download cannot drift back.

```
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor release
```

assembles the same two files under `out\pkg-release\` with every gate run
against them; installing from a version directory here is the same thing.

### What actually gets uploaded

`make-release.ps1` produces two things, and only one of them is committed:

- `releases\<version>\`: tracked, two files per flavour, what you see here.
- `out\upload-<version>\` and `out\xhci98-<version>.zip`: git-ignored, the
  same tree. The zip is the GitHub release asset. It is the one of the two
  named after the project because it is what a stranger downloads and has to
  recognise afterwards; the directory is a workspace inside `out\`. The
  archive carries no top-level directory, so its name is all the download
  says about itself until it is unpacked.

The assembly gates each flavour directory as the install media it is
(`check-inf.ps1 -PackageDir`: every file the INF names present, and no
Microsoft file beside them), refuses a file the INF does not name, and
writes the archive's entry names with forward slashes so that `unzip` on a
Linux or macOS host unpacks it into directories. Do not hand-assemble the
asset.

## release or debug

`release/` is the build to install. No per-line tracing: none of the
`XHCI_DBG_*` sites compile into it, so it does not carry the `0xE9` debug
console and does not import `WRITE_PORT_UCHAR`. It does import `DbgPrint` and
`KeGetCurrentIrql`, and neither is a leak:

- `DbgPrint` is the `XhciLogDebugView` sink (task 11-V.9), which hands the
  stored log to a capture tool in one bulk dump at `StopController`.
  `AGENTS.md` ("Coding Style") states this as the single exception to the
  no-`DbgPrint` rule and why. What the rule forbids is per-line printing from
  DPC and ISR contexts, which is what bugchecks Windows 98 on real hardware;
  one dump of a bounded ring from a PASSIVE-level lifecycle callback is a
  different profile, not a smaller amount of the same one.
- `KeGetCurrentIrql` is the log flush measuring the IRQL it was called at and
  refusing above `PASSIVE_LEVEL`, rather than resting on a derivation of which
  callbacks are passive.

Both are `all`-flavour rows in `scripts\import-gate\xhci98-imports.allow`. The
import gate cannot police the call count: an import table names a symbol once
however many times the code calls it, so the gate answers "may this binary
import `DbgPrint` at all", not "how many call sites are there". Keeping the
exception to one site is a review obligation on whoever adds the second one.

`debug/` is for diagnosing a machine you cannot attach a debugger to. It
imports the same two symbols the release build does. What it adds over
`release` is the DDK's own `DBG`: both flavours compile `/Oxs` and differ by
`/Oy-` against `/Oy`, so the checked build keeps frame pointers, and it also
sets `VS_FF_DEBUG` and reports a `DEBUG` bit in a snapshot. There are no
asserts; every `XHCI_C_ASSERT` in this driver is compile-time and fires in
every flavour. It is not the slow, chatty build that "debug" usually means,
though `/Oy-` is a codegen difference and nobody here has measured what it
costs.

`debug` carries no per-line trace. Every `XHCI_DBG_*` macro compiles to
nothing without `XHCI_DBG_LIVE`, which `src\sources` defines for `qemu` and
for nothing else (it gates `XHCI_DBG_LIVE` / `XHCI_DBG_E9` on
`BUILD_ALT_DIR == chk_qemu`). What both shipping flavours carry instead is
`src\xhci_log.c`'s ring, recorded at any IRQL and handed over in one bulk dump
from the PASSIVE flush. Recording is not emission, and it is the ring that
`XHCISNAP` reads. The debug build is cut at the same version as the release
build so a report can be asked for later without rebuilding a version that
would then have to be reproduced exactly.

Why the split exists: the debug build of a development package gave a real
Windows 98 machine a `Code 2` and loaded nothing, with
`HAL.dll!WRITE_PORT_UCHAR` as the sole import delta from the release build
beside it, and a build without the `0xE9` pair loaded clean and drove devices.
Read that second binary as narrowly as `run-13e.md` writes it: it was itself a
diagnostic build, so it differed from the failing one by the do-not-deploy
marker as well as by the define under test, and the matched control that would
have removed that variable was built and never booted.

Whether the cause was the import failing to resolve or the port being decoded
is still open (defect 2b, `run-13e.md` P6); its two discriminating binaries are
built but not booted.

What the split settles is the default rather than the mechanism: `debug` is the
flavour a user with a problem is told to install, so it must load on metal, and
it no longer carries the import at all. The import gate's one `qemu required`
row makes a published binary carrying it impossible.

Both are called `xhci98.sys` and both carry the same `DriverVer`, so nothing
about a copied file says which one it is except the `VS_FF_DEBUG` flag
`src\xhci98.rc` sets under `#if DBG` (the Version tab, and
`(Get-Item x).VersionInfo.IsDebug` on the build host) and the in-image
`XHCI98_FLAVOUR_*` marker, which is the one that can name all three flavours
rather than only checked-versus-free. That is why they are in separate
directories and must never be merged into one. `make-release.ps1` verifies
that flag on each binary rather than trusting which `obj` directory it came
from.

### These are the names the whole repository uses

| Published as | Build flavour | Built into |
|---|---|---|
| `release/` | `release` | `src\objfre\i386` |
| `debug/` | `debug` | `src\objchk\i386` |
| *(never published)* | `qemu` | `src\objchk_qemu\i386` |

There is a third flavour, and it is kept out of here by design. `qemu` is
`debug` plus the port-`0xE9` trace mirror and the `HAL.dll!WRITE_PORT_UCHAR`
import that carries it: the sole import delta between the two binaries of the
development package whose debug build gave a real Windows 98 machine a
`Code 2`. Why that build failed is not established (see above, and defect 2b).
`qemu` is built and gated like the other two and staged into a release
directory by nothing; `make-release.ps1` publishes the two rows above and no
other.

`-Flavor`, `out\pkg-*`, `build-driver.cmd` and all of `docs/contributing/` say
`release` and `debug` too, so a release directory needs no translating. The
DDK's own words for the same two builds are free and checked. "Free" reads as
free of charge to anyone who has not met that convention, and "checked" says
nothing at all to a first-time reader, so they survive only where the DDK
itself requires them: `setenv.bat`'s flavour argument, and the `objfre` /
`objchk` directories it writes into. Both published names are 8.3-clean,
because a release directory can end up on media a Windows 98 setup engine has
to read.

`docs/using/release-notes.md` is the user-facing document: requirements, the
qualifier to run first, install steps per target, the diagnostic settings and
the `XHCISNAP` report tool that reads them back, and the known limitations.
Read it before installing anything.

## Cutting a release

```
powershell -ExecutionPolicy Bypass -File scripts\package\make-release.ps1
```

It takes the version from the INF's `DriverVer`, packages both flavours
through `make-package.ps1` (the binaries come from a prior
`scripts\build-driver.cmd all`) so every gate that protects an install runs, copies only
the two publishable files out of each, and refuses if the two binaries are not
actually distinct or are not actually the flavours they are being published
as. An existing version directory is never overwritten without `-Force`.

Bumping the version is a separate step. The number and the release date are
edited in `src\xhci_version.h`, and `src\xhci98.inf`'s `DriverVer` is changed
to match, since an INF cannot include a header; the INF gate checks the two
agree on every build. `xhciqual/qual.h` and `xhcisnap/xhcisnap.c` expand the
header's macro, because both tools are published inside the release directory
and both print their version into what a user sends back, and
`make-release.ps1` throws on a staged tool older than the header or on a tool
source that has stopped expanding it. `docs/contributing/build-and-test.md`,
"Versioning the driver", is the authority.
