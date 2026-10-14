# run-usb4-guest.ps1 - boot one of this project's guests on the usb4-emu QEMU
# fork with its Thunderbolt 3 / USB4 device models (roadmap-usb4.md task
# 39-0.7, design record 21 section 10.2).
#
# What it does, in order:
#   1. Resolves the main checkout (vm\ and out\vmlocks\ live there, so the
#      script works the same from a worktree).
#   2. Creates (or reuses) a qcow2 OVERLAY of the guest's base image under
#      vm\<RunRoot>\<Name>\disk.qcow2 (vm\usb4\ by default) - the base is
#      only ever a read-only backing file - and an NVMe disk overlay beside it.
#   3. Takes the host's VM admission lock (out\vmlocks\_admit), checks the
#      vCPU budget (sum of -smp of every running qemu-system process plus
#      this guest's, at most the logical processor count) and >= 6 GB of free
#      RAM after this guest, takes the guest lock out\vmlocks\usb4-<Name>.lock,
#      launches, and drops the admission lock.
#   4. Launches QEMU detached (it outlives this script) with QMP on
#      127.0.0.1:<QmpPort> and VNC on 127.0.0.1:<VncPort>, a port-0xE9 debug
#      console log and a serial log per launch, the trace when asked, and
#      writes vm\<RunRoot>\<Name>\run.json, which usb4-qmp.ps1 reads.
#
# Rules this keeps (record 21 section 10.2): it never writes inside the
# usb4-emu tree (that repository's binary is COPIED to vm\usb4\bin, its
# data directory and disk images are opened read-only), it uses ports
# 47900-47999 only, and it never touches a QEMU process it did not start.
#
# Examples:
#   # Windows 98 SE on the Alpine Ridge tree, P14s BIOS Assist off, pre-boot
#   # off, the device router attached at power-on, usb4_* trace on:
#   powershell -File scripts\usb4\run-usb4-guest.ps1 -Guest win98 -Usb4Profile tb3 -Bios OFF_OFF -Trace
#   # Windows 2000 (xHCI-only image) on the generic USB4 host, Alder Lake model:
#   powershell -File scripts\usb4\run-usb4-guest.ps1 -Guest win2k-xonly -Usb4Profile generic -HostModel intel-adl
#   # Print the command line only:
#   powershell -File scripts\usb4\run-usb4-guest.ps1 -Guest winxp -Usb4Profile tb3 -DryRun
#   # Windows 98 SE from the t26 image with the host controller driver, run
#   # directory vm\p40-topo\<Name>, usb4-generic with the pre-OS CM:
#   powershell -File scripts\usb4\run-usb4-guest.ps1 -Guest win98 -BaseImage vm\t26\win98-gold.qcow2 -RunRoot p40-topo -Usb4Profile generic -PreosCm
#
# Then: powershell -File scripts\usb4\usb4-qmp.ps1 -Name <Name> [-RunRoot <dir>] <verb> ...
#
# Under powershell -File, a value that starts with '-' needs the colon form,
# -Extra:-S; -TracePattern takes a comma-separated list ('usb4_*,pci_nvme_*').
# Several -Extra values need -Command: powershell -Command "& scripts\usb4\
# run-usb4-guest.ps1 -Guest win98 -Extra '-device','usb4-debug-port'".
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('win98', 'winme', 'win2k-xonly', 'win2k-smp', 'winxp', 'winxp64',
                 'vista', 'vista-x64', 'win7', 'win7-x64')]
    [string]$Guest,

    # tb3: tb3-alpine-ridge behind the captured switch tree (root bridge,
    #      8086:15c0 switch ports, NHI 8086:15bf, tunnel bridge, xHCI 15c1),
    #      as usb4-emu scripts/run-win98.sh BIOS=... builds it.
    # generic: usb4-generic, host 1b36:00f4 and the SHPC tunnel bridge on
    #      bus 0, no switch.
    # none: no usb4 device at all (the baseline).
    [ValidateSet('tb3', 'generic', 'none')][string]$Usb4Profile = 'tb3',

    # Run directory name under vm\<RunRoot>\. Default <Guest>-<Usb4Profile>.
    [string]$Name,
    # The directory under the main checkout's vm\ that holds the run
    # directory (default usb4; a worker's own vm\<branch>\ also works).
    [string]$RunRoot = 'usb4',
    # Another qcow2 base under vm\ in place of the guest's vm\<guest>.img,
    # e.g. vm\t26\win98-gold.qcow2 (98 SE with the host controller driver
    # installed; vm\win98.img carries the 1.x miniport). Only ever a backing
    # file; the guest's machine line is kept.
    [string]$BaseImage,

    # tb3 only: P14s "Thunderbolt BIOS Assist"_"Support in Pre Boot Environment".
    [ValidateSet('OFF_OFF', 'OFF_ON', 'ON_OFF', 'ON_ON')][string]$Bios = 'OFF_OFF',
    # Device router (enc0, NVMe behind it) attached at power-on.
    [ValidateSet(0, 1)][int]$DeviceAtBoot = 1,
    # tb3 only: bus 0 slot of the root bridge. SeaBIOS's $PIR lists slots 1-6
    # only; 0x1c reproduces Windows 98's Code 29 for a hot-added endpoint.
    [string]$RootAddr = '0x6',

    # generic only.
    [ValidateSet('minimal', 'intel-adl')][string]$HostModel = 'minimal',
    [switch]$PreosCm,
    [switch]$ForceTunnel,
    # USB3 tunnel: a USB disk on a dedicated qemu-xhci (id u4x) that is only
    # attached while the CM has a USB3 tunnel up to enc0.
    [switch]$Usb3,
    # usb4-device dock=on (PCIe switch + NVMe + e1000e + xHCI). The model
    # supports it only behind a PCIe tunnel port (q35); on these i440FX
    # guests it warns and tunnels the NVMe alone (run-39.md, 39-0.7).
    [switch]$Dock,

    # -boot strict=on with bootindex=0 on the IDE boot disk (one of the
    # workarounds for an NVMe present at POST on Windows 98).
    [switch]$BootStrict,
    # Add this project's usual xHCI (qemu-xhci,p3=0 on NT, qemu-xhci on 9x).
    [switch]$Xhci,

    # -trace <pattern> -D vm\usb4\<Name>\trace-<Tag>.log
    [switch]$Trace,
    [string[]]$TracePattern = @('usb4_*'),

    # Transfer disk: a host directory served as a read-only VVFAT drive
    # (default vm\usb4\<Name>\xfer, created if missing), or -XferImg, a raw
    # FAT image attached writable (read it back with mtools after shutdown).
    [string]$XferDir,
    [string]$XferImg,

    # NVMe behind the tunnel: an overlay nvme.qcow2 in the run directory over
    # -NvmeBase (read-only backing), or a blank -NvmeSize disk without one.
    [string]$NvmeBase,
    [string]$NvmeSize = '1G',

    [ValidateRange(0, 4)][int]$Slot = 0,
    [int]$QmpPort = 0,
    [int]$VncPort = 0,
    [int]$Smp = 0,
    [int]$MemMB = 0,
    [string]$Qemu,
    [string]$Tag,
    [switch]$NewOverlay,
    [int]$AdmitTimeoutSec = 900,
    [switch]$DryRun,
    [string[]]$Extra = @()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

