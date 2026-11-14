# The release matrix's guests: configuration, the QEMU command line, starting
# points (a saved machine state beside a frozen disk), and the serial protocol
# the guest scripts in leg\ speak. Dot-sourced by make-start.ps1, run-leg.ps1
# and release-matrix.ps1. build-and-test.md, "Faster emulator legs", is the
# design.

. (Join-Path $PSScriptRoot 'repo.ps1')
. (Join-Path $PSScriptRoot 'qemu.ps1')
. (Join-Path $PSScriptRoot 'qmp.ps1')
. (Join-Path $PSScriptRoot 'iso.ps1')

function Read-LegConfig {
    param([string]$Path)
    if (-not $Path) { $Path = Join-Path (Get-VmMatrixRepoRoot) 'scripts\vm-matrix\release.config.psd1' }
    $Path = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "no configuration at ${Path}: copy scripts\vm-matrix\release.config.sample.psd1 there and edit it"
    }
    $c = Import-PowerShellDataFile -LiteralPath $Path
    foreach ($k in 'WorkDir', 'PortBase', 'PortCount', 'Guests') {
        if (-not $c.ContainsKey($k)) { throw "$Path has no $k" }
    }
    $c.WorkDir = Resolve-RepoPath $c.WorkDir
    # QemuDllPath: a QEMU build that needs its runtime DLLs on PATH (the
    # usb4-emu build the q35 bases run on); prepended for this process and
    # every QEMU it starts.
    if ($c.ContainsKey('QemuDllPath') -and $c.QemuDllPath) {
        $c.QemuDllPath = Resolve-RepoPath $c.QemuDllPath
        if (-not (Test-Path -LiteralPath $c.QemuDllPath)) { throw "QemuDllPath $($c.QemuDllPath) does not exist" }
        # First on PATH even when already on it further down: another toolchain's
        # DLLs before it would be loaded instead.
        $env:PATH = ((@($c.QemuDllPath) + @($env:PATH -split ';' | Where-Object { $_ -and $_ -ne $c.QemuDllPath })) -join ';')
    }
    $c.QemuExe = Resolve-QemuBinary -Hint ([string]$c.Qemu)
    $c.QemuImg = Join-Path (Split-Path -Parent $c.QemuExe) 'qemu-img.exe'
    if (-not (Test-Path -LiteralPath $c.QemuImg)) { throw "no qemu-img.exe beside $($c.QemuExe)" }
    if ($c.PortCount -lt 3) { throw 'PortCount: each leg takes three ports (QMP, VNC, migration)' }
    $ids = @{}
    foreach ($g in $c.Guests) {
        foreach ($k in 'Id', 'Name', 'Family', 'Base', 'Cpu', 'Smp', 'Memory', 'Layout') {
            if (-not $g.ContainsKey($k)) { throw "guest $($g.Id): no $k" }
        }
        if ($g.Family -notin '9x', 'nt') { throw "guest $($g.Id): Family is 9x or nt" }
        if ($g.Layout -notin 'qwerty', 'dvorak') { throw "guest $($g.Id): Layout is qwerty or dvorak" }
        if ($g.ContainsKey('Whpx') -and $g.Whpx -and "$($g.Whpx)" -notmatch '^whpx(,[A-Za-z0-9=_-]+)*$') { throw "guest $($g.Id): Whpx is the -accel value for WHPX on this host: whpx, or whpx,<option>=<value>" }
        if ($ids.ContainsKey($g.Id)) { throw "guest $($g.Id) twice" }
        $ids[$g.Id] = $true
        $g.Base = Resolve-RepoPath $g.Base
    }
    return $c
}

function Get-LegGuest {
    param($Config, [string]$Id)
    foreach ($g in $Config.Guests) { if ($g.Id -eq $Id) { return $g } }
    throw "no guest '$Id' in the configuration"
}

# A slot is three consecutive ports of the configured block: QMP, VNC (as
# display number port-5900) and the migration socket a starting point is
# written to and read from.
function Get-LegPorts {
    param($Config, [int]$Slot)
    $first = $Config.PortBase + 3 * $Slot
    if ($Slot -lt 0 -or $first + 2 -gt $Config.PortBase + $Config.PortCount - 1) {
        throw "slot $Slot is outside ports $($Config.PortBase)-$($Config.PortBase + $Config.PortCount - 1)"
    }
    return @{ Qmp = $first; Vnc = $first + 1; Mig = $first + 2 }
}

