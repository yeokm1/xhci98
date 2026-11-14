<#
.SYNOPSIS
The release matrix: every standard leg of release-legs.psd1 started in
parallel within the vCPU budget, and one results table gathered from them.
build-and-test.md, "Faster emulator legs".

.DESCRIPTION
Checks every leg's starting point first (make-start.ps1 makes them) and
starts nothing if one is missing or stale. Then starts run-leg.ps1 per leg,
each in its own hidden PowerShell and its own port slot, as many at once as
the slots and the budget allow: the matrix's own vCPUs at most MaxVcpu
(configuration or -MaxVcpu; 0 = no cap of its own) and every QEMU's on the
host together at most the host's logical processors. Writes results.md and
results.csv in the run's directory, each leg's evidence beside them, and
lists the Manual legs (a window leg with no WindowKeys is one) as owed,
and a leg whose guest the configuration does not have as not configured
(-Only naming such a leg is an error).

When no leg is running and none fits the budget (other QEMUs on the host,
or a leg larger than the budget), it says so once and waits at most
-WaitMinutes (default 60), then stops and lists the legs not started.

A leg runs on the accelerator release-legs.psd1 gives it: TCG, or WHPX for
the two Windows 10 legs (roadmap-usb4 49.4). Before any leg starts, WHPX is
probed once for each guest a selected WHPX leg runs on; where it cannot run
that guest the matrix stops with QEMU's own message and starts nothing: a
WHPX leg never falls back to TCG by itself. -ForceTcg runs every leg under
TCG, the periodic cross-check of the WHPX legs. The table names each leg's
accelerator.

The package is the unpacked download (INSTALL.BAT at its top); it goes onto
each leg's CD and is never run on the host.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$Config = '',
    [string]$Legs = '',
    [string[]]$Only = @(),
    [int]$MaxVcpu = -1,
    [int]$WaitMinutes = 60,
    [switch]$Cold,
    [switch]$ForceTcg
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\legvm.ps1')
. (Join-Path $PSScriptRoot 'lib\leghost.ps1')

$cfgPath = $Config
$cfg = Read-LegConfig $Config
if (-not $Legs) { $Legs = Join-Path $PSScriptRoot 'release-legs.psd1' }
$Legs = Resolve-RepoPath $Legs
$legSet = Import-PowerShellDataFile -LiteralPath $Legs
if ($MaxVcpu -lt 0) { $MaxVcpu = 0; if ($cfg.ContainsKey('MaxVcpu')) { $MaxVcpu = [int]$cfg.MaxVcpu } }
$logical = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
if (-not (Test-Path -LiteralPath (Join-Path $Package 'INSTALL.BAT'))) { throw "$Package has no INSTALL.BAT at its top: give the unpacked download" }
$Package = (Resolve-Path -LiteralPath $Package).Path

$qver = Get-LegQemuVersion $cfg
function Test-LegManual {
    param($L)
    if ($L.ContainsKey('Manual') -and $L.Manual) { return $true }
    return ($L.Mode -eq 'window' -and -not ($L.ContainsKey('WindowKeys') -and @($L.WindowKeys).Count -gt 0))
}
function Test-LegInPackage {
    param($L)
    if (-not ($L.ContainsKey('Needs') -and $L.Needs)) { return $true }
    return (Test-Path -LiteralPath (Join-Path $Package $L.Needs))
}
function Test-LegConfigured {
    param($L)
    return [bool](@($cfg.Guests | Where-Object { $_.Id -eq $L.Guest }).Count)
}
# The accelerator a leg's row names: its -accel value, and that it was forced.
function Get-LegAccelText {
    param($L)
    $a = Resolve-LegAccel $L ([bool]$ForceTcg)
    $t = Get-LegAccelArg (Get-LegGuest $cfg $L.Guest) $a.Use
    if ($a.Forced) { $t += ' (forced)' }
    return $t
}
$todo = New-Object System.Collections.ArrayList
$manual = @()
$whpxGuests = [ordered]@{}
$unconfigured = @()
foreach ($l in $legSet.Legs) {
    if ($Only.Count -and $Only -notcontains $l.Id) { continue }
    if (-not (Test-LegConfigured $l)) {
        if ($Only.Count) { throw "leg $($l.Id): guest $($l.Guest) is not in the configuration" }
        $unconfigured += $l; continue
    }
    if (-not (Test-LegInPackage $l)) {
        if ($Only.Count) { throw "leg $($l.Id): the package has no $($l.Needs)" }
        continue
    }
    if (Test-LegManual $l) { $manual += $l; continue }
    $g = Get-LegGuest $cfg $l.Guest
    $null = Get-LegStartPoint $cfg $g $qver
    if ($l.ContainsKey('Audio') -and $l.Audio) { $null = Get-LegWavChk }
    if ((Resolve-LegAccel $l ([bool]$ForceTcg)).Use -eq 'whpx' -and -not $whpxGuests.Contains($g.Id)) { $whpxGuests[$g.Id] = $l.Id }
    if ($MaxVcpu -gt 0 -and [int]$g.Smp -gt $MaxVcpu) { throw "leg $($l.Id) needs $($g.Smp) vCPUs, over MaxVcpu $MaxVcpu" }
    if ([int]$g.Smp -gt $logical) { throw "leg $($l.Id) needs $($g.Smp) vCPUs, over the host's $logical" }
    [void]$todo.Add(@{ Def = $l; Smp = [int]$g.Smp })
}
if ($todo.Count -eq 0 -and $manual.Count -eq 0) { throw 'no leg selected (or no leg has a configured guest)' }