# ---------------------------------------------------------------- locations
$scriptRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$commonDir = (& git -C $scriptRepo rev-parse --path-format=absolute --git-common-dir).Trim()
if (-not $commonDir) { throw 'cannot resolve the main checkout (git rev-parse failed)' }
$mainRoot = Split-Path -Parent ($commonDir -replace '/', '\')
$branch = (& git -C $scriptRepo rev-parse --abbrev-ref HEAD).Trim()
$vmRoot = Join-Path $mainRoot 'vm'
$usb4Root = Join-Path $vmRoot 'usb4'
$lockRoot = Join-Path $mainRoot 'out\vmlocks'
$emuRoot = 'D:\OneDrive\git-projects\usb4-emu'
if ($env:USB4EMU_ROOT) { $emuRoot = $env:USB4EMU_ROOT }
$emuBin = Join-Path $emuRoot 'toolchain\msys64\ucrt64\bin'
$emuShare = Join-Path $emuRoot 'vms\qemu-baseline\share'
$emuQemu = Join-Path $emuRoot 'src\qemu\build\qemu-system-x86_64.exe'
$qemuImg = Join-Path $emuRoot 'vms\qemu-baseline\qemu-img.exe'

foreach ($p in @($vmRoot, $emuBin, $emuShare, $qemuImg)) {
    if (-not (Test-Path $p)) { throw "missing: $p" }
}

# ---------------------------------------------------------------- the guests
# Machine lines follow each guest's existing launcher in
# docs/contributing/build-and-test.md, with two changes every guest needs on
# this fork: -accel tcg (WHPX aborts once any PCI-PCI bridge exists) and, on
# the 9x and Windows 2000 xHCI-only guests, -cpu pentium3,-apic (a TCG guest
# restart with a local APIC hangs at the boot logo, usb4-emu WIN98-TEST.md).
# XP and later run 2 vCPUs (owner, 2026-10-04); 98, ME and the 2000
# xHCI-only image 1; the 2000 SMP image 2.
$guests = @{
    'win98'       = @{ Img = 'win98.img';       Machine = 'pc';          Cpu = 'pentium3,-apic'; Mem = 256;  Smp = 1; Accel = 'tcg';              Vga = 'std';    Nt = $false; Port = 47900; Extra = @() }
    'winme'       = @{ Img = 'winme.img';       Machine = 'pc';          Cpu = 'pentium3,-apic'; Mem = 256;  Smp = 1; Accel = 'tcg';              Vga = 'std';    Nt = $false; Port = 47910; Extra = @() }
    'win2k-xonly' = @{ Img = 'win2k-xonly.img'; Machine = 'pc,acpi=off'; Cpu = 'pentium3,-apic'; Mem = 256;  Smp = 1; Accel = 'tcg';              Vga = 'cirrus'; Nt = $true;  Port = 47920; Extra = @('-global', 'ide-device.win2k-install-hack=on') }
    'win2k-smp'   = @{ Img = 'win2k-smp.img';   Machine = 'pc';          Cpu = 'pentium3';       Mem = 512;  Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'cirrus'; Nt = $true;  Port = 47930; Extra = @() }
    'winxp'       = @{ Img = 'winxp.img';       Machine = 'pc';          Cpu = 'pentium3';       Mem = 512;  Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'std';    Nt = $true;  Port = 47940; Extra = @() }
    'winxp64'     = @{ Img = 'winxp64.img';     Machine = 'pc';          Cpu = 'qemu64';         Mem = 2048; Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'std';    Nt = $true;  Port = 47950; Extra = @() }
    'vista'       = @{ Img = 'vista.img';       Machine = 'pc';          Cpu = 'qemu64';         Mem = 2048; Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'std';    Nt = $true;  Port = 47960; Extra = @('-audiodev', 'none,id=aud0') }
    'vista-x64'   = @{ Img = 'vista-x64.img';   Machine = 'pc';          Cpu = 'qemu64';         Mem = 2048; Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'std';    Nt = $true;  Port = 47970; Extra = @('-audiodev', 'none,id=aud0') }
    'win7'        = @{ Img = 'win7.img';        Machine = 'pc';          Cpu = 'qemu64';         Mem = 2048; Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'std';    Nt = $true;  Port = 47980; Extra = @('-audiodev', 'none,id=aud0') }
    'win7-x64'    = @{ Img = 'win7-x64.img';    Machine = 'pc';          Cpu = 'qemu64';         Mem = 2048; Smp = 2; Accel = 'tcg,thread=multi'; Vga = 'std';    Nt = $true;  Port = 47990; Extra = @('-audiodev', 'none,id=aud0') }
}
$g = $guests[$Guest]
if ($Smp -le 0) { $Smp = $g.Smp }
if ($MemMB -le 0) { $MemMB = $g.Mem }
if ($QmpPort -eq 0) { $QmpPort = $g.Port + 2 * $Slot }
if ($VncPort -eq 0) { $VncPort = $QmpPort + 1 }
foreach ($p in @($QmpPort, $VncPort)) {
    if ($p -lt 47900 -or $p -gt 47999) { throw "port $p is outside this project's block 47900-47999" }
}
if (-not $Name) { $Name = "$Guest-$Usb4Profile" }
if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw "bad -Name '$Name'" }
if ($RunRoot -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or $RunRoot -eq 'bin') { throw "bad -RunRoot '$RunRoot' (one directory name under vm\)" }
if (-not $Tag) { $Tag = Get-Date -Format 'yyyyMMdd-HHmmss' }
if ($Tag -notmatch '^[A-Za-z0-9._-]+$') { throw "bad -Tag '$Tag'" }

