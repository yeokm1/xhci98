# QEMU monitor transport for the Phase 10 device matrix.
#
# This is the committed form of the Send-Mon/Send-Checked idiom that every
# scripts\local\ stage driver grew independently.  It is a library: dot-source
# it and call the functions.  It holds no state of its own beyond the error
# counter, which is what a stage's exit code is built from.
#
# WHY A TELNET MONITOR RATHER THAN STDIN.  Piping monitor commands into
# `-monitor stdio` from PowerShell prepends a UTF-8 BOM to the first line, which
# QEMU reports as `unknown command: '<bom>info'` - measured on this host
# in Phase 10's matrix while probing the device population, and it costs a whole probe run
# because the FIRST command is the one that is eaten.  Setting $OutputEncoding
# does not fix it in Windows PowerShell 5.1.  A TCP monitor takes the bytes this
# file writes, so there is nothing to get wrong.
#
# WHY THE TcpClient LIVES IN A FILE.  Windows Defender flags an inline
# `New-Object System.Net.Sockets.TcpClient` one-liner as Win32/ClickFix.CCJ!MTB
# and kills the process with a bare `EPERM uv_spawn` that names neither Defender
# nor the reason (measured, batch 7b-A.1.0).  In a .ps1 it is fine.

$script:MonitorErrors = 0

function Reset-MonitorErrors {
    $script:MonitorErrors = 0
}

function Get-MonitorErrors {
    return $script:MonitorErrors
}

function Add-MonitorError {
    param([string]$Reason)
    $script:MonitorErrors++
    Write-Host ("  *** {0}" -f $Reason)
}

# WHY EVERY PROGRESS LINE IN THIS FILE IS Write-Host AND NOT Write-Output.
# Send-Checked both echoes what it sent and returns the reply, and in PowerShell
# a function's Write-Output is part of its return value: `$r = Send-Checked ...`
# swallows the echo into $r, so the operator sees a run that went straight from
# "starting" to "info usb" with no record of the twelve commands in between AND
# a caller whose $r is full of its own echo.  Measured while writing this file.
# Progress belongs on the host stream; the reply is the return value.

# Strip the monitor's readline echo and terminal escapes.  QEMU echoes each
# character with a cursor-move escape around it, so a raw reply is unreadable
# and un-matchable; every caller below wants the clean text.
function ConvertFrom-MonitorReply {
    param([string]$Raw, [string]$Command)
    if ($null -eq $Raw) { return @() }
    $clean = $Raw -replace "\x1b\[[0-9;]*[A-Za-z]", "" -replace "\x08", ""
    $lines = $clean -split "`r?`n"
    $out = @()
    foreach ($line in $lines) {
        $t = $line.Trim()
        if ($t -eq "") { continue }
        if ($t -eq "(qemu)") { continue }
        if ($t -like "QEMU*monitor*") { continue }
        $t = $t -replace "^\(qemu\)\s*", ""
        if ($t -eq "") { continue }
        # THE ECHO IS NOT ONE COPY OF THE COMMAND, IT IS EVERY PREFIX OF IT.
        # QEMU's monitor readline redraws the whole input line after each
        # character, so once the cursor escapes are stripped a one-word command
        # comes back as `iininfinfoinfo info uinfo usinfo usb` on a single line.
        # A first version of this filter compared for equality and let all of
        # that through, which put 3 KB of echo into the NOTE column of the task
        # 10.1 table.  What every such line has in common is that it ENDS with
        # the command, because the last redraw is the complete one.
        if ($Command -and ($t -eq $Command -or $t.EndsWith($Command))) { continue }
        $out += $t
    }
    return $out
}

