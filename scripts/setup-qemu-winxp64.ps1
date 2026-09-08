<#
.SYNOPSIS
Create the Windows XP Professional x64 SP2 guest disk image and QEMU launchers (roadmap Phase 21).

.DESCRIPTION
The 64-bit target guest. Design record 11
(docs\contributing\design\11-x64-targets.md) establishes that Windows XP x64
and Windows Server 2003 x64 are the same operating system, NT 5.2.3790, so a
single WNET amd64 binary serves both and this one guest observes it. Roadmap
Phase 21 holds what it owes; docs\contributing\build-and-test.md, "Windows XP
x64 target VM", is the recipe.

This is the sibling of scripts\setup-qemu-winxp.ps1 and most of the 32-bit XP
recipe carries over unchanged. FOUR THINGS DIFFER, and the first two fail
silently or refuse to boot if the 32-bit launcher is copied across:

  -cpu     The 32-bit guest uses pentium3, which has NO LONG MODE. An x64
           guest needs an x86-64 model; qemu64 is the default here and
           -Cpu selects another (core2duo is the other value the design
           record names), so a different model is a regenerated launcher and
           not a hand edit.
  RAM      512 MB is the 32-bit figure; XP x64 wants more and 2048 is
           comfortable. RAM SIZE IS NOT A CORRECTNESS QUESTION HERE:
           measurement M5 (design record 11 section 5) read the NT 5.2 amd64
           usbport creating its DMA adapter 32-bit (Dma32BitAddresses = 1,
           DmaWidth = Width32Bits), so nothing lands above 4 GB whatever the
           guest has. The cap the design record originally called for is a
           convenience now, not a mitigation.
  Disk     8 GB is tight for XP x64; 16 GB by default. vm\winxp64.img.
  Monitor  55562. 55555-55561 are taken (2a, 2b, the SMP 2d, the ACPI-HAL
           Windows 2000 machine, 32-bit XP, the xHCI-only Windows 2000
           machine and Windows ME, in that order); test-qemu-launchers.ps1
           refuses two guests on one port.

qemu-system-x86_64.exe is already what the 32-bit XP launcher uses, so that
does not change.

TCG, NOT WHPX - the reverse of every 32-bit guest here, and measured rather
than assumed (host minis-w11p-ykm, 2026-09-08). Under
-accel whpx,kernel-irqchip=off, XP x64 Setup wedges on "Setup is starting
Windows" indefinitely with RIP pinned; under -accel tcg, the identical command
line with one flag changed reaches "Setup is copying files" in about three
minutes. WHPX itself initialises fine, so this is the guest and not partition
creation. lessons.md's "The vector-0xD1 storm is the accelerator" reads the
other way, but that was 32-bit Windows 2000 Setup under the ACPI APIC HAL - a
different guest, bitness and workload. Neither reading generalises to the
other, so probe both on a new host. See the -Accel parameter.

THERE IS NO amd64 BINARY TO INSTALL YET. This guest is preparation, not the
Phase 21 task 21.5 leg itself: 21.5 depends on 21.2 (the WNET amd64 build) and
21.4 (the code changes measurement 21.1 implies). The guest gets installed and
snapshotted and then sits. Its existence is not progress on 21.5.

Two readings are worth taking as soon as the OS is up, and neither needs a
driver: hash the guest's installed usbport.sys against
tools\winxp64-extracted\usbport.sys (confirming the file Setup placed is the
one every M1-M6 reading was taken from), and check whether an xHCI-only XP x64
install has usbport.sys on disk at all. On 32-bit XP it did not, which was the
Code 39 that release 1.0.1.0's INF fix answers; the expectation is that x64
behaves the same from Driver Cache\amd64, but that is an expectation.

The run launcher attaches qemu-xhci alone by default, for that second reading.
Pass "ehci" as the launcher's second argument to add a companion EHCI, which
makes the in-box stack place usbport.sys instead - the same escape hatch the
32-bit launcher carries.

