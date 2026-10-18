# run-win98-batch.ps1 - validate the DOS batch wrappers on the real target
# shell, MS-DOS 7.1 (Windows 98).
#
# run-qemu-matrix.ps1 drives XHCIQUAL directly; this harness instead drives the
# .BAT field wrappers by name, so it validates their shell logic - the exit-code
# bucketing (IF ERRORLEVEL, since MS-DOS COMMAND.COM has no %ERRORLEVEL%) and
# the completion check (IF EXIST on --done-flag, so no FIND.EXE). It boots the
# same bare Win98SE floppy, stages XHCIQUAL.EXE plus the wrappers on a FAT C:
# drive, presents a qemu-xhci controller, runs a wrapper by name, and checks
# that the wrapper reaches its own success branch on screen.
#
# The Win98 boot image is proprietary and cannot be committed; supply it at
# tools\w98se.img (see xhciqual/README.md). If it is absent this script skips.

param(
    [string]$Qemu = "",
    [string]$BootImage = "",
    [string]$BootImageUrl = "",
    [int]$TimeoutSeconds = 60,
    [string[]]$CaseName = @()
)

$ErrorActionPreference = "Stop"
$testDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$qualDir = Split-Path -Parent $testDir
$repo = Split-Path -Parent $qualDir
$hddDir = Join-Path $testDir "win98hdd"
$usbImage = Join-Path $testDir "usb.img"
if ($BootImage -eq "") { $BootImage = Join-Path $repo "tools\w98se.img" }
if ([string]::IsNullOrWhiteSpace($Qemu)) {
    $qemuCommand = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
    if ($null -ne $qemuCommand) {
        $Qemu = $qemuCommand.Source
    } else {
        $Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
    }
}
if (-not (Test-Path -LiteralPath $Qemu)) { throw "QEMU not found: $Qemu" }
# Some process spawners inject duplicate case-variant PATH keys (both "Path" and
# "PATH"); collapse them to a single canonical value so the child QEMU process
# sees a well-formed environment. Done AFTER the Get-Command lookup above so the
# sanitize cannot defeat PATH-based resolution of $Qemu.
$pathKeys = @([Environment]::GetEnvironmentVariables().Keys |
    Where-Object { $_ -ieq "Path" })
if ($pathKeys.Count -gt 1) {
    [Environment]::SetEnvironmentVariable(
        "PATH", $env:PATH, [EnvironmentVariableTarget]::Process)
}
# Proprietary, not committed: fetch from a user-configured source if missing.
. (Join-Path $testDir "win98-image.ps1")
# Resolve before use: this same path is handed to QEMU below, and QEMU cannot
# open PowerShell-only path forms (PSDrive qualifiers, ~).
$BootImage = Resolve-Win98ImagePath $BootImage
if (-not (Ensure-Win98Image -BootImage $BootImage -Repo $repo -Url $BootImageUrl)) { exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $qualDir "xhciqual.exe"))) {
    throw "Build xhciqual.exe first (xhciqual\build.cmd)"
}

