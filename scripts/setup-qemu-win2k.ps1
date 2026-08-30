<#
.SYNOPSIS
Create the Windows 2000 SP4 target VM disk image and QEMU launchers (Phase 2b).

.DESCRIPTION
Phase 2b stands up a Windows 2000 SP4 VM alongside the Phase 2a Win98 VM. Win2000
SP4 is a co-primary target of this project, not a lab instrument: from Phase 3
onward every checkpoint must be observed here as well as on Win98, and a failure
seen only here is a defect to fix, not a data point to note.

It is also the best differential available, because usbport.sys is native on
Win2000 - so a failure that reproduces on Win98+NUSB but not here isolates NUSB's
back-ported usbport from the miniport itself. Both roles, same VM.

This is the Win2000 counterpart to setup-qemu.ps1 (which handles the Win98 VM).
It checks for qemu-img.exe, optionally creates vm\win2k.img and the vm\xfer
transfer folder, and writes install/preparation/run launchers into scripts\local.

CRITICAL QEMU-11 / TCG LESSON: Windows 2000 Setup hangs forever at
"Setup is starting Windows 2000" on this host (scoop QEMU 11.0.0, TCG-only) when
the guest runs the ACPI/APIC HAL. The guest gets stuck servicing the APIC clock
interrupt (IDT vector 0xD1) in a tight interlocked loop in ntoskrnl - a timer
interrupt storm the ancient guest cannot drain under modern-QEMU TCG. The fix is
to force the Standard-PC (8259 PIC + PIT) HAL by removing the CPU local APIC and
the ACPI tables:  -machine pc,acpi=off  -cpu pentium3,-apic
Unlike Win98, Win2000 still enumerates the PCI bus (and the xHCI device) under the
Standard-PC HAL, so this does not cost us the Phase 2b checkpoint. The IDE
"win2k-install-hack" global and -vga cirrus are the commonly-recommended Win2000
extras and are kept too. See docs/contributing/lessons.md.

