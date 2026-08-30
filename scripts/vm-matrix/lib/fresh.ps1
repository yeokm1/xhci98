# The post-release run's own rules (Phase 16, task 16.1), as functions the
# runner, the preparation script and the self-test share.
#
# The design is docs\contributing\design\09-post-release-unattended-run.md.
# Everything here is a refusal or a reading that record states, and nothing
# here decides anything it does not.  Four of the refusals in its section 6
# are about a qcow2 image and can be checked without booting one, so they are
# functions that take a snapshot list rather than an image, and the self-test
# feeds them lists it made up.
#
# The runner's own decisions that need no guest live here too, at the end of
# the file: which targets a run boots, the refusal of a writable image, the
# two-leg row loop, the target verdict, the report file, and the preparation
# script's clone and stamp refusals.  They are functions so the self-test can
# drive each one without a boot; run-matrix.ps1 and prepare-image.ps1 call
# them and add nothing to what they decide.

# The version under test, from the single source the packager and the INF
# gate read (src\xhci_version.h).  A stamp carries this string, and the run
# compares the stamp against it, so both sides read the same line.
function Get-DriverVersionUnderTest {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $path = Join-Path $RepoRoot "src\xhci_version.h"
    if (-not (Test-Path -LiteralPath $path)) { throw ("version header not found: {0}" -f $path) }
    foreach ($line in (Get-Content -LiteralPath $path)) {
        if ($line -match '^\s*#define\s+XHCI_VER_STR\s+"(\d+\.\d+\.\d+\.\d+)"\s*$') { return $Matches[1] }
    }
    throw ("{0} has no readable XHCI_VER_STR (a bare four-part version in quotes)." -f $path)
}

# `qemu-img snapshot -l` as records, in the order the image lists them, which
# is creation order.  The last record is the newest snapshot.
function Get-ImageSnapshots {
    param(
        [Parameter(Mandatory = $true)][string]$QemuImg,
        [Parameter(Mandatory = $true)][string]$Image
    )
    if (-not (Test-Path -LiteralPath $Image)) { throw ("image not found: {0}" -f $Image) }
    $text = Invoke-NativeText -Exe $QemuImg -Arguments @("snapshot", "-l", $Image)
    # A qemu-img that could not open the image (held by a running QEMU, a
    # missing DLL, a corrupt file) prints its reason and exits nonzero, and
    # its empty listing must not be read as "no snapshots": that turned every
    # such failure into "carries no base- stamp" (repo audit S-5).
    if ($LASTEXITCODE -ne 0) {
        throw ("qemu-img snapshot -l failed on {0} (exit {1}): {2}" -f $Image, $LASTEXITCODE, $text.Trim())
    }
    return (ConvertFrom-SnapshotList -Text $text)
}

# The parser is separate from the call so the self-test can hand it text.
# A line is `<id> <tag> <vm_size> <date> <time> <vm_clock> [<icount>]`, and the
# tag is taken as the second whitespace-separated token: this repository's
# tags carry no spaces (`pre-phase10-prep-2026-08-11`), and the stamp format
# below has none either.
function ConvertFrom-SnapshotList {
    param([string]$Text)
    $out = @()
    if ($null -eq $Text) { return $out }
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*(\d+)\s+(\S+)\s+(\S+\s*\S*?)\s+(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})') {
            $out += [pscustomobject]@{ Id = [int]$Matches[1]; Tag = $Matches[2]; Date = $Matches[4] }
        }
    }
    return $out
}

# The stamp's name.  `base-<DriverVer>-<flavour>`, taken as the last act of the
# preparation.  One function composes it and one parses it, so the two cannot
# drift apart.
function Get-BaseStampName {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [string]$Flavour = "qemu"
    )
    return ("base-{0}-{1}" -f $Version, $Flavour)
}

function ConvertFrom-BaseStampName {
    param([string]$Tag)
    if ($null -eq $Tag) { return $null }
    if ($Tag -match '^base-(\d+\.\d+\.\d+\.\d+)-([a-z]+)$') {
        return [pscustomobject]@{ Tag = $Tag; Version = $Matches[1]; Flavour = $Matches[2] }
    }
    return $null
}

# The image names the run must never boot, whatever their snapshots say.
# Design record 09 section 6: Phase 10's images are not fresh and the run has
# no way to make them so.  Compared by leaf name, case-insensitively, because
# that is how they are named in every config this project has had.
$script:ForbiddenFreshImages = @('win98.img', 'win2k.img', 'win2k-smp.img')