# Stage C: with the field EXE, its MAP, and the batch wrappers normalized to
# CRLF. .gitattributes (eol=crlf) and check-bat-eol.ps1 already keep the
# working tree CRLF, so this is a belt-and-braces conversion for a copy that
# arrived some other way; COMMAND.COM needs CRLF for goto labels.
if (Test-Path -LiteralPath $hddDir) { Remove-Item -LiteralPath $hddDir -Recurse -Force }
New-Item -ItemType Directory -Path $hddDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $qualDir "xhciqual.exe") -Destination (Join-Path $hddDir "XHCIQUAL.EXE") -Force
if (Test-Path -LiteralPath (Join-Path $qualDir "xhciqual.map")) {
    Copy-Item -LiteralPath (Join-Path $qualDir "xhciqual.map") -Destination (Join-Path $hddDir "XHCIQUAL.MAP") -Force
}
foreach ($bat in @("1PROBE.BAT","2XPOLL.BAT","3XIRQ.BAT","4XEMPTY.BAT","5XDEV.BAT","QUALREP.BAT")) {
    $src = Join-Path $qualDir $bat
    if (Test-Path -LiteralPath $src) {
        $text = [IO.File]::ReadAllText($src) -replace "`r`n","`n" -replace "`n","`r`n"
        [IO.File]::WriteAllText((Join-Path $hddDir $bat), $text)
    }
}
# A --log filename named "done-flag" is still the log operand. Preserve the
# following token to prove the completion pre-scan does not reinterpret it.
$prescanBat = @(
    "@echo off",
    "if exist ZQREV.TMP del ZQREV.TMP",
    "echo planted>ZQREV.TMP",
    "XHCIQUAL --log done-flag ZQREV.TMP",
    "cls",
    "type ZQREV.TMP",
    "echo PRESCAN_DONE",
    "if exist ZQREV.TMP del ZQREV.TMP",
    "if exist done-flag del done-flag"
)
$prescanText = ($prescanBat -join "`r`n") + "`r`n"
[IO.File]::WriteAllText((Join-Path $hddDir "PSCAN.BAT"), $prescanText)
# Task 38.1: QUALREP twice in one boot, with the guest clock pinned (-rtc
# below) so the names are known: the second run in the same minute takes
# the next letter, and DIR /B lists both, in creation order.
$qrTwice = @(
    "@echo off",
    "call QUALREP",
    "call QUALREP",
    "cls",
    "dir /b A1314*.LOG",
    "echo QRTWICE_DONE"
)
[IO.File]::WriteAllText((Join-Path $hddDir "QRTWICE.BAT"), (($qrTwice -join "`r`n") + "`r`n"))
# The 38.1 MS-DOS leg found --log-stamp logs of 0 bytes behind a screen that
# said "Log written": the screen alone cannot prove the file. So QUALREP
# runs, then a plain --log run of the same probe, and both files are copied
# to COM1 (a QEMU file backend), read back on the host and compared: the
# stamped log must be the plain one plus its own two lines.
$qrLog = @(
    "@echo off",
    "call QUALREP",
    "XHCIQUAL --probe-only --no-page --log P.LOG",
    "cls",
    "echo =STAMP=>COM1",
    "copy /b A1314*.LOG COM1",
    "echo =PLAIN=>COM1",
    "copy /b P.LOG COM1",
    "echo =END=>COM1",
    "echo QRLOG_DONE"
)
[IO.File]::WriteAllText((Join-Path $hddDir "QRLOG.BAT"), (($qrLog -join "`r`n") + "`r`n"))
# ...and the refusal: every letter of 31 December 23:58 and 23:59 already
# taken, so a run in either minute must refuse and write nothing.
foreach ($minute in @("58","59")) {
    foreach ($letter in [char[]](65..90)) {
        [IO.File]::WriteAllText((Join-Path $hddDir ("C3123" + $minute + $letter + ".LOG")), "planted`r`n")
    }
}

# Task 38.1: QUALREP on read-only media. A second floppy,
# B:, holds only XHCIQUAL.EXE and QUALREP.BAT and is presented read-only,
# which the floppy controller reports to DOS as a write-protected disk. DOS
# then asks Abort, Retry, Fail for each write. qualrep_rdonly answers Fail:
# XHCIQUAL must say it cannot write here, and QUALREP add nothing.
# qualrep_abort answers Abort, which until DOS-1 ended XHCIQUAL from inside
# DOS with exit code 0 (measured 2026-10-13); XHCIQUAL now hands DOS a Fail
# for it, so the screen must be the Fail one: "cannot write here", and
# neither QUALREP's stopped branch nor its cold-boot one.
$roDir = Join-Path $hddDir "RODISK"   # under the git-ignored staging dir
if (Test-Path -LiteralPath $roDir) { Remove-Item -LiteralPath $roDir -Recurse -Force }
New-Item -ItemType Directory -Path $roDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $hddDir "XHCIQUAL.EXE") -Destination (Join-Path $roDir "XHCIQUAL.EXE") -Force
Copy-Item -LiteralPath (Join-Path $hddDir "QUALREP.BAT") -Destination (Join-Path $roDir "QUALREP.BAT") -Force
# DOS-1 (38.1 MS-DOS leg): before XHCIQUAL answered Fail for an Abort, an
# Abort ended it from inside DOS, past DOS/32A's teardown, and the next
# XHCIQUAL in that boot hung at the extender banner. One QUALREP per boot
# cannot see that, so this one answers Abort and then runs XHCIQUAL again.
$qrAbortRerun = @(
    "@echo off",
    "call QUALREP",
    "cls",
    "XHCIQUAL --probe-only --no-page --no-sysinfo",
    "echo QRABRT_DONE"
)
[IO.File]::WriteAllText((Join-Path $roDir "QRABRT.BAT"), (($qrAbortRerun -join "`r`n") + "`r`n"))

