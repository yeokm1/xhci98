<#
.SYNOPSIS
Create the Windows 2000 SP4 SMP stress VM disk image and QEMU launchers (Phase 2d).

.DESCRIPTION
Phase 2d stands up a THIRD VM, separate from the Phase 2a Win98 VM and the Phase
2b Win2000 differential VM: Windows 2000 SP4 with 2 vCPUs running the SMP kernel
under a multiprocessor HAL, hardware-accelerated by WHPX. It is a race detector,
not a development loop.

Why it must be its own VM: on a uniprocessor NT kernel KeAcquireSpinLock only
raises IRQL - it never spins - so a missing interior lock between the miniport's
submit path and its InterruptDpc path (which usbport runs under DIFFERENT locks;
docs/usb-xhci-info/usbport-miniport-abi.md section 7) is structurally invisible on Win98
(uniprocessor forever) and on the 2b VM (Standard-PC uniprocessor HAL). Only an
MP kernel with real contended spinlocks can express it. The HAL is chosen at
install time, so adding -smp 2 to the installed 2b VM changes nothing.

FLAG CONTRAST WITH 2b - the two VMs sit on opposite sides of the APIC decision
and neither's flags may drift toward the other's:

                2b differential VM          2d SMP VM (this script)
  Accelerator   TCG                         -accel whpx,kernel-irqchip=off
  Machine       -machine pc,acpi=off        -machine pc         (ACPI on)
  CPU           -cpu pentium3,-apic         -cpu pentium3       (APIC present)
  SMP           1 vCPU                      -smp 2   (Win2000 Pro caps at 2)
  RAM           -m 256                      -m 512   (Verifier Special Pool)
  Monitor       55556                       55557
  Disk          vm\win2k.img                vm\win2k-smp.img

2b REMOVES the local APIC because the TCG APIC clock (IDT vector 0xD1) storms
and livelocks Setup on this host (docs/contributing/lessons.md, the Standard-PC HAL entry). Every Win2000
multiprocessor HAL REQUIRES the local APIC, so 2d restores it and relies on
the WHPX execution path to avoid the storm.

Phase 2d proved that the working rung is host-dependent, so the accelerator and
the ACPI decision are PARAMETERS rather than hard-coded text: roadmap Phase 2d
task 2 defines a fallback ladder, and each rung must be reproducible from a
recorded command line rather than a hand-edited launcher copy. The checkpoint
host could not initialise plain WHPX, so the proven rung is the safe default:

  rung 0            -Accel whpx                         (explicit host probe)
  rung 1 (default)  -Accel "whpx,kernel-irqchip=off"    (checkpoint config)
  rung 2            -Accel "tcg,thread=multi" -AcpiOff     (Setup picks the
                    MPS Multiprocessor PC HAL; MTTCG keeps one host thread per
                    vCPU, so cross-CPU races stay real)

The APIC is never masked off on any rung - without it no multiprocessor HAL can
be installed, which is the whole point of this VM.

Everything else is inherited from setup-qemu-win2k.ps1: the USBD.SYS staging and
validation, the three-launcher pattern (install / USBD preparation / run), the
read-only VVFAT transfer disk, and the per-boot debug-console trace rotation.
The vm\xfer directory is shared with 2b on purpose - it already stages USBD.SYS,
and a read-only VVFAT backing is safe to share between VMs.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win2k-smp.ps1 -CreateDisk

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win2k-smp.ps1 -Accel "whpx"
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
    [int]$MonitorPort = 55557,
    [int]$Smp = 2,
    [int]$MemoryMb = 512,
    [string]$Accel = "whpx,kernel-irqchip=off",
    [switch]$AcpiOff,
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

