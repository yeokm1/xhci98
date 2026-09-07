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

        #
        # **SYNCHRONISE ON THE BANNER'S PROMPT, THEN DRAIN, THEN SEND.**
        #
        # This used to sleep 150 ms, drain whatever had arrived, and send. The
        # race that leaves is the 2026-09-07 audit's H19, and it produces the
        # exact reading the completeness test below exists to prevent. QEMU
        # prints its banner and a `(qemu)` prompt on connect; if that lands
        # AFTER the drain - a loaded host, a guest mid-boot, a third client
        # queued ahead of this one - the banner's own prompt is still in the
        # buffer when the command goes out. If the guest is then slow to
        # answer, the idle window closes on a buffer whose tail is that
        # prompt, `Test-MonitorReplyComplete` says the reply is complete, and
        # the caller gets an EMPTY complete reply. For `info usb` that reads as
        # "the device is not listed", which is a departure that never happened
        # - the same wrong reading audit S-6 fixed from the other side.
        #
        # So the connect prompt is waited for and consumed here, and only then
        # is anything sent. A monitor that does not produce one within the
        # window is NOT synchronised, and the command is not sent at all: the
        # first cut of this said so out loud and then sent anyway, which left
        # the whole race in place behind a warning nobody reads in a matrix log
        # thousands of lines long. A refusal here costs one row marked as a
        # monitor error, which is what an unsynchronised monitor is; sending
        # costs a departure that never happened.
        #
        $banner = New-Object System.Text.StringBuilder
        $sync = [Diagnostics.Stopwatch]::StartNew()
        $syncBuf = New-Object byte[] 4096
        while ($sync.ElapsedMilliseconds -lt 2000 -and
               -not (Test-MonitorReplyComplete -Raw $banner.ToString())) {
            if ($stream.DataAvailable) {
                $n = $stream.Read($syncBuf, 0, $syncBuf.Length)
                if ($n -gt 0) {
                    [void]$banner.Append([System.Text.Encoding]::ASCII.GetString($syncBuf, 0, $n))
                }
            } else {
                Start-Sleep -Milliseconds 20
            }
        }
        if (-not (Test-MonitorReplyComplete -Raw $banner.ToString())) {
            Write-Host ("monitor: no (qemu) prompt on connect within 2000 ms before '{0}'; the reply could not be separated from the banner, so nothing was sent" -f $Command)
            $script:MonitorErrors++
            $stream.Close(); $client.Close()
            return $null
        }
        # Anything still queued behind the prompt is not this command's.
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
        if (-not (Test-MonitorReplyComplete -Raw $sb.ToString() -Echo $Command -RequireEcho)) {
            $more = [Diagnostics.Stopwatch]::StartNew()
            while ($more.ElapsedMilliseconds -lt $HardMs -and -not (Test-MonitorReplyComplete -Raw $sb.ToString() -Echo $Command -RequireEcho)) {
                if ($stream.DataAvailable) {
                    $n = $stream.Read($buf, 0, $buf.Length)
                    if ($n -gt 0) { [void]$sb.Append([System.Text.Encoding]::ASCII.GetString($buf, 0, $n)) }
                } else {
                    Start-Sleep -Milliseconds 30
                }
            }
        }
        $stream.Close(); $client.Close()
        if (-not (Test-MonitorReplyComplete -Raw $sb.ToString() -Echo $Command -RequireEcho)) {
            Write-Host ("monitor: no (qemu) prompt came back after '{0}'s own echo within {1} ms; the reply is incomplete and is not used" -f $Command, ($HardMs * 2))
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
#
# `-Echo` names the command that was sent. When it is given, the prompt only
# counts if it comes AFTER that command's echo - which is the second half of
# the 2026-09-07 audit's H19, and the belt to the connect-time
# synchronisation's brace. QEMU echoes what is written to the monitor, so the
# echo is where this command's reply begins; a prompt sitting before it
# belongs to the banner or to somebody else's command, and accepting it hands
# the caller an empty answer that reads as a real one.
#
# By default the echo is not required to be PRESENT: this function is also
# used on the connect banner, which answers no command, and on accumulated
# text that starts mid-reply. What is always required is that no prompt is
# accepted BEFORE the echo when the echo is there.
#
# `-RequireEcho` closes the rest of the hole, and `Send-Mon` passes it,
# because QEMU's HMP always echoes what is written to it. Without it a reply
# whose echo has not arrived yet is judged on whatever prompt IS in the
# buffer - the banner's, if the connect-time drain lost the race - and an
# empty reply comes back marked complete. That is the reading the whole of
# H19 is about, and no timeout can see it, because from a timeout's side a
# prompt is a prompt.
#
function Test-MonitorReplyComplete {
    param([string]$Raw, [string]$Echo = "", [switch]$RequireEcho)
    if ($null -eq $Raw) { return $false }
    $clean = $Raw -replace "\x1b\[[0-9;]*[A-Za-z]", "" -replace "\x08", ""
    $sawEcho = $false
    if ($Echo -ne "") {
        $at = $clean.IndexOf($Echo, [System.StringComparison]::Ordinal)
        if ($at -ge 0) {
            $sawEcho = $true
            $clean = $clean.Substring($at + $Echo.Length)
        }
    }
    if ($RequireEcho -and -not $sawEcho) { return $false }
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

# The `chardev-add file,...` command for a row's backend, composed here so the
# quoting is right in one place.  HMP recognises a quoted string only as the
# WHOLE of an argument: `path="C:/out dir/x.log"` embedded inside the
# comma-separated options string does not stop the parser splitting at the
# space, and QEMU 11 answers "extraneous characters at the end of line" (the
# Phase 20 review's round 2 probed it against a null backend).  So the entire
# `file,id=...,path=...` string is what gets quoted.  A comma in the path
# cannot be carried at all - it ends the option whatever the quoting - and is
# refused with the reason.
function New-ChardevAddCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Path
    )
    if ($Path.Contains(',')) {
        throw ("the chardev log path '{0}' contains a comma, which chardev-add's option string cannot carry; name an -OutDir without one" -f $Path)
    }
    return ("chardev-add " + (ConvertTo-HmpArgument -Text ("file,id={0},path={1}" -f $Id, $Path)))
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