if (-not (Test-Path -LiteralPath $usbImage)) {
    $stream = [IO.File]::Create($usbImage); $stream.SetLength(8MB); $stream.Close()
}

function Send-Hmp($writer, [string]$command) {
    $writer.WriteLine($command); $writer.Flush(); Start-Sleep -Milliseconds 40
}
function Send-Key($writer, [string]$key) { Send-Hmp $writer ("sendkey " + $key) }
function Send-Text($writer, [string]$value) {
    foreach ($ch in $value.ToCharArray()) {
        if ($ch -eq " ") { $key = "spc" }
        elseif ($ch -eq "-") { $key = "minus" }
        elseif ($ch -eq ":") { $key = "shift-semicolon" }
        elseif ($ch -eq "\") { $key = "backslash" }
        else { $key = ([string]$ch).ToLowerInvariant() }  # sendkey needs lowercase keynames; DOS is case-insensitive
        Send-Key $writer $key
    }
    Send-Key $writer "ret"
}
function Read-Vga($writer, [string]$path) {
    $dump = $path.Replace([char]92, [char]47)
    Send-Hmp $writer ("pmemsave 0xb8000 4000 " + $dump)
    Start-Sleep -Milliseconds 120
    if (-not (Test-Path -LiteralPath $path)) { return "" }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 4000) { return "" }
    $lines = @()
    for ($row = 0; $row -lt 25; $row++) {
        $chars = for ($col = 0; $col -lt 80; $col++) {
            $value = $bytes[($row * 80 + $col) * 2]
            if ($value -ge 32 -and $value -lt 127) { [char]$value } else { " " }
        }
        $lines += (-join $chars).TrimEnd()
    }
    return ($lines -join [Environment]::NewLine)
}
function Wait-Vga($writer, [string]$path, [string]$pattern, [int]$seconds) {
    $end = [DateTime]::UtcNow.AddSeconds($seconds)
    do {
        $text = Read-Vga $writer $path
        if ($text -match $pattern) { return $text }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $end)
    return $null
}
function Connect-Monitor([int]$port) {
    $end = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $client = New-Object Net.Sockets.TcpClient
        try { $client.Connect("127.0.0.1", $port); return $client }
        catch { $client.Close(); Start-Sleep -Milliseconds 100 }
    } while ([DateTime]::UtcNow -lt $end)
    throw "QEMU monitor did not open"
}

