<#
.SYNOPSIS
Create the Windows Vista Business SP2 (32-bit) guest disk image and QEMU launchers (roadmap Phase 22).

.DESCRIPTION
The first of Phase 22's two guests. The question the phase asks is whether the
32-bit xhci98.sys this project already ships installs, loads and works on
Windows Vista and Windows 7 as it stands - so nothing is built for this guest.
It is the binary on the .NTx86 half of the INF that already exists, and if the
answer is yes it cost guests and readings and no build at all.

The machine is written once in scripts\qemu-nt6-common.ps1, which this and
scripts\setup-qemu-win7.ps1 share; read that file for the recipe and for what
each line differs from the 32-bit XP one for. docs\contributing\build-and-test.md,
"Windows Vista and Windows 7 target VMs", is the procedure.

TCG, NOT WHPX, AND THAT IS THE OPPOSITE OF WHAT THE FIRST PROBE SAID. Under
-accel whpx,kernel-irqchip=off on development host A, 2026-09-10, Vista Setup
reached its "Install Windows" language page under four minutes from launch and
ran the whole first phase - partitioning, the file copy, the reboot - and then
WEDGED on the boot that follows it. Measured at the wedge: EIP confined to two
addresses 188 bytes apart, interrupts enabled (EFL IF set) at CPL=0 with
HLT=0, roughly half a core burning, and ide0-hd0 idle for TWENTY-TWO MINUTES.
The screen kept animating its boot marquee throughout, which is what the two
pinned addresses were painting. A system_reset re-entered the same wedge.

Relaunched on the same half-installed image under -accel tcg, Setup RESUMED at
"Please wait while Windows sets up your computer" and ran to the desktop. So
this guest wants TCG for the same reason the XP x64 guest does, and the image
did not have to be rebuilt to find that out: WHPX and TCG present the same
virtual machine (-machine pc, ACPI on) and differ only in the execution
engine, so the HAL that install-time selected stays correct across the switch.

THE LESSON IS ABOUT THE PROBE, NOT ABOUT VISTA. Reaching the language page
proves only that the accelerator can run Setup's WinPE phase. It exercises no
ACPI bring-up of an installed kernel, and that is where this guest dies. A
probe that stops at the first prompt is not a reading of the accelerator; the
install has to complete before the value may be written down.

The edition is Business, image 1 of the seven in this media's install.wim -
the same image tasks 22.1 and 22.2 read usbport.sys, usbehci.sys, ntoskrnl.exe
and hal.dll out of on 2026-09-09, so what Setup places here is the binary
those six measurements were taken from. usbport.sys is byte-identical across
Vista editions, so that identity does not rest on the edition alone.

.PARAMETER VistaIso
The Windows Vista SP2 32-bit DVD image. Empty by default, like every other ISO
parameter in these scripts (the 2026-09-07 audit's J3): a default naming one
host's media is a tracked file that names nothing on any other host. The
generated launchers carry the "edit this file or pass -VistaIso" guard.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-vista.ps1 -VistaIso "D:\isos\en_windows_vista_sp2_x86_dvd_342266.iso" -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$VistaIso = "",
    [string]$DiskSize = "32G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    [string]$Cpu = "qemu64",
    [string]$Accel = "tcg",
    [int]$MonitorPort = 55565,
    [int]$MemoryMb = 2048,
    [int]$Smp = 4,
    [switch]$CreateDisk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qemu-nt6-common.ps1")

New-Nt6QemuGuest `
    -GuestName "Windows Vista Business SP2" `
    -Stem "vista" `
    -IsoVar "VISTA_ISO" `
    -GeneratorName "setup-qemu-vista.ps1" `
    -IsoParamName "VistaIso" `
    -Accel $Accel `
    -AccelNote @(
        "Measured on development host A, 2026-09-10, and TCG is what this",
        "guest needs - the opposite of every other 32-bit guest here. Under",
        "whpx,kernel-irqchip=off Vista Setup reached its language page in under",
        "four minutes and ran the whole first phase, then WEDGED on the boot",
        "after it: EIP pinned to two addresses, interrupts enabled at CPL=0,",
        "half a core burning, and the disk idle for twenty-two minutes while",
        "the boot marquee kept animating. Under tcg the same half-installed",
        "image RESUMED and ran to the desktop. Reaching the language page",
        "proves only that WinPE runs; the wedge is in the installed kernel's",
        "ACPI bring-up, so an accelerator may not be written down until an",
        "install has COMPLETED under it."
    ) `
    -MonitorPort $MonitorPort `
    -Iso $VistaIso `
    -VmDir $VmDir `
    -LocalScriptDir $LocalScriptDir `
    -DiskSize $DiskSize `
    -QemuBinDir $QemuBinDir `
    -XhciDevice $XhciDevice `
    -Cpu $Cpu `
    -MemoryMb $MemoryMb `
    -Smp $Smp `
    -CreateDisk:$CreateDisk
