<#
.SYNOPSIS
Create the Windows 7 Professional SP1 x64 guest disk image and QEMU launchers (roadmap task 21.8).

.DESCRIPTION
The second of task 21.8's two guests, and the sibling of
scripts\setup-qemu-vista-x64.ps1. Read that file's .DESCRIPTION first: the
reason this pair exists, the naming, and the refusal to carry an unmeasured
accelerator are all the same here and are not repeated.

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
Mandatory in effect. There is no measured value for this guest, and the fact
that the 32-bit Windows 7 guest installed clean under whpx,kernel-irqchip=off
on 2026-09-10 is NOT a reading of this one - its own sibling, one WDM revision
away on the same host in the same afternoon, needed the other rung.

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
    # No default, deliberately - and on this guest the tempting wrong answer is
    # not XP x64's tcg but its OWN 32-bit sibling's whpx,kernel-irqchip=off.
    # That pair disagreed with each other; a bitness away is at least as far.
    [string]$Accel = "",
    [int]$MonitorPort = 55564,
    [int]$MemoryMb = 2048,
    [int]$Smp = 4,
    [switch]$CreateDisk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qemu-nt6-common.ps1")

if ([string]::IsNullOrWhiteSpace($Accel)) {
    throw @"
-Accel is required for this guest: no accelerator has been measured for a
64-bit Windows 7 guest on any host. The 32-bit Windows 7 guest installed clean
under whpx,kernel-irqchip=off on 2026-09-10, and that is NOT a reading of this
one - its own 32-bit sibling, one WDM revision away on the same host in the
same afternoon, needed tcg instead.

Pass one of:
  -Accel whpx,kernel-irqchip=off      (plain 'whpx' cannot initialise here)
  -Accel tcg

then INSTALL under it. An accelerator may not be written into this file as a
default until an install has COMPLETED under it - reaching Setup's language
page proves only that WinPE runs.
"@
}

New-Nt6QemuGuest `
    -GuestName "Windows 7 Professional SP1 x64" `
    -Stem "win7-x64" `
    -IsoVar "WIN7_X64_ISO" `
    -GeneratorName "setup-qemu-win7-x64.ps1" `
    -IsoParamName "Win7X64Iso" `
    -Arch "amd64" `
    -Accel $Accel `
    -AccelNote @(
        "NOT MEASURED. This value was passed on the command line for a probe;",
        "no 64-bit Windows 7 guest has been installed on any host in this",
        "project. The 32-bit Windows 7 guest took whpx,kernel-irqchip=off and",
        "installed clean under it on 2026-09-10 - and that is not a reading of",
        "this guest, because that guest's own sibling, one WDM revision away on",
        "the same host in the same afternoon, wedged under the same rung and",
        "needed tcg. An accelerator may not be written down as this guest's",
        "until an install has COMPLETED under it; reaching Setup's language",
        "page proves only that WinPE runs. When one completes, give",
        "setup-qemu-win7-x64.ps1 a measured default in a commit that says what",
        "was observed, and regenerate."
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