# The machine itself: every argument a saved state depends on, and nothing a
# run changes (paths, ports, chardev files). A starting point records its hash;
# a leg refuses a state saved under any other.
#
# The 9x guests take a CPU with a local APIC (plain pentium3): on QEMU 11.1 a
# qemu-xhci whose MSI-X could not be set up (no APIC, so no MSI) fails its own
# state load with an assertion in msix_vector_unuse, measured on development
# host A on 2026-10-22. Windows 98 SE and ME do not use the APIC.
#
# -Accel: the -accel value of one start (Get-LegAccelArg). Without it the
# line is TCG's, as every starting point was saved: make-start.ps1 and the
# hash never pass it, so a starting point is TCG's whatever its legs run on.
function Get-LegHardwareArgs {
    param($Guest, [string]$Accel = '')
    # (PowerShell's names are case-blind: the parameter is the variable.)
    if (-not $Accel) { $Accel = Get-LegAccelArg $Guest 'tcg' }
    $machine = 'pc'
    if ($Guest.ContainsKey('Machine') -and $Guest.Machine) { $machine = $Guest.Machine }
    $vga = 'std'
    if ($Guest.ContainsKey('Vga') -and $Guest.Vga) { $vga = $Guest.Vga }
    $rtc = 'base=localtime'
    if ($Guest.ContainsKey('Rtc') -and $Guest.Rtc) { $rtc = "base=$($Guest.Rtc)" }
    $a = @('-machine', $machine, '-accel', $Accel, '-cpu', $Guest.Cpu, '-smp', [string]$Guest.Smp,
        '-m', [string]$Guest.Memory, '-vga', $vga, '-nic', 'none', '-rtc', $rtc)
    if ($machine -notlike 'q35*') {
        $a += @('-device', 'ide-hd,drive=hd0,bus=ide.0,unit=0',
            '-device', 'ide-cd,id=cd,drive=cdd,bus=ide.1,unit=0',
            '-device', 'isa-debugcon,iobase=0xe9,chardev=dbg',
            '-device', 'qemu-xhci,id=xh,p2=4,p3=4')
    } else {
        # The q35 machine as the q35 bases run it (<vm root>\q35-bases,
        # launch-q35.ps1; owner, 2026-10-25): native PCIe hot-plug, the
        # controller behind a root port as on a board. DiskBus ahci puts the
        # disk on the board's ICH9 AHCI port 0; piix (2000, XP: no inbox AHCI
        # driver; ME: fails on AHCI) on a piix3-ide at 00:1e.0 with the LBA
        # geometry the bases were made with. CdBus (default: DiskBus) puts the
        # leg's CD on AHCI port 2 or on that piix3-ide's secondary channel.
        # CdBus disk: 98 SE reaches AHCI through INT 13h alone, so it sees no
        # CD there, and a piix3-ide beside the AHCI stopped its start at the
        # logo (read 2026-10-26); its leg's files are a read-only FAT disk
        # on AHCI port 1 instead (QEMU's vvfat over the staged folder), which
        # 98 SE reads as D:. A disk cannot be changed under a running guest,
        # so such a guest's legs start cold with it (Test-LegDiskMedium).
        $disk = 'ahci'; if ($Guest.ContainsKey('DiskBus') -and $Guest.DiskBus) { $disk = $Guest.DiskBus }
        $cdb = $disk; if ($Guest.ContainsKey('CdBus') -and $Guest.CdBus) { $cdb = $Guest.CdBus }
        $a += @('-global', 'ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off')
        if ($Guest.ContainsKey('Hole64Off') -and $Guest.Hole64Off) { $a += @('-global', 'q35-pcihost.pci-hole64-size=0') }
        if ($Guest.ContainsKey('Win2kHack') -and $Guest.Win2kHack) { $a += @('-global', 'ide-device.win2k-install-hack=on') }
        if ($disk -eq 'piix' -or $cdb -eq 'piix') { $a += @('-device', 'piix3-ide,id=pide,bus=pcie.0,addr=0x1e') }
        if ($disk -eq 'piix') { $a += @('-device', 'ide-hd,drive=hd0,bus=pide.0,unit=0,bootindex=0,lcyls=1024,lheads=255,lsecs=63') }
        else { $a += @('-device', 'ide-hd,drive=hd0,bus=ide.0,bootindex=0') }
        if ($cdb -eq 'piix') { $a += @('-device', 'ide-cd,id=cd,drive=cdd,bus=pide.1,unit=0') }
        elseif ($cdb -eq 'disk') { $a += @('-device', 'ide-hd,id=cd,drive=cdd,bus=ide.1') }
        else { $a += @('-device', 'ide-cd,id=cd,drive=cdd,bus=ide.2') }
        $a += @('-device', 'isa-debugcon,iobase=0xe9,chardev=dbg',
            '-device', 'pcie-root-port,id=rpx,bus=pcie.0,addr=0x1c.0,chassis=20,slot=20',
            '-device', 'qemu-xhci,id=xh,p2=4,p3=4,bus=rpx')
    }
    if ($Guest.ContainsKey('Extra') -and $Guest.Extra) { $a += @($Guest.Extra) }
    return , $a
}