$runRoot = Join-Path $cfg.WorkDir ("runs\" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $runRoot | Out-Null
# WHPX, probed once for each guest a selected WHPX leg runs on, in slot 0
# (no leg runs yet). A refusal stops the matrix here: nothing was started.
foreach ($gid in $whpxGuests.Keys) {
    $null = Assert-LegWhpx -Config $cfg -Guest (Get-LegGuest $cfg $gid) -Ports (Get-LegPorts $cfg 0) -What "leg $($whpxGuests[$gid]) (guest $gid)" -Dir (Join-Path $runRoot "whpx-probe-$gid")
}
$slots = [math]::Floor($cfg.PortCount / 3)
$free = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $slots; $i++) { [void]$free.Add($i) }
$running = @()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$idleSince = $null
$notStarted = @()
Write-Host "release matrix: $($todo.Count) legs, $slots slots, MaxVcpu $MaxVcpu, host $logical logical processors; $runRoot"

while ($todo.Count -gt 0 -or $running.Count -gt 0) {
    foreach ($r in @($running)) {
        if ($r.Proc.HasExited) {
            [void]$free.Add($r.Slot)
            $running = @($running | Where-Object { $_ -ne $r })
            Write-Host ("{0,7:N0} s  {1} finished (exit {2})" -f $sw.Elapsed.TotalSeconds, $r.Id, $r.Proc.ExitCode)
        }
    }
    $started = $false
    $external = 0
    if ($todo.Count -gt 0 -and $free.Count -gt 0) {
        # A running leg holds its vCPUs for its whole life (between cold
        # starts and while its CD is built, too); other QEMUs on the host are
        # counted as they run, ours left out of that count by their QMP ports.
        $own = 0; $ownPorts = @()
        foreach ($r in $running) { $own += $r.Smp; $ownPorts += (Get-LegPorts $cfg $r.Slot).Qmp }
        $external = Get-HostQemuVcpus -ExcludeQmpPorts $ownPorts
        foreach ($t in @($todo)) {
            if ($MaxVcpu -gt 0 -and $own + $t.Smp -gt $MaxVcpu) { continue }
            if ($external + $own + $t.Smp -gt $logical) { continue }
            $slot = $free[0]; $free.RemoveAt(0)
            $id = $t.Def.Id
            $dir = Join-Path $runRoot $id
            New-Item -ItemType Directory -Force $dir | Out-Null
            $la = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Quote-LegArg (Join-Path $PSScriptRoot 'run-leg.ps1')),
                '-Leg', $id, '-Package', (Quote-LegArg $Package), '-Legs', (Quote-LegArg $Legs), '-RunDir', (Quote-LegArg $dir), '-Slot', $slot)
            if ($cfgPath) { $la += @('-Config', (Quote-LegArg (Resolve-RepoPath $cfgPath))) }
            if ($Cold) { $la += '-Cold' }
            if ($ForceTcg) { $la += '-ForceTcg' }
            $p = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') `
                -ArgumentList ($la -join ' ') -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput (Join-Path $dir 'run-leg.log') -RedirectStandardError (Join-Path $dir 'run-leg.err')
            $running += @{ Id = $id; Proc = $p; Slot = $slot; Smp = $t.Smp }
            $todo.Remove($t)
            Write-Host ("{0,7:N0} s  {1} started in slot {2}" -f $sw.Elapsed.TotalSeconds, $id, $slot)
            $started = $true
            break
        }
    }
    # Nothing running and nothing started: only another QEMU's vCPUs or a
    # leg larger than the budget stand in the way, and nothing of the
    # matrix's own will free them. Said once, and waited for a bounded time.
    if (-not $started -and $todo.Count -gt 0 -and $running.Count -eq 0) {
        if (-not $idleSince) {
            $idleSince = Get-Date
            $need = (@($todo | ForEach-Object { $_.Smp }) | Measure-Object -Minimum).Minimum
            Write-Host ("{0,7:N0} s  waiting: no leg fits the budget (the smallest needs {1} vCPUs; other QEMUs hold {2} of {3} logical processors; MaxVcpu {4}); at most {5} min" -f $sw.Elapsed.TotalSeconds, $need, $external, $logical, $MaxVcpu, $WaitMinutes)
        } elseif (((Get-Date) - $idleSince).TotalMinutes -ge $WaitMinutes) {
            Write-Host ("{0,7:N0} s  stopped: no leg fitted the budget for {1} min; not started: {2}" -f $sw.Elapsed.TotalSeconds, $WaitMinutes, ((@($todo | ForEach-Object { $_.Def.Id })) -join ', '))
            $notStarted = @($todo | ForEach-Object { $_.Def.Id })
            break
        }
    } else {
        $idleSince = $null
    }
    # A leg's QEMU takes a few seconds to show in the process list the
    # budget reads, so one leg is started per pass, then a pause.
    if ($started) { Start-Sleep -Seconds 20 } else { Start-Sleep -Seconds 5 }
}