# QRLOG's serial capture: the stamped log, then the plain --log one. Returns
# $null when the stamped log is the whole report, or what is wrong.
function Test-StampedLog([string]$serial) {
    $m = [regex]::Match($serial, "=STAMP=\r?\n([\s\S]*?)=PLAIN=\r?\n([\s\S]*?)=END=")
    if (-not $m.Success) { return "the serial capture has no =STAMP= / =PLAIN= / =END= sections" }
    $stamp = $m.Groups[1].Value
    $plain = $m.Groups[2].Value
    if ($stamp.Length -eq 0) { return "the stamped log is empty (0 bytes)" }
    if ($plain.Length -eq 0) { return "the plain --log log is empty" }
    if ($stamp -match "(?<!\r)\n") { return "the stamped log has a line not ended by CRLF" }
    $s = @($stamp.TrimEnd("`r", "`n") -split "`r`n")
    $p = @($plain.TrimEnd("`r", "`n") -split "`r`n")
    if ($s[0] -notmatch '^XHCIQUAL \S+ \(build .*\) - USB host controller qualification$') { return "the stamped log's first line is not the report header: " + $s[0] }
    if ($s.Count -lt 2 -or $s[1] -notmatch '^Log: A1314[0-9]{2}A\.LOG, begun 2026-10-13 14:[0-9]{2}:[0-9]{2} by the DOS clock$') { return "the stamped log's second line is not its Log: line" }
    if (-not ($s -match '^---- Verdict for ')) { return "the stamped log has no verdict section" }
    if (-not ($s -contains 'Probe safety: PASS - no PCI configuration writes.')) { return "the stamped log has no probe safety line" }
    if ($s[-1] -notmatch '^Log written: A1314[0-9]{2}A\.LOG - attach this file$') { return "the stamped log does not end on its Log written line: " + $s[-1] }
    if ($s[-2] -cne 'Done.' -or $p[-1] -cne 'Done. Report copied to P.LOG.') { return "the logs do not end as expected" }
    # The same report: the stamped log without its Log: line and its ending,
    # and the plain one without its ending, line for line.
    $sBody = @($s[0]) + @($s[2..($s.Count - 3)])
    $pBody = @($p[0..($p.Count - 2)])
    $diff = @(Compare-Object -ReferenceObject $pBody -DifferenceObject $sBody -SyncWindow 0 -CaseSensitive)
    if ($diff.Count -ne 0 -or $sBody.Count -ne $pBody.Count) {
        return ("the stamped log is not the --log report: first difference " + ($diff | Select-Object -First 2 | Out-String))
    }
    return $null
}

# Each case: run a wrapper by name and require its own success line to appear.
# The success text is unique to the wrapper (not XHCIQUAL's own verdict line),
# so a match proves the wrapper took its success branch: exit code bucketed
# with IF ERRORLEVEL and completion flag confirmed with IF EXIST.
$xhci = @("-device","qemu-xhci,id=hc,msi=off,msix=off")
$xhciDev = @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off",
             "-drive","if=none,id=ud,file=$usbImage,format=raw",
             "-device","usb-storage,drive=ud,bus=hc.0,port=1")