# The accelerator a leg runs on (roadmap-usb4 49.4; owner, 2026-11-11: the
# two Windows 10 legs on WHPX, every other leg and every starting point on
# TCG). A leg's own is its Accel in release-legs.psd1, tcg when it names
# none; -ForceTcg (run-leg.ps1, release-matrix.ps1) runs a WHPX leg on TCG,
# the periodic cross-check. Nothing here turns a TCG leg into a WHPX one,
# and nothing falls back: a WHPX leg on a host without WHPX is refused
# (Assert-LegWhpx). Returns Own and Use (tcg or whpx) and Forced.
function Resolve-LegAccel {
    param($Def, [bool]$ForceTcg = $false)
    $own = 'tcg'
    if ($Def.ContainsKey('Accel') -and $null -ne $Def.Accel -and "$($Def.Accel)" -ne '') { $own = "$($Def.Accel)" }
    if ($own -cnotin 'tcg', 'whpx') { throw "leg $($Def.Id): Accel is tcg or whpx, not '$own'" }
    $use = $own
    if ($ForceTcg) { $use = 'tcg' }
    return @{ Own = $own; Use = $use; Forced = ($use -ne $own) }
}

# The -accel value for a guest under tcg or whpx. TCG's is what it always
# was, so a starting point's hash does not move: multi-threaded for a guest
# with more than one processor. WHPX's is the guest's Whpx when the
# configuration gives one (a host whose WHPX needs kernel-irqchip=off),
# else plain whpx. One value, never a list: QEMU tries a second -accel when
# the first cannot start, which would hide the accelerator a reading came
# from.
function Get-LegAccelArg {
    param($Guest, [string]$Kind)
    if ($Kind -ceq 'tcg') {
        if ([int]$Guest.Smp -gt 1) { return 'tcg,thread=multi' }
        return 'tcg'
    }
    if ($Kind -ceq 'whpx') {
        if ($Guest.ContainsKey('Whpx') -and $Guest.Whpx) { return [string]$Guest.Whpx }
        return 'whpx'
    }
    throw "accelerator '$Kind': tcg or whpx"
}

# Holds a QEMU command line to one accelerator, the one asked for: exactly
# one -accel, with that value, and no other way of naming one (a guest's
# Extra or a leg's backends could carry a second -accel, which QEMU would
# try when the first cannot start; -machine accel= and -enable-kvm are the
# older spellings). Throws otherwise.
function Assert-LegOneAccel {
    param([string[]]$QemuArgs, [string]$Accel)
    $n = 0
    for ($i = 0; $i -lt $QemuArgs.Count; $i++) {
        $x = [string]$QemuArgs[$i]
        if ($x -ceq '-accel') {
            $n++
            if ($i + 1 -ge $QemuArgs.Count -or [string]$QemuArgs[$i + 1] -cne $Accel) { throw "the QEMU command line names an accelerator other than the leg's '$Accel' (-accel $($QemuArgs[$i + 1])): take it out of the guest's Extra" }
        }
        if ($x -ceq '-enable-kvm' -or $x -ceq '-no-kvm') { throw "the QEMU command line carries $x beside the leg's -accel ${Accel}: take it out of the guest's Extra" }
        if (($x -ceq '-machine' -or $x -ceq '-M') -and $i + 1 -lt $QemuArgs.Count -and [string]$QemuArgs[$i + 1] -match '(^|,)accel=') { throw "the QEMU command line names an accelerator in -machine ($($QemuArgs[$i + 1])) beside the leg's -accel ${Accel}: take it out" }
    }
    if ($n -ne 1) { throw "the QEMU command line holds $n -accel options, not the leg's one (-accel $Accel)" }
}

# The accelerator family of an -accel value: the word before its options.
function Get-LegAccelKind {
    param([string]$Accel)
    return ($Accel -split ',')[0]
}

# Whether QEMU's "-accel help" text lists an accelerator.
function Test-LegAccelListed {
    param([string[]]$HelpLines, [string]$Kind)
    foreach ($l in @($HelpLines)) { if ("$l".Trim() -ceq $Kind) { return $true } }
    return $false
}

# The accelerator a running QEMU says it runs on (QMP query-accelerators,
# which QEMU 11.1.2 answers; read on development host A on 2026-11-11), or
# '' when this QEMU does not answer that.
function Get-LegAccelLive {
    param([int]$QmpPort)
    try {
        $r = Invoke-Qmp -Port $QmpPort -Command 'query-accelerators' -AllowError -TimeoutMs 5000
        if ($r -and $r.PSObject.Properties['enabled'] -and $r.enabled) { return [string]$r.enabled }
    } catch { }
    return ''
}

# The command line of the WHPX probe: the guest's own machine, processors,
# memory and -accel value and its Extra (the firmware's -L), no disk and no
# default device, stopped (-S), QMP on the given port.
function Get-LegWhpxProbeArgs {
    param($Guest, [int]$QmpPort)
    $machine = 'pc'
    if ($Guest.ContainsKey('Machine') -and $Guest.Machine) { $machine = $Guest.Machine }
    $a = @('-name', 'xhleg-whpx-probe', '-machine', $machine, '-accel', (Get-LegAccelArg $Guest 'whpx'),
        '-cpu', $Guest.Cpu, '-smp', [string]$Guest.Smp, '-m', [string]$Guest.Memory,
        '-nodefaults', '-display', 'none', '-S', '-qmp', "tcp:127.0.0.1:$QmpPort,server=on,wait=off")
    if ($Guest.ContainsKey('Extra') -and $Guest.Extra) { $a += @($Guest.Extra) }
    return , $a
}

