<#
.SYNOPSIS
Publish a built driver into releases\<version>\<flavour>-<arch>\ - four
directories for an ordinary cut (release-x86, debug-x86, release-x64,
debug-x64).

.DESCRIPTION
`releases\` is the tracked, published half of packaging. It carries the two
files that are this project's own work - `xhci98.sys` and `xhci98.inf` - and
since release 1.0.0.1 those are the only files on the media at all: the INF has
the operating system supply `usbd.sys` and `usbhub.sys` from its own install
source through `LayoutFile`, joined by `usbport.sys` on the NT path in 1.0.1.0
and by `usbui.dll` on every path in 1.0.2.0.
It is a
separate step from `make-package.ps1` because the package is the gated build
output and the release is the tracked, written-once record of it. See
`releases\README.md`.

**The upload set is the other output, and it is not the tracked one.** It
is the published tree, zipped: `out\upload-<version>\` and
`out\xhci98-<version>.zip` under the git-ignored `out\`, the directory a
workspace and the zip the GitHub release asset, which is why only the second
is named after the project. Since release 1.0.0.1 it carries no Microsoft
file: the INF has the operating system supply `usbd.sys`, `usbhub.sys` and
`usbui.dll` from its own install source (`docs\contributing\legal-provenance.md` section
5 records the decision and what it withdrew). It is still assembled here
rather than by hand, because the layout is checked - every flavour directory
is gated as the install media it is, nothing the INF does not name goes up -
and because the archive's entry names have to be ones a non-Windows unzip
can read.

A release also carries **`xhciqual\`**, the DOS qualification tool, and a
plain-text `readme.txt` at each level. The tool is deliberately *not* on the
install media - that media is a Windows setup payload in which the INF names
every file, and a DOS executable no INF section references would be the only
file on it setup never touches - but a release directory is not install media,
so a sibling subdirectory is exactly where it belongs. It is also built by a
different toolchain (Open Watcom), so it is copied from `xhciqual\` rather than
built here; the freshness check below is what stops a stale one shipping.
`xhcisnap\`, the log reader, sits beside it on the same terms, and since
roadmap-hcd task 37.5 so does **`portsw\`**: the Intel port switchover's
`portsw.vbs`, `portsw-on.bat` and `portsw-off.bat`, copied once from `src\`
rather than into every flavour directory, since they are the same for every
architecture and flavour and the setup engine never reads them. It carries
no `readme.txt` of its own: the top-level readme's section 9 explains them.
Since roadmap-hcd task 38.10 the three sit under **`tools\`** in the version
directory (`tools\xhciqual\`, `tools\xhcisnap\`, `tools\portsw\`), so the
version root holds only the flavour directories, the readme and the licence;
a version published before 2.4.0.0 keeps its tools at its root (portsw\ only
in 2.3.1.0; 2.3.0.0 has the scripts per flavour, older none), and the upload
assembly reads each tree at the layout its version was published with.

**Only the executable and its MAP go out**, not the numbered `.BAT` wrappers.
Those drive the staged active tests - which take ownership of the controller,
reset it and enable bus mastering - and are development instrumentation. What a
user needs is the one read-only command that answers "will this driver work on
this machine", and that is `XHCIQUAL` with no arguments.

**The report batches go out beside their tools** (roadmap-hcd task 38.1):
`QUALREP.BAT` beside `XHCIQUAL.EXE` (a read-only probe whose log is named from
the DOS clock), and `SNAPON.BAT`, `SNAPGET.BAT` and `SNAPOFF.BAT` beside
`XHCISNAP.EXE` (the log channel on, the report and the dump with the channel
left on, the channel off). They are what a field report asks its reporter to
run. They are copied from `xhciqual\` and `xhcisnap\`, published under 8.3
upper-case names, and held to their tools' directories alone
(`Assert-ReportBatchLayout`); a version published before 2.4.0.0 has none. What
they say is held to one checker, `Get-ReportBatchFaults` in `package-common.ps1`:
by `test-package.ps1` on the repository's sources, and here on the batches
actually staged (which must also be the repository's bytes) and on those in the
tree an upload set is assembled from, `-UploadSetOnly`'s included
(`Assert-ReportBatchContent`).

**Both published readmes document the whole command line even so**, split into
read-only options and active ones: the qualifier's own `readme.txt` in a
`COMMAND LINE` section, and the driver's `readme.txt` in step 1. The wrappers
are what stays behind; the *modes* they drive are in the one published
executable whatever this readme says, so listing them - with what each one does
to the machine, next to the safety rules - is what makes the read-only path a
choice the reader can make rather than the only thing they were told about. The
one command that answers the question is still the one each readme opens with.
**They are two copies of one list and both live in this file**, so a flag added
to `xhciqual\main.c` needs both changed; `print_usage` / `print_help` there are
the authority for what the flags are, this is the authority for what a user is
told about them. The driver readme keeps the safety *rules* pointed at the
qualifier's readme rather than copying those too, so the duplication is the
option table and stops there.

**`readme.txt`, not `README.md`.** The audience reads these on the target
machine - Windows 98's Notepad, or DOS `EDIT` - where a `.md` file is neither
rendered nor associated with anything, and markdown syntax is just noise. Plain
text, 78 columns, CRLF.

Every published binary is at one version, in a directory of its own. They are
byte-different builds sharing a file name and a `DriverVer`, so **nothing a
user can SEE identifies one**: the `VS_FF_DEBUG` flag `src\xhci98.rc` sets under
`#if DBG` separates release from debug and says nothing about the
architecture, and Explorer shows nothing that separates the two architectures
at all. What IS in the file is the in-image `XHCI98_FLAVOUR_*` marker, which
names all three flavours, and the PE header's Machine field, which the import
gate reads to tell `0x014C` from `0x8664` - both need a tool.
This script checks the flag on each staged binary rather than
trusting which obj directory it came out of - packaging objfre twice is a
silent mistake otherwise, and it is silent on the target too.

**A published directory is a flavour AND an architecture** (roadmap task 21.3):
`release-x86\`, `debug-x86\`, `release-x64\`, `debug-x64\`. The flavour half is
the vocabulary this project uses everywhere - `-Flavor release|debug`,
`out\pkg-<flavour>-<arch>\` on the build side, and the same two words
throughout `docs\contributing\`. The DDK's own words are "free" and "checked" -
"free" reads as *free of charge* to anyone who has not met that convention -
and they survive only where the DDK itself requires them: `setenv.bat`'s
flavour argument and the `src\objfre` / `src\objchk` trees it writes into.
**These names are NOT 8.3-clean**, and that is a deliberate change from the
bare `release\` and `debug\` that were: `release-x86` is eleven characters, so
a Win98 setup engine reading this directory off media sees a short-name alias
rather than the name printed here. That cost was taken with task 21.3 because
the alternative - two architectures' `xhci98.sys` in one directory, or names
that do not say which is which - is worse, and because nothing in the install
route resolves a directory BY name: the user points Have Disk at whichever
directory they opened, and readme.txt tells them which to open in the same
words this script renders into it.

Every gate lives in `make-package.ps1` and is reached by calling it, not by
reimplementing it here: the host test suite, the import gate, the INF gate, and
the check that the built binary's own version resource matches the INF staged
beside it. A release is exactly a package that passed all of them, minus the two
files that may not be tracked.

.PARAMETER Version
The version to publish as. Optional: the default is read from the INF's
DriverVer, which is the authority the rest of the toolchain already uses. When
given, it must match the INF - this parameter is a statement of intent to be
checked, not an override.

.PARAMETER Flavor
Which flavours to publish - `release` or `debug`, matching
`make-package.ps1`, and each is published under its own name. Defaults to both.
Publishing one at a time is supported, but a version directory holding only
`debug\` is not something to release.

**`qemu` is accepted by this parameter and then REFUSED**, which is deliberate:
there are three build flavours since task 13-L.1 and a parameter-binding error
would say only that the word is unknown, when the fact worth reading is that the
flavour exists and must never be published. It carries the port-0xE9 mirror -
`HAL.dll!WRITE_PORT_UCHAR`, the sole import delta between 0.0.0.4's two
published binaries, of which the debug one gave the ThinkPad E460 a Code 2
under Windows 98 SE - and it is built and
gated like the other two so the instrument keeps working in the emulator.
`scripts\package\make-package.ps1 -Flavor qemu` stages it for a guest; nothing
publishes it.

That refusal is not the only guard, because a flavour name is not evidence: the
binary staged for each published flavour is checked against the
`XHCI98_FLAVOUR_*` marker in its own image, below. `VS_FF_DEBUG` cannot do that
job - `debug` and `qemu` are both checked builds and both set it.

This script does not build; it publishes what is already built and gated. Run
`scripts\build-driver.cmd all` first, which builds and gates all three flavours,
so that the one that is never published has still been through every gate.

.PARAMETER Arch
Which architectures to publish - `x86` or `x64`, which are `make-package.ps1`'s
`x86` and `amd64`. **Defaults to both**, since the owner's instruction of
2026-09-09 and on the strength of roadmap task 21.5, so an ordinary cut writes
four directories. It defaulted to `x86` alone until then, because until then no
amd64 binary of this driver had ever executed.

Read the x64 half of a cut as narrowly as its evidence is: three guests - one
Windows XP x64 and, from 1.1.0.0, one Vista x64 and one Windows 7 x64 - and,
from 2.3.0.0, one real machine, a Dell Latitude E7250 under XP x64 and 7 x64
(the owner's bench, 2026-10-11), against the x86 half's install legs.
Publishing it by default is what claiming the target means, not a
claim that the two halves are equally attested.

`-UploadSetOnly` is the one mode that does not follow the default: it derives
the architectures from the published tree instead, because every version
published before 1.1.0.0 is x86-only and re-assembling one must not fail for
want of a 64-bit directory that cut never wrote. An `-Arch` passed explicitly is held to
exactly, in that mode as in every other.

`scripts\package\test-package.ps1` asserts the default, so changing it back is
as deliberate an act as changing it forwards was.

.PARAMETER ReleasesDir
Where the version directory is created. Defaults to `releases\` in the
repository.

.PARAMETER Force
Overwrite an existing version directory. Off by default: a published version is
meant to be immutable, and re-cutting one silently is how two different binaries
end up having been called by the same version.

.PARAMETER NoTargetEvidence
Passed through to make-package.ps1 for a host with no extracted target binaries
staged.

.PARAMETER AllowUnstampedDriver
Publish a driver binary that has no `xhci98.srcstamp` beside it, so nothing has
checked the published bytes against `src\`. `scripts\build-driver.cmd` writes
that stamp, so the only binary this applies to is one built before the stamp
existed - where rebuilding to acquire one would move the PE link timestamp and
invalidate every reading already taken on these exact bytes. Without this the
cut refuses; a stamp that MISMATCHES is refused either way.

.PARAMETER SnapToolDir
Where `XHCISNAP.EXE` is built. Defaults to `xhcisnap\` in the repository. It is
staged into `releases\<version>\tools\xhcisnap\`, beside the DOS qualifier's own
directory, and it is what a user runs to switch the driver's read channel on and
to take a dump off the machine - **the only route this driver has for getting
evidence off Windows 98**. It has never been in a release before task 13-L.2.

.PARAMETER SkipSnapTool
Do not stage `XHCISNAP.EXE`. There is no reason to on an ordinary host - it
builds with the in-repo MSVC 6.0 and installs nothing - so this exists for the
packager's own self-tests, and a release cut with it publishes a read channel
nobody can open.

.PARAMETER QualtoolDir
Where to take `XHCIQUAL.EXE` and its `.MAP` from. Defaults to `xhciqual\` in the
repository.

.PARAMETER SkipQualtool
Publish without the `tools\xhciqual\` subdirectory. For a host with no Open Watcom
build available; the resulting release is incomplete and says so.

.PARAMETER SkipUploadSet
Publish `releases\<version>\` without assembling the upload set. The tracked
directory is unaffected either way - what this skips is the artifact that
actually gets uploaded, so a release cut with it has nothing to publish yet.
For re-cutting a tracked directory when the point is the tracked directory.

.PARAMETER UploadDir
Where the upload set is assembled. Defaults to `out\` in the repository, which
is git-ignored; the asset is generated output and stays out of the tree.

.PARAMETER UploadSetOnly
Assemble the upload set from the tracked `releases\<version>\` tree alone, and
publish nothing. Nothing is built, nothing is read from `out\`, nothing is
written under `releases\`, and the build-side gates - the host suite, the
import gate, the binary's version resource - do not run again: this mode never
touches a binary, so there is nothing for them to answer about. The **INF gate
does** run, once per assembled flavour directory, because that one is about the
media rather than the build - see the note under the assembly itself. A fresh
clone with no `out\` at all can therefore rebuild the current cut's asset.

**This exists so that a broken or lost upload asset is not a reason to re-cut a
release.** The upload set is assembled after the publish, so without this mode
the only scripted way to produce it again is `-Force`, which rewrites a
written-once version directory - the one thing `releases\README.md` says never
to do. (`-Force` does not rebuild anything: it stages from the existing
`src\objfre` / `src\objchk` trees and every gate still runs, so the binaries
come through byte-identical. What it skips is the single "this version
directory already exists" refusal.) That trap fired for real: the
first cut's asset was malformed and there was no way to rebuild it in place.

**It rebuilds the current cut's asset only.** The INF gate it runs encodes the
current release's rules, and an older cut fails the rules added since it was
cut (measured read-only on 2026-09-05: the 1.0.0.1 INF fails six of them, all
added by 1.0.1.0). A `-Version` other than `src\xhci98.inf`'s `DriverVer` is
refused with that reason; to rebuild an older cut's asset, check out the commit
that cut it and run this mode there, where the gate is the one that cut passed.
roadmap Phase 20, F15 has the record. Until that fix this mode also required
`out\pkg-<flavour>\` to hash-match the published binaries, a dependency left
over from when the media carried files the package supplied; since 1.0.0.1
nothing from the package enters the asset, and the check is gone.

.PARAMETER PackageRoot
Where the gated `pkg-<flavour>\` directories are read from and written to by an
ordinary cut. Defaults to `out\` in the repository, which is where
`make-package.ps1` writes them. Not read under `-UploadSetOnly`.

.PARAMETER TestLabel
Cut a TEST build rather than a release: the generated readme.txt opens with a
banner reading "TEST <version>, <label>." and saying the build is not
published, so the person who unpacks it sees that before anything else. The
label is the build's state in a few words, for example `bench pending` (roadmap
Phase 36's 36.final cut 2.2.0.3 as "TEST 2.2.0.3, bench pending"). Printable
ASCII, at most 50 characters. A test build is never published, so the cut
refuses to write it under the repository's own `releases\`: pass `-ReleasesDir`
and `-UploadDir` under the git-ignored `out\`. Refused with `-UploadSetOnly`,
which renders no readme. Empty (the default) cuts an ordinary release and
renders no banner.

.PARAMETER TestSignPfx
The project test certificate's .pfx (design record 19; owner, 2026-10-11).
**Required of every cut that publishes an x64 directory**, and refused with no
switch past it when absent: each x64 leg's make-package.ps1 run is passed it,
signs xhci98.sys and xhciuas.sys at staging (SHA-1 file digest, no timestamp,
no catalog, the INFs unchanged) and stages xhci98-test.cer, the certificate's
public half, beside them. This script then holds each published x64 binary to
that signature by this key's thumbprint and to the build's bytes, and the
published certificate to this key's. The password is read from
XHCI98_TESTSIGN_PW or, where that is unset, a prompt that does not echo, once,
before anything is built. Refused with -UploadSetOnly, which stages nothing,
and with an -Arch that has no x64. The x86 binaries are never signed. Where
the key is held and how it was made: design record 19.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\make-release.ps1

.EXAMPLE
powershell -File scripts\package\make-release.ps1 -Version 1.0.0.0 -Force

.EXAMPLE
powershell -File scripts\package\make-release.ps1 -UploadSetOnly
#>

[CmdletBinding()]
param(
    [string]$Version = "",
    [ValidateSet("release", "debug", "qemu")]
    [string[]]$Flavor = @("release", "debug"),
    [ValidateSet("x86", "x64")]
    [string[]]$Arch = @("x86", "x64"),
    [string]$ReleasesDir = "",
    [string]$QualtoolDir = "",
    [switch]$SkipQualtool,
    [string]$SnapToolDir = "",
    [switch]$SkipSnapTool,
    [string]$UploadDir = "",
    [switch]$SkipUploadSet,
    [switch]$UploadSetOnly,
    [string]$PackageRoot = "",
    [switch]$Force,
    [switch]$NoTargetEvidence,
    [switch]$AllowUnstampedDriver,
    [string]$TestLabel = "",
    [string]$TestSignPfx = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

$repo = Get-RepoRoot

#
# **qemu is a flavour and it is never published** (task 13-L.1). It is in the
# ValidateSet above so that asking for it produces this sentence rather than a
# parameter-binding error about an unknown word: the fact worth reading is not
# that the name is unrecognised, it is that the build exists, is gated like the
# other two, and must not leave this machine. It carries the port-0xE9 mirror -
# HAL.dll!WRITE_PORT_UCHAR - the sole import delta between 0.0.0.4's two
# published binaries, of which the debug one gave the ThinkPad E460 a Code 2
# under Windows 98 SE.
#
# This is the cheap half of the refusal. The half that cannot be talked past is
# the per-binary flavour-marker check in the publish loop, because a flavour
# NAME is not evidence about the image it was typed beside.
#
if ($Flavor -contains "qemu") {
    throw @"
the qemu flavour is never published.
It is the emulator and bench build: the port-0xE9 mirror
(HAL.dll!WRITE_PORT_UCHAR) and the live per-line trace. That import is the sole
delta between the two published 0.0.0.4 binaries, of which the debug one gave
the ThinkPad E460 a Code 2 under Windows 98 SE, so no binary a user can install
may carry it, and
scripts\import-gate\xhci98-imports.allow enforces that as "qemu required".
To put it on a guest, stage it without publishing:
  scripts\package\make-package.ps1 -Flavor qemu
A release publishes the release and debug directories of each architecture it
was asked for, and nothing else.
"@
}
#
# **One INF answers for the version and the date, and it is the 32-bit one.**
# Since roadmap task 21.3 there are two, and they must agree: the INF gate ties
# each to src\xhci_version.h on every build, and the gate's self-tests compare
# the two files' [Version] sections directly. So reading DriverVer from either
# gives the same answer, and reading it from a fixed one keeps the cut's
# version independent of which architectures it was asked for - a cut with
# -Arch x64 alone is still the same release at the same number.
#
$infPath = Join-Path $repo "src\xhci98.inf"
if ($ReleasesDir -eq "") { $ReleasesDir = Join-Path $repo "releases" }
if ($QualtoolDir -eq "") { $QualtoolDir = Join-Path $repo "xhciqual" }
if ($SnapToolDir -eq "") { $SnapToolDir = Join-Path $repo "xhcisnap" }
# The one place the version is edited (task 14.1.10). Both tools expand a macro
# out of it, so it is both what their declared version is read from and an input
# their staged binaries have to be newer than.
$versionHeader = Join-Path $repo "src\xhci_version.h"
if ($PackageRoot -eq "") { $PackageRoot = Join-Path $repo "out" }
if ($UploadDir -eq "") { $UploadDir = Join-Path $repo "out" }

function Write-GeneratedText {
    <#
    Every plain-text file this script generates goes through here, and there
    are five of them: the top-level readme.txt and a readme.txt plus a
    NOTICE.TXT beside each of the two DOS tools.  Until the 2026-09-07 audit
    only the first was checked, and the other four were written straight out
    with `Write-AsciiFile`; four of them were shipping 79-column lines.

    TWO GATES, BOTH OF THEM CHEAP AND BOTH OF THEM CAUGHT SOMETHING.

    78 columns.  These are read in Windows 98 Notepad and DOS EDIT, where a
    longer line wraps in the wrong place.  Widths are not fixed at authoring
    time either: names, sizes and hashes are substituted in, so a line's
    width depends on the manifest, and the first substitution overran by
    nine characters on the day it was written.

    ASCII.  `Write-AsciiFile` turns any non-ASCII byte into a question mark
    SILENTLY, and the top-level readme embeds `history.md`, which is
    markdown a person edits.  One typographic dash in a release entry would
    ship as `?` in a file nobody re-reads after generating it.
    `make-11v-media.ps1` already refuses non-ASCII for its INF rewrite; this
    is the same rule for the same reason.
    #>
    param(
        [string]$Path,
        [string[]]$Lines
    )

    $name = Split-Path -Leaf $Path

    $overLong = @($Lines | Where-Object { $_.Length -gt 78 })
    if ($overLong.Count -gt 0) {
        throw ("{0} has {1} line(s) past 78 columns, the first being:`n{2}" -f `
               $name, $overLong.Count, $overLong[0])
    }

    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        for ($c = 0; $c -lt $line.Length; $c++) {
            $code = [int][char]$line[$c]
            if ($code -lt 0x20 -or $code -gt 0x7E) {
                if ($code -eq 9) { continue }
                throw ("{0} line {1} column {2} is not printable ASCII (U+{3:X4}); Write-AsciiFile would replace it with a question mark. The line reads:`n{4}" -f `
                       $name, ($i + 1), ($c + 1), $code, $line)
            }
        }
    }

    Write-AsciiFile -Path $Path -Lines $Lines
}

function Resolve-DirectoryArgument {
    # **A directory parameter is made absolute here, and relative means the
    # caller's location.** `make-package.ps1` states the first half of the trap
    # in full and this file inherits it: [Path]::GetFullPath resolves against
    # the *process* directory, which Set-Location does not update, so a session
    # that has changed directory would resolve `-PackageRoot pkgroot` somewhere
    # the caller never named.
    #
    # The second half is this script's own, and it is worse than surprising.
    # These paths are used as string *prefixes*: the upload assembly derives
    # each staged file's relative path by cutting the package directory's
    # length off the front of an absolute FullName. A relative directory makes
    # that cut land mid-path, and every check downstream re-derives it the same
    # wrong way and therefore agrees - so `-PackageRoot .\out` produced a
    # complete, exit-0 upload set whose files were at paths like
    # `release\Data\Local\Temp\...\usbd98.sys`. Found by review.
    param([string]$Path)

    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path $PWD.ProviderPath $Path
    }
    $Path = [System.IO.Path]::GetFullPath($Path)

    # **A mapped drive is expanded, because the containment guard compares
    # strings.** An alias makes one directory reachable under two spellings
    # that share no prefix, so `-UploadDir X:\releases\1.2.3.4` would not be
    # recognised as inside `D:\work\releases\1.2.3.4` and the guard would let a
    # cut write into a written-once release (review round 6). A
    # PSDrive's DisplayRoot names the target of a network mapping and is empty
    # for an ordinary volume, so this closes that form.
    #
    # **`subst` is NOT closed by it, and the attempt to claim otherwise was
    # measured and withdrawn.** A substituted drive is indistinguishable from a
    # local disk here: `Get-PSDrive Z` reports `DisplayRoot` empty and
    # `Win32_LogicalDisk` reports DriveType 3 with no ProviderName, exactly as
    # for a real volume. Telling them apart needs `QueryDosDevice` - a P/Invoke
    # this script has no other reason to carry, compiled at run time by
    # Add-Type, for a case that requires someone to alias a path *into*
    # `releases\` and then point -UploadDir at the alias. Same for a junction
    # or a symbolic link mid-path, which needs GetFinalPathNameByHandle.
    #
    # The limit is written down rather than papered over: this guard is a
    # safety net for a path someone got wrong, not a boundary against someone
    # arranging to defeat it. The written-once rule it protects is a working
    # practice, and the release directory is in git, which is the backstop that
    # actually notices.
    #
    # 8.3 short names need nothing here: GetFullPath above already expands them
    # (measured on this host - `C:\PROGRA~1` comes back `C:\Program Files`).
    if ($Path -match '^([A-Za-z]):') {
        $drive = Get-PSDrive -Name $Matches[1] -PSProvider FileSystem -ErrorAction SilentlyContinue
        if ($null -ne $drive -and -not [string]::IsNullOrEmpty($drive.DisplayRoot)) {
            $rest = $Path.Substring(2).TrimStart('\')
            $Path = if ($rest -eq "") { $drive.DisplayRoot } else { Join-Path $drive.DisplayRoot $rest }
            $Path = [System.IO.Path]::GetFullPath($Path)
        }
    }

    # A trailing separator survives GetFullPath and would break the same prefix
    # arithmetic. A path root keeps its own: "E:\" trimmed is "E:", which names
    # the current directory on that drive rather than its root.
    if ($Path.Length -gt [System.IO.Path]::GetPathRoot($Path).Length) {
        $Path = $Path.TrimEnd('\')
    }
    return $Path
}

$ReleasesDir = Resolve-DirectoryArgument $ReleasesDir
$QualtoolDir = Resolve-DirectoryArgument $QualtoolDir
$SnapToolDir = Resolve-DirectoryArgument $SnapToolDir
$PackageRoot = Resolve-DirectoryArgument $PackageRoot
$UploadDir   = Resolve-DirectoryArgument $UploadDir

# Source name -> published 8.3 name. The tool is written for DOS, so what it is
# called on the media matters: a long name is not what a DOS prompt will show.
$qualtoolFiles = @{ "xhciqual.exe" = "XHCIQUAL.EXE"; "xhciqual.map" = "XHCIQUAL.MAP" }

# The field reports' batch files (roadmap-hcd task 38.1), per tool: source name
# in the tool's own directory -> published 8.3 name beside its executable.
# Copied with the tool, so a cut that skips a tool publishes none of its
# batches; held there and nowhere else by Assert-ReportBatchLayout.
$reportBatches = @{
    "xhcisnap" = @{ "snapon.bat" = "SNAPON.BAT"; "snapget.bat" = "SNAPGET.BAT"; "snapoff.bat" = "SNAPOFF.BAT" };
    "xhciqual" = @{ "QUALREP.BAT" = "QUALREP.BAT" }
}

#
# **A published directory is a flavour AND an architecture, and both are in its
# name** - `release-x86`, `debug-x86`, `release-x64`, `debug-x64` (roadmap task
# 21.3, design record 11 section 8). The two architectures' binaries are both
# called xhci98.sys, so they cannot share a directory; and the x86 pair is
# renamed rather than left bare, because the moment a second set exists an
# untagged `release\` means "x86" without saying so and readme.txt's "INSTALL
# THIS ONE" stops having one referent. That rename is free exactly now - no
# release has been uploaded, so no user has ever seen `release\` - and
# releases\README.md's write-once rule leaves the four existing cuts alone.
#
# A wrong pick by a user fails cleanly in both directions, which is what makes
# four flat siblings safe: the 64-bit setup engine ignores an undecorated
# models section outright and the 32-bit engines skip a %Mfg% line decorated
# NTamd64, so the wrong directory offers no driver rather than installing a
# mismatched binary.
#
# -Arch DEFAULTS TO BOTH ARCHITECTURES since 2026-09-09, and that is a decision
# rather than a default nobody revisited. It was x86 alone until roadmap task
# 21.5 ran, because until then no amd64 binary of this driver had ever executed
# on metal or in a guest. 21.5 passed on a Windows XP Professional x64 SP2
# guest: every checkpoint clause, and then the RELEASE flavour - the one this
# publisher actually stages - installed and read separately, because the clauses
# had been taken on the qemu build and a flavour that is never published cannot
# stand in for one that is.
#
# What that evidence is and is not: one guest, one virtual machine, never real
# hardware, and the x64 half of a cut carries exactly that standing. The x86
# half is unaffected and is still the one with four install legs behind it.
$archDirName  = @{ "x86" = "i386"; "x64" = "amd64" }
$archPkgName  = @{ "x86" = "x86";  "x64" = "amd64" }   # make-package.ps1's -Arch
$archInfPath  = @{ "x86" = "src\xhci98.inf"; "x64" = "src\xhci98-amd64.inf" }

# The two files a release directory may contain, and since 1.0.0.1 the only
# two [SourceDisksFiles] names - see releases\README.md.
$publishable = @("xhci98.inf", "xhci98.sys")

# **A leg may publish a third file of this project's own, when its INF
# declares it** (task 36.15; design record 18 section 8.2): xhcimsi.sys, the
# NT 6 x86 MSI helper, which the 32-bit INF names and the 64-bit one does not.
# So the publishable set is per leg - the two above plus whichever of these
# that leg's own INF declares - and a release cut before 2.3.0.0, whose INFs
# declare none, is published and reconstructed exactly as before.
$legPublishableExtra = @("xhcimsi.sys")

function Get-LegPublishable {
    # The publishable set of one leg, from the media layout its own INF
    # declares (Get-DeclaredMediaLayout).
    param([hashtable]$Layout, [string[]]$Base)
    $out = @($Base)
    foreach ($extra in $legPublishableExtra) {
        if ($Layout.ContainsKey($extra.ToLowerInvariant())) { $out += $extra }
    }
    return $out
}

# The UAS class driver's pair (roadmap task 31-A.2), published beside them in
# every flavour directory a cut writes. Not in $publishable, which is the
# controller INF's declared media and what tells a flavour directory from any
# other: xhciuas.inf declares these two itself, its own gate
# (scripts\inf-gate\check-uas-inf.ps1) checks them in place, and the binary is
# held to every refusal xhci98.sys is (Assert-UasPublished). A version
# published before 2.0.0.0 has no such pair, so -UploadSetOnly gates it where
# it is present and does not require it.
$uasPublishable = @("xhciuas.inf", "xhciuas.sys")

# The text-mode Setup driver description (roadmap task 33.3), published at the
# root of every flavour directory a cut writes, where text-mode Setup of
# Windows 2000 and XP reads it from A:\. Not an INF-declared file either: its
# own gate (scripts\inf-gate\check-txtsetup-oem.ps1) checks it in place against
# the INF beside it. A version published before 2.1.0.0 has none, so
# -UploadSetOnly gates it where it is present and does not require it.
$oemPublishable = @("txtsetup.oem")

# The port switchover's user script (roadmap-hcd task 36.42) and its two
# batch files (task 36.56), which set 1 or 0 through it in one double-click.
# Not INF-declared files: text files the user runs, the same for every
# architecture and flavour, which the setup engine never reads. Since task
# 37.5 a cut publishes them once, from src\, into portsw\ beside xhciqual\
# and xhcisnap\ (all three under tools\ since task 38.10), and into no
# flavour directory; the
# packages no longer carry them. A version published before 2.3.0.0 has
# none and 2.3.0.0 has them at the root of each flavour directory, and a
# published release is never rewritten, so the upload assembly accepts a
# tree with no portsw\ in the layout its version was published with
# (Assert-PortSwitchLayout -PublishedVersion).
$pswPublishable = @("portsw.vbs", "portsw-on.bat", "portsw-off.bat")

# The release's tools in tools\ (roadmap-hcd task 38.10): a cut publishes
# xhciqual\, xhcisnap\ and portsw\ under <version>\tools\, so the version
# root holds only the flavour directories, the readme and the licence. The
# first version so published is 2.4.0.0; every one before it has them at the
# version root, and a published release is never rewritten. So the upload
# assembly reads the layout off the tree as Assert-PortSwitchLayout does: a
# tree with tools\ is held to it at any version (a dry cut before the version
# is stamped has one), and a tree without it is accepted only before 2.4.0.0.
$toolsDirName = "tools"
$toolsLayoutVersion = [version]"2.4.0.0"
$toolNames = @("xhciqual", "xhcisnap", "portsw")

function Get-ToolDirName {
    # A tool's directory relative to the version root: tools\<Name> for the
    # cut (no -Root) and for a tree that has tools\, <Name> for one without.
    param([string]$Name, [string]$Root = "")
    if ($Root -ne "" -and -not (Test-Path -LiteralPath (Join-Path $Root $toolsDirName))) { return $Name }
    return (Join-Path $toolsDirName $Name)
}

function Assert-ToolsLayout {
    # The layout of task 38.10: tools\ holding nothing but the tools'
    # directories, and none of them at the version root.
    #
    # Without -PublishedVersion (the cut) that is the only layout accepted.
    # With it (the upload assembly) a tree that has tools\ is still held to
    # it, and one that has not is accepted only before 2.4.0.0, where it is
    # how every version was published.
    param([string]$Root, [string]$What, [string]$PublishedVersion = "")
    $rootFull = (Get-Item -LiteralPath $Root).FullName.TrimEnd('\')
    $tools = Join-Path $rootFull $toolsDirName
    $faults = @()
    if (Test-Path -LiteralPath $tools) {
        if (-not (Test-Path -LiteralPath $tools -PathType Container)) {
            $faults += "$toolsDirName is a file, not a directory"
        } else {
            foreach ($item in @(Get-ChildItem -LiteralPath $tools -Force)) {
                if (-not $item.PSIsContainer -or $item.Name.ToLowerInvariant() -notin $toolNames) {
                    $faults += "$toolsDirName\$($item.Name) is not one of the tools' directories"
                }
            }
        }
        foreach ($name in $toolNames) {
            if (Test-Path -LiteralPath (Join-Path $rootFull $name)) { $faults += "$name\ is at the version root, not in $toolsDirName\" }
        }
        $layout = "in $toolsDirName\"
    } elseif ($PublishedVersion -eq "" -or [version]$PublishedVersion -ge $toolsLayoutVersion) {
        throw "$What has no $toolsDirName\ directory, where the release's tools are published (roadmap-hcd task 38.10)."
    } else {
        $layout = "at the version root, as a version before $toolsLayoutVersion has them"
    }
    if ($faults.Count -gt 0) {
        throw ("{0} does not carry the release's tools {1} (roadmap-hcd task 38.10):`n  - {2}" -f `
               $What, $layout, ($faults -join "`n  - "))
    }
}
function Publish-PortSwitchScripts {
    # Copies the scripts from SourceDir into <DestRoot>\<DirName>\ and returns
    # that directory. Together or not at all: a missing one is refused before
    # anything is copied, as make-package.ps1 refused it when it staged them.
    param([string]$SourceDir, [string]$DestRoot, [string[]]$Names, [string]$DirName)
    $missing = @($Names | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $SourceDir $_) -PathType Leaf) })
    if ($missing.Count -gt 0) {
        throw ("'{0}' has no {1}: the port switchover's scripts are published together or not at all (roadmap-hcd tasks 36.42, 36.56 and 37.5)." -f `
               $SourceDir, ($missing -join ", "))
    }
    $dir = Join-Path $DestRoot $DirName
    Ensure-Directory $dir
    foreach ($name in $Names) {
        Copy-Item -LiteralPath (Join-Path $SourceDir $name) -Destination (Join-Path $dir $name) -Force
    }
    return $dir
}

function Assert-PortSwitchLayout {
    # The layout of task 37.5 in a version tree: <Root>\<DirName>\ holds the
    # scripts and nothing else, and no other directory of the tree, a flavour
    # directory above all, holds a copy of any of them.
    #
    # Without -PublishedVersion (the cut) that is the only layout accepted.
    # With it (the upload assembly, which -UploadSetOnly also reaches with a
    # tree published before this task) a tree that has <DirName>\ is still
    # held to it, and one that has not is accepted only in the layout its
    # version was published with, checked as strictly: before 2.3.0.0 none of
    # the scripts anywhere; 2.3.0.0 all three at the root of every flavour
    # directory (one holding <InfName>) and nowhere else; after 2.3.0.0 no
    # other layout at all, since every such version is cut by this script.
    param([string]$Root, [string[]]$Names, [string]$DirName, [string]$What,
          [string]$PublishedVersion = "", [string]$InfName = "xhci98.inf")
    $keys = @($Names | ForEach-Object { $_.ToLowerInvariant() })
    $rootFull = (Get-Item -LiteralPath $Root).FullName.TrimEnd('\')
    $dir = Join-Path $rootFull $DirName
    $copies = @(Get-ChildItem -LiteralPath $rootFull -File -Recurse -Force |
                Where-Object { $_.Name.ToLowerInvariant() -in $keys })
    $faults = @()
    if (Test-Path -LiteralPath $dir -PathType Container) {
        foreach ($name in $Names) {
            if (-not (Test-Path -LiteralPath (Join-Path $dir $name) -PathType Leaf)) { $faults += "$DirName\$name is missing" }
        }
        foreach ($item in @(Get-ChildItem -LiteralPath $dir -Force)) {
            if ($item.PSIsContainer -or $item.Name.ToLowerInvariant() -notin $keys) {
                $faults += "$DirName\$($item.Name) is not one of the scripts"
            }
        }
        $allowedDirs = @($dir)
        $layout = "in $DirName\ alone"
    } elseif ($PublishedVersion -eq "" -or ([version]$PublishedVersion -gt [version]"2.3.0.0") -or
              (Test-Path -LiteralPath $dir)) {
        throw "$What has no $DirName\ directory, where the port switchover's scripts are published (roadmap-hcd task 37.5)."
    } elseif ([version]$PublishedVersion -eq [version]"2.3.0.0") {
        $allowedDirs = @(Get-ChildItem -LiteralPath $rootFull -Directory -Force |
                         Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName $InfName) -PathType Leaf } |
                         ForEach-Object { $_.FullName })
        foreach ($flavourDir in $allowedDirs) {
            foreach ($name in $Names) {
                if (-not (Test-Path -LiteralPath (Join-Path $flavourDir $name) -PathType Leaf)) {
                    $faults += ("{0}\{1} is missing" -f (Split-Path -Leaf $flavourDir), $name)
                }
            }
        }
        $layout = "as 2.3.0.0 published them, at the root of each flavour directory"
    } else {
        $allowedDirs = @()
        $layout = "absent, as a version before 2.3.0.0 has them"
    }
    foreach ($item in $copies) {
        if ($item.DirectoryName.TrimEnd('\') -notin $allowedDirs) {
            $faults += ("{0} is a copy outside where they belong" -f $item.FullName.Substring($rootFull.Length + 1))
        }
    }
    if ($faults.Count -gt 0) {
        throw ("{0} does not carry the port switchover's scripts {1} (roadmap-hcd task 37.5):`n  - {2}" -f `
               $What, $layout, ($faults -join "`n  - "))
    }
}

