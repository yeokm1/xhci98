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
    # Size of the qcow2 created by -CreateDisk, in qemu-img's own notation.
    # Only read on creation: it cannot resize an image that already exists.
    [string]$DiskSize = "4G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci",
    # The HMP monitor port the run launcher listens on. It must be UNIQUE
    # across every launcher this repository generates, because the matrix
    # addresses a guest by its port and two guests sharing one would answer
    # for each other; scripts\test-qemu-launchers.ps1 asserts that no two
    # generated launchers share a port.
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

# Get-QemuTool lives in common.ps1 - there were five copies of it and they
# had drifted (the 2026-09-07 audit's H28).

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

# QEMU is resolved at RUN time by each launcher, not baked in here: the host
# that generated a launcher is not always the host that runs it
# (scripts\local is git-ignored and OneDrive-synced), and
# `setup-qemu.ps1 -Install` in particular writes launchers in a process whose
# PATH predates the install it just performed. One resolver for all five
# generators, in common.ps1 (the 2026-09-07 audit's H28 and H29).
$qemuResolve = Get-QemuLauncherResolver -FoundPath $qemuSystemCommand

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
#
# **A floppy controller on the run launcher**, empty at boot. build-and-test.md
# has said since batch 13-L that "the launcher now carries `-drive if=floppy`",
# and no generator wrote one - the fix was made by hand in the git-ignored
# `scripts\local\` copy and was lost the next time this script ran, which is
# the trap that doc paragraph goes on to name. The 2026-09-07 audit's H27.
#
# What it buys: `change floppy0 <path>` on the monitor inserts a disk into a
# running guest at once, so `copy C:\SNAP.TXT A:` gets a file out of Windows
# 2000 without a reboot and without the VVFAT disk, which is read-only. A
# controller cannot be added live, so it has to be here at boot even though it
# is empty; an empty floppy drive costs a Windows 2000 guest nothing.
#
# **EMPTY, not `file=vm\transfer.img`.** The first cut of this fix mounted the
# shared courier image, which setup-qemu.ps1's Windows 98 run launcher already
# mounts WRITABLE under the same path - so booting 2a and 2b together handed
# one raw image to two guests to write, and a floppy image is a FAT volume
# with no arbitration whatsoever. Empty is also what the workflow wants: the
# whole point of `change floppy0` is choosing the disk at the moment the file
# is ready, on a guest that is already up. Verified on QEMU 11: `-drive
# if=floppy` with no `file=` starts, `info block` shows `floppy0: [not
# inserted]`, and `change floppy0 <path>` inserts into it.
$transferImage = Join-Path $VmDir "transfer.img"
# Shared with setup-qemu.ps1's guests, which is deliberate: it is a courier,
# and one blank 1.44 MB image serves every target. Created here too so this
# script stands alone on a host where only the Windows 2000 guest exists.
#
# No launcher this script writes MOUNTS it - see the note above - so nothing
# here creates the two-writer hazard on its own. The sharing is not thereby
# harmless: setup-qemu.ps1's Windows 98 run launcher DOES mount it at boot, so
# inserting it into this guest with `change floppy0` while that one is up is
# still two writers on one FAT image. That is a per-use precondition rather
# than a launcher defect, so it is stated where the operator is told to do it -
# the next-steps text below - and not silently relied on.
if (-not (Test-Path -LiteralPath $transferImage)) {
    $stream = [System.IO.File]::Open($transferImage, [System.IO.FileMode]::CreateNew)
    try {
        $stream.SetLength(1474560)
    } finally {
        $stream.Close()
    }
    Write-Ok "Created blank 1.44 MB transfer floppy image: $transferImage"
    Write-Warn "It is blank; format it inside a guest before first use."
}
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
# Both Win2000 generators stage this file the same way and pin it to the same
# length and version; common.ps1 holds the one copy (audit J6).
$stagedUsbd = Join-Path $xferDir "USBD.SYS"
Install-Win2KUsbdSys -XferDir $xferDir -Win2KUsbdSys $Win2KUsbdSys | Out-Null

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
Write-AsciiFile $installCmd (@(
    "@echo off"
) + $qemuResolve + @(
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
    """%QEMU%"" ^",
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
))

$runCmd = Join-Path $LocalScriptDir "qemu-win2k-run.cmd"
Write-AsciiFile $runCmd (@(
    "@echo off"
) + $qemuResolve + @(
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
    """%QEMU%"" ^",
    "  -name ""xhci98 Windows 2000 SP4 differential"" ^",
    "  -machine pc,acpi=off ^",
    "  -cpu pentium3,-apic ^",
    "  -m 256 ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive if=floppy ^",
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
))
$prepareCmd = Join-Path $LocalScriptDir "qemu-win2k-prepare-usbd.cmd"
Write-AsciiFile $prepareCmd (@(
    "@echo off"
) + $qemuResolve + @(
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
    """%QEMU%"" ^",
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
))

Write-Ok "Wrote $installCmd"
Write-Ok "Wrote $prepareCmd"
Write-Ok "Wrote $runCmd"

Write-Step "Next steps"
Write-Host "  1. Run scripts\local\qemu-win2k-install.cmd and install Windows 2000 (needs a product key)."
Write-Host "  2. Ensure SP4 USBD.SYS is staged (use -Win2KUsbdSys if needed), then boot qemu-win2k-prepare-usbd.cmd and copy it into C:\WINNT\system32\drivers."
Write-Host "  3. Shut down, then boot qemu-win2k-run.cmd (adds EHCI + xHCI)."
Write-Host "  4. Do NOT install NUSB - the usbport stack is native to Win2000 SP4."
Write-Host "  5. To get a file OUT of the running guest: on its monitor, 'change floppy0 $transferImage',"
Write-Host "     then 'copy C:\SNAP.TXT A:' in the guest, then 'eject floppy0' when the copy is done."
Write-Host "     transfer.img is SHARED with the Windows 98 guest, whose run launcher mounts it at boot,"
Write-Host "     so insert it here only while that guest is down - two writers on one FAT image corrupt it."