# THE STAMP CHECK.  Returns the list of reasons this image must not be booted
# by the post-release run; an empty list means it may.  Every clause is one
# refusal from design record 09 section 3.3 or section 6, and each has a
# self-test case in selftest.ps1 that shows it firing.
function Get-FreshImageProblems {
    param(
        [Parameter(Mandatory = $true)][string]$ImagePath,
        [Parameter(Mandatory = $true)]$Snapshots,
        [Parameter(Mandatory = $true)][string]$Version,
        [string]$Flavour = "qemu"
    )
    $problems = @()
    $leaf = Split-Path -Leaf $ImagePath

    if ($script:ForbiddenFreshImages -contains $leaf.ToLowerInvariant()) {
        $problems += ("{0} is one of Phase 10's carried-along images and is never booted by this run, whatever its snapshots say" -f $leaf)
        return $problems
    }

    $snaps = @($Snapshots)
    $stamps = @($snaps | Where-Object { $null -ne (ConvertFrom-BaseStampName -Tag $_.Tag) })
    if ($stamps.Count -eq 0) {
        $problems += ("{0} carries no base-<version>-<flavour> stamp, so it was never prepared for a post-release run (or is one of Phase 10's images under another name)" -f $leaf)
        return $problems
    }

    # The newest snapshot must BE the stamp.  Anything persisted after the
    # preparation finished means the image is no longer the state the stamp
    # names, and the stamp is taken last for this reason.
    $newest = $snaps[$snaps.Count - 1]
    $stamp = ConvertFrom-BaseStampName -Tag $newest.Tag
    if ($null -eq $stamp) {
        $problems += ("{0}'s newest snapshot is '{1}', not its base- stamp ('{2}'): something was persisted after the preparation finished, and the image is no longer the state the stamp names" -f `
            $leaf, $newest.Tag, $stamps[$stamps.Count - 1].Tag)
        return $problems
    }
    if ($stamp.Version -ne $Version) {
        $problems += ("{0} was prepared for {1} and the build under test is {2}; a base image prepared for one release may not be used to judge another" -f `
            $leaf, $stamp.Version, $Version)
    }
    if ($stamp.Flavour -ne $Flavour) {
        $problems += ("{0}'s stamp says the {1} flavour was installed; this run reads the {2} flavour's identity line and counters, and no other flavour produces them" -f `
            $leaf, $stamp.Flavour, $Flavour)
    }
    return $problems
}

# WHICH MATRIX KEYS APPLY TO A TARGET.  The matrix's per-target entries
# (`ExcludedOnTarget`, `ExpectByTarget`, `MayWedgeGuest`, `ExpectNoDriver`) are
# keyed by target id, and a fresh target is the same operating system as the
# Phase 10 target it was cloned from: the tablet install hangs QEMU on a fresh
# Windows 98 exactly as on the taught one.  A fresh target therefore names the
# target whose entries it inherits (`Like = '2a'`), and a lookup tries its own
# id first and the inherited one second, so an entry written for the fresh id
# alone still wins.
function Get-TargetKeys {
    param([Parameter(Mandatory = $true)]$Target)
    $keys = @([string]$Target.Id)
    if ($Target.ContainsKey('Like') -and -not [string]::IsNullOrWhiteSpace($Target.Like)) {
        $keys += [string]$Target.Like
    }
    return $keys
}

# The first key of the target's that a per-target table holds, or $null.
function Find-TargetKey {
    param(
        [Parameter(Mandatory = $true)]$Table,
        [Parameter(Mandatory = $true)]$Target
    )
    if ($null -eq $Table) { return $null }
    foreach ($k in (Get-TargetKeys -Target $Target)) {
        if ($Table.ContainsKey($k)) { return $k }
    }
    return $null
}

function Test-TargetInList {
    param($List, [Parameter(Mandatory = $true)]$Target)
    if ($null -eq $List) { return $false }
    $keys = Get-TargetKeys -Target $Target
    foreach ($k in ([string[]]$List)) { if ($keys -contains $k) { return $true } }
    return $false
}

# A target is a post-release target when its config says where it was cloned
# from.  That key is what separates the two kinds of run: the ordinary matrix
# never boots one of these, and the post-release run boots nothing else.
function Test-FreshTarget {
    param([Parameter(Mandatory = $true)]$Target)
    return ($Target.ContainsKey('CloneFrom') -and $null -ne $Target.CloneFrom)
}

# `ExpectNoDriver = @{ '<target>' = '<reason>' }` on a row: this operating
# system, freshly installed, is known to carry no class driver for the model.
# Design record 09 section 4.2.  The outcome word does not change - the row is
# still NODRIVER by derivation - only whether it counts against the target.
function Get-RowNoDriverReason {
    param($Row, [Parameter(Mandatory = $true)]$Target)
    if ($null -eq $Row -or -not $Row.ContainsKey('ExpectNoDriver')) { return $null }
    $k = Find-TargetKey -Table $Row.ExpectNoDriver -Target $Target
    if ($null -eq $k) { return $null }
    $why = [string]$Row.ExpectNoDriver[$k]
    if ([string]::IsNullOrWhiteSpace($why)) { return $null }
    return $why
}

# A typo in the key or an empty reason is a problem at validation time, before
# a boot is spent - the same rule MayWedgeGuest has.
function Get-RowNoDriverProblems {
    param($Row, [string[]]$KnownKeys)
    $out = @()
    if ($null -eq $Row -or -not $Row.ContainsKey('ExpectNoDriver')) { return $out }
    foreach ($k in @($Row.ExpectNoDriver.Keys)) {
        if ($KnownKeys -notcontains $k) {
            $out += ("row {0}: ExpectNoDriver names '{1}', which is neither a target in this configuration nor one a target inherits from, so it declares nothing" -f $Row.Name, $k)
        }
        if ([string]::IsNullOrWhiteSpace([string]$Row.ExpectNoDriver[$k])) {
            $out += ("row {0}: ExpectNoDriver['{1}'] has no reason; the entry is printed in the report and an empty one reads as nothing" -f $Row.Name, $k)
        }
    }
    return $out
}

# THE REPLUG LEG'S VERDICT.  Design record 09 section 5.1: the reattach is
# expected to reach the same outcome the first attach did.  A row whose first
# attach opened endpoints must open them again; a row that resolved NODRIVER
# must resolve NODRIVER again; either crossing over is a FAIL on the row.  An
# ERROR on either leg is an ERROR, because a reading was not taken.  This
# combines two outcomes and invents no new one.
function Get-ReplugOutcome {
    param(
        [Parameter(Mandatory = $true)][string]$First,
        [Parameter(Mandatory = $true)][string]$Second
    )
    if ($First -eq "ERROR" -or $Second -eq "ERROR") {
        return [pscustomobject]@{ Outcome = "ERROR"; Why = "a reading was not taken on one of the two attach legs" }
    }
    if ($First -eq "FAIL" -or $Second -eq "FAIL") {
        $leg = if ($First -eq "FAIL") { "first attach" } else { "replug" }
        return [pscustomobject]@{ Outcome = "FAIL"; Why = ("an expectation did not hold on the {0}" -f $leg) }
    }
    if ($First -ne $Second) {
        return [pscustomobject]@{
            Outcome = "FAIL"
            Why = ("the replug reached {0} where the first attach reached {1}; a device that comes back differently from how it arrived is the reading this leg exists to take" -f $Second, $First)
        }
    }
    return [pscustomobject]@{ Outcome = $First; Why = "" }
}

# WHAT COUNTS AGAINST A TARGET'S VERDICT in the post-release run.  Design
# record 09 sections 4.1, 4.2 and 5: FAIL counts; ERROR counts unless the
# group ended on a row the matrix declared may wedge this target, which is the
# composite row's pinned reading on Windows 98; NODRIVER counts unless the row
# carries an ExpectNoDriver entry for the target; PASS, INERT and EXCLUDED do
# not.  Returns $true when the row counts against the verdict.
function Test-RowCountsAgainst {
    param(
        [Parameter(Mandatory = $true)][string]$Outcome,
        [bool]$NoDriverExpected = $false,
        [bool]$WedgeDeclared = $false
    )
    switch ($Outcome) {
        "FAIL"     { return $true }
        "ERROR"    { return (-not $WedgeDeclared) }
        "NODRIVER" { return (-not $NoDriverExpected) }
        default    { return $false }
    }
}

# The header block that makes two releases' reports diffable against each
# other (design record 09 section 9).  Every variable thing is here and
# nothing variable is in the body.
function New-PostReleaseHeader {
    param(
        [Parameter(Mandatory = $true)][string]$TargetId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$DriverLine,
        [Parameter(Mandatory = $true)][string]$ImageLine,
        [Parameter(Mandatory = $true)][string]$QemuVersion,
        [Parameter(Mandatory = $true)][string]$Accel,
        [Parameter(Mandatory = $true)][int]$Sizeof,
        [Parameter(Mandatory = $true)][int]$Counters,
        [Parameter(Mandatory = $true)][datetime]$Started,
        [Parameter(Mandatory = $true)][timespan]$Elapsed,
        [Parameter(Mandatory = $true)][string]$Verdict,
        [Parameter(Mandatory = $true)][int]$Rows,
        [Parameter(Mandatory = $true)][int]$NoDriverExpected,
        [Parameter(Mandatory = $true)][int]$NotReached
    )
    $h = @()
    $h += "# post-release run"
    $h += ("# driver:    {0}" -f $DriverLine)
    $h += ("# image:     {0}" -f $ImageLine)
    $h += ("# qemu:      {0}, accel {1}" -f $QemuVersion, $Accel)
    $h += ("# offsets:   SIZEOF {0}, {1} counters" -f $Sizeof, $Counters)
    $h += ("# started:   {0}, elapsed {1}" -f $Started.ToString("yyyy-MM-dd HH:mm:ss"), $Elapsed.ToString("h\:mm\:ss"))
    $h += ("# verdict:   {0} {1}, {2} rows, {3} NODRIVER expected, {4} not reached" -f `
        $TargetId, $Verdict, $Rows, $NoDriverExpected, $NotReached)
    $h += "#"
    $h += "# Outcomes: PASS FAIL NODRIVER INERT ERROR EXCLUDED - see docs/contributing/design/06-device-matrix-verdict.md"
    $h += "# A '-> X' in the outcome column marks the expectation that did not hold."
    $h += "# A row name ending in '/replug' is the same device attached a second time - design record 09 section 5."
    $h += "#"
    return $h
}

# ------------------------------------------------------------- the runner ---

# WHICH TARGETS A RUN BOOTS.  The two kinds of run boot disjoint sets: a fresh
# target (one with `CloneFrom`) is the post-release run's and nobody else's,
# because its verdict rules differ, and the post-release run boots nothing
# else, because Phase 10's images are not fresh.  Naming a target from the
# other set with -Target is an error, not a silent skip.  Throws with the
# reason; returns the targets to boot.
function Select-RunTargets {
    param(
        [Parameter(Mandatory = $true)]$Targets,
        [bool]$PostRelease = $false,
        [string[]]$Requested = @()
    )
    $pool = @($Targets | Where-Object { (Test-FreshTarget -Target $_) -eq $PostRelease })
    $poolWord = if ($PostRelease) { "fresh (CloneFrom) targets" } else { "Phase 10 targets" }
    if ($pool.Count -eq 0) {
        throw ("this config has no {0}. {1}" -f $poolWord, $(if ($PostRelease) {
            "Add 2a-fresh and 2b-fresh as scripts\vm-matrix\config.sample.psd1 shows, then prepare their images (scripts\vm-matrix\README.md, the post-release section)."
        } else { "" }))
    }
    $Requested = @($Requested | Where-Object { $null -ne $_ -and $_ -ne "" })
    if ($Requested.Count -eq 0) { return $pool }
    $chosen = @($pool | Where-Object { $Requested -contains $_.Id })
    $wrongKind = @($Targets | Where-Object { $Requested -contains $_.Id -and $pool -notcontains $_ })
    if ($wrongKind.Count -gt 0) {
        throw ("-Target '{0}' names a target the {1} does not boot ({2}). {3}" -f `
            (($wrongKind | ForEach-Object { $_.Id }) -join ","), $(if ($PostRelease) { "post-release run" } else { "ordinary matrix" }),
            $(if ($PostRelease) { "it has no CloneFrom, so it is one of Phase 10's images" } else { "it is a fresh target, which only -PostRelease boots" }),
            $(if ($PostRelease) { "" } else { "Pass -PostRelease." }))
    }
    if ($chosen.Count -eq 0) {
        throw ("-Target '{0}' matched no target. This config's {1}: {2}" -f `
            ($Requested -join ","), $poolWord, (($pool | ForEach-Object { $_.Id }) -join ", "))
    }
    return $chosen
}

# Design record 09 section 6: the post-release run never writes to the image
# it boots, and a config that turns Snapshot off is refused rather than
# overridden quietly.  Returns the problem text, or $null when the config is
# acceptable.
function Get-SnapshotOffProblem {
    param(
        [Parameter(Mandatory = $true)]$Config,
        [Parameter(Mandatory = $true)][string]$TargetId
    )
    if ($Config.ContainsKey('Snapshot') -and -not [bool]$Config.Snapshot) {
        return ("target {0}: the config turns Snapshot off, and the post-release run never writes to the image it boots. Set Snapshot = `$true (or remove it) and re-run." -f $TargetId)
    }
    return $null
}

# ONE LEG, OR TWO.  The ordinary matrix attaches once; the post-release run
# attaches, detaches and attaches again (design record 09 section 5), and
# each leg is read and judged on its own.  A leg that took no reading ends the
# row: its replug would measure a bus the first leg left in an unknown state.
#
# The callbacks keep the runner's own work (the monitor, the counters, the
# report lines, the screenshots) out of this function, so the self-test can
# drive the loop with stand-ins:
#   RunLeg     param($Leg, $LegName)             -> an object with .After (null = no reading) and .Error
#   JudgeLeg   param($Leg, $LegName, $LegResult) -> the leg's outcome word
#   OnLegError param($Leg, $LegName, $LegResult) (optional), called before the row ends on that leg
# Returns the row's outcome, the replug rule's reason when the legs disagree,
# and the per-leg outcomes actually taken.
function Invoke-RowLegs {
    param(
        [Parameter(Mandatory = $true)][string]$RowName,
        [Parameter(Mandatory = $true)][int]$LegCount,
        [Parameter(Mandatory = $true)][scriptblock]$RunLeg,
        [Parameter(Mandatory = $true)][scriptblock]$JudgeLeg,
        [scriptblock]$OnLegError = $null
    )
    $legOutcomes = @()
    for ($leg = 1; $leg -le $LegCount; $leg++) {
        $legName = if ($leg -eq 1) { $RowName } else { $RowName + "/replug" }
        $legResult = & $RunLeg $leg $legName
        if ($null -eq $legResult -or $null -eq $legResult.After) {
            if ($null -ne $OnLegError) { & $OnLegError $leg $legName $legResult }
            $legOutcomes += "ERROR"
            break
        }
        $legOutcomes += [string](& $JudgeLeg $leg $legName $legResult)
    }
    # The row's outcome: one leg's, or the two legs combined by the replug
    # rule.  Nothing new is invented; a mismatch between the legs is a FAIL,
    # and either leg's ERROR is the row's.
    $rowOutcome = $legOutcomes[0]
    $why = ""
    if ($LegCount -gt 1) {
        $combined = Get-ReplugOutcome -First $legOutcomes[0] -Second $(if ($legOutcomes.Count -gt 1) { $legOutcomes[1] } else { "ERROR" })
        $rowOutcome = $combined.Outcome
        $why = $combined.Why
    }
    return [pscustomobject]@{ Outcome = $rowOutcome; Why = $why; LegOutcomes = $legOutcomes }
}

# A TARGET'S VERDICT from its tally.  A target on which no row was evaluated
# is a FAIL, not an empty pass; otherwise any row that counted against it is
# a FAIL.
function Get-TargetVerdict {
    param([Parameter(Mandatory = $true)]$Tally)
    if ([int]$Tally.Rows -eq 0) { return "FAIL" }
    if ([int]$Tally.Against -gt 0) { return "FAIL" }
    return "PASS"
}

# The per-target report file: the header block, the column line, then this
# target's slice of the row lines.  Returns the path written.
function Write-PostReleaseReport {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Header,
        [Parameter(Mandatory = $true)][string]$ColumnLine,
        [AllowEmptyCollection()][string[]]$Body = @()
    )
    $lines = @()
    $lines += $Header
    $lines += $ColumnLine
    if ($null -ne $Body) { $lines += $Body }
    Set-Content -LiteralPath $Path -Value $lines -Encoding utf8
    return $Path
}