# Refuses a WHPX leg where WHPX cannot run it, before anything of the leg is
# started: this QEMU must list whpx, and the probe (Get-LegWhpxProbeArgs)
# must come up and say whpx is what it runs on. WHPX makes its partition and
# processors before QMP answers, so a host whose hypervisor platform is off,
# or whose WHPX needs another option, ends the probe with QEMU's own
# message, which the refusal carries. There is no fallback: the caller
# stops, and -ForceTcg is the operator's to give. Returns the -accel value.
function Assert-LegWhpx {
    param($Config, $Guest, [hashtable]$Ports, [string]$What, [string]$Dir)
    $accel = Get-LegAccelArg $Guest 'whpx'
    $tail = 'Nothing was started. A WHPX leg does not fall back to TCG; -ForceTcg runs it under TCG (the cross-check).'
    $help = @(& $Config.QemuExe -accel help 2>$null)
    if (-not (Test-LegAccelListed $help 'whpx')) {
        throw "${What}: $($Config.QemuExe) has no WHPX (-accel help: $((@($help | Select-Object -Skip 1) -join ' ').Trim())). $tail"
    }
    if (-not (Test-LegPortFree $Ports.Qmp)) { throw "${What}: port $($Ports.Qmp) is in use (the WHPX probe). $tail" }
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $err = Join-Path $Dir 'whpx-probe.err'
    $pa = Get-LegWhpxProbeArgs $Guest $Ports.Qmp
    Assert-LegOneAccel $pa $accel
    $line = ($pa | ForEach-Object { Quote-LegArg $_ }) -join ' '
    $p = Start-Process -FilePath $Config.QemuExe -ArgumentList $line -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput (Join-Path $Dir 'whpx-probe.out')
    # From here the probe is ended whatever happens, a failed log line too.
    try {
        $null = $p.Handle
        Add-LegPidLog $Config "started xhleg-whpx-probe pid $($p.Id) qmp $($Ports.Qmp) ($What)"
        $deadline = (Get-Date).AddSeconds(30)
        while (-not (Test-QmpUp $Ports.Qmp)) {
            if ($p.HasExited) {
                $said = "$(Get-Content -Raw -LiteralPath $err -ErrorAction SilentlyContinue)".Trim()
                throw "${What}: WHPX cannot run this guest here (-accel $accel): $said $tail"
            }
            if ((Get-Date) -gt $deadline) { throw "${What}: the WHPX probe's QMP did not come up on $($Ports.Qmp) in 30 s. $tail" }
            Start-Sleep -Milliseconds 300
        }
        $live = Get-LegAccelLive $Ports.Qmp
        if ($live -and $live -cne 'whpx') { throw "${What}: asked for -accel $accel, QEMU runs on '$live'. $tail" }
    } finally {
        if (-not $p.HasExited) {
            # QMP quit; this exact process when QMP does not take it.
            $asked = $false
            try { $null = Invoke-Qmp -Port $Ports.Qmp -Command 'quit' -AllowError -TimeoutMs 5000; $asked = $true } catch { }
            if (-not $asked -or -not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $null = $p.WaitForExit(10000) }
        }
        try { Add-LegPidLog $Config "ended xhleg-whpx-probe pid $($p.Id)" } catch { }
    }
    return $accel
}

# What of a start's QEMU stderr is a reason against the leg: all of it, but
# under WHPX the one warning QEMU 11.1 prints at every WHPX start of these
# machines, "warning: Ignoring request for interrupt vector 0" (read on
# development host A on 2026-11-11, on the probe and on every Windows 10
# start, each of which went on to pass). The file itself keeps every line.
# Returns the text left, '' when nothing is.
function Get-LegStderrReason {
    param([string]$Text, [string]$Accel)
    $keep = @()
    foreach ($l in @("$Text" -split "`r?`n")) {
        if (-not $l.Trim()) { continue }
        if ((Get-LegAccelKind $Accel) -ceq 'whpx' -and $l -cmatch ': warning: Ignoring request for interrupt vector 0\s*$') { continue }
        $keep += $l.TrimEnd()
    }
    return ($keep -join "`n")
}

