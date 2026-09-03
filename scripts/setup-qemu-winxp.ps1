<#
.SYNOPSIS
Create the Windows XP SP3 (32-bit) guest disk image and QEMU launchers (roadmap Phase 19).

.DESCRIPTION
Windows XP is the best-effort secondary target (AGENTS.md Quick Reference;
docs\usb-xhci-info\win98-wdm.md, "What about Windows XP?"). This guest exists
to observe the one binary on XP itself; it is not a checkpointed target and
nothing waits on it. Roadmap Phase 19 holds the readings it has given and the
ones it still owes, and docs\contributing\build-and-test.md, "Windows XP target
VM", the recipe. The launchers this writes are the ones that took the first
readings on 2026-09-03, reproduced here so a new host regenerates them
instead of inheriting another host's hand-edited copy.

WHPX, NOT TCG. XP Setup selects the ACPI APIC HAL, and under TCG this host
family storms the APIC-clock ISR on the Windows 2000 Setup workload
(docs\contributing\lessons.md, "The vector-0xD1 storm is the accelerator").
The win2k-acpi launcher's -accel whpx,kernel-irqchip=off is what ran that
Setup, and the same flags ran XP Setup to the desktop with no storm. Probe
WHPX on a new host before trusting it (the lessons entry names the probe).

The run launcher attaches qemu-xhci alone by default. That is the reading
release 1.0.0.2 claims: an NT install that never had a USB controller has no
usbport.sys (Code 39 on the first XP boot, 2026-09-03), and since 1.0.0.2 the
INF has Windows place it from Driver Cache\i386 itself. Pass "ehci" as the
launcher's second argument to add the companion EHCI the spike used before
that fix, which makes the in-box stack place usbport.sys instead.

.PARAMETER WinXpIso
The XP SP3 32-bit CD image. The run launcher keeps it attached when it exists
and boots with no CD when it does not, and says so; Setup's own reboots need
-boot d, which the install launcher uses every boot (the CD's "Press any key"
falls through to the hard disk).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-winxp.ps1 -WinXpIso D:\isos\winxp-sp3.iso -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$WinXpIso = "D:\isos\en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso",
    [string]$DiskSize = "8G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    [int]$MonitorPort = 55559,
    [int]$MemoryMb = 512,
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
$diskImage = Join-Path $VmDir "winxp.img"
$debugConLog = Join-Path $VmDir "winxp-debugcon.log"
$debugConPreviousLog = Join-Path $VmDir "winxp-debugcon.previous.log"
$traceLogBase = Join-Path $VmDir "winxp-qemu-trace"
Write-Ok "VM directory: $VmDir"
Write-Ok "Local script directory: $LocalScriptDir"
Write-Ok "Transfer (VVFAT) directory: $xferDir"

