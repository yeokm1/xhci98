<#
.SYNOPSIS
Record the gated binaries of one flavour (roadmap-usb4.md SIGN.1).

.DESCRIPTION
Called by scripts\build-driver.cmd, five ways, and refused from anywhere
else. The record it ends with is out\gated-<arch>-<flavor>.txt
(package-common.ps1, "The gated build's manifest"), which
make-package.ps1 -SignOnly reads.

  -RunStart when the run takes its lock, before any self-test: the commit
            and whether the tree had uncommitted changes, in the lock's
            start.txt (no -Flavor)
  -Begin    when a flavour's build starts: deletes the last record, and
            writes out\gated-<arch>-<flavor>.pending with the run's token
  -Hash     right before one binary's import gate: its SHA-256 and path
  -Capture  right after that gate passes: the file hashed again, which must
            equal the -Hash value; that value is the binary's capture
  -Finish   once every gate of the flavour has passed: every expected binary
            captured, each file re-hashed and still equal to its capture, the
            tree read again; the record is written from the CAPTURED hashes
            and the pending file deleted

With -Amend (build-driver.cmd's Stor98-only run, roadmap-usb4 task 48.9),
-Begin keeps the last record, -Hash and -Capture take Stor98's images
alone, and -Finish replaces their hashes in that record - only when it was
written at the commit the run started at, from a clean tree, and the tree
is still clean at that commit; any other record is deleted, since it names
Stor98 images the tree no longer holds and its other binaries were gated
at another commit or from another tree. With no record, nothing is
written.

build-driver.cmd holds out\gated-<arch>.lock for its whole run, with the
run's token in run.txt and in XHCI98_GATED_RUN; each mode refuses unless the
two agree with each other and with the pending file. build-driver.cmd
deletes the pending files when the run ends, so a flavour that fails leaves
no record and nothing half-made.

The record says dirty=0 only when the tree was clean at -RunStart and at
-Finish, at the same commit.

What this protects against is a mismatch by accident and another process
on the same machine changing a binary while the build runs. It is not a
defence against someone who deliberately forges these files.

.PARAMETER Arch
x86 or amd64, as build-driver.cmd builds.

.PARAMETER Flavor
release or qemu (debug was retired with task 45.45); every mode but -RunStart.

.PARAMETER Name
-Hash and -Capture: the binary's file name (xhci98.sys, xhciuas.sys,
xhcicm.sys, xhcihlpr.sys, stor98.sys, stor98.mpd, xhcinet.sys, xhcinet6.sys).

.PARAMETER Image
-Hash and -Capture: the path of the binary the import gate reads.

.PARAMETER NoTargetEvidence
-Finish: the build's import gates ran with -NoTargetEvidence.

.PARAMETER Amend
-Begin and -Finish: the Stor98-only run's record (above).
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86", "amd64")]
    [string]$Arch,
    [ValidateSet("", "release", "qemu")]
    [string]$Flavor = "",
    [switch]$RunStart,
    [switch]$Begin,
    [switch]$Hash,
    [switch]$Capture,
    [switch]$Finish,
    [string]$Name = "",
    [string]$Image = "",
    [switch]$NoTargetEvidence,
    [switch]$Amend
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

function Read-Pending {
    param([string]$Path)
    $p = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        $eq = $line.IndexOf("=")
        if ($eq -lt 1) { continue }
        $p[$line.Substring(0, $eq)] = $line.Substring($eq + 1)
    }
    return $p
}

