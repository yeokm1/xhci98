<#
.SYNOPSIS
Create the Windows Vista Business SP2 x64 guest disk image and QEMU launchers (roadmap task 21.8).

.DESCRIPTION
The first of task 21.8's two guests, and the first guest in this project that
may turn out to be unable to run the driver at all.

Task 21.7 read this system's usbport.sys statically on 2026-09-09 and found
nothing in the interface against it: all six measurements pass, USBPORT_GetHciMn
returns the 0x10000001 this driver already accepts, and the packet the miniport
fills is the same 0x250 one. So the ABI question is settled and this guest is
not asked it. What it IS asked is the two questions 21.7 named as true
regardless, neither of which is an ABI question and both of which only a guest
settles: whether an unsigned amd64 driver can be made to load on a system that
enforces kernel-mode code signing, and whether the driver store accepts the
package's file list.

The machine is written once in scripts\qemu-nt6-common.ps1, which this file
shares with setup-qemu-win7-x64.ps1 and with the two 32-bit generators;
-Arch amd64 is what makes it the 64-bit recipe. Read that file for the machine
and for what each line differs from the 32-bit XP one for.
docs\contributing\build-and-test.md, "Vista x64 and Windows 7 x64 target VMs",
is the procedure and carries the code-signing routes.

WHY THIS IS NOT NAMED setup-qemu-winvista64.ps1, which is the name
build-and-test.md's planning table carried from 2026-09-09 to 2026-09-10. That
table also named its sibling setup-qemu-win7x64.ps1, so the pair disagreed with
each other, and it was written when the model for these guests was "siblings of
setup-qemu-winxp64.ps1" - which they are not. They are the 64-bit half of the
Vista and Windows 7 recipe, sharing one body with setup-qemu-vista.ps1 and
setup-qemu-win7.ps1, so they take those two stems with an explicit -x64 on the
end. The architecture is the whole point of this pair (a second binary, from a
second toolchain, in a second package), so it is worth a visible suffix rather
than a digit appended to a stem.

THE ACCELERATOR IS NOT SET HERE AND THIS GENERATOR REFUSES TO RUN WITHOUT ONE.
Every other generator in this directory carries a measured default; this one
cannot, because nothing has been measured. A 64-bit Vista guest is a fourth
workload - XP x64 wants tcg, the 32-bit guests want WHPX, and the 32-bit Vista
and Windows 7 pair disagreed with EACH OTHER on 2026-09-10 - and none of those
readings generalises here.

The rule that refusal enforces was paid for on 2026-09-10 and it is the one
most likely to be shortcut: AN ACCELERATOR MAY NOT BE WRITTEN DOWN UNTIL AN
INSTALL HAS COMPLETED UNDER IT. The first probe of the 32-bit Vista guest
stopped at Setup's language page, wrote down whpx,kernel-irqchip=off, and was
wrong - that guest ran its entire first phase and then wedged on the boot after
it, EIP pinned to two addresses with the disk idle for twenty-two minutes while
the boot marquee kept animating. Reaching a prompt proves only that WinPE runs.

So: pass -Accel for the probe, install, and when an install COMPLETES, give
this file a measured default in a commit that says what was observed. The
candidates are "tcg" and "whpx,kernel-irqchip=off", and plain "whpx" is not one
of them - it cannot initialise on this host at all ("Failed to enable nested
virtualization, hr=80370302"), so kernel-irqchip=off is the only WHPX there is
here and the alternative to it is TCG rather than another WHPX rung.

.PARAMETER VistaX64Iso
The Windows Vista SP2 x64 DVD image - the same media task 21.7 read its six
measurements out of, so the stack this guest installs is the stack that was
measured. Empty by default, like every other ISO parameter in these scripts
(the 2026-09-07 audit's J3): a default naming one host's media is a tracked
file that names nothing on any other host. The generated launchers carry the
"edit this file or pass -VistaX64Iso" guard.

.PARAMETER Accel
Mandatory in effect: see above. There is no measured value for this guest.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-vista-x64.ps1 -VistaX64Iso "D:\isos\en_windows_vista_sp2_x64_dvd_342267.iso" -Accel tcg -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$VistaX64Iso = "",
    [string]$DiskSize = "32G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    [string]$Cpu = "qemu64",
    # No default, deliberately. See the .DESCRIPTION: nothing has been measured
    # for this guest, and this project has already been wrong once about an
    # accelerator it wrote down from a Setup prompt.
    [string]$Accel = "",
    [int]$MonitorPort = 55563,
    # 2048 for the install. Raising a 64-bit guest above 4 GB is roadmap task
    # 21.8's own experiment with its own record, taken AFTER the install; the
    # shared body's -MemoryMb note says what it does and does not test.
    [int]$MemoryMb = 2048,
    [int]$Smp = 4,
    [switch]$CreateDisk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qemu-nt6-common.ps1")

if ([string]::IsNullOrWhiteSpace($Accel)) {
    throw @"
-Accel is required for this guest: no accelerator has been measured for a
64-bit Vista guest on any host, and this project has already written one down
from a Setup prompt and been wrong (2026-09-10, the 32-bit Vista guest, which
ran its whole first phase under whpx,kernel-irqchip=off and then wedged on the
boot after it).

Pass one of:
  -Accel tcg
  -Accel whpx,kernel-irqchip=off      (plain 'whpx' cannot initialise here)

then INSTALL under it. An accelerator may not be written into this file as a
default until an install has COMPLETED under it - reaching Setup's language
page proves only that WinPE runs.
"@
}

New-Nt6QemuGuest `
    -GuestName "Windows Vista Business SP2 x64" `
    -Stem "vista-x64" `
    -IsoVar "VISTA_X64_ISO" `
    -GeneratorName "setup-qemu-vista-x64.ps1" `
    -IsoParamName "VistaX64Iso" `
    -Arch "amd64" `
    -Accel $Accel `
    -AccelNote @(
        "NOT MEASURED. This value was passed on the command line for a probe;",
        "no 64-bit Vista guest has been installed on any host in this project.",
        "XP x64 wants tcg, the 32-bit guests want WHPX, and the 32-bit Vista",
        "and Windows 7 pair disagreed with EACH OTHER on 2026-09-10 - so none",
        "of those readings reaches this guest. An accelerator may not be",
        "written down as this guest's until an install has COMPLETED under it:",
        "the 32-bit Vista probe stopped at Setup's language page, recorded",
        "whpx,kernel-irqchip=off, and was wrong. When an install completes,",
        "give setup-qemu-vista-x64.ps1 a measured default in a commit that says",
        "what was observed, and regenerate."
    ) `
    -MonitorPort $MonitorPort `
    -Iso $VistaX64Iso `
    -VmDir $VmDir `
    -LocalScriptDir $LocalScriptDir `
    -DiskSize $DiskSize `
    -QemuBinDir $QemuBinDir `
    -XhciDevice $XhciDevice `
    -Cpu $Cpu `
    -MemoryMb $MemoryMb `
    -Smp $Smp `
    -CreateDisk:$CreateDisk