function Publish-ReportBatches {
    # Copies one tool's report batches (roadmap-hcd task 38.1) from SourceDir
    # into DestDir under their published names. Together or not at all: a
    # missing one is refused before anything is copied.
    param([string]$SourceDir, [string]$DestDir, [hashtable]$Map)
    $missing = @($Map.Keys | Sort-Object | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $SourceDir $_) -PathType Leaf) })
    if ($missing.Count -gt 0) {
        throw ("'{0}' has no {1}: a tool's report batches are published together or not at all (roadmap-hcd task 38.1)." -f `
               $SourceDir, ($missing -join ", "))
    }
    Ensure-Directory $DestDir
    foreach ($src in @($Map.Keys | Sort-Object)) {
        Copy-Item -LiteralPath (Join-Path $SourceDir $src) -Destination (Join-Path $DestDir $Map[$src]) -Force
    }
}

function Assert-ReportBatchLayout {
    # The layout of task 38.1 in a version tree: in a tree with tools\, each
    # tool directory that is there holds every one of its report batches, and
    # no other directory of the tree holds a copy of any of them. A tree
    # without tools\ is a version published before 2.4.0.0 (Assert-ToolsLayout
    # refuses it at the cut and from 2.4.0.0), and none of those has any.
    param([string]$Root, [hashtable]$Batches, [string]$What, [string]$PublishedVersion = "")
    $rootFull = (Get-Item -LiteralPath $Root).FullName.TrimEnd('\')
    $batchHome = @{}
    foreach ($tool in $Batches.Keys) {
        foreach ($pub in $Batches[$tool].Values) { $batchHome[$pub.ToLowerInvariant()] = $tool }
    }
    $faults = @()
    $tools = Join-Path $rootFull "tools"
    if (Test-Path -LiteralPath $tools -PathType Container) {
        foreach ($tool in @($Batches.Keys | Sort-Object)) {
            $dir = Join-Path $tools $tool
            if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
            foreach ($pub in @($Batches[$tool].Values | Sort-Object)) {
                if (-not (Test-Path -LiteralPath (Join-Path $dir $pub) -PathType Leaf)) { $faults += "tools\$tool\$pub is missing" }
            }
        }
        $layout = "beside their tools in tools\"
        $withTools = $true
    } elseif ($PublishedVersion -eq "" -or [version]$PublishedVersion -ge [version]"2.4.0.0") {
        throw "$What has no tools\ directory, where the report batches are published beside their tools (roadmap-hcd task 38.1)."
    } else {
        $layout = "absent, as a version before 2.4.0.0 has them"
        $withTools = $false
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $rootFull -File -Recurse -Force)) {
        $key = $item.Name.ToLowerInvariant()
        if (-not $batchHome.ContainsKey($key)) { continue }
        $want = Join-Path $tools $batchHome[$key]
        if (-not $withTools -or $item.DirectoryName.TrimEnd('\') -ne $want) {
            $faults += ("{0} is a copy outside where it belongs" -f $item.FullName.Substring($rootFull.Length + 1))
        }
    }
    if ($faults.Count -gt 0) {
        throw ("{0} does not carry the report batches {1} (roadmap-hcd task 38.1):`n  - {2}" -f `
               $What, $layout, ($faults -join "`n  - "))
    }
}

function Assert-ReportBatchContent {
    # What the report batches in a version tree SAY (roadmap-hcd task 38.1),
    # where Assert-ReportBatchLayout checks only where they are: each one in
    # a tools\<tool>\ directory that is there is read as bytes and held to
    # package-common.ps1's Get-ReportBatchFaults, the one definition
    # test-package.ps1 holds the repository's sources to. So a stale or
    # malformed batch that came in with -SnapToolDir or -QualtoolDir, or that
    # sits in the published tree -UploadSetOnly re-assembles, is refused
    # rather than published or archived (Codex review of 38.1, round 1).
    #
    # With -Reference (the cut: the repository root) each one must also be
    # the repository's own tracked source byte for byte, so a tool directory
    # holding an older copy that still passes the checker cannot publish it.
    # The upload assembly passes none: the tree it reads was cut from the
    # sources of its own commit. Run it after Assert-ReportBatchLayout, which
    # reports a missing batch; a tree without tools\ has none to read.
    param([string]$Root, [hashtable]$Batches, [string]$What, [string]$Reference = "")
    $tools = Join-Path (Get-Item -LiteralPath $Root).FullName "tools"
    if (-not (Test-Path -LiteralPath $tools -PathType Container)) { return }
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $faults = @()
    foreach ($spec in @(Get-ReportBatchSpecs)) {
        $dir = Join-Path $tools $spec.Dir
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        $pub = $Batches[$spec.Dir][$spec.Name]
        $path = Join-Path $dir $pub
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $bytes = [System.IO.File]::ReadAllBytes($path)
        # Bytes, then Latin-1, so a BOM or UTF-16 is seen rather than decoded.
        foreach ($f in @(Get-ReportBatchFaults -Text $latin1.GetString($bytes) -Spec $spec)) {
            $faults += "tools\$($spec.Dir)\${pub}: $f"
        }
        if ($Reference -ne "") {
            $refPath = Join-Path $Reference ($spec.Dir + "\" + $spec.Name)
            if (-not (Test-Path -LiteralPath $refPath -PathType Leaf)) {
                $faults += "tools\$($spec.Dir)\${pub}: the repository has no $($spec.Dir)\$($spec.Name) to compare it with"
            } else {
                $refBytes = [System.IO.File]::ReadAllBytes($refPath)
                if ($bytes.Length -ne $refBytes.Length -or
                    [System.BitConverter]::ToString($bytes) -cne [System.BitConverter]::ToString($refBytes)) {
                    $faults += "tools\$($spec.Dir)\${pub} is not the repository's $($spec.Dir)\$($spec.Name) byte for byte (a stale copy in the tool directory?)"
                }
            }
        }
    }
    if ($faults.Count -gt 0) {
        throw ("{0} carries report batches that are not the ones this release publishes (roadmap-hcd task 38.1):`n  - {1}" -f `
               $What, ($faults -join "`n  - "))
    }
}

# The project test certificate's public half (design record 19), published at
# the root of every x64 directory a cut writes since 2.3.0.0, beside the
# binaries it signed. Not an INF-declared file: the setup engine never reads
# it, and it is there to check a signer's thumbprint against. A version published before 2.3.0.0
# has none, so it is accepted where present and not required of one. Never
# the .pfx, which holds the private key and is never staged.
$certPublishable = @("xhci98-test.cer")

# The DDK's obj directory is the one place its vocabulary is still read; the
# published directory name is built from the flavour and the architecture
# together by New-ReleaseLegs below, so the flavour word is no longer a
# directory name on its own.
$objDirName = @{ "release" = "objfre"; "debug" = "objchk" }

function New-ReleaseLegs {
    #
    # Every (flavour, architecture) pair this cut publishes, in a fixed order -
    # architecture outermost, so a four-directory cut reads release-x86,
    # debug-x86, release-x64, debug-x64 and the readme's contents list comes
    # out in that order too.
    #
    # One object per published directory, and everything that used to be
    # derived from the flavour word alone hangs off it: which obj tree the
    # binary is built into, which INF the package is staged around, what
    # make-package.ps1 is asked for, and what the directory is called.
    #
    param([string[]]$Flavors, [string[]]$Arches)
    $out = New-Object System.Collections.ArrayList
    foreach ($a in $Arches) {
        foreach ($f in $Flavors) {
            [void]$out.Add([pscustomobject]@{
                Id      = "$f-$a"
                Dir     = "$f-$a"
                Flavor  = $f
                Arch    = $a
                PkgArch = $archPkgName[$a]
                ObjDir  = ("src\" + $objDirName[$f] + "\" + $archDirName[$a])
                InfPath = $archInfPath[$a]
            })
        }
    }
    return @($out)
}

function Format-Wrapped {
    # Greedy word wrap with a separate first-line and continuation prefix, so a
    # bullet hangs under its own text rather than under the marker.
    param(
        [string]$Text,
        [int]$Width,
        [string]$FirstPrefix = "",
        [string]$RestPrefix = ""
    )

    $words = @($Text -split '\s+' | Where-Object { $_ -ne "" })
    if ($words.Count -eq 0) { return @() }

    $out = @()
    $line = $FirstPrefix + $words[0]
    for ($i = 1; $i -lt $words.Count; $i++) {
        if (($line + " " + $words[$i]).Length -gt $Width) {
            $out += $line
            $line = $RestPrefix + $words[$i]
        } else {
            $line = $line + " " + $words[$i]
        }
    }
    # @() or a single-line block returns a bare string, and Set-StrictMode 2.0
    # then makes .Count on it a runtime error at the call site.
    return @($out + $line)
}

function ConvertFrom-MarkdownBlocks {
    # The narrow subset releases\history.md actually uses: '###' headings,
    # '-' bullets that may continue on indented lines, and plain paragraphs.
    # Inline '**bold**', '*emphasis*' and `code` are stripped rather than
    # rendered - there is nothing to render them with in a .txt file.
    param([string[]]$Lines, [int]$Width = 78)

    $out = @()
    $pending = ""          # text of the block being accumulated
    $kind = "none"         # none | bullet | para

    function Flush {
        param([string]$Text, [string]$Kind, [int]$W, [object[]]$Acc)
        if ($Text -eq "") { return @($Acc) }
        if ($Kind -eq "bullet") {
            return @(@($Acc) + (Format-Wrapped -Text $Text -Width $W -FirstPrefix "    * " -RestPrefix "      "))
        }
        return @(@($Acc) + (Format-Wrapped -Text $Text -Width $W -FirstPrefix "  " -RestPrefix "  "))
    }

    foreach ($raw in $Lines) {
        $line = $raw -replace '\*\*', '' -replace '`', ''
        # Single-asterisk emphasis, but only around a word - never a bullet
        # marker, which lives at the start of the line and is handled below.
        $line = $line -replace '(?<=\S)\*(\S[^*]*?)\*', '$1' -replace '(?<=\s)\*(\S[^*]*?)\*', '$1'

        if ($line.Trim() -eq "") {
            $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
            $pending = ""; $kind = "none"
            if ($out.Count -gt 0 -and $out[-1] -ne "") { $out += "" }
            continue
        }
        if ($line -match '^###\s+(.*)$') {
            $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
            $pending = ""; $kind = "none"
            if ($out.Count -gt 0 -and $out[-1] -ne "") { $out += "" }
            $out += ("  " + $Matches[1].Trim())
            $out += ""
            continue
        }
        if ($line -match '^\s*-\s+(.*)$') {
            $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
            $pending = $Matches[1].Trim(); $kind = "bullet"
            continue
        }
        # A continuation of whatever block is open, or the start of a paragraph.
        if ($kind -eq "none") { $kind = "para" }
        $pending = if ($pending -eq "") { $line.Trim() } else { $pending + " " + $line.Trim() }
    }
    $out = @(Flush -Text $pending -Kind $kind -W $Width -Acc $out)
    return $out
}

function Get-InfDriverVersion {
    param([string]$Path)

    $line = (Get-Content -LiteralPath $Path) |
        Where-Object { $_ -match '^\s*DriverVer\s*=' } | Select-Object -First 1
    if ($null -eq $line -or $line -notmatch '=\s*[^,]+,\s*([\d.]+)') {
        throw "'$Path' has no readable DriverVer to take the release version from."
    }
    return $Matches[1].Trim()
}

# The INF's DriverVer date, as the mm/dd/yyyy the file declares it in. The
# release readme prints a date, and three files used to answer that question
# differently - the INF said one date, history.md said another, and the
# generated readme said whatever day it happened to be generated on. A release
# is dated by the release, not by the build machine's clock.
function Get-InfDriverDate {
    param([string]$Path)

    $line = (Get-Content -LiteralPath $Path) |
        Where-Object { $_ -match '^\s*DriverVer\s*=' } | Select-Object -First 1
    if ($null -eq $line -or $line -notmatch '=\s*(\d{2}/\d{2}/\d{4})\s*,') {
        throw "'$Path' has no readable mm/dd/yyyy DriverVer date."
    }
    # **A calendar date, not a digit shape.** The pattern above accepts
    # 99/99/2026, and so did the HISTORY heading check, so two nonsense dates
    # could agree with each other and satisfy a gate whose whole promise is
    # "a release has one date". Parse it.
    $parsed = [datetime]::MinValue
    if (-not [datetime]::TryParseExact($Matches[1], 'MM/dd/yyyy',
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::None, [ref]$parsed)) {
        throw "'$Path' has DriverVer date '$($Matches[1])', which is not a calendar date in mm/dd/yyyy form."
    }
    return $parsed.ToString('yyyy-MM-dd')
}

# The draft markers a document carries while a reading is still owed: a
# `TODO(<what fills it>)` tag, or `TBD`. Roadmap task 32.1 drafted the
# 2.0.0.0 release notes, the readme template and the history entry ahead of
# their readings with exactly these two markers, so that what was still owed
# was greppable; this is what makes the cut refuse while any is left. It
# writes one line per hit, "<label> line <n>: <text>", and nothing when
# there is none (callers wrap it in @()). Case-sensitive, and TBD only as a
# whole word, so prose such as "todo" or "TBDx" is not caught.
function Find-DraftMarkers {
    param([string[]]$Lines, [string]$Label)
    $hits = @()
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -cmatch 'TODO\(|\bTBD\b') {
            $hits += ("{0} line {1}: {2}" -f $Label, ($i + 1), $Lines[$i].Trim())
        }
    }
    return $hits
}

# The declared package version, out of src\xhci_version.h - the one place it is
# edited since task 14.1.10. It used to be read out of xhciqual\qual.h, which
# declared its own literal; that file now expands the shared macro, and this
# reader follows the number to where it lives.
#
# **The check this performs has not changed, and neither has the failure it is
# for**: the qualifier is published *inside* a release directory, so a freshly
# rebuilt EXE still answering the previous number ties a user's saved log to the
# wrong artifact. The staleness checks beside it compare timestamps, which
# cannot see a version string.
#
# **What DID change is what a bump touches**, and it opens a gap this function's
# callers close: `qual.h` and `xhcisnap.c` no longer move when the version does,
# so the "EXE newer than every .c/.h beside it" checks would no longer see a
# bump at all. Both of them take `src\xhci_version.h` as an extra input for
# exactly that reason.
#
# The snapshot wire-format schema, read out of a `#define` in either the driver
# header or the tool's own copy of the format. Two files declare it because the
# tool may not include a kernel header, and the only thing that can be done
# about that is to compare them - which is what the caller does.
#
function Get-SnapSchema {
    param([string]$Path, [string]$Macro)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "'$Path' is missing; the snapshot wire schema cannot be checked."
    }
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line -match ("^\s*#define\s+{0}\s+(\d+)" -f [regex]::Escape($Macro))) {
            return [int]$Matches[1]
        }
    }
    throw "'$Path' declares no $Macro; the snapshot wire schema cannot be checked."
}

function Get-DeclaredVersion {
    # src\xhci_version.h's XHCI_VER_STR - the single source. The INF gate proves
    # this agrees with XHCI_VER_CSV and with the INF's DriverVer on every build;
    # what is read here is the same number, for the two tools that cannot be
    # checked by that gate because they are not the driver.
    param([string]$Path)

    $line = (Get-Content -LiteralPath $Path) |
        Where-Object { $_ -match '^\s*#define\s+XHCI_VER_STR\s' } | Select-Object -First 1
    if ($null -eq $line -or $line -notmatch '"([^"]+)"') {
        throw "'$Path' has no readable XHCI_VER_STR."
    }
    return $Matches[1].Trim()
}

function Assert-TakesSharedVersion {
    # **A tool source must expand the shared macro rather than declare a number.**
    # Without this, the reader above would happily report the header's version
    # for a tool whose own source had been edited back to a literal - which is
    # the same vacuity the INF gate refuses in src\xhci98.rc, one directory
    # over, and it compiles just as cleanly here.
    param([string]$Path, [string]$Macro)

    $line = (Get-Content -LiteralPath $Path) |
        Where-Object { $_ -match ('^\s*#define\s+' + [regex]::Escape($Macro) + '\s') } | Select-Object -First 1
    if ($null -eq $line) {
        throw "'$Path' has no $Macro at all; the tool's version cannot be checked against the release version."
    }
    if ($line -notmatch 'XHCI_VER_STR') {
        throw @"
'$Path' declares $Macro as its own value:
  $($line.Trim())
Since task 14.1.10 the version is edited in src\xhci_version.h and nowhere else,
and this tool is published inside a release directory printing that number into
every log a user sends back. Make it read: #define $Macro XHCI_VER_STR
"@
    }
}

function Get-PackageRelativePath {
    # $FullName expressed relative to $PackageDir, refused rather than guessed.
    #
    # **This replaced a `.Substring($PackageDir.Length)`**, which is only
    # correct when both are absolute and the prefix really matches. When it was
    # not - a relative -PackageRoot, before the resolution above existed - the
    # cut landed mid-path and produced a plausible-looking relative path that
    # every check downstream re-derived identically and therefore accepted.
    # Uri arithmetic cannot fail that way silently: it either yields a genuine
    # relative path or it does not, and "does not" throws here.
    param([string]$FullName, [string]$PackageDir)

    $rel = Get-RelativePathFrom -FromDir $PackageDir -To $FullName
    if ($null -eq $rel -or $rel -eq "" -or $rel.StartsWith("..") -or
        [System.IO.Path]::IsPathRooted($rel)) {
        throw "'$FullName' is not under '$PackageDir', so it has no relative path there."
    }
    return $rel
}

function Get-DeclaredMediaLayout {
    # Where the INF says each of its [SourceDisksFiles] entries goes, taken
    # from check-inf.ps1's own parse rather than from a second one here. Same
    # rule make-package.ps1 follows, and the same call: two parsers would be
    # free to disagree, and the only way they can disagree is a file staged at
    # one path and authenticated at another.
    #
    # **-Arch is mandatory, and it is the gate's vocabulary (x86 / amd64), not
    # this script's (x86 / x64).** The gate defaults to x86 and its PATH-* and
    # OS-* rules are written per install path, so handing it
    # src\xhci98-amd64.inf under the default profile fails five checks
    # (PATH-MFGDEC, BOTH-XREF twice, OS-DEFAULT twice) - which is a refusal of
    # the caller, not of the INF. It was omitted here until 2026-09-16, so the
    # two-architecture cut this script has defaulted to since 2026-09-09 threw
    # at its own pre-build layout step; make-package.ps1 has always passed it.
    param(
        [Parameter(Mandatory = $true)][string]$InfPath,
        [Parameter(Mandatory = $true)][ValidateSet("x86", "amd64")][string]$Arch,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $gate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-inf.ps1"
    $layoutFile = Join-Path ([System.IO.Path]::GetTempPath()) ("xhci98-uploadlayout-" + [System.IO.Path]::GetRandomFileName())
    try {
        # Output captured, not left on the pipeline: callers of this return a
        # value, and a bare native call would return the gate's stdout with it.
        # ErrorActionPreference is relaxed for the reason test-package.ps1
        # relaxes it - in Windows PowerShell 5.1 a native command's stderr line
        # becomes an ErrorRecord, and under "Stop" the first one aborts here
        # with an empty message.
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $out = & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $gate,
                                      "-InfPath", $InfPath, "-Arch", $Arch,
                                      "-EmitMediaLayout", $layoutFile) 2>&1 | Out-String
        } finally {
            $ErrorActionPreference = $savedEap
        }
        if ($LASTEXITCODE -ne 0) {
            throw @"
$Label failed scripts\inf-gate\check-inf.ps1, so the media layout it declares
cannot be trusted to say what belongs in the upload set.

$out
"@
        }
        return (Read-MediaLayout -Path $layoutFile)
    } finally {
        if (Test-Path -LiteralPath $layoutFile) {
            Remove-Item -LiteralPath $layoutFile -Force -ErrorAction SilentlyContinue
        }
    }
}

function Assert-PublishableAtMediaRoot {
    # **This script publishes its own two files at the root of a flavour
    # directory, and that is checked rather than assumed.** Three places rest
    # on it - the staging copy takes them from the package root,
    # `releases\<version>\<flavour>\` holds them there, and the readme tells
    # the reader to look in that one directory for every file the INF names -
    # and none of them said so.
    #
    # **It has to run before the publish swap, which is why it is a function.**
    # An INF saying `xhci98.inf=1,setup` does not fail the staging copy:
    # make-package.ps1 stages `setup\xhci98.inf` and then *also* writes a root
    # launcher copy if the layout left the root empty, so the root file this
    # script looks for is there. The release therefore publishes, and a check
    # living only in the upload assembly fires afterwards - leaving a
    # written-once version directory published with no asset and no scripted
    # way back to one. Review round 4 established that against the
    # actual control flow, correcting round 3's reading and mine.
    #
    # Supporting a subdirectory would be a change to the published layout as
    # well as to the INF. Nothing has needed it: the INF has always named both
    # at the root, where a Win98 setup engine reading 8.3 paths wants them.
    param([hashtable]$Layout, [string[]]$PublishableKeys, [string]$InfName, [string]$Label)

    $misplaced = @($Layout.Keys | Where-Object {
        $_ -in $PublishableKeys -and
        $Layout[$_] -ne [System.IO.Path]::GetFileName($Layout[$_])
    })
    if ($misplaced.Count -eq 0) { return }

    throw @"
$Label declares $($misplaced.Count) of this project's own file(s) in a subdirectory:
  - $(@($misplaced | ForEach-Object { "$_ -> $($Layout[$_])" }) -join "`n  - ")
A release directory carries those two at its root - that is where the staging
copy takes them from, where releases\<version>\<flavour>\ holds them, and where
the readme tells the reader to look. Publishing them anywhere else is a change
to the release layout as well as to the INF, so it is refused here rather than
half-done.
"@
}

function Get-UploadSetPaths {
    # The two things the assembly writes under -UploadDir, named in one place
    # so that the early containment check and the assembly itself cannot
    # disagree about what is about to be written where.
    #
    # **The archive is not named after the directory it is assembled from**,
    # and the two names differ on purpose (project owner). `upload-` is a
    # workspace name inside the git-ignored `out\`; the `.zip` is what a
    # stranger downloads from a GitHub release and finds in their Downloads
    # folder, where `upload-0.0.0.5.zip` says nothing about what project it
    # belongs to. The archive carries no top-level directory - every entry is
    # written relative to the upload root - so this name is the only thing the
    # download says about itself until it is unpacked.
    #
    # **-Staged names the same two under a `.staging` suffix.** The ordinary
    # cut assembles the set BEFORE the publish swap (the 2026-09-17 audit's
    # D5), and assembling it straight onto the final names there deleted the
    # previous asset and overwrote the versioned zip while the swap could still
    # fail and roll the tracked release back - leaving the old release paired
    # with the new release's zip (Codex review round 2). So the pre-swap set is
    # built under these names and renamed onto the final ones only once both
    # renames of the swap have succeeded; a rollback removes the staged pair
    # and never touches a pre-existing asset. -UploadSetOnly, which publishes
    # nothing, writes the final names directly.
    param([string]$UploadDir, [string]$Version, [switch]$Staged)
    $suffix = if ($Staged) { ".staging" } else { "" }
    return [pscustomobject]@{
        Root = Join-Path $UploadDir ("upload-" + $Version + $suffix)
        Zip  = Join-Path $UploadDir ("xhci98-" + $Version + ".zip" + $suffix)
    }
}

# Move a set New-UploadSet built with -Staged onto its final names. Called only
# after the publish swap has completed, so this is the first moment the previous
# asset is touched - and by then the cut IS published, so nothing here may
# unpublish it; what this function can do is keep the window in which out\
# holds no usable zip as small as a rename (Codex review round 3).
#
# Directory first, then the zip, and the previous zip is RENAMED ASIDE rather
# than deleted: `.previous` beside it, the staged zip moved onto the final
# name, and only then the aside deleted. A failure at any step restores the
# aside when the final zip is missing, so the states this can leave are: the
# previous asset as it was; the new directory beside the previous zip; or the
# new pair - never no zip at all. Every one of them is recoverable with
# -UploadSetOnly, which rebuilds the asset from the published tree.
function Publish-UploadSet {
    param([Parameter(Mandatory = $true)]$Set)
    if ($null -eq $Set.Final) { throw "Publish-UploadSet was handed a set that was not built with -Staged." }
    if (Test-Path -LiteralPath $Set.Final.Root) {
        Remove-Item -LiteralPath $Set.Final.Root -Recurse -Force
    }
    Move-Item -LiteralPath $Set.Root -Destination $Set.Final.Root

    # A `.previous` left by an interrupted earlier run is handled BEFORE this
    # run's own rename, and by what is beside it (Codex review round 4): with
    # the final zip absent it is the last good asset and goes back onto the
    # final name; with the final zip present it is the older of the two and
    # goes. It is never deleted while the final zip is absent.
    $zipAside = $Set.Final.Zip + ".previous"
    if (Test-Path -LiteralPath $zipAside) {
        if (-not (Test-Path -LiteralPath $Set.Final.Zip)) {
            Write-Warn ("'{0}' is left from an interrupted run and '{1}' is absent; restoring it as the previous asset first" -f $zipAside, $Set.Final.Zip)
            Move-Item -LiteralPath $zipAside -Destination $Set.Final.Zip
        } else {
            Remove-Item -LiteralPath $zipAside -Force
        }
    }
    $hadPrevious = Test-Path -LiteralPath $Set.Final.Zip
    try {
        if ($hadPrevious) {
            Move-Item -LiteralPath $Set.Final.Zip -Destination $zipAside
        }
        Move-Item -LiteralPath $Set.Zip -Destination $Set.Final.Zip
    } catch {
        if ($hadPrevious -and (Test-Path -LiteralPath $zipAside) -and -not (Test-Path -LiteralPath $Set.Final.Zip)) {
            Move-Item -LiteralPath $zipAside -Destination $Set.Final.Zip -ErrorAction SilentlyContinue
        }
        throw
    }
    if ($hadPrevious) {
        Remove-Item -LiteralPath $zipAside -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $zipAside) {
            Write-Warn ("could not remove the previous asset, left at '{0}'; it is superseded by '{1}'" -f $zipAside, $Set.Final.Zip)
        }
    }
    return $Set.Final
}

# The one repair command every post-swap warning prints, rendered from the
# arguments actually in effect (Codex review round 4): -UploadSetOnly honours
# -Version, -Flavor, -ReleasesDir and -UploadDir, and -Arch only when the
# caller passed it (otherwise it derives the architectures from the published
# tree - see the -UploadSetOnly branch). A bare `-UploadSetOnly -Version`
# would repair the default locations, which is not always the cut that just
# happened.
function Get-UploadSetRepairCommand {
    $cmd = "scripts\package\make-release.ps1 -UploadSetOnly -Version " + $Version
    # The SCRIPT's bound parameters, captured at script scope: inside a
    # function $PSBoundParameters is the function's own, and this one has none.
    $bound = $script:boundArgs
    if ($null -eq $bound) { $bound = @{} }
    if ($bound.ContainsKey("Flavor")) { $cmd += " -Flavor " + ($Flavor -join ",") }
    if ($bound.ContainsKey("Arch")) { $cmd += " -Arch " + ($Arch -join ",") }
    # Single-quoted, with an embedded apostrophe doubled - the one escape a
    # single-quoted PowerShell string has (Codex review round 5: a path such
    # as D:\O'Brien\ rendered as an unparseable line). The apostrophe is built
    # from its code point so this file carries no doubled-apostrophe literal
    # for the charset rules to trip on.
    $q = [string][char]39
    $quote = { param([string]$s) return $q + ($s -replace $q, ($q + $q)) + $q }
    if ($bound.ContainsKey("ReleasesDir")) { $cmd += " -ReleasesDir " + (& $quote $ReleasesDir) }
    if ($bound.ContainsKey("UploadDir")) { $cmd += " -UploadDir " + (& $quote $UploadDir) }
    return $cmd
}

