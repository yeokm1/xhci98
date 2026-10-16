<#
.SYNOPSIS
Record the gated binaries of one flavour (roadmap-usb4.md SIGN.1).

.DESCRIPTION
Called by scripts\build-driver.cmd, three ways, and refused from anywhere
else. The record it ends with is out\gated-<arch>-<flavor>.txt
(package-common.ps1, "The gated build's manifest"), which
make-package.ps1 -SignOnly reads.

  -Begin    when the flavour's build starts: deletes the last record, and
            writes out\gated-<arch>-<flavor>.pending with the run's token,
            the commit and whether the tree had uncommitted changes
  -Capture  right after one binary's import gate passes: that binary's
            SHA-256 and path, appended to the pending file
  -Finish   once every gate of the flavour has passed: every expected binary
            captured, each file re-hashed and still equal to its capture, the
            tree read again; the record is written from the CAPTURED hashes
            and the pending file deleted

build-driver.cmd holds out\gated-<arch>.lock for its whole run, with the
run's token in run.txt and in XHCI98_GATED_RUN; each mode refuses unless the
two agree with each other and with the pending file. build-driver.cmd
deletes the pending files when the run ends, so a flavour that fails leaves
no record and nothing half-made.

The record says dirty=0 only when the tree was clean at -Begin and at
-Finish, at the same commit.

What this protects against is a mismatch by accident and another process
on the same machine changing a binary while the build runs. It is not a
defence against someone who deliberately forges these files.

.PARAMETER Arch
x86 or amd64, as build-driver.cmd builds.

.PARAMETER Flavor
release, debug or qemu.

.PARAMETER Name
-Capture: the binary's file name (xhci98.sys, xhciuas.sys, xhcicm.sys,
xhcihlpr.sys).

.PARAMETER Image
-Capture: the path of the binary whose import gate just passed.

.PARAMETER NoTargetEvidence
-Finish: the build's import gates ran with -NoTargetEvidence.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86", "amd64")]
    [string]$Arch,
    [Parameter(Mandatory = $true)]
    [ValidateSet("debug", "release", "qemu")]
    [string]$Flavor,
    [switch]$Begin,
    [switch]$Capture,
    [switch]$Finish,
    [string]$Name = "",
    [string]$Image = "",
    [switch]$NoTargetEvidence
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
    if (@(@($Begin, $Capture, $Finish) | Where-Object { $_ }).Count -ne 1) {
        throw "give exactly one of -Begin, -Capture and -Finish."
    }
    $repo = Get-RepoRoot
    $record = Get-GatedManifestPath -Repo $repo -Arch $Arch -Flavor $Flavor
    $pending = [System.IO.Path]::ChangeExtension($record, ".pending")
    $lockRun = Join-Path $repo ("out\gated-{0}.lock\run.txt" -f $Arch)

    # Only from the build-driver.cmd run that holds this architecture's lock.
    $token = "$env:XHCI98_GATED_RUN".Trim()
    if ($token -eq "" -or -not (Test-Path -LiteralPath $lockRun -PathType Leaf) -or
        ([System.IO.File]::ReadAllText($lockRun).Trim() -ne $token)) {
        throw "this records a gated build and is run by scripts\build-driver.cmd while it holds '$(Split-Path -Parent $lockRun)'; it does not certify binaries on its own."
    }

    if ($Begin) {
        if (Test-Path -LiteralPath $record) { Remove-Item -LiteralPath $record -Force }
        if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending -Force }
        $tree = Get-GitTreeState -Repo $repo
        [System.IO.File]::WriteAllText($pending, "", [System.Text.Encoding]::ASCII)
        Add-PendingLines -Path $pending -Lines @(
            "run=$token",
            "head_start=$($tree.Head)",
            ("dirty_start=" + $(if ($tree.Dirty) { "1" } else { "0" }))
        )
        exit 0
    }

    if (-not (Test-Path -LiteralPath $pending -PathType Leaf)) {
        throw "no '$pending': the $Flavor $Arch build did not begin a record in this run."
    }
    $p = Read-Pending -Path $pending
    if ($p["run"] -ne $token) {
        throw "'$pending' belongs to another build-driver.cmd run."
    }

    if ($Capture) {
        $key = $Name.ToLowerInvariant()
        if ($key -notmatch '^(xhci98|xhciuas|xhcicm|xhcihlpr)\.sys$') { throw "-Capture -Name '$Name' is not a binary this build gates." }
        if ($p.ContainsKey("sha256.$key")) { throw "$key was captured twice in one flavour's build." }
        if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { throw "no $key at '$Image'." }
        $full = (Resolve-Path -LiteralPath $Image).ProviderPath
        Add-PendingLines -Path $pending -Lines @(
            "sha256.$key=$(Get-FileSha256 -Path $full)",
            "path.$key=$full"
        )
        exit 0
    }

    # -Finish: every expected binary captured, and each still its capture.
    $expected = @("xhci98.sys", "xhciuas.sys", "xhcicm.sys")
    if ($Arch -eq "x86") { $expected += "xhcihlpr.sys" }
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
    $end = Get-GitTreeState -Repo $repo
    $dirty = ($p["dirty_start"] -ne "0") -or $end.Dirty -or ($p["head_start"] -ne $end.Head)
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
