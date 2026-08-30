<#
.SYNOPSIS
Build the install media batch 11-V's package clauses need (roadmap task 11-B.3).

.DESCRIPTION
Task 11-V.3 asks for three things a single `make-package.ps1` run cannot give
it, and none of them can be produced during the VM session that consumes them:

  1. The **upgrade candidate** - the current package, at the current version.
  2. The **baseline** - the package one version older, which is what the upgrade
     is performed over. It is not built here: a package is a build plus a gated
     INF, and the honest older package is the one that was actually made at that
     version. This script checks for it and prints the exact commands to
     regenerate it from git if it is missing.
  3. The **2003-dated variant** - the same binary and the same version behind a
     `DriverVer` whose *date* half is Microsoft's own `2/15/2003`, zero-padded
     to satisfy this repository's INF-gate rule. That is the discriminating
     experiment for the open question in `docs\contributing\build-and-test.md`, "Versioning
     the driver": Windows 2000 shows Driver Date `Not available` for our
     package and `15-Feb-03` for Microsoft's EHCI on the same machine, and
     **three** uncontrolled differences remain - signed versus unsigned, the
     date value, and its zero-padding (this repository's INF-gate rule
     `^\d{2}/\d{2}/\d{4}` refuses Microsoft's unpadded `2/15/2003`, so the
     variant cannot control padding).

     What installing this variant onto a freshly uninstalled devnode does is
     change **one** variable against our own package: the date value. If the
     date then appears, padding and signing are both exonerated. If it does
     not, the date value is exonerated and signing and padding stay
     confounded - so it narrows the question rather than separating it, and
     `docs\contributing\runs\run-11v.md` stage B5 is where that is written out.

**The variant INF is generated, never kept as a second copy.** One edited
duplicate of `src\xhci98.inf` in the tree is a file that drifts from the real
one silently and is then installed on a VM to answer a question about the real
one. Only the `DriverVer` line is rewritten, the rewrite is asserted to have
changed exactly one line, and the result goes through `make-package.ps1` - and
therefore through the INF gate and the binary-version cross-check - like any
other media.

