<#
.SYNOPSIS
Assemble the install package both targets are installed from.

.DESCRIPTION
The package is an 8.3-clean directory - a floppy image, a VVFAT share, or a
CD - holding everything [SourceDisksFiles] names, which since release 1.0.0.1
is this project's two files and nothing else:

    xhci98.inf     the dual-path INF
    xhci98.sys     the built miniport (debug or release)
    xhciuas.inf    the UAS class driver's INF (roadmap task 31-A.2;
                   src\uas\xhciuas-amd64.inf under this name for amd64)
    xhciuas.sys    the UAS class driver, the same flavour

The Microsoft files the driver depends on, usbd.sys and usbhub.sys (both
targets), usbport.sys (the NT targets; on Windows 98 the USB 2.0 stack
places it) and, since 1.0.2.0, usbui.dll (every target, the root hub's
property-page provider, to dirid 11 rather than 10), are not on the media:
the INF names LayoutFile=layout.inf and
the Windows setup engine copies them from the operating system's own install
source (docs\contributing\build-and-test.md, "The files the OS supplies"). Release 1.0.0.0 carried them here under
per-target media names, authenticated against a manifest; that was withdrawn
before any upload, and check-inf.ps1 -PackageDir now refuses a package that
carries a Microsoft file under any name.

Where each file goes is not decided here. check-inf.ps1 -EmitMediaLayout is
asked for the layout its own [SourceDisksFiles] parse produces, and this script
stages against that - so a per-file subdirectory is honoured automatically and
the staged path and the gated path cannot drift apart. That call also gates
the INF *before* anything is copied, which is the right order: there is no
value in staging a package around an INF that will be rejected.

.PARAMETER Arch
x86 (default) or amd64 - which architecture's binary and INF to package. It
selects three things together and they must not be mixed: the obj
subdirectory (src\obj*\i386 or src\obj*\amd64), the INF (src\xhci98.inf or
src\xhci98-amd64.inf), and the -Arch the INF and import gates are run under.
A package staging an amd64 binary under the 32-bit INF would install on a
32-bit machine and fail its load with no diagnostic, which is why one switch
moves all three rather than three switches that can disagree.

The two architectures' binaries share a filename, so they cannot share a
directory: the default output is out\pkg-<flavor>-<arch>, and a release cuts
them into release-x86, debug-x86, release-x64 and debug-x64 (roadmap task
21.3, design record 11 section 8).

.PARAMETER Flavor
debug (default), release or qemu - which src\obj*\<arch>\xhci98.sys to package.
The DDK's own words for the two *checked* builds are both "checked" and for the
free one "free", and they survive only in the obj directory names it writes.

qemu is a first-class flavour and this script stages it, because that is how it
reaches a guest at all - it is the emulator and bench build, carrying the
port-0xE9 mirror and the live per-line trace. **It is NEVER PUBLISHED**: it
stages to out\pkg-qemu-<arch> with a banner saying so, and scripts\package\make-release.ps1
refuses it by reading the image's own flavour marker rather than by trusting a
path. See docs\contributing\design\08-build-flavours-and-the-log-channel.md.

.PARAMETER OutDir
Where to build the package. A relative path is taken as relative to the current
directory. Defaults to out\pkg-<flavor>-<arch> in the repository.

.PARAMETER InfPath
The INF to stage, overriding the one -Arch selects (`src\xhci98.inf` for x86,
`src\xhci98-amd64.inf` for amd64). It is still gated under -Arch, so handing
this the wrong architecture's file is refused rather than staged - which is the
case that matters, because a caller who edited only -DriverPath produces
exactly it. Exists for the packager's own self-tests, which stage mutated INFs.

.PARAMETER DriverPath
The binary to stage, overriding `src\obj<flavour>\<archdir>\xhci98.sys`. The
flavour marker in the image is still checked against -Flavor, so this cannot be
used to publish one flavour under another's name. Exists for the self-tests,
which stage text stand-ins carrying a marker and nothing else.

.PARAMETER UasInfPath
The UAS class driver's INF to stage (task 31-A.2), overriding
`src\uas\xhciuas.inf` (x86) or `src\uas\xhciuas-amd64.inf` (amd64). Staged
as `xhciuas.inf` either way, the name its [SourceDisksFiles] gives it, and
gated under -Arch.