The same qemu-xhci device model and GDB/monitor conventions as the Win98 VM apply;
the monitor port is 55556 (vs Win98's 55555) so both VMs can run at once.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win2k.ps1 -Win2KIso D:\isos\win2ksp4.ISO -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$Win2KIso = "D:\isos\win2ksp4.ISO",
    [string]$Win2KUsbdSys = "",
    [string]$DiskSize = "4G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci",
    [int]$MonitorPort = 55556,
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

if ($null -eq $qemuSystem) {
    Write-Warn "qemu-system-x86_64.exe is not on PATH. Install QEMU (see setup-qemu.ps1) or pass -QemuBinDir."
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
$xferDir = Join-Path $VmDir "xfer"
Ensure-Directory $xferDir
$diskImage = Join-Path $VmDir "win2k.img"
$debugConLog = Join-Path $VmDir "win2k-debugcon.log"
$debugConPreviousLog = Join-Path $VmDir "win2k-debugcon.previous.log"
Write-Ok "VM directory: $VmDir"
Write-Ok "Local script directory: $LocalScriptDir"
Write-Ok "Transfer (VVFAT) directory: $xferDir"

if (-not (Test-Path -LiteralPath $Win2KIso)) {
    Write-Warn "Win2000 ISO not found at: $Win2KIso (pass -Win2KIso to override)."
} else {
    Write-Ok "Win2000 ISO: $Win2KIso"
}
function Assert-Win2KUsbdFile {
    param([string]$Path)
    $file = Get-Item -LiteralPath $Path
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName).FileVersion
    if ($file.Length -ne 20688 -or $version -ne "5.00.2195.6658") {
        throw "Expected Win2000 SP4 USBD.SYS 5.00.2195.6658 (20688 bytes); found version '$version' ($($file.Length) bytes) at: $Path"
    }
}

$stagedUsbd = Join-Path $xferDir "USBD.SYS"
$defaultUsbd = Join-Path (Get-DefaultToolsDir) "win2ksp4-extracted\USBD.SYS"
if ([string]::IsNullOrWhiteSpace($Win2KUsbdSys) -and
    (Test-Path -LiteralPath $defaultUsbd)) {
    $Win2KUsbdSys = $defaultUsbd
}
if (-not [string]::IsNullOrWhiteSpace($Win2KUsbdSys)) {
    if (-not (Test-Path -LiteralPath $Win2KUsbdSys)) {
        throw "Win2000 USBD.SYS not found at: $Win2KUsbdSys"
    }
    Assert-Win2KUsbdFile -Path $Win2KUsbdSys
    Copy-Item -LiteralPath $Win2KUsbdSys -Destination $stagedUsbd -Force
    Write-Ok "Staged Win2000 USBD.SYS for the preparation boot: $stagedUsbd"
} elseif (Test-Path -LiteralPath $stagedUsbd) {
    Assert-Win2KUsbdFile -Path $stagedUsbd
    Write-Ok "Using already-staged Win2000 USBD.SYS: $stagedUsbd"
} else {
    Write-Warn "Win2000 USBD.SYS is not staged. Extract I386\USBD.SY_ from the SP4 ISO, expand it, then rerun with -Win2KUsbdSys <path> before attaching EHCI."
}

if ($CreateDisk) {
    Write-Step "Creating QEMU disk image"
    if (-not (Test-Path -LiteralPath $diskImage)) {
        if ($null -eq $qemuImg) {
            Write-Warn "Skipping $diskImage because qemu-img.exe is not available."
        } else {
            & $qemuImg create -f qcow2 $diskImage $DiskSize | Out-Host
            Write-Ok "Created $diskImage ($DiskSize)"
        }
    } else {
        Write-Ok "Disk image already exists: $diskImage"
    }
}

Write-Step "Writing QEMU launchers"
Write-Ok "Using xHCI device model: $XhciDevice"

$installCmd = Join-Path $LocalScriptDir "qemu-win2k-install.cmd"
Write-AsciiFile $installCmd @(
    "@echo off",
    "rem Phase 2b: Windows 2000 SP4 differential VM install launcher.",
    "rem The ISO is Win2000 Pro with SP4 integrated (retail FPP - Setup prompts",
    "rem for a product key).",
    "rem",
    "rem HANG FIX (scoop QEMU 11.0.0, TCG): without -cpu ...,-apic + acpi=off the",
    "rem guest hangs forever at ""Setup is starting Windows 2000"" in an APIC-clock",
    "rem (IDT vector 0xD1) interrupt storm. Forcing the Standard-PC (8259 PIC + PIT)",
    "rem HAL by removing the CPU local APIC and the ACPI tables clears it. Win2000",
    "rem still enumerates PCI (and the xHCI) under the Standard-PC HAL, so this does",
    "rem not cost Phase 2b. See docs/contributing/lessons.md.",
    "rem The xHCI is intentionally absent during install and added by the run",
    "rem launcher, so it appears as a fresh unrecognised device on first boot.",
    "rem -boot once=d boots the CD only for the first boot; -action reboot=reset",
    "rem keeps the guest reboots during setup inside this one QEMU session.",
    "set ""WIN2K_ISO=$Win2KIso""",
    "if not exist ""%WIN2K_ISO%"" (",
    "  echo Missing ISO: %WIN2K_ISO%",
    "  exit /b 1",
    ")",
    """$qemuSystemCommand"" ^",
    "  -name ""xhci98 Windows 2000 SP4 differential"" ^",
    "  -machine pc,acpi=off ^",
    "  -global ide-device.win2k-install-hack=on ^",
    "  -cpu pentium3,-apic ^",
    "  -m 256 ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -cdrom ""%WIN2K_ISO%"" ^",
    "  -boot once=d ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)

$runCmd = Join-Path $LocalScriptDir "qemu-win2k-run.cmd"
Write-AsciiFile $runCmd @(
    "@echo off",
    "rem Phase 2b: boot the installed Windows 2000 SP4 differential VM from HDD.",
    "rem Keep the SAME Standard-PC HAL flags as install (-cpu ...,-apic + acpi=off)",
    "rem or the installed system hits the same APIC-clock storm on normal boot.",
    "rem",
    "rem Two USB controllers on purpose:",
    "rem  - usb-ehci: an EHCI the NATIVE Win2000 SP4 stack binds (usbehci.sys ->",
    "rem    usbport.sys). Win2000 only extracts usbport.sys from driver.cab when a",
    "rem    controller needs it - with NO USB controller (as during install) the",
    "rem    file never lands on disk. The EHCI is what makes native usbport.sys",
    "rem    present + loaded, so its version can be recorded for the miniport ABI.",
    "rem    (Contrast Win98+NUSB, which copies usbport.sys unconditionally.)",
    "rem  - qemu-xhci: no Win2000 driver -> shows as an unrecognised PCI device,",
    "rem    the Phase 2b Device Manager checkpoint.",
    "rem PREREQUISITE: use qemu-win2k-prepare-usbd.cmd first and copy USBD.SYS",
    "rem into C:\WINNT\system32\drivers. Attaching EHCI before that copy makes",
    "rem usbhub20.sys fail and the next boot bugcheck c000026c / 0xc0000034.",
    "rem The VVFAT backing directory is read-only; snapshot=on gives the guest a",
    "rem temporary writable overlay without exposing host files to guest writes.",
    "rem The isa-debugcon at port 0xE9 captures the QEMU-flavour driver's trace",
    "rem channel - since task 13-L.1 no other flavour writes there at all",
    "rem (src\xhci_dbg.c writes each finished line there as well as to DbgPrint),",
    "rem which is how a Phase 3 registration log is read without a kernel debugger.",
    "rem This is the Win2000 counterpart of the Win98 trace and is deliberately a",
    "rem SEPARATE file: the two targets' logs must never be mixed, because the whole",
    "rem point of the Phase 3 gate is comparing them.",
    "rem Before each boot a non-empty prior trace is archived as",
    "rem win2k-debugcon.previous.log and this boot gets a fresh log, so stale",
    "rem DriverEntry lines cannot be attributed to a replacement driver. An EMPTY",
    "rem current log is left alone: it means the previous launch died before QEMU",
    "rem wrote anything, and rotating it would replace the last real trace with",
    "rem nothing.",
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
    "  -name ""xhci98 Windows 2000 SP4 differential"" ^",
    "  -machine pc,acpi=off ^",
    "  -cpu pentium3,-apic ^",
    "  -m 256 ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  -device usb-ehci,id=ehci ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -chardev file,id=dbgcon,path=""$debugConLog"" ^",
    "  -device isa-debugcon,iobase=0xe9,chardev=dbgcon ^",
    "  -boot c ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)
$prepareCmd = Join-Path $LocalScriptDir "qemu-win2k-prepare-usbd.cmd"
Write-AsciiFile $prepareCmd @(
    "@echo off",
    "rem SAFE PREPARATION BOOT: no USB controller is attached, so the incomplete",
    "rem Win2000 USB 2.0 stack cannot start. Before shutting down the guest, copy:",
    "rem   D:\USBD.SYS C:\WINNT\system32\drivers\USBD.SYS",
    "rem The VVFAT disk may receive another drive letter; locate volume QEMU VVFAT.",
    "rem Only after this succeeds should qemu-win2k-run.cmd attach EHCI + xHCI.",
    "if not exist ""$stagedUsbd"" (",
    "  echo Missing staged file: $stagedUsbd",
    "  echo Rerun setup-qemu-win2k.ps1 with -Win2KUsbdSys ^<path-to-SP4-USBD.SYS^>.",
    "  exit /b 1",
    ")",
    """$qemuSystemCommand"" ^",
    "  -name ""xhci98 Windows 2000 SP4 USBD preparation"" ^",
    "  -machine pc,acpi=off ^",
    "  -cpu pentium3,-apic ^",
    "  -m 256 ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  -boot c ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
)

Write-Ok "Wrote $installCmd"
Write-Ok "Wrote $prepareCmd"
Write-Ok "Wrote $runCmd"

Write-Step "Next steps"
Write-Host "  1. Run scripts\local\qemu-win2k-install.cmd and install Windows 2000 (needs a product key)."
Write-Host "  2. Ensure SP4 USBD.SYS is staged (use -Win2KUsbdSys if needed), then boot qemu-win2k-prepare-usbd.cmd and copy it into C:\WINNT\system32\drivers."
Write-Host "  3. Shut down, then boot qemu-win2k-run.cmd (adds EHCI + xHCI)."
Write-Host "  4. Do NOT install NUSB - the usbport stack is native to Win2000 SP4."