$rows = @()
foreach ($l in $legSet.Legs) {
    if ($Only.Count -and $Only -notcontains $l.Id) { continue }
    $res = Join-Path (Join-Path $runRoot $l.Id) 'result.json'
    if (-not (Test-LegConfigured $l)) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Accel = ''; Verdict = 'NOT CONFIGURED'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "no guest $($l.Guest) in the configuration" }
        continue
    }
    if (-not (Test-LegInPackage $l)) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Accel = (Get-LegAccelText $l); Verdict = 'NOT IN PACKAGE'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "the package has no $($l.Needs)" }
        continue
    }
    if (Test-LegManual $l) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Accel = (Get-LegAccelText $l); Verdict = 'OWED (manual)'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "run-leg.ps1 -Leg $($l.Id) with an operator at VNC" }
        continue
    }
    if ($notStarted -contains $l.Id) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Accel = (Get-LegAccelText $l); Verdict = 'NOT STARTED'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "no room in the vCPU budget for $WaitMinutes min" }
        continue
    }
    if (-not (Test-Path -LiteralPath $res)) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Accel = (Get-LegAccelText $l); Verdict = 'ERROR'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "no result.json; see $($l.Id)\run-leg.log" }
        continue
    }
    $j = Get-Content -Raw -LiteralPath $res | ConvertFrom-Json
    $ex = @($j.exits.PSObject.Properties | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '
    $rs = @($j.boots | Where-Object { $_.restore } | ForEach-Object { $_.restoreSeconds }) -join ''
    # The accelerator is the one the leg's own record names, never the one
    # the matrix expected.
    $ja = [string]$j.accel
    if ($j.accelForced) { $ja += ' (forced)' }
    $notes = @($j.reasons)
    if ($j.PSObject.Properties['ctrlRemoveNote'] -and $j.ctrlRemoveNote) { $notes += [string]$j.ctrlRemoveNote }
    $rows += [pscustomobject]@{
        Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Accel = $ja; Verdict = $j.verdict
        Minutes = [math]::Round($j.seconds / 60, 1); Boots = @($j.boots).Count; Restore = $rs; Exits = $ex
        Notes = ($notes -join '; ')
    }
}
$md = @("# Release matrix $(Split-Path -Leaf $runRoot)", '', "Package: $Package", "QEMU: $qver", "Wall time: $([math]::Round($sw.Elapsed.TotalMinutes, 1)) min", '',
    '| Leg | Guest | Mode | Accel | Verdict | Minutes | Starts | Restore s | Exit codes | Notes |', '|---|---|---|---|---|---|---|---|---|---|')
foreach ($r in $rows) { $md += "| $($r.Leg) | $($r.Guest) | $($r.Mode) | $($r.Accel) | $($r.Verdict) | $($r.Minutes) | $($r.Boots) | $($r.Restore) | $($r.Exits) | $($r.Notes) |" }
Set-Content -LiteralPath (Join-Path $runRoot 'results.md') -Value $md -Encoding ASCII
$rows | Export-Csv -LiteralPath (Join-Path $runRoot 'results.csv') -NoTypeInformation -Encoding ASCII
$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
Write-Host "results: $(Join-Path $runRoot 'results.md')"
if (@($rows | Where-Object { $_.Verdict -ne 'PASS' -and $_.Verdict -notlike 'OWED*' -and $_.Verdict -ne 'NOT CONFIGURED' -and $_.Verdict -ne 'NOT IN PACKAGE' }).Count) { exit 1 }
exit 0