.PARAMETER UasDriverPath
The UAS class driver to stage, overriding
`src\uas\obj<flavour>\<archdir>\xhciuas.sys`. Both packages carry the
UAS driver whenever they are built from the default paths: a missing xhciuas.sys
is then a refusal, not an omission. A caller that names -DriverPath (the
self-tests' stand-ins) stages the UAS pair only when it names -UasDriverPath
too, so a stand-in package is not filled from whatever the last build left
in src\uas.

.PARAMETER SkipPackageGate
Skip the post-staging check-inf.ps1 -PackageDir run only. The INF is gated
before staging either way, because that run is also where the media layout
comes from.

.PARAMETER SkipBinaryGates
Skip the pre-staging host-test suite and import gate. This exists for the
packager's own self-tests, which stage text stand-ins rather than a linked
binary. Never pass it for media a VM will be installed from: those two gates
are what roadmap Phase 4 task 1 requires before every deploy, and neither
failure they catch is visible on the target (an unresolved import stops the
driver before DriverEntry with no call-site diagnostic; a bad carve or ring
constant DMAs into memory the driver does not own).

.PARAMETER NoTargetEvidence
Passed through to the import gate on a host with no extracted target binaries
staged. Enforcement of the committed allowlist still runs.

.PARAMETER FailStartArtifact
**No current build produces this artifact.** It was the miniport's; since
2026-10-02 src\ is the successor HCD, which has no diagnostic builds, and both
src\sources and scripts\build-driver.cmd refuse any XHCI_EXTRA_DEFINES. The
switch and its marker checks stay, fail-closed: an HCD image carries no
XHCI98_FAILSTART_ARTIFACT_TASK_12_3, so the switch refuses it. The recipe below
is the miniport's, and works on branch 1.2.0.0 only.

Stage roadmap task 12.3's failed-start artifact: a package that installs,
*loads*, and then fails inside StartController, which is the one recovery route
neither target has ever exercised. Build it first with

    set XHCI_EXTRA_DEFINES=-DXHCI_FAIL_START_CONTROLLER
    scripts\build-driver.cmd debug

This is the **only** exception to the do-not-deploy rule above, and it is narrow
by construction rather than by intention: the image must carry a *second*
marker, XHCI98_FAILSTART_ARTIFACT_TASK_12_3, which only that define emits. A
resource-size probe carries the first marker and not the second, so this switch
cannot be used to talk the gate into packaging any other diagnostic build - the
failure mode task 12.3 explicitly warned about ("a packaging gate that can be
talked into passing a broken driver is worth less afterwards than the clause is
worth").

It is also loud: the run prints a banner, and the default output directory is
out\pkg-failstart-<flavor>-<arch> rather than out\pkg-<flavor>-<arch>, so the artifact cannot
quietly occupy the path a real package is copied from.

The artifact must be built with XHCI_EXTRA_DEFINES holding that define and
nothing else. Both markers are present in any build that merely *includes*
-DXHCI_FAIL_START_CONTROLLER, so a mixed diagnostic build would satisfy this
switch's marker test while behaving like neither artifact. This switch cannot
tell the difference - a marker says what was defined, not what else was - so the
refusal lived at build time in three places (on branch 1.2.0.0): src\sources refused any
XHCI_EXTRA_DEFINES that was not exactly -DXHCI_FAIL_START_CONTROLLER (which bound
a bare `build` from a DDK prompt), scripts\build-driver.cmd refused it earlier
with a fuller message, and src\xhci_dispatch.c carried an #error for the one
other define this tree documents. Review finding 2, round 2 finding 4.

Mutually exclusive with -UnpaddedDriverVerExperiment.

.PARAMETER UnpaddedDriverVerExperiment
Stage roadmap task 12.4's experiment package: identical to the ordinary package
in every respect except that the INF's DriverVer date loses its leading zeros -
08/16/2026 becomes 8/16/2026, which is Microsoft's own form.

Windows 2000 records no driver date for this package at all, and stage B ruled
out the date *value*; what is left uncontrolled is that the package is unsigned
and that its date is zero-padded. This is the one-variable package that
separates them. If it records a date, padding is the cause and the fix is ours;
if it does not, signing is - and signing is out of scope, so that outcome is a
limitation rather than a defect.

The INF variant is derived here rather than kept in the tree, so there is no
second INF for src\xhci98.inf to drift from: the date is rewritten in place,
xhci98.rc is copied beside it so the version cross-check still runs, and the
gate is invoked with -AllowUnpaddedDriverVer, which relaxes the padding rule and
nothing else. Like the artifact above it is loud and lands in its own directory,
out\pkg-datefmt-<flavor>-<arch>.

Mutually exclusive with -FailStartArtifact: a package varying both the driver's
behaviour and the INF's date answers neither question.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\make-package.ps1

.EXAMPLE
powershell -File scripts\package\make-package.ps1 -Flavor release -OutDir E:\xhci98
#>

[CmdletBinding()]
param(
    [ValidateSet("debug", "release", "qemu")]
    [string]$Flavor = "debug",
    [ValidateSet("x86", "amd64")]
    [string]$Arch = "x86",
    [string]$OutDir = "",
    [string]$InfPath = "",
    [string]$DriverPath = "",
    [string]$UasInfPath = "",
    [string]$UasDriverPath = "",
    [switch]$SkipPackageGate,
    [switch]$SkipBinaryGates,
    [switch]$NoTargetEvidence,
    [switch]$FailStartArtifact,
    [switch]$UnpaddedDriverVerExperiment
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

$repo = Get-RepoRoot
#
# The three things -Arch moves, in one place. build-driver.cmd puts the amd64
# output at src\obj<flavour>\amd64 beside i386 rather than in a second obj
# root (roadmap task 21.2), so the only difference here is the last path
# component - and the INF, which is a whole second file.
#
$archDir = if ($Arch -eq "amd64") { "amd64" } else { "i386" }
$archInf = if ($Arch -eq "amd64") { "src\xhci98-amd64.inf" } else { "src\xhci98.inf" }
if ($InfPath -eq "") { $InfPath = Join-Path $repo $archInf }
#
# The UAS class driver (task 31-A.2), in both packages. Decided before
# -DriverPath is defaulted, because whether the caller named one is the
# question: see the -UasDriverPath help.
#
$stageUas = ($DriverPath -eq "") -or ($UasDriverPath -ne "")
$uasInfName = if ($Arch -eq "amd64") { "xhciuas-amd64.inf" } else { "xhciuas.inf" }
$uasAllow = if ($Arch -eq "amd64") { "xhciuas-imports-amd64.allow" } else { "xhciuas-imports.allow" }
if ($UasInfPath -eq "") { $UasInfPath = Join-Path $repo "src\uas\$uasInfName" }
if ($UasDriverPath -eq "") {
    $uasObjDir = switch ($Flavor) {
        "debug"   { "objchk" }
        "qemu"    { "objchk_qemu" }
        default   { "objfre" }
    }
    $UasDriverPath = Join-Path $repo "src\uas\$uasObjDir\$archDir\xhciuas.sys"
}
if ($DriverPath -eq "") {
    # Three flavours, two of them checked: "objchk" alone stopped identifying a
    # build when task 13-L.1 added qemu, which is exactly why it has a tree of
    # its own (scripts\build-driver.cmd :flavordirs).
    $objDir = switch ($Flavor) {
        "debug"   { "objchk" }
        "qemu"    { "objchk_qemu" }
        default   { "objfre" }
    }
    $DriverPath = Join-Path $repo "src\$objDir\$archDir\xhci98.sys"
}
if ($OutDir -eq "") {
    # A distinct default path for the artifact, so it cannot land where a real
    # package is copied to a VM from. Naming it is half of "narrow and loud".
    #
    # The architecture is in the directory name for both packages, x86
    # included. The moment a second one exists an untagged out\pkg-release
    # means "x86" without saying so, and the file inside it is called
    # xhci98.sys either way - so what a stale directory holds cannot be read
    # off its name. Free to do now: nothing under out\ is tracked or published.
    #
    if ($FailStartArtifact) {
        $OutDir = Join-Path $repo "out\pkg-failstart-$Flavor-$Arch"
    } elseif ($UnpaddedDriverVerExperiment) {
        $OutDir = Join-Path $repo "out\pkg-datefmt-$Flavor-$Arch"
    } else {
        $OutDir = Join-Path $repo "out\pkg-$Flavor-$Arch"
    }
}
# Anchor a relative path to PowerShell's location, not the process directory.
# [Path]::GetFullPath uses the latter, and Set-Location does not update it - so
# in any session that has changed directory since it started, a bare
# "-OutDir pkg" would resolve somewhere the caller never named.
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $PWD.ProviderPath $OutDir
}
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
#
# Strip a trailing separator, which GetFullPath preserves. The staging
# directory below is named by appending a suffix to this string, so with
# `-OutDir out\pkg-debug\` it would be `out\pkg-debug\.staging-xxx` - a *child*
# of the destination rather than a sibling, and the swap would then delete its
# own source. Review round 2 finding 1. A path root keeps its separator: "E:\"
# trimmed is "E:", which names the current directory on that drive.
#
if ($OutDir.Length -gt [System.IO.Path]::GetPathRoot($OutDir).Length) {
    $OutDir = $OutDir.TrimEnd('\')
}
#
# **And the volume-or-repository-root refusal is made HERE**, not at the swap
# (repo audit D6). The crafted message below is the one a caller who typed
# `-OutDir E:\` needs, and it was unreachable: staging runs first and calls
# `Ensure-Directory (Split-Path -Parent $OutDir)`, which for a volume root is
# `Ensure-Directory ""` - a parameter-binding error, with no explanation of what
# was actually wrong. Fail-closed either way; this is about which sentence the
# caller reads.
#
$outRootEarly = [System.IO.Path]::GetPathRoot($OutDir)
if ($OutDir.TrimEnd('\') -eq $outRootEarly.TrimEnd('\') -or
    $OutDir.TrimEnd('\') -eq $repo.TrimEnd('\')) {
    throw @"
refusing to package into '$OutDir': that is a volume or repository root, and
this script replaces its output directory wholesale. Name a subdirectory.
"@
}
#
# Nothing under releases\ either. A published flavour directory holds only
# xhci98.inf and xhci98.sys, both in the staged set, so the foreign-file check
# at the swap would let this script replace it with the package - and every
# version directory under releases\ is written once and never edited. When the
# package still carried Microsoft files (0.0.0.4 to 1.0.0.0) the cost was
# larger: .gitignore admits *.sys under releases\, so the next `git add -A`
# would have committed them. The compare is on the normalised strings, as
# make-release.ps1's Assert-UploadSetOutsideRelease does.
#
$releasesRoot = (Join-Path $repo "releases").TrimEnd('\')
if ($OutDir.TrimEnd('\') -eq $releasesRoot -or
    $OutDir.StartsWith($releasesRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw @"
refusing to package into '$OutDir': that is inside releases\, which git tracks
and whose version directories are written once and never edited. A package
there would replace a cut's files and ride into the next commit. Point -OutDir
at a git-ignored directory; out\ is the default.
"@
}

function Test-ImageMarker {
    param([string]$Path, [string]$Marker)

    $imageText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($Path))
    return $imageText.Contains($Marker)
}

function Test-ProbeBuildMarker {
    param([string]$Path)
    return Test-ImageMarker -Path $Path -Marker "XHCI98_PROBE_BUILD_DO_NOT_DEPLOY"
}

function Test-FailStartMarker {
    param([string]$Path)
    return Test-ImageMarker -Path $Path -Marker "XHCI98_FAILSTART_ARTIFACT_TASK_12_3"
}


$gateExtra = @()
$variantDir = ""
#
# Everything is staged here and moved into $OutDir only once every gate has
# passed. Review finding 1: the packager used to copy straight into
# $OutDir, so a run that failed *after* staging left a directory holding a
# mixture of the new files and whatever was there before - and the only thing
# that distinguishes a good package from a half-written one, once the run has
# scrolled away, is a hash. HANDOFF's preflight checks markers and the
# DriverVer line, and a mixed package passes both. So a failure now leaves the
# previous package exactly as it was, which is the state a recovery package has
# to be in to be worth having.
#
$stageDir = ""

try {
    Write-Step ("Package ({0}, {1}) -> {2}" -f $Flavor, $Arch, $OutDir)

    #
    # Review finding 4: the two special modes are not composable.
    # Each exists to vary exactly one thing against the ordinary package - the
    # driver's behaviour (12.3) or the INF's date padding (12.4) - and a package
    # varying both answers neither question while looking like it answers one.
    # They also disagree about where to land, so the combination silently used
    # the artifact's directory.
    #
    if ($FailStartArtifact -and $UnpaddedDriverVerExperiment) {
        throw @"
-FailStartArtifact and -UnpaddedDriverVerExperiment are mutually exclusive.
Each varies one thing against the ordinary package - a driver that fails inside
StartController (task 12.3), or an unpadded DriverVer date (task 12.4) - and a
package carrying both varies two, which measures neither. Build them one at a
time; they land in different directories on purpose.
"@
    }

    if (-not (Test-Path -LiteralPath $InfPath)) {
        throw "no INF at '$InfPath'"
    }

    #
    # Roadmap task 12.4's experiment package: the same INF with the DriverVer
    # date's leading zeros removed, and nothing else changed. Derived here
    # rather than committed, because a second INF in the tree is a second copy
    # of every rule this one carries and it would drift within a release.
    #
    if ($UnpaddedDriverVerExperiment) {
        $infText = [System.Text.Encoding]::ASCII.GetString(
            [System.IO.File]::ReadAllBytes($InfPath))
        $dv = [regex]::Matches($infText, '(?m)^\s*DriverVer\s*=\s*(\d{1,2})/(\d{1,2})/(\d{4})\s*,')
        if ($dv.Count -ne 1) {
            throw @"
'$InfPath' has $($dv.Count) DriverVer date line(s); this experiment rewrites
exactly one. Fix the INF rather than the rewrite - the gate requires one too.
"@
        }
        $padded = $dv[0].Groups[1].Value + "/" + $dv[0].Groups[2].Value + "/" + $dv[0].Groups[3].Value
        $unpadded = ("{0}/{1}/{2}" -f [int]$dv[0].Groups[1].Value,
                                      [int]$dv[0].Groups[2].Value,
                                      $dv[0].Groups[3].Value)
        if ($padded -eq $unpadded) {
            throw @"
the DriverVer date in '$InfPath' is already unpadded ($padded), so this switch
would produce a package byte-identical to the ordinary one. That is not an
experiment: task 12.4 needs the padding to be the single difference, and there
is nothing here to remove.
"@
        }

        # Rewrite in place, on the matched line only - the whole point is that
        # one field differs and everything else is byte-for-byte the shipping
        # INF, including its CRLF line endings and its ASCII encoding.
        $infText = $infText.Remove($dv[0].Index, $dv[0].Length).Insert(
            $dv[0].Index, $dv[0].Value.Replace($padded, $unpadded))

        $variantDir = Join-Path ([System.IO.Path]::GetTempPath()) `
            ("xhci98-datefmt-" + [System.IO.Path]::GetRandomFileName())
        Ensure-Directory $variantDir
        $variantInf = Join-Path $variantDir "xhci98.inf"
        [System.IO.File]::WriteAllBytes($variantInf,
            [System.Text.Encoding]::ASCII.GetBytes($infText))
        # The gate's DriverVer/FILEVERSION cross-check reads xhci98.rc from
        # beside the INF. Carry it along, or this package would be the one that
        # skips the check that ties the INF's version to the binary's.
        $rcSource = Join-Path (Split-Path -Parent $InfPath) "xhci98.rc"
        if (Test-Path -LiteralPath $rcSource) {
            Copy-Item -LiteralPath $rcSource -Destination (Join-Path $variantDir "xhci98.rc") -Force
        }

        $InfPath = $variantInf
        $gateExtra = @("-AllowUnpaddedDriverVer")

        Write-Host ""
        Write-Warn "UNPADDED-DriverVer EXPERIMENT - roadmap task 12.4, not install media."
        Write-Warn ("DriverVer date {0} -> {1}; nothing else differs from src\xhci98.inf." -f $padded, $unpadded)
        Write-Warn "The INF gate's zero-padding rule is relaxed for this run only, and for"
        Write-Warn "nothing but the padding. Install it on a Windows 2000 guest to see whether"
        Write-Warn "a Driver Date is recorded: if it is, padding is why ours is missing; if it"
        Write-Warn "is not, the remaining difference is that we are unsigned."
        Write-Host ""
    }
    if (-not (Test-Path -LiteralPath $DriverPath)) {
        throw @"
no $Flavor driver at '$DriverPath'.
Build it first: scripts\build-driver.cmd $Flavor
"@
    }
    #
    # The successor HCD's build scaffold (roadmap task 25.8) must never reach a
    # package: it is a DriverEntry that registers nothing. Refused first, by the
    # marker the image carries, so no later message can describe it as anything
    # else. Task 26-A.1 removed the marker from the source when the HCD became
    # stageable; the refusal stays for an old scaffold image left in an obj
    # directory.
    #
    if (Test-ImageMarker -Path $DriverPath -Marker "XHCI98_SCAFFOLD_DO_NOT_STAGE") {
        throw @"
'$DriverPath' is the build scaffold of the successor HCD (roadmap task 25.8)
and cannot be packaged. It registers nothing and drives no hardware. Rebuild:
since task 26-A.1 the source carries no scaffold marker.
"@
    }
    #
    # The image has to be the flavour that was asked for (task 13-L.1). Checked
    # here rather than inferred from the path, because -DriverPath can name a
    # binary anywhere and because "objchk_qemu" contains "objchk" - so a path
    # inference is wrong in exactly the direction that would stage a qemu binary
    # as debug. That is the mistake this marker exists to make impossible: both
    # are checked builds, VS_FF_DEBUG sets the same bit in each, and only one of
    # them may ever be published.
    #
    # Get-ImageFlavourMarker is in package-common.ps1, so make-release.ps1
    # reads the same answer the same way (task 13-L.1).
    $imageFlavour = Get-ImageFlavourMarker -Path $DriverPath
    if ($imageFlavour -eq "") {
        throw @"
'$DriverPath' carries no XHCI98_FLAVOUR_* marker, so what it is cannot be read
off the file. Every image built since task 13-L.1 has one - src\sources derives
it from BUILD_ALT_DIR and DriverEntry reads it so the linker keeps it - so this
is a stale binary from before that. Rebuild:
  scripts\build-driver.cmd $Flavor
"@
    }
    if ($imageFlavour -ne $Flavor) {
        throw @"
'$DriverPath' is the $imageFlavour build, but -Flavor $Flavor was asked for.
Staging one flavour under another's name is how a qemu binary - which carries
the port-0xE9 mirror and must never be published - reaches media labelled
debug. Rebuild the flavour you meant:
  scripts\build-driver.cmd $Flavor
"@
    }
    if ($Flavor -eq "qemu") {
        Write-Host ""
        Write-Warn "QEMU FLAVOUR - an emulator and bench build, NEVER PUBLISHED."
        Write-Warn "It carries the port-0xE9 mirror (HAL.dll!WRITE_PORT_UCHAR) and the live"
        Write-Warn "per-line trace. That import is the sole delta between 0.0.0.4's two"
        Write-Warn "published binaries, of which the debug one gave the ThinkPad E460 a Code 2"
        Write-Warn "under Windows 98 SE - so this package is for a guest and not for metal,"
        Write-Warn "and make-release.ps1 refuses it."
        Write-Warn ("Staging to: {0}" -f $OutDir)
        Write-Host ""
    }
    #
    # The do-not-deploy rule, and its one exception (roadmap task 12.3).
    #
    # The exception is keyed on a *second* marker rather than on the switch
    # alone, so what it admits is one named artifact and not "any build the
    # caller was willing to pass a flag for". Both directions are checked, and
    # both are failures rather than warnings: a diagnostic build without the
    # switch, and the switch without the artifact.
    #
    if ($FailStartArtifact) {
        if (-not (Test-FailStartMarker -Path $DriverPath)) {
            throw @"
-FailStartArtifact was passed, but '$DriverPath' does not carry
XHCI98_FAILSTART_ARTIFACT_TASK_12_3, so it is not roadmap task 12.3's artifact.
This switch admits that one artifact and nothing else - it is not a general
"package a diagnostic build" flag, and widening it here would give away the
gate the task asked to keep. Build the artifact:
  set XHCI_EXTRA_DEFINES=-DXHCI_FAIL_START_CONTROLLER
  scripts\build-driver.cmd $Flavor
"@
        }
        if (-not (Test-ProbeBuildMarker -Path $DriverPath)) {
            throw @"
'$DriverPath' carries the failed-start marker but not
XHCI98_PROBE_BUILD_DO_NOT_DEPLOY, which every diagnostic build must have
(src\sources). An artifact that has lost the do-not-deploy marker is one this
gate could not refuse if the switch were absent, so it is refused here instead.
"@
        }
        Write-Host ""
        Write-Warn "FAILED-START ARTIFACT - roadmap task 12.3, not install media."
        Write-Warn "This package installs and loads and then FAILS inside StartController,"
        Write-Warn "deliberately. It is for exercising start-time cleanup and recovery on a"
        Write-Warn "guest you are prepared to recover - never on a machine you need working."
        Write-Warn ("Staging to: {0}" -f $OutDir)
        Write-Host ""
    } elseif (Test-ProbeBuildMarker -Path $DriverPath) {
        throw @"
'$DriverPath' is a diagnostic probe build and cannot be packaged.
Clear XHCI_EXTRA_DEFINES and rebuild:
  set XHCI_EXTRA_DEFINES=
  scripts\build-driver.cmd $Flavor
"@
    }

    # --- the two gates that must have passed before a VM sees the binary -----
    #
    # build-driver.cmd runs both, but it is not the only way a .sys can appear
    # under src\obj*: scripts\local\ddk-debug.cmd gives an interactive DDK
    # prompt, and nothing stops a binary built there from being packaged. Run
    # them again here, where the media is actually assembled, so "gated" is a
    # property of the package rather than of how it was built (roadmap Phase 4
    # task 1). Both are seconds; a wrong binary on a guest is a reboot cycle at
    # best and a Win2000 bugcheck-and-recover at worst.
    if (-not $SkipBinaryGates) {
        Write-Step "host test suite"
        $hostTests = Join-Path $repo "test\run-host-tests.cmd"
        # ErrorActionPreference is relaxed around every native call in this
        # script for the reason make-release.ps1 gives: in Windows PowerShell
        # 5.1 a child's stderr line becomes an ErrorRecord when the caller
        # redirects with 2>&1, and under "Stop" the first one aborts before the
        # exit code is read, with an empty message. The exit code is the verdict.
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & cmd.exe /c "`"$hostTests`""
        } finally {
            $ErrorActionPreference = $savedEap
        }
        $hostRc = $LASTEXITCODE
        if ($hostRc -eq 2) {
            throw @"
the host test suite produced no result line, so it did not run.
That is Smart App Control blocking a freshly linked unsigned exe, not a test
failure (docs\contributing\lessons.md, "Smart App Control"). Run this again.
"@
        }
        if ($hostRc -ne 0) {
            throw @"
the host test suite failed. Do not package this binary: the suite covers the
common-buffer carve, the ring/cycle logic and the PORTSC masks, and every one
of those failures is silent on the target.
"@
        }

        Write-Step ("import gate ({0}, {1})" -f $Flavor, $Arch)
        $importGate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) `
            "import-gate") "check-imports.ps1"
        # -Arch is what selects the amd64 allowlist and baselines - and, more
        # to the point, what makes the gate read the image's own PE machine
        # word and refuse a mismatch. MSVC 6.0's dumpbin reads an amd64 image,
        # exits 0 and prints no import section at all, so without the arch the
        # gate would enforce the allowlist against an empty set and pass
        # (design record 11 section 8, "A dumper trap").
        $gateArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                      $importGate, "-Image", $DriverPath, "-Flavor", $Flavor,
                      "-Arch", $Arch)
        if ($NoTargetEvidence) { $gateArgs += "-NoTargetEvidence" }
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & powershell.exe @gateArgs
        } finally {
            $ErrorActionPreference = $savedEap
        }
        if ($LASTEXITCODE -ne 0) {
            throw @"
'$DriverPath' failed the import-compatibility gate.
An unresolved or wrong-module import stops the driver before DriverEntry with
no call-site diagnostic, and on Win98 the only symptom is a yellow bang that
looks exactly like a bad INF.
"@
        }
    }

    # --- the UAS class driver, held to the same gates (task 31-A.2) ---------
    #
    # After xhci98.sys's gates, so a stand-in refused by those is refused for
    # the reason the self-tests expect, and before anything is staged.
    if ($stageUas) {
        Write-Step ("UAS class driver ({0}, {1})" -f $Flavor, $Arch)
        if (-not (Test-Path -LiteralPath $UasDriverPath)) {
            throw @"
no $Flavor xhciuas.sys at '$UasDriverPath'.
The package carries the UAS class driver. Build it first:
  scripts\build-driver.cmd $Flavor
"@
        }
        $uasFlavour = Get-ImageFlavourMarker -Path $UasDriverPath
        if ($uasFlavour -ne $Flavor) {
            throw "'$UasDriverPath' carries the flavour marker '$uasFlavour', not $Flavor's. Rebuild: scripts\build-driver.cmd $Flavor"
        }
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            if (-not $SkipBinaryGates) {
                $uasGateArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                 (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "import-gate") "check-imports.ps1"),
                                 "-Image", $UasDriverPath, "-Flavor", $Flavor, "-Arch", $Arch,
                                 "-AllowPath", (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "import-gate") $uasAllow))
                if ($NoTargetEvidence) { $uasGateArgs += "-NoTargetEvidence" }
                & powershell.exe @uasGateArgs
                if ($LASTEXITCODE -ne 0) {
                    throw "'$UasDriverPath' failed the import-compatibility gate against $uasAllow."
                }
            }
            & powershell -NoProfile -ExecutionPolicy Bypass -File `
                (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-uas-inf.ps1") `
                -InfPath $UasInfPath -Arch $Arch
            if ($LASTEXITCODE -ne 0) {
                throw "'$UasInfPath' failed scripts\inf-gate\check-uas-inf.ps1 - fix it before packaging."
            }
        } finally {
            $ErrorActionPreference = $savedEap
        }
    } else {
        Write-Warn "no UAS class driver staged: -DriverPath was named without -UasDriverPath (a stand-in package)."
    }

    # --- where each file goes, from the gate's own parse ---------------------
    Write-Step "INF gate, and the media layout it derives"

    $gate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-inf.ps1"
    $layoutFile = Join-Path ([System.IO.Path]::GetTempPath()) ("xhci98-layout-" + [System.IO.Path]::GetRandomFileName())
    try {
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
                -InfPath $InfPath -Arch $Arch -EmitMediaLayout $layoutFile `
                @gateExtra
        } finally {
            $ErrorActionPreference = $savedEap
        }
        if ($LASTEXITCODE -ne 0) {
            throw "'$InfPath' failed scripts\inf-gate\check-inf.ps1 - fix it before packaging."
        }
        $layout = Read-MediaLayout -Path $layoutFile
    } finally {
        if (Test-Path -LiteralPath $layoutFile) {
            Remove-Item -LiteralPath $layoutFile -Force -ErrorAction SilentlyContinue
        }
    }

    # Every [SourceDisksFiles] entry needs a source here, or setup would look
    # for a file the package cannot contain. xhci98.inf is also staged at the
    # package root unconditionally - that is the file the user points setup at.
    $staging = @{ "xhci98.inf" = $InfPath; "xhci98.sys" = $DriverPath }

    $unsourced = @($layout.Keys | Where-Object { -not $staging.ContainsKey($_) })
    if ($unsourced.Count -gt 0) {
        throw @"
[SourceDisksFiles] in '$InfPath' names file(s) this script has no source for:
  - $($unsourced -join "`n  - ")
Add them to the staging table in scripts\package\make-package.ps1. A Microsoft
file is never one of them: since 1.0.0.1 the OS supplies usbd.sys and
usbhub.sys through the INF's LayoutFile, and the gate refuses them here.
"@
    }

    # Beside the destination rather than in %TEMP%, so the final move is a
    # rename within one volume and cannot half-succeed across a device boundary.
    Ensure-Directory (Split-Path -Parent $OutDir)
    $stageDir = $OutDir + ".staging-" + [System.IO.Path]::GetRandomFileName()
    Ensure-Directory $stageDir

    $mediaPaths = @{}
    foreach ($name in $staging.Keys) {
        $relative = if ($layout.ContainsKey($name)) { $layout[$name] } else { $name }
        $dest = Join-Path $stageDir $relative
        Ensure-Directory (Split-Path -Parent $dest)
        Copy-Item -LiteralPath $staging[$name] -Destination $dest -Force
        $mediaPaths[$name] = $dest
    }
    # The UAS pair goes to the package root, where xhciuas.inf's one
    # SourceDisksNames entry looks for both files.
    if ($stageUas) {
        Copy-Item -LiteralPath $UasInfPath -Destination (Join-Path $stageDir "xhciuas.inf") -Force
        Copy-Item -LiteralPath $UasDriverPath -Destination (Join-Path $stageDir "xhciuas.sys") -Force
    }
    $rootInf = Join-Path $stageDir "xhci98.inf"
    if (-not (Test-Path -LiteralPath $rootInf)) {
        Copy-Item -LiteralPath $InfPath -Destination $rootInf -Force
    }

    Write-Host ""
    foreach ($item in (Get-ChildItem -LiteralPath $stageDir -File -Recurse | Sort-Object FullName)) {
        $version = $item.VersionInfo.FileVersion
        if ([string]::IsNullOrWhiteSpace($version)) { $version = "-" }
        $shown = $item.FullName.Substring($stageDir.Length).TrimStart('\')
        Write-Host ("  {0,-22} {1,9} B  {2}" -f $shown, $item.Length, $version)
    }
    Write-Host ""

    #
    # **The version the media actually carries, checked against the INF beside
    # it.** `scripts\inf-gate\check-inf.ps1` ties `DriverVer` to the four fields
    # in `src\xhci98.rc`, but those are *sources*: nothing there can see the
    # version compiled into the `.sys` this package stages, which is the only
    # copy an installed machine reads. Bumping the INF and the resource without
    # rebuilding produces exactly the mismatch that gate claims to prevent - the
    # media installs, Windows 2000 accepts it as an upgrade, and the driver
    # reports the older build for ever afterwards.
    #
    # Run unconditionally rather than under -SkipPackageGate: that switch exists
    # for the packager's own self-tests, which stage stand-in binaries, and this
    # check is skipped for a file with no version resource at all rather than by
    # a flag.
    #
    $stagedDriver = $mediaPaths["xhci98.sys"]
    $stagedVersion = (Get-Item -LiteralPath $stagedDriver).VersionInfo.FileVersion
    if ([string]::IsNullOrWhiteSpace($stagedVersion)) {
        #
        # **A hard failure for a real driver binary, not a warning.**
        # `src\xhci98.rc` is in SOURCES from task 8-A.4, so every correctly built
        # binary carries a version - which makes "no version resource" mean
        # precisely one thing: this .sys was built before that task and is stale
        # by at least a whole batch. Warning and packaging it anyway would let
        # through the worst form of the mismatch this check exists to catch: an
        # old binary under a new INF, with nothing in the media to say which
        # build it is.
        #
        # `-SkipBinaryGates` is the one case where it is not a defect, and that
        # is what the switch already means: the packager's own self-tests stage
        # stand-in files rather than driver builds. Note the *comparison* below
        # is not gated on it - a stand-in that does carry a version is still
        # checked, which is what keeps the negative control for this rule honest.
        #
        if (-not $SkipBinaryGates) {
            throw @"
the staged xhci98.sys carries no version resource, so its build cannot be tied
to the INF at all. src\xhci98.rc has been in src\sources since task 8-A.4, so
this binary predates it. Rebuild with scripts\build-driver.cmd before packaging.
"@
        }
        Write-Warn "the staged xhci98.sys carries no version resource (-SkipBinaryGates: assumed to be a stand-in, not a driver build)."
    } else {
        $infLine = (Get-Content -LiteralPath $rootInf) |
            Where-Object { $_ -match '^\s*DriverVer\s*=' } | Select-Object -First 1
        if ($infLine -notmatch '=\s*[^,]+,\s*([\d.]+)') {
            throw "the staged xhci98.inf has no readable DriverVer to check the binary against."
        }
        $infVersion = $matches[1].Trim()
        # The comparison itself lives in package-common.ps1 so that
        # scripts\package\test-package.ps1 can drive it on strings - there is no
        # stand-in binary in the tree carrying a chosen version resource, so the
        # comparison rather than the whole path is the testable unit.
        if (-not (Test-DriverVersionMatches -Reported $stagedVersion -Declared $infVersion)) {
            throw @"
the staged binary and the staged INF disagree about the version:
  - xhci98.sys FileVersion $stagedVersion
  - xhci98.inf DriverVer   $infVersion
Rebuild with scripts\build-driver.cmd before packaging. Windows 2000 ranks
drivers by DriverVer, so this media would install as an upgrade and then report
a build that was never made.
"@
        }
        Write-Ok ("xhci98.sys {0} matches the INF's DriverVer" -f $stagedVersion)
    }

    if (-not $SkipPackageGate) {
        Write-Step "INF gate against the staged package"
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & powershell -NoProfile -ExecutionPolicy Bypass -File $gate `
                -InfPath $rootInf -Arch $Arch -PackageDir $stageDir `
                @gateExtra
        } finally {
            $ErrorActionPreference = $savedEap
        }
        if ($LASTEXITCODE -ne 0) {
            throw "the staged package failed scripts\inf-gate\check-inf.ps1 - do not install it."
        }
        if ($stageUas) {
            $savedEap = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                & powershell -NoProfile -ExecutionPolicy Bypass -File `
                    (Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-uas-inf.ps1") `
                    -InfPath (Join-Path $stageDir "xhciuas.inf") -PackageDir $stageDir -Arch $Arch
            } finally {
                $ErrorActionPreference = $savedEap
            }
            if ($LASTEXITCODE -ne 0) {
                throw "the staged package failed scripts\inf-gate\check-uas-inf.ps1 - do not install it."
            }
        }
    }

    #
    # Every gate has passed, so this is the first moment $OutDir may change.
    # The old package is removed and the staging directory renamed onto it -
    # the window in which neither exists is a rename, not a copy, and nothing
    # before this point could have touched what a VM installs from.
    #
    # **Replacing is not the same as merging, and -OutDir is a caller's path.**
    # The pre-staging packager copied *into* whatever was there, so pointing it
    # at a populated directory - the documented `-OutDir E:\xhci98` example is a
    # transfer volume - added files. Removing the destination outright would
    # instead delete the caller's other files, which is a worse failure than the
    # one the staging directory exists to prevent. So a destination this run did
    # not make is refused rather than replaced: a package directory is one whose
    # root holds xhci98.inf, an empty directory is fine, and a volume or
    # repository root is never either.
    #
    # **The volume/repository-root half is settled before anything is built**,
    # at the top of this script, so that a caller who names one is told before
    # paying for a host suite and a link. What is left here is the
    # foreign-directory question, which needs the staged package to exist.
    # There was a second copy of the root refusal in this block, unreachable
    # behind the early one and therefore untestable (the 2026-09-16 audit's
    # D6); the file-not-directory check below is NOT a duplicate and stays.
    #
    $retired = ""
    if (Test-Path -LiteralPath $OutDir) {
        if (-not (Test-Path -LiteralPath $OutDir -PathType Container)) {
            throw "'$OutDir' exists and is a file, not a directory."
        }
        #
        # **What counts as "a package this script made" is every entry, not one
        # of them.** Round 2 finding 2: testing only for xhci98.inf at the root
        # calls a transfer directory holding a package *and* a README.TXT ours,
        # and then deletes the README. So the destination is compared against
        # what this run just staged: anything it does not also contain is
        # somebody else's and stops the run.
        #
        $stagedRel = @{}
        foreach ($f in (Get-ChildItem -LiteralPath $stageDir -Recurse -Force)) {
            $stagedRel[$f.FullName.Substring($stageDir.Length).TrimStart('\')] = $true
        }
        $foreign = @()
        foreach ($f in (Get-ChildItem -LiteralPath $OutDir -Recurse -Force)) {
            $rel = $f.FullName.Substring($OutDir.Length).TrimStart('\')
            if (-not $stagedRel.ContainsKey($rel)) { $foreign += $rel }
        }
        if ($foreign.Count -gt 0) {
            throw @"
'$OutDir' holds file(s) this run did not stage:
  - $(($foreign | Select-Object -First 10) -join "`n  - ")
This script replaces its output directory rather than copying into it, so it
will not delete anything it cannot account for. Point -OutDir at a directory
that does not exist, or clear that one yourself.
"@
        }
        #
        # Retire by rename, not by delete. Round 2 finding 2: a recursive
        # delete mutates the old package one child at a time, so a single open
        # handle - Explorer, a scanner, a guest's vvfat mount - can leave it
        # half-removed with the replacement not yet in place. A rename either
        # happens or does not, and if the second one fails the first is put
        # back, so the destination is never a mixture of two packages.
        #
        $retired = $OutDir + ".previous-" + [System.IO.Path]::GetRandomFileName()
        Move-Item -LiteralPath $OutDir -Destination $retired -Force
    }
    try {
        Move-Item -LiteralPath $stageDir -Destination $OutDir -Force
    } catch {
        if ($retired -ne "") {
            Move-Item -LiteralPath $retired -Destination $OutDir -Force
            $retired = ""
        }
        throw
    }
    $stageDir = ""
    if ($retired -ne "") {
        # The replacement is in place, so this is now only disk space. A failure
        # to remove it must not fail a run whose package is already correct.
        Remove-Item -LiteralPath $retired -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $retired) {
            Write-Warn ("could not remove the previous package, left at '{0}'" -f $retired)
        }
    }

    Write-Step "Done"
    if ($FailStartArtifact) {
        Write-Warn "This is the failed-start artifact. It will install and then not start."
        Write-Host "Run it from a snapshot you can revert, and read the run sheet first:"
        Write-Host "  docs\contributing\build-and-test.md"
        Write-Host "    'Staging a driver that starts and fails (task 12.3)'"
        Write-Host "  which records what task 12.3's runs observed on both targets"
        Write-Host "  (Windows 98 does not survive it)."
        Write-Host ""
    }
    if ($UnpaddedDriverVerExperiment) {
        Write-Warn "This is the unpadded-date experiment package (task 12.4), for a Windows"
        Write-Warn "2000 guest only. Delete %SystemRoot%\inf\oemN.inf and its .pnf first, or"
        Write-Warn "the cached copy of the installed INF outranks it and nothing is measured."
        Write-Host "  The question was ANSWERED (roadmap task 12.4): the unpadded"
        Write-Host "  date records no DriverDate either, so padding is excluded and what remains"
        Write-Host "  is that the package is unsigned. Rebuilding this is re-running a settled"
        Write-Host "  experiment unless something about the INF or the target has changed."
        Write-Host ""
    }
    Write-Host "Copy '$OutDir' to the VM's transfer volume and install from it:"
    if ($Arch -eq "amd64") {
        #
        # The 32-bit targets are not offered here, and that is the point rather
        # than an omission: this media's INF is decorated .NTamd64 throughout,
        # so a 32-bit engine pointed at it finds no driver at all. Naming Win98
        # and Win2000 under an amd64 package would send someone to spend an
        # install cycle discovering that.
        #
        Write-Host "  XP x64   Device Manager -> the xHCI device -> Update Driver -> Have Disk"
        Write-Host "  2003 x64 the same; both are NT 5.2.3790 and one binary serves them"
        Write-Host "  Vista x64 / 7 x64  boot with F8 -> Disable Driver Signature Enforcement"
        Write-Host "           (every boot), then Device Manager -> Update Driver -> this directory"
        Write-Host ""
        #
        # This warning named XP x64 as the only amd64 guest and Vista x64 /
        # Windows 7 x64 as outside the tier until 2026-09-18; roadmap task 21.8
        # closed on 2026-09-16 with both in it, on an F8 boot only.
        #
        Write-Warn "Three guests have run an amd64 build: Windows XP x64 SP2 (both shipping"
        Write-Warn "flavours, roadmap task 21.5), Vista x64 SP2 and Windows 7 x64 SP1 (the qemu"
        Write-Warn "build, tasks 21.8 and 22.5). Windows Server 2003 x64 rests on being the same"
        Write-Warn "operating system as XP x64 and was never booted. On Vista x64 and 7 x64 the"
        Write-Warn "driver loads only on a boot with signature enforcement disabled from F8."
        Write-Warn "No amd64 build has ever run on real hardware. Treat a guest booted from this"
        Write-Warn "media as the experiment it is, and do not put it on a machine you need working."
    } else {
        Write-Host "  Win98    Device Manager -> the xHCI device -> Update Driver -> Specify a location"
        Write-Host "  Win2000  Device Manager -> the xHCI device -> Update Driver -> Have Disk"
    }
} catch {
    Write-Err $_.Exception.Message
    exit 1
} finally {
    if ($variantDir -ne "" -and (Test-Path -LiteralPath $variantDir)) {
        Remove-Item -LiteralPath $variantDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    # A run that threw after staging leaves this behind and $OutDir untouched.
    if ($stageDir -ne "" -and (Test-Path -LiteralPath $stageDir)) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit 0