if ($Usb4Profile -ne 'generic' -and ($PreosCm -or $ForceTunnel -or $HostModel -ne 'minimal')) {
    throw '-PreosCm, -ForceTunnel and -HostModel apply to -Usb4Profile generic only'
}
if ($Usb4Profile -eq 'none' -and ($Usb3 -or $Dock)) { throw '-Usb3 and -Dock need a usb4 profile' }
if ($XferDir -and $XferImg) { throw 'give -XferDir or -XferImg, not both' }

# True when path lies under root.
function Test-Under([string]$path, [string]$root) {
    $p = [System.IO.Path]::GetFullPath($path).TrimEnd('\') + '\'
    $r = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    return $p.StartsWith($r, [System.StringComparison]::OrdinalIgnoreCase)
}
$runParent = Join-Path $vmRoot $RunRoot
$runDir = Join-Path $runParent $Name
$base = Join-Path $vmRoot $g.Img
if ($BaseImage) {
    # a relative path names a file of the main checkout (vm\ lives there)
    if (-not [System.IO.Path]::IsPathRooted($BaseImage)) { $BaseImage = Join-Path $mainRoot $BaseImage }
    if (-not (Test-Path $BaseImage -PathType Leaf)) { throw "-BaseImage $BaseImage does not exist" }
    $base = (Resolve-Path $BaseImage).Path
    if (-not (Test-Under $base $vmRoot) -or $base -notmatch '\.(qcow2|img)$') {
        throw "-BaseImage must be a qcow2 image under $vmRoot"
    }
}
$disk = Join-Path $runDir 'disk.qcow2'
$nvme = Join-Path $runDir 'nvme.qcow2'
if (-not (Test-Path $base)) { throw "base image missing: $base" }
if (Test-Under $base $runDir) { throw "-BaseImage cannot sit in the run directory $runDir" }

# ---------------------------------------------------------------- QEMU binary
# Default: the fork's build, copied to vm\usb4\bin under the admission lock.
$qemuIsCopy = $false
if (-not $Qemu) {
    $Qemu = Join-Path $usb4Root 'bin\qemu-system-x86_64.exe'
    $qemuIsCopy = $true
}
$env:PATH = "$emuBin;$env:PATH"

function Invoke-QemuImg([string[]]$a) {
    $out = & $qemuImg @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "qemu-img $($a -join ' ') failed: $out" }
}