# The accelerator is a ladder rung, so it is stated in every launcher rather
# than assumed. WHPX additionally needs the host hypervisor, which is a Phase 2c
# prerequisite - warn here rather than let a guest fail obscurely.
$accelName = ($Accel -split ",")[0]
if ($accelName -eq "whpx") {
    if ($null -ne $qemuSystem) {
        $accelHelp = ""
        try {
            $accelHelp = (& $qemuSystem -accel help) -join "`n"
        } catch {
            Write-Warn "Could not run '$qemuSystem -accel help' to confirm whpx support."
        }
        if ($accelHelp -notmatch "whpx") {
            Write-Warn "This qemu-system-x86_64.exe does not list whpx in -accel help. Phase 2c task 2 covers the WHPX-capable binary."
        } else {
            Write-Ok "QEMU supports the whpx accelerator"
        }
    }
    $hypervisorPresent = $false
    try {
        $hypervisorPresent = [bool](Get-CimInstance -ClassName Win32_ComputerSystem).HypervisorPresent
    } catch {
        Write-Warn "Could not query Win32_ComputerSystem.HypervisorPresent."
    }
    if ($hypervisorPresent) {
        Write-Ok "Host hypervisor is present (a launch probe is still required to prove this WHPX rung)"
    } else {
        Write-Warn "No host hypervisor detected. Enable it (elevated): Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform, then reboot."
    }
}

if ($Smp -gt 2) {
    Write-Warn "-Smp $Smp exceeds the 2 processors Windows 2000 Professional supports; the extra vCPUs are wasted."
}

Write-Step "Creating local directories"
Ensure-Directory $VmDir
Ensure-Directory $LocalScriptDir
$xferDir = Join-Path $VmDir "xfer"
Ensure-Directory $xferDir
$diskImage = Join-Path $VmDir "win2k-smp.img"
$debugConLog = Join-Path $VmDir "win2k-smp-debugcon.log"
$debugConPreviousLog = Join-Path $VmDir "win2k-smp-debugcon.previous.log"
Write-Ok "VM directory: $VmDir"
Write-Ok "Local script directory: $LocalScriptDir"
Write-Ok "Transfer (VVFAT) directory: $xferDir (shared read-only with the 2b VM)"

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
$machine = if ($AcpiOff) { "pc,acpi=off" } else { "pc" }
# The HAL Setup selects follows from the ACPI decision alone, so it is derived
# once: the launcher comment and the post-generation verification guidance must
# name the same HAL, and two parallel conditionals would eventually disagree.
$expectedHal = if ($AcpiOff) {
    "MPS Multiprocessor PC"
} else {
    "ACPI Multiprocessor PC"
}
$acpiState = if ($AcpiOff) { "ACPI OFF" } else { "ACPI ON" }
$halNote = "rem   -machine ${machine}: $acpiState - Setup should pick the $expectedHal HAL."
Write-Ok "Accelerator: $Accel"
Write-Ok "Machine: -machine $machine  CPU: -cpu pentium3 (local APIC present)  -smp $Smp  -m $MemoryMb"

# Every launcher repeats the same rung, because a guest installed under one
# accelerator/HAL combination must be booted under it too - that is the 2b
# lesson (install and run flags must match) applied to a parameterised script.
$rungComment = @(
    "rem Phase 2d ladder rung recorded by the generator - install and run MUST",
    "rem use identical machine/CPU/accel flags, and the local APIC is never",
    "rem masked off on any rung (no multiprocessor HAL exists without it):",
    "rem   -accel $Accel",
    $halNote,
    "rem   -cpu pentium3: local APIC PRESENT (contrast 2b's -cpu pentium3,-apic).",
    "rem   -smp ${Smp}: Windows 2000 Professional supports at most 2 processors.",
    "rem   -m ${MemoryMb}: headroom for Driver Verifier Special Pool.",
    "rem Regenerate with -Accel/-AcpiOff to move to another rung; do not hand-edit."
)

