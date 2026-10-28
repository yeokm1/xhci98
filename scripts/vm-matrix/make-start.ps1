<#
.SYNOPSIS
Make one guest's starting point for the release matrix: its disk with the
poller set up, and its machine state saved at the desktop after a cold start
that followed a shut down. build-and-test.md, "Faster emulator legs".

.DESCRIPTION
1. An overlay of the guest's base image (the configuration's Base, only ever
   read) becomes the starting point's disk.
2. First start, cold, with a CD holding the poller: the host escapes a 9x
   guest's hardware wizard (ColdStep), types the CD's XHSETUP into Start,
   Run, waits for "XHLEG SETUP OK" on COM1, then shuts Windows down from
   Start, Run.
3. Second start, cold, with an empty CD: the poller says BOOT; after
   SettleSeconds the host stops the machine and saves its state to
   state.bin. That disk and that state are the starting point; every leg
   restores them, so Windows 98 SE's rule (never a warm restart, always a
   shut down and a cold start) holds for the state every leg begins from.

Refuses an existing starting point without -Force. Writes start.json with the
machine's hash (lib\legvm.ps1 Get-LegHardwareHash): a leg refuses a starting
point saved for another QEMU or another machine definition.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Guest,
    [string]$Config = '',
    [int]$Slot = 0,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\legvm.ps1')
. (Join-Path $PSScriptRoot 'lib\leghost.ps1')

$cfg = Read-LegConfig $Config
$g = Get-LegGuest $cfg $Guest
$ports = Get-LegPorts $cfg $Slot
$qver = Get-LegQemuVersion $cfg
if (-not (Test-Path -LiteralPath $g.Base)) { throw "guest ${Guest}: no base image $($g.Base)" }
$dir = Get-LegStartDir $cfg $Guest
if (Test-Path -LiteralPath (Join-Path $dir 'start.json')) {
    if (-not $Force) { throw "guest $Guest already has a starting point in $dir (-Force makes it again)" }
}
if (Test-Path -LiteralPath $dir) {
    Get-ChildItem -LiteralPath $dir -File | ForEach-Object { $_.IsReadOnly = $false; Remove-Item -LiteralPath $_.FullName -Force }
}
New-Item -ItemType Directory -Force $dir | Out-Null
$disk = Join-Path $dir 'disk.qcow2'
$idle = Join-Path $dir 'idle.iso'
$prep = Join-Path $dir 'prep.iso'
New-LegOverlay $cfg $g.Base $disk
New-LegIdleIso $idle $dir $g
$pd = Join-Path $dir 'prep-cd'
New-Item -ItemType Directory -Force $pd | Out-Null
$legDir = Join-Path $PSScriptRoot 'leg'
if ($g.Family -eq '9x') { $files = 'XHSETUP.BAT', 'XHSETUP.REG', 'XHPOLL.BAT'; $setup = 'XHSETUP.BAT' }
else { $files = 'XHSETUP.CMD', 'XHPOLL.CMD', 'XHSETNT.REG', 'XHOFF.VBS'; $setup = 'XHSETUP.CMD' }
foreach ($f in $files) { Copy-Item -LiteralPath (Join-Path $legDir $f) -Destination $pd }
New-LegMedium -Guest $g -SourceDir $pd -Path $prep -Label 'XHPREP'
Remove-LegTree $pd

$layout = $g.Layout
$letters = @('D', 'E', 'F', 'G')
if ($g.ContainsKey('CdLetter')) { $letters = @($g.CdLetter) + ($letters | Where-Object { $_ -ne $g.CdLetter }) }
$bootWait = 600; if ($g.ContainsKey('BootSeconds')) { $bootWait = [int]$g.BootSeconds }
$settle = 30; if ($g.ContainsKey('SettleSeconds')) { $settle = [int]$g.SettleSeconds }

function Wait-Line {
    param([string]$Serial, [string]$Pattern, [int]$Seconds, $Proc)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline -and -not $Proc.HasExited) {
        if ((Get-LegSerialLines $Serial) -match $Pattern) { return $true }
        Start-Sleep -Seconds 2
    }
    return [bool]((Get-LegSerialLines $Serial) -match $Pattern)
}

# SetupElevated (Vista, 7): the setup must run elevated to create its logon
# task, so it is typed into the Start menu's search box and started with
# Ctrl+Shift+Enter, and the consent prompt answered (ConsentKey).
function Invoke-ElevatedBox {
    param([string]$Text)
    Send-QmpChord -Port $ports.Qmp -Chord 'esc' -Layout $layout; Start-Sleep -Seconds 2
    Send-QmpChord -Port $ports.Qmp -Chord 'meta_l' -Layout $layout; Start-Sleep -Seconds 4
    Send-QmpText -Port $ports.Qmp -Text $Text -Layout $layout; Start-Sleep -Seconds 3
    Send-QmpChord -Port $ports.Qmp -Chord 'ctrl-shift-ret' -Layout $layout; Start-Sleep -Seconds 8
    # ConsentKey: the consent prompt's Yes (7: Alt+Y) or Continue (Vista: Alt+C).
    $ck = 'alt-y'; if ($g.ContainsKey('ConsentKey') -and $g.ConsentKey) { $ck = $g.ConsentKey }
    Send-QmpChord -Port $ports.Qmp -Chord $ck -Layout $layout
}