$roFloppy = @("-drive","if=floppy,index=1,file=fat:floppy:$roDir,format=raw,readonly=on")
# Answer: a screen pattern that wants a key, and the key. QUALREP ends with
# PAUSE, so a case that runs it more than once must press a key for each.
$pauseKey = @{ Pattern = "Press any key to continue"; Key = "ret" }
$failKey = @{ Pattern = "Abort, Retry, Fail\?"; Key = "f" }
$abortKey = @{ Pattern = "Abort, Retry, Fail\?"; Key = "a" }
# QUALREP's last line is the one naming the file to attach, then its pause
# (owner, 2026-10-13); each QUALREP case holds it to that.
$qrPause = "\s*Press any key to continue"
$qrAny = "ended abnormally|stopped before it finished|cannot write here|refused|unexpected exit code|NOT complete"
$qrPin = @("-rtc","base=2026-10-13T14:25:00,clock=vm")
$cases = @(
    [pscustomobject]@{ Name="irq_selftest"; Batch="3XIRQ";  Args=$xhci;    Success="completed normally with IRQ SELF-TEST PASS"; Fail="self-test FAILED|ended abnormally|was not created" },
    [pscustomobject]@{ Name="empty_full";   Batch="4XEMPTY"; Args=$xhci;    Success="completed normally with a QUALIFIED verdict"; Fail="NOT QUALIFIED|ended abnormally|was not created" },
    [pscustomobject]@{ Name="device_full";  Batch="5XDEV";   Args=$xhciDev; Success="completed normally with a QUALIFIED verdict"; Fail="NOT QUALIFIED|ended abnormally|was not created" },
    [pscustomobject]@{ Name="no_controller"; Batch="4XEMPTY"; Args=@();      Success="completed normally but NOT QUALIFIED"; Fail="ended abnormally|was not created|with a QUALIFIED verdict" },
    [pscustomobject]@{ Name="done_flag_operand"; Batch="PSCAN"; Args=@(); Success="planted"; Fail="PRESCAN_DONE" },
    [pscustomobject]@{ Name="qualrep_read"; Batch="QUALREP"; Args=$xhci; Success=("Log written: [0-9A-C][0-9]{6}[A-Z]\.LOG - attach this file" + $qrPause); Fail=$qrAny },
    [pscustomobject]@{ Name="qualrep_log"; Batch="QRLOG"; Answer=@($pauseKey); Serial=$true; Args=($xhci + $qrPin); Success="QRLOG_DONE"; Fail=$qrAny },
    [pscustomobject]@{ Name="qualrep_twice"; Batch="QRTWICE"; Answer=@($pauseKey); Args=($xhci + $qrPin); Success="A1314([0-9][0-9])A\.LOG\s+A1314(\1B|[0-9][0-9]A)\.LOG\s+QRTWICE_DONE"; Fail=$qrAny },
    [pscustomobject]@{ Name="qualrep_all_taken"; Batch="QUALREP"; Args=($xhci + @("-rtc","base=2026-12-31T23:58:00,clock=vm")); Success=("C31235[89]A\.LOG to C31235[89]Z\.LOG all[\s\S]*No log was written by this run, so it has no file to attach\." + $qrPause); Fail="Log written|ended abnormally|stopped before it finished|cannot write here" },
    [pscustomobject]@{ Name="qualrep_rdonly"; Batch="QUALREP"; Drive="b:"; Answer=@($failKey); Args=($xhci + $roFloppy); Success=("XHCIQUAL: cannot write here\. --log-stamp could not create [0-9A-C][0-9]{6}A\.LOG[\s\S]*nothing needs a cold\s+boot[\s\S]*No log was written, so there is no file to attach\." + $qrPause); Fail="ended abnormally|stopped before it finished|Log written|warning: could not create" },
    [pscustomobject]@{ Name="qualrep_abort"; Batch="QUALREP"; Drive="b:"; Answer=@($abortKey); Args=($xhci + $roFloppy); Success=("Abort, Retry, Fail\?a[\s\S]*XHCIQUAL: cannot write here\. --log-stamp could not create [0-9A-C][0-9]{6}A\.LOG[\s\S]*No log was written, so there is no file to attach\." + $qrPause); Fail="ended abnormally|stopped before it finished|Log written|warning: could not create" },
    [pscustomobject]@{ Name="qualrep_abort_rerun"; Batch="QRABRT"; Drive="b:"; Answer=@($abortKey, $pauseKey); Args=($xhci + $roFloppy); Success="Probe safety: PASS - no PCI configuration writes\.\s+Done\.\s+QRABRT_DONE"; Fail="ended abnormally|stopped before it finished|Log written" }
)
if ($CaseName.Count -ne 0) {
    $cases = @($cases | Where-Object { $_.Name -in $CaseName })
    if ($cases.Count -eq 0) { throw "No requested cases matched" }
}