**Nothing here installs anything.** The output is directories under
`out\media-11v\`, which is git-ignored; `docs\contributing\runs\run-11v.md` is the sheet that
consumes them.

.PARAMETER Flavor
qemu (default), debug, release, both, or all. **`qemu` is the default because it
is the only flavour that produces a LIVE trace** - since task 13-L.1
 every `XHCI_DBG_*` site and the port-0xE9 mirror compile only into
`qemu`, so the `DriverEntry (built ...)` identity line
`docs\contributing\runs\run-11v.md` blocks on, and every clause that reads a
counter off a running guest through `readcounters.ps1`, need a `qemu` guest.
(The
*counters themselves* are in every flavour and `XHCISNAP` reads them from any of
the three; what `qemu` alone gives is the live channel this sheet's VM clauses
are written against.) `release` is what task 11-V.3 validates as the shipping
media; `debug` is the other shipping flavour and is what stage B6's upgrade legs
install. `both` is the two shipping flavours and `all` is the three, which is
`scripts\build-driver.cmd`'s vocabulary. **The import allowlist's `FLAVORS`
column is deliberately NOT the same list** - it refuses `both` outright and
takes release, debug, qemu or all - because a row written about two builds must
not silently cover three.

*(This parameter defaulted to `debug` and described it as "what a run reads
counters from" until the post-Phase 13 review rounds. That was true before the three-flavour split
and false after it, and it fails the way the vm-matrix harness failed: the guest boots and drives devices perfectly well, and the run then
dies as a timeout on an identity line that build cannot print.)*

.PARAMETER OutRoot
Where to build. Defaults to out\media-11v in the repository.

.PARAMETER BaselineVersion
The package version the upgrade is performed OVER. Defaults to 0.0.0.6, the
version cut before the current one. It is checked rather than assumed: the
staged baseline binary's own version resource must equal it, and it must be
lower than the current package version. A directory name is not evidence of
what is in it.

*(It defaulted to `1.0.0.2` - the version at the tip before batch 11-B - until
a later review, and by then the script could not run at all with its own defaults:
the project renumbered down to the `0.0.0.x` series, so `1.0.0.2` ranks ABOVE
the current package and the script refused it before packaging anything. The
refusal was correct and the default was stale. Bump both defaults with the
version, or the first thing a re-run of batch 11-V meets is an error about its
own parameters.)*

.PARAMETER BaselineCommit
The commit whose src\xhci98.inf and src\xhci98.rc carry -BaselineVersion, used
only in the regeneration recipe this script prints. There is no default: the
caller names the last commit whose `src\xhci98.inf` reads the baseline
version (`git log -S"0.0.0.6" -- src\xhci98.inf` finds it), and the recipe
shows a placeholder when none is given. It moves with -BaselineVersion; the
two are a pair and a mismatched one prints a recipe that regenerates the
wrong package.

.PARAMETER SkipBaselineCheck
Do not fail when the baseline package is absent. Use only when the run being
prepared does not include the upgrade leg.

.PARAMETER NoTargetEvidence
Passed through to make-package.ps1 for a host with no extracted target binaries
staged.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\make-11v-media.ps1

.EXAMPLE
powershell -File scripts\package\make-11v-media.ps1 -Flavor both
#>

[CmdletBinding()]
param(
    [ValidateSet("qemu", "debug", "release", "both", "all")]
    [string]$Flavor = "qemu",
    [string]$OutRoot = "",
    [string]$BaselineVersion = "0.0.0.6",
    [string]$BaselineCommit = "",
    [switch]$SkipBaselineCheck,
    [switch]$NoTargetEvidence
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
# Test-DriverVersionMatches lives here. A version resource can report
# "1, 0, 0, 2" where the INF says "1.0.0.2", and this repository already has one
# comparison that knows that - reusing it is the difference between a check and
# a second, subtly different check.
. (Join-Path $PSScriptRoot "package-common.ps1")

$repo = Get-RepoRoot
$srcInf = Join-Path $repo "src\xhci98.inf"
$packer = Join-Path $PSScriptRoot "make-package.ps1"

# The date the experiment turns on, and it is not arbitrary: it is the date on
# Windows 2000 SP4's own [EHCI.NT] section, i.e. the value the control in that
# open question is already displaying. Zero-padded because this repository's
# INF gate requires ^\d{2}/\d{2}/\d{4} - SP4's own "2/15/2003" would not pass
# it, which is itself one of the differences the experiment is separating.
$DATED_2003 = "02/15/2003"

if ($OutRoot -eq "") { $OutRoot = Join-Path $repo "out\media-11v" }
if (-not [System.IO.Path]::IsPathRooted($OutRoot)) {
    $OutRoot = Join-Path $PWD.ProviderPath $OutRoot
}
$OutRoot = [System.IO.Path]::GetFullPath($OutRoot)

# "both" is the two SHIPPING flavours and "all" is the three - never blur them,
# because a run that wanted the trace and got `debug` fails as a boot timeout
# with no hint of the cause (the vm-matrix break batch 13-L caused).
$flavors = switch ($Flavor) {
    "both" { @("debug", "release") }
    "all"  { @("qemu", "debug", "release") }
    default { @($Flavor) }
}

function Get-DriverVerLine {
    param([string]$Path)
    $line = @(Get-Content -LiteralPath $Path | Where-Object { $_ -match '^\s*DriverVer\s*=' })
    if ($line.Count -ne 1) {
        throw "'$Path' has $($line.Count) DriverVer lines; expected exactly one."
    }
    if ($line[0] -notmatch '^\s*DriverVer\s*=\s*([^,]+)\s*,\s*([\d.]+)\s*$') {
        throw "'$Path' has a DriverVer this script cannot parse: $($line[0])"
    }
    return @{ Line = $line[0]; Date = $matches[1].Trim(); Version = $matches[2].Trim() }
}

function New-DatedInf {
    # Rewrite only the date half of DriverVer, in place in a copy, and prove
    # that is all that changed. A silent no-op here would produce a "variant"
    # identical to the real package and an experiment that answers nothing.
    param([string]$SourcePath, [string]$DestPath, [string]$Date)

    $bytes = [System.IO.File]::ReadAllBytes($SourcePath)

    # **Refuse what this function cannot faithfully round-trip, rather than
    # transforming it.** Decoding as ASCII turns every byte above 0x7F into
    # '?', and the changed-line check below splits on CRLF, so an LF-only file
    # would be treated as one aggregate line and the check would prove nothing.
    # Both are conditions the INF gate already requires of src\xhci98.inf
    # (FILE-ENCODING, FILE-EOL) - so checking them here is cheap, and the
    # alternative is a variant INF that differs from its source in ways nobody
    # asked for and then gets installed on a VM to answer a question about the
    # source.
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -gt 0x7F) {
            throw ("'{0}' has a non-ASCII byte at offset {1}; this rewrite decodes as ASCII and would replace it with '?'." -f $SourcePath, $i)
        }
    }
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    # Every LF must be the tail of a CRLF. Checking only "contains no CRLF at
    # all" lets a **mixed** file through, and the changed-line comparison below
    # splits on CRLF - so a bare LF inside the file silently merges two source
    # lines into one and the "exactly one line changed" proof stops proving it.
    # Every CR must be followed by an LF and every LF preceded by a CR. Counting
    # only LF against CRLF catches a bare LF but not a bare **CR**, which an
    # earlier draft's "CRLF only" claim did not cover and which splits a logical
    # line for some readers while this rewrite's CRLF split does not see it.
    $lf = ([regex]::Matches($text, "`n")).Count
    $cr = ([regex]::Matches($text, "`r")).Count
    $crlf = ([regex]::Matches($text, "`r`n")).Count
    if ($lf -ne $crlf -or $cr -ne $crlf) {
        throw ("'{0}' is not CRLF-only ({1} CR, {2} LF, {3} CRLF); Win98's setup engine wants CRLF and this rewrite's own check splits on it." -f $SourcePath, $cr, $lf, $crlf)
    }
    $original = $text
    $text = [regex]::Replace($text,
        '(?m)^(\s*DriverVer\s*=\s*)[^,\r\n]+(\s*,)',
        ('${1}' + $Date + '${2}'))
    if ($text -eq $original) {
        throw "the DriverVer date rewrite changed nothing in '$SourcePath'."
    }
    $before = ($original -split "`r`n")
    $after = ($text -split "`r`n")
    if ($before.Count -ne $after.Count) {
        throw "the DriverVer date rewrite changed the line count."
    }
    $changed = 0
    for ($i = 0; $i -lt $before.Count; $i++) {
        if ($before[$i] -ne $after[$i]) { $changed++ }
    }
    if ($changed -ne 1) {
        throw "the DriverVer date rewrite changed $changed lines; exactly one was expected."
    }
    # ASCII/CRLF, because Win98's 16-bit engine requires both and reports
    # neither (docs\contributing\build-and-test.md, "Win98 INF-parser traps"). The read and
    # the write are both byte-level for that reason.
    [System.IO.File]::WriteAllBytes($DestPath, [System.Text.Encoding]::ASCII.GetBytes($text))
}

