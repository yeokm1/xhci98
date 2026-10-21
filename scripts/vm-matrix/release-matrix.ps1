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
lists the Manual legs (the installer's window, one a system) as owed.

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
    [switch]$Cold
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
$todo = New-Object System.Collections.ArrayList
$manual = @()
foreach ($l in $legSet.Legs) {
    if ($Only.Count -and $Only -notcontains $l.Id) { continue }
    if ($l.ContainsKey('Manual') -and $l.Manual) { $manual += $l; continue }
    $g = Get-LegGuest $cfg $l.Guest
    $null = Get-LegStartPoint $cfg $g $qver
    if ($MaxVcpu -gt 0 -and [int]$g.Smp -gt $MaxVcpu) { throw "leg $($l.Id) needs $($g.Smp) vCPUs, over MaxVcpu $MaxVcpu" }
    if ([int]$g.Smp -gt $logical) { throw "leg $($l.Id) needs $($g.Smp) vCPUs, over the host's $logical" }
    [void]$todo.Add(@{ Def = $l; Smp = [int]$g.Smp })
}
if ($todo.Count -eq 0 -and $manual.Count -eq 0) { throw 'no leg selected' }

$runRoot = Join-Path $cfg.WorkDir ("runs\" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $runRoot | Out-Null
$slots = [math]::Floor($cfg.PortCount / 3)
$free = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $slots; $i++) { [void]$free.Add($i) }
$running = @()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
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
    # A leg's QEMU takes a few seconds to show in the process list the
    # budget reads, so one leg is started per pass, then a pause.
    if ($started) { Start-Sleep -Seconds 20 } else { Start-Sleep -Seconds 5 }
}

$rows = @()
foreach ($l in $legSet.Legs) {
    if ($Only.Count -and $Only -notcontains $l.Id) { continue }
    $res = Join-Path (Join-Path $runRoot $l.Id) 'result.json'
    if ($l.ContainsKey('Manual') -and $l.Manual) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Verdict = 'OWED (manual)'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "run-leg.ps1 -Leg $($l.Id) with an operator at VNC" }
        continue
    }
    if (-not (Test-Path -LiteralPath $res)) {
        $rows += [pscustomobject]@{ Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Verdict = 'ERROR'; Minutes = ''; Boots = ''; Restore = ''; Exits = ''; Notes = "no result.json; see $($l.Id)\run-leg.log" }
        continue
    }
    $j = Get-Content -Raw -LiteralPath $res | ConvertFrom-Json
    $ex = @($j.exits.PSObject.Properties | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '
    $rs = @($j.boots | Where-Object { $_.restore } | ForEach-Object { $_.restoreSeconds }) -join ''
    $rows += [pscustomobject]@{
        Leg = $l.Id; Guest = $l.Guest; Mode = $l.Mode; Verdict = $j.verdict
        Minutes = [math]::Round($j.seconds / 60, 1); Boots = @($j.boots).Count; Restore = $rs; Exits = $ex
        Notes = (@($j.reasons) -join '; ')
    }
}
$md = @("# Release matrix $(Split-Path -Leaf $runRoot)", '', "Package: $Package", "QEMU: $qver", "Wall time: $([math]::Round($sw.Elapsed.TotalMinutes, 1)) min", '',
    '| Leg | Guest | Mode | Verdict | Minutes | Starts | Restore s | Exit codes | Notes |', '|---|---|---|---|---|---|---|---|---|')
foreach ($r in $rows) { $md += "| $($r.Leg) | $($r.Guest) | $($r.Mode) | $($r.Verdict) | $($r.Minutes) | $($r.Boots) | $($r.Restore) | $($r.Exits) | $($r.Notes) |" }
Set-Content -LiteralPath (Join-Path $runRoot 'results.md') -Value $md -Encoding ASCII
$rows | Export-Csv -LiteralPath (Join-Path $runRoot 'results.csv') -NoTypeInformation -Encoding ASCII
$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
Write-Host "results: $(Join-Path $runRoot 'results.md')"
if (@($rows | Where-Object { $_.Verdict -ne 'PASS' -and $_.Verdict -notlike 'OWED*' }).Count) { exit 1 }
exit 0