# ---------------------------------------------------------------- disks
# A writable transfer image must be a raw xfer*.img under vm\<RunRoot>\, so no
# base image (vm\*.img) and nothing in usb4-emu can be attached writable; an
# NVMe base is only ever a qcow2 backing file.
if ($XferImg) {
    if (-not (Test-Path $XferImg -PathType Leaf)) { throw "-XferImg $XferImg does not exist" }
    $XferImg = (Resolve-Path $XferImg).Path
    if (-not (Test-Under $XferImg $runParent) -or (Split-Path -Leaf $XferImg) -notmatch '^xfer[A-Za-z0-9._-]*\.img$') {
        throw "-XferImg must be a raw image named xfer*.img under $runParent (it is attached writable)"
    }
}
if ($NvmeBase) {
    if (-not (Test-Path $NvmeBase -PathType Leaf)) { throw "-NvmeBase $NvmeBase does not exist" }
    $NvmeBase = (Resolve-Path $NvmeBase).Path
}
if (-not $XferImg -and -not $XferDir) { $XferDir = Join-Path $runDir 'xfer' }
$ustor = $null
$udisk = $null

$dbg = Join-Path $runDir "debugcon-$Tag.log"
$ser = Join-Path $runDir "serial-$Tag.log"
$trc = Join-Path $runDir "trace-$Tag.log"
$pidFile = Join-Path $runDir 'qemu.pid'
$a = New-Object System.Collections.Generic.List[string]
function Add([string[]]$x) { foreach ($i in $x) { $a.Add($i) } }

