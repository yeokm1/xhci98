<#
.SYNOPSIS
Create the Windows 7 Professional SP1 x64 guest disk image and QEMU launchers (roadmap task 21.8).

.DESCRIPTION
The second of task 21.8's two guests, and the sibling of
scripts\setup-qemu-vista-x64.ps1. Read that file's .DESCRIPTION first: the
reason this pair exists and the naming are the same here and are not repeated.

THE ACCELERATOR IS MEASURED AND IT IS TCG, 2026-09-10, by an install that
completed under tcg,thread=multi (snapshot win7-x64-clean-install). Both
generators used to refuse to run without an explicit -Accel; both now carry the
measurement instead.

THIS GUEST IS THE REASON THE "COMPLETED INSTALL" RULE IS WORTH ITS COST, because
it very nearly bought the same wrong answer twice. Under whpx,kernel-irqchip=off
it cleared WinPE, ran its ENTIRE first phase and wrote 7.27 GB - which read at
the time like a disagreement with its sibling, since Vista x64 bugchecks in
WinPE on that rung. It then WEDGED at the first restart: screen unchanged for
eight minutes, ide0-hd0 idle climbing monotonically past nine, RIP revisiting
the same three addresses with HLT=0. Anything short of a completed install would
have recorded WHPX here. Screen at
out\task-21-8\win7-x64-whpx-wedge-at-first-restart.png.

AND IT COST A REINSTALL, WHICH IS NOT WHAT THIS PROJECT HAD WRITTEN DOWN. The
rule was "switching to TCG costs no reinstall - same virtual machine, only the
execution engine differs, so the install-time HAL stays correct". That held for
vm\vista.img, which wedged on a boot AFTER a completed phase and so still had a
bootable disk. This guest wedged AT the transition, before Setup laid its boot
files down, and the TCG relaunch got "BOOTMGR is missing" - the half-install was
unrecoverable and Setup had to be run again from the DVD. The rule needs its
qualifier: no reinstall PROVIDED THE GUEST ALREADY HAS A BOOTABLE DISK.

WHAT IS DIFFERENT ABOUT THIS GUEST RATHER THAN ITS SIBLING, and it is one thing
that matters and one that does not.

The one that matters: 6.1's usbport has A SECOND IoGetDmaAdapter CALL SITE, the
one that asks for a 64-bit adapter (task 21.7, measurement recorded in
usbport-miniport-abi.md, "The 6.0 and 6.1 lineages"). Vista's has one. It is
gated on the miniport declaring Version >= 310 AND filling packet slot 0x398,
and this driver declares Version = 200 and fills neither, so it is unreachable
from here - which is exactly why the high-DWORD check at src\xhci_xfer.c:542
must stay, and why measurement M8 is the standing argument that such checks
earn their keep. **THE GATE IS THE VERSION, NOT THE GUEST'S RAM**, so giving
this guest 8192 MB does not open it and must not be read as having done so.

The one that does not: Windows 7 Setup creates a 100 MB System Reserved
partition that Vista does not. It changes nothing about the machine, but it
breaks the one-pass listing trick every other guest here allows - `7z l
vm\win7-x64.img` stops at the MBR and returns about thirty lines that read like
an empty disk, with no error, because 7-Zip only descends automatically when
there is a single nested stream. Extract the Windows volume and list that
instead. build-and-test.md has the two commands and the VHD alternative.

.PARAMETER Win7X64Iso
The Windows 7 Professional SP1 x64 DVD image - the same media task 21.7 read
its six measurements out of, so the stack this guest installs is the stack that
was measured. Empty by default, like every other ISO parameter in these scripts
(the 2026-09-07 audit's J3). The media is volume-licence, so Setup asks for a
key; the owner drives Setup at the console, the standing decision of
2026-09-03.

.PARAMETER Accel
Defaults to the measured value, tcg. The 32-bit Windows 7 guest installs clean
under whpx,kernel-irqchip=off and that was never a reading of this one: this
guest gets further under WHPX than its 64-bit sibling does and still does not
finish. See above.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\setup-qemu-win7-x64.ps1 -Win7X64Iso "D:\isos\en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso" -Accel whpx,kernel-irqchip=off -CreateDisk
#>

[CmdletBinding()]
param(
    [string]$VmDir = "",
    [string]$LocalScriptDir = "",
    [string]$Win7X64Iso = "",
    [string]$DiskSize = "32G",
    [string]$QemuBinDir = "",
    [string]$XhciDevice = "qemu-xhci,p3=0",
    [string]$Cpu = "qemu64",
    # Measured 2026-09-10: an install COMPLETED under this rung. WHPX runs this
    # guest's whole first phase and then wedges at the restart - see the
    # .DESCRIPTION, which is the clearest case this project has for why a
    # partial install may not be read as an accelerator measurement.
    [string]$Accel = "tcg",
    [int]$MonitorPort = 55564,
    [int]$MemoryMb = 2048,
    [int]$Smp = 4,
    [switch]$CreateDisk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qemu-nt6-common.ps1")

New-Nt6QemuGuest `
    -GuestName "Windows 7 Professional SP1 x64" `
    -Stem "win7-x64" `
    -IsoVar "WIN7_X64_ISO" `
    -GeneratorName "setup-qemu-win7-x64.ps1" `
    -IsoParamName "Win7X64Iso" `
    -Arch "amd64" `
    -Accel $Accel `
    -AccelNote @(
        "MEASURED 2026-09-10, by an install that COMPLETED under it: Setup",
        "driven to the desktop and shut down, snapshot win7-x64-clean-install.",
        "THE NEGATIVE HERE IS THE INSTRUCTIVE ONE. Under",
        "whpx,kernel-irqchip=off this guest clears WinPE, runs its ENTIRE",
        "first phase and writes 7.27 GB - and then wedges at the first",
        "restart: screen unchanged for eight minutes, disk idle climbing past",
        "nine, RIP revisiting three addresses with HLT=0. Any test short of a",
        "completed install would have written WHPX down here.",
        "It also cost a REINSTALL. 'Switching to TCG costs no reinstall' holds",
        "only where the guest already has a bootable disk; this one wedged",
        "before Setup laid its boot files down, so the TCG relaunch met",
        "'BOOTMGR is missing' and Setup had to run again from the DVD."
    ) `
    -MonitorPort $MonitorPort `
    -Iso $Win7X64Iso `
    -VmDir $VmDir `
    -LocalScriptDir $LocalScriptDir `
    -DiskSize $DiskSize `
    -QemuBinDir $QemuBinDir `
    -XhciDevice $XhciDevice `
    -Cpu $Cpu `
    -MemoryMb $MemoryMb `
    -Smp $Smp `
    -CreateDisk:$CreateDisk