$installCmd = Join-Path $LocalScriptDir "qemu-win2k-smp-install.cmd"
Write-AsciiFile $installCmd (@(
    "@echo off",
    "rem Phase 2d: Windows 2000 SP4 SMP stress VM install launcher.",
    "rem The ISO is Win2000 Pro with SP4 integrated (retail FPP - Setup prompts",
    "rem for a product key).",
    "rem",
    "rem Do NOT press F5 at setup start: with ACPI tables, a local APIC and 2 CPUs",
    "rem visible, Setup selects the multiprocessor HAL on its own. The HAL is",
    "rem chosen HERE, at install time - it cannot be swapped afterwards, so a",
    "rem uniprocessor HAL means reinstalling on a fresh disk image, not fixing.",
    "rem",
    "rem If Setup hangs at ""Setup is starting Windows 2000"", do not assume a",
    "rem freeze: sample EIP over the monitor first (docs/contributing/lessons.md, the Standard-PC HAL entry).",
    "rem A tiny EIP range inside an interrupt prologue is the vector-0xD1 APIC",
    "rem clock storm; walk the roadmap Phase 2d task 2 ladder from a fresh disk",
    "rem image, one rung at a time, regenerating this launcher for each.",
    "rem",
    "rem The xHCI is intentionally absent during install and added by the run",
    "rem launcher, so it appears as a fresh unrecognised device on first boot.",
    "rem -boot once=d boots the CD only for the first boot; -action reboot=reset",
    "rem keeps the guest reboots during setup inside this one QEMU session."
) + $rungComment + @(
    "set ""WIN2K_ISO=$Win2KIso""",
    "if not exist ""%WIN2K_ISO%"" (",
    "  echo Missing ISO: %WIN2K_ISO%",
    "  exit /b 1",
    ")",
    """$qemuSystemCommand"" ^",
    "  -name ""xhci98 Windows 2000 SP4 SMP stress"" ^",
    "  -machine $machine ^",
    "  -accel $Accel ^",
    "  -smp $Smp ^",
    "  -global ide-device.win2k-install-hack=on ^",
    "  -cpu pentium3 ^",
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

$prepareCmd = Join-Path $LocalScriptDir "qemu-win2k-smp-prepare-usbd.cmd"
Write-AsciiFile $prepareCmd (@(
    "@echo off",
    "rem SAFE PREPARATION BOOT: no USB controller is attached, so the incomplete",
    "rem Win2000 USB 2.0 stack cannot start. Before shutting down the guest, copy:",
    "rem   D:\USBD.SYS C:\WINNT\system32\drivers\USBD.SYS",
    "rem The VVFAT disk may receive another drive letter; locate volume QEMU VVFAT.",
    "rem Only after this succeeds should qemu-win2k-smp-run.cmd attach EHCI + xHCI.",
    "rem The trap is identical to 2b's (docs/contributing/lessons.md, the usbhub20.sys / USBD.SYS entry): Win2000 lays",
    "rem USB files down on demand per detected controller and omits usbd.sys, which",
    "rem usbhub20.sys imports - attaching EHCI first bugchecks c000026c / 0xc0000034",
    "rem with no Safe-Mode escape."
) + $rungComment + @(
    "if not exist ""$stagedUsbd"" (",
    "  echo Missing staged file: $stagedUsbd",
    "  echo Rerun setup-qemu-win2k-smp.ps1 with -Win2KUsbdSys ^<path-to-SP4-USBD.SYS^>.",
    "  exit /b 1",
    ")",
    """$qemuSystemCommand"" ^",
    "  -name ""xhci98 Windows 2000 SP4 SMP USBD preparation"" ^",
    "  -machine $machine ^",
    "  -accel $Accel ^",
    "  -smp $Smp ^",
    "  -cpu pentium3 ^",
    "  -m $MemoryMb ^",
    "  -vga cirrus ^",
    "  -drive file=""$diskImage"",format=qcow2,if=ide ^",
    "  -drive ""file=fat:$xferDir,format=raw,if=ide,snapshot=on"" ^",
    "  -boot c ^",
    "  -rtc base=localtime ^",
    "  -net none ^",
    "  -action reboot=reset -no-shutdown ^",
    "  -monitor tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
))