# What stands between a leg's record and PASS, as reasons (none: PASS, with
# no reason from the starts themselves). The guest must have said END;
# every check the leg needs must be reported and every reported check
# must pass; each exit code must be one the leg allows; no host step may
# have failed.
# A leg with Devices needs its six controller checks, so a guest script
# that skipped the sequence cannot pass, and the remove's answer (done, or
# restart asked) must be on record, though neither answer is a reason.
# Its uninstall may end with exit 4 only as a pending restart: the guest
# read that in the installer's log (uninstall-pending) and ran the
# uninstall again after the next start (uninstall-again, required then
# and only then). A leg with TrustCert needs cert-trust and test-signing.
function Get-LegVerdictReasons {
    param($Def, $Rec, [bool]$Ended, [bool]$Audio, [bool]$Devices, [bool]$Trust)
    $why = @()
    if (-not $Ended) { $why += 'the guest never said END' }
    $need = @('driver-file', 'list-current', 'stick-seen', 'stick-copy', 'list-removed')
    if ($Audio) { $need += @('audio-play', 'urbs-malformed') }
    if ($Devices) { $need += @('ctrl-disable', 'ctrl-enable', 'stick-copy-enable', 'ctrl-remove', 'ctrl-rescan', 'stick-copy-rescan') }
    if ($Trust) { $need += @('cert-trust', 'test-signing') }
    if ($Def.ContainsKey('Checks')) { $need = @($Def.Checks) }
    foreach ($c in $need) {
        if (-not $Rec.checks.Contains($c)) { $why += "check $c not reported" }
    }
    foreach ($k in @($Rec.checks.Keys)) {
        if ($Rec.checks[$k] -notlike 'PASS*') { $why += "check $k $($Rec.checks[$k])" }
    }
    $un4 = ($Rec.exits.Contains('uninstall') -and [string]$Rec.exits['uninstall'] -eq '4')
    if ($Def.ContainsKey('Exits')) {
        foreach ($k in $Def.Exits.Keys) {
            $allowed = @($Def.Exits[$k] | ForEach-Object { [string]$_ })
            if ($k -eq 'uninstall-again' -and -not $Rec.exits.Contains($k) -and -not $un4) { continue }
            if (-not $Rec.exits.Contains($k)) { $why += "no exit code for $k" }
            elseif ($allowed -notcontains [string]$Rec.exits[$k]) { $why += "exit $k $($Rec.exits[$k]), allowed $($allowed -join '/')" }
        }
    }
    foreach ($s in $Rec.steps) { if ($s -match 'FAILED|still running|no BOOT line') { $why += "step: $s" } }
    if ($Devices) {
        if ($un4) {
            if (-not $Rec.checks.Contains('uninstall-pending')) { $why += 'check uninstall-pending not reported (the uninstall ended with exit 4)' }
            if (-not $Rec.exits.Contains('uninstall-again')) { $why += 'no exit code for uninstall-again (the uninstall ended with exit 4)' }
        }
        if ([string]$Rec.ctrlRemove -cnotin 'done', 'restart asked') { $why += 'the remove step did not say whether Windows asked for a restart' }
    }
    return , $why
}

# The line a leg's record and the matrix's table carry for the controller's
# remove: its answer and the accelerator it was given under. The answer is
# recorded under either accelerator and judged under neither, since TCG
# itself has given both (runs/run-49.md, "49.4"). Under WHPX it is not
# that step's reading: the step keeps a TCG reading, which only the same
# leg under -ForceTcg on the same package gives, so the line says that the
# reading is owed there and does not claim one.
function Get-LegRemoveNote {
    param([string]$Answer, [string]$Kind)
    if (-not $Answer) { $Answer = 'not read' }
    $t = "remove: $Answer under $Kind"
    if ($Kind -cne 'tcg') { $t += '; recorded only: this step keeps a TCG reading, which this leg with -ForceTcg on the same package gives' }
    return $t
}

function Get-LegHardwareHash {
    param($Guest, [string]$QemuVersion)
    $text = ((Get-LegHardwareArgs $Guest) -join "`n") + "`n" + $QemuVersion
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $h = $sha.ComputeHash([System.Text.Encoding]::ASCII.GetBytes($text))
    return ([BitConverter]::ToString($h) -replace '-', '').ToLowerInvariant()
}

function Get-LegQemuVersion {
    param($Config)
    $v = & $Config.QemuExe --version 2>$null | Select-Object -First 1
    if (-not $v) { throw "$($Config.QemuExe) --version said nothing" }
    return [string]$v
}

function Quote-LegArg {
    param([string]$A)
    if ($A -match '[\s"]') { return '"' + ($A -replace '"', '\"') + '"' }
    return $A
}