$failures = @()
for ($index = 0; $index -lt $cases.Count; $index++) {
    $case = $cases[$index]
    $port = 20500 + ([Diagnostics.Process]::GetCurrentProcess().Id % 20000) + $index
    $vgaPath = Join-Path $testDir ("vga-win98-" + $case.Name + ".bin")
    $stderrPath = Join-Path $testDir ("stderr-win98-" + $case.Name + ".log")
    Remove-Item -LiteralPath $vgaPath,$stderrPath -Force -ErrorAction SilentlyContinue
    # Read-only VVFAT backing + per-drive snapshot: guest writes (logs, ZQREV.TMP,
    # done-flag) land in a throwaway overlay, so the host win98hdd\ staging dir
    # stays immutable. Success is read from the screen, not this disk, except
    # where a case copies a file to COM1 (qualrep_log), whose capture is read
    # back on the host. Do not use fat:rw: - that writes back to the host.
    $qargs = @("-machine","pc","-m","64","-boot","a",
               "-drive","if=floppy,file=$BootImage,format=raw",
               "-drive","file=fat:$hddDir,format=raw,if=ide,media=disk,snapshot=on",
               "-display","none",
               "-monitor","tcp:127.0.0.1:$port,server=on,wait=off") + $case.Args
    $serialPath = $null
    if ($case.PSObject.Properties.Name -contains "Serial") {
        $serialPath = Join-Path $testDir ("serial-win98-" + $case.Name + ".log")
        Remove-Item -LiteralPath $serialPath -Force -ErrorAction SilentlyContinue
        $qargs += @("-serial", ("file:" + $serialPath))
    }
    Write-Host ("[{0}/{1}] {2} ({3})" -f ($index+1),$cases.Count,$case.Name,$case.Batch)
    $process = Start-Process -FilePath $Qemu -ArgumentList $qargs -PassThru -WindowStyle Hidden -RedirectStandardError $stderrPath
    $client = $null; $writer = $null
    try {
        $client = Connect-Monitor $port
        $writer = New-Object IO.StreamWriter($client.GetStream())
        if (-not (Wait-Vga $writer $vgaPath "[A-Z]:\\>" 45)) {
            throw "guest never reached a DOS prompt"
        }
        $drive = "c:"
        if ($case.PSObject.Properties.Name -contains "Drive") { $drive = $case.Drive }
        Send-Text $writer $drive
        Send-Text $writer "cd \"
        Send-Text $writer $case.Batch
        $answers = @()
        if ($case.PSObject.Properties.Name -contains "Answer") { $answers = @($case.Answer) }
        if ($answers.Count -eq 0) {
            $hit = Wait-Vga $writer $vgaPath ($case.Success + "|" + $case.Fail) $TimeoutSeconds
        } else {
            # A prompt stays on screen after it is answered, so a key is sent
            # at most once in 1.5 s; a spare key reaches the prompt or PAUSE.
            $hit = $null
            $lastKey = [DateTime]::MinValue
            $end = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            do {
                $text = Read-Vga $writer $vgaPath
                if ($text -match $case.Success -or $text -match $case.Fail) { $hit = $text; break }
                foreach ($a in $answers) {
                    if ($text -match $a.Pattern -and ([DateTime]::UtcNow - $lastKey).TotalSeconds -ge 1.5) {
                        Send-Key $writer $a.Key
                        $lastKey = [DateTime]::UtcNow
                    }
                }
                Start-Sleep -Milliseconds 300
            } while ([DateTime]::UtcNow -lt $end)
        }
        if ($null -eq $hit) {
            $failures += ("{0}: timed out; last screen:`n{1}" -f $case.Name,(Read-Vga $writer $vgaPath))
            Write-Host "  FAIL (timeout)"
        } elseif ($hit -match $case.Success) {
            $logFault = $null
            if ($null -ne $serialPath) {
                # The guest has copied the logs to COM1 by the time it
                # prints the done line; QEMU flushes the file on quit.
                Send-Hmp $writer "quit"
                if (-not $process.WaitForExit(10000)) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
                $serialText = ""
                if (Test-Path -LiteralPath $serialPath) {
                    $serialText = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($serialPath))
                }
                $logFault = Test-StampedLog $serialText
            }
            if ($null -eq $logFault) {
                Write-Host "  PASS"
            } else {
                $failures += ("{0}: the log read back from the guest: {1}" -f $case.Name,$logFault)
                Write-Host ("  FAIL (" + $logFault + ")")
            }
        } else {
            $failures += ("{0}: wrapper reported a non-success branch:`n{1}" -f $case.Name,$hit)
            Write-Host "  FAIL"
        }
    } catch {
        $failures += ("{0}: {1}" -f $case.Name,$_.Exception.Message)
        Write-Host ("  ERROR: " + $_.Exception.Message)
    } finally {
        if ($writer) { try { Send-Hmp $writer "quit" } catch {} ; $writer.Dispose() }
        if ($client) { $client.Close() }
        if ($process -and -not $process.HasExited) {
            Start-Sleep -Milliseconds 300
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        }
    }
}

Write-Host ""
if ($failures.Count -eq 0) {
    Write-Host ("Win98 batch: {0} passed, 0 failed" -f $cases.Count)
    exit 0
} else {
    foreach ($f in $failures) { Write-Host ("--- " + $f) }
    Write-Host ("Win98 batch: {0} passed, {1} failed" -f ($cases.Count-$failures.Count),$failures.Count)
    exit 1
}
