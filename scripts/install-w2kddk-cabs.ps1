<#
.SYNOPSIS
Install the Win2K DDK build environment from tools\WIN2KDDK.EXE without the
GUI installer.

.DESCRIPTION
WIN2KDDK.EXE is a Wextract cabinet self-extractor whose payload is the
kitsetup.exe installer plus per-component INF/CAB pairs (advpack-style INFs:
each [Files_N] section maps "realname,cabmembername" and [DestinationDirs]
maps the section to a subdirectory of install root dirid 49000). This script
performs the same file placement kitsetup would do for the selected
components, deterministically and with no interactive UI:

  1. Extracts the WIN2KDDK.EXE payload with Windows bsdtar (reads CABs).
  2. For each selected component, parses its INF and copies the renamed CAB
     members to their real paths under -DdkPath.
  3. Writes -DdkPath\bin\ddkvars.bat pointing the DDK's setenv.bat at the
     MSVC 6.0 toolchain extracted by setup-msvc6.ps1 (setenv.bat calls
     ddkvars.bat when present, instead of requiring a registry-installed
     MSVC; verified against the setenv.bat shipped in x86dBIN.CAB).

It skips each INF's InfFiles section (dirid 17 = %windir%\INF uninstall
bookkeeping) and writes no registry keys; the DDK build tools need neither.

-DdkPath defaults to tools\ntddk inside this repository, so nothing is
installed machine-wide and a clone builds wherever it is unpacked. When the
DDK lands inside the repo, ddkvars.bat reaches MSVC through %~dp0 rather than
a baked-in absolute path - which is what used to break when the repository was
renamed or opened on a second host (the symptom was `'nmake.exe' is not
recognized`, from a ddkvars.bat still naming the old location).

Default components are the minimum for the Phase 1 checkpoint: x86 build
tools (x86dBIN), debug+release x86 libraries (x86dLIBc/x86dLIBf), DDK+SDK
headers (NINC_DDK/NINC_SDK), core/license files (COREddk, RELNOTE), and the
general driver samples containing the toaster sample (NGEN_DDK - note this
DDK places toaster at src\general\toaster, not src\wdm\toaster).
#>

[CmdletBinding()]
param(
    [string]$DdkPath = "",
    [string]$ToolsDir = "",
    [string[]]$Components = @("COREddk","RELNOTE","x86dBIN","x86dLIBc","x86dLIBf","NINC_DDK","NINC_SDK","NGEN_DDK")
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

if ([string]::IsNullOrWhiteSpace($ToolsDir)) {
    $ToolsDir = Get-DefaultToolsDir
}
if ([string]::IsNullOrWhiteSpace($DdkPath)) {
    $DdkPath = Get-DefaultDdkPath
}

Write-Step "Checking host"
Test-SetupHost

Write-Step "Checking Win2K DDK archive"
$ddkExe = Join-Path $ToolsDir "WIN2KDDK.EXE"
if (-not (Test-Path -LiteralPath $ddkExe)) {
    throw "Missing $ddkExe"
}
Write-Ok "Found $ddkExe"

Write-Step "Extracting WIN2KDDK.EXE payload"
$payloadDir = Join-Path $ToolsDir "win2kddk-extracted"
Ensure-Directory $payloadDir
if (Test-Path -LiteralPath (Join-Path $payloadDir "cabs.ini")) {
    Write-Ok "Payload already extracted at $payloadDir"
} else {
    tar -xf $ddkExe -C $payloadDir
    if ($LASTEXITCODE -ne 0) { throw "tar extraction of $ddkExe failed" }
    Write-Ok "Extracted payload to $payloadDir"
}

function Parse-IniSections {
    param([string]$Path)
    $sections = @{}
    $current = ""
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith(";")) { continue }
        if ($t -match '^\[(.+)\]\s*$') {
            $current = $matches[1].Trim()
            if (-not $sections.ContainsKey($current)) { $sections[$current] = New-Object System.Collections.ArrayList }
            continue
        }
        if ($current -ne "") { [void]$sections[$current].Add($t) }
    }
    return $sections
}

