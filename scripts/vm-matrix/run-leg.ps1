<#
.SYNOPSIS
One release-matrix leg: a guest restored from its starting point, the
package installed from a CD by the guest's own leg script, the results read
over COM1. build-and-test.md, "Faster emulator legs", is the design.

.DESCRIPTION
The leg's CD carries the package under PKG\, the guest's leg script as
XHLEG.BAT (98 SE, ME) or XHLEG.CMD (2000 to 7), and a 4 MiB file for the
stick's copy check. The guest's poller finds the CD and runs the script, one
phase per Windows start; every phase ends by switching the machine off and
the host starts it again cold, so 98 SE never restarts warm. The host takes
the steps a guest cannot (release-legs.psd1 HostSteps: a 98 SE hardware
wizard), plugs the stick when asked, and gives the verdict: PASS when the
guest says END, every check it names passed, each exit code is one the leg
allows, and no step failed.

-Cold boots the first phase from the starting point's disk instead of
restoring its saved state: the same leg, timed the way legs ran before the
starting points (a cold start to the desktop first).

-Window is the leg kept for the installer's window: the guest is restored with
the CD in and VNC on 127.0.0.1, the operator runs SETUP.EXE and shuts Windows
down, and the host then runs the same checks as the console leg. Nothing here
opens a viewer.

The package is never run on the host: the host only copies it into the CD.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Leg,
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$Config = '',
    [string]$Legs = '',
    [string]$RunDir = '',
    [int]$Slot = 0,
    [switch]$Cold,
    [int]$LegMinutes = 0
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\legvm.ps1')
. (Join-Path $PSScriptRoot 'lib\leghost.ps1')

