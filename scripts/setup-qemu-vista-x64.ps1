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

THE ACCELERATOR IS MEASURED AND IT IS TCG, 2026-09-10. This generator used to
refuse to run without an explicit -Accel, because nothing had been measured for
a 64-bit Vista guest on any host. An install has now COMPLETED under
tcg,thread=multi - Setup driven to the desktop, shut down from the Start menu,
snapshot vista-x64-clean-install - so the refusal has served its purpose and the
default below is a reading rather than an inheritance.

WHAT WHPX DOES HERE, because a negative measured this precisely is worth
keeping: under whpx,kernel-irqchip=off this guest BUGCHECKS INSIDE WinPE, before
Setup writes a single byte. STOP 0x0000000A, IRQL_NOT_LESS_OR_EQUAL, referenced
address 0x10 at IRQL 0xC on a read. A near-null dereference at device IRQL is an
interrupt-delivery fault, which is the surface kernel-irqchip=off touches, and
on this host that rung is the only WHPX there is - plain -accel whpx cannot
initialise at all ("Failed to enable nested virtualization, hr=80370302"), so
the alternative to it is TCG rather than another WHPX rung. The screen is kept
at out\task-21-8\vista-x64-whpx-stop-0x0A.png.

THE RULE THAT THE REFUSAL ENFORCED STILL STANDS FOR EVERY FUTURE GUEST: AN
ACCELERATOR MAY NOT BE WRITTEN DOWN UNTIL AN INSTALL HAS COMPLETED UNDER IT.
The first probe of the 32-bit Vista guest stopped at Setup's language page,
wrote down whpx,kernel-irqchip=off, and was wrong - that guest ran its entire
first phase and then wedged on the boot after it. Reaching a prompt proves only
that WinPE runs. It very nearly cost this pair the same mistake a second time:
the 64-bit Windows 7 guest cleared WinPE under WHPX and looked like a
disagreement with this one, then wedged at its first restart.

.PARAMETER VistaX64Iso
The Windows Vista SP2 x64 DVD image - the same media task 21.7 read its six
measurements out of, so the stack this guest installs is the stack that was
measured. Empty by default, like every other ISO parameter in these scripts
(the 2026-09-07 audit's J3): a default naming one host's media is a tracked
file that names nothing on any other host. The generated launchers carry the
"edit this file or pass -VistaX64Iso" guard.

.PARAMETER Accel
Defaults to the measured value, tcg. See above for what WHPX does instead.

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
    # Measured 2026-09-10: an install COMPLETED under this rung. WHPX bugchecks
    # this guest inside WinPE (STOP 0x0A) - see the .DESCRIPTION.
    [string]$Accel = "tcg",
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

New-Nt6QemuGuest `
    -GuestName "Windows Vista Business SP2 x64" `
    -Stem "vista-x64" `
    -IsoVar "VISTA_X64_ISO" `
    -GeneratorName "setup-qemu-vista-x64.ps1" `
    -IsoParamName "VistaX64Iso" `
    -Arch "amd64" `
    -Accel $Accel `
    -AccelNote @(
        "MEASURED 2026-09-10, by an install that COMPLETED under it: Setup",
        "driven to the desktop and shut down from the Start menu, snapshot",
        "vista-x64-clean-install.",
        "The other rung was tried first and is a clean negative. Under",
        "whpx,kernel-irqchip=off this guest bugchecks INSIDE WinPE, before",
        "Setup writes a byte: STOP 0x0000000A, IRQL_NOT_LESS_OR_EQUAL,",
        "address 0x10 at IRQL 0xC on a read - a near-null dereference at",
        "device IRQL, which is the surface kernel-irqchip=off touches. That",
        "rung is the only WHPX on this host; plain 'whpx' cannot initialise",
        "at all, so the alternative to it is TCG and not another WHPX rung.",
        "The 64-bit Windows 7 guest wants TCG too, but do not read that as a",
        "rule: it failed DIFFERENTLY, clearing WinPE and wedging at its first",
        "restart, and the 32-bit pair disagreed with each other outright."
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