# Starts QEMU hidden and headless and returns its process. Every reset ends
# the process (-action reboot=shutdown): Windows 98 SE hangs on a warm restart,
# and on every guest a restart then becomes a cold start the host makes.
# Backends: arguments no saved state depends on, so not in the starting
# point's hash (an audio leg's -audiodev, whose device the host plugs later).
# Accel: the start's -accel value (Get-LegAccelArg); none is TCG's. A saved
# state is TCG's, and QEMU saves and loads none under WHPX ("State blocked
# due to missing dirty memory tracking support", read 2026-11-11), so
# -Incoming with any other accelerator is refused here. Once QMP is up QEMU
# is asked which accelerator it runs on, and a start on another than the
# one asked for is ended at once.
function Start-LegQemu {
    param(
        $Config, $Guest, [hashtable]$Ports,
        [string]$Disk, [string]$Iso, [string]$RunDir, [string]$Tag,
        [switch]$Incoming, [string]$Name = 'xhleg', [string[]]$Backends = @(), [string]$Accel = ''
    )
    if (-not $Accel) { $Accel = Get-LegAccelArg $Guest 'tcg' }
    $kind = Get-LegAccelKind $Accel
    if ($Incoming -and $kind -cne 'tcg') { throw "a saved state is restored under TCG only, not under -accel $Accel" }
    $serial = Join-Path $RunDir "serial-$Tag.log"
    $a = @('-name', $Name) + (Get-LegHardwareArgs $Guest -Accel $Accel) + @($Backends)
    $a += @('-display', 'none', '-vnc', "127.0.0.1:$($Ports.Vnc - 5900)",
        '-qmp', "tcp:127.0.0.1:$($Ports.Qmp),server=on,wait=off",
        '-action', 'reboot=shutdown',
        '-drive', "if=none,id=hd0,format=qcow2,file=$Disk",
        '-drive', (Get-LegMediumDrive $Guest $Iso),
        '-chardev', "file,id=dbg,path=$(Join-Path $RunDir "debugcon-$Tag.log")",
        '-serial', "file:$serial",
        '-boot', 'order=c')
    if ($Incoming) { $a += @('-S', '-incoming', "tcp:127.0.0.1:$($Ports.Mig)") }
    Assert-LegOneAccel $a $Accel
    foreach ($p in $Ports.Qmp, $Ports.Vnc, $Ports.Mig) {
        if (-not (Test-LegPortFree $p)) { throw "port $p is in use" }
    }
    $err = Join-Path $RunDir "qemu-$Tag.err"
    $line = ($a | ForEach-Object { Quote-LegArg $_ }) -join ' '
    Set-Content -LiteralPath (Join-Path $RunDir "cmdline-$Tag.txt") -Value "`"$($Config.QemuExe)`" $line" -Encoding ASCII
    # QEMU itself, no shell between: its stderr (an assertion, a refused
    # option) goes to a file, and its console window is hidden.
    $p = Start-Process -FilePath $Config.QemuExe -ArgumentList $line -PassThru -WindowStyle Hidden `
        -RedirectStandardError $err -RedirectStandardOutput (Join-Path $RunDir "qemu-$Tag.out")
    $deadline = (Get-Date).AddSeconds(30)
    # Reading the handle now keeps the exit code readable after the exit.
    $null = $p.Handle
    try {
        while (-not (Test-QmpUp $Ports.Qmp)) {
            if ($p.HasExited) { throw "QEMU exited at start: $(Get-Content -Raw -LiteralPath $err -ErrorAction SilentlyContinue)" }
            if ((Get-Date) -gt $deadline) { throw "QEMU's QMP did not come up on $($Ports.Qmp)" }
            Start-Sleep -Milliseconds 300
        }
        $live = Get-LegAccelLive $Ports.Qmp
        if ($live -and $live -cne $kind) { throw "asked for -accel $Accel, QEMU runs on '$live'" }
        Add-LegPidLog $Config "started $Name tag $Tag pid $($p.Id) qmp $($Ports.Qmp) accel $Accel"
    } catch {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $null = $p.WaitForExit(10000) }
        Add-LegPidLog $Config "start failed, pid $($p.Id) ended ($Name tag $Tag)"
        throw
    }
    return $p
}

# The writes the guest's disk has taken so far (query-blockstats, drive hd0);
# -1 when it cannot be read.
function Get-LegDiskWrites {
    param([int]$QmpPort)
    try {
        foreach ($d in @(Invoke-Qmp -Port $QmpPort -Command 'query-blockstats' -TimeoutMs 5000)) {
            if ($d.device -eq 'hd0') { return [int64]$d.stats.wr_operations }
        }
    } catch { }
    return -1
}

# Whether every processor of the guest is halted with interrupts off, in five
# samples a second apart: where an NT kernel ends its shut down on a machine
# it cannot switch off ("It is now safe to turn off your computer"). A
# shut down that hangs instead keeps a processor running, or interrupts on.
function Test-LegCpuHalted {
    param([int]$QmpPort)
    for ($i = 0; $i -lt 5; $i++) {
        try { $r = [string](Invoke-Hmp -Port $QmpPort -Line 'info registers -a') } catch { return $false }
        $cpus = @([regex]::Matches($r, 'EFL=([0-9a-fA-F]+)[^\n]*HLT=(\d)'))
        if ($cpus.Count -eq 0) { return $false }
        foreach ($m in $cpus) {
            if ($m.Groups[2].Value -ne '1') { return $false }
            if (([Convert]::ToInt64($m.Groups[1].Value, 16) -band 0x200) -ne 0) { return $false }
        }
        Start-Sleep -Seconds 1
    }
    return $true
}

