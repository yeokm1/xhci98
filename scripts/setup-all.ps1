<#
.SYNOPSIS
Run all Phase 1 host setup helpers.

.DESCRIPTION
This is a thin orchestrator for the separate setup scripts:
- setup-msvc6.ps1
- setup-w2kddk.ps1
- setup-qemu.ps1        (Phase 2a: the Win98 SE target VM)
- setup-qemu-win2k.ps1  (Phase 2b: the Win2000 SP4 target VM)

Win98 SE and Win2000 SP4 are co-primary targets, so both VMs are part of a full
setup. The Win2000 half needs -Win2KIso; without it that step is skipped with a
warning rather than failing the run.

The Phase 2d SMP Win2000 stress VM is deliberately not included - it has its own
script and its own checkpoint.

Run the individual scripts directly when you only want to configure one
component.

It then checks, and only says, what no helper here stages and every build
now needs: WDK 7.1 in tools\WinDDK71 (every x86 run builds xhcihlpr.sys and
xhcinet6.sys with it, and the -amd64 leg is built with it) and the Windows
98 DDK headers in tools\w98ddk\inc (Stor98's 98 SE and ME target), and Open
Watcom, which the DOS qualifier, XHCIDI.EXE and S98VSD.VXD need. A missing
one is a warning naming where docs\contributing\build-and-test.md says how
to stage it; the summary says the setup is incomplete.
#>

[CmdletBinding()]
param(
    [string]$DdkPath = "",
    [string]$ToolsDir = "",
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$Win98Iso = "",
    [string]$Win2KIso = "",
    [string]$Win2KUsbdSys = "",
    [string]$DiskSize = "4G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci",
    [switch]$CreateDisk,
    [switch]$RunInstallers,
    [switch]$InstallQemu,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

if ([string]::IsNullOrWhiteSpace($ToolsDir)) {
    $ToolsDir = Get-DefaultToolsDir
}
if ([string]::IsNullOrWhiteSpace($DdkPath)) {
    $DdkPath = Get-DefaultDdkPath
}
if ([string]::IsNullOrWhiteSpace($VmDir)) {
    $VmDir = Get-DefaultVmDir
}
if ([string]::IsNullOrWhiteSpace($LocalScriptDir)) {
    $LocalScriptDir = Get-DefaultLocalScriptDir
}

function Invoke-SetupChild {
    param(
        [string]$Name,
        [string[]]$Arguments
    )
    & powershell @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Write-Step "Running MSVC 6.0 setup"
$msvcArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "setup-msvc6.ps1"),
    "-ToolsDir", $ToolsDir
)
if ($RunInstallers) {
    $msvcArgs += "-RunInstaller"
}
if ($Force) {
    $msvcArgs += "-Force"
}
Invoke-SetupChild -Name "setup-msvc6.ps1" -Arguments $msvcArgs

#
# **UNPACK THE DDK, then write the wrappers.** `setup-w2kddk.ps1` writes
# command wrappers and, without -RunInstaller, only PRINTS the name of the
# script that unpacks the DDK - so a fresh clone that followed
# build-and-test.md's "Run all setup helpers" got wrappers pointing at a DDK
# that was not there and could not build anything (the audit of 2026-09-07).
#
# Unpacking is idempotent and needs no GUI and no machine-wide install, which
# is why it belongs in the "run everything" path rather than behind
# -RunInstallers: that switch is about launching vendor installers, and this is
# not one. A host that already has the DDK unpacked is left alone.
#
Write-Step "Unpacking the Win2K DDK"
$ddkSetenv = Join-Path $DdkPath "bin\setenv.bat"
if (Test-Path -LiteralPath $ddkSetenv) {
    Write-Ok "Win2K DDK already unpacked at $DdkPath"
} else {
    Invoke-SetupChild -Name "install-w2kddk-cabs.ps1" -Arguments @(
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "install-w2kddk-cabs.ps1"),
        "-DdkPath", $DdkPath,
        "-ToolsDir", $ToolsDir
    )
}

Write-Step "Running Win2K DDK setup"
$ddkArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "setup-w2kddk.ps1"),
    "-DdkPath", $DdkPath,
    "-ToolsDir", $ToolsDir,
    "-LocalScriptDir", $LocalScriptDir
)
if ($RunInstallers) {
    $ddkArgs += "-RunInstaller"
}
Invoke-SetupChild -Name "setup-w2kddk.ps1" -Arguments $ddkArgs

