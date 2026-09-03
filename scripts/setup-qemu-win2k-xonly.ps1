<#
.SYNOPSIS
Create the xHCI-only Windows 2000 SP4 guest disk image and QEMU launchers (roadmap Phase 19).

.DESCRIPTION
A Windows 2000 SP4 install that has NEVER seen a USB controller, and since the
owner's decision of 2026-09-03 the base the fresh Windows 2000 clone of the
post-release run is taken from (scripts\vm-matrix\config.sample.psd1,
`2b-fresh`'s CloneFrom; docs\contributing\design\09-post-release-unattended-run.md,
section 3.1). Every earlier Windows 2000 image in this project was installed or
first booted with an EHCI attached, and the in-box stack binding that EHCI is
what placed usbport.sys from Driver Cache\i386: Windows 2000 gives usbport.sys,
usbhub.sys, usbehci.sys and usbd.sys the layout.inf disposition that does not
copy them at Setup (docs\contributing\lessons.md, the 2026-09-03 entry). So on
those images a package install could never show whether the INF's LayoutFile
line places usbport.sys itself, which is the reading release 1.0.1.0 claims
for an xHCI-only NT machine (roadmap task 19.5). This image can.

Same machine as the checkpointed 2b recipe (scripts\setup-qemu-win2k.ps1): TCG
with acpi=off and -cpu pentium3,-apic, the Standard-PC HAL, because on this
host family the APIC-clock ISR storms Setup under TCG (lessons.md, "The
vector-0xD1 storm is the accelerator"). Install and run launchers carry the
same flags: the HAL is fixed at install time.

The install launcher attaches no USB controller of any kind. The run launcher
attaches qemu-xhci alone by default, with the transfer drive vm\xferxp (one
qemu-flavour package serves every target: make-package.ps1 -Flavor qemu -OutDir
vm\xferxp). Its second argument is `none` for a boot with no controller at all
(the drivers listing before any controller exists) or `ehci` for the companion
EHCI the old vehicle carried, for comparison only.

.PARAMETER Win2KIso
The Windows 2000 SP4 CD image (retail FPP: Setup asks for a product key, which
the owner types). The install launcher requires it; the run launcher keeps it
attached when it exists and boots with no CD when it does not, and says so.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win2k-xonly.ps1 -Win2KIso D:\isos\win2ksp4-retail.ISO -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$Win2KIso = "D:\isos\win2ksp4.ISO",
    [string]$DiskSize = "4G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    [int]$MonitorPort = 55560,
    [int]$MemoryMb = 256,
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
    $qemuSystemCommand = "C:\Program Files\qemu\qemu-system-x86_64.exe"
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
$xferDir = Join-Path $VmDir "xferxp"
Ensure-Directory $xferDir
$diskImage = Join-Path $VmDir "win2k-xonly.img"
$debugConLog = Join-Path $VmDir "win2k-xonly-debugcon.log"
$debugConPreviousLog = Join-Path $VmDir "win2k-xonly-debugcon.previous.log"
$traceLogBase = Join-Path $VmDir "win2k-xonly-qemu-trace"
Write-Ok "VM directory: $VmDir"
Write-Ok "Local script directory: $LocalScriptDir"
Write-Ok "Transfer (VVFAT) directory: $xferDir"

if (-not (Test-Path -LiteralPath $Win2KIso)) {
    Write-Warn "Windows 2000 ISO not found at: $Win2KIso (pass -Win2KIso to override)."
} else {
    Write-Ok "Windows 2000 ISO: $Win2KIso"
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

# The host that generated a launcher is not always the host that runs it
# (scripts\local is git-ignored and OneDrive-synced), so the launcher resolves
# QEMU at run time: the generating host's path first, then the two places
# this project has found QEMU on its hosts, then a message naming the override.
$qemuResolve = @(
    "if not defined QEMU set ""QEMU=$qemuSystemCommand""",
    "if not exist ""%QEMU%"" set ""QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe""",
    "if not exist ""%QEMU%"" set ""QEMU=%USERPROFILE%\scoop\apps\qemu\current\qemu-system-x86_64.exe""",
    "if not exist ""%QEMU%"" (",
    "  echo Could not find qemu-system-x86_64.exe on this host - set QEMU to its full path.",
    "  exit /b 1",
    ")"
)

$installCmd = Join-Path $LocalScriptDir "qemu-win2k-xonly-install.cmd"
Write-AsciiFile $installCmd (@(
    "@echo off",
    "rem Windows 2000 SP4 xHCI-only guest - install launcher (roadmap task 19.5).",
    "rem Generated by scripts\setup-qemu-win2k-xonly.ps1; regenerate rather than edit.",
    "rem",
    "rem A Windows 2000 install that NEVER sees a USB controller, the base of the",
    "rem fresh Windows 2000 clone since 2026-09-03 (config.sample.psd1, 2b-fresh).",
    "rem Every earlier Windows 2000 image carried an EHCI whose install placed",
    "rem usbport.sys from Driver Cache\i386, which hid whether the 1.0.1.0 INF",
    "rem places it itself on an xHCI-only machine (lessons.md, 2026-09-03).",
    "rem",
    "rem Same machine as the checkpointed 2b recipe (qemu-win2k-install.cmd): TCG",
    "rem with acpi=off and -apic, the Standard-PC HAL, because on this host family",
    "rem the APIC-clock ISR storms Setup under TCG. Retail FPP media: Setup asks",
    "rem for the product key (the owner types it). No USB controller of any kind",
    "rem is attached. After Setup, before any controller exists, take the reading",
    "rem   dir %windir%\system32\drivers\usb*.sys",
    "rem (expected: usbd.sys, usbcamd*.sys, usbintel.sys; NO usbport.sys,",
    "rem usbhub.sys or usbhub20.sys), shut down, and snapshot:",
    "rem   qemu-img snapshot -c win2k-xonly-clean-install vm\win2k-xonly.img",
    "rem -boot once=d boots the CD for the first boot only; Setup's own reboots",
    "rem stay inside this QEMU session (-action reboot=reset).",
    "set ""WIN2K_ISO=$Win2KIso""",
    "if not exist ""%WIN2K_ISO%"" (",
    "  echo Missing ISO: %WIN2K_ISO%",
    "  exit /b 1",
    ")",
    "if not exist ""$diskImage"" (",
    "  echo Missing image: $diskImage - rerun setup-qemu-win2k-xonly.ps1 -CreateDisk",
    "  exit /b 1",
    ")"
) + $qemuResolve + @(
    """%QEMU%"" ^",
    "  -name ""xhci98 Windows 2000 SP4 xHCI-only"" ^",
    "  -machine pc,acpi=off ^",
    "  -global ide-device.win2k-install-hack=on ^",
    "  -cpu pentium3,-apic ^",
    "  -m $MemoryMb ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -cdrom ""%WIN2K_ISO%"" ^",
    "  -boot once=d ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
))

$runCmd = Join-Path $LocalScriptDir "qemu-win2k-xonly-run.cmd"
Write-AsciiFile $runCmd (@(
    "@echo off",
    "rem Windows 2000 SP4 xHCI-only guest - RUN launcher (roadmap task 19.5).",
    "rem Generated by scripts\setup-qemu-win2k-xonly.ps1; regenerate rather than edit.",
    "rem",
    "rem Same machine as qemu-win2k-xonly-install.cmd (TCG, acpi=off, -apic: the",
    "rem HAL is fixed at install time) plus:",
    "rem  - the xHCI ($($XhciDevice)) ALONE by default: no in-box Windows 2000",
    "rem    driver, so the package installs through the INF's .NTx86 half from",
    "rem    the transfer drive (Have Disk), and the reading is whether the OS",
    "rem    places usbport.sys from Driver Cache\i386 (sp4.cab) itself, whether",
    "rem    the root hub comes up (usbhub20.sys from the OS's own USB.INF when",
    "rem    usbport creates the ROOT_HUB20 PDO, or not), and a hot-plugged mouse.",
    "rem    p3=0 (USB 2.0 root ports only, as every other guest): QEMU pins a",
    "rem    SuperSpeed-capable device to a SuperSpeed-capable port and never",
    "rem    falls it back to USB 2.0.",
    "rem  - a VVFAT transfer drive backed by vm\xferxp (one qemu-flavour package",
    "rem    serves every target: make-package.ps1 -Flavor qemu -OutDir vm\xferxp;",
    "rem    read-only on the host side, snapshot=on gives the guest a throw-away",
    "rem    writable overlay).",
    "rem  - isa-debugcon at 0xE9 -> vm\win2k-xonly-debugcon.log, rotated like the",
    "rem    other guests' logs so a stale DriverEntry cannot be read as this boot's.",
    "rem  - the QEMU xhci trace events of scripts\local\xhci-trace-events.txt,",
    "rem    when that file exists.",
    "rem %1 = run tag (the QEMU trace file name; a relaunch with the same tag",
    "rem      overwrites it).",
    "rem %2 = none  boots with NO USB controller (the drivers listing before any",
    "rem      controller exists);  %2 = ehci  adds the companion EHCI the old",
    "rem      vehicle carried, for comparison only.",
    "rem No USB device is boot-attached: hot-plug from the monitor (port $MonitorPort)",
    "rem after the desktop is up, e.g.  device_add usb-mouse,id=m1,bus=xhci.0",
    "rem The CD stays attached when the ISO is on this host, in case Setup asks.",
    "setlocal",
    "set TAG=%1",
    "if ""%TAG%""=="""" set TAG=run",
    "set ""CTRL=-device $XhciDevice,id=xhci""",
    "if /i ""%2""==""none"" set ""CTRL=""",
    "if /i ""%2""==""ehci"" set ""CTRL=-device $XhciDevice,id=xhci -device usb-ehci,id=ehci""",
    "set ""WIN2K_ISO=$Win2KIso""",
    "set ""CDROM=-cdrom ""%WIN2K_ISO%""""",
    "if not exist ""%WIN2K_ISO%"" (",
    "  echo NOTE: %WIN2K_ISO% is not on this host - booting with no CD attached.",
    "  set ""CDROM=""",
    ")",
    "set ""TRACE=""",
    "if exist ""%~dp0xhci-trace-events.txt"" set ""TRACE=-trace events=%~dp0xhci-trace-events.txt,file=$traceLogBase.%TAG%.log""",
    "if not exist ""$xferDir"" (",
    "  echo Missing transfer directory: $xferDir",
    "  exit /b 1",
    ")",
    "if exist ""$debugConLog"" (",
    "  for %%S in (""$debugConLog"") do if not ""%%~zS""==""0"" (",
    "    move /y ""$debugConLog"" ""$debugConPreviousLog"" >nul",
    "    if errorlevel 1 (",
    "      echo Could not archive the prior debug-console log.",
    "      exit /b 1",
    "    )",
    "  )",
    ")"
) + $qemuResolve + @(
    """%QEMU%"" ^",
    "  -name ""xhci98 Windows 2000 SP4 xHCI-only"" ^",
    "  -machine pc,acpi=off ^",
    "  -global ide-device.win2k-install-hack=on ^",
    "  -cpu pentium3,-apic ^",
    "  -m $MemoryMb ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  %CDROM% ^",
    "  %CTRL% ^",
    "  -chardev file,id=dbgcon,path=""$debugConLog"" ^",
    "  -device isa-debugcon,iobase=0xe9,chardev=dbgcon ^",
    "  %TRACE% ^",
    "  -boot c ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
))

Write-Ok "Wrote $installCmd"
Write-Ok "Wrote $runCmd"

Write-Step "Next steps"
Write-Host "  1. Run scripts\local\qemu-win2k-xonly-install.cmd and install Windows 2000 by hand (a product key is asked for)."
Write-Host "  2. On the desktop, before any controller: dir %windir%\system32\drivers\usb*.sys (expect no usbport.sys, usbhub.sys or usbhub20.sys)."
Write-Host "  3. Shut down from the Start menu (quit at the monitor once 'safe to turn off' is up); snapshot: qemu-img snapshot -c win2k-xonly-clean-install vm\win2k-xonly.img"
Write-Host "  4. Stage the package: make-package.ps1 -Flavor qemu -OutDir vm\xferxp; boot qemu-win2k-xonly-run.cmd <tag> and install it from the transfer drive (Have Disk)."
Write-Host "  5. Read vm\win2k-xonly-debugcon.log (DriverEntry, StartController, the RH_* family) and the drivers directory again (usbport.sys placed; usbhub20.sys, or not)."