# Waits for a guest's shut down to end QEMU. A guest marked HostPowerOff
# (Windows 2000 on a machine without ACPI, which stops at "It is now safe to
# turn off your computer") cannot switch the machine off: once its disk has
# taken no write for 30 s, at least 45 s after the shut down began, its
# screen is still and its processors are halted (Test-LegCpuHalted), the host
# quits QEMU over QMP. Returns $true when QEMU has
# ended.
function Wait-LegPowerOff {
    param($Config, $Guest, [hashtable]$Ports, $Process, [string]$Dir, [string]$Tag, [int]$Seconds = 300)
    if (-not ($Guest.ContainsKey('HostPowerOff') -and $Guest.HostPowerOff)) { return $Process.WaitForExit($Seconds * 1000) }
    $t0 = Get-Date
    $deadline = $t0.AddSeconds($Seconds)
    $last = -2; $since = Get-Date
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) { return $true }
        $w = Get-LegDiskWrites $Ports.Qmp
        if ($w -ne $last -or $w -lt 0) { $last = $w; $since = Get-Date }
        elseif (((Get-Date) - $since).TotalSeconds -ge 30 -and ((Get-Date) - $t0).TotalSeconds -ge 45 -and (Test-ScreenSettled -QmpPort $Ports.Qmp -Dir $Dir) -and (Test-LegCpuHalted $Ports.Qmp)) {
            try { Save-QmpScreen -Port $Ports.Qmp -Path (Join-Path $Dir "$Tag-poweroff.png") } catch { }
            try { $null = Invoke-Qmp -Port $Ports.Qmp -Command 'quit' -AllowError -TimeoutMs 5000 } catch { }
            $null = $Process.WaitForExit(15000)
            Add-LegPidLog $Config "quit pid $($Process.Id) at the guest's shut down (HostPowerOff, $Tag)"
            return $Process.HasExited
        }
        Start-Sleep -Seconds 5
    }
    return $Process.HasExited
}

# A guest whose leg medium is a disk (CdBus disk), not a CD.
function Test-LegDiskMedium {
    param($Guest)
    return ($Guest.ContainsKey('CdBus') -and $Guest.CdBus -eq 'disk')
}

# The medium's -drive: a CD image, or for a disk-medium guest the staged
# folder beside it (<image>.dir) as a read-only FAT16 disk.
function Get-LegMediumDrive {
    param($Guest, [string]$Iso)
    if (Test-LegDiskMedium $Guest) { return "if=none,id=cdd,format=raw,snapshot=on,file=fat:16:$(Get-LegMediumDir $Iso)" }
    return "if=none,id=cdd,format=raw,readonly=on,media=cdrom,file=$Iso"
}

function Get-LegMediumDir {
    param([string]$Iso)
    return "$Iso.dir"
}

function Test-LegPortFree {
    param([int]$Port)
    $l = New-Object System.Net.Sockets.TcpListener ([System.Net.IPAddress]::Loopback), $Port
    try { $l.Start(); return $true } catch { return $false } finally { try { $l.Stop() } catch { } }
}

function Add-LegPidLog {
    param($Config, [string]$Text)
    $f = Join-Path $Config.WorkDir 'pids.txt'
    Add-Content -LiteralPath $f -Value "$(Get-Date -Format s) $Text" -Encoding ASCII
}

# Ends a leg's QEMU: QMP quit, then, only if that fails, this exact process.
function Stop-LegQemu {
    param($Config, $Process, [int]$QmpPort, [string]$Why)
    if ($Process.HasExited) { return }
    try { $null = Invoke-Qmp -Port $QmpPort -Command 'quit' -AllowError -TimeoutMs 5000 } catch { }
    if (-not $Process.WaitForExit(15000)) { Stop-Process -Id $Process.Id -Force }
    Add-LegPidLog $Config "stopped pid $($Process.Id) ($Why)"
}

# The machine state over the migration socket into a file. QEMU 11.1 on
# Windows cannot migrate to a file: URI ("Failed to set FD nonblocking"), so
# the host listens and writes the stream itself. QEMU keeps the socket open
# after the migration completes, so the copy ends when QEMU says completed and
# every byte it counted has arrived.
function Save-LegState {
    param([hashtable]$Ports, [string]$Path)
    $l = New-Object System.Net.Sockets.TcpListener ([System.Net.IPAddress]::Loopback), $Ports.Mig
    $l.Start()
    $f = $null; $c = $null
    try {
        $null = Invoke-Qmp -Port $Ports.Qmp -Command 'stop'
        $null = Invoke-Qmp -Port $Ports.Qmp -Command 'migrate' -Arguments @{ uri = "tcp:127.0.0.1:$($Ports.Mig)" }
        $iar = $l.BeginAcceptTcpClient($null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(30000)) { throw 'QEMU did not connect to the migration socket' }
        $c = $l.EndAcceptTcpClient($iar)
        $f = [System.IO.File]::Create($Path)
        $t = $c.GetStream().CopyToAsync($f)
        $deadline = (Get-Date).AddMinutes(10)
        while ($true) {
            $done = $t.Wait(500)
            $m = Invoke-Qmp -Port $Ports.Qmp -Command 'query-migrate'
            if ($m.status -eq 'failed' -or $m.status -eq 'cancelled') { throw "migration $($m.status): $($m.'error-desc')" }
            if ($m.status -eq 'completed') {
                $f.Flush()
                if ($f.Length -ge [int64]$m.ram.transferred) { break }
                if ($done) { throw "the stream ended at $($f.Length) bytes of $($m.ram.transferred)" }
            } elseif ($done) { throw "the stream ended with the migration $($m.status)" }
            if ((Get-Date) -gt $deadline) { throw 'migration did not complete in 10 minutes' }
        }
        return [int64]$f.Length
    } finally {
        if ($f) { $f.Close() }
        if ($c) { $c.Close() }
        $l.Stop()
    }
}

