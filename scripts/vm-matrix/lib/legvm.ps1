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
function Get-LegHardwareArgs {
    param($Guest)
    $accel = 'tcg'
    if ([int]$Guest.Smp -gt 1) { $accel = 'tcg,thread=multi' }
    $machine = 'pc'
    if ($Guest.ContainsKey('Machine') -and $Guest.Machine) { $machine = $Guest.Machine }
    $vga = 'std'
    if ($Guest.ContainsKey('Vga') -and $Guest.Vga) { $vga = $Guest.Vga }
    $a = @('-machine', $machine, '-accel', $accel, '-cpu', $Guest.Cpu, '-smp', [string]$Guest.Smp,
        '-m', [string]$Guest.Memory, '-vga', $vga, '-nic', 'none', '-rtc', 'base=localtime',
        '-device', 'ide-hd,drive=hd0,bus=ide.0,unit=0',
        '-device', 'ide-cd,id=cd,drive=cdd,bus=ide.1,unit=0',
        '-device', 'isa-debugcon,iobase=0xe9,chardev=dbg',
        '-device', 'qemu-xhci,id=xh,p2=4,p3=4')
    if ($Guest.ContainsKey('Extra') -and $Guest.Extra) { $a += @($Guest.Extra) }
    return , $a
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
function Start-LegQemu {
    param(
        $Config, $Guest, [hashtable]$Ports,
        [string]$Disk, [string]$Iso, [string]$RunDir, [string]$Tag,
        [switch]$Incoming, [string]$Name = 'xhleg'
    )
    $serial = Join-Path $RunDir "serial-$Tag.log"
    $a = @('-name', $Name) + (Get-LegHardwareArgs $Guest)
    $a += @('-display', 'none', '-vnc', "127.0.0.1:$($Ports.Vnc - 5900)",
        '-qmp', "tcp:127.0.0.1:$($Ports.Qmp),server=on,wait=off",
        '-action', 'reboot=shutdown',
        '-drive', "if=none,id=hd0,format=qcow2,file=$Disk",
        '-drive', "if=none,id=cdd,format=raw,readonly=on,media=cdrom,file=$Iso",
        '-chardev', "file,id=dbg,path=$(Join-Path $RunDir "debugcon-$Tag.log")",
        '-serial', "file:$serial",
        '-boot', 'order=c')
    if ($Incoming) { $a += @('-S', '-incoming', "tcp:127.0.0.1:$($Ports.Mig)") }
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
        Add-LegPidLog $Config "started $Name tag $Tag pid $($p.Id) qmp $($Ports.Qmp)"
    } catch {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $null = $p.WaitForExit(10000) }
        Add-LegPidLog $Config "start failed, pid $($p.Id) ended ($Name tag $Tag)"
        throw
    }
    return $p
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

# vCPUs of every QEMU on the host, ours and others' (the owner's budget is
# the host's sum, feedback-vm-core-budget). A QEMU with no -smp counts one.
function Get-HostQemuVcpus {
    param([int[]]$ExcludeQmpPorts = @())
    $sum = 0
    foreach ($q in @(Get-CimInstance Win32_Process -Filter "Name like 'qemu-system%'")) {
        $mine = $false
        foreach ($p in $ExcludeQmpPorts) { if ($q.CommandLine -like "*tcp:127.0.0.1:$p,server*") { $mine = $true } }
        if ($mine) { continue }
        # -smp N or -smp cpus=N; a -smp this cannot read counts as four, so
        # an unread topology errs towards the budget.
        if ($q.CommandLine -match '-smp\s+"?(?:cpus=)?(\d+)') { $sum += [int]$Matches[1] }
        elseif ($q.CommandLine -match '-smp\b') { $sum += 4 }
        else { $sum += 1 }
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
