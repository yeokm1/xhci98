# usb4-qmp.ps1 - drive a guest started by run-usb4-guest.ps1 over QMP.
#
#   powershell -File scripts\usb4\usb4-qmp.ps1 -Name <run name> <verb> [args...]
#
# Verbs:
#   status                 QEMU's run state, and whether the pid lives
#   unplug [id]            device_del of the device router (default enc0):
#                          the tunnelled device unplugged through the CM path
#   plug                   device_add of enc0 exactly as the launcher built it
#   tunnel up|down|eject|surprise
#                          qom-set on the tunnel bridge (bypasses the CM: the
#                          driverless baseline only, record 21 section 10.2)
#   harness <prop> [value] the fault-injection harness on the tunnel bridge
#                          (usb4-emu docs/HARNESS.md): qom-set of an x-*
#                          property, or qom-get with no value, e.g.
#                          harness x-faults "at=fetch op=read nth=3 act=surprise"
#                          harness x-harness-state
#   shot <file.png>        screendump (PNG)
#   hmp <command...>       any HMP command, e.g. hmp info pci
#   key <keys...>          HMP sendkey for each chord, e.g. key ctrl-esc ret
#   type <text>            types text (US-Dvorak mapping applied on the
#                          Windows 2000 guests, -Layout overrides)
#   run <command line>     Win+R, types the line, Enter (the Run box)
#   powerdown              ACPI power button
#   quit                   QEMU quit, then waits for the process and frees
#                          the guest lock
#   release                frees the guest lock if the process is gone
#   wait                   waits for the process to exit, then frees the lock
#
# It acts only on the process run.json names (a QEMU this project started).
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('status', 'unplug', 'plug', 'tunnel', 'harness', 'shot', 'hmp', 'key', 'type', 'run',
                 'powerdown', 'quit', 'release', 'wait')]
    [string]$Verb,
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest = @(),
    [ValidateSet('auto', 'qwerty', 'dvorak')][string]$Layout = 'auto',
    [int]$DelayMs = 40,
    [int]$TimeoutSec = 120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$scriptRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$commonDir = (& git -C $scriptRepo rev-parse --path-format=absolute --git-common-dir).Trim()
