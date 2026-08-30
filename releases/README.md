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

The upload set adds `usbd98.sys`, `usbd2k.sys` and `usbhub98.sys` to each of
`release\` and `debug\` and changes nothing else; see "What actually gets
uploaded" below.

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

## These directories are not complete install media - the upload set is

`src\xhci98.inf` names five files in `[SourceDisksFiles]`, and only two of
them are this project's own work:

| File | In `releases\<version>\` | In the GitHub release download | Why |
|---|---|---|---|
| `xhci98.sys` | yes | yes | this project's, GPL-2.0-only |
| `xhci98.inf` | yes | yes | this project's, GPL-2.0-only |
| `usbd98.sys` | no | yes | Windows 98 SE's own `usbd.sys` |
| `usbd2k.sys` | no | yes | Windows 2000 SP4's own `usbd.sys` |
| `usbhub98.sys` | no | yes | Windows 98 SE's own `usbhub.sys`; the Windows 98 install path only |

The last two columns differ by design. The last three files are Microsoft
binaries, and `AGENTS.md` forbids tracking a third-party binary, so a version
directory in this repository carries the driver and its INF and stops there.
A user downloading a release needs all five, so the release download carries
them and the repository does not. A release asset can be withdrawn; a git blob
cannot be without rewriting history, so the asset was chosen over a tracked
file. `docs/contributing/legal-provenance.md` section 5 records the
decision.

Note the tense: that describes the asset `make-release.ps1` assembles. No
release has been uploaded yet, so it is a decided channel rather than one that
has carried anything; section 5 of `legal-provenance.md` carries the status
note. `usbhub98.sys` joined the two `usbd.sys` builds on the same grounds and
by the same mechanism (task 13-E.1's remedy): composite devices do not bind on
Windows 98 without it, and an xHCI-only machine never gets one from setup.

Installing from a version directory in this repository as it stands will fail
on a machine that has no `usbd.sys`, and it fails in a way that reads as a
fault in this driver: `usbhub20.sys` imports `USBD.SYS` on both targets,
nothing on an xHCI-only machine ever places it, and the missing import fails
the root hub with `0xc0000034` naming `usbhub20.sys`. A downloaded release does
not have this problem. From a clone, complete the media first:

```
powershell -ExecutionPolicy Bypass -File scripts\package\extract-usbd-sources.ps1 -Win98Iso <w98se.iso> -Win2KIso <win2ksp4.iso>
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1 -Flavor release
```

That stages the per-target Microsoft builds from your own Windows install
media (the two ISO arguments; without them the script has nothing to stage
and says so) into `tools\`, checks each against
`scripts\package\usbd-sources.expected` by SHA-256, and assembles the real
package under `out\pkg-release\`: the driver, its INF, the two `usbd.sys`
builds and Windows 98's composite parent `usbhub.sys`.

### What actually gets uploaded

`make-release.ps1` produces two things, and only one of them is committed:

- `releases\<version>\`: tracked, two files per flavour, what you see here.
- `out\upload-<version>\` and `out\xhci98-<version>.zip`: git-ignored, the same
  tree plus every Microsoft file `scripts\package\usbd-sources.expected` names
  (`usbd98.sys`, `usbd2k.sys` and `usbhub98.sys`) in each flavour directory.
  The zip is the GitHub release asset. It is the one of the two named after
  the project because it is what a stranger downloads and has to recognise
  afterwards; the directory is a workspace inside `out\`. The archive carries
  no top-level directory, so its name is all the download says about itself
  until it is unpacked.

Those three Microsoft files are copied out of the `out\pkg-<flavor>\`
directories `make-package.ps1` had already authenticated, and are then
re-checked by SHA-256 in the assembled upload tree, because the bytes that
matter are the ones that go up, and a swap between `usbd98.sys` and
`usbd2k.sys` is invisible after install (both are called `usbd.sys` on the
target). Do not hand-assemble the asset; that check is the only thing standing
between a swapped pair and a user whose root hub fails for the reason this
mechanism exists to prevent.

Each flavour directory in the upload set is therefore complete install media
in its own right, the same shape as `out\pkg-<flavor>\`. All three files
appear in both `release\` and `debug\`, because Windows resolves
`[SourceDisksFiles]` relative to the INF and each directory has its own.

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