function Invoke-RunBox {
    param([string]$Text)    Send-QmpChord -Port $ports.Qmp -Chord 'esc' -Layout $layout; Start-Sleep -Seconds 2
    Send-QmpChord -Port $ports.Qmp -Chord 'meta_l-r' -Layout $layout; Start-Sleep -Seconds 4
    Send-QmpText -Port $ports.Qmp -Text $Text -Layout $layout; Start-Sleep -Seconds 1
    Send-QmpChord -Port $ports.Qmp -Chord 'ret' -Layout $layout
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
# First start: the poller onto the disk.
$t0 = Get-Date
$proc = Start-LegQemu -Config $cfg -Guest $g -Ports $ports -Disk $disk -Iso $prep -RunDir $dir -Tag 'prep' -Name "xhleg-start-$Guest"
try {
    $serial = Join-Path $dir 'serial-prep.log'
    if ($g.ContainsKey('ColdStep')) {
        # No poller yet, so no BOOT line: escape the wizard for a while.
        $deadline = $t0.AddSeconds([math]::Min($bootWait, 240))
        while ((Get-Date) -lt $deadline) {
            if (((Get-Date) - $t0).TotalSeconds -ge 30) { Send-QmpChord -Port $ports.Qmp -Chord 'esc' }
            Start-Sleep -Seconds 10
            if (((Get-Date) - $t0).TotalSeconds -ge 90 -and (Test-ScreenSettled -QmpPort $ports.Qmp -Dir $dir)) { break }
        }
    } else {
        $null = Wait-LegScreen -QmpPort $ports.Qmp -Dir $dir -Since $t0 -MinSeconds 60 -MaxSeconds $bootWait
    }
    Save-LegShot $ports.Qmp $dir 'prep-desktop'
    $ok = $false
    foreach ($L in $letters) {
        if ($g.ContainsKey('SetupElevated') -and $g.SetupElevated) { Invoke-ElevatedBox "$($L):\$setup $L" }
        else { Invoke-RunBox "$($L):\$setup $L" }
        if (Wait-Line $serial '^XHLEG SETUP (OK|FAIL)' 60 $proc) { $ok = $true; break }
    }
    Save-LegShot $ports.Qmp $dir 'prep-setup'
    if (-not $ok -or -not ((Get-LegSerialLines $serial) -match '^XHLEG SETUP OK')) { throw "guest ${Guest}: XHSETUP did not report OK on COM1 (see $dir\prep-setup.png)" }
    if ($g.ContainsKey('PrepSteps')) {
        foreach ($s in @($g.PrepSteps)) {
            $r = Invoke-LegHostStep -Step $s -QmpPort $ports.Qmp -Dir $dir -Since (Get-Date) -Process $proc -SerialPath $serial -Tag 'prep' -Layout $layout
            Write-Host $r
            if ($r -match 'FAILED') { throw "guest ${Guest}: $r" }
        }
    }
    # ShutdownCommand: Windows 2000 has no shutdown.exe (XHOFF.VBS, which
    # XHSETUP.CMD copied).
    if ($g.ContainsKey('ShutdownCommand') -and $g.ShutdownCommand) { Invoke-RunBox $g.ShutdownCommand }
    elseif ($g.Family -eq '9x') { Invoke-RunBox 'rundll32.exe shell32.dll,SHExitWindowsEx 5' }
    else { Invoke-RunBox 'shutdown -s -f -t 0' }
    if (-not (Wait-LegPowerOff -Config $cfg -Guest $g -Ports $ports -Process $proc -Dir $dir -Tag 'prep')) { Save-LegShot $ports.Qmp $dir 'prep-noshutdown'; throw "guest $Guest did not power off after the shut down" }
    Add-LegPidLog $cfg "exited pid $($proc.Id) (start $Guest prep)"
} catch {
    Stop-LegQemu $cfg $proc $ports.Qmp 'make-start failed'
    throw
}

# Second start, cold after that shut down: the state at the desktop, poller up.
$t1 = Get-Date
$proc = Start-LegQemu -Config $cfg -Guest $g -Ports $ports -Disk $disk -Iso $idle -RunDir $dir -Tag 'save' -Name "xhleg-start-$Guest"
try {
    $serial = Join-Path $dir 'serial-save.log'
    if ($g.ContainsKey('ColdStep')) {
        $r = Invoke-LegHostStep -Step $g.ColdStep -QmpPort $ports.Qmp -Dir $dir -Since $t1 -Process $proc -SerialPath $serial -Tag 'save' -Layout $layout
        Write-Host $r
        if ($r -match 'FAILED|no BOOT line') { throw "guest ${Guest}: $r" }
    }
    if (-not (Wait-Line $serial '^XHLEG BOOT' $bootWait $proc)) { Save-LegShot $ports.Qmp $dir 'save-noboot'; throw "guest ${Guest}: no BOOT line on COM1 in $bootWait s" }
    $coldSeconds = [math]::Round(((Get-Date) - $t1).TotalSeconds, 1)
    Start-Sleep -Seconds $settle
    Save-LegShot $ports.Qmp $dir 'save-desktop'
    $bytes = Save-LegState $ports (Join-Path $dir 'state.bin')
    Stop-LegQemu $cfg $proc $ports.Qmp 'state saved'
} catch {
    Stop-LegQemu $cfg $proc $ports.Qmp 'make-start failed'
    throw
}
(Get-Item -LiteralPath $disk).IsReadOnly = $true
(Get-Item -LiteralPath (Join-Path $dir 'state.bin')).IsReadOnly = $true
$meta = [ordered]@{
    guest = $Guest; name = $g.Name; base = $g.Base; qemuVersion = $qver
    hardwareHash = (Get-LegHardwareHash $g $qver); hardware = (Get-LegHardwareArgs $g) -join ' '
    created = (Get-Date -Format s); coldBootSeconds = $coldSeconds; settleSeconds = $settle; stateBytes = $bytes
    makeSeconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
}
$meta | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'start.json') -Encoding ASCII
Write-Host "starting point for $Guest in $dir (cold start to the poller $coldSeconds s, state $bytes bytes)"