$mainRoot = Split-Path -Parent ($commonDir -replace '/', '\')
$runDir = Join-Path $mainRoot "vm\usb4\$Name"
$runJson = Join-Path $runDir 'run.json'
if (-not (Test-Path $runJson)) { throw "no run.json for '$Name' ($runJson)" }
$run = Get-Content $runJson -Raw | ConvertFrom-Json
if ($run.qmpPort -lt 47900 -or $run.qmpPort -gt 47999) { throw "run.json names port $($run.qmpPort), outside 47900-47999" }

# Is the recorded process still the very QEMU run-usb4-guest.ps1 started?
#   ours  - same pid, creation time and executable, listening on this
#           run's QMP port
#   gone  - no process with that pid, or one created at another time (the
#           guest exited and the pid was reused)
#   other - a process with that pid whose identity cannot be confirmed
#           (also every run.json without the identity fields); nothing is
#           sent to it and its lock is left alone
function Get-RunState {
    $procId = [int]$run.pid
    if ($procId -le 0) { return 'gone' }
    $p = Get-CimInstance Win32_Process -Filter "ProcessId = $procId" -ErrorAction SilentlyContinue
    if (-not $p) { return 'gone' }
    $hasId = ($run.PSObject.Properties['created'] -and $run.created -and $run.PSObject.Properties['qemu'] -and $run.qemu)
    if (-not $hasId) { return 'other' }
    if ($p.CreationDate.ToUniversalTime().ToString('o') -ne $run.created) { return 'gone' }
    if (-not $p.ExecutablePath -or
        [System.IO.Path]::GetFullPath($p.ExecutablePath) -ine [System.IO.Path]::GetFullPath($run.qemu)) { return 'other' }
    if (-not $p.CommandLine -or $p.CommandLine -notlike "*tcp:127.0.0.1:$($run.qmpPort),*") { return 'other' }
    return 'ours'
}
# Frees the guest lock only if it is still this run's (its token), under the
# host's admission lock so a launch that is replacing it cannot interleave.
function Clear-Lock {
    if (-not $run.lock -or -not (Test-Path $run.lock)) { return }
    $admit = Join-Path (Split-Path $run.lock) '_admit'
    $got = $false
    $deadline = (Get-Date).AddSeconds(120)
    while (-not $got) {
        try { New-Item -ItemType Directory $admit -ErrorAction Stop | Out-Null; $got = $true }
        catch {
            if ((Get-Date) -gt $deadline) { Write-Warning "admission lock busy; $($run.lock) left in place"; return }
            Start-Sleep -Seconds 2
        }
    }
    try {
        if (Test-Path $run.lock) {
            $t = Get-Content $run.lock -Raw
            $mine = $false
            if ($run.PSObject.Properties['token'] -and $run.token) { $mine = ($t -match "token=$($run.token)\b") }
            else { $mine = ($t -match "pid=$($run.pid)\b") }
            if ($mine) { Remove-Item $run.lock -Force; Write-Host "freed $($run.lock)" }
        }
    } finally { Remove-Item $admit -Force -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------- QMP
$script:client = $null
$script:reader = $null
$script:writer = $null
function Open-Qmp {
    $script:client = New-Object System.Net.Sockets.TcpClient
    $script:client.Connect('127.0.0.1', [int]$run.qmpPort)
    $s = $script:client.GetStream()
    $s.ReadTimeout = $TimeoutSec * 1000
    $script:reader = New-Object System.IO.StreamReader($s, [System.Text.Encoding]::ASCII)
    $script:writer = New-Object System.IO.StreamWriter($s, [System.Text.Encoding]::ASCII)
    $script:writer.NewLine = "`r`n"
    $script:writer.AutoFlush = $true
    [void]$script:reader.ReadLine()          # greeting
    [void](Invoke-Qmp 'qmp_capabilities' $null)
}
function Invoke-Qmp([string]$cmd, $arguments) {
    $o = @{ execute = $cmd }
    if ($null -ne $arguments) { $o['arguments'] = $arguments }
    $script:writer.WriteLine(($o | ConvertTo-Json -Compress -Depth 6))
    while ($true) {
        $line = $script:reader.ReadLine()
        if ($null -eq $line) { throw 'QMP connection closed' }
        $r = $line | ConvertFrom-Json
        if ($r.PSObject.Properties['event']) { Write-Verbose "event $($r.event)"; continue }
        if ($r.PSObject.Properties['error']) { throw "QMP $cmd : $($r.error.class): $($r.error.desc)" }
        if ($r.PSObject.Properties['return']) { return $r.return }
    }
}
function Invoke-Hmp([string]$line) {
    $r = Invoke-Qmp 'human-monitor-command' @{ 'command-line' = $line }
    if ($r) { Write-Output ($r.TrimEnd()) }
}
function Close-Qmp { if ($script:client) { $script:client.Close() } }

# ---------------------------------------------------------------- keys
$named = @{
    ' ' = 'spc'; "`n" = 'ret'; "`t" = 'tab'
    '-' = 'minus'; '=' = 'equal'; '[' = 'bracket_left'; ']' = 'bracket_right'
    ';' = 'semicolon'; "'" = 'apostrophe'; '`' = 'grave_accent'; '\' = 'backslash'
    ',' = 'comma'; '.' = 'dot'; '/' = 'slash'
    '_' = 'shift-minus'; '+' = 'shift-equal'; '{' = 'shift-bracket_left'; '}' = 'shift-bracket_right'
    ':' = 'shift-semicolon'; '"' = 'shift-apostrophe'; '~' = 'shift-grave_accent'; '|' = 'shift-backslash'
    '<' = 'shift-comma'; '>' = 'shift-dot'; '?' = 'shift-slash'
    '!' = 'shift-1'; '@' = 'shift-2'; '#' = 'shift-3'; '$' = 'shift-4'; '%' = 'shift-5'
    '^' = 'shift-6'; '&' = 'shift-7'; '*' = 'shift-8'; '(' = 'shift-9'; ')' = 'shift-0'
}
# US-Dvorak: the character at each QWERTY-labelled position, unshifted and shifted.
$qwU = "-=qwertyuiop[]asdfghjkl;'zxcvbnm,./"
$dvU = "[]',.pyfgcrl/=aoeuidhtns-;qjkxbmwvz"
$qwS = '_+QWERTYUIOP{}ASDFGHJKL:"ZXCVBNM<>?'
$dvS = '{}"<>PYFGCRL?+AOEUIDHTNS_:QJKXBMWVZ'
function Get-QwertyKey([char]$c) {
    $s = [string]$c
    if ($named.ContainsKey($s)) { return $named[$s] }
    if ($s -cmatch '^[a-z0-9]$') { return $s }
    if ($s -cmatch '^[A-Z]$') { return 'shift-' + $s.ToLower() }
    throw "no key for character '$s'"
}
function Get-Key([char]$c, [bool]$dvorak) {
    if ($dvorak) {
        $i = $dvU.IndexOf($c)
        if ($i -ge 0) { return Get-QwertyKey $qwU[$i] }
        $i = $dvS.IndexOf($c)
        if ($i -ge 0) { return Get-QwertyKey $qwS[$i] }
    }
    return Get-QwertyKey $c
}

# ---------------------------------------------------------------- verbs
switch ($Verb) {
    'release' {
        $st = Get-RunState
        if ($st -ne 'gone') { throw "pid $($run.pid) still runs ($st); use quit, or stop it by hand" }
        Clear-Lock; return
    }
    'wait' {
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-RunState) -ne 'gone' -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
        if ((Get-RunState) -ne 'gone') { throw "pid $($run.pid) still runs after $TimeoutSec s" }
        Clear-Lock; return
    }
}
$state = Get-RunState
if ($state -eq 'gone') { Clear-Lock; throw "$Name (pid $($run.pid)) is not running" }
if ($state -ne 'ours') { throw "pid $($run.pid) cannot be confirmed as the QEMU this launcher started for $Name; nothing sent" }

Open-Qmp
try {
    switch ($Verb) {
        'status' {
            $st = Invoke-Qmp 'query-status' $null
            Write-Output "$Name pid $($run.pid) status=$($st.status) running=$($st.running) qmp=$($run.qmpPort) vnc=$($run.vncPort)"
        }
        'unplug' {
            $id = 'enc0'; if ($Rest.Count -gt 0) { $id = $Rest[0] }
            [void](Invoke-Qmp 'device_del' @{ id = $id })
            Write-Output "device_del $id"
        }
        'plug' {
            if (-not $run.enc0) { throw 'this run has no usb4 device router' }
            Invoke-Hmp "device_add $($run.enc0)"
            Write-Output "device_add $($run.enc0)"
        }
        'tunnel' {
            if ($Rest.Count -ne 1 -or $Rest[0] -notin @('up', 'down', 'eject', 'surprise')) { throw 'tunnel up|down|eject|surprise' }
            [void](Invoke-Qmp 'qom-set' @{ path = "/machine/peripheral/$($run.tunnelBridge)"; property = 'tunnel'; value = $Rest[0] })
            Write-Output "tunnel $($Rest[0])"
        }
        'harness' {
            if ($Rest.Count -lt 1 -or $Rest[0] -notlike 'x-*') { throw 'harness <x-property> [value]' }
            $path = "/machine/peripheral/$($run.tunnelBridge)"
            if ($Rest.Count -eq 1) {
                Write-Output (Invoke-Qmp 'qom-get' @{ path = $path; property = $Rest[0] })
            } else {
                $v = ($Rest[1..($Rest.Count - 1)]) -join ' '
                $val = $v
                if ($v -match '^-?\d+$') { $val = [long]$v } elseif ($v -eq 'true' -or $v -eq 'false') { $val = ($v -eq 'true') }
                [void](Invoke-Qmp 'qom-set' @{ path = $path; property = $Rest[0]; value = $val })
                Write-Output "$($Rest[0]) = $v"
            }
        }
        'shot' {
            if ($Rest.Count -ne 1) { throw 'shot <file.png>' }
            $f = [System.IO.Path]::GetFullPath($Rest[0])
            [void](Invoke-Qmp 'screendump' @{ filename = $f; format = 'png' })
            Write-Output $f
        }
        'hmp' { Invoke-Hmp ($Rest -join ' ') }
        'key' {
            foreach ($k in $Rest) { Invoke-Hmp "sendkey $k"; Start-Sleep -Milliseconds $DelayMs }
        }
        { $_ -in @('type', 'run') } {
            $dv = $false
            if ($Layout -eq 'dvorak' -or ($Layout -eq 'auto' -and $run.dvorak)) { $dv = $true }
            if ($Verb -eq 'run') {
                Invoke-Hmp ('sendkey meta_l-' + (Get-Key ([char]'r') $dv)); Start-Sleep -Milliseconds 1500
            }
            foreach ($c in ($Rest -join ' ').ToCharArray()) {
                Invoke-Hmp ('sendkey ' + (Get-Key $c $dv)); Start-Sleep -Milliseconds $DelayMs
            }
            if ($Verb -eq 'run') { Invoke-Hmp 'sendkey ret' }
        }
        'powerdown' { [void](Invoke-Qmp 'system_powerdown' $null); Write-Output 'powerdown sent' }
        'quit' {
            try { [void](Invoke-Qmp 'quit' $null) } catch { Write-Verbose $_.Exception.Message }
            Close-Qmp
            $deadline = (Get-Date).AddSeconds($TimeoutSec)
            while ((Get-RunState) -eq 'ours' -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
            if ((Get-RunState) -ne 'gone') { throw "pid $($run.pid) did not exit" }
            Clear-Lock
            Write-Output "$Name stopped"
        }
    }
} finally { Close-Qmp }
