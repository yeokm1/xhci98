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

WHPX, and on this guest that is the ordinary answer rather than a surprising
one - but it was measured rather than assumed, because roadmap task 22.4 says
to probe the accelerator per host AND per guest and Phase 21 paid for that
rule twice in opposite directions. Under -accel whpx,kernel-irqchip=off on
host minis-w11p-ykm, 2026-09-10, Vista Setup was at its "Install Windows"
language page under four minutes from launch, and had been in 32-bit kernel
code with a varied EIP well before that. TCG was not needed and was not tried.

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
    [string]$Accel = "whpx,kernel-irqchip=off",
    [int]$MonitorPort = 55565,
    [int]$MemoryMb = 2048,
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
        "Measured on host minis-w11p-ykm, 2026-09-10, rather than inherited:",
        "under whpx,kernel-irqchip=off Vista Setup was at its ""Install",
        "Windows"" language page under four minutes from launch, having been in",
        "32-bit kernel code with a varied EIP well before that. TCG was not",
        "needed and was not tried. That the 32-bit answer here is the 32-bit",
        "answer everywhere else in this project is a result and not an",
        "assumption: the XP x64 guest wedges under WHPX and wants TCG, which is",
        "why each guest is probed."
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
    -CreateDisk:$CreateDisk