.PARAMETER WinXp64Iso
The XP Professional x64 SP2 CD image. Empty by default, like every other ISO
parameter in these scripts (the 2026-09-07 audit's J3): a default naming one
host's media is a tracked file that names nothing on any other host. The
generated launchers carry the "edit this file or pass -WinXp64Iso" guard. The
run launcher keeps the CD attached when it exists and boots with no CD when it
does not, and says so; Setup's own reboots need -boot d, which the install
launcher uses every boot (the CD's "Press any key" falls through to the hard
disk).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-winxp64.ps1 -WinXp64Iso "D:\isos\Win XP SP2 VL x64.iso" -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$WinXp64Iso = "",
    [string]$DiskSize = "16G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    # An x86-64 model. pentium3, the 32-bit guest's, has no long mode and XP
    # x64 Setup will not boot on it.
    [string]$Cpu = "qemu64",
    #
    # **TCG, AND ON THIS GUEST THAT IS NOT THE FALLBACK - IT IS THE ONE THAT
    # WORKS.** This is the reverse of every 32-bit guest in this project and it
    # was measured, on host minis-w11p-ykm, 2026-09-08:
    #
    #   -accel whpx,kernel-irqchip=off  XP x64 Setup wedges on "Setup is
    #     starting Windows" and stays there. Six minutes, RIP pinned at one
    #     address across five samples four seconds apart, CS64/CPL=0, IF set.
    #     WHPX initialises fine (the throwaway probe passes), so this is the
    #     guest, not partition creation.
    #   -accel tcg  the identical command line, one flag changed: text-mode
    #     Setup reaches "Setup is copying files" within about three minutes and
    #     RIP samples are varied and productive.
    #
    # So the accelerator is the discriminating variable here too, pointing the
    # other way. lessons.md's "The vector-0xD1 storm is the accelerator" read
    # TCG storming and WHPX running - but that was 32-bit Windows 2000 Setup
    # under the ACPI APIC HAL, a different guest, a different bitness and a
    # different workload. Neither reading generalises to the other; both are
    # per-host, per-guest facts and both had to be measured.
    #
    # THE ACCELERATOR IS NOT A FLAG TO FLIP ON AN INSTALLED SYSTEM. The HAL is
    # fixed at install time, so a guest installed under one rung must be booted
    # under it too (the 2b lesson) - which is why both launchers carry this one
    # value, and why changing it means redoing the install. Regenerate with
    # -Accel rather than hand-editing scripts\local (the J3 rule); the
    # setup-qemu-win2k-smp.ps1 -Accel parameter is the precedent.
    #
    [string]$Accel = "tcg",
    [int]$MonitorPort = 55562,
    [int]$MemoryMb = 2048,
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

Write-Step "Checking QEMU"
$qemuSystem = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-system-x86_64.exe"
$qemuImg = Get-QemuTool -QemuBinDir $QemuBinDir -ToolName "qemu-img.exe"

if ($null -eq $qemuSystem) {
    Write-Warn "qemu-system-x86_64.exe is not on PATH. Install QEMU (see setup-qemu.ps1) or pass -QemuBinDir."
    $qemuSystemCommand = ""
} else {
    Write-Ok "Found $qemuSystem"
    $qemuSystemCommand = $qemuSystem
}

if ($null -eq $qemuImg) {
    Write-Warn "qemu-img.exe is not on PATH. Disk image creation will be skipped unless QEMU is installed."
} else {
    Write-Ok "Found $qemuImg"
}

# The 32-bit guest's CPU model has no long mode, and copying it across is the
# first of the four things this recipe changes. Refusing it here is cheaper
# than reading the failure off a guest that never leaves the BIOS.
if ($Cpu -match '^(pentium|486|coreduo|athlon-|n270)') {
    throw "-Cpu $Cpu has no long mode; Windows XP x64 needs an x86-64 model (qemu64, core2duo, ...)."
}

Write-Step "Creating local directories"
Ensure-Directory $VmDir
Ensure-Directory $LocalScriptDir
$xferDir = Join-Path $VmDir "xferxp64"
Ensure-Directory $xferDir
$diskImage = Join-Path $VmDir "winxp64.img"
$debugConLog = Join-Path $VmDir "winxp64-debugcon.log"
$debugConPreviousLog = Join-Path $VmDir "winxp64-debugcon.previous.log"
$traceLogBase = Join-Path $VmDir "winxp64-qemu-trace"
Write-Ok "VM directory: $VmDir"
Write-Ok "Local script directory: $LocalScriptDir"
Write-Ok "Transfer (VVFAT) directory: $xferDir"

# The empty case first: -LiteralPath refuses an empty string outright, and
# under "Stop" that ends the run with a parameter-binding error instead of the
# sentence the operator needs.
if ([string]::IsNullOrWhiteSpace($WinXp64Iso)) {
    Write-Warn "No -WinXp64Iso given. The launchers are still written; edit the WINXP64_ISO line in them, or re-run with -WinXp64Iso <path>."
} elseif (-not (Test-Path -LiteralPath $WinXp64Iso)) {
    Write-Warn "Windows XP x64 ISO not found at: $WinXp64Iso (pass -WinXp64Iso to override)."
} else {
    Write-Ok "Windows XP x64 ISO: $WinXp64Iso"
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
Write-Ok "Using CPU model: $Cpu; $MemoryMb MB; monitor port $MonitorPort"

# QEMU is resolved at RUN time by the launcher, not baked in here: the host
# that generated a launcher is not always the host that runs it
# (scripts\local is git-ignored and OneDrive-synced). One resolver for all
# the generators, in common.ps1 (the 2026-09-07 audit's H28).
$qemuResolve = Get-QemuLauncherResolver -FoundPath $qemuSystemCommand

$installCmd = Join-Path $LocalScriptDir "qemu-winxp64-install.cmd"
Write-AsciiFile $installCmd (@(
    "@echo off",
    "rem Windows XP Professional x64 SP2 guest - install launcher.",
    "rem Generated by scripts\setup-qemu-winxp64.ps1; regenerate rather than edit.",
    "rem",
    "rem The 64-bit target of roadmap Phase 21. XP x64 and Server 2003 x64 are the",
    "rem same operating system (NT 5.2.3790), so one WNET amd64 binary serves both",
    "rem and this one guest observes it (design record 11).",
    "rem",
    "rem -cpu $Cpu, NOT the 32-bit guest's pentium3: that model has no long mode.",
    "rem $MemoryMb MB, not 512. RAM size is not a correctness question here -",
    "rem measurement M5 read the NT 5.2 amd64 usbport creating its DMA adapter",
    "rem 32-bit, so nothing lands above 4 GB whatever the guest has.",
    "rem",
    "rem Accelerator: -accel $Accel. TCG is the DEFAULT for this guest and not a",
    "rem fallback - it is the one that works. Measured 2026-09-08: under",
    "rem whpx,kernel-irqchip=off XP x64 Setup wedges on ""Setup is starting",
    "rem Windows"" indefinitely with RIP pinned; the same command line under tcg",
    "rem reaches ""Setup is copying files"" in about three minutes. That is the",
    "rem reverse of the 32-bit guests here (lessons.md, ""The vector-0xD1 storm is",
    "rem the accelerator"", which read 32-bit Windows 2000 Setup); neither reading",
    "rem generalises to the other. Regenerate with -Accel to change rung - and",
    "rem note the HAL is fixed at INSTALL time, so a guest installed under one",
    "rem rung must be booted under it too.",
    "rem -vga std because cirrus does not restore across an ACPI sleep.",
    "rem",
    "rem The xHCI is absent during install, as for every other guest, so it",
    "rem appears as a fresh unrecognised device on the first boot from the run",
    "rem launcher. -boot d every time: the CD's ""Press any key to boot from CD""",
    "rem falls through to the hard disk when no key is pressed, which is what",
    "rem Setup's own reboots need (text phase -> GUI phase -> first boot).",
    "rem",
    "rem The media is volume-licence, so Setup asks for a key. The owner drives",
    "rem Setup at the console - the standing decision of 2026-09-03, taken for the",
    "rem 32-bit XP guest and unchanged here.",
    "set ""WINXP64_ISO=$WinXp64Iso""",
    "if ""%WINXP64_ISO%""=="""" (",
    "  echo Edit this file or set -WinXp64Iso when running setup-qemu-winxp64.ps1.",
    "  exit /b 1",
    ")",
    "if not exist ""%WINXP64_ISO%"" (",
    "  echo Missing ISO: %WINXP64_ISO%",
    "  exit /b 1",
    ")",
    "if not exist ""$diskImage"" (",
    "  echo Missing image: $diskImage - rerun setup-qemu-winxp64.ps1 -CreateDisk",
    "  exit /b 1",
    ")"
) + $qemuResolve + @(
    """%QEMU%"" ^",
    "  -name ""xhci98 Windows XP x64 SP2"" ^",
    "  -machine pc ^",
    "  -accel $Accel ^",
    "  -cpu $Cpu ^",
    "  -m $MemoryMb ^",
    "  -vga std ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -cdrom ""%WINXP64_ISO%"" ^",
    "  -boot d ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
))

$runCmd = Join-Path $LocalScriptDir "qemu-winxp64-run.cmd"
Write-AsciiFile $runCmd (@(
    "@echo off",
    "rem Windows XP Professional x64 SP2 guest - RUN launcher.",
    "rem Generated by scripts\setup-qemu-winxp64.ps1; regenerate rather than edit.",
    "rem",
    "rem Same machine as qemu-winxp64-install.cmd (ACPI on, -accel $Accel,",
    "rem -cpu $Cpu, $MemoryMb MB, -vga std) plus:",
    "rem  - the xHCI ($($XhciDevice)): no in-box XP x64 driver for PCI\CC_0C0330,",
    "rem    so it shows as an unrecognised ""Universal Serial Bus (USB) Controller""",
    "rem    and the package installs through the INF's .NTamd64 half from the",
    "rem    transfer drive. p3=0 (USB 2.0 root ports only, as every other guest's",
    "rem    launcher) because QEMU pins a SuperSpeed-capable device to a",
    "rem    SuperSpeed-capable port and does not model the USB 2.0 fallback real",
    "rem    hardware gives: on the default 4+4 layout a hot-plugged usb-storage",
    "rem    attached at 5000 Mb/s lands on a USB3 port this driver leaves",
    "rem    unmanaged and the guest never saw it (2026-09-03, run p194, on the",
    "rem    32-bit guest). Regenerate with -XhciDevice qemu-xhci for the 4+4",
    "rem    layout's own reading.",
    "rem  - an audio backend (-audiodev none,id=xp64aud) so a composite usb-audio",
    "rem    can be hot-plugged: device_add usb-audio,id=a1,bus=xhci.0,audiodev=xp64aud",
    "rem  - a VVFAT transfer drive backed by vm\xferxp64 (read-only on the host",
    "rem    side; snapshot=on gives the guest a throw-away writable overlay). It",
    "rem    carries the qemu-flavour package, the only flavour that writes the",
    "rem    0xE9 trace. THERE IS NO amd64 BINARY YET: roadmap task 21.5 waits on",
    "rem    21.2 (the WNET amd64 build) and 21.4 (the code changes 21.1 implies).",
    "rem  - isa-debugcon at 0xE9 -> vm\winxp64-debugcon.log, rotated like the",
    "rem    other guests' logs so a stale DriverEntry cannot be read as this",
    "rem    boot's.",
    "rem  - the QEMU xhci trace events of scripts\local\xhci-trace-events.txt,",
    "rem    when that file exists.",
    "rem  - NO companion EHCI by default. Whether an xHCI-only XP x64 install has",
    "rem    usbport.sys on disk at all is one of the two cheap readings this guest",
    "rem    is asked for: on 32-bit XP it did NOT (Code 39 on the first boot,",
    "rem    2026-09-03), and since 1.0.1.0 the INF has Windows place it from",
    "rem    Driver Cache. The x64 expectation is the same from Driver Cache\amd64,",
    "rem    but it is an expectation. %2 = ehci adds the companion EHCI, which",
    "rem    makes the in-box stack place usbport.sys instead.",
    "rem No USB device is boot-attached: hot-plug from the monitor (port $MonitorPort)",
    "rem after the desktop is up, e.g.  device_add usb-mouse,id=m1,bus=xhci.0",
    "rem (no port= is needed: QEMU takes the first free root port, and a number",
    "rem above the port count is refused). QEMU 11.1.0-rc2 parks a boot in",
    "rem SeaBIOS's SMM handler when a USB device is attached at boot",
    "rem (docs\contributing\lessons.md).",
    "rem The CD stays attached when the ISO is on this host, in case Setup asks.",
    "rem %1 = run tag for the QEMU trace file name.",
    "setlocal",
    "set TAG=%1",
    "if ""%TAG%""=="""" set TAG=run",
    "set ""EHCI=""",
    "if /i ""%2""==""ehci"" set ""EHCI=-device usb-ehci,id=ehci""",
    "set ""WINXP64_ISO=$WinXp64Iso""",
    "set ""CDROM=-cdrom ""%WINXP64_ISO%""""",
    "if not exist ""%WINXP64_ISO%"" (",
    "  echo NOTE: %WINXP64_ISO% is not on this host - booting with no CD attached.",
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
    "  -name ""xhci98 Windows XP x64 SP2"" ^",
    "  -machine pc ^",
    "  -accel $Accel ^",
    "  -cpu $Cpu ^",
    "  -m $MemoryMb ^",
    "  -vga std ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  %CDROM% ^",
    "  %EHCI% ^",
    "  -device $XhciDevice,id=xhci ^",
    "  -audiodev none,id=xp64aud ^",
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
Write-Host "  1. Run scripts\local\qemu-winxp64-install.cmd and install Windows XP x64 by hand (the VL media asks for a key)."
Write-Host "  2. Shut the guest down from the Start menu; take a snapshot: qemu-img snapshot -c winxp64-clean-install vm\winxp64.img"
Write-Host "  3. Two cheap readings, neither needing a driver: hash the guest's usbport.sys against tools\winxp64-extracted\usbport.sys,"
Write-Host "     and check whether an xHCI-only install has usbport.sys on disk at all."
Write-Host "  4. Then WAIT. Task 21.5 needs an amd64 binary, which depends on 21.2 and 21.4; this guest is preparation, not progress on 21.5."
