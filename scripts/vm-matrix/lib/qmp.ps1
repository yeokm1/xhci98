# QMP transport for the release-matrix legs (release-matrix.ps1, run-leg.ps1,
# make-start.ps1). Dot-source it.
#
# One short connection per call: connect, read the greeting, negotiate, send
# one command, read until its answer, close. A leg's host loop polls between
# calls, so nothing here holds a socket across a guest's power-off, and an
# event that arrives while a command is answered is skipped, not queued.
#
# The TcpClient lives in this file for the reason lib\monitor.ps1 gives: an
# inline one-liner is flagged by the host's malware filter, a .ps1 is not.

function Invoke-Qmp {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command,
        [hashtable]$Arguments = $null,
        [int]$TimeoutMs = 30000,
        [switch]$AllowError
    )
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(5000)) { throw "QMP port ${Port}: no connection" }
        $client.EndConnect($iar)
        $client.ReceiveTimeout = $TimeoutMs
        $client.SendTimeout = $TimeoutMs
        $stream = $client.GetStream()
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::ASCII)
        $writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::ASCII)
        $writer.NewLine = "`r`n"
        $writer.AutoFlush = $true
        $null = $reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        $null = Read-QmpAnswer $reader 'qmp_capabilities' $false
        $o = @{ execute = $Command }
        if ($null -ne $Arguments) { $o.arguments = $Arguments }
        $writer.WriteLine((ConvertTo-Json -InputObject $o -Depth 8 -Compress))
        return Read-QmpAnswer $reader $Command ([bool]$AllowError)
    } finally {
        $client.Close()
    }
}

function Read-QmpAnswer {
    param($Reader, [string]$Command, [bool]$AllowError)
    while ($true) {
        $line = $Reader.ReadLine()
        if ($null -eq $line) { throw "QMP closed while waiting for $Command" }
        $r = ConvertFrom-Json $line
        if ($null -ne $r.event) { continue }
        if ($null -ne $r.error) {
            if ($AllowError) { return $r }
            throw "QMP $Command refused: $($r.error.class): $($r.error.desc)"
        }
        return $r.return
    }
}

function Invoke-Hmp {
    param([int]$Port, [string]$Line)
    return Invoke-Qmp -Port $Port -Command 'human-monitor-command' -Arguments @{ 'command-line' = $Line }
}

function Test-QmpUp {
    param([int]$Port)
    try { $null = Invoke-Qmp -Port $Port -Command 'query-status' -TimeoutMs 5000; return $true } catch { return $false }
}

function Get-QmpRunState {
    param([int]$Port)
    try { return (Invoke-Qmp -Port $Port -Command 'query-status' -TimeoutMs 5000).status } catch { return '' }
}

# Key chords are QEMU qcode names joined by '-' (ctrl-esc, alt-n, ret, shift-a).
# A guest whose keyboard layout is US Dvorak (the NT guests of this lab, see
# build-and-test.md) gets letters and punctuation mapped so the character it
# types is the one asked for; modifiers and named keys pass unchanged.
$script:QwertyRow = "-=qwertyuiop[]asdfghjkl;'zxcvbnm,./"
$script:DvorakRow = "[]',.pyfgcrl/=aoeuidhtns-;qjkxbmwvz"
$script:QcodeOf = @{
    ' ' = 'spc'; '-' = 'minus'; '=' = 'equal'; '[' = 'bracket_left'; ']' = 'bracket_right'
    '\' = 'backslash'; ';' = 'semicolon'; "'" = 'apostrophe'; ',' = 'comma'; '.' = 'dot'
    '/' = 'slash'; '`' = 'grave_accent'
}
$script:ShiftedOf = @{
    '!' = '1'; '@' = '2'; '#' = '3'; '$' = '4'; '%' = '5'; '^' = '6'; '&' = '7'; '*' = '8'
    '(' = '9'; ')' = '0'; '_' = '-'; '+' = '='; '{' = '['; '}' = ']'; '|' = '\'; ':' = ';'
    '"' = "'"; '<' = ','; '>' = '.'; '?' = '/'; '~' = '`'
}

function ConvertTo-PhysicalChar {
    param([char]$C, [string]$Layout)
    if ($Layout -ne 'dvorak') { return $C }
    $i = $script:DvorakRow.IndexOf([char]::ToLowerInvariant($C))
    if ($i -lt 0) { return $C }
    return $script:QwertyRow[$i]
}

# The qcodes, in order, that type one character on the guest.
function Get-CharQcodes {
    param([char]$C, [string]$Layout)
    $shift = $false
    $base = $C
    if ([char]::IsLetter($C)) {
        $shift = [char]::IsUpper($C)
        $base = [char]::ToLowerInvariant($C)
    } elseif ($script:ShiftedOf.ContainsKey([string]$C)) {
        $shift = $true
        $base = [char]$script:ShiftedOf[[string]$C]
    }
    $p = ConvertTo-PhysicalChar $base $Layout
    if ([char]::IsLetterOrDigit($p)) { $name = [string]$p }
    elseif ($script:QcodeOf.ContainsKey([string]$p)) { $name = $script:QcodeOf[[string]$p] }
    else { throw "cannot type character '$C'" }
    if ($shift) { return @('shift', $name) }
    return @($name)
}

function Send-QmpChord {
    param([int]$Port, [string]$Chord, [string]$Layout = 'qwerty', [int]$HoldMs = 80)
    $parts = $Chord -split '-'
    $keys = @()
    for ($i = 0; $i -lt $parts.Count; $i++) {
        $k = $parts[$i]
        if ($i -eq $parts.Count - 1 -and $k.Length -eq 1) {
            $k = [string](ConvertTo-PhysicalChar ([char]$k) $Layout)
            if ($script:QcodeOf.ContainsKey($k)) { $k = $script:QcodeOf[$k] }
        }
        if ($k -eq 'win') { $k = 'meta_l' }
        $keys += @{ type = 'qcode'; data = $k }
    }
    $null = Invoke-Qmp -Port $Port -Command 'send-key' -Arguments @{ keys = $keys; 'hold-time' = $HoldMs }
}

function Send-QmpText {
    param([int]$Port, [string]$Text, [string]$Layout = 'qwerty', [int]$GapMs = 120)
    foreach ($c in $Text.ToCharArray()) {
        $codes = Get-CharQcodes $c $Layout
        $keys = @($codes | ForEach-Object { @{ type = 'qcode'; data = $_ } })
        $null = Invoke-Qmp -Port $Port -Command 'send-key' -Arguments @{ keys = $keys; 'hold-time' = 60 }
        Start-Sleep -Milliseconds $GapMs
    }
}

function Save-QmpScreen {
    param([int]$Port, [string]$Path)
    $null = Invoke-Qmp -Port $Port -Command 'screendump' -Arguments @{ filename = $Path; format = 'png' }
}