# Feeds a saved state to a QEMU started with -Incoming, and waits until the
# load is done (the machine paused, under -S).
function Restore-LegState {
    param([hashtable]$Ports, [string]$Path)
    $c = New-Object System.Net.Sockets.TcpClient
    $deadline = (Get-Date).AddSeconds(30)
    while ($true) {
        try { $c.Connect('127.0.0.1', $Ports.Mig); break } catch {
            if ((Get-Date) -gt $deadline) { throw "no incoming migration socket on $($Ports.Mig)" }
            Start-Sleep -Milliseconds 200
        }
    }
    # A write that QEMU stops taking times out rather than hanging the leg.
    $c.SendTimeout = 60000
    $f = [System.IO.File]::OpenRead($Path)
    try { $f.CopyTo($c.GetStream()) } finally { $f.Close(); $c.Close() }
    $deadline = (Get-Date).AddSeconds(120)
    while ($true) {
        $s = Get-QmpRunState $Ports.Qmp
        if ($s -eq 'paused' -or $s -eq 'running') { return }
        if ($s -eq 'postmigrate' -or $s -eq 'internal-error' -or $s -eq 'shutdown') { throw "state load ended in $s" }
        if ((Get-Date) -gt $deadline) { throw "state load not finished (status '$s')" }
        Start-Sleep -Milliseconds 300
    }
}

function Set-LegCd {
    param([hashtable]$Ports, [string]$Iso)
    $null = Invoke-Qmp -Port $Ports.Qmp -Command 'blockdev-change-medium' -Arguments @{
        id = 'cd'; filename = $Iso; format = 'raw'; 'read-only-mode' = 'read-only'
    }
}

# The guest's lines on COM1 that start "XHLEG ". Returns every such line in
# the file so far; callers keep their own position.
function Get-LegSerialLines {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return , @() }
    try {
        $fs = New-Object System.IO.FileStream($Path, 'Open', 'Read', 'ReadWrite')
        try {
            $r = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::ASCII)
            $text = $r.ReadToEnd()
        } finally { $fs.Close() }
    } catch { return , @() }
    # Only complete lines: one still being written is read on a later call.
    $cut = $text.LastIndexOf("`n")
    if ($cut -lt 0) { return , @() }
    $text = $text.Substring(0, $cut)
    return , @($text -split "`r?`n" | Where-Object { $_ -like 'XHLEG *' } | ForEach-Object { $_.TrimEnd() })
}

# The vCPUs one QEMU command line asks for: -smp N, -smp cpus=N, maxcpus=N
# when cpus is not given, or the product of its topology (sockets, dies, clusters, cores, threads); none
# is one. A -smp this cannot read returns a count no budget admits beside,
# so the matrix waits rather than guess.
function Get-QemuSmpCount {
    param([string]$CommandLine)
    if ($CommandLine -notmatch '-smp\s+"?([^\s"]+)') { return 1 }
    $spec = $Matches[1]
    if ($spec -match '^(\d+)(,|$)') { return [int]$Matches[1] }
    if ($spec -match '(^|,)cpus=(\d+)') { return [int]$Matches[2] }
    # QEMU starts maxcpus vCPUs when cpus is omitted.
    if ($spec -match '(^|,)maxcpus=(\d+)') { return [int]$Matches[2] }
    $n = 1; $any = $false
    foreach ($k in 'sockets', 'dies', 'clusters', 'cores', 'threads') {
        if ($spec -match "(^|,)$k=(\d+)") { $n *= [int]$Matches[2]; $any = $true }
    }
    if ($any) { return $n }
    return 100000
}

# vCPUs of every QEMU on the host, ours and others' (the owner's budget is
# the host's sum, feedback-vm-core-budget). A QEMU with no -smp counts one.
function Get-HostQemuVcpus {
    param([int[]]$ExcludeQmpPorts = @())
    $sum = 0
    foreach ($q in @(Get-CimInstance Win32_Process -Filter "Name like 'qemu-system%'")) {
        $mine = $false
        foreach ($p in $ExcludeQmpPorts) { if ($q.CommandLine -like "*tcp:127.0.0.1:$p,server*") { $mine = $true } }
        if ($mine) { continue }
        $sum += Get-QemuSmpCount $q.CommandLine
    }
    return $sum
}

function Test-ScreenSettled {
    param([int]$QmpPort, [string]$Dir, [int]$GapSeconds = 5)
    $a = Join-Path $Dir 'settle-a.png'; $b = Join-Path $Dir 'settle-b.png'
    try {
        Save-QmpScreen -Port $QmpPort -Path $a
        Start-Sleep -Seconds $GapSeconds
        Save-QmpScreen -Port $QmpPort -Path $b
        Start-Sleep -Milliseconds 500
        return ((Get-FileHash -LiteralPath $a).Hash -eq (Get-FileHash -LiteralPath $b).Hash)
    } catch { return $false }
}
