<#
.SYNOPSIS
Create the Windows 7 Professional SP1 (32-bit) guest disk image and QEMU launchers (roadmap Phase 22).

.DESCRIPTION
The second of Phase 22's two guests, and the sibling of
scripts\setup-qemu-vista.ps1. The question the phase asks is whether the
32-bit xhci98.sys this project already ships installs, loads and works on
Windows Vista and Windows 7 as it stands - so nothing is built for this guest.
It is the binary on the .NTx86 half of the INF that already exists.

The machine is written once in scripts\qemu-nt6-common.ps1, which this and the
Vista generator share; read that file for the recipe and for what each line
differs from the 32-bit XP one for. docs\contributing\build-and-test.md,
"Windows Vista and Windows 7 target VMs", is the procedure.

-cpu qemu64 MATTERS MORE HERE THAN ON THE VISTA GUEST. Windows 7 requires the
NX bit and Setup refuses a processor model without one, so the 32-bit XP
recipe's pentium3 does not merely run slowly here - it does not install. The
shared body refuses such a -Cpu outright and the launcher gate asserts the
generated text.

WHPX, measured rather than inherited (roadmap task 22.4 asks for the
accelerator to be probed per host AND per guest). Under
-accel whpx,kernel-irqchip=off on host minis-w11p-ykm, 2026-09-10, Windows 7
Setup reached its "Install Windows" language page about two minutes from
launch AND RAN THE WHOLE INSTALL THROUGH TO A FINISHED DESKTOP, first reboot
included. That second half is what makes the value usable: the language page
alone proves only that WinPE runs, which is exactly the reading that was wrong
about the Vista guest.

AND THE SIBLING GUEST DISAGREES. scripts\setup-qemu-vista.ps1 defaults to tcg,
because Vista wedges on the boot after Setup's first reboot under this very
rung. Two guests one WDM revision apart, out of one recipe, on one host, in
one afternoon, do not share an accelerator - so do not "tidy" these two
defaults into agreement.

One reading from that probe is worth keeping, because it is the reading that
went the other way on the XP x64 guest: at the language page EIP was IDENTICAL
across samples 36 seconds apart, with HLT=0 - the signature that was a wedge
there. It is not one here. A guest sitting at a Setup prompt is spinning in an
input wait, and the screen is what tells the two apart. Take the screendump
before calling a pinned EIP a wedge.

The edition is Professional, the single image in this volume-licence media's
install.wim - the same image tasks 22.1 and 22.2 read usbport.sys,
usbehci.sys, ntoskrnl.exe and hal.dll out of on 2026-09-09, so what Setup
places here is the binary those six measurements were taken from.

.PARAMETER Win7Iso
The Windows 7 SP1 32-bit DVD image. Empty by default, like every other ISO
parameter in these scripts (the 2026-09-07 audit's J3): a default naming one
host's media is a tracked file that names nothing on any other host. The
generated launchers carry the "edit this file or pass -Win7Iso" guard.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win7.ps1 -Win7Iso "D:\isos\en_windows_7_professional_with_sp1_vl_build_x86_dvd_u_677896.iso" -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$Win7Iso = "",
    [string]$DiskSize = "32G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    [string]$Cpu = "qemu64",
    [string]$Accel = "whpx,kernel-irqchip=off",
    [int]$MonitorPort = 55566,
    [int]$MemoryMb = 2048,
    [int]$Smp = 4,
    [switch]$CreateDisk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qemu-nt6-common.ps1")

New-Nt6QemuGuest `
    -GuestName "Windows 7 Professional SP1" `
    -Stem "win7" `
    -IsoVar "WIN7_ISO" `
    -GeneratorName "setup-qemu-win7.ps1" `
    -IsoParamName "Win7Iso" `
    -Accel $Accel `
    -AccelNote @(
        "Measured on host minis-w11p-ykm, 2026-09-10, rather than inherited:",
        "under whpx,kernel-irqchip=off Windows 7 Setup reached its ""Install",
        "Windows"" language page about two minutes from launch. TCG was not",
        "needed and was not tried. At that page EIP was IDENTICAL across samples",
        "36 seconds apart with HLT=0 - the signature that WAS a wedge on the XP",
        "x64 guest, and is not one here. A guest at a Setup prompt spins in an",
        "input wait; the screendump is what tells the two apart."
    ) `
    -MonitorPort $MonitorPort `
    -Iso $Win7Iso `
    -VmDir $VmDir `
    -LocalScriptDir $LocalScriptDir `
    -DiskSize $DiskSize `
    -QemuBinDir $QemuBinDir `
    -XhciDevice $XhciDevice `
    -Cpu $Cpu `
    -MemoryMb $MemoryMb `
    -Smp $Smp `
    -CreateDisk:$CreateDisk