if (-not (Test-Path -LiteralPath $WinXpIso)) {
    Write-Warn "Windows XP ISO not found at: $WinXpIso (pass -WinXpIso to override)."
} else {
    Write-Ok "Windows XP ISO: $WinXpIso"
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

$installCmd = Join-Path $LocalScriptDir "qemu-winxp-install.cmd"
Write-AsciiFile $installCmd (@(
    "@echo off",
    "rem Windows XP Professional SP3 (32-bit) guest - install launcher.",
    "rem Generated by scripts\setup-qemu-winxp.ps1; regenerate rather than edit.",
    "rem",
    "rem XP is the best-effort secondary target (AGENTS.md Quick Reference;",
    "rem docs\usb-xhci-info\win98-wdm.md, ""What about Windows XP?""). This guest",
    "rem takes the runtime observation of the one binary on XP itself. It is not a",
    "rem checkpointed target and nothing waits on it.",
    "rem",
    "rem WHPX, NOT TCG. XP Setup selects the ACPI APIC HAL, and under TCG this",
    "rem host family storms the APIC-clock ISR on the Windows 2000 Setup workload",
    "rem (docs\contributing\lessons.md, ""The vector-0xD1 storm is the accelerator"");",
    "rem the win2k-acpi launcher's -accel whpx,kernel-irqchip=off is what ran that",
    "rem Setup, and the same flags ran XP Setup to the desktop on 2026-09-03.",
    "rem -vga std because cirrus does not restore across an ACPI sleep.",
    "rem",
    "rem The xHCI is absent during install, as for every other guest, so it",
    "rem appears as a fresh unrecognised device on the first boot from the run",
    "rem launcher. -boot d every time: the XP CD's ""Press any key to boot from CD""",
    "rem falls through to the hard disk when no key is pressed, which is what",
    "rem Setup's own reboots need (text phase -> GUI phase -> first boot).",
    "set ""WINXP_ISO=$WinXpIso""",
    "if not exist ""%WINXP_ISO%"" (",
    "  echo Missing ISO: %WINXP_ISO%",
    "  exit /b 1",
    ")",
    "if not exist ""$diskImage"" (",
    "  echo Missing image: $diskImage - rerun setup-qemu-winxp.ps1 -CreateDisk",
    "  exit /b 1",
    ")"
) + $qemuResolve + @(
    """%QEMU%"" ^",
    "  -name ""xhci98 Windows XP SP3"" ^",
    "  -machine pc ^",
    "  -accel whpx,kernel-irqchip=off ^",
    "  -cpu pentium3 ^",
    "  -m $MemoryMb ^",
    "  -vga std ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -cdrom ""%WINXP_ISO%"" ^",
    "  -boot d ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
))

$runCmd = Join-Path $LocalScriptDir "qemu-winxp-run.cmd"
Write-AsciiFile $runCmd (@(
    "@echo off",
    "rem Windows XP Professional SP3 (32-bit) guest - RUN launcher.",
    "rem Generated by scripts\setup-qemu-winxp.ps1; regenerate rather than edit.",
    "rem",
    "rem Same machine as qemu-winxp-install.cmd (ACPI on, WHPX with",
    "rem kernel-irqchip=off, -vga std) plus:",
    "rem  - the xHCI ($($XhciDevice)): no in-box XP driver for PCI\CC_0C0330, so",
    "rem    it shows as an unrecognised ""Universal Serial Bus (USB) Controller"" and",
    "rem    the package installs through the INF's .NTx86 half from the transfer",
    "rem    drive. p3=0 (USB 2.0 root ports only, as every other guest's launcher)",
    "rem    because QEMU pins a SuperSpeed-capable device to a SuperSpeed-capable",
    "rem    port and does not model the USB 2.0 fallback real hardware gives: on",
    "rem    the default 4+4 layout a hot-plugged usb-storage attached at 5000 Mb/s",
    "rem    on a USB3 port this driver leaves unmanaged (""port event: not a",
    "rem    managed port"") and XP never saw it (2026-09-03, run p194). The 4+4",
    "rem    layout's own reading (4 USB2 companions managed, 4 USB3 unpowered) is",
    "rem    in build-and-test.md; regenerate with -XhciDevice qemu-xhci for it.",
    "rem  - an audio backend (-audiodev none,id=xpaud) so a composite usb-audio",
    "rem    can be hot-plugged: device_add usb-audio,id=a1,bus=xhci.0,audiodev=xpaud",
    "rem  - a VVFAT transfer drive backed by vm\xferxp (read-only on the host",
    "rem    side; snapshot=on gives the guest a throw-away writable overlay). It",
    "rem    carries the qemu-flavour package, the only flavour that writes the",
    "rem    0xE9 trace: make-package.ps1 -Flavor qemu -OutDir vm\xferxp.",
    "rem  - isa-debugcon at 0xE9 -> vm\winxp-debugcon.log, rotated like the other",
    "rem    guests' logs so a stale DriverEntry cannot be read as this boot's.",
    "rem  - the QEMU xhci trace events of scripts\local\xhci-trace-events.txt,",
    "rem    when that file exists.",
    "rem  - NO companion EHCI by default. An NT install that never had a USB",
    "rem    controller has no usbport.sys (Code 39 on the first XP boot,",
    "rem    2026-09-03: XP keeps it in Driver Cache\i386\sp3.cab and layout.inf",
    "rem    says do not copy it at Setup), and since 1.0.0.2 the INF has Windows",
    "rem    place it itself. That xHCI-only reading is what roadmap task 19.4",
    "rem    takes. %2 = ehci adds the companion EHCI the spike used before that",
    "rem    fix: the in-box stack binding it is what placed usbport.sys then.",
    "rem No USB device is boot-attached: hot-plug from the monitor (port $MonitorPort)",
    "rem after the desktop is up, e.g.  device_add usb-mouse,id=m1,bus=xhci.0",
    "rem (no port= is needed: QEMU takes the first free root port, and a number",
    "rem above the port count is refused).",
    "rem The CD stays attached when the ISO is on this host, in case XP asks.",
    "rem %1 = run tag for the QEMU trace file name.",
    "setlocal",
    "set TAG=%1",
    "if ""%TAG%""=="""" set TAG=run",
    "set ""EHCI=""",
    "if /i ""%2""==""ehci"" set ""EHCI=-device usb-ehci,id=ehci""",
    "set ""WINXP_ISO=$WinXpIso""",
    "set ""CDROM=-cdrom ""%WINXP_ISO%""""",
    "if not exist ""%WINXP_ISO%"" (",
    "  echo NOTE: %WINXP_ISO% is not on this host - booting with no CD attached.",
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
    "  -name ""xhci98 Windows XP SP3"" ^",
    "  -machine pc ^",
    "  -accel whpx,kernel-irqchip=off ^",
    "  -cpu pentium3 ^",
    "  -m $MemoryMb ^",
    "  -vga std ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  %CDROM% ^",
    "  %EHCI% ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -audiodev none,id=xpaud ^",
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
Write-Host "  1. Run scripts\local\qemu-winxp-install.cmd and install Windows XP by hand (a product key is asked for)."
Write-Host "  2. Shut the guest down from the Start menu; take a snapshot: qemu-img snapshot -c winxp-clean-install vm\winxp.img"
Write-Host "  3. Stage the package: make-package.ps1 -Flavor qemu -OutDir vm\xferxp"
Write-Host "  4. Boot qemu-winxp-run.cmd <tag> and install the driver from the transfer drive (Have Disk)."
Write-Host "  5. Read vm\winxp-debugcon.log: DriverEntry, USBPORT_GetHciMn=10000001, StartController, the RH_* family."