# -------------------------------------------------- prepare-image's refusals ---

# -Clone's refusals, from facts the caller has already read: the source's
# snapshot list, and whether the destination exists.  An empty list means the
# clone may proceed.
function Get-CloneProblems {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Tag,
        [Parameter(Mandatory = $true)][bool]$SourceExists,
        [AllowEmptyCollection()]$SourceSnapshots = @(),
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][bool]$DestinationExists,
        [bool]$FreshCopy = $false
    )
    $problems = @()
    if (-not $SourceExists) {
        $problems += ("clone source not found: {0}" -f $Source)
        return $problems
    }
    $have = @(@($SourceSnapshots) | Where-Object { $_.Tag -eq $Tag })
    if ($have.Count -eq 0) {
        $problems += ("{0} has no snapshot named '{1}'. Its snapshots: {2}" -f $Source, $Tag, `
            ((@($SourceSnapshots) | ForEach-Object { $_.Tag }) -join ", "))
    }
    if ($DestinationExists -and -not $FreshCopy) {
        $problems += ("{0} already exists. A half-prepared fresh image is not silently replaced - pass -FreshCopy to clone over it, or stamp it if it is finished." -f $Destination)
    }
    return $problems
}

# -Stamp's refusals.  A stamp says what was installed, and the last prep
# boot's debug console is the only witness this side of a boot that anything
# was; the image the stamp is written to must be the one that boot ran, or the
# stamp names an install the file does not hold (repo audit S-4).  An empty
# list means the stamp may be taken.
# -CopyBack's refusals, kept pure so the self-test can drive them. The work
# copy is named by the paths file's second line; a copy back is refused while
# the prep guest still holds it, when there is no work copy to copy, and when
# the last boot already ran the vm-dir file (nothing to copy back).
function Get-CopyBackProblems {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][bool]$PortFree,
        [Parameter(Mandatory = $true)][string]$Image,
        [string]$BootedImage = "",
        [Parameter(Mandatory = $true)][bool]$BootedExists
    )
    $problems = @()
    if (-not $PortFree) {
        $problems += ("the prep guest is still listening on {0}. Shut it down from inside Windows first; a work copy a running QEMU is writing to is not a state to copy back." -f $Port)
        return $problems
    }
    if ([string]::IsNullOrWhiteSpace($BootedImage)) {
        $problems += "no prep boot is recorded for this target (the paths file has no image line): boot with -Boot -WorkDir first."
        return $problems
    }
    if ($BootedImage.Trim() -eq $Image) {
        $problems += ("the last prep boot ran {0} itself, not a work copy: there is nothing to copy back. Stamp directly." -f $Image)
        return $problems
    }
    if (-not $BootedExists) {
        $problems += ("the work copy the last boot ran is gone: {0}" -f $BootedImage.Trim())
    }
    return $problems
}

# The effectful half of -CopyBack: copy the work copy over the vm-dir image,
# then rewrite the paths file's second line to the destination. The record is
# written only after the copy succeeded, so a failed copy leaves -Stamp
# refusing on the old record (the vm-dir file still holds the pre-install
# state). Its own function so the self-test can run it on scratch files.
function Invoke-CopyBackRecord {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$PathsFile
    )
    $lines = @(Get-Content -LiteralPath $PathsFile)
    Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
    Set-Content -LiteralPath $PathsFile -Value @([string]$lines[0], $Destination) -Encoding ascii
}

function Get-StampProblems {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][bool]$PortFree,
        [Parameter(Mandatory = $true)][string]$Image,
        [Parameter(Mandatory = $true)][bool]$ImageExists,
        [string]$BootedImage = "",
        [Parameter(Mandatory = $true)][string]$DebugconLog,
        $IdentSize = $null,
        [Parameter(Mandatory = $true)][int]$TableSizeof
    )
    $problems = @()
    if (-not $PortFree) {
        $problems += ("the prep guest is still listening on {0}. Shut it down from inside Windows first; a stamp on an image a running QEMU is writing to names a state that does not exist yet." -f $Port)
        return $problems
    }
    if (-not $ImageExists) {
        $problems += ("fresh image not found: {0} (run -Clone first)" -f $Image)
        return $problems
    }
    if (-not [string]::IsNullOrWhiteSpace($BootedImage) -and ($BootedImage.Trim() -ne $Image)) {
        $problems += ("the last prep boot ran {0}, not {1}: the install it witnessed is in the work copy, and the stamp would name a state this file does not hold. Run -CopyBack first (it copies the work copy over this file and records that), then stamp." -f $BootedImage.Trim(), $Image)
        return $problems
    }
    if ($null -eq $IdentSize) {
        $problems += ("no MiniPortExtensionSize in {0}: the last prep boot never showed the qemu build running, so there is nothing to stamp as installed. Boot with -Boot -Xfer, install, restart the guest, confirm with -Status, shut down, then stamp." -f $DebugconLog)
        return $problems
    }
    if ([int]$IdentSize -ne $TableSizeof) {
        $problems += ("the last prep boot ran a driver with MiniPortExtensionSize={0} and the offset table says SIZEOF {1}: that is not the build under test. Reinstall from a package built from this tree (or regenerate the offsets), and confirm with -Status before stamping." -f $IdentSize, $TableSizeof)
    }
    return $problems
}