Add @('-name', "xhci98-usb4-$Name")
Add @('-machine', $g.Machine, '-accel', $g.Accel, '-cpu', $g.Cpu, '-smp', "$Smp", '-m', "$MemMB")
Add @('-L', $emuShare)
Add @('-display', 'none', '-vga', $g.Vga, '-vnc', "127.0.0.1:$($VncPort - 5900)")
Add @('-qmp', "tcp:127.0.0.1:$QmpPort,server=on,wait=off")
Add @('-pidfile', $pidFile)
Add @('-rtc', 'base=localtime', '-nic', 'none')
Add $g.Extra
if ($BootStrict) {
    Add @('-drive', "if=none,id=hd0,format=qcow2,file=$disk")
    Add @('-device', 'ide-hd,drive=hd0,bus=ide.0,unit=0,bootindex=0')
    Add @('-boot', 'strict=on')
} else {
    Add @('-drive', "if=ide,index=0,format=qcow2,file=$disk")
    Add @('-boot', 'c')
}
if ($XferImg) {
    Add @('-drive', "if=ide,index=1,format=raw,file=$XferImg")
} else {
    Add @('-drive', "if=ide,index=1,format=raw,snapshot=on,file=fat:16:$XferDir")
}
Add @('-chardev', "file,id=dbgcon,path=$dbg", '-device', 'isa-debugcon,iobase=0xe9,chardev=dbgcon')
Add @('-serial', "file:$ser")
if ($Xhci) {
    if ($g.Nt) { Add @('-device', 'qemu-xhci,p3=0,id=xhci') } else { Add @('-device', 'qemu-xhci,id=xhci') }
}

$enc0 = $null
$tunnelBridge = $null
if ($Usb4Profile -ne 'none') {
    Add @('-global', 'PIIX4_PM.acpi-pci-hotplug-with-bridge-support=off')
    Add @('-drive', "if=none,id=nvme0,format=qcow2,file=$nvme")
    $encExtra = ''
    if ($Usb3) {
        $ustor = Join-Path $runDir 'ustor.qcow2'
        Add @('-device', 'qemu-xhci,id=u4x,bus=pci.0,addr=0x7')
        Add @('-drive', "if=none,id=ustor,format=qcow2,file=$ustor")
        Add @('-device', 'usb-storage,id=ustor0,bus=u4x.0,port=1,drive=ustor,serial=USB3EMU01')
        Add @('-global', 'usb4-host.usb3-xhci=u4x', '-global', 'usb4-host.usb3-port1=1')
        $encExtra += ',usb3-dev=ustor0'
    }
    if ($Dock) {
        $udisk = Join-Path $runDir 'usbdisk.qcow2'
        Add @('-blockdev', "driver=qcow2,node-name=usbdisk,file.driver=file,file.filename=$udisk")
        Add @('-netdev', 'user,id=docknet,restrict=on')
        $encExtra += ',dock=on,nic-netdev=docknet,usb-drive=usbdisk'
    }
    if ($Usb4Profile -eq 'tb3') {
        $assist = if ($Bios.StartsWith('ON_')) { 'on' } else { 'off' }
        $preboot = if ($Bios.EndsWith('_ON')) { 'on' } else { 'off' }
        $hostDev = 'usb4-host,id=u4,bus=tbd0,addr=0x0,tunnel-port=tb0,msi=off,msix=off'
        $hostDev += ",profile=tb3-alpine-ridge,bios-assist=$assist,preboot=$preboot"
        # enc0's UUID (serial USB4EMU01 on port 1) is in the boot ACL
        $hostDev += ',boot-acl=203de24d-7c47-f446-ffff-ffffffffffff'
        Add @('-device', "usb4-tunnel-bridge,id=tbrp,bus=pci.0,addr=$RootAddr,chassis_nr=1,msi=off")
        Add @('-device', 'usb4-tb3-pci-bridge,id=tbusp,bus=tbrp,addr=0x1,chassis_nr=2,shpc=off,msi=off')
        Add @('-device', 'usb4-tb3-pci-bridge,id=tbd0,bus=tbusp,addr=0x0,chassis_nr=3,shpc=off,msi=off')
        Add @('-device', 'usb4-tb3-tunnel-bridge,id=tb0,bus=tbusp,addr=0x1,chassis_nr=4,shpc-autoenable=on,msi=off')
        Add @('-device', 'usb4-tb3-pci-bridge,id=tbd2,bus=tbusp,addr=0x2,chassis_nr=5,shpc=off,msi=off')
        Add @('-device', $hostDev)
        if ($assist -eq 'off') { Add @('-device', 'usb4-tb3-xhci,id=tbxhci,bus=tbd2,addr=0x0,msi=off,msix=off') }
        $enc0 = "usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01,dialect=tb3$encExtra"
    } else {
        $tb = 'usb4-tunnel-bridge,id=tb0,bus=pci.0,addr=0x5,chassis_nr=1,shpc-autoenable=on,msi=off'
        if ($ForceTunnel) { $tb += ',x-force-tunnel=on,nvme-drive=nvme0,nvme-serial=USB4EMU01' }
        $hostDev = 'usb4-host,id=u4,bus=pci.0,addr=0x4,tunnel-port=tb0,msi=off,msix=off,profile=usb4-generic'
        if ($HostModel -ne 'minimal') { $hostDev += ",host-model=$HostModel" }
        if ($PreosCm) { $hostDev += ',preos-cm=on' }
        Add @('-device', $tb)
        Add @('-device', $hostDev)
        $enc0 = "usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01$encExtra"
    }
    $tunnelBridge = 'tb0'
    if ($DeviceAtBoot -eq 1) { Add @('-device', $enc0) }
}
if ($Trace) {
    foreach ($t in (($TracePattern -join ',') -split ',')) { Add @('-trace', $t) }
    Add @('-D', $trc)
}
# -Extra is an allowlist, so it cannot take over what this script owns (the
# monitor and display ports, the disks, the vCPU and memory counts the
# admission check budgets): -S alone, and -device, -global, -trace and -d
# each with one value that names no backend, file or path.
$i = 0
while ($i -lt $Extra.Count) {
    $x = $Extra[$i]
    if ($x -ceq '-S') { $i++; continue }
    if (@('-device', '-global', '-trace', '-d') -cnotcontains $x) {
        throw "-Extra may carry only -S, -device, -global, -trace and -d (got $x)"
    }
    if ($i + 1 -ge $Extra.Count) { throw "-Extra $x needs a value" }
    $v = $Extra[$i + 1]
    # No backend, and no path at all: a property such as usb-mouse's pcap=
    # opens a file for writing, so any value with a path separator or a
    # drive colon is refused along with the named backends.
    if ($v -match '(^|[,.])(drive|file|filename|chardev|netdev|romfile|pcap|path|logfile|events)=' -or
        $v -match '^-' -or $v -match '[\\/:]') {
        throw "-Extra $x $v names a backend, a file or a path; the launcher owns those"
    }
    # The model refuses host-model on tb3-alpine-ridge and ignores preos-cm
    # there, as -HostModel and -PreosCm are refused above.
    if ($x -ceq '-global' -and $v -match '^usb4-host\.(host-model|preos-cm)=' -and $Usb4Profile -ne 'generic') {
        throw "-Extra $x $v applies to -Usb4Profile generic only"
    }
    $i += 2
}
Add $Extra

