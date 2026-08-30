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

Write-Step "Phase 1 setup complete"
Write-Host "Useful wrappers are under $LocalScriptDir"
Write-Host "Run setup-msvc6.ps1, setup-w2kddk.ps1, setup-qemu.ps1, or setup-qemu-win2k.ps1 directly for component-specific setup."