function Add-PendingLines {
    param([string]$Path, [string[]]$Lines)
    [System.IO.File]::AppendAllText($Path, (($Lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
}

try {
    if (@(@($RunStart, $Begin, $Hash, $Capture, $Finish) | Where-Object { $_ }).Count -ne 1) {
        throw "give exactly one of -RunStart, -Begin, -Hash, -Capture and -Finish."
    }
    if (($Flavor -eq "") -ne [bool]$RunStart) {
        throw "-Flavor goes with every mode but -RunStart."
    }
    if ($Amend -and -not ($Begin -or $Finish)) {
        throw "-Amend goes with -Begin and -Finish."
    }
    $repo = Get-RepoRoot
    $lockRun = Join-Path $repo ("out\gated-{0}.lock\run.txt" -f $Arch)
    $lockStart = Join-Path $repo ("out\gated-{0}.lock\start.txt" -f $Arch)

    # Only from the build-driver.cmd run that holds this architecture's lock.
    $token = "$env:XHCI98_GATED_RUN".Trim()
    if ($token -eq "" -or -not (Test-Path -LiteralPath $lockRun -PathType Leaf) -or
        ([System.IO.File]::ReadAllText($lockRun).Trim() -ne $token)) {
        throw "this records a gated build and is run by scripts\build-driver.cmd while it holds '$(Split-Path -Parent $lockRun)'; it does not certify binaries on its own."
    }

    if ($RunStart) {
        $tree = Get-GitTreeState -Repo $repo
        [System.IO.File]::WriteAllText($lockStart, "", [System.Text.Encoding]::ASCII)
        Add-PendingLines -Path $lockStart -Lines @(
            "head_start=$($tree.Head)",
            ("dirty_start=" + $(if ($tree.Dirty) { "1" } else { "0" }))
        )
        exit 0
    }

    $record = Get-GatedManifestPath -Repo $repo -Arch $Arch -Flavor $Flavor
    $pending = [System.IO.Path]::ChangeExtension($record, ".pending")
    if ($Begin) {
        if (-not $Amend -and (Test-Path -LiteralPath $record)) { Remove-Item -LiteralPath $record -Force }
        if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending -Force }
        [System.IO.File]::WriteAllText($pending, "", [System.Text.Encoding]::ASCII)
        Add-PendingLines -Path $pending -Lines @("run=$token")
        if ($Amend) { Add-PendingLines -Path $pending -Lines @("amend=1") }
        exit 0
    }

    if (-not (Test-Path -LiteralPath $pending -PathType Leaf)) {
        throw "no '$pending': the $Flavor $Arch build did not begin a record in this run."
    }
    $p = Read-Pending -Path $pending
    if ($p["run"] -ne $token) {
        throw "'$pending' belongs to another build-driver.cmd run."
    }
    $amending = $p["amend"] -eq "1"
    if ($Finish -and ([bool]$Amend -ne $amending)) {
        throw $(if ($amending) { "this flavour's record was begun with -Amend; finish it with -Amend." } else { "-Finish -Amend needs a record begun with -Amend." })
    }

    if ($Hash -or $Capture) {
        $key = $Name.ToLowerInvariant()
        if ($key -notmatch '^((xhci98|xhciuas|xhcicm|xhcihlpr|stor98|xhcinet|xhcinet6)\.sys|stor98\.mpd)$') { throw "-Name '$Name' is not a binary this build gates." }
        if ($amending -and $key -notmatch '^stor98\.(sys|mpd)$') { throw "-Name '$Name': the Stor98-only run gates Stor98's images alone." }
        if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { throw "no $key at '$Image'." }
        $full = (Resolve-Path -LiteralPath $Image).ProviderPath
        $now = Get-FileSha256 -Path $full
        if ($Hash) {
            if ($p.ContainsKey("pre.$key")) { throw "$key was hashed twice in one flavour's build." }
            Add-PendingLines -Path $pending -Lines @("pre.$key=$now", "path.$key=$full")
            exit 0
        }
        if (-not $p.ContainsKey("pre.$key")) { throw "$key was not hashed before its import gate." }
        if ($p.ContainsKey("sha256.$key")) { throw "$key was captured twice in one flavour's build." }
        if ($p["path.$key"] -ne $full) { throw "$key was hashed at '$($p["path.$key"])' and captured at '$full'." }
        if ($now -ne $p["pre.$key"]) {
            throw "$key at '$full' changed while its import gate ran (SHA-256 $($p["pre.$key"]) before, $now after); the gate's verdict is not on these bytes."
        }
        Add-PendingLines -Path $pending -Lines @("sha256.$key=$now")
        exit 0
    }

    # -Finish: every expected binary captured, and each still its capture.
    # Stor98's images (task 42-A.30): stor98.mpd and the x86 stor98.sys on
    # x86, the amd64 stor98.sys on amd64, built once per run from src\stor98
    # and gated in every flavour's record. Thunderbolt networking's two images
    # on both architectures (task 45.59, record 35 section 9.5).
    $expected = @("xhci98.sys", "xhciuas.sys", "xhcicm.sys", "stor98.sys", "xhcinet.sys", "xhcinet6.sys")
    if ($Arch -eq "x86") { $expected += @("xhcihlpr.sys", "stor98.mpd") }
    if ($amending) {
        $expected = @("stor98.sys")
        if ($Arch -eq "x86") { $expected += "stor98.mpd" }
    }
    $sha = @{}
    foreach ($n in $expected) {
        if (-not $p.ContainsKey("sha256.$n")) {
            throw "$n was not captured after its import gate in this build."
        }
        $now = Get-FileSha256 -Path $p["path.$n"]
        if ($now -ne $p["sha256.$n"]) {
            throw "$n at '$($p["path.$n"])' changed after its import gate passed (SHA-256 $now, captured $($p["sha256.$n"])); no record is written."
        }
        $sha[$n] = $p["sha256.$n"]
    }
    # Clean only if the tree is still what it was when the run took its
    # lock, before any self-test ran, and was clean then.
    if (-not (Test-Path -LiteralPath $lockStart -PathType Leaf)) { throw "no '$lockStart': the run did not note the tree's state when it started." }
    $s = Read-Pending -Path $lockStart
    $end = Get-GitTreeState -Repo $repo
    $dirty = ($s["dirty_start"] -ne "0") -or $end.Dirty -or ($s["head_start"] -ne $end.Head)
    if ($amending) {
        Remove-Item -LiteralPath $pending -Force
        if (-not (Test-Path -LiteralPath $record -PathType Leaf)) {
            Write-Host ("  no {0} {1} record to amend: Stor98's images are gated and stamped, and no record names them" -f $Flavor, $Arch)
            exit 0
        }
        $why = ""
        try {
            $old = ConvertFrom-GatedManifest -Text ([System.IO.File]::ReadAllText($record))
        } catch {
            $old = $null
            $why = "it cannot be read ($($_.Exception.Message))"
        }
        if ($null -ne $old) {
            if ($old.Arch -ne $Arch -or $old.Flavor -ne $Flavor) { $why = "it records the $($old.Flavor) $($old.Arch) build" }
            elseif ($old.Dirty -ne "0") { $why = "its build ran in a tree with uncommitted changes" }
            elseif ($dirty -or $end.Head -eq "none") { $why = "this run's tree had uncommitted changes, or its commit moved or could not be read" }
            elseif ($old.Commit -ne $end.Head.ToLowerInvariant()) { $why = "it was written at commit $($old.Commit), and this run is at $($end.Head)" }
            else {
                foreach ($n in $expected) {
                    if (-not $old.Sha256.ContainsKey($n)) { $why = "it names no $n" }
                }
            }
        }
        if ($why -ne "") {
            Remove-Item -LiteralPath $record -Force
            Write-Warn ("{0} deleted: {1}. It named Stor98 images the tree no longer holds; a full build-driver.cmd run writes a new one." -f $record, $why)
            exit 0
        }
        $shaNew = @{}
        foreach ($k in $old.Sha256.Keys) { $shaNew[$k] = $old.Sha256[$k] }
        foreach ($n in $expected) { $shaNew[$n] = $sha[$n] }
        $text = ConvertTo-GatedManifest -Arch $Arch -Flavor $Flavor -Commit $old.Commit -Dirty $false `
            -TargetEvidence ($old.TargetEvidence -eq "1") `
            -Time ([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")) -Sha256 $shaNew
        $tmp = $record + ".tmp"
        [System.IO.File]::WriteAllBytes($tmp, [System.Text.Encoding]::ASCII.GetBytes($text))
        Move-Item -LiteralPath $tmp -Destination $record -Force
        Write-Ok ("gated build record amended (Stor98 alone): {0}" -f $record)
        foreach ($n in $expected) { Write-Host ("  {0,-13} {1}" -f $n, $sha[$n]) }
        exit 0
    }
    $text = ConvertTo-GatedManifest -Arch $Arch -Flavor $Flavor -Commit $end.Head -Dirty $dirty `
        -TargetEvidence (-not $NoTargetEvidence) `
        -Time ([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")) -Sha256 $sha
    $tmp = $record + ".tmp"
    [System.IO.File]::WriteAllBytes($tmp, [System.Text.Encoding]::ASCII.GetBytes($text))
    Move-Item -LiteralPath $tmp -Destination $record -Force
    Remove-Item -LiteralPath $pending -Force
    Write-Ok ("gated build recorded: {0}" -f $record)
    foreach ($n in $expected) { Write-Host ("  {0,-13} {1}" -f $n, $sha[$n]) }
    $state = if ($end.Head -eq "none") { "no commit (git could not say)" }
             elseif ($dirty) { "$($end.Head), with uncommitted changes during the build" }
             else { $end.Head }
    Write-Host ("  commit        {0}" -f $state)
    if ($dirty -or $end.Head -eq "none") {
        Write-Warn "make-package.ps1 -SignOnly refuses this record: build from a clean, committed tree to sign without re-running the gates."
    }
} catch {
    Write-Err $_.Exception.Message
    exit 1
}
exit 0