Write-Step "Installing DDK components to $DdkPath"
$grandTotal = 0
foreach ($comp in $Components) {
    $inf = Get-ChildItem -LiteralPath $payloadDir -Filter "$comp.inf" | Select-Object -First 1
    $cab = Get-ChildItem -LiteralPath $payloadDir -Filter "$comp.cab" | Select-Object -First 1
    if ($null -eq $inf) { throw "Missing INF for component $comp" }
    if ($null -eq $cab) { throw "Missing CAB for component $comp" }

    $sections = Parse-IniSections $inf.FullName

    $installKey = "DefaultInstall"
    if (-not $sections.ContainsKey($installKey)) { $installKey = "DefaultInstall.NT" }
    if (-not $sections.ContainsKey($installKey)) { throw "${comp}: no DefaultInstall section" }

    $copyFiles = @()
    foreach ($line in $sections[$installKey]) {
        if ($line -match '^CopyFiles\s*=\s*(.*)$') {
            $copyFiles += ($matches[1] -split ',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
        }
    }

    $destMap = @{}
    foreach ($line in $sections["DestinationDirs"]) {
        if ($line -match '^([^=]+)=\s*(\d+)\s*(?:,\s*"?([^"]*)"?)?\s*$') {
            $sec = $matches[1].Trim(); $dirid = $matches[2]; $sub = $matches[3]
            if ($dirid -eq "49000") {
                if ($null -eq $sub -or $sub -eq "." -or $sub -eq "") { $destMap[$sec] = "" } else { $destMap[$sec] = $sub }
            }
        }
    }

    $tmp = Join-Path $env:TEMP ("ddkcab_" + $comp)
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    New-Item -ItemType Directory -Path $tmp | Out-Null
    tar -xf $cab.FullName -C $tmp
    if ($LASTEXITCODE -ne 0) { throw "${comp}: tar extraction of $($cab.Name) failed" }

    $count = 0
    foreach ($fsec in $copyFiles) {
        if ($fsec -eq "InfFiles") { continue }
        if (-not $destMap.ContainsKey($fsec)) {
            Write-Warn "${comp}: section $fsec has no dirid-49000 destination - skipped"
            continue
        }
        if (-not $sections.ContainsKey($fsec)) {
            Write-Warn "${comp}: file section [$fsec] missing - skipped"
            continue
        }
        $destDir = Join-Path $DdkPath $destMap[$fsec]
        if (-not (Test-Path -LiteralPath $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
        foreach ($entry in $sections[$fsec]) {
            $parts = ($entry -split ',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
            if ($parts.Count -eq 0) { continue }
            $destName = $parts[0]
            $srcName = if ($parts.Count -ge 2) { $parts[1] } else { $parts[0] }
            $srcPath = Join-Path $tmp $srcName
            if (-not (Test-Path -LiteralPath $srcPath)) { throw "${comp}: $srcName not found in $($cab.Name) (wanted for $destName)" }
            Copy-Item -LiteralPath $srcPath -Destination (Join-Path $destDir $destName) -Force
            $count++
        }
    }
    Remove-Item -LiteralPath $tmp -Recurse -Force
    Write-Ok ("{0}: {1} files installed" -f $comp, $count)
    $grandTotal += $count
}
Write-Ok "Total: $grandTotal files into $DdkPath"

Write-Step "Writing ddkvars.bat (portable MSVC 6.0 hookup)"
$msvcRoot = Get-DefaultMsvcRoot
if ($ToolsDir -ne (Get-DefaultToolsDir)) {
    $msvcRoot = Join-Path $ToolsDir "MSVC600"
}
$msvcVc98 = Join-Path $msvcRoot "VC98"
$msvcMsdev = Join-Path $msvcRoot "Common\MSDev98"
if (Test-Path -LiteralPath (Join-Path $msvcVc98 "BIN\CL.EXE")) {
    $ddkBin = Join-Path $DdkPath "bin"
    Ensure-Directory $ddkBin
    $ddkvars = Join-Path $ddkBin "ddkvars.bat"

    # %~dp0 is this file's own directory, so a relative hop from <ddk>\bin to
    # the MSVC tree survives moving or renaming the repository - the failure
    # mode the absolute form had. It only exists when both sides are on the
    # same volume; Get-RelativePathFrom answers $null otherwise and we fall
    # back to absolute paths, which is correct for a DDK installed elsewhere.
    $relVc98 = Get-RelativePathFrom -FromDir $ddkBin -To $msvcVc98
    $relMsdev = Get-RelativePathFrom -FromDir $ddkBin -To $msvcMsdev

    $header = @(
        "@echo off",
        "rem Point the DDK at the MSVC 6.0 toolchain extracted from tools\MSVC600.zip.",
        "rem setenv.bat calls this file when present instead of requiring a",
        "rem registry-installed MSVC (see setenv.bat's ddkvars.bat branch).",
        "rem Generated by scripts\install-w2kddk-cabs.ps1 - re-run it to refresh."
    )
    if ($null -ne $relVc98 -and $null -ne $relMsdev) {
        $body = @(
            "rem Paths are relative to this file (%~dp0), so they follow the repository.",
            "for %%I in (""%~dp0$relVc98"") do set ""MSVCDIR=%%~fI""",
            "for %%I in (""%~dp0$relMsdev"") do set ""MSDEVDIR=%%~fI"""
        )
        $shape = "relative to %~dp0"
    } else {
        $body = @(
            "rem The MSVC tree is not on this DDK's volume, so these are absolute.",
            "set ""MSVCDIR=$msvcVc98""",
            "set ""MSDEVDIR=$msvcMsdev"""
        )
        $shape = "absolute (different volume)"
    }
    Write-AsciiFile $ddkvars ($header + $body + @(
        "set ""Path=%MSVCDIR%\BIN;%MSDEVDIR%\BIN;%Path%""",
        "set ""INCLUDE=%MSVCDIR%\INCLUDE""",
        "set ""LIB=%MSVCDIR%\LIB"""
    ))
    Write-Ok "Wrote $ddkvars - MSVC paths $shape"
} else {
    Write-Warn "Extracted MSVC not found at $msvcVc98 - run setup-msvc6.ps1 first, then re-run this script"
}