function Send-Mon {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command,
        [switch]$Reply,
        [switch]$Quiet,
        [int]$IdleMs = 0,
        [int]$HardMs = 0
    )
    if ($IdleMs -le 0) { $IdleMs = if ($Reply) { 900 } else { 250 } }
    if ($HardMs -le 0) { $HardMs = if ($Reply) { 8000 } else { 1500 } }
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $client.Connect("127.0.0.1", $Port)
        $stream = $client.GetStream()
        Start-Sleep -Milliseconds 150
        while ($stream.DataAvailable) { $null = $stream.ReadByte() }
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($Command + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        if ($Quiet) {
            Start-Sleep -Milliseconds 120
            $stream.Close(); $client.Close()
            return $null
        }
        $sb = New-Object System.Text.StringBuilder
        $idle = [Diagnostics.Stopwatch]::StartNew()
        $hard = [Diagnostics.Stopwatch]::StartNew()
        $buf = New-Object byte[] 65536
        while ($hard.ElapsedMilliseconds -lt $HardMs -and $idle.ElapsedMilliseconds -lt $IdleMs) {
            if ($stream.DataAvailable) {
                $n = $stream.Read($buf, 0, $buf.Length)
                if ($n -gt 0) {
                    [void]$sb.Append([System.Text.Encoding]::ASCII.GetString($buf, 0, $n))
                    $idle.Restart()
                }
            } else {
                Start-Sleep -Milliseconds 30
            }
        }
        # A REPLY IS COMPLETE WHEN THE PROMPT IS BACK, NOT WHEN THE WIRE WENT
        # QUIET.  Silence alone is also what a busy or wedged guest produces,
        # and what a third monitor client queued behind two others gets, and an
        # empty string read as "the device is not listed" turned every one of
        # those into a departure that never happened (repo audit S-6).  So a
        # reply without a trailing `(qemu)` is given a second window, and one
        # that still lacks it is an error and returns nothing rather than a
        # fragment a caller could mistake for the whole answer.
        if (-not (Test-MonitorReplyComplete -Raw $sb.ToString())) {
            $more = [Diagnostics.Stopwatch]::StartNew()
            while ($more.ElapsedMilliseconds -lt $HardMs -and -not (Test-MonitorReplyComplete -Raw $sb.ToString())) {
                if ($stream.DataAvailable) {
                    $n = $stream.Read($buf, 0, $buf.Length)
                    if ($n -gt 0) { [void]$sb.Append([System.Text.Encoding]::ASCII.GetString($buf, 0, $n)) }
                } else {
                    Start-Sleep -Milliseconds 30
                }
            }
        }
        $stream.Close(); $client.Close()
        if (-not (Test-MonitorReplyComplete -Raw $sb.ToString())) {
            Write-Host ("monitor: no (qemu) prompt came back after '{0}' within {1} ms; the reply is incomplete and is not used" -f $Command, ($HardMs * 2))
            $script:MonitorErrors++
            return $null
        }
        return $sb.ToString()
    } catch {
        Write-Host ("monitor: {0}" -f $_.Exception.Message)
        $script:MonitorErrors++
    }
    return $null
}

# The monitor's prompt is the end-of-reply marker: QEMU prints `(qemu) ` after
# every command's output, including a command that printed nothing.
function Test-MonitorReplyComplete {
    param([string]$Raw)
    if ($null -eq $Raw) { return $false }
    $clean = $Raw -replace "\x1b\[[0-9;]*[A-Za-z]", "" -replace "\x08", ""
    return ($clean.TrimEnd() -match '\(qemu\)$')
}

# An argument for a monitor command that takes a path or an option string.
# HMP splits its arguments on whitespace, so a path with a space in it arrives
# as two arguments and the command fails on the second one; a double-quoted
# argument is read whole, but inside quotes a backslash starts an escape and
# `\U` is refused.  Forward slashes open the same file on Windows, so a quoted
# argument is written with them.  Verified guestless on QEMU 11: `screendump
# "C:/dir with space/x.ppm"` writes the file, the unquoted form does not.
function ConvertTo-HmpArgument {
    param([Parameter(Mandatory = $true)][string]$Text)
    if ($Text -notmatch '[\s"]') { return $Text }
    return ('"' + (($Text -replace '\\', '/') -replace '"', '\"') + '"')
}

# A monitor command whose failure must be LOUD.  hub7bv0.ps1's defect 2: a
# stage that did nothing must not look like a stage that worked, and
# `bus=hub1.0` is not a bus - the error came back on the wire and was thrown
# away, so five stages "passed" having attached nothing.
function Send-Checked {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command
    )
    Write-Host ("+ {0}" -f $Command)
    $lines = ConvertFrom-MonitorReply (Send-Mon -Port $Port -Command $Command -Reply) $Command
    foreach ($line in $lines) {
        Write-Host ("  {0}" -f $line)
        if ($line -match "(?i)error|not found|failed|no such|cannot|unable|invalid|unknown command") {
            $script:MonitorErrors++
        }
    }
    return $lines
}

# A command whose REFUSAL is the expected answer (QEMU's own hub-depth limit,
# an intentionally impossible device).  Anything other than the named refusal
# is still an error, because a different failure means the stage did not
# establish what it says it did.
function Send-ExpectedRefusal {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string]$Pattern
    )
    Write-Host ("+ {0}   (a refusal matching /{1}/ is the expected answer)" -f $Command, $Pattern)
    $lines = ConvertFrom-MonitorReply (Send-Mon -Port $Port -Command $Command -Reply) $Command
    $matched = $false
    foreach ($line in $lines) {
        Write-Host ("  {0}" -f $line)
        if ($line -match $Pattern) { $matched = $true }
    }
    if ($matched) { return $true }
    Add-MonitorError ("the expected refusal /{0}/ did not appear - read the reply above" -f $Pattern)
    return $false
}