function Quote([string]$s) {
    if ($s -match '[\s"&|<>^%]') {
        if ($s -match '[%"]') { throw "argument cannot be quoted safely for cmd: $s" }
        return '"' + $s + '"'
    }
    return $s
}
$cmdline = (Quote $Qemu) + ' ' + (($a | ForEach-Object { Quote $_ }) -join ' ')
if ($DryRun) { Write-Output $cmdline; return }

# ---------------------------------------------------------------- ports
foreach ($p in @($QmpPort, $VncPort)) {
    $l = New-Object System.Net.Sockets.TcpListener ([System.Net.IPAddress]::Loopback), $p
    try { $l.Start() } catch { throw "port $p cannot be bound (in use, or in a Windows excluded range): $($_.Exception.Message)" }
    finally { try { $l.Stop() } catch { } }
}

# ---------------------------------------------------------------- admission
# Everything that changes shared state - the guest lock, the binary copy, the
# overlays, the launch - happens while this script holds _admit, so two
# launches (of this script or of another worker's) are serialised.
New-Item -ItemType Directory -Force $lockRoot | Out-Null
$admit = Join-Path $lockRoot '_admit'
$guestLock = Join-Path $lockRoot "usb4-$Name.lock"
$runJson = Join-Path $runDir 'run.json'
$token = [guid]::NewGuid().ToString('N')

function Test-PidAlive([int]$procId) {
    if ($procId -le 0) { return $false }
    return [bool](Get-Process -Id $procId -ErrorAction SilentlyContinue)
}
# vCPUs a running QEMU uses, from its -smp argument; $null when unknown.
function Get-QemuVcpus([string]$cl) {
    if (-not $cl) { return $null }
    if ($cl -notmatch '-smp\s+"?([^\s"]+)') { return 1 }
    $v = $Matches[1]
    if ($v -match '^(\d+)(,|$)') { return [int]$Matches[1] }
    if ($v -match '(^|,)cpus=(\d+)') { return [int]$Matches[2] }
    # QEMU starts maxcpus vCPUs when cpus is omitted
    if ($v -match '(^|,)maxcpus=(\d+)') { return [int]$Matches[2] }
    $n = 1
    foreach ($k in @('sockets', 'dies', 'clusters', 'cores', 'threads')) {
        if ($v -match "(^|,)$k=(\d+)") { $n *= [int]$Matches[2] }
    }
    return $n
}

