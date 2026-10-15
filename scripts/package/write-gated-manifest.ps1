<#
.SYNOPSIS
Record the gated binaries of one flavour (roadmap-usb4.md SIGN.1).

.DESCRIPTION
Called by scripts\build-driver.cmd at the end of a flavour, once every gate
for it has passed, and by nothing else. Writes out\gated-<arch>-<flavor>.txt
(package-common.ps1, "The gated build's manifest"): the SHA-256 of
xhci98.sys, xhciuas.sys, xhcicm.sys and, on x86, xhcihlpr.sys where the
build left them, the commit and whether the tree had uncommitted changes,
whether the import gates had their target evidence, and the time (UTC).
make-package.ps1 -SignOnly reads it.

The file is written beside itself and renamed into place, so a reader sees
the old manifest or the new one and never half of one. build-driver.cmd
deletes the old one before the flavour builds, so a flavour that fails
leaves none.

.PARAMETER Arch
x86 or amd64, as build-driver.cmd built.

.PARAMETER Flavor
release, debug or qemu.

.PARAMETER NoTargetEvidence
The build's import gates ran with -NoTargetEvidence.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86", "amd64")]
    [string]$Arch,
    [Parameter(Mandatory = $true)]
    [ValidateSet("debug", "release", "qemu")]
    [string]$Flavor,
    [switch]$NoTargetEvidence
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

try {
    $repo = Get-RepoRoot
    $archDir = if ($Arch -eq "amd64") { "amd64" } else { "i386" }
    $objDir = switch ($Flavor) {
        "debug" { "objchk" }
        "qemu"  { "objchk_qemu" }
        default { "objfre" }
    }
    # The same paths make-package.ps1 stages from by default.
    $binaries = [ordered]@{
        "xhci98.sys"  = Join-Path $repo "src\$objDir\$archDir\xhci98.sys"
        "xhciuas.sys" = Join-Path $repo "src\uas\$objDir\$archDir\xhciuas.sys"
        "xhcicm.sys"  = Join-Path $repo "src\cm\$objDir\$archDir\xhcicm.sys"
    }
    if ($Arch -eq "x86") {
        $binaries["xhcihlpr.sys"] = Join-Path $repo "src\hlpr\$objDir\i386\xhcihlpr.sys"
    }
    $sha = @{}
    foreach ($name in $binaries.Keys) {
        if (-not (Test-Path -LiteralPath $binaries[$name] -PathType Leaf)) {
            throw "no $name at '$($binaries[$name])', though every gate of the $Flavor $Arch build passed."
        }
        $sha[$name] = Get-FileSha256 -Path $binaries[$name]
    }
    $tree = Get-GitTreeState -Repo $repo
    $text = ConvertTo-GatedManifest -Arch $Arch -Flavor $Flavor -Commit $tree.Head -Dirty $tree.Dirty `
        -TargetEvidence (-not $NoTargetEvidence) `
        -Time ([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")) -Sha256 $sha
    $path = Get-GatedManifestPath -Repo $repo -Arch $Arch -Flavor $Flavor
    Ensure-Directory (Split-Path -Parent $path)
    $tmp = $path + ".tmp"
    [System.IO.File]::WriteAllBytes($tmp, [System.Text.Encoding]::ASCII.GetBytes($text))
    Move-Item -LiteralPath $tmp -Destination $path -Force
    Write-Ok ("gated build recorded: {0}" -f $path)
    foreach ($name in $binaries.Keys) { Write-Host ("  {0,-13} {1}" -f $name, $sha[$name]) }
    $state = if ($tree.Head -eq "none") { "no commit (git could not say)" }
             elseif ($tree.Dirty) { "$($tree.Head), with uncommitted changes" }
             else { $tree.Head }
    Write-Host ("  commit        {0}" -f $state)
    if ($tree.Head -eq "none" -or $tree.Dirty) {
        Write-Warn "make-package.ps1 -SignOnly refuses this record: build from a clean, committed tree to sign without re-running the gates."
    }
} catch {
    Write-Err $_.Exception.Message
    exit 1
}
exit 0