function Invoke-Packager {
    param([string]$FlavorName, [string]$Out, [string]$Inf = "")
    $a = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $packer,
           "-Flavor", $FlavorName, "-OutDir", $Out)
    if ($Inf -ne "") { $a += @("-InfPath", $Inf) }
    if ($NoTargetEvidence) { $a += "-NoTargetEvidence" }
    & powershell.exe @a
    if ($LASTEXITCODE -ne 0) {
        throw "make-package.ps1 failed for '$Out'."
    }
}

try {
    $current = Get-DriverVerLine -Path $srcInf
    Write-Step ("batch 11-V media -> {0}" -f $OutRoot)
    Write-Host ("  current package: {0}, {1}" -f $current.Date, $current.Version)

    # ---- the baseline relationship, checked BEFORE anything is built --------
    #
    # **This comparison used to sit after the packaging loop**, which made a
    # refusal expensive and its description wrong: the default invocation built
    # both flavours twice, ran every gate in make-package.ps1 including the full
    # host suite, wrote staged output into $OutRoot, and only then threw. The
    # inputs are two strings from files already read, so there is nothing to
    # learn from building first. Fail closed, immediately, and leave no
    # half-staged tree behind. (Found by an audit, which caught
    # build-and-test.md describing the script as one that "no longer runs".)
    #
    # Four parts are **required**, not merely expected. System.Version stores a
    # missing Build or Revision as -1, so "1.0.0" sorts below "1.0.0.0" while
    # Test-DriverVersionMatches zero-extends it and calls a binary reporting
    # 1.0.0.0 a match - an equal package could then pass as older through one
    # check and as the baseline through the other. The scheme this repository
    # records is four-part; anything else is refused rather than interpreted.
    #
    # One comparison, on parsed values, so that an alternative spelling of the
    # same version - "1.0.0.03" against "1.0.0.3" - is caught as equal rather
    # than sliding past a textual inequality test.
    foreach ($v in @(@{ N = "-BaselineVersion"; V = $BaselineVersion },
                     @{ N = "the INF's DriverVer"; V = $current.Version })) {
        if ($v.V -notmatch '^\d+\.\d+\.\d+\.\d+$') {
            throw ("{0} is '{1}'; this script compares four-part versions only (docs\contributing\build-and-test.md, `"Versioning the driver`")." -f $v.N, $v.V)
        }
    }
    try {
        $baselineParsed = [System.Version]::Parse($BaselineVersion)
        $currentParsed = [System.Version]::Parse($current.Version)
    } catch {
        throw ("cannot compare versions: -BaselineVersion '{0}' or the INF's '{1}' did not parse." -f $BaselineVersion, $current.Version)
    }
    if ($baselineParsed -ge $currentParsed) {
        throw ("-BaselineVersion {0} is not older than the current package version {1}. The upgrade leg needs a package the setup engine will rank below this one; bump src\xhci98.inf and src\xhci98.rc, or pass the real predecessor. (Nothing has been built or staged - this is checked before any packaging runs.)" -f $BaselineVersion, $current.Version)
    }
    Write-Ok ("baseline {0} ranks below the current package {1}" -f $BaselineVersion, $current.Version)

    Ensure-Directory (Split-Path -Parent $OutRoot)
    Ensure-Directory $OutRoot

    $work = Join-Path $OutRoot ".work"
    Ensure-Directory $work

    $built = New-Object System.Collections.ArrayList

    foreach ($f in $flavors) {
        Write-Step ("upgrade candidate ({0})" -f $f)
        $dir = Join-Path $OutRoot ("new-{0}-{1}" -f $current.Version, $f)
        Invoke-Packager -FlavorName $f -Out $dir
        [void]$built.Add($dir)

        Write-Step ("2003-dated variant ({0})" -f $f)
        $variantInf = Join-Path $work ("xhci98-{0}-2003.inf" -f $f)
        New-DatedInf -SourcePath $srcInf -DestPath $variantInf -Date $DATED_2003
        $check = Get-DriverVerLine -Path $variantInf
        if ($check.Date -ne $DATED_2003 -or $check.Version -ne $current.Version) {
            throw ("the variant INF reads '{0},{1}'; expected '{2},{3}'." -f `
                $check.Date, $check.Version, $DATED_2003, $current.Version)
        }
        $dir = Join-Path $OutRoot ("dated2003-{0}-{1}" -f $current.Version, $f)
        Invoke-Packager -FlavorName $f -Out $dir -Inf $variantInf
        [void]$built.Add($dir)
    }

    # ---- the baseline, which is checked for rather than built ---------------
    #
    # An older package is an older build. Rebuilding one from the current
    # sources under an older version string would produce a package that lies
    # about which binary it carries, which is precisely the failure
    # make-package.ps1's own version cross-check exists to prevent.
    Write-Step "upgrade baseline"

    # The version relationship was checked at the top, before anything was
    # built. What is left here is the *artifact* half of the same question: a
    # directory called old-1.0.0.2-debug is not evidence that the binary in it
    # is 1.0.0.2.
    $missing = New-Object System.Collections.ArrayList
    foreach ($f in $flavors) {
        # **There is no qemu baseline and there never was one.** The baseline is
        # the package the upgrade is performed OVER, taken from the commit that
        # actually built $BaselineVersion - and the three-flavour split is task
        # 13-L.1, long after it. A qemu package has also never been
        # published, so nothing could have been installed for one to upgrade
        # over. Stage B's upgrade legs are about the two shipping flavours;
        # qemu is staged here for the trace the later stages read.
        if ($f -eq "qemu") { continue }

        $baseline = Join-Path $OutRoot ("old-{0}-{1}" -f $BaselineVersion, $f)
        $sys = Join-Path $baseline "xhci98.sys"
        if (-not (Test-Path -LiteralPath $sys)) {
            [void]$missing.Add($baseline)
            continue
        }

        # **Three properties, because the setup engine reads more than one file
        # and a directory name is evidence of none of them.** An earlier draft
        # checked only the binary's version: a baseline holding an old .sys
        # beside a current INF would have passed while presenting Setup with a
        # DriverVer it ranks as equal to the upgrade candidate's, and a
        # debug/release swap would have passed too.
        $item = Get-Item -LiteralPath $sys
        $v = $item.VersionInfo.FileVersion
        if (-not (Test-DriverVersionMatches -Reported $v -Declared $BaselineVersion)) {
            throw @"
'$sys' reports FileVersion '$v', not the expected baseline '$BaselineVersion'.
A stale or mis-staged binary under a correctly named directory passes every
other check here and invalidates the upgrade experiment silently. Regenerate
the baseline with the recipe below.
"@
        }

        $baselineInf = Join-Path $baseline "xhci98.inf"
        if (-not (Test-Path -LiteralPath $baselineInf)) {
            throw "'$baseline' has no xhci98.inf; it is a directory of files, not a package."
        }
        $baselineDv = Get-DriverVerLine -Path $baselineInf
        if ($baselineDv.Version -ne $BaselineVersion) {
            throw @"
'$baselineInf' declares DriverVer version '$($baselineDv.Version)', not
'$BaselineVersion'. Windows 2000 ranks candidates by the INF, not by the
binary, so a baseline whose INF is the current one is not a baseline at all.
"@
        }

        # VS_FF_DEBUG is set by src\xhci98.rc under #if DBG, so the binary
        # itself says whether it is a CHECKED build - no naming convention
        # required. It says no more than that: since task 13-L.1 both `debug`
        # and `qemu` are checked and both set it, which is why the image also
        # carries an XHCI98_FLAVOUR_* marker and why scripts\check-flavour-
        # marker.ps1 exists. It is enough here only because the qemu case is
        # skipped above, and a baseline predates the marker anyway.
        $isDebug = $item.VersionInfo.IsDebug
        $wantDebug = ($f -ne "release")
        if ($isDebug -ne $wantDebug) {
            throw @"
'$sys' is the $(if ($isDebug) { "debug" } else { "release" }) build, but it is staged as the $f baseline.
The flavour is read from VS_FF_DEBUG in the binary's own version resource, not
from the directory name. A swapped pair would upgrade one flavour over the
other and prove nothing about either.
"@
        }

        Write-Ok ("baseline present and authenticated: {0} (xhci98.sys {1}, INF {2}, {3})" -f `
            $baseline, $v, $baselineDv.Version, $(if ($isDebug) { "debug" } else { "release" }))
    }
    if ($missing.Count -gt 0) {
        if ($BaselineCommit -eq "") { $BaselineCommit = "<commit whose src\xhci98.inf reads $BaselineVersion>" }
        $how = @"
the upgrade baseline package(s) are not staged:
  - $($missing -join "`n  - ")
The baseline is the package as it was actually built at version
$BaselineVersion, so it is regenerated from git rather than rebuilt from these
sources under an older version string:

  git checkout $BaselineCommit -- src
  scripts\build-driver.cmd both
  powershell -File scripts\package\make-package.ps1 -Flavor debug   -OutDir out\media-11v\old-$BaselineVersion-debug
  powershell -File scripts\package\make-package.ps1 -Flavor release -OutDir out\media-11v\old-$BaselineVersion-release
  git checkout HEAD -- src
  scripts\build-driver.cmd all

COMMIT OR STASH src FIRST. Both checkouts overwrite the index and the working
tree, so uncommitted work under src is destroyed with no warning from git. If
you would rather not touch this tree at all, use a worktree instead - it needs
its own generated import library:

  git worktree add ..\xhci98-baseline $BaselineCommit
  cd ..\xhci98-baseline
  scripts\make-usbport-lib.cmd
  scripts\build-driver.cmd both
  ...package from there, then: cd - ; git worktree remove ..\xhci98-baseline

Check out the WHOLE of src, not just xhci98.inf and xhci98.rc. Those two carry
the version and nothing else, so checking out only them builds TODAY'S driver
wearing an old version number - which is exactly the lying package the comment
above this recipe says a baseline must not be, and it is what this recipe said
to do until the post-Phase 13 review rounds. Only 'both' is needed for the baseline because a
baseline is a shipping flavour; the restore says 'all' so this tree gets its
qemu build back.

Verified end to end: checkout, build, package, and the staged
result is accepted by this script's own baseline check as the baseline / debug.

Re-run this script afterwards, or pass -SkipBaselineCheck if the run being
prepared has no upgrade leg.
"@
        if ($SkipBaselineCheck) {
            Write-Warn $how
        } else {
            throw $how
        }
    }

    Write-Step "Done"
    foreach ($d in $built) { Write-Host ("  {0}" -f $d) }
    Write-Host ""
    Write-Host "docs\contributing\runs\run-11v.md says which stage installs which of these."
} catch {
    Write-Err $_.Exception.Message
    exit 1
}

exit 0
