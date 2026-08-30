<#
.SYNOPSIS
Unpack the local MSVC 6.0 tool archive for Phase 1.

.DESCRIPTION
This script validates tools/MSVC600.zip and extracts it in place, so the
toolchain lands at tools/MSVC600 (the archive's own top-level directory) and
is used straight from there - run-in-place, no machine-wide install, nothing
under C:\. Every script that compiles anything derives that path from its own
location, so a clone works wherever it is unpacked.

The legacy setup program is located and can be launched with -RunInstaller,
but it is not needed and never has been: cl.exe 12.00.8804 runs from
VC98\BIN once Common\MSDev98\Bin (which holds MSPDB60.DLL) is on PATH.
#>

[CmdletBinding()]
param(
    [string]$ToolsDir = "",
    [switch]$RunInstaller,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

if ([string]::IsNullOrWhiteSpace($ToolsDir)) {
    $ToolsDir = Get-DefaultToolsDir
}

Write-Step "Checking host"
Test-SetupHost

Write-Step "Checking MSVC 6.0 archive"
$msvcZip = Join-Path $ToolsDir "MSVC600.zip"
if (-not (Test-Path -LiteralPath $msvcZip)) {
    throw "Missing $msvcZip"
}
Write-Ok "Found $msvcZip"

Write-Step "Extracting MSVC 6.0 archive"
# MSVC600.zip's own root entry is MSVC600\, so extracting to tools\ yields
# tools\MSVC600 - one directory, not the tools\extracted\MSVC600\MSVC600 the
# earlier arrangement produced.
$msvcExtract = Join-Path $ToolsDir "MSVC600"

if ((Test-Path -LiteralPath $msvcExtract) -and $Force) {
    if (-not (Test-PathUnderRoot -Path $msvcExtract -Root $ToolsDir)) {
        throw "Refusing to remove extraction directory outside tools: $msvcExtract"
    }
    Remove-Item -LiteralPath $msvcExtract -Recurse -Force
}

if (Test-Path -LiteralPath $msvcExtract) {
    Write-Ok "MSVC archive already extracted at $msvcExtract"
} else {
    Expand-Archive -LiteralPath $msvcZip -DestinationPath $ToolsDir -Force
    Write-Ok "Extracted to $msvcExtract"
}

$cl = Join-Path $msvcExtract "VC98\BIN\CL.EXE"
if (Test-Path -LiteralPath $cl) {
    Write-Ok "Compiler ready: $cl"
} else {
    Write-Warn "CL.EXE not found at $cl - the archive did not unpack as expected."
}

$msvcInstallers = @(
    Get-ChildItem -LiteralPath $msvcExtract -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ieq "setup.exe" -or $_.Name -ieq "acmsetup.exe" } |
        Sort-Object FullName
)

if ($msvcInstallers.Count -eq 0) {
    Write-Warn "Could not find setup.exe/acmsetup.exe under $msvcExtract"
} else {
    Write-Ok "Found likely MSVC installer: $($msvcInstallers[0].FullName)"
}

if ($RunInstaller) {
    if ($msvcInstallers.Count -eq 0) {
        throw "No MSVC installer was found to launch."
    }
    Write-Step "Launching MSVC 6.0 installer"
    Start-Process -FilePath $msvcInstallers[0].FullName -Wait
} else {
    Write-Warn "Installer was not launched, and is not required - the extracted"
    Write-Warn "toolchain runs in place. Re-run with -RunInstaller only if you want it."
}
