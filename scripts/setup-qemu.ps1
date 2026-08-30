<#
.SYNOPSIS
Install or configure QEMU launchers for Phase 2.

.DESCRIPTION
This script checks for qemu-system-x86_64.exe and qemu-img.exe, optionally
installs QEMU through winget, optionally creates the Win98 disk/transfer
images, and writes local QEMU launcher scripts.
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$Win98Iso = "",
    [string]$DiskSize = "4G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci",
    [int]$MonitorPort = 55555,
    [switch]$Install,
    [switch]$CreateDisk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

if ([string]::IsNullOrWhiteSpace($VmDir)) {
    $VmDir = Get-DefaultVmDir
}
if ([string]::IsNullOrWhiteSpace($LocalScriptDir)) {
    $LocalScriptDir = Get-DefaultLocalScriptDir
}

Write-Step "Checking host"
Test-SetupHost

function Get-QemuTool {
    param(
        [string]$QemuBinDir,
        [string]$ToolName
    )
    if (-not [string]::IsNullOrWhiteSpace($QemuBinDir)) {
        $candidate = Join-Path $QemuBinDir $ToolName
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return (Find-Tool $ToolName)
}

Write-Step "Checking QEMU"
$qemuSystem = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-system-x86_64.exe"
$qemuImg = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-img.exe"

if (($null -eq $qemuSystem -or $null -eq $qemuImg) -and $Install) {
    Write-Step "Installing QEMU with winget"
    $winget = Find-Tool "winget.exe"
    if ($null -eq $winget) {
        throw "winget.exe was not found. Install QEMU manually or add -QemuBinDir."
    }
    & $winget install --id SoftwareFreedomConservancy.QEMU -e --source winget | Out-Host
    $qemuSystem = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-system-x86_64.exe"
    $qemuImg = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-img.exe"
}

if ($null -eq $qemuSystem) {
    Write-Warn "qemu-system-x86_64.exe is not on PATH. Install QEMU or pass -QemuBinDir."
    $qemuSystemCommand = "qemu-system-x86_64"
} else {
    Write-Ok "Found $qemuSystem"
    $qemuSystemCommand = $qemuSystem
}

if ($null -eq $qemuImg) {
    Write-Warn "qemu-img.exe is not on PATH. Disk image creation will be skipped unless QEMU is installed."
} else {
    Write-Ok "Found $qemuImg"
}

Write-Step "Creating local directories"
Ensure-Directory $VmDir
Ensure-Directory $LocalScriptDir
$diskImage = Join-Path $VmDir "win98.img"
$transferImage = Join-Path $VmDir "transfer.img"
$usbDataImage = Join-Path $VmDir "usbdata.img"
$xferDir = Join-Path $VmDir "xfer98"
$debugConLog = Join-Path $VmDir "win98-debugcon.log"
$debugConPreviousLog = Join-Path $VmDir "win98-debugcon.previous.log"
Ensure-Directory $xferDir
Write-Ok "VM directory: $VmDir"
Write-Ok "Local script directory: $LocalScriptDir"
Write-Ok "Transfer (VVFAT) directory: $xferDir"

if ($CreateDisk) {
    Write-Step "Creating QEMU images"
    if (-not (Test-Path -LiteralPath $diskImage)) {
        if ($null -eq $qemuImg) {
            Write-Warn "Skipping $diskImage because qemu-img.exe is not available."
        } else {
            & $qemuImg create -f qcow2 $diskImage $DiskSize | Out-Host
        }
    } else {
        Write-Ok "Disk image already exists: $diskImage"
    }

    if (-not (Test-Path -LiteralPath $transferImage)) {
        $stream = [System.IO.File]::Open($transferImage, [System.IO.FileMode]::CreateNew)
        try {
            $stream.SetLength(1474560)
        } finally {
            $stream.Close()
        }
        Write-Ok "Created blank 1.44 MB transfer floppy image: $transferImage"
        Write-Warn "The transfer floppy image is blank. Format it inside Win98 before first use."
    } else {
        Write-Ok "Transfer image already exists: $transferImage"
    }

    if (-not (Test-Path -LiteralPath $usbDataImage)) {
        $stream = [System.IO.File]::Open($usbDataImage, [System.IO.FileMode]::CreateNew)
        try {
            $stream.SetLength(64MB)
        } finally {
            $stream.Close()
        }
        Write-Ok "Created blank 64 MB USB storage image: $usbDataImage"
        Write-Warn "The USB storage image is blank. Format it inside Win98 (FAT) before first use."
    } else {
        Write-Ok "USB storage image already exists: $usbDataImage"
    }
}

Write-Step "Writing QEMU launchers"
Write-Ok "Using xHCI device model: $XhciDevice"

$installCmd = Join-Path $LocalScriptDir "qemu-win98-install.cmd"
Write-AsciiFile $installCmd @(
    "@echo off",
    "rem IMPORTANT: install Win98 with the ACPI HAL or PCI never enumerates under",
    "rem QEMU (plain auto-install -> ""Plug and Play BIOS"" Code 24, nothing on PCI).",
    "rem At the CD boot menu pick ""Start computer with CD-ROM support"", then at the",
    "rem A:\ prompt: fdisk (create+activate primary), reboot, format c:, then run",
    "rem   D:\setup.exe /p j    (the /p j forces the ACPI HAL). Do NOT add acpi=off.",
    "rem -boot once=d boots the CD only for the first boot; guest reboots during",
    "rem setup then continue from the HDD. -action reboot=reset keeps those guest",
    "rem reboots inside this one QEMU session (this build otherwise exits on reboot).",
    "set ""WIN98_ISO=$Win98Iso""",
    "if ""%WIN98_ISO%""=="""" (",
    "  echo Edit this file or set -Win98Iso when running setup-qemu.ps1.",
    "  exit /b 1",
    ")",
    "if not exist ""%WIN98_ISO%"" (",
    "  echo Missing ISO: %WIN98_ISO%",
    "  exit /b 1",
    ")",
    """$qemuSystemCommand"" ^",
    "  -machine pc ^",
    "  -cpu pentium3 ^",
    "  -m 256 ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive if=floppy,file=""$transferImage"",format=raw ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -cdrom ""%WIN98_ISO%"" ^",
    "  -boot once=d ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)

$runCmd = Join-Path $LocalScriptDir "qemu-win98-run.cmd"
Write-AsciiFile $runCmd @(
    "@echo off",
    "rem -boot c boots the installed HDD (the attached floppy is A:, not a boot",
    "rem source). -action reboot=reset keeps guest reboots in this QEMU session.",
    "rem The VVFAT drive exposes vm\xfer98 as a writable guest disk backed by a",
    "rem READ-ONLY host directory. snapshot=on supplies a temporary qcow2 overlay;",
    "rem guest writes are discarded and cannot modify the host deploy files.",
    "rem Do not change this to fat:rw: - that mode can write back to the host.",
    "rem The isa-debugcon at port 0xE9 captures the QEMU-flavour driver's trace",
    "rem channel - since task 13-L.1 no other flavour writes there at all",
    "rem (src\xhci_dbg.c writes each finished line there as well as to DbgPrint),",
    "rem which is how a Phase 3 registration log is read without a kernel debugger.",
    "rem Before each boot a non-empty prior trace is archived as",
    "rem win98-debugcon.previous.log and this boot gets a fresh log. That keeps",
    "rem stale DriverEntry lines from being mistaken for evidence from this run.",
    "rem An EMPTY current log is left in place instead: it means the previous",
    "rem launch died before QEMU wrote anything, and rotating it would overwrite",
    "rem the last real trace with nothing. QEMU truncates the current log when it",
    "rem opens it, so leaving the empty file alone costs nothing.",
    "if exist ""$debugConLog"" (",
    "  for %%S in (""$debugConLog"") do if not ""%%~zS""==""0"" (",
    "    move /y ""$debugConLog"" ""$debugConPreviousLog"" >nul",
    "    if errorlevel 1 (",
    "      echo Could not archive the prior debug-console log.",
    "      exit /b 1",
    "    )",
    "  )",
    ")",
    """$qemuSystemCommand"" ^",
    "  -machine pc ^",
    "  -cpu pentium3 ^",
    "  -m 256 ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  -drive if=floppy,file=""$transferImage"",format=raw ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -chardev file,id=dbgcon,path=""$debugConLog"" ^",
    "  -device isa-debugcon,iobase=0xe9,chardev=dbgcon ^",
    "  -boot c ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)

$usbTestCmd = Join-Path $LocalScriptDir "qemu-win98-usb-test.cmd"
Write-AsciiFile $usbTestCmd @(
    "@echo off",
    """$qemuSystemCommand"" ^",
    "  -machine pc ^",
    "  -cpu pentium3 ^",
    "  -m 256 ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive if=floppy,file=""$transferImage"",format=raw ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -device usb-kbd,bus=xhci.0,port=1 ^",
    "  -device usb-mouse,bus=xhci.0,port=2 ^",
    "  -boot c ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)

$netStorageTestCmd = Join-Path $LocalScriptDir "qemu-win98-net-storage-test.cmd"
Write-AsciiFile $netStorageTestCmd @(
    "@echo off",
    "rem Phase 8 bulk-path smoke test: USB Ethernet + USB mass storage on the xHCI bus.",
    "rem usb-net needs a Win98 function driver to bind; QEMU's emulated NIC may not have one.",
    "rem usb-storage needs NUSB mass-storage support or another Win98 function driver.",
    """$qemuSystemCommand"" ^",
    "  -machine pc ^",
    "  -cpu pentium3 ^",
    "  -m 256 ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive if=floppy,file=""$transferImage"",format=raw ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -netdev user,id=usbnet0 ^",
    "  -device usb-net,netdev=usbnet0,bus=xhci.0,port=1 ^",
    "  -drive if=none,id=usbdisk0,file=""$usbDataImage"",format=raw ^",
    "  -device usb-storage,drive=usbdisk0,bus=xhci.0,port=2 ^",
    "  -boot c ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)

Write-Ok "Wrote $installCmd"
Write-Ok "Wrote $runCmd"
Write-Ok "Wrote $usbTestCmd"
Write-Ok "Wrote $netStorageTestCmd"