Write-Step "Running QEMU setup (Phase 2a - Win98 SE target VM)"
$qemuArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "setup-qemu.ps1"),
    "-VmDir", $VmDir,
    "-LocalScriptDir", $LocalScriptDir,
    "-DiskSize", $DiskSize
)
if (-not [string]::IsNullOrWhiteSpace($XhciDevice)) {
    $qemuArgs += @("-XhciDevice", $XhciDevice)
}
if (-not [string]::IsNullOrWhiteSpace($Win98Iso)) {
    $qemuArgs += @("-Win98Iso", $Win98Iso)
}
if (-not [string]::IsNullOrWhiteSpace($QemuBinDir)) {
    $qemuArgs += @("-QemuBinDir", $QemuBinDir)
}
if ($CreateDisk) {
    $qemuArgs += "-CreateDisk"
}
if ($InstallQemu) {
    $qemuArgs += "-Install"
}
Invoke-SetupChild -Name "setup-qemu.ps1" -Arguments $qemuArgs

Write-Step "Running QEMU setup (Phase 2b - Win2000 SP4 target VM)"
if ([string]::IsNullOrWhiteSpace($Win2KIso)) {
    Write-Warn "No -Win2KIso given: skipping the Windows 2000 SP4 target VM."
    Write-Warn "Win2000 SP4 is a co-primary target - rerun with -Win2KIso, or run"
    Write-Warn "scripts\setup-qemu-win2k.ps1 directly, before starting Phase 3."
} else {
    $win2kArgs = @(
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "setup-qemu-win2k.ps1"),
        "-VmDir", $VmDir,
        "-LocalScriptDir", $LocalScriptDir,
        "-DiskSize", $DiskSize,
        "-Win2KIso", $Win2KIso
    )
    if (-not [string]::IsNullOrWhiteSpace($XhciDevice)) {
        $win2kArgs += @("-XhciDevice", $XhciDevice)
    }
    if (-not [string]::IsNullOrWhiteSpace($Win2KUsbdSys)) {
        $win2kArgs += @("-Win2KUsbdSys", $Win2KUsbdSys)
    }
    if (-not [string]::IsNullOrWhiteSpace($QemuBinDir)) {
        $win2kArgs += @("-QemuBinDir", $QemuBinDir)
    }
    if ($CreateDisk) {
        $win2kArgs += "-CreateDisk"
    }
    Invoke-SetupChild -Name "setup-qemu-win2k.ps1" -Arguments $win2kArgs
}

# What the helpers above do not stage and every build needs.
$missing = 0
$wdk = Join-Path $ToolsDir "WinDDK71"
if (Test-Path -LiteralPath (Join-Path $wdk "bin\setenv.bat") -PathType Leaf) {
    Write-Ok "WDK 7.1 found at $wdk"
} else {
    $missing++
    Write-Warn ("WDK 7.1 is not at $wdk (no bin\setenv.bat). Every x86 build needs it for " +
        "xhcihlpr.sys and xhcinet6.sys, and the -amd64 leg is built with it; unpack it there " +
        "or set WDKROOT (docs\contributing\build-and-test.md, the x86 helper's paragraph under " +
        """Building"").")
}
$w98inc = Join-Path $ToolsDir "w98ddk\inc"
if ((Test-Path -LiteralPath (Join-Path $w98inc "SCSI.H") -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $w98inc "SRB.H") -PathType Leaf)) {
    Write-Ok "the Windows 98 DDK's SCSI.H and SRB.H found in $w98inc"
} else {
    $missing++
    Write-Warn ("the Windows 98 DDK's SCSI.H and SRB.H are not in $w98inc. Stor98's 98 SE and ME " +
        "build needs them (docs\contributing\build-and-test.md, ""Building Stor98"").")
}
$watcom = if ([string]::IsNullOrEmpty($env:WATCOM)) { "C:\WATCOM" } else { $env:WATCOM }
if ((Test-Path -LiteralPath (Join-Path $watcom "binnt\wcc386.exe")) -or (Test-Path -LiteralPath (Join-Path $watcom "binnt64\wcc386.exe"))) {
    Write-Ok "Open Watcom found at $watcom"
} else {
    Write-Warn ("Open Watcom is not at $watcom (WATCOM). The DOS qualifier, XHCIDI.EXE and S98VSD.VXD " +
        "need it (docs\contributing\build-and-test.md, ""Host Machine (Windows)"").")
}

if ($missing -gt 0) {
    Write-Step "Phase 1 setup done, but $missing toolchain(s) every build needs are missing (see the warnings above)"
} else {
    Write-Step "Phase 1 setup complete"
}
Write-Host "Useful wrappers are under $LocalScriptDir"
Write-Host "Run setup-msvc6.ps1, setup-w2kddk.ps1, setup-qemu.ps1, or setup-qemu-win2k.ps1 directly for component-specific setup."