$cfg = Read-LegConfig $Config
if (-not $Legs) { $Legs = Join-Path $PSScriptRoot 'release-legs.psd1' }
$legSet = Import-PowerShellDataFile -LiteralPath (Resolve-RepoPath $Legs)
$def = $null
foreach ($l in $legSet.Legs) { if ($l.Id -eq $Leg) { $def = $l } }
if (-not $def) { throw "no leg '$Leg' in $Legs" }
$guest = Get-LegGuest $cfg $def.Guest
$mode = $def.Mode
if ($mode -notin 'console', 'window') { throw "leg ${Leg}: Mode is console or window" }
if (-not (Test-Path -LiteralPath (Join-Path $Package 'INSTALL.BAT'))) { throw "$Package has no INSTALL.BAT at its top: give the unpacked download" }
if (-not $RunDir) { $RunDir = Join-Path $cfg.WorkDir ("runs\{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Leg) }
New-Item -ItemType Directory -Force $RunDir | Out-Null
$ports = Get-LegPorts $cfg $Slot
$qver = Get-LegQemuVersion $cfg
$start = Get-LegStartPoint $cfg $guest $qver
if (-not $cfg.ContainsKey('StickImage') -or -not (Test-Path -LiteralPath (Resolve-RepoPath $cfg.StickImage))) {
    throw 'StickImage: a qcow2 USB stick image with TESTDATA.BIN at its root'
}
$stickBase = Resolve-RepoPath $cfg.StickImage
if ($LegMinutes -le 0) { $LegMinutes = 90; if ($def.ContainsKey('Minutes')) { $LegMinutes = [int]$def.Minutes } }
$stall = 900; if ($def.ContainsKey('StallSeconds')) { $stall = [int]$def.StallSeconds }
$hostSteps = @{}; if ($def.ContainsKey('HostSteps')) { $hostSteps = $def.HostSteps }

$rec = [ordered]@{
    leg = $Leg; guest = $guest.Id; mode = $mode; cold = [bool]$Cold; package = (Resolve-Path -LiteralPath $Package).Path
    qemu = $qver; started = (Get-Date -Format s); boots = @(); steps = @(); checks = [ordered]@{}; exits = [ordered]@{}
    verdict = ''; reasons = @(); seconds = 0; pausedSeconds = 0
}
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$legDeadline = (Get-Date).AddMinutes($LegMinutes)

# The CD.
$cd = Join-Path $RunDir 'cd'
Remove-LegTree $cd
if (Test-Path -LiteralPath $cd) { throw "the staging directory $cd could not be emptied; give a new -RunDir" }
New-Item -ItemType Directory -Force $cd, (Join-Path $cd 'XHDATA') | Out-Null
$legDir = Join-Path $PSScriptRoot 'leg'
if ($guest.Family -eq '9x') {
    Copy-Item -LiteralPath (Join-Path $legDir 'LEG9X.BAT') -Destination (Join-Path $cd 'XHLEG.BAT')
    foreach ($f in 'XHEXIT.BAT', 'XHSEND.BAT') { Copy-Item -LiteralPath (Join-Path $legDir $f) -Destination $cd }
    [IO.File]::WriteAllText((Join-Path $cd 'XHMODE.BAT'), "@echo off`r`nset XHMODE=$mode`r`n", [Text.Encoding]::ASCII)
} else {
    Copy-Item -LiteralPath (Join-Path $legDir 'LEGNT.CMD') -Destination (Join-Path $cd 'XHLEG.CMD')
    [IO.File]::WriteAllText((Join-Path $cd 'XHMODE.CMD'), "@echo off`r`nset `"XHMODE=$mode`"`r`n", [Text.Encoding]::ASCII)
}
Copy-Item -LiteralPath $Package -Destination (Join-Path $cd 'PKG') -Recurse
$rnd = New-Object byte[] (4MB)
(New-Object System.Random 4312).NextBytes($rnd)
[IO.File]::WriteAllBytes((Join-Path $cd 'XHDATA\RAND.BIN'), $rnd)
$legIso = Join-Path $RunDir 'leg.iso'
New-LegIso -SourceDir $cd -Path $legIso -Label 'XHLEG'
Remove-LegTree $cd

$disk = Join-Path $RunDir 'disk.qcow2'
New-LegOverlay $cfg $start.Disk $disk

$ended = $false
$stickN = 0
$boot = 0
$maxBoots = 6
$failed = $false
$pendingStep = ''

# The guest's new complete lines since the last call, acted on in order.
function Read-LegLines {
    param([string]$Serial, $Proc, [string]$Tag)
    $lines = Get-LegSerialLines $Serial
    if ($lines.Count -le $script:nLines) { return $false }
    foreach ($ln in $lines[$script:nLines..($lines.Count - 1)]) {
        if ($ln -match '^XHLEG BOOT' -and -not $script:b.Contains('bootLineSecond')) { $script:b.bootLineSecond = [math]::Round(((Get-Date) - $script:t0).TotalSeconds, 1) }
        # A phase seen now schedules the host step for the next start, once.
        $ph = ''
        if ($ln -match '^XHLEG PHASE (\S+)') { $ph = $Matches[1] }
        if ($ln -match '^XHLEG OPERATOR') { $ph = '1' }
        if ($ph) { $script:pendingStep = ''; if ($hostSteps.ContainsKey("AfterPhase$ph")) { $script:pendingStep = $hostSteps["AfterPhase$ph"] } }
        if ($ln -match '^XHLEG POWEROFF') { $script:sawPowerOff = $true }
        if ($ln -match '^XHLEG CHECK (\S+) (PASS|FAIL)(.*)$') { $rec.checks[$Matches[1]] = ($Matches[2] + $Matches[3]).Trim() }
        if ($ln -match '^XHLEG EXIT (\S+) (\S+)$') { $rec.exits[$Matches[1]] = $Matches[2] }
        if ($ln -match '^XHLEG END') { $script:ended = $true; if (-not $script:endAt) { $script:endAt = Get-Date } }
        if (-not $Proc -or $Proc.HasExited) { continue }
        if ($ln -match '^XHLEG WANT plug-stick') {
            $script:stickN++
            $so = Join-Path $RunDir "stick-$($script:stickN).qcow2"
            try {
                New-LegOverlay $cfg $stickBase $so
                Add-LegStick $cfg $ports.Qmp $so
                $rec.steps += "boot $($script:boot) stick plugged"
                if ($def.ContainsKey('PlugStep')) {
                    $r = Invoke-LegHostStep -Step $def.PlugStep -QmpPort $ports.Qmp -Dir $RunDir -Since (Get-Date) -Process $Proc -SerialPath $Serial -Tag "$Tag-plug"
                    $rec.steps += "boot $($script:boot) $r"
                }
            } catch { $rec.steps += "boot $($script:boot) stick plug FAILED: $_" }
        }
        if ($ln -match '^XHLEG WANT unplug-stick') {
            try { Remove-LegStick $ports.Qmp; $rec.steps += "boot $($script:boot) stick unplugged" } catch { $rec.steps += "boot $($script:boot) stick unplug FAILED: $_" }
        }
    }
    $script:nLines = $lines.Count
    return $true
}

try {
    while (-not $ended -and $boot -lt $maxBoots -and (Get-Date) -lt $legDeadline) {
        $boot++
        $tag = "b$boot"
        $restore = ($boot -eq 1 -and -not $Cold)
        $b = [ordered]@{ boot = $boot; restore = $restore; startSecond = [math]::Round($sw.Elapsed.TotalSeconds, 1) }
        $t0 = Get-Date
        $serial = Join-Path $RunDir "serial-$tag.log"
        $proc = $null
        $nLines = 0
        $endAt = $null
        $sawPowerOff = $false
        $stepEnded = $false
        $hostStopped = $false
        try {
            if ($restore) {
                $proc = Start-LegQemu -Config $cfg -Guest $guest -Ports $ports -Disk $disk -Iso $start.Idle -RunDir $RunDir -Tag $tag -Incoming -Name "xhleg-$Leg"
                Restore-LegState $ports $start.State
                Set-LegCd $ports $legIso
                $null = Invoke-Qmp -Port $ports.Qmp -Command 'cont'
                $b.restoreSeconds = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
            } else {
                $proc = Start-LegQemu -Config $cfg -Guest $guest -Ports $ports -Disk $disk -Iso $legIso -RunDir $RunDir -Tag $tag -Name "xhleg-$Leg"
            }
            # The step a start needs comes from the phase the last start ended
            # in (AfterPhaseN), so it follows the guest, not a count of starts.
            $step = ''
            if ($boot -eq 1 -and $Cold -and $guest.ContainsKey('ColdStep')) { $step = $guest.ColdStep }
            if ($boot -gt 1 -and $pendingStep) { $step = $pendingStep; $pendingStep = '' }
            if ($mode -eq 'window' -and $boot -eq 1) {
                Write-Host "OPERATOR: VNC 127.0.0.1:$($ports.Vnc). Run the installer's window from the CD (PKG\SETUP.EXE), take the defaults, Finish; then shut Windows down (98 SE: never Restart). The host waits."
            }
            if ($step) {
                $r = Invoke-LegHostStep -Step $step -QmpPort $ports.Qmp -Dir $RunDir -Since $t0 -Process $proc -SerialPath $serial -Tag $tag
                $rec.steps += "boot $boot $r"
                if ($r -match 'QEMU ended at the restart') { $stepEnded = $true }
            }
            $lastNew = Get-Date
            $lastShot = Get-Date
            $pausedSince = $null
            while (-not $proc.HasExited) {
                # A guest frozen from outside (QMP stop, for a quiet host) does
                # not run down the stall timer or the leg's deadline: the time
                # from the first sample that saw it paused to the first that
                # sees it running again is added to both.
                if ((Get-QmpRunState $ports.Qmp) -eq 'paused') {
                    if (-not $pausedSince) { $pausedSince = Get-Date }
                    Start-Sleep -Seconds 5; continue
                }
                if ($pausedSince) {
                    $gap = (Get-Date) - $pausedSince
                    $pausedSince = $null
                    $lastNew = $lastNew + $gap; $legDeadline = $legDeadline + $gap
                    if ($endAt) { $endAt = $endAt + $gap }
                    $rec.pausedSeconds = [math]::Round($rec.pausedSeconds + $gap.TotalSeconds, 1)
                }
                if (Read-LegLines $serial $proc $tag) { $lastNew = Get-Date }                if (((Get-Date) - $lastShot).TotalSeconds -ge 60) { Save-LegShot $ports.Qmp $RunDir "$tag-last"; $lastShot = Get-Date }
                $waitLimit = $stall
                if ($mode -eq 'window' -and $boot -eq 1) { $waitLimit = 4 * 3600 }
                if (((Get-Date) - $lastNew).TotalSeconds -gt $waitLimit) {
                    Save-LegShot $ports.Qmp $RunDir "$tag-stalled"
                    $rec.reasons += "boot $boot stalled: nothing new on COM1 for $waitLimit s"
                    $failed = $true
                    break
                }
                if ($endAt -and ((Get-Date) - $endAt).TotalSeconds -gt 180) {
                    Save-LegShot $ports.Qmp $RunDir "$tag-nopoweroff"
                    $rec.reasons += "boot $boot did not power off within 180 s of END"
                    break
                }
                if ((Get-Date) -gt $legDeadline) {
                    Save-LegShot $ports.Qmp $RunDir "$tag-deadline"
                    $rec.reasons += "leg deadline of $LegMinutes min"
                    $failed = $true
                    break
                }
                Start-Sleep -Seconds 2
            }
            # A pause still open when QEMU ended (it resumed and powered off
            # between two samples) is credited too.
            if ($pausedSince) {
                $gap = (Get-Date) - $pausedSince
                $pausedSince = $null
                $legDeadline = $legDeadline + $gap
                $rec.pausedSeconds = [math]::Round($rec.pausedSeconds + $gap.TotalSeconds, 1)
            }
        } catch {
            $rec.reasons += "boot ${boot}: $_"
            $failed = $true
        } finally {
            if ($proc) {
                if (-not $proc.HasExited) { $hostStopped = $true; Stop-LegQemu $cfg $proc $ports.Qmp "leg $Leg boot $boot ended by the host" }
                $null = $proc.WaitForExit(30000)
                Add-LegPidLog $cfg "exited pid $($proc.Id) (leg $Leg boot $boot)"
            }
        }
        # What the guest wrote between the last poll and its power-off.
        $null = Read-LegLines $serial $proc $tag
        # A start ends by the guest's own power-off (POWEROFF on COM1), or by
        # the restart a host step answered; QEMU ending any other way, or
        # with an error code, is a failure even when a later start passes.
        if ($proc -and -not $hostStopped) {
            $code = $null; try { $code = $proc.ExitCode } catch { }
            if ($null -ne $code -and $code -ne 0) { $rec.reasons += "boot $boot QEMU exit code $code"; $failed = $true }
            elseif (-not $sawPowerOff -and -not $stepEnded -and -not ($mode -eq 'window' -and $boot -eq 1)) { $rec.reasons += "boot $boot ended with no power-off from the guest"; $failed = $true }
        }
        $err = Join-Path $RunDir "qemu-$tag.err"
        if ((Test-Path -LiteralPath $err) -and (Get-Item -LiteralPath $err).Length -gt 0) {
            $rec.reasons += "boot $boot QEMU stderr: " + ((Get-Content -Raw -LiteralPath $err).Trim())
        }
        $b.seconds = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
        $rec.boots += $b
        if ($failed) { break }
    }
} catch {
    $rec.reasons += "host: $_"
}
# The verdict.
if (-not $ended) { $rec.reasons += 'the guest never said END' }
$need = @('driver-file', 'list-current', 'stick-seen', 'stick-copy', 'list-removed')
if ($def.ContainsKey('Checks')) { $need = @($def.Checks) }
foreach ($c in $need) {
    if (-not $rec.checks.Contains($c)) { $rec.reasons += "check $c not reported" }
}
foreach ($k in @($rec.checks.Keys)) {
    if ($rec.checks[$k] -notlike 'PASS*') { $rec.reasons += "check $k $($rec.checks[$k])" }
}
if ($def.ContainsKey('Exits')) {
    foreach ($k in $def.Exits.Keys) {
        $allowed = @($def.Exits[$k] | ForEach-Object { [string]$_ })
        if (-not $rec.exits.Contains($k)) { $rec.reasons += "no exit code for $k" }
        elseif ($allowed -notcontains [string]$rec.exits[$k]) { $rec.reasons += "exit $k $($rec.exits[$k]), allowed $($allowed -join '/')" }
    }
}
foreach ($s in $rec.steps) { if ($s -match 'FAILED|still running|no BOOT line') { $rec.reasons += "step: $s" } }
if ($rec.reasons.Count -eq 0) { $rec.verdict = 'PASS' } else { $rec.verdict = 'FAIL' }
$rec.seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
$rec.finished = (Get-Date -Format s)
$rec | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $RunDir 'result.json') -Encoding ASCII
Remove-Item -LiteralPath $legIso -Force -ErrorAction SilentlyContinue
Write-Host ("{0} {1} in {2} s{3}" -f $Leg, $rec.verdict, $rec.seconds, $(if ($rec.reasons.Count) { ': ' + ($rec.reasons -join '; ') } else { '' }))
if ($rec.verdict -eq 'PASS') { exit 0 } else { exit 1 }