$runCmd = Join-Path $LocalScriptDir "qemu-win2k-smp-run.cmd"
Write-AsciiFile $runCmd (@(
    "@echo off",
    "rem Phase 2d: boot the installed Windows 2000 SP4 SMP stress VM from HDD.",
    "rem This is the RACE DETECTOR, not the development loop: from Phase 4 onward",
    "rem every checkpoint's build is also exercised here, with Driver Verifier on.",
    "rem A failure that reproduces ONLY here and not on the uniprocessor 2b VM",
    "rem points first to a cross-CPU race (docs/contributing/failure-diagnosis.md).",
    "rem",
    "rem Two USB controllers on purpose, same rationale as 2b:",
    "rem  - usb-ehci: an EHCI the NATIVE Win2000 SP4 stack binds (usbehci.sys ->",
    "rem    usbport.sys). Win2000 only extracts usbport.sys from driver.cab when a",
    "rem    controller needs it, so the EHCI is what makes native usbport.sys",
    "rem    present + loaded.",
    "rem  - qemu-xhci: no Win2000 driver -> shows as an unrecognised PCI device.",
    "rem PREREQUISITE: use qemu-win2k-smp-prepare-usbd.cmd first and copy USBD.SYS",
    "rem into C:\WINNT\system32\drivers. Attaching EHCI before that copy makes",
    "rem usbhub20.sys fail and the next boot bugcheck c000026c / 0xc0000034.",
    "rem The VVFAT backing directory is read-only; snapshot=on gives the guest a",
    "rem temporary writable overlay without exposing host files to guest writes.",
    "rem The isa-debugcon at port 0xE9 captures the QEMU-flavour driver's trace",
    "rem channel - since task 13-L.1 no other flavour writes there at all",
    "rem (src\xhci_dbg.c writes each finished line there as well as to DbgPrint).",
    "rem This VM's trace is a SEPARATE file from 2a's and 2b's: the targets are",
    "rem compared against each other, so no two may share a log.",
    "rem Before each boot a non-empty prior trace is archived as",
    "rem win2k-smp-debugcon.previous.log and this boot gets a fresh log, so stale",
    "rem DriverEntry lines cannot be attributed to a replacement driver. An EMPTY",
    "rem current log is left alone: it means the previous launch died before QEMU",
    "rem wrote anything, and rotating it would replace the last real trace with",
    "rem nothing."
) + $rungComment + @(
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
    "  -name ""xhci98 Windows 2000 SP4 SMP stress"" ^",
    "  -machine $machine ^",
    "  -accel $Accel ^",
    "  -smp $Smp ^",
    "  -cpu pentium3 ^",
    "  -m $MemoryMb ^",
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
))

Write-Ok "Wrote $installCmd"
Write-Ok "Wrote $prepareCmd"
Write-Ok "Wrote $runCmd"

Write-Step "Next steps"
Write-Host "  1. Run scripts\local\qemu-win2k-smp-install.cmd and install Windows 2000 (needs a product key)."
Write-Host "     Do NOT press F5; let Setup detect the multiprocessor HAL itself."
Write-Host "  2. Verify the MP kernel landed: Computer node = '$expectedHal',"
Write-Host "     two Task Manager CPU graphs, ntoskrnl.exe original filename = ntkrnlmp.exe."
Write-Host "  3. Boot qemu-win2k-smp-prepare-usbd.cmd and copy USBD.SYS into C:\WINNT\system32\drivers."
Write-Host "  4. Shut down, then boot qemu-win2k-smp-run.cmd (adds EHCI + xHCI)."
Write-Host "  5. Snapshot: qemu-img snapshot -c phase2d-clean $diskImage"
Write-Host "  6. Do NOT install NUSB - the usbport stack is native to Win2000 SP4."