# The rollback half: a staged set whose cut did not publish is removed, and
# whatever asset was there before the run is left exactly as it was. Best
# effort - this runs on the failure path, where a second error would hide the
# first.
function Remove-StagedUploadSet {
    param([Parameter(Mandatory = $true)]$Set)
    foreach ($p in @($Set.Root, $Set.Zip)) {
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Assert-UploadSetOutsideRelease {
    # **Nothing the assembly writes may land inside `releases\`, and the
    # upload directory may not contain it either.** The assembly deletes and
    # recreates `upload-<version>\` and overwrites `xhci98-<version>.zip`, so
    # either of them at or below `releases\` would write inside a tree whose
    # every version directory is written once and never edited.
    #
    # **The whole `releases\` root is protected, not just the version being
    # cut.** The first version of this guard compared the upload directory
    # with the published release alone, so `-UploadDir releases\1.0.0.1` while
    # assembling 1.0.1.0 was accepted: the older cut's directory would have
    # gained an `upload-1.0.1.0\` and a sibling zip, and .gitignore ignores
    # neither, so the next `git add -A` would have committed them into a cut
    # (roadmap Phase 20, F4, reproduced read-only with the extracted guard). The
    # published root is still named separately because the ordinary cut's
    # staging directory sits under `releases\` too and the ancestor direction -
    # an upload directory that would CONTAIN the release - is checked against
    # both.
    #
    # **Called before the build as well as inside the assembly**, because the
    # geometry depends only on arguments that are known from the start. Checked
    # only in the assembly, it fired after the publish swap - so a cut with a
    # bad -UploadDir ran both non-reproducible builds, published
    # `releases\<version>\`, and then refused, leaving the "published, with no
    # asset" state. Review round 5. Recoverable with -UploadSetOnly
    # and a sane -UploadDir, unlike the media-root case, but there is no reason
    # to reject an argument late that can be rejected free.
    #
    # Every path is already absolute and separator-normalised by
    # Resolve-DirectoryArgument, so this compares strings rather than resolving:
    # the early call happens before `releases\<version>\` exists, and
    # Resolve-Path cannot answer for a directory that is not there yet.
    param([string]$UploadRoot, [string]$UploadZip, [string]$PublishedRoot, [string]$ReleasesRoot)

    # The repository's own releases\ is protected whatever -ReleasesDir says:
    # an override points the CUT somewhere else, and the cuts that exist under
    # the canonical root are still written once and never edited, so an
    # -UploadDir under one of them is as wrong with the override as without it
    # (the Phase 20 review's fifth finding).
    $protected = @()
    foreach ($p in @($ReleasesRoot, $PublishedRoot, (Join-Path $repo "releases"))) {
        if (-not [string]::IsNullOrEmpty($p)) { $protected += $p.TrimEnd('\') }
    }
    $outputs = @(
        @{ What = "upload directory"; Path = $UploadRoot },
        @{ What = "upload archive";   Path = $UploadZip }
    )
    foreach ($o in $outputs) {
        if ([string]::IsNullOrEmpty($o.Path)) { continue }
        $out = $o.Path.TrimEnd('\')
        foreach ($p in $protected) {
            $inside = $out -eq $p -or
                      $out.StartsWith($p + '\', [System.StringComparison]::OrdinalIgnoreCase) -or
                      $p.StartsWith($out + '\', [System.StringComparison]::OrdinalIgnoreCase)
            if (-not $inside) { continue }
            throw @"
the $($o.What) would be written at '$out', which is inside - or contains -
'$p'.
Every version directory under releases\ is written once and never edited
(releases\README.md), and the assembly deletes and recreates its upload
directory and overwrites its archive, so neither may land there: not in the
version being cut, and not in any older one either. Point -UploadDir somewhere
git-ignored and outside releases\; out\ is the default.
"@
        }
    }
}

function Find-TestClientFiles {
    # Selective suspend's test client (roadmap-hcd.md task 36.34; design
    # record 14 section 11: tracked under test\, never shipped, and this gate
    # refuses it in a release). Every file under $Root whose name is one of
    # the client's - XHCIPM.SYS, XHCIPM.INF, PMCTL.EXE - or whose bytes carry
    # its marker XHCI98_TEST_CLIENT_DO_NOT_SHIP, wherever it sits and
    # whatever it is renamed to: the names catch the ordinary mistake, the
    # marker a renamed copy. Returns the relative paths; empty when clean.
    param([string]$Root)

    $names = @('xhcipm.sys', 'xhcipm.inf', 'pmctl.exe')
    $marker = [System.Text.Encoding]::ASCII.GetBytes('XHCI98_TEST_CLIENT_DO_NOT_SHIP')
    $hits = @()
    foreach ($item in (Get-ChildItem -LiteralPath $Root -File -Recurse)) {
        $relative = $item.FullName.Substring($Root.TrimEnd('\').Length).TrimStart('\')
        if ($item.Name.ToLowerInvariant() -in $names) {
            $hits += $relative
            continue
        }
        $bytes = [System.IO.File]::ReadAllBytes($item.FullName)
        $last = $bytes.Length - $marker.Length
        for ($i = 0; $i -le $last; $i++) {
            if ($bytes[$i] -ne $marker[0]) { continue }
            $j = 1
            while ($j -lt $marker.Length -and $bytes[$i + $j] -eq $marker[$j]) { $j++ }
            if ($j -eq $marker.Length) {
                $hits += $relative
                break
            }
        }
    }
    return $hits
}

function Assert-NoTestClient {
    param([string]$Root, [string]$What)

    $hits = @(Find-TestClientFiles -Root $Root)
    if ($hits.Count -gt 0) {
        throw @"
$What holds selective suspend's test client (test\pmclient, roadmap-hcd.md
task 36.34), which is never shipped:
  - $($hits -join "`n  - ")
It is a test driver for the VM legs only. Take it out; a release carries
this project's own files and nothing from test\.
"@
    }
}

function Assert-PackageMatchesDeclaredMedia {
    # Nothing in the package may be unaccounted for. Both halves matter: a file
    # the INF does not name must not be published, and a declared file must not
    # be missing from the package it is supposed to come out of.
    #
    # **Called from the build loop as well as from the assembly**, for the same
    # reason as the containment check above: a package is fully known the
    # moment make-package.ps1 returns, which is before the publish swap, and a
    # refusal that waits for the assembly refuses after `releases\<version>\`
    # has been written.
    param(
        [string]$PkgDir,
        [hashtable]$Expected,
        [hashtable]$PublishedPaths,
        [string]$InfName,
        [string]$Flavor,
        # Files accepted at the package root beside the declared set: the UAS
        # pair, which its own INF declares and its own gate checks.
        [string[]]$Also = @()
    )

    $alsoKeys = @($Also | ForEach-Object { $_.ToLowerInvariant() })
    $unexpected = @()
    $seen = @{}
    foreach ($item in (Get-ChildItem -LiteralPath $PkgDir -File -Recurse)) {
        $relative = Get-PackageRelativePath -FullName $item.FullName -PackageDir $PkgDir
        $key = $item.Name.ToLowerInvariant()
        if ($PublishedPaths.ContainsKey($key) -and
            $PublishedPaths[$key].ToLowerInvariant() -eq $relative.ToLowerInvariant()) {
            # One of this project's own two, at the place the INF puts it. It
            # comes from the published tree, not from the package.
            continue
        }
        if ($key -in $alsoKeys -and $relative.ToLowerInvariant() -eq $key) {
            continue
        }
        if (-not $Expected.ContainsKey($key) -or
            $Expected[$key].ToLowerInvariant() -ne $relative.ToLowerInvariant()) {
            $unexpected += $relative
            continue
        }
        $seen[$key] = $true
    }
    if ($unexpected.Count -gt 0) {
        throw @"
'$PkgDir' holds $($unexpected.Count) file(s) the published $InfName does not name, or names elsewhere:
  - $($unexpected -join "`n  - ")
Since 1.0.0.1 the media carries this project's two files and nothing else: the
operating system supplies usbd.sys, usbhub.sys, usbport.sys and usbui.dll
through the INF's LayoutFile, and the INF gate refuses a Microsoft file on the media
(docs\contributing\legal-provenance.md section 5 records the decision). A file
the INF does not name is not published; take it out of the package. Declaring a
new media file is a release-layout decision recorded there, not a packaging step.
"@
    }
    $absent = @($Expected.Keys | Where-Object { -not $seen.ContainsKey($_) })
    if ($absent.Count -gt 0) {
        throw @"
'$PkgDir' is missing $($absent.Count) file(s) the published $InfName names:
  - $($absent -join "`n  - ")
Rebuild the package with scripts\package\make-package.ps1 -Flavor $Flavor, which
stages every file the INF declares and gates the result.
"@
    }
}

function Assert-TestSigned {
    # A published x64 binary (design record 19): signed by the project test
    # certificate this cut was given, as record 19 prescribes
    # (Test-ProjectTestSignature), and otherwise exactly the build beside its
    # source stamp (Test-SignedImageOf), so the stamp still speaks for it.
    # make-package.ps1 made both checks as it signed; they are made again
    # here on the copy that is published, because that is the one that ships.
    param([string]$Sys, [string]$BuiltSys, [string]$Label)

    $why = ""
    try {
        $why = Test-ProjectTestSignature -Signature (Get-EmbeddedSignature -Path $Sys) -Thumbprint $testSignCert.Thumbprint
    } catch {
        $why = "its signature cannot be read: $($_.Exception.Message)"
    }
    if ($why -ne "") {
        throw @"
$Label is not signed as every published x64 binary must be: $why.
Design record 19 (owner, 2026-10-11): make-package.ps1 -TestSignPfx signs it
at staging, with the project test certificate, a SHA-1 digest and no
timestamp. Re-run the cut with the key; never sign a binary by hand.
"@
    }
    if (-not (Test-SignedImageOf -Signed ([System.IO.File]::ReadAllBytes($Sys)) `
                                 -Unsigned ([System.IO.File]::ReadAllBytes($BuiltSys)))) {
        throw @"
$Label is not the build at $BuiltSys plus a signature: it differs by more than
the checksum, the certificate-table entry and the appended table, so the
source stamp beside that build says nothing about the bytes being shipped.
Rebuild, and re-run the cut so the packager signs what was built.
"@
    }
    Write-Ok ("{0} is the build, signed by the project test certificate {1}" -f $Label, $testSignCert.Thumbprint)
}

function Assert-UasPublished {
    # xhciuas.sys, the UAS class driver (roadmap task 31-A.2), held to the
    # refusals the build loop makes of xhci98.sys, in the same order and for
    # the same reasons, which are argued there: the release FileVersion,
    # VS_FF_DEBUG, the flavour marker, the staged bytes being the built ones,
    # and the source stamp beside the build (scripts\source-stamp.ps1
    # -Driver xhciuas), unstamped only under -AllowUnstampedDriver.
    param([string]$Sys, [object]$Leg, [string]$Flavor, [string]$Version, [string]$ObjRoot, [switch]$Signed)

    $rebuild = "scripts\build-driver.cmd $Flavor" + $(if ($Leg.Arch -eq "x64") { " -amd64" } else { "" })
    if (-not (Test-Path -LiteralPath $Sys)) {
        throw "the $($Leg.Id) leg has no xhciuas.sys to publish. Rebuild: $rebuild"
    }
    $info = (Get-Item -LiteralPath $Sys).VersionInfo
    if (-not (Test-DriverVersionMatches -Reported $info.FileVersion -Declared $Version)) {
        throw "the UAS binary for $($Leg.Dir)\ reports FileVersion '$($info.FileVersion)', not $Version. Rebuild: $rebuild"
    }
    if ($info.IsDebug -ne ($Flavor -eq "debug")) {
        throw "the UAS binary about to be published as $($Leg.Dir)\ has VS_FF_DEBUG = $($info.IsDebug); that is the wrong obj directory. Rebuild: $rebuild"
    }
    $marker = Get-ImageFlavourMarker -Path $Sys
    if ($marker -ne $Flavor) {
        $found = if ($marker -eq "") { "none" } else { $marker }
        throw "the UAS binary about to be published as $($Leg.Dir)\ carries the '$found' flavour marker. Only release and debug are published. Rebuild: $rebuild"
    }
    $builtSys = Join-Path $ObjRoot "xhciuas.sys"
    if (-not (Test-Path -LiteralPath $builtSys)) {
        throw "the $($Leg.Id) UAS binary about to be published has no counterpart at $builtSys, so nothing ties it to the sources in src\uas. Rebuild: $rebuild"
    }
    if ($Signed) {
        Assert-TestSigned -Sys $Sys -BuiltSys $builtSys -Label "$($Leg.Dir)\xhciuas.sys"
    } elseif ((Get-FileHash -LiteralPath $builtSys -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $Sys -Algorithm SHA256).Hash) {
        throw "the UAS binary about to be published as $($Leg.Dir)\ is not the one in $builtSys, so its source stamp says nothing about the bytes being shipped. Rebuild: $rebuild"
    }
    $stampScript = Join-Path $repo "scripts\source-stamp.ps1"
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $stampOut = & powershell -NoProfile -ExecutionPolicy Bypass `
            -File $stampScript -Driver xhciuas -Check $ObjRoot 2>&1
        $stampCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if ($stampCode -eq 1) {
        throw @"
the UAS binary about to be published as $($Leg.Dir)\ was built from sources this
tree no longer holds:
$($stampOut -join "`n")
Rebuild it ($rebuild) so the published bytes are ones the tree can reproduce.
"@
    } elseif ($stampCode -eq 2) {
        if (-not $AllowUnstampedDriver) {
            throw @"
no source stamp beside the $Flavor UAS binary in $ObjRoot, so this script
cannot tell whether src\uas still reproduces it. Rebuild it ($rebuild), which
writes the stamp as it goes; or, for a binary that must ship unstamped, pass
-AllowUnstampedDriver, as for xhci98.sys.
"@
        }
        Write-Warn "no source stamp beside the $Flavor UAS binary, and -AllowUnstampedDriver was passed."
    } elseif ($stampCode -ne 0) {
        throw "scripts\source-stamp.ps1 -Driver xhciuas -Check '$ObjRoot' failed: $($stampOut -join "`n")"
    } else {
        Write-Ok "$($Leg.Id) UAS binary matches the sources in src\uas"
    }
}

function Assert-MsiHelperPublished {
    # xhcimsi.sys, the NT 6 x86 MSI helper (design record 18; task 36.15),
    # held to the refusals Assert-UasPublished makes of xhciuas.sys, which are
    # the build loop's of xhci98.sys: the release FileVersion, VS_FF_DEBUG,
    # the flavour marker, the staged bytes being the built ones, and the
    # source stamp beside the build (scripts\source-stamp.ps1 -Driver
    # xhcimsi), unstamped only under -AllowUnstampedDriver. x86 legs only:
    # the caller publishes it where that leg's INF declares it.
    param([string]$Sys, [object]$Leg, [string]$Flavor, [string]$Version, [string]$ObjRoot)

    $rebuild = "scripts\build-driver.cmd $Flavor"
    if (-not (Test-Path -LiteralPath $Sys)) {
        throw "the $($Leg.Id) leg has no xhcimsi.sys to publish. Rebuild: $rebuild"
    }
    $info = (Get-Item -LiteralPath $Sys).VersionInfo
    if (-not (Test-DriverVersionMatches -Reported $info.FileVersion -Declared $Version)) {
        throw "the MSI helper for $($Leg.Dir)\ reports FileVersion '$($info.FileVersion)', not $Version. Rebuild: $rebuild"
    }
    if ($info.IsDebug -ne ($Flavor -eq "debug")) {
        throw "the MSI helper about to be published as $($Leg.Dir)\ has VS_FF_DEBUG = $($info.IsDebug); that is the wrong obj directory. Rebuild: $rebuild"
    }
    $marker = Get-ImageFlavourMarker -Path $Sys
    if ($marker -ne $Flavor) {
        $found = if ($marker -eq "") { "none" } else { $marker }
        throw "the MSI helper about to be published as $($Leg.Dir)\ carries the '$found' flavour marker. Only release and debug are published. Rebuild: $rebuild"
    }
    $builtSys = Join-Path $ObjRoot "xhcimsi.sys"
    if (-not (Test-Path -LiteralPath $builtSys)) {
        throw "the $($Leg.Id) MSI helper about to be published has no counterpart at $builtSys, so nothing ties it to the sources in src\msi86. Rebuild: $rebuild"
    }
    if ((Get-FileHash -LiteralPath $builtSys -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $Sys -Algorithm SHA256).Hash) {
        throw "the MSI helper about to be published as $($Leg.Dir)\ is not the one in $builtSys, so its source stamp says nothing about the bytes being shipped. Rebuild: $rebuild"
    }
    $stampScript = Join-Path $repo "scripts\source-stamp.ps1"
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $stampOut = & powershell -NoProfile -ExecutionPolicy Bypass `
            -File $stampScript -Driver xhcimsi -Check $ObjRoot 2>&1
        $stampCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedEap
    }
    if ($stampCode -eq 1) {
        throw @"
the MSI helper about to be published as $($Leg.Dir)\ was built from sources this
tree no longer holds:
$($stampOut -join "`n")
Rebuild it ($rebuild) so the published bytes are ones the tree can reproduce.
"@
    } elseif ($stampCode -eq 2) {
        if (-not $AllowUnstampedDriver) {
            throw @"
no source stamp beside the $Flavor MSI helper in $ObjRoot, so this script
cannot tell whether src\msi86 still reproduces it. Rebuild it ($rebuild), which
writes the stamp as it goes; or, for a binary that must ship unstamped, pass
-AllowUnstampedDriver, as for xhci98.sys.
"@
        }
        Write-Warn "no source stamp beside the $Flavor MSI helper, and -AllowUnstampedDriver was passed."
    } elseif ($stampCode -ne 0) {
        throw "scripts\source-stamp.ps1 -Driver xhcimsi -Check '$ObjRoot' failed: $($stampOut -join "`n")"
    } else {
        Write-Ok "$($Leg.Id) MSI helper matches the sources in src\msi86"
    }
}

function New-UploadSet {
    # Assembles out\upload-<version>\ and out\xhci98-<version>.zip: the
    # published tree, each flavour directory gated as the install media it is.
    #
    # Its own function because there are two ways in - the tail of an ordinary
    # cut, and -UploadSetOnly, which reaches it with a published tree it did not
    # write. Both have to produce the same bytes; a second copy of this would be
    # free to differ from the first in exactly the way that is invisible until
    # someone installs from the download.
    #
    # **$LegArches is what keeps the INF gate on the right profile.** To this
    # function a "flavour" is the name of a directory in the published tree, and
    # since task 21.3 that name carries the architecture too - but the legacy
    # `release\`/`debug\` directories the four already-cut versions hold do not,
    # so the architecture cannot be recovered from the name. Both callers know
    # it and hand it over per directory: gate an amd64 directory under the
    # gate's default x86 profile and it fails seven checks that are about the
    # caller rather than about the INF.
    param(
        [string]$PublishedRoot,
        [string]$Version,
        [string[]]$Flavors,
        [hashtable]$LegArches,
        [hashtable]$PkgDirs,
        [string]$UploadDir,
        [string]$Repo,
        [string[]]$Publishable,
        # Build under the `.staging` names of Get-UploadSetPaths -Staged; the
        # caller moves the result onto the final names with Publish-UploadSet
        # once its publish has succeeded, or drops it with
        # Remove-StagedUploadSet. Without it the final names are written
        # directly, which is what -UploadSetOnly wants.
        [switch]$Staged
    )

    $missingArch = @($Flavors | Where-Object { -not $LegArches.ContainsKey($_) })
    if ($missingArch.Count -gt 0) {
        # Not reachable from either caller, both of which build $LegArches from
        # the same leg list they build $Flavors from. It is checked rather than
        # assumed because the consequence is silent: a $null -Arch would let the
        # gate fall back to its x86 default, which is exactly the defect this
        # parameter exists to close.
        throw "no architecture was given for published directory(ies): $($missingArch -join ', ')."
    }

    Write-Step "Upload set"

    # Named in Get-UploadSetPaths, beside the early containment check that
    # names the same two.
    $paths = Get-UploadSetPaths -UploadDir $UploadDir -Version $Version -Staged:$Staged
    $uploadRoot = $paths.Root
    $uploadZip = $paths.Zip
    $finalPaths = $null
    if ($Staged) {
        $finalPaths = Get-UploadSetPaths -UploadDir $UploadDir -Version $Version
        # The final names are checked too: they are what Publish-UploadSet
        # writes later, from a place that has no PublishedRoot to check against.
        Assert-UploadSetOutsideRelease -UploadRoot $finalPaths.Root -UploadZip $finalPaths.Zip `
                                       -PublishedRoot $PublishedRoot -ReleasesRoot $ReleasesDir
    }

    Assert-UploadSetOutsideRelease -UploadRoot $uploadRoot -UploadZip $uploadZip `
                                   -PublishedRoot $PublishedRoot -ReleasesRoot $ReleasesDir

    # Under -Staged these are the `.staging` names, so a leftover from an
    # interrupted run is what goes here and the previous asset is untouched.
    if (Test-Path -LiteralPath $uploadRoot) {
        Remove-Item -LiteralPath $uploadRoot -Recurse -Force
    }
    Ensure-Directory $UploadDir

    # Start from the published tree, so the two differ by exactly the files
    # added below and nothing else can drift between them.
    #
    # **$uploadRoot is deliberately not created first, and that is the whole
    # point of this line.** Copy-Item of a directory into a destination that
    # does not exist copies the source's *contents* under the destination name;
    # into one that already exists it copies the source as a *child*. This ran
    # after an Ensure-Directory until that review, so every asset it built nested
    # the published tree as `upload-<version>\<version>\...` while the loop
    # below wrote the usbd files into `upload-<version>\<flavour>\` - leaving no
    # directory in the download holding all four install files, and the
    # readme's "if all four are present, install from here" false for the one
    # artifact it was written about. The layout assertion below is what makes
    # that a failed run rather than a shipped one; this comment is why the call
    # is shaped the way it is.
    Copy-Item -LiteralPath $PublishedRoot -Destination $uploadRoot -Recurse -Force

    $publishableKeys = @($Publishable | ForEach-Object { $_.ToLowerInvariant() })
    $gate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-inf.ps1"

    # **Which directories have to be completed is read off the tree, not taken
    # from -Flavor.** The published tree is copied whole, so a release holding
    # both flavours brings both into the upload set - but only the flavours
    # named on the command line are completed and gated. When the media still
    # carried Microsoft files (0.0.0.4 to 1.0.0.0), assembling with `-Flavor
    # release` from a release published with both shipped a `debug\` directory
    # holding this project's two files and none of the three, exit 0, no
    # warning: the nesting defect again, through a different door (review
    # finding 1). The media has carried no such file since 1.0.0.1; what the
    # rule protects now is that every flavour directory in the asset was gated
    # by this run.
    #
    # A directory carrying `xhci98.inf` is install media in the making. That is
    # the test used here rather than a list of flavour names, because it stays
    # true if the flavours are ever renamed and it cannot be satisfied by an
    # empty directory. `xhciqual\` carries no INF and is correctly not one.
    $infName = @($Publishable | Where-Object { $_.ToLowerInvariant().EndsWith(".inf") })[0]
    if ([string]::IsNullOrEmpty($infName)) {
        # Not reachable while $publishable is a literal naming the INF - but an
        # empty PowerShell array indexed at [0] yields $null even under
        # Set-StrictMode 2.0, and a $null here would make Join-Path return the
        # directory itself and Test-Path answer true for every one of them. The
        # whole media/not-media distinction below would silently invert.
        throw "the publishable set names no .inf file, so nothing here can tell a flavour directory from any other."
    }
    $mediaDirs = @(Get-ChildItem -LiteralPath $uploadRoot -Directory | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName $infName)
    } | ForEach-Object { $_.Name })

    $unclaimed = @($mediaDirs | Where-Object { $_ -notin $Flavors })
    if ($unclaimed.Count -gt 0) {
        throw @"
'$PublishedRoot' publishes $($unclaimed.Count) flavour directory(ies) this run was not asked to complete:
  - $($unclaimed -join "`n  - ")
Every directory in the published tree that carries $infName is install media, and
the upload set carries all of them - so completing only some would ship a
directory holding this project's two files and neither of Microsoft's, which is
the incomplete-media defect the layout checks exist to prevent. Name every one of
them - -Flavor for the flavours and -Arch for the architectures - or leave both at
their defaults.
"@
    }

    # **What the INF says belongs on the media, from the gate's own parse.**
    # The copy below used to take everything in the package that was not one of
    # this project's own two files, which is a rule about what to *exclude* and
    # therefore silently includes anything nobody thought about: a stray
    # `notes.txt`, a `setup.exe`, a second binary left in `out\pkg-release\` by
    # hand (review round 2, finding 1). So the rule is inverted: the media set
    # is declared by the INF, and a package holding anything else is refused
    # rather than quietly trimmed. Since 1.0.0.1 that set is this project's two
    # files and nothing else - the Microsoft files the 1.0.0.0 media carried
    # come from the OS now - and check-inf.ps1 refuses an INF or a package that
    # names one, so the refusal below is what keeps a fourth file from riding
    # along without anyone choosing it.
    # **One INF answers for every published directory here, and what makes that
    # sound is a check elsewhere.** Since roadmap task 21.3 a cut may hold two
    # architectures' INFs, which differ in their decorations; what this parse
    # is used for is the media LAYOUT - which file sits where on the disk - and
    # the two agree about that because scripts\inf-gate\test-inf-checks.ps1
    # compares their [SourceDisksFiles] and [SourceDisksNames] directly and
    # fails if they ever stop agreeing. Each directory is still checked against
    # the layout individually below; this is where the layout comes from, not a
    # substitute for checking.
    # **Per leg since task 36.15** (design record 18 section 8.2): the two
    # INFs no longer declare the same media - the 32-bit one adds the x86 MSI
    # helper - so each directory's layout comes from its own INF, and the
    # first leg's is no longer made to answer for the others. The comment
    # above describes the arrangement this replaced.
    $legLayouts = @{}
    foreach ($legDir in $Flavors) {
        $legLayouts[$legDir] = Get-DeclaredMediaLayout `
            -InfPath (Join-Path $PublishedRoot (Join-Path $legDir $infName)) `
            -Arch $LegArches[$legDir] `
            -Label "the published $legDir\$infName"
    }
    $mediaLayout = $legLayouts[$Flavors[0]]

    # Source name -> where the INF puts it, lower-cased on both sides, split
    # into the files this assembly adds and the two a release directory already
    # carries.
    #
    # **The two publishable files are matched by their declared path, not by
    # name.** Excluding them by name alone made a package staging
    # `setup\xhci98.inf` - which an INF saying `xhci98.inf=1,setup` legitimately
    # produces - look like a file nobody declared (review round 3).
    # The path is the thing everything else here is checked against, so it is
    # what these are checked against too.
    foreach ($f in $Flavors) {
        $mediaLayout = $legLayouts[$f]
        $legKeys = @((Get-LegPublishable -Layout $mediaLayout -Base $Publishable) |
                     ForEach-Object { $_.ToLowerInvariant() })
        $expected = @{}
        $publishedPaths = @{}
        foreach ($name in $mediaLayout.Keys) {
            if ($name -in $legKeys) {
                $publishedPaths[$name] = $mediaLayout[$name]
            } else {
                $expected[$name] = $mediaLayout[$name]
            }
        }
        foreach ($name in $legKeys) {
            if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $uploadRoot $f) $name))) {
                throw "'$f' in the published tree has no '$name', which its own $infName declares."
            }
        }

        # The backstop for the same rule the ordinary cut now checks before it
        # builds anything - see Assert-PublishableAtMediaRoot. It is repeated
        # here because -UploadSetOnly reaches this function without passing
        # through that check, and reads its INF from the published tree rather
        # than from src\.
        Assert-PublishableAtMediaRoot -Layout $mediaLayout -PublishableKeys $legKeys `
                                      -InfName $infName -Label "the published $f\$infName"

        $pkgDir = $null
        if ($null -ne $PkgDirs -and $PkgDirs.ContainsKey($f)) { $pkgDir = $PkgDirs[$f] }
        $uploadFlavorDir = Join-Path $uploadRoot $f
        $mediaPaths = @{}

        if ($null -ne $pkgDir) {
            # The ordinary cut: the backstop for the check the build loop
            # already made on the package it has just built.
            Assert-PackageMatchesDeclaredMedia -PkgDir $pkgDir -Expected $expected `
                                               -PublishedPaths $publishedPaths `
                                               -InfName $infName -Flavor $f `
                                               -Also ($uasPublishable + $oemPublishable + $(if ($LegArches[$f] -eq "amd64") { $certPublishable } else { @() }))

            # Copied at the path the INF declares, which is where the check
            # below will look for it. Taking the layout from the gate's parse
            # rather than re-parsing [SourceDisksFiles] here is the rule
            # make-package.ps1 follows for the same reason: two parsers would
            # be free to disagree, and the only way they can disagree is a file
            # staged at one path and authenticated at another. Since 1.0.0.1
            # `$expected` is empty and this loop copies nothing.
            foreach ($name in $expected.Keys) {
                $relative = $expected[$name]
                $source = Join-Path $pkgDir $relative
                $target = Join-Path $uploadFlavorDir $relative
                Ensure-Directory (Split-Path -Parent $target)
                Copy-Item -LiteralPath $source -Destination $target -Force
                $mediaPaths[$name] = $target
            }
        } elseif ($expected.Count -gt 0) {
            # -UploadSetOnly assembles from the tracked directory alone, and
            # that directory carries this project's two files and nothing else.
            # An INF naming a third file on the media would need a source this
            # mode does not have - and since 1.0.0.1 no INF this gate accepts
            # names one, so this is the refusal for a future layout change
            # rather than a path a current cut can reach.
            throw @"
the published $infName names $($expected.Count) media file(s) beyond this project's two:
  - $($expected.Keys -join "`n  - ")
-UploadSetOnly assembles the download from the tracked releases\<version>\
directory alone, which carries only xhci98.sys and xhci98.inf, so it has no
source for those. A release whose media carries a third file is a layout
decision recorded in docs\contributing\legal-provenance.md section 5, and its
asset has to be assembled by the cut that publishes it.
"@
        }

        # **Is this directory complete install media? Asked of the INF, not of
        # the package.** This is the check nothing was making, and the first
        # attempt at it got the anchor wrong: it compared the assembled
        # directory against the package it had just copied, using the same
        # relative-path derivation the copy used - so a systematic error agreed
        # with itself and passed, and a package holding `usbfiles\usbd98.sys`
        # while the INF names it at the root was accepted (review
        # findings 2 and 3).
        #
        # check-inf.ps1 -PackageDir is the authority instead. It is a second,
        # independent parse of the INF that is about to be shipped in this very
        # directory: it fails PKG-* if any [SourceDisksFiles] entry is absent or
        # in the wrong place, or if a Microsoft file is beside them (the
        # per-target SHA-256 manifest it once took went with the Microsoft
        # files in 1.0.0.1). It is the same gate make-package.ps1 runs against
        # a staged package, which is exactly what this directory now is.
        # **Its output is captured, not left on the pipeline.** This function
        # returns an object, and anything a command inside it writes to stdout
        # is returned alongside - so calling the gate bare made the caller's
        # `$set.Zip` fail on an array. The gate's own words are kept and handed
        # to the reader in the failure below, where they are the useful part.
        # ErrorActionPreference is relaxed across the call for the reason
        # test-package.ps1 relaxes it: in Windows PowerShell 5.1 a native
        # command's stderr line becomes an ErrorRecord, and under "Stop" the
        # first one would abort here with an empty message.
        $flavorInf = Join-Path $uploadFlavorDir $infName
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $gateOut = & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $gate,
                                          "-InfPath", $flavorInf, "-Arch", $LegArches[$f],
                                          "-PackageDir", $uploadFlavorDir) 2>&1 | Out-String
        } finally {
            $ErrorActionPreference = $savedEap
        }
        if ($LASTEXITCODE -ne 0) {
            throw @"
the assembled upload set's $f\ directory failed scripts\inf-gate\check-inf.ps1.
Do not upload it. That gate reads the INF shipping in that directory and checks
every file it names is present, in the place it names, and that no Microsoft
file is beside them - so a failure here means the download is not the install
media it claims to be, whatever the directory listing looks like.

$gateOut
"@
        }

        Write-Ok ("{0}\ is complete install media, by its own INF" -f $f)

        # The UAS pair, by its own INF and gate. Required of a directory this
        # cut built; gated where present in one -UploadSetOnly reads, since a
        # version published before 2.0.0.0 has none.
        $uasInf = Join-Path $uploadFlavorDir "xhciuas.inf"
        if (Test-Path -LiteralPath $uasInf) {
            $uasGate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-uas-inf.ps1"
            $savedEap = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $gateOut = & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $uasGate,
                                              "-InfPath", $uasInf, "-Arch", $LegArches[$f],
                                              "-PackageDir", $uploadFlavorDir) 2>&1 | Out-String
            } finally {
                $ErrorActionPreference = $savedEap
            }
            if ($LASTEXITCODE -ne 0) {
                throw @"
the assembled upload set's $f\ directory failed scripts\inf-gate\check-uas-inf.ps1.
Do not upload it: the UAS class driver's INF and binary are not the media they
claim to be.

$gateOut
"@
            }
            Write-Ok ("{0}\ carries the UAS class driver, by its own INF" -f $f)
        } elseif ($null -ne $pkgDir) {
            throw "the assembled upload set's $f\ directory has no xhciuas.inf, and this cut built it with one."
        }

        # The text-mode Setup description, by its own gate against the INF
        # beside it. Required of a directory this cut built; gated where
        # present in one -UploadSetOnly reads, since a version published
        # before 2.1.0.0 has none.
        $oemFile = Join-Path $uploadFlavorDir "txtsetup.oem"
        if (Test-Path -LiteralPath $oemFile) {
            $oemGate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-txtsetup-oem.ps1"
            $oemArch = if ($LegArches[$f] -eq "amd64") { "amd64" } else { "x86" }
            $savedEap = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $gateOut = & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $oemGate,
                                              "-OemPath", $oemFile, "-InfPath", $flavorInf, "-Arch", $oemArch,
                                              "-PackageDir", $uploadFlavorDir) 2>&1 | Out-String
            } finally {
                $ErrorActionPreference = $savedEap
            }
            if ($LASTEXITCODE -ne 0) {
                throw @"
the assembled upload set's $f\ directory failed scripts\inf-gate\check-txtsetup-oem.ps1.
Do not upload it: the text-mode Setup description does not match the driver and
INF beside it.

$gateOut
"@
            }
            Write-Ok ("{0}\ carries the text-mode Setup description, by its own gate" -f $f)
        } elseif ($null -ne $pkgDir) {
            throw "the assembled upload set's $f\ directory has no txtsetup.oem, and this cut built it with one."
        }
    }

    # **Every file is dated the release's date, at noon.** Windows 98 SE's
    # Driver tab shows the driver file's own date, not the INF's DriverVer
    # (the 2.1.1.0 asset legs: 10-5-2026 from a build at 23:23 the evening
    # before a cut dated 10/06/2026, where ME and every NT system read the
    # INF), and the archive carries each file's modification time to whoever
    # unpacks it. The date is the upload set's own INFs' DriverVer, which
    # every published release declares once, so -UploadSetOnly dates an old
    # release by that release; noon keeps the day whatever an unpacker makes
    # of the archive's zoneless times. The bytes are untouched.
    # The test client (task 36.34) anywhere in the asset, the tools'
    # directories included, before anything is dated or zipped.
    Assert-NoTestClient -Root $uploadRoot -What "the upload set '$uploadRoot'"
    # The tools in tools\ where the tree has it, and at the version root only
    # before 2.4.0.0 (task 38.10). And the port switchover's scripts in their
    # directory alone where the tree has it (task 37.5); a version cut before
    # it is accepted with the layout it was published with.
    Assert-ToolsLayout -Root $uploadRoot -What "the upload set '$uploadRoot'" -PublishedVersion $Version
    # A tree with tools\ is cut in task 38.10's layout, so it is held to the
    # cut's port switchover layout too, whatever its version: the legacy
    # layouts -PublishedVersion admits belong to trees without tools\.
    $pswVersion = if (Test-Path -LiteralPath (Join-Path $uploadRoot $toolsDirName)) { "" } else { $Version }
    Assert-PortSwitchLayout -Root $uploadRoot -Names $pswPublishable `
                            -DirName (Get-ToolDirName -Name "portsw" -Root $uploadRoot) `
                            -What "the upload set '$uploadRoot'" -PublishedVersion $pswVersion
    # The report batches beside their tools where the tree has tools\, and
    # none anywhere in a tree published before 2.4.0.0 (task 38.1).
    Assert-ReportBatchLayout -Root $uploadRoot -Batches $reportBatches -What "the upload set '$uploadRoot'" -PublishedVersion $Version
    # And what they say, before anything is archived: the cut checked them
    # when it staged them, but -UploadSetOnly reads a tree it did not stage.
    Assert-ReportBatchContent -Root $uploadRoot -Batches $reportBatches -What "the upload set '$uploadRoot'"
    $infDates = @(Get-ChildItem -LiteralPath $uploadRoot -Filter '*.inf' -File -Recurse |
                  ForEach-Object { Get-InfDriverDate -Path $_.FullName } | Sort-Object -Unique)
    if ($infDates.Count -ne 1) {
        throw "the assembled upload set's INFs declare $($infDates.Count) DriverVer dates ($($infDates -join ', ')), not one: the release has no single date to give its files."
    }
    $fileStamp = [datetime]::ParseExact($infDates[0], 'yyyy-MM-dd',
        [Globalization.CultureInfo]::InvariantCulture).AddHours(12)
    foreach ($item in (Get-ChildItem -LiteralPath $uploadRoot -File -Recurse)) {
        $item.LastWriteTime = $fileStamp
    }
    Write-Ok ("every file dated {0:yyyy-MM-dd HH:mm}, the INFs' DriverVer date" -f $fileStamp)

    if (Test-Path -LiteralPath $uploadZip) {
        Remove-Item -LiteralPath $uploadZip -Force
    }
    # **Every entry name is written with forward slashes, by hand.** The ZIP
    # format specifies them, and neither of the two obvious ways to build this
    # archive on Windows PowerShell 5.1 produces them: `Compress-Archive` writes
    # backslashes, and so does .NET Framework's own
    # `ZipFile::CreateFromDirectory`. Windows tools tolerate that; `unzip` on a
    # Linux or macOS host does not - it extracts flat files literally named
    # `release\usbd98.sys`, which is not install media in any directory. The
    # asset is a download for *preparing* media, so the machine that unpacks it
    # is not necessarily the machine being installed, and quite often is not
    # even running Windows.
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # **$zipped is set after Dispose, not before it.** Dispose is what writes
    # the central directory, so it is a step that can fail on its own - a full
    # volume after the last entry is the ordinary way - and an archive with no
    # central directory is not readable by anything. Setting the flag at the
    # end of the entry loop would have called that a success (review
    # round 2, finding 2). Nesting the two try blocks is what makes the order
    # right: the inner `finally` disposes, the outer one then sees whether
    # everything including the dispose returned.
    $zipped = $false
    try {
        $archive = [System.IO.Compression.ZipFile]::Open(
            $uploadZip, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($item in (Get-ChildItem -LiteralPath $uploadRoot -File -Recurse |
                               Sort-Object FullName)) {
                $entry = (Get-PackageRelativePath -FullName $item.FullName -PackageDir $uploadRoot).Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $item.FullName, $entry,
                    [System.IO.Compression.CompressionLevel]::Optimal)
            }
        } finally {
            $archive.Dispose()
        }
        $zipped = $true
    } finally {
        # A half-written archive must not be left beside a good directory: it
        # is named exactly what a complete one is named, and the next person to
        # look in out\ cannot tell them apart.
        if (-not $zipped -and (Test-Path -LiteralPath $uploadZip)) {
            Remove-Item -LiteralPath $uploadZip -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Ok ("zipped to {0} ({1:N0} B)" -f $uploadZip, (Get-Item -LiteralPath $uploadZip).Length)

    return [pscustomobject]@{ Root = $uploadRoot; Zip = $uploadZip; Final = $finalPaths }
}

# The upload set the ordinary cut has built but not yet published, for the
# failure path at the bottom: a cut that throws with this set still staged
# removes it and leaves any previous asset as it was.
$stagedUpload = $null
# Set the moment the publish swap has succeeded. From then on the cut is
# published whatever happens after, and the failure path below must say so
# rather than call it a failed cut or touch what was published.
$swapDone = $false
# What the caller actually passed, for Get-UploadSetRepairCommand.
$boundArgs = $PSBoundParameters

try {
    if ($UploadSetOnly -and $SkipUploadSet) {
        throw "-UploadSetOnly and -SkipUploadSet ask for opposite things: one assembles only the upload set, the other assembles everything but."
    }
    if ($UploadSetOnly -and $Force) {
        throw @"
-UploadSetOnly and -Force do not go together.
-UploadSetOnly exists so that rebuilding a release asset never needs -Force: it
does not write to releases\ at all, so there is nothing for -Force to overwrite.
If what you want really is to re-cut the published version, drop -UploadSetOnly.
"@
    }

    # --- a TEST build: labelled in the readme, never under releases\ --------
    #
    # A test build (36.final's 2.2.0.3) is cut through this same script so that
    # every gate runs, but it is not a release: its readme says so in its
    # first lines, and it is never written where a release lives. The check is
    # on the arguments alone, so it refuses before anything is built.
    if ($TestLabel -ne "") {
        if ($UploadSetOnly) {
            throw "-TestLabel labels the readme a cut renders, and -UploadSetOnly renders none: it re-assembles an already published tree."
        }
        if ($TestLabel -notmatch '^[\x20-\x7E]{1,50}$' -or $TestLabel -ne $TestLabel.Trim()) {
            throw "-TestLabel '$TestLabel' must be 1 to 50 printable ASCII characters with no leading or trailing space; it is printed in a 78-column plain-text readme."
        }
        # $ReleasesDir is already absolute and separator-normalised
        # (Resolve-DirectoryArgument), and defaults to releases\ itself. The
        # upload directory needs no check of its own: the assembly refuses one
        # inside any releases\ already, and its default is out\.
        $repoReleases = (Join-Path $repo "releases").TrimEnd('\')
        $rd = $ReleasesDir.TrimEnd('\')
        if ($rd -eq $repoReleases -or
            $rd.StartsWith($repoReleases + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw @"
a TEST build (-TestLabel) is never published, so it is not cut into the
repository's own releases\ ('$ReleasesDir'): pass -ReleasesDir at a directory
under the git-ignored out\, holding a history.md that carries the build's entry.
"@
        }
        Write-Warn ("TEST build ('{0}'): the readme opens with the TEST banner; this cut is not a release" -f $TestLabel)
    }

    # --- the x64 test signature: required of every x64 cut ------------------
    #
    # Design record 19 (owner, 2026-10-11): every published x64 directory
    # carries xhci98.sys and xhciuas.sys signed by the project test
    # certificate, applied by make-package.ps1 -TestSignPfx at staging. So a
    # cut that publishes an x64 directory and was given no key is refused
    # here, before anything is built, and there is no switch past it: a clone
    # without the key stages unsigned packages for its own guests with
    # make-package.ps1, or cuts -Arch x86. The key is opened now too, so a
    # wrong password costs a prompt and not two builds.
    $testSignCert = $null
    if ($TestSignPfx -ne "") {
        if ($UploadSetOnly) {
            throw "-TestSignPfx signs what a cut stages, and -UploadSetOnly stages nothing: it re-assembles an already published tree, signed or not as it was cut."
        }
        if ($Arch -notcontains "x64") {
            throw "-TestSignPfx signs the x64 directories, and this cut publishes none (-Arch $($Arch -join ',')). The x86 binaries are never signed (design record 19)."
        }
        if (-not (Test-Path -LiteralPath $TestSignPfx -PathType Leaf)) {
            throw "no test-signing key at '$TestSignPfx' (-TestSignPfx). Design record 19 says how the project's is made and where it is held."
        }
        $TestSignPfx = (Resolve-Path -LiteralPath $TestSignPfx).ProviderPath
        # Asked once here and held in this script's memory; each x64
        # make-package.ps1 child gets it through XHCI98_TESTSIGN_PW, set in
        # this process's environment only around that one call and restored
        # after it (review round 1: a script run from an open session must
        # not leave the password in it). Never printed, never written.
        $testSignPassword = Get-TestSignPassword
        $testSignCert = Get-TestSignCertificate -PfxPath $TestSignPfx -Password $testSignPassword
        Write-Ok ("x64 binaries will be signed by the project test certificate {0}" -f $testSignCert.Thumbprint)
    } elseif (-not $UploadSetOnly -and $Arch -contains "x64") {
        throw @"
this cut publishes x64 directories and was given no test-signing key.
Every published x64 directory carries xhci98.sys and xhciuas.sys with an
embedded signature by the project test certificate, applied at staging
(design record 19; owner, 2026-10-11), so that Vista x64 and Windows 7 x64
load the driver in test-signing mode. Pass the key:
      scripts\package\make-release.ps1 ... -TestSignPfx <path to the .pfx>
with its password in XHCI98_TESTSIGN_PW or typed at the prompt. Without the
key, cut -Arch x86 only, or stage an unsigned x64 package for a guest with
scripts\package\make-package.ps1 -Arch amd64, which is never published.
"@
    }

    # --- the version, from the INF ------------------------------------------
    $infVersion = Get-InfDriverVersion -Path $infPath
    if ($Version -eq "") {
        $Version = $infVersion
    } elseif (-not (Test-DriverVersionMatches -Reported ($Version + "") -Declared $infVersion)) {
        if ($UploadSetOnly) {
            # The gate this mode runs on each assembled flavour directory is
            # the current tree's, and it encodes the current release's rules;
            # an older cut fails the rules added since it was cut. Measured
            # read-only on 2026-09-05: the 1.0.0.1 INF fails six (OS-MISSING
            # for usbport.sys and usbhub.sys on both NT routes, SUSP-MISSING on
            # both), all added by 1.0.1.0, and the 1.0.0.0 INF names Microsoft
            # files on the media, which OS-MEDIA refuses. Weakening the gate or
            # pinning it per version is not the answer (roadmap Phase 20, F15);
            # the tree that cut the older version carries the gate it passed.
            throw @"
-UploadSetOnly rebuilds the asset of the current cut only, and $Version is not
it: src\xhci98.inf's DriverVer is $infVersion.
The INF gate this mode runs on each assembled flavour directory encodes the
current release's rules, and an older cut fails the rules added since it was
cut, so its asset cannot be rebuilt from this tree without weakening the gate -
which is not done. To rebuild the asset of $Version,
check out the commit that cut it (releases\history.md names the cut) and run
this mode there; the gate at that commit is the one $Version passed. Whether an
older cut's asset should be rebuilt at all is the owner's decision under
releases\README.md.
"@
        }
        throw @"
-Version $Version does not match the INF's DriverVer $infVersion.
The version is edited in src\xhci_version.h and nowhere else (task 14.1.10);
this INF's DriverVer is the copy that cannot include it, and the INF gate checks
the two on every build. So this disagreement means one of them was hand-edited:
put the number back in the header, rebuild, and see
docs\contributing\build-and-test.md, "Versioning the driver".
"@
    }
    if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        throw "release version '$Version' is not four dot-separated numbers."
    }

    $finalRoot = Join-Path $ReleasesDir $Version

    # --- -UploadSetOnly: rebuild the download, publish nothing ---------------
    #
    # The upload set is assembled at the end of a cut, from a tree this script
    # has just published. That left one shape unreachable: the release is
    # published and committed, and the *asset* is wrong or gone. Re-running the
    # cut means -Force, which rewrites a written-once version directory, so the
    # only scripted repair was the one thing releases\README.md forbids. This
    # is the way back in. (-Force skips one "already exists" refusal and
    # rebuilds nothing - it stages from the existing obj trees with every gate
    # still running.)
    #
    # Nothing here is built, and the gates that are about the *build* do not
    # run again - the release this assembles for was gated when it was cut, and
    # this mode never touches a binary. The gate that is about the *media* does
    # run, inside the assembly, once per flavour directory.
    #
    # **The tracked directory is the only input.** Until the Phase 20 fix pass
    # this mode also required `out\pkg-<flavour>\` to exist and to hash-match
    # the published binaries, standing in for the build gates. That was a
    # dependency left over from when the media carried Microsoft files the
    # package supplied: since 1.0.0.1 nothing from the package enters the
    # asset, so the requirement only stopped a fresh clone - the machine a lost
    # asset is most likely to be rebuilt on - from rebuilding it
    # (roadmap Phase 20, F15). What the asset is made of is the published tree,
    # and the INF gate on each assembled directory is what checks it.
    if ($UploadSetOnly) {
        Write-Step ("Upload set for {0}, from {1}" -f $Version, $finalRoot)

        if (-not (Test-Path -LiteralPath $finalRoot)) {
            throw @"
'$finalRoot' does not exist, so there is no published release to assemble from.
-UploadSetOnly rebuilds the download for a version that is already published.
To cut one, run this script without it.
"@
        }

        #
        # -UploadSetOnly re-assembles the download for a version that is already
        # published, so what it must name is the directories that cut actually
        # wrote. A cut made before task 21.3 wrote `release\` and `debug\`; one
        # made after writes `release-x86\` and so on. Rather than guess, take
        # the leg names this run was asked for and fall back to the bare flavour
        # word when the published tree has that instead - which is what the four
        # already-cut versions under releases\ hold, and releases\README.md's
        # write-once rule says they keep.
        #
        # AND THE ARCHITECTURE IS TAKEN FROM THE PUBLISHED TREE WHEN -Arch WAS
        # NOT ASKED FOR. Since 2026-09-09 -Arch defaults to both, which is right
        # for a cut and wrong here: every version published before 1.1.0.0 is x86-only,
        # and re-assembling one must not fail for want of a 64-bit directory
        # that version never had. An -Arch the caller actually passed is still
        # held to exactly - a missing directory is then the error it looks like.
        #
        $wantedArches = $Arch
        if (-not $PSBoundParameters.ContainsKey("Arch")) {
            $present = @()
            foreach ($a in $Arch) {
                foreach ($leg in (New-ReleaseLegs -Flavors $Flavor -Arches @($a))) {
                    $tagged = Join-Path $finalRoot $leg.Dir
                    $legacy = Join-Path $finalRoot $leg.Flavor
                    if ((Test-Path -LiteralPath $tagged) -or
                        ($a -eq "x86" -and (Test-Path -LiteralPath $legacy))) {
                        $present += $a
                        break
                    }
                }
            }
            if ($present.Count -eq 0) {
                throw "'$finalRoot' holds no published flavour directory for any of: $($Arch -join ', ')."
            }
            if ($present.Count -ne $Arch.Count) {
                Write-Ok ("architectures present in the published tree: {0} (pass -Arch to require others)" -f ($present -join ", "))
            }
            $wantedArches = $present
        }

        $legs = New-ReleaseLegs -Flavors $Flavor -Arches $wantedArches
        $legDirs = @()
        # The architecture of each published directory, under the name that
        # directory actually has - which is the tagged one for a cut made after
        # task 21.3 and the bare flavour word for the four made before, all of
        # them x86. New-UploadSet cannot recover it from a legacy name, so it is
        # carried rather than re-derived.
        $legArches = @{}
        foreach ($leg in $legs) {
            $pubDir = Join-Path $finalRoot $leg.Dir
            $dirName = $leg.Dir
            if (-not (Test-Path -LiteralPath $pubDir)) {
                $legacy = Join-Path $finalRoot $leg.Flavor
                if ($leg.Arch -eq "x86" -and (Test-Path -LiteralPath $legacy)) {
                    $pubDir = $legacy
                    $dirName = $leg.Flavor
                } else {
                    throw "'$finalRoot' has no $($leg.Dir)\ directory, so $Version was not published with that flavour and architecture."
                }
            }
            # The two every version publishes; a leg whose INF declares the
            # x86 helper is held to it inside New-UploadSet, from that INF.
            foreach ($name in $publishable) {
                if (-not (Test-Path -LiteralPath (Join-Path $pubDir $name))) {
                    throw "'$pubDir' has no '$name', so it is not a published flavour directory of $Version."
                }
            }
            $legDirs += $dirName
            $legArches[$dirName] = $leg.PkgArch
            Write-Ok ("{0}\: '{1}' holds the published binary and INF" -f $dirName, $pubDir)
        }

        $set = New-UploadSet -PublishedRoot $finalRoot -Version $Version -Flavors $legDirs `
                             -LegArches $legArches `
                             -PkgDirs @{} -UploadDir $UploadDir -Repo $repo `
                             -Publishable $publishable

        Write-Step "Done"
        Write-Host "Upload:     $($set.Zip)"
        Write-Host "            Assembled from the published $finalRoot; nothing under"
        Write-Host "            releases\ was read for anything but copying, and nothing was"
        Write-Host "            written there. This is the GitHub release asset; it is"
        Write-Host "            git-ignored and must stay that way."
        exit 0
    }

    Write-Step ("Release {0} -> {1}" -f $Version, $finalRoot)

    if ((Test-Path -LiteralPath $finalRoot) -and -not $Force) {
        throw @"
'$finalRoot' already exists.
A published version is meant to be immutable: re-cutting one is how two
different binaries end up both having been called $Version. Bump the version, or
pass -Force if this version was never published.
"@
    }

    # --- everything is staged beside the destination, never into it ----------
    #
    # **A release directory is only ever created whole.** Writing straight into
    # `releases\<version>\` gives two ways to publish a tree nobody built in one
    # piece, and both are silent. Re-cutting with `-Flavor release` would
    # replace `release\` and leave the *previous* `debug\`, qualifier and
    # readme.txt in place - a directory whose two binaries come from different
    # source trees while both claim one version, which is the exact confusion
    # -Force exists to be careful about. And a failure part-way through - a gate
    # that rejects the second flavour, a build that breaks - would leave a
    # half-updated tree that still looks published.
    #
    # So the run assembles into a sibling staging directory and the destination
    # is replaced only after every gate has passed. A crash leaves the staging
    # directory behind and the published one untouched; the next run clears it.
    $destRoot = Join-Path $ReleasesDir (".staging-" + $Version)
    if (Test-Path -LiteralPath $destRoot) {
        Write-Warn ("clearing a leftover staging directory: {0}" -f $destRoot)
        Remove-Item -LiteralPath $destRoot -Recurse -Force
    }

    # **A leftover `.replaced-<version>` with no `releases\<version>\` beside it
    # is a previous release that was moved aside and never put back**, which
    # means a run was interrupted between the two renames at the end of this
    # script. It is not scratch and must not be cleared like the staging tree:
    # it may be the only copy of a published version on this machine. Refuse,
    # and say which way to resolve it - the decision is whose copy is
    # authoritative, and that is not a script's to take. (Both working
    # directories are git-ignored, so an interrupted run cannot leave a second
    # copy of a release to be committed by accident.)
    #
    # **With `releases\<version>\` beside it, the aside is the other case**: the
    # swap completed and only the removal afterwards did not. On an
    # OneDrive-hosted tree a sync handle inside the just-renamed aside makes
    # that removal fail routinely, and until the 2026-09-17 audit's D4 this
    # check then refused the next run with "a previous run was interrupted",
    # which was false. The published tree is the authoritative copy by
    # construction - the aside is what it replaced - so this is a warning and a
    # second attempt at the removal, and a refusal only if the aside still
    # cannot be removed, said as that.
    $asideCheck = Join-Path $ReleasesDir (".replaced-" + $Version)
    if (Test-Path -LiteralPath $asideCheck) {
        if (-not (Test-Path -LiteralPath $finalRoot)) {
            throw @"
'$asideCheck' exists and '$finalRoot' does not, so a previous run was
interrupted while publishing $Version. That directory is the release that was
moved aside to make room, and it may be the only copy of it here. Resolve it by
hand before cutting again:
  - if the aside is the release you want, rename it back to '$finalRoot';
  - otherwise delete it.
This script will not choose between two copies of a published version.
"@
        }
        Write-Warn ("'{0}' is left over from a previous -Force cut of {1} whose publish completed but whose cleanup did not; it is superseded by '{2}' and is removed now" -f `
            $asideCheck, $Version, $finalRoot)
        Remove-Item -LiteralPath $asideCheck -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $asideCheck) {
            throw @"
'$asideCheck' could not be removed. It is the release '$finalRoot' replaced on a
previous -Force cut of $Version, not a copy this run needs, but the publish
swap at the end of this run moves the current tree aside under that same name.
Something - a sync client, an open Explorer window - is holding it; delete it by
hand and re-run.
"@
        }
    }

    # --- the changelog, checked before anything is built ---------------------
    #
    # **A release gate, not a nicety.** A published version with no entry is one
    # nobody can tell apart from the one before it, and the moment to write that
    # entry is while cutting the release. Checked here rather than at the point
    # of use for the same reason make-package.ps1 gates the INF before staging:
    # there is no value in building two flavours around a release that will be
    # rejected, and both builds run the full host suite.
    $historyPath = Join-Path $ReleasesDir "history.md"
    if (-not (Test-Path -LiteralPath $historyPath)) {
        throw "no changelog at '$historyPath' - a release cannot be published without one."
    }
    $historyLines = @(Get-Content -LiteralPath $historyPath)
    $versionPattern = '^##\s+' + [regex]::Escape($Version) + '(\s|$)'
    $releaseDate = Get-InfDriverDate -Path $infPath
    $headings = @($historyLines | Where-Object { $_ -match $versionPattern })
    if ($headings.Count -eq 0) {
        throw @"
'$historyPath' has no entry for $Version.
Add one - a '## $Version - $releaseDate' heading and what changed for the
person installing it - then cut the release again. A published version nobody
wrote a changelog entry for is one no user can tell apart from its predecessor.
"@
    }
    # **Exactly one, and dated, and dated the same as the INF.** All three parts
    # were missing until a later review. The heading match alone accepted a bare
    # "## 0.0.0.2" with no date at all, so the cross-check below silently did
    # nothing; and a second heading for the same version would have made "the"
    # entry ambiguous. The INF is the authority for the version everywhere else
    # in this script, so it is the authority for the date - the readme prints
    # that date beside this very entry, and the two disagreeing is what this
    # replaced. Checked here, before anything is built or staged.
    if ($headings.Count -gt 1) {
        throw "'$historyPath' has $($headings.Count) headings for $Version; a version has one changelog entry."
    }
    $headingDate = [datetime]::MinValue
    if ($headings[0] -match '^##\s+' + [regex]::Escape($Version) + '\s+-\s+(\S+)\s*$' -and
        -not [datetime]::TryParseExact($Matches[1], 'yyyy-MM-dd',
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::None, [ref]$headingDate)) {
        throw "'$historyPath' dates $Version as '$($Matches[1])', which is not a calendar date in yyyy-mm-dd form."
    }
    if ($headings[0] -notmatch '^##\s+' + [regex]::Escape($Version) + '\s+-\s+(\d{4}-\d{2}-\d{2})\s*$') {
        throw @"
'$historyPath' heading for $Version is not a dated entry:
  $($headings[0])
Write it as '## $Version - $releaseDate'. The generated readme.txt prints the
release date beside this entry, so an undated or differently shaped heading
leaves that date unchecked - which is how a release came to date itself two
different ways in one file.
"@
    }
    if ($Matches[1] -ne $releaseDate) {
        throw @"
releases\history.md dates $Version as $($Matches[1]), but src\xhci98.inf's
DriverVer dates it $releaseDate. A release has one date; the generated
readme.txt prints it beside the history entry, so the two cannot disagree.
"@
    }
    Write-Ok ("changelog entry for {0} found in releases\history.md, dated {1} in agreement with the INF" -f $Version, $releaseDate)
    # --- no draft marker in what the user reads -----------------------------
    #
    # Roadmap task 32.3. The 2.0.0.0 documents were drafted ahead of their
    # readings, each owed reading tagged TODO(...) or TBD, and a cut that
    # shipped one would tell a user "TODO(bench)" about their own machine.
    # Checked here, before anything is built, for the two sources a cut takes
    # its user-facing text from: this version's history.md entry, which the
    # readme embeds verbatim, and docs\using\release-notes.md, which the readme
    # is transcribed from and which is the user-facing record of it. The
    # rendered readme is checked again after substitution, below, because the
    # template lives in this script and is not in scope yet.
    $entryStart = [Array]::IndexOf($historyLines, $headings[0])
    $entryEnd = $historyLines.Count
    for ($i = $entryStart + 1; $i -lt $historyLines.Count; $i++) {
        if ($historyLines[$i] -match '^##\s') { $entryEnd = $i; break }
    }
    $draftHits = @(Find-DraftMarkers -Lines $historyLines[$entryStart..($entryEnd - 1)] -Label "releases\history.md ($Version entry, from its heading)")
    $notesPath = Join-Path $repo "docs\using\release-notes.md"
    if (Test-Path -LiteralPath $notesPath) {
        $draftHits += @(Find-DraftMarkers -Lines @(Get-Content -LiteralPath $notesPath) -Label "docs\using\release-notes.md")
    }
    if ($draftHits.Count -gt 0) {
        throw @"
$Version is still a draft: $($draftHits.Count) TODO(...) or TBD marker(s) remain
in what this cut would publish. Each is a reading still owed; take it, write
the result in place of the marker, then cut again. The first:
  $(($draftHits | Select-Object -First 10) -join "`n  ")
"@
    }
    Write-Ok "no TODO(...) or TBD in the $Version history entry or the release notes"


    # --- where the INF puts this project's own two files ---------------------
    #
    # **Checked here, before anything is built, because after the publish is
    # too late.** The upload set is assembled at the very end of a cut, so a
    # refusal that lives only there fires with `releases\<version>\` already
    # written - and that directory is written once, so the run leaves a
    # published version with no asset and no scripted way back to one.
    #
    # It is not caught earlier by accident either. An INF saying
    # `xhci98.inf=1,setup` sails through the staging copy below: make-package.ps1
    # stages `setup\xhci98.inf` and then also writes a root launcher copy when
    # the layout leaves the root empty, so the root file that copy looks for is
    # there. Review round 4, which established this against the
    # control flow after round 3 and I had both read it the other way.
    #
    # Gating it here rather than at the point of use follows the changelog check
    # above: there is no value in building two flavours around a release that
    # will be rejected, and both builds run the full host suite.
    # **Per architecture, because there are two INFs.** Each declares its own
    # media, and each is checked on its own terms rather than one standing in
    # for the other. The two do agree today - scripts\inf-gate\test-inf-checks.ps1
    # compares their [SourceDisksFiles] directly - and this loop is what makes
    # that agreement something the publisher rests on rather than assumes.
    $publishableKeys = @($publishable | ForEach-Object { $_.ToLowerInvariant() })
    $declaredExpected = @{}
    $declaredPublished = @{}
    $declaredPublishable = @{}
    foreach ($a in $Arch) {
        $archInf = Join-Path $repo $archInfPath[$a]
        $layoutA = Get-DeclaredMediaLayout -InfPath $archInf -Arch $archPkgName[$a] -Label $archInfPath[$a]
        Assert-PublishableAtMediaRoot -Layout $layoutA -PublishableKeys $publishableKeys `
                                      -InfName "xhci98.inf" -Label $archInfPath[$a]
        Write-Ok ("{0} puts xhci98.sys and xhci98.inf at the media root, where a release directory carries them" -f $archInfPath[$a])

        # The same split the assembly makes, made once per architecture here so
        # the build loop below can hold each package to it as soon as
        # make-package.ps1 returns.
        # The leg's own publishable set (task 36.15): the x86 INF's helper
        # is published beside xhci98.sys.
        $legPublishable = @(Get-LegPublishable -Layout $layoutA -Base $publishable)
        $legKeysA = @($legPublishable | ForEach-Object { $_.ToLowerInvariant() })
        Assert-PublishableAtMediaRoot -Layout $layoutA -PublishableKeys $legKeysA `
                                      -InfName "xhci98.inf" -Label $archInfPath[$a]
        $expectedA = @{}
        $publishedA = @{}
        foreach ($name in $layoutA.Keys) {
            if ($name -in $legKeysA) { $publishedA[$name] = $layoutA[$name] }
            else                     { $expectedA[$name] = $layoutA[$name] }
        }
        $declaredExpected[$a] = $expectedA
        $declaredPublished[$a] = $publishedA
        $declaredPublishable[$a] = $legPublishable
    }

    # **And where the upload set would land, which is knowable now.** Checked
    # only inside the assembly, this refused after the publish swap - so a cut
    # with an -UploadDir pointing into releases\ ran both non-reproducible
    # builds and published the version directory before saying no. Review
    # round 5.
    if (-not $SkipUploadSet) {
        $early = Get-UploadSetPaths -UploadDir $UploadDir -Version $Version
        Assert-UploadSetOutsideRelease -UploadRoot $early.Root -UploadZip $early.Zip `
                                       -PublishedRoot $finalRoot -ReleasesRoot $ReleasesDir
    }

    # --- build and gate each flavour, then take only what may be tracked -----
    $makePackage = Join-Path $PSScriptRoot "make-package.ps1"
    $staged = @{}

    $legs = New-ReleaseLegs -Flavors $Flavor -Arches $Arch
    foreach ($leg in $legs) {
        $f = $leg.Flavor
        Write-Step ("make-package.ps1 -Flavor {0} -Arch {1}" -f $f, $leg.PkgArch)

        # **-OutDir is passed, not left to default.** The package this script
        # publishes from is read at $PackageRoot, so the package it *builds*
        # has to be written there too. Letting make-package.ps1 use its own
        # default while reading somewhere else means the gated package and the
        # published package can be two different things: a stale
        # out\pkg-release at the same version with the right VS_FF_DEBUG flag
        # passes every check this script makes, while the build that actually
        # ran is discarded. Review finding 4.
        $pkgDirForBuild = Join-Path $PackageRoot ("pkg-" + $f + "-" + $leg.PkgArch)
        $pkgArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                     $makePackage, "-Flavor", $f, "-Arch", $leg.PkgArch,
                     "-OutDir", $pkgDirForBuild)
        if ($NoTargetEvidence) { $pkgArgs += "-NoTargetEvidence" }
        # The x64 legs are signed at staging (design record 19); the password
        # reaches the child through XHCI98_TESTSIGN_PW, set above.
        $legSigned = ($leg.Arch -eq "x64")
        if ($legSigned) {
            if ($null -eq $testSignCert) {
                # Not reachable: an x64 cut without a key is refused before
                # anything is built. Checked so that no edit above can make
                # an unsigned x64 leg publishable by omission.
                throw "the $($leg.Id) leg would be published unsigned; every x64 directory is signed (design record 19)."
            }
            $pkgArgs += @("-TestSignPfx", $TestSignPfx)
        }
        # ErrorActionPreference relaxed across the call, as every other native
        # call in this script does and this one did not until the 2026-09-07
        # audit's H17. In Windows PowerShell 5.1 a native command's stderr line
        # becomes an ErrorRecord, so under "Stop" a single warning from the
        # packager aborts the cut here with an empty message instead of
        # reaching the exit-code test below. It only bites when the child's
        # stderr is redirected, which is why it survived: an interactive run
        # never showed it.
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $savedPw = [Environment]::GetEnvironmentVariable($script:TestSignPasswordVariable, "Process")
        try {
            if ($legSigned) {
                [Environment]::SetEnvironmentVariable($script:TestSignPasswordVariable, $testSignPassword, "Process")
            }
            & powershell.exe @pkgArgs
        } finally {
            if ($legSigned) {
                [Environment]::SetEnvironmentVariable($script:TestSignPasswordVariable, $savedPw, "Process")
            }
            $ErrorActionPreference = $savedEap
        }
        if ($LASTEXITCODE -ne 0) {
            throw @"
make-package.ps1 failed for the $($leg.Id) leg, so there is nothing to release.
Do not work around this by copying $($leg.ObjDir)\xhci98.sys by hand: the gates it
runs are what stand between a broken binary and a guest that cannot boot.
"@
        }

        $pkgDir = $pkgDirForBuild
        $destDir = Join-Path $destRoot $leg.Dir
        Ensure-Directory $ReleasesDir
        Ensure-Directory $destRoot
        Ensure-Directory $destDir

        $legCopy = $declaredPublishable[$leg.Arch] + $uasPublishable + $oemPublishable
        if ($legSigned) { $legCopy += $certPublishable }
        foreach ($name in $legCopy) {
            $src = Join-Path $pkgDir $name
            if (-not (Test-Path -LiteralPath $src)) {
                throw "make-package.ps1 produced no '$name' in '$pkgDir'."
            }
            Copy-Item -LiteralPath $src -Destination (Join-Path $destDir $name) -Force
        }

        # **Held to the INF's declared media set here, not at the assembly.**
        # A package is fully known the moment make-package.ps1 returns, which
        # is before the publish swap; the same check inside the upload assembly
        # runs after it, and a refusal there leaves a written-once version
        # directory published with no asset. Review round 5, which
        # is the second instance of that shape in this loop.
        Assert-PackageMatchesDeclaredMedia -PkgDir $pkgDir -Expected $declaredExpected[$leg.Arch] `
                                           -PublishedPaths $declaredPublished[$leg.Arch] `
                                           -InfName "xhci98.inf" -Flavor $leg.Id `
                                           -Also ($uasPublishable + $oemPublishable + $(if ($legSigned) { $certPublishable } else { @() }))
        Assert-NoTestClient -Root $pkgDir -What "the package '$pkgDir'"
        # The certificate beside a signed leg's binaries must be this key's
        # public half, byte for byte - the one a user would inspect or import.
        if ($legSigned) {
            $pubCer = [System.IO.File]::ReadAllBytes((Join-Path $destDir $certPublishable[0]))
            if ([Convert]::ToBase64String($pubCer) -ne [Convert]::ToBase64String($testSignCert.CerBytes)) {
                throw "the $($certPublishable[0]) staged for $($leg.Dir)\ is not the public half of the key given by -TestSignPfx."
            }
        }

        $sys = Join-Path $destDir "xhci98.sys"
        $info = (Get-Item -LiteralPath $sys).VersionInfo

        # make-package.ps1 already tied this binary to the INF beside it. Re-check
        # against the *release* version so a stale out\pkg-* left by an earlier
        # build cannot be published under a version it was never built as.
        if (-not (Test-DriverVersionMatches -Reported $info.FileVersion -Declared $Version)) {
            throw @"
the binary for $($leg.Dir)\ reports FileVersion '$($info.FileVersion)', not $Version.
Rebuild with scripts\build-driver.cmd $f$(if ($leg.Arch -eq "x64") { " -amd64" }) before releasing.
"@
        }

        # **Which build this actually is, read from the binary.** VS_FF_DEBUG is
        # set by src\xhci98.rc under #if DBG, so it is the one field that
        # distinguishes two files that are otherwise the same name at the same
        # version. Trusting the obj directory instead would let a release build be
        # published as the debug one - and the person who later asks a user for a
        # trace would get silence and blame the machine.
        $expectDebug = ($f -eq "debug")
        if ($info.IsDebug -ne $expectDebug) {
            throw @"
the binary about to be published as $($leg.Dir)\ has VS_FF_DEBUG = $($info.IsDebug), expected $expectDebug.
That is the wrong obj directory: src\objchk\$($archDirName[$leg.Arch]) is the debug build and
src\objfre\$($archDirName[$leg.Arch]) is the release one. Rebuild the flavour you meant.
"@
        }

        # **And which of the three it is, which VS_FF_DEBUG cannot say.** Task
        # 13-L.1 made `debug` and `qemu` both checked builds, so the field above
        # is TRUE for either and would publish a qemu binary as the diagnostic
        # download without a word. The marker in the image is the discriminator,
        # and it is read here rather than inferred from a directory because
        # "objchk_qemu" contains "objchk" - a path match is wrong in exactly the
        # direction that matters.
        # The HCD's task 25.8 scaffold registered nothing and carried
        # XHCI98_SCAFFOLD_DO_NOT_STAGE; make-package.ps1 refuses it first, and
        # this is the same refusal at publish time (design record 13 section
        # 9.3, task 26-A.1), so a binary staged by any other route is caught.
        $sysText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($sys))
        if ($sysText.Contains("XHCI98_SCAFFOLD_DO_NOT_STAGE")) {
            throw "the binary about to be published as $($leg.Dir)\ is the HCD's build scaffold (XHCI98_SCAFFOLD_DO_NOT_STAGE, roadmap task 25.8): it registers nothing and is never published."
        }

        $publishedFlavour = Get-ImageFlavourMarker -Path $sys
        if ($publishedFlavour -ne $f) {
            $found = if ($publishedFlavour -eq "") { "none" } else { $publishedFlavour }
            throw @"
the binary about to be published as $($leg.Dir)\ carries the '$found' flavour marker.
Only release and debug may be published; qemu carries the port-0xE9 mirror
(HAL.dll!WRITE_PORT_UCHAR), the sole import delta of the build that gave the
ThinkPad E460 a Code 2 under Windows 98 SE, and it is never shipped. Rebuild the flavour you
meant: scripts\build-driver.cmd $f
"@
        }

        #
        # **And that the tree can still reproduce it** (the 2026-09-07 audit's
        # H13). This script already refuses a staged XHCIQUAL or XHCISNAP older
        # than its own sources; the driver had no equivalent, and the only tie
        # between src\ and the published .sys was the file version against the
        # INF's - which cannot see a code edit that did not bump the version.
        #
        # `scripts\source-stamp.ps1` compares CONTENT, not timestamps, and
        # says at length why: an mtime moves on a checkout, a revert or a
        # comment-only commit, so a timestamp rule would fire on binaries it is
        # byte-for-byte responsible for and would be bypassed on its first use.
        #
        # Exit 2 is "no stamp beside this binary", which only a build older than
        # that script gives - `scripts\build-driver.cmd` has written one since
        # the same commit, so every binary this tree produces has one.
        #
        # It was a bare warning at first, and that made the whole check
        # optional: an unstamped binary is exactly the case where nothing is
        # known about its sources, and it was the case that passed. It is now
        # a REFUSAL with a named way past it, because the reason for the
        # leniency is real but narrow - a binary built before the stamp existed
        # cannot be rebuilt to acquire one without moving the PE link timestamp
        # and invalidating every reading already taken on the published bytes.
        # That is a decision about a specific release, so it is spelled as one
        # on the command line and printed into the log, rather than being the
        # silent default for every cut thereafter. A mismatch (exit 1) is a
        # refusal with no way past it at all.
        #
        # The stamp is written beside the BUILD output, not beside the staged
        # copy: only the INF and the .sys are copied into the staging
        # directory. Deriving the directory from $sys - which by here points
        # into releases\.staging-<version>\<flavour> - is how the first cut of
        # this check took the "no stamp" path on every ordinary release, and
        # would have gone on taking it with the sources changed underneath.
        $stampScript = Join-Path $repo "scripts\source-stamp.ps1"
        $objRoot = Join-Path $repo $leg.ObjDir
        $builtSys = Join-Path $objRoot "xhci98.sys"

        # And a stamp only speaks for the binary it sits beside, so the staged
        # bytes have to BE those bytes. Without this the check answers for
        # src\obj*\i386\xhci98.sys while some other file ships.
        if (-not (Test-Path -LiteralPath $builtSys)) {
            throw @"
the $($leg.Id) binary about to be published has no counterpart at
$builtSys, so nothing ties it to the sources in src\. Build the leg you
meant: scripts\build-driver.cmd $f$(if ($leg.Arch -eq "x64") { " -amd64" })
"@
        }
        $builtHash = (Get-FileHash -LiteralPath $builtSys -Algorithm SHA256).Hash
        $stagedHash = (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash
        if ($legSigned) {
            # A signed leg's bytes are the build's plus the signature and
            # nothing else (design record 19), and the signature is the
            # project certificate's by this cut's key.
            Assert-TestSigned -Sys $sys -BuiltSys $builtSys -Label "$($leg.Dir)\xhci98.sys"
        } elseif ($builtHash -ne $stagedHash) {
            throw @"
the binary about to be published as $($leg.Dir)\ is not the one in
$builtSys ($stagedHash vs $builtHash), so the source stamp beside that build
says nothing about the bytes being shipped. Re-run the packager against the
build you mean: scripts\build-driver.cmd $f
"@
        }

        if (Test-Path -LiteralPath $stampScript) {
            # Relaxed across the call for the H17 reason above: with stderr
            # redirected, a `throw` on the stamp's hashing path would become a
            # terminating NativeCommandError here before the exit code is read
            # (the 2026-09-17 audit's D9).
            $savedEap = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $stampOut = & powershell -NoProfile -ExecutionPolicy Bypass `
                    -File $stampScript -Check $objRoot 2>&1
                $stampCode = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $savedEap
            }
            if ($stampCode -eq 1) {
                throw @"
the binary about to be published as $($leg.Dir)\ was built from sources this tree no
longer holds:
$($stampOut -join "`n")
Rebuild it (scripts\build-driver.cmd $f) so the published bytes are ones the
tree can reproduce - or, if the difference is deliberate and the bytes are the
ones you mean to ship, rebuild anyway and re-read whatever was measured on the
old ones. A release nobody can rebuild is a release nobody can debug.
"@
            } elseif ($stampCode -eq 2) {
                if (-not $AllowUnstampedDriver) {
                    throw @"
no source stamp beside the $f binary in
$objRoot, so this script cannot tell whether src\ still reproduces it - which
is the one case where nothing at all is known about the published bytes.
Rebuild it (scripts\build-driver.cmd $f), which writes the stamp as it goes.
If this binary must ship UNSTAMPED - the only good reason is that it predates
scripts\source-stamp.ps1 and a rebuild would move its PE link timestamp and
invalidate readings already taken on these exact bytes - say so:
      scripts\package\make-release.ps1 ... -AllowUnstampedDriver
"@
                }
                Write-Warn ("no source stamp beside the $f binary, so this script " +
                            "cannot tell whether src\ still reproduces it, and " +
                            "-AllowUnstampedDriver was passed. Nothing here has " +
                            "checked the published bytes against src\.")
            } elseif ($stampCode -ne 0) {
                throw "scripts\source-stamp.ps1 -Check '$objRoot' failed: $($stampOut -join "`n")"
            } else {
                Write-Ok "$($leg.Id) binary matches the sources in src\"
            }
        }

        # The UAS class driver beside it, held to the same refusals.
        $uasSys = Join-Path $destDir "xhciuas.sys"
        Assert-UasPublished -Sys $uasSys -Leg $leg -Flavor $f -Version $Version `
                            -ObjRoot (Join-Path $repo ("src\uas\" + $objDirName[$f] + "\" + $archDirName[$leg.Arch])) `
                            -Signed:$legSigned

        # The NT 6 x86 MSI helper, where this leg's INF declares it (design
        # record 18), held to the same refusals.
        if ($declaredPublishable[$leg.Arch] -contains "xhcimsi.sys") {
            Assert-MsiHelperPublished -Sys (Join-Path $destDir "xhcimsi.sys") -Leg $leg -Flavor $f -Version $Version `
                                      -ObjRoot (Join-Path $repo ("src\msi86\" + $objDirName[$f] + "\i386"))
        }

        $staged[$leg.Id] = [pscustomobject]@{
            Flavor    = $f
            Arch      = $leg.Arch
            Published = $leg.Dir
            Path      = $sys
            Dir       = $destDir
            # Kept so the upload set can take any file the INF declares beyond
            # this project's two from the directory make-package.ps1 already
            # gated - none since 1.0.0.1 - rather than re-deriving where it came
            # from or parsing [SourceDisksFiles] a second time.
            PkgDir    = $pkgDir
            Length    = (Get-Item -LiteralPath $sys).Length
            Sha256    = (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash
            UasLength = (Get-Item -LiteralPath $uasSys).Length
            UasSha256 = (Get-FileHash -LiteralPath $uasSys -Algorithm SHA256).Hash
        }
    }

    # --- the DOS qualifier ---------------------------------------------------
    #
    # Copied, not built: it is an Open Watcom target and this script runs in the
    # MSVC/DDK world. That makes staleness the risk, so compare it against its
    # own sources rather than trusting that someone ran build.cmd - a qualifier
    # older than the code it was built from would be answering for a different
    # tool than the one the docs describe.
    $qualtoolStaged = $null
    if (-not $SkipQualtool) {
        Write-Step "DOS qualifier"

        $missing = @($qualtoolFiles.Keys | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $QualtoolDir $_)) })
        if ($missing.Count -gt 0) {
            throw @"
'$QualtoolDir' has no $($missing -join ', ').
Build it first:  xhciqual\SETENV.BAT  then  xhciqual\build.cmd
It needs Open Watcom (C:\WATCOM). On a host without it, pass -SkipQualtool and
say so in the release notes - the published directory is incomplete without it.
"@
        }

        $exePath = Join-Path $QualtoolDir "xhciqual.exe"
        $exeTime = (Get-Item -LiteralPath $exePath).LastWriteTimeUtc
        #
        # **`src\xhci_version.h` counts as one of its sources**, and this is the
        # one line of this check that is not obvious. Since task 14.1.10 a
        # version bump does not touch anything in `xhciqual\` at all - `qual.h`
        # expands a macro rather than declaring a number - so without the header
        # in this set, bumping the version and shipping yesterday's EXE would
        # pass every check in this script: the declared version would be read
        # from the header and match, and nothing beside the EXE would be newer
        # than it. The single source removed a copy and moved a dependency, and
        # this is where the dependency is paid for.
        #
        $newer = @(@(Get-ChildItem -LiteralPath $QualtoolDir -File |
            Where-Object { $_.Extension -in @(".c", ".h", ".asm") }) +
            @(Get-Item -LiteralPath $versionHeader) |
            Where-Object { $_.LastWriteTimeUtc -gt $exeTime })
        if ($newer.Count -gt 0) {
            throw @"
'$exePath' is older than $($newer.Count) of its own source file(s):
  - $(($newer | ForEach-Object { $_.Name }) -join "`n  - ")
Rebuild it (xhciqual\SETENV.BAT then xhciqual\build.cmd) before releasing. A
qualifier that predates its sources answers for a tool nobody can rebuild.
"@
        }

        # **The .MAP has to belong to the .EXE beside it**, and the readme below
        # tells the user it "only matches THIS build of the EXE" - so a MAP left
        # over from an earlier build would make that sentence false in the one
        # direction that costs something: an address decoded against the wrong
        # map produces a *plausible* wrong answer, not an obvious failure. The
        # linker writes both in the same pass, so a MAP older than the EXE is a
        # MAP from a different build.
        #
        # This checks relative age, which is what a timestamp can carry. It does
        # not prove the pair came from one link - the freshness test above has
        # the same limit, since an EXE can be copied or touched after its
        # sources. Proving it needs a build identity embedded in both, which the
        # qualifier does not carry; the honest statement of that gap is here
        # rather than in a check that reads stronger than it is.
        $mapPath = Join-Path $QualtoolDir "xhciqual.map"
        $mapTime = (Get-Item -LiteralPath $mapPath).LastWriteTimeUtc
        if ($mapTime -lt $exeTime) {
            throw @"
'$mapPath' is older than '$exePath'.
The .MAP only decodes addresses for the build of the EXE it was linked with, and
the release readme tells the user exactly that. Rebuild the qualifier
(xhciqual\SETENV.BAT then xhciqual\build.cmd) so the pair comes from one link.
"@
        }

        # **The sixth copy of the version, and the only gate that reaches it.**
        # Both checks above compare timestamps, so they catch a stale binary and
        # not a stale number inside a fresh one - which is the failure that
        # matters here, because the qualifier prints TOOL_VERSION into every log
        # a user saves and sends back. A log headed "XHCIQUAL 0.11" taken from
        # inside a 0.0.0.2 release names an artifact that does not exist.
        $qualHeader = Join-Path $QualtoolDir "qual.h"
        if (-not (Test-Path -LiteralPath $qualHeader)) {
            throw "'$qualHeader' is missing; the qualifier's TOOL_VERSION cannot be checked against the release version."
        }
        Assert-TakesSharedVersion -Path $qualHeader -Macro "TOOL_VERSION"
        $toolVersion = Get-DeclaredVersion -Path $versionHeader
        if (-not (Test-DriverVersionMatches -Reported $toolVersion -Declared $Version)) {
            throw @"
'$versionHeader' declares the version as "$toolVersion", but this release is $Version.
The qualifier is published inside releases\$Version\tools\xhciqual\ and prints that
string into every log a user saves, so the two must agree. Bump the version in
src\xhci_version.h and rebuild the qualifier (xhciqual\SETENV.BAT then
xhciqual\build.cmd) - see docs\contributing\build-and-test.md, "Versioning the
driver".
"@
        }

        $qualDir = Join-Path $destRoot (Get-ToolDirName -Name "xhciqual")
        Ensure-Directory $qualDir
        foreach ($src in $qualtoolFiles.Keys) {
            Copy-Item -LiteralPath (Join-Path $QualtoolDir $src) `
                      -Destination (Join-Path $qualDir $qualtoolFiles[$src]) -Force
        }
        # Its report batch (roadmap-hcd task 38.1), beside it. Not compiled, so
        # no freshness rule applies; what it says is held, once staged, to the
        # shared checker and the repository's own bytes (Assert-ReportBatchContent).
        Publish-ReportBatches -SourceDir $QualtoolDir -DestDir $qualDir -Map $reportBatches["xhciqual"]
        $qualtoolStaged = $qualDir
        Write-Ok (("XHCIQUAL.EXE staged at version {0} from src\xhci_version.h, " +
                   "newer than every .c/.h/.asm beside it and than that header, " +
                   "with a .MAP no older than itself, and QUALREP.BAT beside it") -f $toolVersion)
    }

    # --- the snapshot reader ------------------------------------------------
    #
    # **XHCISNAP has never been in a release** (task 13-L.2). It was a bench
    # companion to a probe build that could not ship either, and now it is how a
    # user gets evidence off a Windows 98 machine at all: the driver's snapshot
    # channel is in every shipping flavour and this is the only thing that
    # speaks to it. A release without it publishes a channel nobody can open.
    #
    # It is built with MSVC 6.0 in place, so unlike the DOS qualifier it needs
    # no extra toolchain on the cutting host - but it gets the **same staleness
    # throw**, and it matters more here than there: the tool duplicates the
    # driver's wire format rather than including `src\xhci.h`, and refuses any
    # driver whose reply signature, schema version or header size is not the one
    # it was built against. **That refusal is a good failure and a wasted
    # download**, and a stale XHCISNAP.EXE in a cut is exactly how a user gets
    # one - they would install a working driver, run the tool, and be told to
    # rebuild something they cannot rebuild.
    $snaptoolStaged = $null
    if (-not $SkipSnapTool) {
        Write-Step "snapshot reader"

        $snapExe = Join-Path $SnapToolDir "XHCISNAP.EXE"
        if (-not (Test-Path -LiteralPath $snapExe)) {
            throw @"
'$SnapToolDir' has no XHCISNAP.EXE.
Build it first:  xhcisnap\build.cmd
It uses the in-repo MSVC 6.0 and installs nothing, so there is no toolchain to
acquire. A release without it publishes a read channel nobody can open.
"@
        }

        # `src\xhci_version.h` is one of its sources too - see the same set in
        # the qualifier's check above for why a version bump would otherwise be
        # invisible to every check in this script.
        $snapTime = (Get-Item -LiteralPath $snapExe).LastWriteTimeUtc
        $snapNewer = @(@(Get-ChildItem -LiteralPath $SnapToolDir -File |
            Where-Object { $_.Extension -in @(".c", ".h") }) +
            @(Get-Item -LiteralPath $versionHeader) |
            Where-Object { $_.LastWriteTimeUtc -gt $snapTime })
        if ($snapNewer.Count -gt 0) {
            throw @"
'$snapExe' is older than $($snapNewer.Count) of its own source file(s):
  - $(($snapNewer | ForEach-Object { $_.Name }) -join "`n  - ")
Rebuild it (xhcisnap\build.cmd) before releasing. This tool refuses any driver
whose snapshot schema is not the one it was built against, so a stale one is a
download that answers a working driver with "rebuild the tool".
"@
        }

        # **And the wire format has to be the driver's own.** The timestamp
        # above catches a tool older than its sources; this catches the case
        # that actually bites - a tool rebuilt from sources that never learned
        # about a schema the DRIVER moved. The number is in two files by
        # construction (the tool may not include a kernel header), so the only
        # honest check is to compare them.
        $snapSchemaTool = Get-SnapSchema -Path (Join-Path $SnapToolDir "xhcisnap.c") -Macro "SNAP_SCHEMA"
        $snapSchemaDrv = Get-SnapSchema -Path (Join-Path $repo "src\xhci.h") -Macro "XHCI_SNAPSHOT_SCHEMA"
        if ($snapSchemaTool -ne $snapSchemaDrv) {
            throw @"
xhcisnap\xhcisnap.c declares SNAP_SCHEMA $snapSchemaTool and src\xhci.h declares
XHCI_SNAPSHOT_SCHEMA $snapSchemaDrv. The tool duplicates the wire format rather
than including a kernel header, and refuses at run time any driver whose schema
is not its own - so publishing these two together would ship a tool that cannot
read the driver beside it.
"@
        }

        # **The reader's version, and this is the only gate that reaches it.**
        # It was a seventh copy of the number until task 14.1.10 made it an
        # expansion of the shared macro, so what is checked here is now two
        # things rather than one: that the source still takes the macro, and
        # that the macro's value is this release's. Both for the reason the
        # qualifier's check above exists: the timestamp checks catch a stale
        # binary and not a stale number inside a fresh one, and this tool now
        # prints its version on its default screen AND into the header of every
        # report a user pastes into an issue. A report headed "xhcisnap 0.0.0.5"
        # taken out of a 0.0.0.6 release names a build that was never published.
        $snapSource = Join-Path $SnapToolDir "xhcisnap.c"
        Assert-TakesSharedVersion -Path $snapSource -Macro "XHCISNAP_VERSION"
        $snapVersion = Get-DeclaredVersion -Path $versionHeader
        if (-not (Test-DriverVersionMatches -Reported $snapVersion -Declared $Version)) {
            throw @"
'$versionHeader' declares the version as "$snapVersion", but this release is $Version.
XHCISNAP is published inside releases\$Version\tools\xhcisnap\, prints that string on
its default screen, and writes it into the header of every report a user sends
back, so the two must agree. Bump the version in src\xhci_version.h and rebuild
the tool (xhcisnap\build.cmd) - see docs\contributing\build-and-test.md,
"Versioning the driver".
"@
        }

        $snapDir = Join-Path $destRoot (Get-ToolDirName -Name "xhcisnap")
        Ensure-Directory $snapDir
        Copy-Item -LiteralPath $snapExe -Destination (Join-Path $snapDir "XHCISNAP.EXE") -Force
        # Its three report batches (roadmap-hcd task 38.1), beside it.
        Publish-ReportBatches -SourceDir $SnapToolDir -DestDir $snapDir -Map $reportBatches["xhcisnap"]
        $snaptoolStaged = $snapDir
        Write-Ok (("XHCISNAP.EXE staged at version {0} from src\xhci_version.h, " +
                   "wire schema {1}, matching src\xhci.h, newer than every .c/.h " +
                   "beside it and than that header, with SNAPON.BAT, SNAPGET.BAT " +
                   "and SNAPOFF.BAT beside it") -f $snapVersion, $snapSchemaTool)
    }

    # --- the port switchover's scripts ---------------------------------------
    #
    # Once, into tools\portsw\ beside the other tools' directories (roadmap-hcd
    # tasks 37.5 and 38.10), from src\ rather than from a package: the packages no longer
    # carry them, and the four flavour directories once held four
    # byte-identical copies of files that are neither architecture- nor
    # flavour-specific. No readme.txt of their own: section 9 of the
    # top-level readme is where they are explained, and each script says
    # what it does when run.
    Write-Step "port switchover scripts"
    $pswStaged = Publish-PortSwitchScripts -SourceDir (Join-Path $repo "src") -DestRoot $destRoot -Names $pswPublishable -DirName (Get-ToolDirName -Name "portsw")
    Write-Ok ("{0} published once, in {1}\" -f ($pswPublishable -join ", "), (Get-ToolDirName -Name "portsw"))

    #
    # Two flavours that hash the same are one binary published twice. The
    # VS_FF_DEBUG check above already makes that nearly impossible, which is the
    # reason to keep this: it is the check that fails if that one is ever
    # weakened, and it costs nothing.
    #
    # **Compared WITHIN an architecture, and it has to be.** The x86 and x64
    # binaries of the same flavour are different builds of different machine
    # code and could never hash alike, so a flat comparison across four staged
    # files would find every pair distinct and say nothing - while the pair
    # this check exists for, release and debug of one architecture, would be
    # buried among them. Two comparisons, each of the pair that could actually
    # collide.
    #
    foreach ($a in $Arch) {
        $rel = "release-$a"
        $dbg = "debug-$a"
        if ($staged.ContainsKey($rel) -and $staged.ContainsKey($dbg) -and
            $staged[$rel].Sha256 -eq $staged[$dbg].Sha256) {
            throw "the release and debug $a binaries are identical - one build was packaged twice."
        }
    }

    # --- the per-version README ---------------------------------------------
    #
    # Generated rather than hand-written: every release says the same thing, and
    # the parts that vary (version, date, sizes, hashes) are exactly what a human
    # copy gets wrong. It has to stand on its own - someone who downloaded a
    # release directory has no repository to read - so the install and usage
    # procedure is transcribed here rather than linked to. Everything below is
    # from docs\using\release-notes.md; when that file changes, change this too.
    # Rewritten for the host controller driver and its two drivers per
    # directory by roadmap task 32.1. Its TODO(...) lines are readings the
    # cut (task 32.3) owes; a readme still carrying one is not ready to cut.
    #
    # **A literal here-string, not a double-quoted one.** Backtick is PowerShell's
    # escape character, so in @"..."@ every markdown `code span` would lose its
    # backticks silently - `xhci98.inf` comes out as xhci98.inf. Placeholders are
    # substituted afterwards instead.
    # **The release date is the release's, not the build host's.** This used to
    # be (Get-Date), so a directory cut a day after the version was settled
    # printed one date above a release-history section, embedded in the same
    # file, whose entry for the same version read another - and the INF's
    # DriverVer agreed with the entry rather than with the readme. It is taken
    # from the INF and was agreed with history.md's heading up
    # at the changelog gate, before anything was built.
    $today = $releaseDate

    # Embed from the first '##' heading down, so history.md's own title and
    # maintenance note stay out of the user-facing file, and demote each entry
    # heading by one level to sit under this README's "Release history" section.
    # The *check* that an entry exists ran before anything was built.
    $firstHeading = 0
    for ($i = 0; $i -lt $historyLines.Count; $i++) {
        if ($historyLines[$i] -match '^##\s') { $firstHeading = $i; break }
    }
    $history = @($historyLines[$firstHeading..($historyLines.Count - 1)] | ForEach-Object {
        if ($_ -match '^##\s') { "#" + $_ } else { $_ }
    })

    #
    # One block per published directory, in leg order. The architecture is named
    # in the heading rather than left to the directory name alone, because the
    # user reading this has to pick one and the two directories differ in
    # nothing a file listing shows: same two filenames, same version, same INF
    # text apart from its decorations.
    #
    # A wrong pick is safe and is said so plainly. It is not a claim about
    # taste - the 64-bit setup engine ignores an undecorated models section
    # outright and the 32-bit engines skip an NTamd64-decorated one, so the
    # wrong directory offers no driver at all rather than installing one that
    # cannot load.
    #
    $multiArch = @($legs | ForEach-Object { $_.Arch } | Sort-Object -Unique).Count -gt 1
    $contents = @()
    foreach ($leg in $legs) {
        if (-not $staged.ContainsKey($leg.Id)) { continue }
        $s = $staged[$leg.Id]
        $which = if ($leg.Arch -eq "x64") { "64-bit Windows" } else { "32-bit Windows" }
        if ($leg.Flavor -eq "release") {
            if ($multiArch) {
                $contents += ("  {0}\  - INSTALL THIS ONE, on {1}" -f $s.Published.ToUpper(), $which)
            } else {
                $contents += ("  {0}\  - INSTALL THIS ONE" -f $s.Published.ToUpper())
            }
            $contents += ""
            $contents += "  The normal driver. This is the one you want."
        } else {
            $contents += ("  {0}\  - only when diagnosing a problem" -f $s.Published.ToUpper())
            $contents += ""
            $contents += "  The same driver, built so that a crash on it can be traced"
            $contents += ("  further back. It records nothing more than {0}\ does, and" -f ("release-" + $leg.Arch).ToUpper())
            $contents += "  it prints nothing as it runs. It is here only so that it can be"
            $contents += "  installed at this exact version if something goes wrong. Do not"
            $contents += "  install it otherwise - and note that BOTH builds answer"
            $contents += "  XHCISNAP, so you do not need this one to send a report."
        }
        $contents += ""
        $contents += "      xhci98.inf"
        $contents += ("      xhci98.sys   {0:N0} bytes" -f $s.Length)
        $contents += "      SHA-256"
        $contents += ("      {0}" -f $s.Sha256)
        $contents += "      xhciuas.inf  (the UAS storage class driver's INF)"
        $contents += ("      xhciuas.sys  {0:N0} bytes" -f $s.UasLength)
        $contents += "      SHA-256"
        $contents += ("      {0}" -f $s.UasSha256)
        $contents += "      txtsetup.oem (for Windows 2000 and XP Setup's F6 prompt)"
        if ($leg.Arch -eq "x86") {
            $contents += "      xhcimsi.sys  (the MSI helper for 32-bit Vista and 7; section 4)"
        } else {
            $contents += "      xhci98-test.cer (the test certificate both .sys files are"
            $contents += "                   signed with; you need not import it; section 4)"
        }
        $contents += ""
    }
    if ($multiArch) {
        $contents += "  Which pair: the -X86 directories are for 32-bit Windows and the"
        $contents += "  -X64 ones for 64-bit Windows. If you pick the wrong one nothing"
        $contents += "  breaks - Windows simply finds no driver in it and says so - so"
        $contents += "  try the other."
        $contents += ""
    }
    if ($null -ne $qualtoolStaged) {
        $qexe = Get-Item -LiteralPath (Join-Path $qualtoolStaged "XHCIQUAL.EXE")
        $contents += "  TOOLS\XHCIQUAL\  - the DOS machine checker from step 1"
        $contents += ""
        $contents += ("      XHCIQUAL.EXE {0:N0} bytes" -f $qexe.Length)
        $contents += "      XHCIQUAL.MAP keep it beside the EXE; see TOOLS\XHCIQUAL\readme.txt"
        $contents += "      QUALREP.BAT  the read-only probe a bug report asks for, saved"
        $contents += "                   to a new .LOG named from the date and time"
        $contents += "      readme.txt"
        $contents += "      NOTICE.TXT   third-party notices this EXE carries"
        $contents += ""
    }
    if ($null -ne $snaptoolStaged) {
        $sexe = Get-Item -LiteralPath (Join-Path $snaptoolStaged "XHCISNAP.EXE")
        $contents += "  TOOLS\XHCISNAP\  - what to run if something goes wrong"
        $contents += ""
        $contents += ("      XHCISNAP.EXE {0:N0} bytes" -f $sexe.Length)
        $contents += "      SNAPON.BAT   turns the driver's log channel on"
        $contents += "      SNAPGET.BAT  writes the report and the dump a bug report asks for"
        $contents += "      SNAPOFF.BAT  turns the channel off again"
        $contents += "      readme.txt"
        $contents += "      NOTICE.TXT   third-party notices this EXE carries"
        $contents += ""
        $contents += "      It reads the driver's own log off the running"
        $contents += "      machine and writes a report you can attach to a"
        $contents += "      bug report. On Windows 98 it is the only way to"
        $contents += "      get anything out at all. Four steps, and none of"
        $contents += "      them is regedit (section 6):"
        $contents += ""
        $contents += "        SNAPON.BAT"
        $contents += "        restart Windows"
        $contents += "        make the problem happen again"
        $contents += "        SNAPGET.BAT"
        $contents += ""
        $contents += "      Then attach the .DEV and .TXT files it names,"
        $contents += "      and run SNAPOFF.BAT once you have sent them."
        $contents += "      At a prompt, SNAPON 3 sets level 3 rather than"
        $contents += "      2 (1 to 4), and SNAPGET MYPC saves the files as"
        $contents += "      MYPC.DEV and so on; section 6 has the rules."
        $contents += ""
    }
    if ($null -ne $pswStaged) {
        $contents += "  TOOLS\PORTSW\  - the Intel port switchover's switch (section 9)"
        $contents += ""
        $contents += "      portsw.vbs     shows XhciIntelPortSwitch, and sets it on or off"
        $contents += "      portsw-on.bat  turns it on in one double-click"
        $contents += "      portsw-off.bat turns it off in one double-click"
        $contents += ""
        $contents += "      One copy for every driver directory above. Run them from"
        $contents += "      this directory."
        $contents += ""
    }

    $contents += "  LICENSE"
    $contents += ""
    $contents += "  The GNU GPL v2 this driver is published under, with the note on"
    $contents += "  what in the wider project is third-party material and is NOT"
    $contents += "  covered by it. The LICENCE section at the end points here."
    $contents += ""

    #
    # **The install section names the directories this cut actually wrote.**
    # Since roadmap task 21.3 those are RELEASE-X86 and DEBUG-X86, and a cut
    # carrying both architectures has four. A one-architecture cut names its
    # pair outright, which is what a reader wants; a two-architecture one names
    # the pair for the architecture-neutral case and adds a paragraph saying
    # how to choose, because there is then no single directory to point at.
    #
    # Hardcoding RELEASE\ here is what this replaces, and it would have been
    # wrong in the quietest possible way: a readme telling a user to install
    # from a directory the download does not contain.
    #
    $readmeArches = @($legs | ForEach-Object { $_.Arch } | Sort-Object -Unique)
    if ($readmeArches.Count -eq 1) {
        $readmeReleaseDir = ("release-" + $readmeArches[0]).ToUpper()
        $readmeDebugDir   = ("debug-" + $readmeArches[0]).ToUpper()
        $readmeArchNote   = ""
        # Where the INF says its two files live, and what tells two copies of
        # xhci98.sys apart. Both were hardcoded in the template as "release\
        # and debug\" and "the debug flag on the Version tab", which a
        # four-directory cut makes false twice over: there are four
        # directories, and the debug flag does not separate x86 from x64 (the
        # 2026-09-16 audit's D5).
        $readmeDirList    = ("{0}\ and in {1}\" -f $readmeReleaseDir, $readmeDebugDir)
        $readmeTellApart  = @"
The one thing that tells them apart is the "debug" flag shown on the Version
tab of the file's properties.
"@
        # An x86-only cut carries no 64-bit driver, so its readme may not name
        # a 64-bit target. This is not hypothetical: -UploadSetOnly re-renders
        # the readme of an already-published version, and every version
        # published before 1.1.0.0 is x86-only.
        $readmeArchOs     = ""
        $readmeArchOsReq  = ""
        $readmeArchFiles  = ""
        $readmeArchInstall = ""
    } else {
        $readmeReleaseDir = "RELEASE-X86 or RELEASE-X64"
        $readmeDebugDir   = "DEBUG-X86 or DEBUG-X64"
        $readmeDirList    = "RELEASE-X86\, DEBUG-X86\, RELEASE-X64\ and DEBUG-X64\"
        $readmeTellApart  = @"
Nothing you can see in Explorer tells the two architectures apart: the "debug"
flag on the Version tab separates RELEASE from DEBUG, and there is no
equivalent for 32-bit against 64-bit. (The architecture IS in the file, in
the PE header, but reading it takes a tool.) So the directory a copy came out
of is what identifies it.
"@
        $readmeArchNote   = @"


There are two of each, one per architecture: the -X86 directories are for
32-bit Windows and the -X64 ones for 64-bit Windows. If you pick the wrong
one nothing breaks - Windows finds no driver in it and says so - so try the
other.
"@
        $readmeArchOs = @"

  - Windows XP, Vista and 7, x64, through separate 64-bit drivers. On Vista
    x64 and 7 x64 they carry a test signature only, so Windows must be in
    test-signing mode, or driver signature enforcement disabled (section 4).
"@
        $readmeArchOsReq = @"

                     64-bit: Windows XP x64 (SP2), Windows Vista x64 (SP2)
                     or Windows 7 x64 (SP1), in
                     virtual machines; XP x64 and 7 x64 also on one real
                     machine, a Dell Latitude E7250.
"@
        $readmeArchInstall = @"


  64-BIT WINDOWS
      The same route as the 32-bit edition of the same Windows, pointed at
      RELEASE-X64\. Windows XP x64 needs nothing more.

      WINDOWS VISTA X64 AND WINDOWS 7 X64: TEST-SIGNING MODE
      The 64-bit xhci98.sys and xhciuas.sys are test-signed, so on these
      two systems the driver can run in test-signing mode instead of
      disabling driver signature enforcement with F8 at every start. Once:

        1. Open an elevated Command Prompt: Start, type cmd, right-click
           it, Run as administrator.
        2. Type:   bcdedit /set testsigning on
           This is a boot setting of the current Windows entry, so it stays
           on for every start until it is turned off; no F8 is needed.
        3. Restart. "Test Mode" shows on the desktop while it is on:
           at the bottom right on 7, with the version and build, and in
           the corners on Vista.
        4. Install the driver as above. Windows still says it cannot
           verify the publisher; choose "Install this driver software
           anyway". Afterwards the Driver tab says "Not digitally
           signed". Both are expected: the driver is test-signed, not
           signed by Microsoft.

      To check: bcdedit /enum {current} shows "testsigning Yes".
      To turn it off: bcdedit /set testsigning off, and restart. The
      driver then needs F8 again, or it does not load and the controller
      shows Code 39 on Vista, Code 52 on 7.

      - With BitLocker on the system drive, suspend BitLocker before
        changing the boot setting, or the next start asks for the
        recovery key.
      - This is not Microsoft signing: the certificate is this project's
        own test certificate, which no Windows trusts by itself.
      - The X64 directories carry xhci98-test.cer, its public half.
        Importing it is not needed: test-signing mode works without it,
        and it does not remove the install prompt either. It is not
        recommended.
      - F8 -> Disable Driver Signature Enforcement still works as before,
        for one start, without test mode.
      - 32-bit Vista and 7, and XP x64, need neither.
"@
        $readmeArchFiles = @"

  WINDOWS XP x64  Nothing to do either: both come from Driver Cache\amd64.
                  The machine tried (a virtual one, which had never had a
                  USB controller) asked for no file.
"@
    }

    # Plain text, 78 columns. Not markdown: this is read on the target machine,
    # in Windows 98 Notepad or DOS EDIT, where a .md file renders as nothing and
    # its syntax is just noise.
    $template = @'
==============================================================================
                              x h c i 9 8   {VERSION}
    USB 3.x for Windows 98 SE, ME, 2000, XP, Vista and 7 on xHCI machines
==============================================================================
{TESTBANNER}
Released {DATE}.{INCOMPLETE}

Most PCs made from the mid 2010s onward have only USB 3.x (xHCI)
controllers, which older Windows cannot use. This package drives them on:

  - Windows 98 SE, ME, 2000 SP4, XP, Vista and 7, 32-bit{ARCHOS}

xhci98.sys is a whole USB host controller driver. It runs the controller,
the root hub, every hub behind it and the splitting of composite devices
itself, and Windows' own drivers for keyboards, mice, storage, audio and
network adapters sit on top of it unchanged. It drives SuperSpeed (USB 3.x)
devices and hubs as well as High, Full and Low Speed ones. The second driver
beside it, xhciuas.sys, runs UAS (USB Attached SCSI) storage, which none of
these systems has a driver of its own for.


ISSUE REPORTING
------------------------------------------------------------------------------

This is a hobby driver for operating systems that left support two decades
ago, it has run on a small number of machines, and BUGS ARE NOT UNEXPECTED.

Please report what you find, on the project's GitHub page:

      https://github.com/yeokm1/xhci98/issues

The form there asks for what a report needs.


CONTENTS OF THIS FILE
------------------------------------------------------------------------------

  1. Check the machine first (optional, but recommended)
  2. What you need
  3. The files Windows supplies, and USB storage on Windows 98 SE
  4. Install
  5. Using it
  6. If something goes wrong
  7. Known limitations
  8. What is in this directory
  9. Registry settings
 10. Release history


==============================================================================
 1. CHECK THE MACHINE FIRST        (optional, but recommended)
==============================================================================

You can skip to step 2 and simply try the driver. Nothing here is required,
and a machine that cannot run it fails visibly rather than dangerously.

It is recommended because not every xHCI controller can be driven, and ONE OF
THE WAYS IT CAN FAIL CANNOT BE FIXED IN SOFTWARE. Finding that out in thirty
seconds is cheaper than finding it out after installing an operating system.

The checker is in the TOOLS\XHCIQUAL\ subdirectory. Copy XHCIQUAL.EXE to a DOS
boot disk, and run ONE command:

      XHCIQUAL

That is it - no arguments. It is read-only: it takes ownership of nothing and
writes no PCI configuration register. It prints one of four verdicts:

  LOOKS QUALIFIED   nothing a read-only pass can see disqualifies this
                    machine. Go ahead and install.

  LOOKS QUALIFIED WITH CAVEATS
                    the same, but the controller has no interrupt pin. The
                    driver reads its events on a timer instead (polling),
                    or on Vista and 7 may take a message-signalled
                    interrupt; the line says whether the controller offers
                    one. Go ahead and install.

  DISQUALIFIED      something it can see rules the machine out: no xHCI
                    controller, the controller's memory window is unusable
                    or sits above 4 GB, or there are no USB 2.0 ports.

  CANNOT SAY        something it is not allowed to change is in the way -
                    the controller is powered down, or its memory access is
                    switched off. Check the BIOS and try again.

The tool takes options too, and you need none of them to answer the question
above. The whole command line is below so that a log someone asks you for can
be produced without guesswork; TOOLS\XHCIQUAL\readme.txt carries the same list
with the safety notes spelled out.

      XHCIQUAL                          the read-only quick scan, one screen
      XHCIQUAL [xhci|ehci|ohci|all] [options]
      XHCIQUAL --scan TYPE [--scan TYPE ...] [options]
      XHCIQUAL --help

  Options may be written in any order, before or after a family word.

  WHICH CONTROLLERS IT LOOKS AT. With no family word it looks at all three.
  The driver only cares about xHCI; the other two are there because a
  machine's other controllers are part of the picture when something does
  not add up.

      xhci | ehci | ohci     one family only
      all                    all three - the default
      --xhci --ehci --ohci   the same three selectors, written as options
      --scan TYPE            the same again; repeat it to combine families

  READ-ONLY MODES - these change nothing on the machine.

      --quick           the no-argument quick scan, asked for explicitly
      --probe-only      read-only discovery, fuller than --quick. It reads
                        the controller's memory window only if the firmware
                        has already switched it on, and switches nothing on
                        itself
      --no-active       another name for --probe-only
      --gsi N           (also --gsi=N, N decimal 0-255) compare an Intel
                        chipset's interrupt routing against N, the IRQ
                        Device Manager shows for the controller under the
                        system in question. Accepted only beside --quick or
                        --probe-only; alone it is a usage error

  MODIFIERS - these say what to do with the report, not what to run.
  ON THEIR OWN THEY DO NOT MAKE THE RUN READ-ONLY. Any argument at all
  turns off the no-argument quick scan, so XHCIQUAL --log FILE performs
  the FULL ACTIVE run below. Pair one with --quick or --probe-only when a
  read-only run is what you want.

      --no-page         do not stop at the end of each screenful
      --serial          mirror the output to COM1, 115200 8N1
      --log [FILE]      also write the report to a file, default
                        XHCIQUAL.LOG. A family word after --log is read as a
                        selector, so name the file explicitly if you pass
                        both
      --log-stamp       write the report to a NEW file in the current
                        directory named from the DOS date and time,
                        mDDhhmmS.LOG; it never overwrites a file, and not
                        with --log. QUALREP.BAT uses it (section 6)
      --done-flag FILE  create FILE only if the run finishes normally, so a
                        batch file can tell a crash apart from a bad verdict
      --no-sysinfo      do not read the machine's description (SMBIOS,
                        processor, memory, DOS version) for the report's
                        header; the header then says it was skipped
      --help, -h, /?    a longer help text, printed by the program itself

  ACTIVE OPTIONS - THESE TAKE OVER THE CONTROLLER, reset it, and reset its
  ports. They are development instrumentation; installing this driver never
  requires them, and TOOLS\XHCIQUAL\readme.txt carries the precautions they
  need.

      --full            the full active run, across all three families
      --poll-only       active bring-up with no interrupt handler installed.
                        The mildest of these, and the one to try first
      --irq-selftest    xHCI only: an isolated, one-shot interrupt test
      --set-intel-ports try to route the Intel USB2 ports to xHCI and read
                        the result back. This one writes PCI configuration
                        space
      --no-wait         do not wait 15 seconds for a device to be plugged in
      --no-devid        skip the xHCI device identification step

FOR A BUG REPORT, run TOOLS\XHCIQUAL\QUALREP.BAT from its own directory under
real DOS: the read-only probe, saved to a new .LOG named from the date and
time. Attach the .LOG it names (section 6).

IF YOU ARE ASKED FOR LOGS, run these two in order. The first reads everything
the read-only path can see, writes nothing to the machine, and leaves
PROBE.LOG beside it:

      XHCIQUAL --probe-only --log PROBE.LOG

If that does not crash the machine, continue with the full run, which leaves
FULL.LOG. THIS ONE TAKES OVER THE CONTROLLER, resets it and resets its ports;
use a PS/2 keyboard, and do not write the log to a drive on the controller
being tested:

      XHCIQUAL --log FULL.LOG

Send FULL.LOG if the full run finished; only if it did not, send PROBE.LOG.
The report's header names the machine it ran on - make, model and BIOS,
processor, memory and DOS version - and never a serial number, a UUID or an
asset tag, so a log can be posted publicly as it is.

It returns 0 if the active tests passed, 1 if the machine is not qualified or
the run was read-only - which cannot pass tests it does not run, so 1 is the
normal result of every read-only command above - 2 if the command line was
wrong or no controller of that kind is here, and 3 if the full run passed a
controller only WITH CAVEATS (no working interrupt, so the driver polls
it), and 4 if --log-stamp could not name or create its log, before reading
anything. The verdict on screen is what to read; those codes are for batch
files.

>> IT MUST BE RUN FROM REAL DOS, NOT A DOS WINDOW INSIDE WINDOWS. <<

   Boot the machine to a plain MS-DOS or FreeDOS floppy, CD or USB key. Do
   not run it from a "MS-DOS Prompt" in Windows 98, and not from CMD.EXE on
   Windows 2000 or later. The tool talks to the controller directly and needs
   memory it can address one-to-one, which a DOS box inside Windows does not
   give it - there, the answers would be wrong or it would simply fail.
   For the same reason, boot without EMM386 or any other memory manager.

   HIMEM.SYS IS THE EXCEPTION, AND ON SOME MACHINES IT IS NEEDED. It is not
   a memory manager in the sense above - it does not put the processor into
   the mode that breaks this tool - and XHCIQUAL runs in 32-bit mode, so it
   needs the extended memory HIMEM provides. If the program will not run at
   all on a boot that loads nothing, add this one line to CONFIG.SYS and try
   again:

         DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V

   Use whatever path HIMEM.SYS is actually at - C:\WINDOWS\ on a Windows 98
   machine, the root of the disk on a boot floppy. The /V makes it say at
   boot whether it loaded.

   Two more things, only if you go on to run the deeper tests listed in
   TOOLS\XHCIQUAL\readme.txt: use a PS/2 keyboard, because a USB keyboard on
   the controller being tested can stop responding mid-run; and do not boot
   or write a log through that same controller.

A controller reporting no interrupt pin is still driven: the driver reads
its events on a timer instead (polling, XhciInterruptMode in section 9),
and on Windows Vista and 7 it may use a message-signalled interrupt (MSI)
in place of the pin. A controller whose interrupt line the driver sees
raised and never delivered is polled the same way.

The checker's last criterion, "no USB 2.0 ports", dates from the earlier,
USB 2.0-only releases. This driver accepts a controller with no USB 2.0
port at all, built from the specification; no such controller has been
tested.


==============================================================================
 2. WHAT YOU NEED
==============================================================================

  Operating system   Windows 98 SE (4.10.2222) or Windows 2000 SP4; Windows
                     ME, 32-bit Windows XP (SP3), 32-bit Windows Vista (SP2)
                     and 32-bit Windows 7 (SP1) in virtual machines only.
                     Windows 2000 has never run on real hardware.{ARCHOSREQ}

  On Windows 98 SE   Nothing for the controller, hubs, mice, keyboards and
                     audio: no USB 2.0 stack is needed. FOR USB STORAGE,
                     NUSB 3.3 or 3.6, or at least its mass-storage part
                     (section 3). SweetLow's USB 2.0 stack alone has no
                     storage part.

  On Windows ME      Nothing to install: no USB 2.0 stack is needed, and
                     SweetLow's, if already there, also works. Windows ME
                     carries its own USB storage files. DO NOT install
                     NUSB on Windows ME: it is a Windows 98 SE package.

  On Windows 2000,   Nothing to install. DO NOT install NUSB on these.
  XP, Vista and 7

  Controller         xHCI, PCI class code 0C0330, and a memory window
                     below 4 GB. No interrupt pin is needed: a controller
                     without one is polled (section 9, XhciInterruptMode).


==============================================================================
 3. THE FILES WINDOWS SUPPLIES, AND USB STORAGE ON WINDOWS 98 SE
==============================================================================

The package's own files are in:

      {DIRLIST}

  xhci98.inf, xhci98.sys     the USB host controller driver
  xhciuas.inf, xhciuas.sys   the UAS storage driver
  txtsetup.oem               the driver description Windows 2000 and XP
                             Setup read when F6 is pressed (section 4)
  xhcimsi.sys               in the -X86 directories only: the helper that
                             gives 32-bit Vista and 7 a message-signalled
                             interrupt. The install puts it on those two
                             systems and no other

Nothing else is in the package, and there is nothing to complete. The
Intel port switchover's scripts (section 9, XhciIntelPortSwitch) are not
part of it: they are in the TOOLS\PORTSW directory, one copy for all of
them.

Two files the driver depends on are NOT in the package, because they are
Windows' own, unmodified, and no Microsoft file is in this download:

  usbd.sys     Helper routines the drivers above this one call. Without it
               the USB devices' own drivers do not load.

  usbui.dll    The USB property pages in Device Manager. Only cosmetic.

UP TO WINDOWS XP, WINDOWS ONLY INSTALLS ITS USB FILES WHEN SETUP FINDS A USB
CONTROLLER IT RECOGNISES, and on an xHCI-only machine it never does. The
install in step 4 therefore asks Windows to copy them from its own
installation source, each only if it is absent.

  WINDOWS 98 SE   HAVE THE WINDOWS 98 SE INSTALLATION CD AT HAND. Unless the
                  Windows CABs are on the hard disk (C:\WINDOWS\OPTIONS\CABS,
                  as on OEM and Windows 98 QuickInstall installs), the
                  install shows "Insert Disk" asking for the Windows 98
                  Second Edition CD-ROM: insert it and click OK, and if it
                  then asks where to copy from, give it the CD's WIN98
                  folder. It is asking for usbd.sys, not for anything of
                  this driver's. The first mouse or keyboard can ask again,
                  for Windows' own hidclass.sys; give it the same.

  WINDOWS ME      The same, with the Windows ME CD, or the CABs its Setup
                  left on the hard disk.

  WINDOWS 2000    Nothing to do: both come from the driver cache every
  AND XP          installation has, with no prompt.{ARCHFILES}

  WINDOWS VISTA   Nothing to do: every installation already has both.
  AND 7

USB STORAGE ON WINDOWS 98 SE NEEDS NUSB'S MASS-STORAGE PART
...........................................................

Windows 98 SE has no USB storage driver, and a drive letter there comes from
a layer this package does not replace. It needs five files, which NUSB 3.3
and NUSB 3.6 install:

      USBSTOR.INF, USBSTOR.SYS     the Bulk-Only storage driver
      USBNTMAP.INF, USBNTMAP.SYS   what gives a USB disk a drive letter
      USBMPHLP.PDR                 the mapping port driver

They are Microsoft's, and this package does not carry them.

  WITHOUT THEM a USB stick has no driver at all ("Unknown Device", Code 28),
  and a UAS disk installs and then sits at Code 2 ("The NTKERN.VXD device
  loader(s) for this device could not load the device driver"). A restart
  does not change that.

  WITH NUSB 3.3 OR 3.6 INSTALLED, storage works.

  THE FIVE FILES CAN ALSO BE INSTALLED ON THEIR OWN, without the rest of
  NUSB: point the Add New Hardware Wizard, or Update Driver on the device,
  at a folder holding them. ON THE FIRST STORAGE DEVICE, ONE STEP IS NOT
  OBVIOUS. Windows gives the new disk its own generic "Disk drive" before it
  has seen USBNTMAP.INF, and no drive letter appears. Then:

    1. Device Manager -> Disk drives -> "Disk drive" -> Driver -> Update
       Driver, pointed at the same folder. It installs "USB Disk".
    2. Unplug the device and plug it back in.

  After that every storage device, USB stick or UAS disk, works with no
  extra step.

On Windows ME, Windows' own storage files serve USB sticks: they are copied
from the Windows ME CABs when the first stick installs. UAS disks need the
same files: a UAS disk plugged in before any ordinary stick shows Code 2.
Plug in any ordinary USB stick once, then unplug the UAS disk and plug it
back in.


==============================================================================
 4. INSTALL
==============================================================================

INSTALL FROM THE RELEASE DIRECTORY:

      {RELEASEDIR}\

This package carries BOTH builds side by side, RELEASE and DEBUG, each a
complete set of files with the same names, so the directory you point
Windows at is what decides which drivers you get. RELEASE is the one you
want. DEBUG ({DEBUGDIR}\) is the same drivers built
so that a crash on them can be traced further back. It records nothing more
than RELEASE does, and it is there only for troubleshooting a machine that
has already gone wrong. It prints nothing as it runs. Section 8 describes
both, and nothing about a copied file says which one it is - so point at a
directory, never at a loose .sys file.{ARCHNOTE}

Put the whole unzipped package somewhere the machine can read - a floppy, a
CD, a shared folder - then:

  WINDOWS 98 SE
      If you want USB storage, install NUSB 3.3 or 3.6 first (section 3).
      Then open Device Manager and find the unrecognised xHCI controller:
      it sits unclaimed with a yellow mark, usually under "Other devices".
      Then
          Properties -> Driver -> Update Driver -> Specify a location
      and point it at the RELEASE-X86\ directory. "Insert Disk" may ask for
      the Windows 98 Second Edition CD-ROM (section 3). Restart when asked.

      (If Windows finds the controller for you first, the Add New Hardware
      Wizard asks the same question - give it the same directory.)

  WINDOWS ME
      SweetLow's USB 2.0 stack first (section 2). Then the same Device
      Manager route as Windows 98 SE, pointed at the RELEASE-X86\
      directory, and restart when asked. Windows ME has only been run in a
      virtual machine.

  WINDOWS 2000 SP4 AND WINDOWS XP (32-BIT)
      Open Device Manager and find the unrecognised xHCI controller, then
          Properties -> Driver -> Update Driver -> Have Disk
      and point it at the RELEASE-X86\ directory. On Windows XP choose
      "Continue Anyway" at the unsigned-driver warning. Nothing else is
      asked for. Windows XP has only been run in a virtual machine.

  WINDOWS VISTA AND WINDOWS 7 (32-BIT)
      Open Device Manager and find the unrecognised xHCI controller, then
          Update Driver Software -> Browse my computer for driver software
      and point it at the RELEASE-X86\ directory. If Windows warns that it
      cannot verify the publisher, install the driver anyway. Use Device
      Manager, not a right-click on the INF. RESTART WHEN ASKED: the install
      and every update ask for one, for xhcimsi.sys, and after a first
      install the controller does not start until then (Device Manager
      shows code 28 on 7, code 1 on Vista meanwhile). A reinstall (the
      controller removed in Device Manager and found again) needs the
      restart too, even if Windows does not ask: 32-bit Vista showed only
      an "installed successfully" balloon, and the controller stayed at
      code 1 until a restart. Both have only been run in virtual
      machines.{ARCHINSTALL}

It installs as "xHCI98 USB 3.x eXtensible Host Controller", with "xHCI98
USB 3.x Root Hub" underneath it. Neither should carry a warning mark.

THE UAS DRIVER installs the first time a UAS disk is plugged in. On
Windows 98 SE, ME, 2000, XP and Vista a New Hardware wizard asks for its
driver: point it at the same directory. Vista's wizard names the disk by
its own product name; "xHCI98 USB Attached SCSI Storage" is its name once
installed. Windows 7 shows no wizard: it searches Windows Update, then
says the device's driver was not installed. Install it from Device
Manager instead, on the UAS disk's entry, which carries a warning mark:
    Update Driver Software -> Browse my computer for driver software
and point it at the same directory. No restart is needed.

WINDOWS 2000 OR XP SETUP, OR THE RECOVERY CONSOLE, ON AN xHCI-ONLY MACHINE
..........................................................................

When the keyboard or the install medium is on the xHCI controller, Setup
can load this driver at its F6 prompt. Copy the files of RELEASE-X86\ (or
RELEASE-X64\ for Windows XP x64) to the root of a floppy disk - it holds
txtsetup.oem, xhci98.sys and xhci98.inf, which is all F6 reads. Start
Setup, press F6 when "Press F6 if you need to install a third party SCSI
or RAID driver" shows, press S at the next screen, insert the floppy, and
pick "xHCI98 USB 3.x Host Controller". Setup then uses its own keyboard,
mouse and USB storage drivers above it.

The same floppy serves the RECOVERY CONSOLE of Windows 2000 and XP: press
F6 as for an install, then R at Setup's Welcome screen (on Windows 2000,
then C for the console). The USB keyboard logs in and types commands. The
console runs in text mode throughout, so the Windows XP restriction below
does not touch it.

  - Pressing F6 itself needs the firmware's own USB keyboard support.
  - The floppy must be drive A: as the firmware sees it.
  - A USB disk that supports ONLY UAS is not usable during Setup. One
    that also supports the older Bulk-Only mode, as most USB 3
    enclosures, SSDs and sticks do, runs as Bulk-Only during Setup and
    works, as do Bulk-Only flash sticks.
  - Installing Windows ONTO a USB disk is not supported.
  - A USB DRIVE PRESENT AT THE PARTITION SCREEN TAKES THE LETTER C:.
    Unplug the USB drives you do not need, or Windows installs to the next
    letter.
  - On Windows 2000, plug the USB keyboard and the USB stick in BEFORE
    Setup starts. Its text-mode Setup uses only the USB devices present
    when the driver first reports them; one plugged in later stays unused
    there. The driver waits up to 5 seconds for them (section 9,
    XhciFirstEnumWaitMs).
  - Later in Setup, Windows installs the driver again from xhci98.inf and
    may ask for the floppy or the Windows CD.
  - INSTALLING WINDOWS XP NEEDS A PS/2 KEYBOARD, OR A LAPTOP'S BUILT-IN
    ONE, LATER IN SETUP. Setup copies Windows' own HID and USB helper files
    only with Microsoft's own USB controller drivers, so the USB keyboard
    and mouse do nothing until the "Installing Devices" step installs them
    from the CD - and that step first asks about this unsigned driver,
    default No. A machine whose only keyboard is USB cannot answer, and
    Setup stops. A laptop's built-in keyboard (connected inside as PS/2) or
    any PS/2 keyboard answers it; afterwards the USB keyboard and mouse
    work. This package cannot carry those files. Windows 2000 does not ask.

  What has run in virtual machines: text mode with a USB keyboard and a
  USB stick on Windows 2000 and XP, and the Recovery Console on both,
  logged in and running commands with the USB keyboard alone; and
  Windows XP and XP x64 installed to the desktop this way. On real
  hardware: Windows XP x64 Setup started from a USB stick on a Dell
  Latitude E7250's xHCI controller, with this driver integrated into the
  install, ran to the desktop on this release's code. Nothing else of
  this has run on real hardware. A repair install, Windows 2000's
  Emergency Repair Disk and Windows XP's Automated System Recovery were
  not tried.

UPDATING FROM AN EARLIER 2.x RELEASE
....................................

Install over it with Update Driver on "xHCI98 USB 3.x eXtensible Host
Controller", then on "xHCI98 USB 3.x Root Hub", both pointed at the same
directory as a new install. Nothing needs renaming first. As read in
virtual machines:

  WINDOWS 98 SE AND ME: RESTART AFTERWARDS, ALTHOUGH WINDOWS DOES NOT ASK.
  The new file waits to replace the old one at the next start, and until
  then the earlier release keeps running. Under SweetLow's stack the
  controller showed a problem for a minute or two after Finish; on ME it
  shows one until the restart, while the devices keep working.

  WINDOWS 2000: USE HAVE DISK. Letting Windows search answers that a
  suitable driver is already installed and keeps the earlier release. Use
  "Display a list of the known drivers" -> Have Disk, as below.

  WINDOWS XP, XP X64, AND 64-BIT VISTA AND 7: the update took effect at
  once, with no restart. (Read with a command-line driver update rather
  than Device Manager.)

  WINDOWS VISTA AND 7: USE HAVE DISK. Browse my computer with the folder
  typed in answers that the best driver is already installed and keeps
  the earlier release. Use Browse my computer for driver software ->
  "Let me pick from a list of device drivers on my computer" -> Have Disk,
  pointed at the same directory. (Read on 32-bit Vista and 7.)

  32-BIT WINDOWS VISTA AND 7: RESTART WHEN ASKED. Every update asks for
  one, for the helper xhcimsi.sys (above). Until then the earlier release
  keeps running and Device Manager says the device needs a restart.

  GOING BACK TO AN OLDER RELEASE ON VISTA OR 7: the controller's request
  for a message-signalled interrupt stays in its hardware key, and an
  older driver does not know what to do with one. First uninstall the
  controller in Device Manager with "Delete the driver software for this
  device" ticked, or set MSISupported to 0 in its hardware key, under
  Device Parameters, Interrupt Management,
  MessageSignaledInterruptProperties. That tick removes only the package
  in use: older xhci98 packages stay in the driver store, and Windows may
  install one of them the next time it finds the controller. From an
  administrator's prompt pnputil -e lists the store; remove each
  oemNN.inf whose provider is "Yeo Kheng Meng" and class "Universal Serial
  Bus controllers" with pnputil -d oemNN.inf. The downgrade itself has not
  been read.

  THE ROOT HUB'S DRIVER TAB STILL SHOWS THE EARLIER VERSION (on 98 SE and
  ME, its date), although it runs the new file. To change it, run Update
  Driver on "xHCI98 USB 3.x Root Hub" too: on Vista and 7, "Let me pick
  from a list of device drivers on my computer" and the new entry;
  elsewhere Have Disk.

  From the first 2.x release only, each device is then found once more as
  new hardware, exactly once: a stick under its serial number, the other
  devices under a new id. On the NT systems this needs no answer; on
  Windows 98 SE the wizard runs for each and may ask for the CD for
  hidclass.sys. Let Windows install them. A hidusbf setting on such a
  device has to be applied again (section 5).


==============================================================================
 5. USING IT
==============================================================================

Plug devices in and they are found and installed the usual way. Keyboards,
mice, flash drives, USB disks, USB Ethernet adapters, audio devices and hubs
all work through the system's own drivers.

Things specific to this driver, worth knowing in advance:

  * USB DISKS GET UAS WHERE THEY OFFER IT. A disk that can do UAS gets
    xhciuas.sys; anything else gets Windows' own Bulk-Only storage driver,
    at whatever speed it connects. A disk that offers both can be kept on
    Bulk-Only (XhciForceBulkOnly, section 9).

  * DEVICE MANAGER SHOWS A SUPERSPEED DEVICE AS HIGH SPEED AT MOST. The
    interface it reads on these systems predates SuperSpeed, so that display
    says nothing about the real link. XHCISNAP's report (section 6) shows the
    speed the driver actually uses.

  * HUBS HAVE ENTRIES OF THEIR OWN IN DEVICE MANAGER. A hub appears as
    "xHCI98 USB Hub", with the devices behind it beneath it. A USB 3 hub
    appears twice, the second time as "xHCI98 USB 3.x Hub" for its
    SuperSpeed half. Each hub has a Power tab. A hub installs from the
    driver already installed: on Windows 98 SE and 2000 with nothing to
    answer, on Windows XP with the Found New Hardware wizard and the
    unsigned-driver warning (Continue Anyway) for each newly plugged hub.
    Disabling and enabling a hub in Device Manager brings back the devices
    behind it (read on 98 SE, 2000, XP). While a drive behind the hub is
    open in Explorer, Windows refuses the disable, asks for a restart and
    leaves the hub running; close the window first (read on Vista). The
    driver still runs every hub itself.

  * A DEVICE WITH A SERIAL NUMBER KEEPS ITS ENTRY ON ANY PORT. Moved to
    another port or behind a hub, it is not found again as new hardware.
    A device without a serial number is known by its port, as under
    Microsoft's own hub driver, and moved elsewhere it is found again.

  * DEVICES NO WINDOWS INF NAMES ARE LISTED UNDER THEIR OWN PRODUCT NAMES,
    in the Add New Hardware wizard and in Device Manager, instead of "USB
    Device". Where one of Windows' own INFs names a device (a mouse, a
    keyboard, a USB stick), that name shows, as over Microsoft's own
    stack. On Windows 98 SE and ME a character outside plain ASCII shows
    as '?'.

  * SWEETLOW'S HIDUSBF sets a mouse's polling rate, and this driver
    programs the rate it sets, up to 1000 Hz for a Low- or Full-Speed
    device (more only with XhciFastPollFsLs, section 9). Read in virtual
    machines at a root port and behind a hub on Windows 98 SE under NUSB
    and on ME, and at a root port on XP and on a stock Windows 98 SE. It
    works behind a hub on XP too (read). Its setting is
    kept on the device's Device Manager entry, so it has to be applied
    again whenever the device is found as new hardware.

  * IDLE DEVICES ARE NOT PUT TO SLEEP UNLESS YOU TURN IT ON. With
    XhciSelectiveSuspend at 1 (section 9) a device is suspended when
    Windows asks; hubs, the root hub and the controller never are, and the
    driver never suspends anything on a timer of its own. Off, the
    default, an idle device draws its normal power.

  * A COMPOSITE DEVICE HAS AN ENTRY OF ITS OWN in Device Manager, with its
    functions beneath it, and USBView shows a device's manufacturer,
    product, serial number and interface names.

  UPDATING xhciuas.sys OVER AN OLDER COPY
  .......................................

  When the new xhciuas.sys carries the same version as the one installed,
  "Search for a better driver" keeps the copy Windows already has. Instead:
  Update Driver on "xHCI98 USB Attached SCSI Storage" -> "Display a list of
  all the drivers in a specific location" -> Have Disk -> this package's
  directory.


==============================================================================
 6. IF SOMETHING GOES WRONG
==============================================================================

  REPORT IT ON GITHUB
  ...................

  Open an issue on the project's GitHub page (section 7) and describe what
  happened. The bug report form asks for two things these batch files
  gather; installing and using the driver never needs them.

  IF THE MACHINE KEEPS RUNNING: SNAPON, SNAPGET, SNAPOFF

  TOOLS\XHCISNAP holds XHCISNAP.EXE and three batch files that run it. It
  reads the driver's own log straight out of the running machine and writes
  a report you can attach to that issue. On Windows 98 and Windows ME it is
  the ONLY way to get anything out. Four steps, and none of them is REGEDIT.
  Copy the directory somewhere writable first (not a CD), and on Vista and
  7 right-click each batch file and choose Run as administrator:

      1. SNAPON.BAT                 turns the log channel on
      2. restart Windows
      3. make the problem happen again
      4. SNAPGET.BAT                writes the report and the dump

  SNAPGET.BAT writes its files in the directory it runs in, not in C:\,
  under a name made from the date and time - for example 3141022A.DEV
  and 3141022A.TXT for 14 March at 10:22 (the month is one character, 1 to
  9 then A, B, C) - and a second run never overwrites the first. It prints
  the names. Attach the .DEV file (the controller's Device Manager status
  and problem code, its resources, and the versions and SHA-256 hashes of
  the installed driver files) and the .TXT file (the report). Keep the .BIN
  and .PSC beside them and attach those only if you are asked for them. It
  leaves the channel on, so you can run it again after the next attempt.
  Step 1 finds the right registry key for you, on every controller this
  driver runs - see section 9 for what it sets.

  Two of the batch files take one optional word, typed after the name at a
  prompt in their directory (on Vista and 7, an administrator's prompt):

      SNAPON 3      level 3 instead of 2 (1 to 4; with none, 2, the log -
                    use it unless you are asked for another). SNAPON 0 is
                    refused: 0 is off, and SNAPOFF.BAT turns it off.
      SNAPGET MYPC  saves the files as MYPC.DEV, MYPC.TXT, .BIN and .PSC
                    instead of the date-and-time name: 1 to 8 letters,
                    digits, _ or -, not starting with -, no path, drive or
                    extension, and not a DOS device name (CON, PRN, AUX,
                    NUL, COM0 to COM9, LPT0 to LPT9), in the same
                    directory. A name already taken there is refused;
                    nothing is overwritten.

  The batch files pass the word on to XHCISNAP as it came (SNAPON runs
  XHCISNAP -on LEVEL, SNAPGET XHCISNAP -report NAME), and XHCISNAP checks
  it: a word it does not take changes nothing and writes nothing.

  Each batch file runs XHCISNAP last, and XHCISNAP says what happened and
  what to do next. Its last line, just above "Press any key", names the
  files to attach (for example "Attach: 3141022A.DEV 3141022A.TXT"), or
  says there is nothing to attach. What it says when SNAPGET.BAT stops
  follows XHCISNAP -report's exit code:

      0  the .DEV and the .TXT were written in full: attach them.
      2  the name given was refused, and the line above says why: give
         a name by the rules above, or no name. (If XHCISNAP printed its
         usage instead, it is older than the batch: use this package's.)
      3  the .TXT is not complete: attach it, and say so.
      4  no dump was taken, and the .DEV says why - often the channel is
         off: SNAPON.BAT, restart, make it happen, SNAPGET.BAT. Attach the
         .DEV.
      5  the .DEV could not be written in full: the disk is full or cannot
         be written. Copy the directory somewhere writable, run it there.
      6  no free name: all 26 for this minute are taken (wait a minute and
         run it again), or the name given is taken (give another, or
         none). Nothing was written or overwritten.
      7  the clock is not a valid date and time: set it, run it again.
         Nothing was written.
      8  another run made a file of this set while it ran. That file was
         left as it was and no dump was kept under the name: run
         SNAPGET.BAT again - it takes the next letter, or give another
         name.

  Without the batch files the same steps are:

      1. XHCISNAP -verbosity 2
      2. restart Windows
      3. make the problem happen again
      4. XHCISNAP -report           (or XHCISNAP -o NAME for the dump alone)

  STEP 2 IS NOT OPTIONAL. The driver reads that setting when it starts.
  Without the restart the driver is still at whatever it read last time -
  which on a fresh install is OFF, and then XHCISNAP gets no answer at all
  rather than an empty one.

  If nothing comes back at all, run XHCISNAP -probe. It checks the route to
  the driver separately from whether this driver answers on it.

  USE THE XHCISNAP.EXE FROM THIS PACKAGE, not a copy kept from an earlier
  release. This driver's report is snapshot schema 5.

  XHCISNAP.EXE changes nothing about how the driver behaves on the bus, and
  writes no file it was not asked to. It does READ the controller's port
  registers, which is a hardware access - it just does not write one.

  What step 1 DOES change is ONE of this driver's own settings, and it says
  so as it writes it - that is the point of it, and it is why step 2 is a
  restart. SNAPOFF.BAT (XHCISNAP -disable) puts it back, and you should run
  that once you have sent the capture: while it is on, anyone using this
  machine can read the driver's diagnostic state. See section 9.

  IF THE MACHINE WILL NOT START WINDOWS, OR CRASHES: QUALREP UNDER DOS

  TOOLS\XHCIQUAL\QUALREP.BAT runs XHCIQUAL's read-only probe under real
  MS-DOS (not a DOS box inside Windows; see step 1) and saves it to a new
  .LOG in the directory it runs in, named from the DOS date and time the
  same way. Run it from writable storage that is not on the controller
  being tested, and attach the .LOG file it names. Without the batch file:
  XHCIQUAL --probe-only --no-page --log-stamp

  WHAT THE LOG CAN AND CANNOT ANSWER

  It answers "the device does not work" and "transfers are wrong". It answers
  NOTHING ABOUT A CRASH - a machine that has crashed is not running for
  anything to read.

  THE DRIVER WRITES NO LOG FILE ITSELF, and there is no registry value that
  makes it. XHCISNAP writes the file, named from the clock or by you.

  DEBUGVIEW (Sysinternals), with "Capture Kernel" switched on, receives this
  driver's log as it runs if XhciLogDebugView is set. LIKE XHCISNAP, USE IT
  ONLY WHEN THE MAINTAINER ASKS FOR IT.

      !! Do not run DebugView on Windows 98 on real hardware while
         capturing. Under the earlier releases, plugging in a device while
         it captured crashed the machine, and this driver has not been
         run under DebugView on Windows 98 hardware. Inside a virtual
         machine it is fine. You do not need DebugView to send a report.


==============================================================================
 7. KNOWN LIMITATIONS
==============================================================================

The full measured list is in the project's docs/using/release-notes.md,
together with what has and has not been tested on real hardware as opposed
to in a virtual machine. Read it before reporting a problem, and then report
it anyway if it is not there - the reports are what fix it:

      https://github.com/yeokm1/xhci98/issues

Outside this driver's control - these come from Windows, NUSB or the driver
being unsigned, and no change to this driver can remove them:

  * WINDOWS VISTA X64 AND 7 X64 NEED TEST-SIGNING MODE, OR DRIVER
    SIGNATURE ENFORCEMENT DISABLED. The x64 drivers carry this project's
    own test signature, not Microsoft's. Turn on test-signing mode once
    (section 4), which shows "Test Mode" on the desktop, or disable driver
    signature enforcement at every start (F8); otherwise the controller
    sits at Code 39 on Vista, Code 52 on 7.

  * NO USB STORAGE ON A STOCK WINDOWS 98 SE (section 3). With no NUSB
    installed there is no mass-storage driver at all. HID and audio still
    work.

  * WINDOWS ME: DO NOT UNPLUG A DEVICE WHILE WINDOWS IS INSTALLING IT. ME's
    own device manager stops responding; it does the same on Microsoft's
    own USB stack. Wait for the install to finish before unplugging.

  * A SUPERSPEED DEVICE DECLARING MORE THAN 510 mA READS 510 mA on the
    Power tab (a bus-powered hub is charged (ports + 1) x 100 mA, at most
    500 mA, instead): the value the page reads cannot count past 510 mA. With
    XhciLogVerbosity at 2, XHCISNAP's report gives the exact figure as
    dev.ss.maxpower.ma, in hexadecimal mA.

  * INTEL 7-, 8- AND 9-SERIES CHIPSETS: A DEVICE ON A BLUE CONNECTOR MOVES
    BETWEEN THE USB 2.0 CONTROLLER AND THIS DRIVER when this driver's
    controller starts or stops, so a drive busy there is disconnected.
    Only with the switchover on (XhciIntelPortSwitch 1 or 2); it is off
    by default (section 9).

  * WINDOWS XP FROM THE F6 FLOPPY: GUI-MODE SETUP ASKS ABOUT THE UNSIGNED
    DRIVER BEFORE THE USB KEYBOARD WORKS. A PS/2 or built-in laptop
    keyboard answers it (section 4).

  * WINDOWS VISTA AND 7: DELETING THE DRIVER KEEPS THE OLDER PACKAGES.
    "Delete the driver software for this device" removes only the package
    in use, and Windows may install an older one the next time it finds
    the controller. Section 4, "Going back to an older release", says how
    to remove them with pnputil.

May be addressed in a later release:

  * SELECTIVE SUSPEND IS OFF UNLESS YOU TURN IT ON, AND HUBS ARE NEVER
    SUSPENDED (sections 5 and 9).

  * 32-BIT WINDOWS VISTA AND 7: THE INSTALL ASKS FOR A RESTART, and the
    controller does not start until it is done, because the helper
    xhcimsi.sys starts only at boot (section 4). A reinstall needs the
    restart even if Windows does not ask for it: 32-bit Vista did not.

  * WINDOWS ME: A UAS DRIVE AS THE FIRST USB STORAGE DEVICE SHOWS CODE 2.
    ME has not yet copied its own USBNTMAP.SYS and USBMPHLP.PDR, which it
    installs only when its first ordinary USB stick is plugged in. Plug in
    any ordinary USB stick once, then unplug the UAS drive and plug it back
    in. No Remove and no restart are needed.

  * WINDOWS VISTA AND 7: THE CONTROLLER'S ADVANCED TAB SHOWS NO BANDWIDTH.
    The figure comes from a query this driver does not answer.

  * WINDOWS 98 SE AND ME SHOW A DEVICE NAME'S NON-ASCII CHARACTERS AS '?'.

  * A DEVICE THE CONTROLLER FAULTS ON IS RE-ENUMERATED BY THE DRIVER, as if
    unplugged and plugged in again (XhciTolerance, section 9). Whatever it
    was doing ends as on an unplug: a copy to a drive fails part way. After
    three on one port, the next fault there removes the device and holds
    the port until the device is unplugged, or, held unpowered after
    over-currents, until the controller restarts. A recovery of the
    controller itself re-enumerates every device on it the same way, so a
    copy running across it fails.

  * DISABLING A CONTROLLER THE DRIVER CLOSED OFF WITHOUT PROOF THAT IT
    STOPPED HANGS: RESTART THE MACHINE. Only after the controller stopped
    answering altogether, or a recovery could not stop it, and the driver
    could not prove it had stopped writing to memory, so kept everything it
    might still write into (XHCISNAP reads "CONTAINED" and "PINNED"): a
    later disable of the controller in Device Manager, or an update or
    removal of its driver, waits for ever on the kept transfers, and only
    restarting the machine ends it. Seen in virtual machines on Windows 98
    SE and 2000 with the fault made on purpose. Restart the machine
    instead.

  * WINDOWS 2000: A MOUNTED USB DRIVE MAY COME BACK AT CODE 31 AFTER A
    CONTROLLER RECOVERY OR A RESUME FROM HIBERNATION. After the driver
    recovers the controller in place (a halted controller, or a failed
    endpoint reset), or after a hibernation on a controller that lost its
    state, where the driver finds every device again, a USB drive whose
    volume was mounted may stay at Code 31 until it is unplugged and
    plugged in again, or the controller is disabled and enabled in Device
    Manager; a restart of the drive's own device also cleared it. Other
    devices come back. Seen in virtual machines, with the controller
    polled as well; not on Windows 98 SE.

Untested ground:

  * SUPERSPEED ISOCHRONOUS TRANSFERS. Built from the specification. No
    SuperSpeed isochronous device has been held and QEMU models none.

  * SUPERSPEEDPLUS (USB 3.1 GEN 2, USB 3.2 GEN 1X2 AND GEN 2X2). Accepted at
    its trained rate, built from the specification. No Gen 2 device has been
    tested, so every mode is untested.

  * A UAS-ONLY DRIVE AT SUPERSPEED ON A CONTROLLER WITHOUT STREAMS. It is
    sent back to its USB 2.0 port and runs UAS at High Speed, or is refused
    if it has no USB 2.0 port. Built from the specification; no such
    controller has been held.

  * POLLING ABOVE 1000 HZ (XhciFastPollFsLs, section 9). Outside the xHCI
    specification, and read on no real controller; value 4's programming
    was read in virtual machines only.

  * STAND BY ON WINDOWS 98 SE. Read only in QEMU virtual machines, where
    Windows 98 SE itself does not resume reliably, with no USB controller
    as well. With a USB disk attached the resume stopped before the
    desktop under this driver and under NUSB's EHCI driver alike; with a
    USB mouse alone, or behind a hub, it completed and the mouse worked.
    Untested on real hardware: REMOVE USB DISKS BEFORE STAND BY.

  * LOW- AND FULL-SPEED DEVICES BEHIND A BUILT-IN HUB WHOSE USB 2.0 SPEED
    TABLE LISTS ONLY HIGH SPEED, as on the VIA VL805
    (XhciUsb2HubSpeedFallback, section 9). Built from the specification
    and never run on such a controller.

  * XhciIntelPortSwitch AT 2 (section 9). Read on no unlisted Intel
    controller, and at your own risk.

  * THE CONTROLLER-FAULT HANDLING ON REAL HARDWARE (XhciTolerance, section
    9). Each fault was made on purpose at a root port in QEMU virtual
    machines, under Windows 98 SE and Windows 2000; QEMU has no
    SuperSpeed hub, so nothing was read behind one. No real controller has
    raised one under this driver yet. XhciIntervalCap and XhciAvgTrbEsit
    have been read on no AMD or other real controller.


==============================================================================
 8. WHAT IS IN THIS DIRECTORY
==============================================================================

{CONTENTS}
Both drivers are called xhci98.sys and xhciuas.sys in every directory, and
every copy carries driver version {VERSION}, so a copy taken out of its
directory cannot be identified by name or by version. (The -X86 directories
also hold xhcimsi.sys, the 32-bit Vista and 7 helper, which has no 64-bit
twin.)
{TELLAPART}

(In Windows driver-kit terms, RELEASE is what the DDK calls a "free" build
and DEBUG is what it calls a "checked" build. This project says release and
debug throughout, in its build scripts and its documentation alike.)


==============================================================================
 9. REGISTRY SETTINGS
==============================================================================

Every value this driver reads from the controller's driver key. There are
nineteen, all DWORDs, and the install writes all nineteen, each at the
default it states below except XhciImodInterval250ns, which it writes as
160, and XhciForceBulkOnly, which it writes as 0 where absent counts as
1, so every one is already in the key to change. The install writes
each only where it is missing, so a value you changed survives an
install or update; to go back to the default, set it by hand, or delete
the value and update the driver. If an earlier install left
XhciImodInterval250ns at 500, the update keeps it; set it to 160 by hand.
A value that is absent means the default each one states.

  YOU SHOULD NOT NEED THIS SECTION FOR A LOG. If the maintainer asks for one,
  XHCISNAP -verbosity 2 sets the value that matters, on every controller, and
  finds the key itself.

  XhciLogVerbosity  -  the whole log switch
  .........................................

  Default 0. Level 0 is off outright; above it each level is the one below
  plus one thing:

      0   OFF. The driver does not answer XHCISNAP at all. This is what a
          fresh install does.
      1   the channel, plus the counters. The log of what happened is still
          off, so this is the cheapest reading there is.
      2   plus the log of what happened.  USE THIS ONE.
      3   plus the USB port register table.  Still no internal addresses.
      4   plus everything, including internal addresses. Only if asked -
          it is more than you would want to paste in public.

  A value outside 0-4 is REFUSED rather than treated as the nearest one: the
  driver falls back to 0, which is off.

  XhciLogDebugView  -  the log to a capture tool
  ..............................................

  Default 0. Set it to 1 to have the log handed to DebugView as the driver
  runs, and once more when the controller stops. It does not affect what
  XHCISNAP reads. Leave it at 0 unless the maintainer asks for a DebugView
  capture, and see section 6 about DebugView on Windows 98.

  XhciImodInterval250ns  -  how long the controller holds back an interrupt
  .........................................................................

  Counted in UNITS OF 250 NANOSECONDS. It sets how long the controller
  waits after one interrupt before raising the next. A shorter interval
  makes USB storage faster at the cost of more interrupts.

      160    written by the install: 40 microseconds, at most 25,000
             interrupts a second. The value Linux uses.
      4000   used when the value is MISSING, UNREADABLE, OR OUTSIDE
             10-4000: 1 ms, at most 1,000 a second.
      10     the lowest accepted: 2.5 microseconds, at most 400,000 a
             second.

  A value outside 10-4000 is REPLACED BY 4000, not rounded to the nearest
  limit, so a mistyped 0 cannot turn moderation off. 4000 is the
  controller's own power-on value.

  On a ThinkPad P14s Gen 1 under Windows 98 SE, a UAS flash drive at
  SuperSpeed read about 221 MB/s and wrote about 211 MB/s at 8 MB transfers
  at 160, against about 181 MB/s at 500, the value the earlier releases
  wrote. 40 added only 1 to 3% more.

  FEEL FREE TO TUNE IT. Raise it towards 4000 (or delete it) if you get
  audio stutter or instability under load. Enter it as a decimal DWORD.
  The driver reads it when it starts, so a change takes effect after a
  restart; XHCISNAP's report then shows under "registry values" the value
  it read, the interval in force, and what the controller took.

  XhciForceBulkOnly  -  keep storage on Bulk-Only instead of UAS
  ..............................................................

  The install writes 0: a disk that offers UAS gets UAS. Set it to 1 and
  every disk on that controller that offers BOTH transports gets
  Bulk-Only, Windows' own storage driver, instead. Absent counts as 1:
  that is text-mode Setup, before the INF has run, where Windows' own
  storage driver is the only one. A UAS-only disk stays on UAS whatever
  this says.

  It is read each time a device is plugged in, so unplug the disk and plug
  it back in after changing it. On Windows 2000 and later a disk already
  installed keeps its driver until you uninstall it in Device Manager and
  plug it back in.

  XhciFastPollFsLs  -  Low- and Full-Speed polling above 1000 Hz
  ..............................................................

  Default 0 (absent): off. For a mouse on a ROOT PORT that hidusbf has set
  to its "31 Hz" or "62 Hz" rate:

      2      "31 Hz" becomes 2000 Hz, "62 Hz" becomes 4000 Hz
      3      "31 Hz" becomes 4000 Hz, "62 Hz" becomes 8000 Hz

  4 is described below; any other value is off. With 2 or 3 a device
  behind a hub keeps its normal rate.

  THIS IS OUTSIDE THE xHCI SPECIFICATION, which sets 1 ms as the shortest
  interval for these devices. With 2 or 3, a controller that refuses it is
  caught: the device runs at its normal rate, and XHCISNAP's report counts
  it as fastpoll.fallbacks. A controller that accepts it and then misbehaves
  cannot be caught; if anything misbehaves, delete the value and restart.
  While 2 or 3 is set, ANY Low- or Full-Speed device on a root port that
  asks for 16 to 63 ms is polled faster too. Read when the controller
  starts, so restart after changing it. The faster rates of 2 and 3 have
  been measured on no real controller and in no virtual machine; in
  virtual machines 2 and 3 were read only ignoring value 4's codes.

  Set to 4, it does something else: the rate the client driver chooses.
  An interrupt endpoint whose polling interval in the configuration
  Windows selects is 128, 129 or 130 is polled at 2000, 4000 or 8000 Hz;
  every other interval is polled as the specification reads it, so 4 does
  not speed up 16 to 63 ms, and 2 and 3 do not honour these codes. It
  applies to a Low- or Full-Speed device on a root port only, and to a
  High- or SuperSpeed device at a root port or behind a hub. Set the value
  first and restart, then have hidusbf (or another filter) write 128, 129
  or 130. A device that itself asks for 128 to 130 ms is polled that fast
  too while it is 4; a code written while the value is not 4 means what
  the number says (128 ms, or 4.096 s at High Speed and SuperSpeed; 32 ms
  instead where XhciIntervalCap applies). A controller that refuses a code
  is caught and the device polled every 1 ms. Value 4's programming was
  read in virtual machines only.

  XhciFirstEnumWaitMs, XhciFirstEnumPortMs  -  the first report's wait
  ....................................................................

  When the root hub or a hub first reports its devices after it starts,
  the driver waits for the devices already plugged in to be ready, so
  that they are in that first report (Windows 2000's Setup uses only
  those, section 4). The wait ends as soon as they are ready.

      XhciFirstEnumWaitMs   the longest wait, in milliseconds. Default
                            5000; 0 turns the wait off; above 30000 is
                            held to 30000.
      XhciFirstEnumPortMs   the longest one port may hold it. Default
                            2000, held to the total; a slower device is
                            reported later instead. 0 sets no limit
                            per port.

  In virtual machines, with the defaults, the first report went 20 to 30
  ms after the start with nothing plugged in, and 0.3 to 0.9 s after it
  with a mouse and a stick plugged in.

  XhciIntelPortSwitch  -  the Intel 7/8/9-series port switchover
  ..............................................................

  Default 0: off. Set to 1, it is on for an Intel xHCI with PCI device id
  1E31, 8C31, 9C31, 8CB1, 9CB1 or 8D31 (7-, 8- and 9-series chipsets, Ivy
  Bridge to Broadwell, and C610/X99). It is read on every Intel controller
  and on no other vendor's. On those chipsets each switchable
  connector, usually a blue one, is wired to both the USB 2.0 (EHCI) and
  the xHCI controller, and firmware on "Auto", or with no setting,
  typically leaves it on the USB 2.0 one, where it works if a USB 2.0
  driver is loaded. At 1 this driver moves
  those connectors to itself at each start and resume, and hands them back
  when its controller stops and at shutdown.

  A device under a running USB 2.0 driver (NUSB's, for example) on such a
  connector is disconnected there when this driver starts and comes back
  under it, and the reverse at a stop: do not have a drive busy there then.

  At 0, absent or not a DWORD, the driver leaves the connectors where
  firmware put them; 1 or any other number but 2 is on. Earlier releases
  wrote 1, and an update KEEPS the value already there, so a machine
  updated from one of them stays at 1 until it is changed.

  PORTSW.VBS, in the TOOLS\PORTSW directory, shows and sets it on every
  xhci98 controller, with nothing to install, on every Windows this
  driver runs on. Double-click it, or, at a prompt in the TOOLS\PORTSW
  directory, run
      cscript //nologo portsw.vbs show     (or 1, or 0)
  as an administrator. Read when the controller starts, so RESTART
  WINDOWS after changing it. A stop gives back only the registers the
  driver itself wrote. Read on a Lenovo B490 (1E31) under Windows 98 SE
  with NUSB; standby and the other five ids are untested.

  PORTSW-ON.BAT and PORTSW-OFF.BAT, beside it, do the same in one
  double-click: each shows the value, sets 1 or 0 through PORTSW.VBS and
  says to restart. On Vista and 7, right-click one and choose Run as
  administrator. At a prompt on Windows 98 SE and ME, change to the
  TOOLS\PORTSW directory first. Then RESTART WINDOWS.

  On a 7-, 8- or 9-series machine, a USB 3 stick or drive that copies
  only at USB 2.0 speed on a blue connector is most likely still on the
  USB 2.0 controller, because the switchover is off by default. Run
  PORTSW-ON.BAT and restart Windows.

  Set to exactly 2, it does the same on ANY Intel xHCI controller, listed
  or not, for a chipset with these connectors that the list misses (on a
  listed one 2 is the same as 1). AT YOUR OWN RISK:

      Setting XhciIntelPortSwitch to 2 makes the driver read four Intel
      chipset registers and write two of them on any Intel USB 3
      controller, not only on the 7-, 8- and 9-series and C610/X99
      chipsets it was written for. On a controller that does not have
      those registers - every Intel chipset from the 100-series (Skylake)
      on, and any other whose layout has not been read - it writes
      registers of unknown meaning, at every start, resume, stop and
      shutdown, and the result is unknown. Use 2 only for an Intel chipset
      that has both an EHCI and an xHCI controller and that this driver
      does not list, and at your own risk. Everyone else should use 0
      or 1.

  Value 2 has been read on no unlisted Intel controller.

  XhciIntelIrqRoute  -  the Intel interrupt routing repair
  ..............................................................

  On the same Intel chipsets two chipset registers decide
  which interrupt line the xHCI controller's interrupt reaches, and some
  firmware points them at a different line from the one it tells Windows.
  The Dell Latitude E7250's sends it to IRQ 16 while Windows listens on IRQ
  21, so the driver never hears from the controller and every USB device
  on it is slow or stops. At each start and resume the driver reads those
  registers and, where this value allows it, points the interrupt at the
  line Windows assigned. It changes nothing else.

      1   the default, and the meaning of absent or any other value: only
          on the E7250 (controller 8086:9CB1, subsystem 1028:062D, chipset
          9CC3); elsewhere it reads and reports.
      2   on any of the six controllers listed under XhciIntelPortSwitch,
          each with one of its own chipset ids.
      0   reads and reports, writes nothing.

  IT DOES NOTHING on any other controller, NOTHING when the interrupt is a
  message (MSI) rather than a line, as it can be on Vista and 7, and
  NOTHING while the driver is polling the controller. On 2000, XP, Vista
  and 7 it also writes nothing when Windows gave the controller an
  interrupt below 16. XHCISNAP's report
  has an "interrupt routing" section saying what it found and did. The
  same register written by hand fixed the E7250 under XP x64; the
  driver's own write has not yet been read on any machine.

  XhciTolerance  -  the controller tolerance, all together
  ........................................................

  Default 1: on. The driver recovers by itself from transfer, port and
  controller faults that used to leave a device dead until it was
  replugged: an interrupt that never arrived, a USB transaction error on a
  bulk or interrupt endpoint (retried up to three times, except for a Low-
  or Full-Speed device behind a High-Speed hub's transaction translator - a
  mouse or a keyboard in a USB 2.0 hub running at High Speed - on UAS
  streams and on four controllers), a fault it cannot pin on a transfer
  (the device is re-enumerated, as if unplugged and plugged in again), a
  USB 2.0 root port the controller disabled or an over-current (the port
  re-enumerated or powered again), a controller that halts (recovered at
  most three times in a window of at least ten minutes) or stops answering
  (closed off). Each but the lost interrupt, which is picked up every time,
  a few times at most: after three on one port, the next fault removes the
  device and holds the port until its device is unplugged for a few
  seconds, or, held unpowered, until the controller restarts. While no
  fault is reported none of it acts. A fault need not mean an unhealthy
  controller: a healthy one reports a device's missing handshake, a CRC
  failure or a timeout as a transaction error.

  Set it to 0 to turn all of that off together and have the earlier
  handling back, but for the rules below that apply at every value.
  ONLY 0 TURNS IT OFF; absent or any other number is on. It does not
  touch the next two values. At 0 XHCISNAP's report still counts the
  faults it sees and what the rules below did, while the counters of the
  behaviours switched off stay at 0. Read when the controller starts, so
  restart after changing it.

  These apply at every value, 0 included: a device the controller
  reports incompatible is removed and re-enumerated, as the xHCI
  specification asks, at most three times on one port before the port is
  held; and a recovery that cannot stop the controller, and cannot prove
  it has stopped writing to memory, closes the controller off until the
  machine restarts. XHCISNAP then reports "CONTAINED: halt and reset did
  not complete and Bus Master Enable would not clear; DMA not proven
  stopped, common buffer pinned". And on a controller latched failed (its
  recoveries refused or failed three times in a row): once it shows it
  has halted, the transfers it holds are completed and its devices
  removed; a resume from standby no longer brings it back, only a disable
  and enable or a restart does (at 0 a change from earlier releases); and its
  state is not saved or restored for standby.

  XhciIntervalCap  -  the AMD interrupt-interval cap
  ..................................................

  Default 1: on AMD controllers (PCI vendor 1022) only, an interrupt
  endpoint - a mouse, a keyboard, a hub - asking to be polled less often
  than every 32 ms is polled every 32 ms, as Linux does on some AMD
  controllers. Audio is never capped. Set it to 0 to turn it off, or to 2
  to apply it on every controller; absent or any other number is 1. Read
  when the controller starts. Read on no AMD controller yet.

  XhciAvgTrbEsit  -  a comparison switch
  ......................................

  Default 0. Set to 1 it tells the controller, for each interrupt
  endpoint, an average transfer size equal to the endpoint's largest
  transfer per interval, as Linux does, instead of the 1024 bytes the xHCI
  specification suggests. Any other value is 0. It exists so that a
  machine whose USB devices misbehave can be compared both ways; leave it
  at 0 otherwise. Read when the controller starts.

  XhciInterruptMode, XhciPollIdleMs, XhciPollActiveMs  -  how events arrive
  .........................................................................

  XhciInterruptMode, default 0: automatic. The interrupt line where it
  works; a controller with no interrupt pin, or whose line the driver sees
  raised and never delivered, is polled - the driver reads its events on a
  timer. On Vista and 7 a message-signalled interrupt is used where Windows
  grants one. 1 is the interrupt line only: a controller given no line -
  on Vista and 7 also one given a message instead - fails to start (Code
  10). 2 polls always, at the cost of more processor wake-ups. 3 is a
  message-signalled interrupt only: on Vista and 7 the driver asks for the
  message, and the controller fails to start (Code 10) if none is granted
  or it cannot be connected (on 32-bit, when xhcimsi.sys is not running);
  on 98 SE, ME, 2000 and XP, which have none, 3 always fails the start.
  Any other value is 0.
  Read when the controller starts: disable and enable it, or restart.

  While polling, XhciPollIdleMs (default 50, held to 5000; 0 is 50) is how
  often in ms the driver looks with nothing outstanding, and
  XhciPollActiveMs (default 1, held to the idle period; 0 is 1) how often
  with transfers outstanding - except that when only a mouse's or
  keyboard's periodic transfers are outstanding it looks at that
  endpoint's interval, held between the two. Windows runs the timer no
  faster than its clock tick, so 1 means as often as Windows allows.
  Polling adds up to one period to each transfer's completion. Neither
  value matters while the interrupt works.

  XhciMaxTransferKB  -  the UAS command size
  ..........................................

  Default 1024: the most one UAS command asks the drive to move, in KiB.
  Any multiple of 4 from 64 to 2048 is taken; anything else is 1024. 64 is
  what earlier releases always used - set it for a drive that misbehaves
  at large commands. Bulk-Only devices keep Windows' own limits, and on
  Windows 98 SE and ME Windows' mapping layer sends at most 64 KiB at a
  time whatever this says. Windows reads the size when the drive starts,
  so RESTART WINDOWS after changing it. An update keeps the number already
  in the key.

  ONE DRIVE MODEL CAN BE SET APART, on Windows 2000 and later, with the
  value Windows' own Bulk-Only driver reads: MaximumTransferLength, a
  DWORD in BYTES, under
    HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP
  (the drive's vendor and product IDs, four uppercase hex digits each).
  65535 to 2097152 is taken, rounded to 4 KiB, and REPLACES
  XhciMaxTransferKB for every drive of that model - so if you lower
  XhciMaxTransferKB to test a drive, change or delete that value too.
  Restart after setting it.

  XhciWideMap  -  two transfer pieces at once
  ...........................................

  Written as 0 by the install; 0 or absent is off. Set to 1 and restart
  Windows, and on Windows 2000 and later the driver gives a large bulk
  write on any device, and a large bulk read on a UAS drive's streams, to
  the controller two pieces at a time; a UAS drive at SuperSpeed may also
  take one command's data as a single piece of up to 2 MiB. It does so
  only where the machine's DMA needs no translation, which it checks at
  every start and for each transfer; under Driver Verifier's DMA checks,
  or where the DMA does need translation, every transfer goes one piece
  at a time as before. It is off by default because a fault, though
  unlikely, would show as wrong data rather than an error: if a large
  copy misbehaves with it on, set 0 and restart.

  XhciSelectiveSuspend  -  putting idle devices to sleep
  ......................................................

  Written as 0 by the install; 0, absent or any number but 1 is off, and
  the driver then suspends no device. Set to 1 and restart, and a USB 2.0
  or SuperSpeed device is suspended when Windows asks and woken when
  Windows asks again or the device signals a remote wake. Hubs, the root
  hub, a composite device's own entry and the controller are never
  suspended. Read in virtual machines only so far; leave it off unless
  you are testing it, and if a device misbehaves with it on, set 0.

  XhciUsb2HubSpeedFallback  -  Low- and Full-Speed IDs behind a hub
  .................................................................

  Written as 1 by the install; 1, absent or any number but 0 is on.

  Some controllers reach Low- and Full-Speed devices only through a USB
  2.0 hub built into the chip, and the speed table of the USB 2.0 port
  that hub hangs from names High Speed alone; the VIA VL805 (1106:3483) is
  one. Every mouse, keyboard and other Low- or Full-Speed device on such a
  controller sits behind that hub, and the driver must write a speed ID
  for each of them from that table. With this on, such a device gets the
  standard speed ID (Full Speed 1, Low Speed 2) when the table names
  neither that speed nor that number; a table that names them is
  followed, a controller with no table or one that lists Low and Full
  Speed is not affected, and a device on a root port of its own is never
  given one. It is on by default because without it every such device on
  such a controller is refused. Set 0 and restart to refuse them again.
  XHCISNAP shows whether it is on and counts the devices given an ID
  (Usb2HubSpeedFallbacks) and those still refused (TopoBehindHubNoSpeed).
  It was built without the hardware and has never run on a VL805.

  THOSE NINETEEN ARE THE WHOLE LIST. The earlier releases' XhciVirtualHSHub,
  XhciVirtualHSHubVid and XhciVirtualHSHubPid are not read: a copy left in
  the key by an earlier install has no effect, because this driver reports
  every device at its true speed with no virtual hub in the way. Delete
  them if you want them gone.

  WHERE THE KEY IS. The values live in the controller's own driver key,
  which is spelled one way on the NT systems and another on the 9x ones:

    Windows 2000, XP, Vista and 7
      HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\
        {36FC9E60-C465-11CF-8056-444553540000}\0002

    Windows 98 SE and Windows ME
      HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\USB\0002

  THE LAST PART OF THE PATH IS ASSIGNED BY THE MACHINE AND WILL NOT
  NECESSARILY BE 0002 ON YOURS. That is also why no ready-made .REG file
  ships here.

  DO NOT IDENTIFY THAT KEY BY ITS DESCRIPTION, AND DO NOT ASSUME THERE IS
  ONLY ONE. If the controller has ever been enumerated at more than one PCI
  slot, there is one such key per slot, all carrying this driver's name.
  Values typed into a stale one are read by nothing. Ask the device itself
  which key it uses. Find your controller under

    Windows 98    HKEY_LOCAL_MACHINE\Enum\PCI
    Windows 2000  HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\PCI
    and later

  open the subkey for the slot it occupies, and read its Driver value. It
  names the key to edit - for example USB\0004.

  SET THE TWO LOG VALUES ONLY WHILE DIAGNOSING SOMETHING, AND RUN
  XHCISNAP -DISABLE WHEN YOU HAVE SENT THE CAPTURE. While the channel is
  enabled, anyone using this machine can read the driver's own diagnostic
  state through it - counters, the log, the port table, and at level 4
  internal addresses. It is this driver's own state and nothing else: no
  documents, no passwords, no other program's memory. At level 0 the driver
  does not answer at all, so the value you set IS the lock. On Windows 2000
  and later you need administrator rights to set it (on Vista and 7, a
  Command Prompt started with "Run as administrator").


==============================================================================
 10. RELEASE HISTORY
==============================================================================

{HISTORY}

==============================================================================
 LICENCE
==============================================================================

GNU GPL v2 - see the LICENSE file in this directory, beside this readme. This
applies to this project's own code in xhci98.sys, xhciuas.sys and the 32-bit
helper xhcimsi.sys, and to their INFs. The 64-bit xhci98.sys and xhcimsi.sys
also carry a few small support routines that Microsoft's driver kit links in
(a stack-cookie check, and in xhcimsi.sys a device-security helper), which
are not this project's work; docs/contributing/legal-provenance.md, in the
project's source repository, records them.

No Microsoft file is in this download. The usbd.sys and usbui.dll the
install needs are copied by Windows from your own Windows installation
source (section 3), and on Windows 98 SE the storage files of section 3 come
from NUSB; nothing here grants you any right in them, and nothing here
copies them. xhciuas.inf names NUSB's USBNTMAP.SYS on Windows 98 SE; the
file is named, never shipped.

The provenance record for everything the project depends on but does not own
is in docs/contributing/legal-provenance.md, in the project's source
repository rather than here.
'@

    #
    # **Version literals in the perpetual template, each one a decision.**
    #
    # The template above is written once and used by every cut. Sentences about
    # a particular release - "new in 1.0.2.0", "since 1.1.0.0", what an
    # upgrade from an older version leaves behind - stay true at every later
    # version, which is exactly why nobody notices them going stale, and until
    # the 2026-09-07 audit's H15 nothing made anyone look.
    #
    # **The owner's rule since 2026-09-17: the readme states the current state
    # only.** What changed between versions, and why, belongs in section 10,
    # which is history.md embedded; the template itself names no release. So
    # the list below is empty, and a four-part version written into the
    # template by hand fails the cut. Write it with {VERSION}, or put the
    # sentence in history.md. This runs against the template BEFORE
    # substitution, so {VERSION} and the embedded history are not in scope.
    #
    $allowedTemplateVersions = @{}
    $templateVersions = @([regex]::Matches($template, '\b\d+\.\d+\.\d+\.\d+\b') |
                          ForEach-Object { $_.Value } | Sort-Object -Unique)
    $unlisted = @($templateVersions | Where-Object { -not $allowedTemplateVersions.ContainsKey($_) })
    if ($unlisted.Count -gt 0) {
        throw @"
the readme template names version(s) $($unlisted -join ', ') by hand, and
nothing in this script says why. Everything version-specific in that template
is substituted from {VERSION}; a literal is release-specific prose in a
perpetual file, which is how a readme goes quietly stale. Either write it with
{VERSION}, or add it to `$allowedTemplateVersions` above with the reason it has
to be spelled out - and check the others there are still worth saying while you
are in it.
"@
    }

    # **The schema number section 6 names is the driver's, or the cut fails.**
    # It is the one number in the template that is neither a version nor
    # substituted, and a user reads it to tell a stale XHCISNAP from this one.
    $templateSchemas = @([regex]::Matches($template, 'snapshot schema (\d+)') |
                         ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object -Unique)
    $driverSchema = Get-SnapSchema -Path (Join-Path $repo "src\xhci.h") -Macro "XHCI_SNAPSHOT_SCHEMA"
    $staleSchemas = @($templateSchemas | Where-Object { $_ -ne $driverSchema })
    if ($staleSchemas.Count -gt 0) {
        throw @"
the readme template names snapshot schema $($staleSchemas -join ', '), and
src\xhci.h declares XHCI_SNAPSHOT_SCHEMA $driverSchema. Section 6 tells the user
which XHCISNAP matches this driver by that number; correct the sentence.
"@
    }

    # Markdown -> plain text for the embedded history.
    #
    # **Re-wrapped, not just re-prefixed.** history.md is wrapped for markdown at
    # its own margin; adding a bullet indent to each line pushes the long ones
    # past 78 columns, which is where a DOS console and Windows 98 Notepad start
    # breaking lines in the wrong places. So each block is joined back into one
    # string, stripped of inline markup, and wrapped to fit with a hanging
    # indent - the paragraph shape survives, the width is enforced.
    $historyText = @(ConvertFrom-MarkdownBlocks -Lines $history -Width 78)

    #
    # **An incomplete cut has to say so IN THE FILE**, which is the 2026-09-07
    # audit's H14. `-SkipQualtool` and `-SkipSnapTool` warn on the console and
    # leave the section-8 listing out, but every other mention of those
    # directories stayed: section 1 tells the reader the checker is in
    # TOOLS\XHCIQUAL\, section 6 tells them to run XHCISNAP, and neither directory is
    # there. build-and-test.md says the result "is incomplete and says so", and
    # the console is not where a user reads it.
    #
    $missingTools = @()
    if ($null -eq $qualtoolStaged) { $missingTools += "TOOLS\XHCIQUAL\ (the DOS machine checker of section 1)" }
    if ($null -eq $snaptoolStaged) { $missingTools += "TOOLS\XHCISNAP\ (the report tool of section 6)" }
    $incomplete = ""
    if ($missingTools.Count -gt 0) {
        # Leading blank line included, so an empty substitution leaves the
        # "Released <date>." line exactly as it was.
        $incomplete = @"


******************************************************************************
 THIS DOWNLOAD IS INCOMPLETE. It was cut without:

$(($missingTools | ForEach-Object { "      " + $_ }) -join "`r`n")

 The driver itself is whole and installs normally. What is missing is the
 tooling, so the parts of this file that tell you to run those programs
 cannot be followed - ignore them, or get a complete download.
******************************************************************************
"@
    }

    # **A TEST build says so in the readme's first lines** (-TestLabel). The
    # placeholder sits alone on the blank line under the title box, so an
    # ordinary cut renders that blank line exactly as before; a test cut puts
    # the banner there, between two blank lines.
    $testBanner = ""
    if ($TestLabel -ne "") {
        $testBanner = @"

******************************************************************************
 TEST $Version, $TestLabel.
 This is a test build, not a release. It is not published, and a later
 release replaces it. Section 10, its release history entry, says what it
 is for and what it changes.
******************************************************************************

"@
    }

    $readme = $template.
        Replace("{TESTBANNER}", $testBanner).
        Replace("{VERSION}", $Version).
        Replace("{DATE}", $today).
        Replace("{RELEASEDIR}", $readmeReleaseDir).
        Replace("{DEBUGDIR}", $readmeDebugDir).
        Replace("{DIRLIST}", $readmeDirList).
        Replace("{TELLAPART}", $readmeTellApart).
        Replace("{ARCHNOTE}", $readmeArchNote).
        Replace("{ARCHOS}", $readmeArchOs).
        Replace("{ARCHOSREQ}", $readmeArchOsReq).
        Replace("{ARCHFILES}", $readmeArchFiles).
        Replace("{ARCHINSTALL}", $readmeArchInstall).
        Replace("{INCOMPLETE}", $incomplete).
        Replace("{CONTENTS}", (($contents -join "`r`n").TrimEnd() + "`r`n")).
        Replace("{HISTORY}", (($historyText -join "`r`n").TrimEnd()))

    # Nothing may reach the reader still carrying a placeholder: a typo in one
    # would otherwise ship as literal braces in a file nobody re-reads.
    # Case-sensitive (-cmatch): every placeholder is upper case, and the
    # readme legitimately carries lower-case braces, such as bcdedit's
    # {current} in the x64 test-signing text; -match ignores case.
    if ($readme -cmatch '\{[A-Z]+\}') {
        throw "readme template left an unsubstituted placeholder: $($Matches[0])"
    }

    # The rendered readme, after substitution: the template's own TODO(...)
    # lines (task 32.1's draft) and anything a substitution brought in. The
    # history entry and the release notes were checked before the build.
    $readmeDraft = @(Find-DraftMarkers -Lines ($readme -split "`r?`n") -Label "readme.txt (rendered)")
    if ($readmeDraft.Count -gt 0) {
        throw @"
the rendered readme.txt still carries $($readmeDraft.Count) TODO(...) or TBD
marker(s); the template near the end of make-release.ps1 is a draft until each
is replaced by its reading. The first:
  $(($readmeDraft | Select-Object -First 10) -join "`n  ")
"@
    }

    # 78 columns, checked rather than trusted. The file is read in Windows 98
    # Notepad and DOS EDIT, where a longer line wraps in the wrong place - and
    # now that names and lists are substituted in, a line's width depends on the
    # manifest. The first substitution overran by nine characters on the day it
    # was written, in a file nobody re-reads after generating it.
    Write-GeneratedText -Path (Join-Path $destRoot "readme.txt") `
                        -Lines ($readme -split "`r?`n")

    # --- the qualifier's own readme -----------------------------------------
    if ($null -ne $qualtoolStaged) {
        $qualReadme = @'
==============================================================================
 XHCIQUAL - will this machine run the xhci98 driver?
==============================================================================

This is a small DOS program that looks at the machine's USB controller and
tells you whether the xhci98 driver can work on it. It is read-only: it takes
ownership of nothing and changes no setting.

Running it is optional. It is worth doing because one of the ways a machine
can be unsuitable CANNOT BE FIXED IN SOFTWARE, and this tells you before you
spend an afternoon installing an operating system.


HOW TO RUN IT
------------------------------------------------------------------------------

  1. Copy XHCIQUAL.EXE onto a DOS boot disk - a floppy, a CD, or a bootable
     USB key with plain MS-DOS or FreeDOS on it.

  2. Boot the machine from it.

  3. Type one command, with no arguments:

           XHCIQUAL

  4. Read the verdict on the last line.


>> IT MUST BE RUN FROM REAL DOS, NOT A DOS WINDOW INSIDE WINDOWS. <<

   Do not run it from a "MS-DOS Prompt" inside Windows 98, and not from
   CMD.EXE on Windows 2000 or later. The program talks to the controller
   hardware directly and needs memory it can address one-to-one. A DOS box
   inside Windows does not give it either of those, so the answers would be
   wrong or it would simply fail.

   For the same reason, boot without EMM386 or any other memory manager
   loaded. A bare boot disk is exactly right.

   HIMEM.SYS IS THE EXCEPTION, AND ON SOME MACHINES IT IS NEEDED. It is not
   a memory manager in the sense above - it does not put the processor into
   the mode that breaks this program - and this program runs in 32-bit mode
   through an embedded DOS extender, so it needs the extended memory HIMEM
   provides. If it will not run at all on a bare boot, add one line to
   CONFIG.SYS and try again:

         DEVICE=C:\WINDOWS\HIMEM.SYS /M:1 /V

   Use whatever path HIMEM.SYS is actually at - C:\WINDOWS\ on a Windows 98
   machine, the root of the disk on a boot floppy. /M:1 fixes how it enables
   the A20 line instead of letting it guess, and /V makes it say at boot
   whether it loaded.


WHAT THE VERDICT MEANS
------------------------------------------------------------------------------

  LOOKS QUALIFIED
      Nothing this pass can see disqualifies the machine. Go ahead and
      install the driver.

      One thing it does not cover, on either verdict. This tool runs under
      DOS, so when it tests interrupt delivery it tests the old PIC path.
      That is exactly the path Windows 98 uses, and the one Windows 2000
      uses with a PIC HAL. Windows 2000 on a multi-core machine normally
      installs an APIC HAL instead, which routes interrupts a different way
      that DOS cannot exercise. So a pass here is proof for Windows 98, and
      strong but not complete evidence for Windows 2000. Nothing about the
      controller is being hidden from you - it is a limit of testing from
      DOS, and the same limit applies to every machine.

  LOOKS QUALIFIED WITH CAVEATS
      The same, but the controller has no interrupt pin. The driver reads
      its events on a timer instead (polling), or on Windows Vista and 7
      may take a message-signalled interrupt; the line says whether the
      controller offers one. Go ahead and install.

  DISQUALIFIED
      Something it can see rules the machine out. It will say which:

        - no xHCI controller present
        - the controller's memory window is unusable, or sits above 4 GB
        - no USB 2.0 ports on the controller

  CANNOT SAY
      Something the tool is not allowed to change is in the way: the
      controller is powered down, or its memory access is switched off.
      Look for xHCI, "USB 3.0 Mode", or Legacy USB settings in the BIOS,
      then run it again.


COMMAND LINE
------------------------------------------------------------------------------

The no-argument command above is the one to use, and it is the whole of what
most people need. The program does take arguments, and they are listed here
so that a log someone asks you for can be produced without guesswork.

  XHCIQUAL                          the read-only quick scan, one screen
  XHCIQUAL [xhci|ehci|ohci|all] [options]
  XHCIQUAL --scan TYPE [--scan TYPE ...] [options]
  XHCIQUAL --help

  Options may be written in any order, before or after a family word.


WHICH CONTROLLERS IT LOOKS AT

  With no family word it looks at all three. The driver only cares about
  xHCI; the other two are there because a machine's other controllers are
  part of the picture when something does not add up.

  xhci | ehci | ohci     one family only
  all                    all three - the default
  --xhci --ehci --ohci   the same three selectors, written as options
  --scan TYPE            the same again; repeat it to combine families


READ-ONLY MODES - these change nothing on the machine

  --quick           the no-argument quick scan, asked for explicitly
  --probe-only      read-only discovery, fuller than --quick. It reads the
                    controller's memory window only if the firmware has
                    already switched it on, and switches nothing on itself
  --no-active       another name for --probe-only
  --gsi N           (also --gsi=N, N decimal 0-255) compare an Intel
                    chipset's interrupt routing against N, the IRQ Device
                    Manager shows for the controller under the system in
                    question. Accepted only beside --quick or --probe-only;
                    alone it is a usage error


MODIFIERS - these say what to do with the report, not what to run

  ON THEIR OWN THEY DO NOT MAKE THE RUN READ-ONLY. Any argument at all
  turns off the no-argument quick scan, so XHCIQUAL --log FILE performs the
  FULL ACTIVE run below. Pair one with --quick or --probe-only when a
  read-only run is what you want.

  --no-page         do not stop at the end of each screenful
  --serial          mirror the output to COM1, 115200 8N1
  --log [FILE]      also write the report to a file, default XHCIQUAL.LOG.
                    A family word after --log is read as a selector, so
                    name the file explicitly if you pass both
  --log-stamp       write the report to a NEW file in the current directory
                    named from the DOS date and time, mDDhhmmS.LOG (month 1
                    to 9 then A, B, C; day; hour; minute; the first free
                    letter A to Z). It never overwrites a file, and not
                    with --log. QUALREP.BAT uses it
  --done-flag FILE  create FILE only if the run finishes normally, so a
                    batch file can tell a crash apart from a bad verdict
  --no-sysinfo      do not read the machine's description (SMBIOS,
                    processor, memory, DOS version) for the report's
                    header; the header then says it was skipped
  --help, -h, /?    a longer help text, printed by the program itself


ACTIVE OPTIONS - THESE TAKE OVER THE CONTROLLER

  Read A NOTE ON SAFETY below before using any of them. You do not need
  them to answer "will the driver work".

  --full            the full active run, across all three families
  --poll-only       active bring-up with no interrupt handler installed.
                    The mildest of these, and the one to try first
  --irq-selftest    xHCI only: an isolated, one-shot interrupt test
  --set-intel-ports try to route the Intel USB2 ports to xHCI and read the
                    result back. This one writes PCI configuration space
  --no-wait         do not wait 15 seconds for a device to be plugged in
  --no-devid        skip the xHCI device identification step


FOR A BUG REPORT: QUALREP.BAT

  QUALREP.BAT, beside XHCIQUAL.EXE, is the one command a bug report asks
  for. Run it from this directory, under real DOS as above:

           QUALREP

  It runs XHCIQUAL --probe-only --no-page --log-stamp: the read-only probe,
  written to a new .LOG in the current directory named from the DOS date
  and time, for example 3141022A.LOG for 14 March at 10:22. A second run
  never overwrites the first. It prints the name; attach that .LOG file to
  the report. If the DOS clock is not set to a date, XHCIQUAL refuses
  before reading anything - set it with DATE and TIME and run it again.


IF YOU ARE ASKED FOR LOGS

  Run these two in order. The first reads everything the read-only path can
  see and leaves PROBE.LOG in the current directory. Nothing in it writes to
  the machine:

           XHCIQUAL --probe-only --log PROBE.LOG

  If that does not crash the machine, continue with the full run, which
  leaves FULL.LOG. THIS ONE TAKES OVER THE CONTROLLER - read A NOTE ON SAFETY
  below first:

           XHCIQUAL --log FULL.LOG

  Send FULL.LOG if the full run finished; only if it did not, send
  PROBE.LOG. Keep a note of the BIOS settings you had, and of any device that
  was plugged in.


WHAT IT RETURNS TO DOS

  0   the active tests passed
  1   not qualified, or a read-only run - which cannot pass tests it does
      not run, so 1 is the normal result of the commands above
  2   the command line was wrong, or no controller of that kind is here
  3   a full active run whose controllers all qualified, at least one only
      WITH CAVEATS (no working interrupt, so the driver polls it)
  4   --log-stamp could not name or create its log; nothing was read

  The verdict on screen is what to read. These are for batch files.

  The report's header names the machine it ran on - make, model and BIOS,
  processor, memory and DOS version - and never a serial number, a UUID
  or an asset tag, so a log can be posted publicly as it is.


THE .MAP FILE
------------------------------------------------------------------------------

XHCIQUAL.MAP is not needed to run the tool, and you can ignore it. Keep it
beside XHCIQUAL.EXE anyway: if the program ever crashes with an address on
screen, that file is what turns the address back into a location in the
source, and it only matches THIS build of the EXE.


A NOTE ON SAFETY
------------------------------------------------------------------------------

The plain XHCIQUAL command only reads, and so do --quick and --probe-only:
they take ownership of nothing and write no PCI configuration register.

The active options are a different thing. They take ownership of the
controller, reset it, transfer data and reset ports, and a machine can stop
responding while they run. They are development instrumentation, listed
above because this one program contains them - not because installing the
driver involves them. The numbered batch files the project uses to drive
them in a fixed order are not included here; QUALREP.BAT, which is, runs
only the read-only probe.

If you run one anyway:

  - Boot real DOS with no memory manager, exactly as above - including the
    HIMEM.SYS exception, which the active options need just as much.
  - Use a PS/2 keyboard if the machine has one. A USB keyboard on the
    controller being tested can stop responding part-way through.
  - Do not boot the machine through that same controller, and do not write
    the log to a disk attached to it.
  - Unplug storage you care about. Ports get reset.

If the machine ever stops responding during a hardware test, power it off
completely and cold boot. Do not carry on from whatever state was left
behind.


------------------------------------------------------------------------------
Part of the xhci98 {VERSION} release. See the readme.txt two directories up
for the driver itself.

This project's own code is under the GNU GPL v2 - see the LICENSE file two
directories up. XHCIQUAL.EXE is not only this project's code: it is linked
as a DOS/32 Advanced DOS Extender executable and statically includes that
extender and the Open Watcom C runtime, which carry their own notices. Those
notices are in NOTICE.TXT beside this file, and this product uses DOS/32
Advanced DOS Extender technology.
'@
        $qualReadme = $qualReadme.Replace("{VERSION}", $Version)
        Write-GeneratedText -Path (Join-Path $qualtoolStaged "readme.txt") `
                            -Lines ($qualReadme -split "`r?`n")

        # --- the notices the qualifier binary carries with it ----------------
        #
        # **XHCIQUAL.EXE is a published binary that statically contains code
        # this project did not write.** xhciqual\makefile links it `system
        # dos32a`, which embeds the full DOS/32A extender as the EXE stub so the
        # tool is one standalone file, and XHCIQUAL.MAP records Open Watcom
        # runtime modules (clib3r.lib) linked in beside it.
        #
        # DOS/32A's own licence text (C:\WATCOM\binw\license.d32 in an Open
        # Watcom install) contains a clause 2 about reproducing its copyright
        # notice, condition list and disclaimer in materials provided with a
        # binary distribution, and a clause 3 about an acknowledgement sentence
        # in end-user documentation. Until `0.0.0.2` this release carried
        # neither text, naming only this project's own licence.
        #
        # This file reproduces that licence in full and records the Open Watcom
        # linkage. It states facts and no verdict; the full provenance record
        # is docs\contributing\legal-provenance.md section 2a. See AGENTS.md,
        # "Third-Party Material and Provenance" - a legal conclusion may not be
        # written into this repository.
        $qualNotice = @'
==============================================================================
                          x h c i 9 8   {VERSION}
                    THIRD-PARTY NOTICES FOR XHCIQUAL.EXE
==============================================================================

XHCIQUAL.EXE is built from this project's own C and assembly sources, which
are under the GNU GPL v2 - see the LICENSE file two directories up.

It is linked as a DOS/32 Advanced DOS Extender (LE-style) executable. That
means the extender itself is embedded in the EXE as its stub, so the tool is a
single standalone file with no separate extender to carry. The Open Watcom C
runtime is statically linked in as well. Neither is this project's code, and
each carries its own terms. Below, the DOS/32 Advanced DOS Extender licence is
reproduced in full; for the Open Watcom runtime, what is recorded is the
linkage, the copyright lines its linker writes, and the name of its licence.


------------------------------------------------------------------------------
 DOS/32 Advanced DOS Extender
------------------------------------------------------------------------------

This product uses DOS/32 Advanced DOS Extender technology.

DOS/32 Advanced DOS Extender Software License

Copyright (C) 1996-2006 by Narech K. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. The end-user documentation included with the redistribution, if any, must
include the following acknowledgment:

"This product uses DOS/32 Advanced DOS Extender technology."

Alternately, this acknowledgment may appear in the software itself, if and
wherever such third-party acknowledgments normally appear.

4. Products derived from this software may not be called "DOS/32A" or
"DOS/32 Advanced".

THIS SOFTWARE AND DOCUMENTATION IS PROVIDED "AS IS" AND ANY EXPRESSED OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


------------------------------------------------------------------------------
 Open Watcom C runtime
------------------------------------------------------------------------------

XHCIQUAL.EXE statically links modules from the Open Watcom C runtime library
(clib3r.lib); XHCIQUAL.MAP beside this file names them individually. The
Open Watcom linker records its own copyright at the head of that map:

  Copyright (c) 2002-2026 The Open Watcom Contributors. All Rights Reserved.
  Portions Copyright (c) 1985-2002 Sybase, Inc. All Rights Reserved.

Open Watcom is distributed under the Sybase Open Watcom Public License. What
this file records is the linkage and the copyright above; the project's full
provenance record is docs\contributing\legal-provenance.md, section 2a.
'@
        $qualNotice = $qualNotice.Replace("{VERSION}", $Version)
        Write-GeneratedText -Path (Join-Path $qualtoolStaged "NOTICE.TXT") `
                            -Lines ($qualNotice -split "`r?`n")
        Write-Ok "NOTICE.TXT written beside XHCIQUAL.EXE (DOS/32A + Open Watcom runtime)"
    }

    # --- the snapshot reader's own readme ------------------------------------
    #
    # Written for the person who has a problem, not for a maintainer. What it
    # must get across is the one thing that is not obvious: **the channel is off
    # by default**, so a dump taken without step 1 comes back correct and empty,
    # and that is not a fault.
    if ($null -ne $snaptoolStaged) {
        $snapReadme = @'
==============================================================================
 XHCISNAP - getting a report out of the xhci98 driver
==============================================================================

If USB is not working properly with this driver, this is what to run. It reads
the driver's own log straight out of the running machine and writes a report
you can attach to a bug report.

It talks to the driver directly, through the controller's own device, on
every Windows version this driver supports. Nothing else needs installing,
and no log file is written unless you ask for one.

It changes nothing about how the driver behaves on the bus, and writes no file
it was not asked to. It does READ the controller's port registers, which is a
hardware access - it just does not write one, and it deliberately leaves the
"something changed here" flags it finds standing, so it takes no evidence away
from the driver either.

What step 1 DOES change is ONE of this driver's own settings - that is the
point of it, and it is why step 2 is a restart. It prints it as it writes it,
and SNAPOFF.BAT (XHCISNAP -disable) puts it back. Run that once you have sent
the capture: while it is on, anyone using this machine can read the driver's
diagnostic state.


 THE FOUR STEPS
------------------------------------------------------------------------------

Three batch files beside XHCISNAP.EXE do the typing. Double-click them, or
run them at a prompt in this directory; on Windows Vista and 7, right-click
each one and choose Run as administrator. Copy this directory somewhere
writable first if it is not (a CD is not).

  1.  SNAPON.BAT     XHCISNAP -on (-verbosity 2): the log channel on
  2.  restart Windows
  3.  make the problem happen again
  4.  SNAPGET.BAT    XHCISNAP -report: the report and the dump

SNAPGET.BAT writes its files in the directory it runs in, not in C:\, under a
name made from the date and time, and prints the names. Attach the .DEV and
.TXT files to the bug report. Attach the .BIN and .PSC as well only if you
are asked for them. It leaves the channel on, so you can run it again after
the next attempt. At a prompt, SNAPON 3 sets level 3 rather than 2 (1 to 4;
SNAPON 0 is refused, SNAPOFF.BAT is off), and SNAPGET MYPC saves the files
as MYPC.DEV, .TXT, .BIN and .PSC (1 to 8 letters, digits, _ or -, not
starting with -, not a DOS device name such as CON, NUL, COM1 or LPT1, in
this directory; a name already taken is refused, never overwritten). The
readme.txt at the top of the download, section 6, says what each of
SNAPGET.BAT's endings means. When you are done:

  5.  SNAPOFF.BAT    XHCISNAP -disable: the channel off

You never have to open REGEDIT. Step 1 does the whole of that for you, on
every xHCI controller the machine has. Without the batch files, type the
commands they run; XHCISNAP -o NAME writes the dump alone under a name you
choose.


 WHY STEP 1 IS NOT OPTIONAL
------------------------------------------------------------------------------

The driver answers nothing until it is asked to, and it reads that setting
once when it starts. So without step 1 and the restart this tool gets no
answer at all - which is right, not broken.

Level 2 is the one to use. The others exist and a maintainer may ask for one:

  0   OFF. The driver does not answer this tool at all. This is the default.
  1   answers, with counters only. The cheapest reading there is.
  2   plus the driver's own log of what happened. USE THIS ONE.
  3   plus the USB port register table.
  4   plus everything, including internal addresses. Only if asked - it is
      more than you would want to paste in public.


 WHAT THE FILES ARE
------------------------------------------------------------------------------

Each name is mDDhhmmS: the month as one character (1 to 9, then A, B, C for
October to December), the day, the hour, the minute, and the first letter A
to Z not yet taken, so 3141022A is 14 March at 10:22 and a second run in the
same minute is 3141022B. Nothing is ever overwritten.

  3141022A.DEV The controller as Windows sees it: its Device Manager status
               and problem code, the resources it was given, and the versions
               and SHA-256 hashes of xhci98.sys, xhciuas.sys, xhcimsi.sys,
               usbd.sys and usbui.dll as installed. Attach it.
  3141022A.TXT The report. Plain text. Below level 4 it holds no internal
               addresses - the driver refuses to record one at those levels,
               so that is a property of the file and not a promise about it.
               Attach it.
  3141022A.BIN The driver's raw internal state. A maintainer can decode it
               against the exact build you are running; nobody else can. Send
               it as an attachment if asked.
  3141022A.PSC The raw port register values, for the same audience.

The report's header names the system it was taken on - the Windows
version, BIOS, processor and memory - and never a serial number or a
name. If reading those details causes a problem on your machine, type
XHCISNAP -nosysinfo -report instead of step 4; the header then says they
were skipped.


 IF NOTHING COMES BACK
------------------------------------------------------------------------------

  XHCISNAP -probe

That checks whether the route to the driver works at all, separately from
whether this driver answers on it. If it says the request reached a driver and
that driver declined, the usual cause is simply that step 1 has not been
done - or that the machine has more than one USB controller and this is not
the right one, in which case try -c 1 and -c 2.

If it cannot open the device at all, no xHCI controller is started on this
machine, and there is nothing for this tool to read.


------------------------------------------------------------------------------
Part of the xhci98 {VERSION} release. See the readme.txt two directories up
for the driver itself.

This project's own code is under the GNU GPL v2 - see the LICENSE file two
directories up. XHCISNAP.EXE is not only this project's code: the Microsoft
Visual C++ 6.0 C runtime is statically linked into it, which is why it is one
file that runs on a machine with nothing installed on it. That linkage is
recorded in NOTICE.TXT beside this file.
'@
        $snapReadme = $snapReadme.Replace("{VERSION}", $Version)
        Write-GeneratedText -Path (Join-Path $snaptoolStaged "readme.txt") `
                            -Lines ($snapReadme -split "`r?`n")
        Write-Ok "readme.txt written beside XHCISNAP.EXE"

        # --- the notices the snapshot reader carries with it ------------------
        #
        # **XHCISNAP.EXE is a published binary that statically contains code
        # this project did not write**, and it reached its first release
        # (0.0.0.6) with no record of that at all - which is the qualifier's
        # own ordering failure, one tool over and nine days later.
        #
        # It is a much smaller case than the qualifier's: an ordinary Win32
        # console PE with no DOS extender and no Open Watcom code, whose only
        # third-party content is MSVC 6.0's C runtime, bound in because
        # xhcisnap\build.cmd does not pass /MD. `link -dump -imports` on the
        # published EXE names KERNEL32.dll and ADVAPI32.dll and no C runtime
        # DLL, which is how that is re-derivable by someone who has neither the
        # compiler nor this comment.
        #
        # **Nothing of that licence is quoted here, because nothing of it has
        # been read.** The qualifier's NOTICE.TXT reproduces the DOS/32A text
        # because that licence's own clause 2 asks for it; no equivalent clause
        # has been established here, and inventing a reproduction would be a
        # verdict rather than a fact. What this file records is the linkage.
        # See AGENTS.md, "Third-Party Material and Provenance", and
        # docs\contributing\legal-provenance.md section 2a.
        $snapNotice = @'
==============================================================================
                          x h c i 9 8   {VERSION}
                    THIRD-PARTY NOTICES FOR XHCISNAP.EXE
==============================================================================

XHCISNAP.EXE is built from this project's own C source, which is under the GNU
GPL v2 - see the LICENSE file two directories up.

It is not only this project's code. It is compiled with Microsoft Visual C++
6.0 and linked against that compiler's STATIC C runtime - the build passes no
/MD - so those runtime modules are bound into this executable rather than
loaded from a DLL when it runs. That is deliberate: it is what makes the
tool a single file that works on a Windows 98 SE machine with nothing
installed on it, which is the machine it exists for. You can see it for
yourself in the import table, which names only KERNEL32.dll and ADVAPI32.dll
and no C runtime DLL.

Those runtime modules are Microsoft's. They are not covered by the GPL grant
above, and they keep whatever terms accompany Visual C++ 6.0. What this file
records is the linkage; the project's full provenance record is
docs\contributing\legal-provenance.md, section 2a. Nothing in this file is a
statement that the arrangement is or is not sufficient.

No DOS extender and no Open Watcom code is in this executable; the NOTICE.TXT
beside XHCIQUAL.EXE covers a different program and does not apply here.
'@
        $snapNotice = $snapNotice.Replace("{VERSION}", $Version)
        Write-GeneratedText -Path (Join-Path $snaptoolStaged "NOTICE.TXT") `
                            -Lines ($snapNotice -split "`r?`n")
        Write-Ok "NOTICE.TXT written beside XHCISNAP.EXE (static MSVC 6.0 C runtime)"
    }

    # --- the licence the readme sends the reader to --------------------------
    #
    # **The generated guide says "GNU GPL v2 - see LICENSE", so LICENSE has to
    # be in the directory it says that in.** A release directory is described by
    # that same guide as a standalone thing someone may have downloaded on its
    # own, and the repository root is exactly what such a person does not have.
    # The file also carries the scope note - what the GPL grant covers and what
    # is third-party material this project does not redistribute - which is the
    # half a reader chasing provenance actually needs.
    $licenseSrc = Join-Path $repo "LICENSE"
    if (-not (Test-Path -LiteralPath $licenseSrc)) {
        throw @"
no LICENSE at '$licenseSrc'.
The per-release readme.txt tells the reader "GNU GPL v2 - see LICENSE", and a release
directory is meant to stand on its own, so it cannot be published without it.
"@
    }
    # **Copied, not generated - so this is the one published text file
    # Write-AsciiFile does not protect.** Everything else in a release
    # directory is written by this script through that helper, which writes
    # ASCII and CRLF whatever the host does. LICENSE comes off disk exactly as
    # the working tree holds it, and the working tree holds what the clone's
    # core.autocrlf and .gitattributes between them decided. A lone LF is one
    # line to Windows 98's Notepad and to DOS EDIT, which is the entire licence
    # as a single unreadable row on the machine the reader is standing at.
    # `.gitattributes` pins it at checkout; this is the same check at the point
    # of use, for the host whose attributes were bypassed or overridden.
    $licenseBytes = [System.IO.File]::ReadAllBytes($licenseSrc)
    $bareLf = 0
    for ($i = 0; $i -lt $licenseBytes.Length; $i++) {
        if ($licenseBytes[$i] -eq 10 -and ($i -eq 0 -or $licenseBytes[$i - 1] -ne 13)) {
            $bareLf++
        }
    }
    if ($bareLf -gt 0) {
        throw @"
'$licenseSrc' has $bareLf line ending(s) that are a bare LF, not CRLF.
A release directory is read on the target machine, where Windows 98's Notepad
and DOS EDIT show an LF-only file as one line - so this would publish the
licence as an unreadable single row, and the upload asset would carry it too.
The repository stores this file LF and `.gitattributes` pins it to eol=crlf on
checkout, so this host either has that attribute overridden or wrote the file
after checkout. Restore it with:  git checkout -- LICENSE
"@
    }
    Copy-Item -LiteralPath $licenseSrc -Destination (Join-Path $destRoot "LICENSE") -Force
    Write-Ok "LICENSE copied into the release root, CRLF throughout"

    # --- the upload set: what actually goes to the GitHub release ------------
    #
    # `releases\<version>\` is the tracked half. The download a user gets is
    # the same tree, zipped, with each flavour directory gated as install
    # media on the way. Since 1.0.0.1 it carries no Microsoft file: the OS
    # supplies usbd.sys, usbhub.sys and usbui.dll through the INF's
    # LayoutFile. See docs\contributing\legal-provenance.md section 5.
    #
    # **Assembled from the staging tree, BEFORE the publish swap below.** It
    # ran after the swap until the 2026-09-17 audit's D5, so a per-directory
    # INF-gate refusal or a zip failure inside it fired with the old release
    # already moved aside and the new one already under the real name - the
    # written-once release with no asset that the ordering test in
    # test-package.ps1 exists to prevent. Nothing in the set embeds the
    # published path: the tree is copied whole under out\upload-<version>\,
    # the zip entries are named relative to that copy, and the readme was
    # rendered into the staging tree above with no path in it - so the bytes
    # are the ones a post-swap run produced.
    #
    # **And built under the `.staging` names, published after the swap.** The
    # previous asset - `upload-<version>\` and `xhci98-<version>.zip` from an
    # earlier -Force cut of this version - is not touched here; it is replaced
    # by Publish-UploadSet below only once both renames of the swap have
    # succeeded, and a swap that fails removes the staged pair and leaves it
    # (Codex review round 2 on D5).
    $uploadRoot = $null
    $uploadZip = $null
    $uploadPublishError = ""
    if (-not $SkipUploadSet) {
        # Keyed by published directory name, which is what New-UploadSet walks:
        # to it a "flavour" is the name of a directory in the published tree,
        # and since this task that name carries the architecture too.
        $pkgDirs = @{}
        $legDirs = @()
        $legArches = @{}
        foreach ($leg in $legs) {
            $pkgDirs[$leg.Dir] = $staged[$leg.Id].PkgDir
            $legDirs += $leg.Dir
            $legArches[$leg.Dir] = $leg.PkgArch
        }
        $set = New-UploadSet -PublishedRoot $destRoot -Version $Version -Flavors $legDirs `
                             -LegArches $legArches `
                             -PkgDirs $pkgDirs -UploadDir $UploadDir -Repo $repo `
                             -Publishable $publishable -Staged
        $stagedUpload = $set
    }

    # --- publish: replace the destination only now ---------------------------
    #
    # Everything above has passed, the upload set included. What follows is the
    # first and only thing this script does to `releases\<version>\`.
    #
    # **The old release is moved aside, not deleted, and only removed once the
    # new one is in place.** A delete-then-move has two failure windows that
    # leave nothing publishable: a partial delete leaves a damaged release, and
    # a delete that succeeds followed by a move that does not leaves none at
    # all. Renaming first means every window has a complete tree in it - either
    # the old one under its aside name or the new one under the real name - so a
    # failure is recoverable by renaming a directory back rather than by
    # rebuilding.
    #
    # This is as atomic as a directory swap gets on Windows without a
    # transaction: `Move-Item` within one volume is a rename, and `releases\` is
    # one directory, so the aside and the staging tree are both on the same
    # volume as the destination by construction.
    # The whole staged release, the tools' directories included, whatever
    # -SkipUploadSet says (Codex review of 36.34, round 1, finding 6), and
    # before -Force moves an existing release aside (round 2, finding 4),
    # so a refusal leaves releases\ as it was. The port switchover's scripts
    # in tools\portsw\ and nowhere else (tasks 37.5 and 38.10), and the tools
    # in tools\ alone (task 38.10), at the same point and for the same reason.
    Assert-ToolsLayout -Root $destRoot -What "the staged release '$destRoot'"
    Assert-PortSwitchLayout -Root $destRoot -Names $pswPublishable -DirName (Get-ToolDirName -Name "portsw") `
                            -What "the staged release '$destRoot'"
    # The report batches beside their tools and nowhere else (task 38.1).
    Assert-ReportBatchLayout -Root $destRoot -Batches $reportBatches -What "the staged release '$destRoot'"
    # And what they say: the shared checker, and the repository's own bytes.
    Assert-ReportBatchContent -Root $destRoot -Batches $reportBatches -What "the staged release '$destRoot'" -Reference $repo
    Assert-NoTestClient -Root $destRoot -What "the staged release '$destRoot'"
    $asideRoot = $null
    if (Test-Path -LiteralPath $finalRoot) {
        Write-Warn ("-Force: replacing the existing {0}" -f $finalRoot)
        $asideRoot = Join-Path $ReleasesDir (".replaced-" + $Version)
        if (Test-Path -LiteralPath $asideRoot) {
            Remove-Item -LiteralPath $asideRoot -Recurse -Force
        }
        Move-Item -LiteralPath $finalRoot -Destination $asideRoot
    }

    try {
        Move-Item -LiteralPath $destRoot -Destination $finalRoot
    } catch {
        if ($null -ne $asideRoot) {
            # Put it back. The publish did not happen, so the previously
            # published tree is what should be on disk.
            Move-Item -LiteralPath $asideRoot -Destination $finalRoot
            Write-Warn ("the publish failed; the previous {0} has been restored" -f $finalRoot)
        }
        throw
    }
    $swapDone = $true

    if ($null -ne $asideRoot) {
        # The replacement is in place, so this is now only disk space - the
        # make-package.ps1 rule for its retired package. A failure to remove it
        # must not fail a cut whose publish is already correct, and on this
        # OneDrive-hosted tree a sync handle inside the just-renamed aside made
        # it fail under Stop: the cut reported as failed with the release
        # published, and the next run refused on the aside (the 2026-09-17
        # audit's D4). The early check at the top of this run removes a
        # leftover aside beside a published tree for the same reason.
        Remove-Item -LiteralPath $asideRoot -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $asideRoot) {
            Write-Warn ("could not remove the replaced release, left at '{0}'; it is superseded by '{1}' and the next cut removes it" -f `
                $asideRoot, $finalRoot)
        }
    }
    $destRoot = $finalRoot

    # --- the upload set onto its final names, now that the release is published
    #
    # **A failure here is not a failed cut.** The release is published and the
    # asset is derived from it, so a rename that fails in out\ is a warning
    # and a recovery hint, not an exit 1 and not a rollback: the outer catch
    # would report a published cut as failed and could not undo the publish
    # anyway (Codex review round 3). The staged pair is left where it is so
    # nothing is lost, the state of out\ is said, and -UploadSetOnly - which
    # rebuilds the asset from the published tree - is the way back.
    if ($null -ne $stagedUpload) {
        try {
            $final = Publish-UploadSet -Set $stagedUpload
            $stagedUpload = $null
            $uploadRoot = $final.Root
            $uploadZip = $final.Zip
            Write-Ok ("upload set published as {0}" -f $uploadZip)
        } catch {
            $uploadPublishError = $_.Exception.Message
            $stateLines = @()
            foreach ($p in @($stagedUpload.Final.Root, $stagedUpload.Final.Zip, $stagedUpload.Root, $stagedUpload.Zip, ($stagedUpload.Final.Zip + ".previous"))) {
                $stateLines += ("              {0}  {1}" -f $(if (Test-Path -LiteralPath $p) { "present" } else { "absent " }), $p)
            }
            Write-Warn ("the release IS published at {0}, but its upload set could not be moved onto its final names: {1}" -f $finalRoot, $uploadPublishError)
            Write-Host   "            The staged set has been left in place. out\ now holds:"
            foreach ($l in $stateLines) { Write-Host $l }
            Write-Host   "            Rebuild the asset from the published tree with:"
            Write-Host  ("              {0}" -f (Get-UploadSetRepairCommand))
            $stagedUpload = $null
        }
    }

    # --- summary -------------------------------------------------------------
    Write-Host ""
    foreach ($item in (Get-ChildItem -LiteralPath $destRoot -File -Recurse | Sort-Object FullName)) {
        $shown = $item.FullName.Substring($destRoot.Length).TrimStart('\')
        Write-Host ("  {0,-28} {1,9} B" -f $shown, $item.Length)
    }
    Write-Host ""
    foreach ($leg in $legs) {
        if ($staged.ContainsKey($leg.Id)) {
            Write-Ok ("{0}\xhci98.sys is the {1} {2} build (VS_FF_DEBUG = {3})" -f `
                $staged[$leg.Id].Published, $leg.Arch, $leg.Flavor, ($leg.Flavor -eq "debug"))
        }
    }
    if ($null -eq $qualtoolStaged) {
        Write-Warn "no tools\xhciqual\ in this release (-SkipQualtool) - it is incomplete."
    }
    if ($null -eq $snaptoolStaged) {
        Write-Warn "no tools\xhcisnap\ in this release (-SkipSnapTool) - it publishes a read channel nobody can open."
    }

    Write-Step "Done"
    Write-Host "Tracked:    $destRoot"
    Write-Host "            Two files per published directory - this project's own. Commit this."
    if ($null -ne $uploadRoot) {
        Write-Host ""
        Write-Host "Upload:     $uploadZip"
        Write-Host "            The same tree, zipped: this project's two files per flavour,"
        Write-Host "            the tools and the readmes, and no Microsoft file. This is the"
        Write-Host "            GitHub release asset; it is git-ignored and must stay that way."
        Write-Host "            See releases\README.md and docs\contributing\legal-provenance.md"
        Write-Host "            section 5."
    } elseif ($uploadPublishError -ne "") {
        Write-Warn ("the upload set was NOT moved onto its final names (see above); rebuild it with: {0}" -f (Get-UploadSetRepairCommand))
    } else {
        Write-Warn "-SkipUploadSet: no release asset was assembled, so there is nothing to upload."
        Write-Host "            Install from out\pkg-<flavor>\ or from the tracked directory;"
        Write-Host "            both hold the same two files."
    }
} catch {
    Write-Err $_.Exception.Message
    if ($null -ne $stagedUpload -and -not $swapDone) {
        # The cut did not publish, so the set built for it goes, and the
        # asset that was there before the run stays what it was. Only before
        # the swap: after it the cut is published, the staged set is handled
        # (and kept) by the local catch above, and this wording would be false.
        Remove-StagedUploadSet -Set $stagedUpload
        Write-Warn ("the staged upload set was removed; the previous {0}, if any, is untouched" -f $stagedUpload.Final.Zip)
    } elseif ($swapDone) {
        Write-Warn ("the release IS published at {0}; what failed came after the publish swap. If the upload set is missing or stale, rebuild it with: {1}" -f $finalRoot, (Get-UploadSetRepairCommand))
    }
    exit 1
}

exit 0