$admitted = $false
$deadline = (Get-Date).AddSeconds($AdmitTimeoutSec)
while (-not $admitted) {
    try { New-Item -ItemType Directory $admit -ErrorAction Stop | Out-Null; $admitted = $true }
    catch {
        if ((Get-Date) -gt $deadline) { throw "admission lock $admit not free after $AdmitTimeoutSec s" }
        Start-Sleep -Seconds 3
    }
}
$lockOwned = $false
$launched = $false
try {
    if (Test-Path $guestLock) {
        $old = Get-Content $guestLock -Raw
        if ($old -notmatch 'pid=(\d+)') { throw "$guestLock exists and names no pid; remove it by hand if stale" }
        $oldPid = [int]$Matches[1]
        if (Test-PidAlive $oldPid) { throw "$Name is already running (pid $oldPid, $guestLock)" }
        if ($oldPid -eq 0 -and (Get-Item $guestLock).LastWriteTime -gt (Get-Date).AddMinutes(-10)) {
            throw "$guestLock records a launch in progress (pid=0, under 10 minutes old)"
        }
        Remove-Item $guestLock -Force
    }

    $cpus = (Get-CimInstance Win32_Processor | Measure-Object NumberOfLogicalProcessors -Sum).Sum
    $used = 0
    foreach ($q in @(Get-CimInstance Win32_Process -Filter "Name like 'qemu-system%'")) {
        $n = Get-QemuVcpus $q.CommandLine
        if ($null -eq $n) { throw "cannot read the command line of qemu pid $($q.ProcessId); vCPU budget unknown" }
        $used += $n
    }
    $freeMB = [int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1024)
    Write-Host "admission: running vCPUs $used + $Smp of $cpus; free RAM $freeMB MB, this guest $MemMB MB"
    if ($used + $Smp -gt $cpus) { throw "vCPU budget: $used running + $Smp > $cpus logical processors; wait for a guest to stop" }
    if ($freeMB - $MemMB -lt 6144) { throw "RAM: $freeMB MB free, $MemMB MB for this guest would leave less than 6 GB" }

    Set-Content -Path $guestLock -Value "branch=$branch name=$Name guest=$Guest pid=0 token=$token started=$(Get-Date -Format s)" -Encoding ASCII
    $lockOwned = $true

    # The fork's binary, copied so a rebuild in usb4-emu is never blocked.
    if ($qemuIsCopy) {
        $srcInfo = Get-Item $emuQemu
        $copy = $true
        if (Test-Path $Qemu) {
            $have = Get-Item $Qemu
            $copy = ($have.Length -ne $srcInfo.Length -or $have.LastWriteTimeUtc -lt $srcInfo.LastWriteTimeUtc)
        }
        if ($copy) {
            $running = @(Get-CimInstance Win32_Process -Filter "Name like 'qemu-system%'" |
                         Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $Qemu) })
            if ($running.Count -gt 0) {
                Write-Warning "a newer fork binary exists but $Qemu is in use; keeping the old copy"
            } else {
                New-Item -ItemType Directory -Force (Split-Path $Qemu) | Out-Null
                Copy-Item $emuQemu $Qemu -Force
                Write-Host "copied $emuQemu ($($srcInfo.LastWriteTime)) to $Qemu"
            }
        }
    }
    if (-not (Test-Path $Qemu)) { throw "QEMU binary missing: $Qemu" }

    # Overlays: the base images are only ever backing files.
    New-Item -ItemType Directory -Force $runDir | Out-Null
    if ($NewOverlay) {
        foreach ($f in @($disk, $nvme)) { if (Test-Path $f) { Remove-Item $f -Force } }
    }
    if (-not (Test-Path $disk)) {
        Invoke-QemuImg @('create', '-q', '-f', 'qcow2', '-b', $base, '-F', 'qcow2', $disk)
        Write-Host "overlay $disk over $base"
    }
    if ($Usb4Profile -ne 'none' -and -not (Test-Path $nvme)) {
        if ($NvmeBase) {
            Invoke-QemuImg @('create', '-q', '-f', 'qcow2', '-b', $NvmeBase, '-F', 'qcow2', $nvme)
        } else {
            Invoke-QemuImg @('create', '-q', '-f', 'qcow2', $nvme, $NvmeSize)
        }
    }
    foreach ($f in @($ustor, $udisk)) {
        if ($f -and -not (Test-Path $f)) { Invoke-QemuImg @('create', '-q', '-f', 'qcow2', $f, '256M') }
    }
    if (-not $XferImg) { New-Item -ItemType Directory -Force $XferDir | Out-Null }

    # A generated batch file carries PATH (the fork's DLLs) and the
    # redirections; WMI starts it so QEMU is not a child of this shell and
    # survives the shell that launched it.
    if (Test-Path $pidFile) { Remove-Item $pidFile -Force }
    $bat = Join-Path $runDir "launch-$Tag.cmd"
    $out = Join-Path $runDir "qemu-$Tag.out"
    $batText = "@echo off`r`nset `"PATH=$emuBin;%PATH%`"`r`ncd /d `"$runDir`"`r`n$cmdline > `"$out`" 2>&1`r`n"
    [System.IO.File]::WriteAllText($bat, $batText, [System.Text.Encoding]::ASCII)
    Set-Content -Path (Join-Path $runDir 'qemu.cmdline') -Value $cmdline -Encoding ASCII
    $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = "cmd.exe /c `"$bat`""; CurrentDirectory = $runDir }
    if ($r.ReturnValue -ne 0) { throw "Win32_Process.Create returned $($r.ReturnValue)" }

    $qpid = 0
    for ($i = 0; $i -lt 60 -and $qpid -eq 0; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $pidFile) {
            $t = (Get-Content $pidFile -Raw) -replace '[^0-9]', ''
            if ($t) { $qpid = [int]$t }
        }
        if ($qpid -eq 0 -and -not (Test-PidAlive ([int]$r.ProcessId)) -and (Test-Path $out) -and (Get-Item $out).Length -gt 0) { break }
    }
    $proc = $null
    if ($qpid -gt 0) { $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $qpid" }
    if (-not $proc -or -not $proc.CommandLine -or $proc.CommandLine -notlike "*tcp:127.0.0.1:$QmpPort,*") {
        $msg = ''
        if (Test-Path $out) { $msg = Get-Content $out -Raw }
        throw "QEMU did not start; $out says: $msg"
    }
    $created = $proc.CreationDate.ToUniversalTime().ToString('o')
    $launched = $true
    Set-Content -Path $guestLock -Value "branch=$branch name=$Name guest=$Guest pid=$qpid token=$token started=$(Get-Date -Format s)" -Encoding ASCII

    $info = [ordered]@{
        name = $Name; guest = $Guest; profile = $Usb4Profile; bios = $Bios; deviceAtBoot = $DeviceAtBoot
        hostModel = $HostModel; preosCm = [bool]$PreosCm; usb3 = [bool]$Usb3; dock = [bool]$Dock
        bootStrict = [bool]$BootStrict; dvorak = [bool]$g.Nt
        qmpPort = $QmpPort; vncPort = $VncPort; pid = $qpid; created = $created; qemu = $proc.ExecutablePath
        token = $token; branch = $branch; tag = $Tag
        runRoot = $RunRoot; base = $base; runDir = $runDir; lock = $guestLock; enc0 = $enc0; tunnelBridge = $tunnelBridge
        debugcon = $dbg; serial = $ser; trace = $(if ($Trace) { $trc } else { $null }); out = $out
        started = (Get-Date -Format s)
    }
    Set-Content -Path $runJson -Value ($info | ConvertTo-Json) -Encoding ASCII
} finally {
    if ($lockOwned -and -not $launched -and (Test-Path $guestLock)) {
        if ((Get-Content $guestLock -Raw) -match "token=$token\b") { Remove-Item $guestLock -Force }
    }
    Remove-Item $admit -Force -ErrorAction SilentlyContinue
}

Write-Host "started $Name (pid $qpid): QMP 127.0.0.1:$QmpPort, VNC 127.0.0.1:$VncPort"
Write-Host "  debugcon $dbg"
if ($Trace) { Write-Host "  trace    $trc" }
Write-Host "  stop:    powershell -File scripts\usb4\usb4-qmp.ps1 -Name $Name -RunRoot $RunRoot quit"