# Query only: no error scanning, because the caller is going to parse the text.
# Returns $null, not an empty list, when no complete reply came back: an empty
# list is a real answer (`info usb` on an empty bus prints nothing at all), and
# the two must stay distinguishable.
function Get-MonitorText {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Command
    )
    $raw = Send-Mon -Port $Port -Command $Command -Reply
    if ($null -eq $raw) { return $null }
    return (ConvertFrom-MonitorReply $raw $Command)
}

# IS A DEVICE ID ON THE BUS, as three answers rather than two.  $true: `info
# usb` lists it.  $false: a complete reply did not.  $null: no complete reply
# came back, so nothing is known.  Every "did the pull take" wait in this
# directory reads this, because `info usb` on an empty bus prints no lines and
# an incomplete reply prints none either; only the prompt tells them apart.
function Test-UsbDeviceListed {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Id
    )
    $lines = Get-MonitorText -Port $Port -Command "info usb"
    if ($null -eq $lines) { return $null }
    return (($lines -join " ") -match ('ID:\s*' + [regex]::Escape($Id) + '\b'))
}

# Is something ALREADY listening on this monitor port, before we launch?
#
# THIS IS NOT A TIDINESS CHECK.  A guest left running by a previous run - a
# killed harness, a group the operator kept with -KeepGuestOnFailure - listens
# on the same port.  Wait-Monitor would then succeed against it, and the harness
# would attach devices to, read counters out of, and publish a report about THE
# PREVIOUS RUN'S GUEST, with the new QEMU sitting beside it unable to bind and
# nothing anywhere saying so.  Measured while building the harness: a QEMU from
# a killed run survived, and the only reason it was caught is that it also held
# the debug console log open.  A locked file is not a check.
function Test-MonitorPortFree {
    param([Parameter(Mandatory = $true)][int]$Port)
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $client.Connect("127.0.0.1", $Port)
        $client.Close()
        return $false
    } catch {
        return $true
    }
}

# CAN THIS PORT BE BOUND AT ALL, before a boot is spent finding out.  Windows
# reserves port ranges (Hyper-V and WSL take new blocks after a reboot; `netsh
# interface ipv4 show excludedportrange protocol=tcp` lists them), and a QEMU
# told to listen inside one dies on `Failed to bind socket`, which the harness
# otherwise meets as a monitor that never answered, sixty seconds later.
# Returns "" when a listener could be opened and closed, else the reason.
function Test-MonitorPortBindable {
    param([Parameter(Mandatory = $true)][int]$Port)
    $listener = $null
    try {
        $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Port)
        $listener.Start()
        return ""
    } catch {
        $why = $_.Exception.Message
        if ($null -ne $_.Exception.InnerException) { $why = $_.Exception.InnerException.Message }
        return ("port {0} cannot be bound on 127.0.0.1 ({1}). Either a process already listens there, or Windows has reserved the range: run `netsh interface ipv4 show excludedportrange protocol=tcp` and pick a Monitor port outside every range it lists." -f $Port, $why.Trim())
    } finally {
        if ($null -ne $listener) { try { $listener.Stop() } catch { } }
    }
}

# Wait until the monitor answers at all.  A QEMU that died on its command line
# (a bad -audiodev is fatal and QEMU does not fall back; a declared image that
# is not there) leaves nothing listening, and every later stage would report a
# connection error instead of the real cause.
function Wait-Monitor {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutSeconds = 60
    )
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $before = $script:MonitorErrors
        $raw = Send-Mon -Port $Port -Command "info version" -Reply
        $script:MonitorErrors = $before
        if ($null -ne $raw -and $raw -ne "") { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# Keep the controller out of idle suspend.  Windows 98 suspends the controller
# about half a second after the last transfer, and a device_add onto a halted
# controller is invisible to the whole stack (batch 7a-V).  mouse_move drives
# the USB pointer, not only the PS/2 one, which is the whole reason this works
# - so it needs a USB pointer device present to have any effect.
function Invoke-Pump {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][int]$Seconds
    )
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $dx = 3
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        Send-Mon -Port $Port -Command ("mouse_move {0} 0" -f $dx) -Quiet | Out-Null
        $dx = -$dx
    }
}
