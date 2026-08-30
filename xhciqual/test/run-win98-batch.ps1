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
foreach ($bat in @("1PROBE.BAT","2XPOLL.BAT","3XIRQ.BAT","4XEMPTY.BAT","5XDEV.BAT")) {
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

# Each case: run a wrapper by name and require its own success line to appear.
# The success text is unique to the wrapper (not XHCIQUAL's own verdict line),
# so a match proves the wrapper took its success branch: exit code bucketed
# with IF ERRORLEVEL and completion flag confirmed with IF EXIST.
$xhci = @("-device","qemu-xhci,id=hc,msi=off,msix=off")
$xhciDev = @("-device","qemu-xhci,id=hc,p3=0,msi=off,msix=off",
             "-drive","if=none,id=ud,file=$usbImage,format=raw",
             "-device","usb-storage,drive=ud,bus=hc.0,port=1")
$cases = @(
    [pscustomobject]@{ Name="irq_selftest"; Batch="3XIRQ";  Args=$xhci;    Success="completed normally with IRQ SELF-TEST PASS"; Fail="self-test FAILED|ended abnormally|was not created" },
    [pscustomobject]@{ Name="empty_full";   Batch="4XEMPTY"; Args=$xhci;    Success="completed normally with a QUALIFIED verdict"; Fail="NOT QUALIFIED|ended abnormally|was not created" },
    [pscustomobject]@{ Name="device_full";  Batch="5XDEV";   Args=$xhciDev; Success="completed normally with a QUALIFIED verdict"; Fail="NOT QUALIFIED|ended abnormally|was not created" },
    [pscustomobject]@{ Name="no_controller"; Batch="4XEMPTY"; Args=@();      Success="completed normally but NOT QUALIFIED"; Fail="ended abnormally|was not created|with a QUALIFIED verdict" },
    [pscustomobject]@{ Name="done_flag_operand"; Batch="PSCAN"; Args=@(); Success="planted"; Fail="PRESCAN_DONE" }
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
    # stays immutable. Success is read from the screen, not this disk. Do not use
    # fat:rw: - that writes back to the host.
    $qargs = @("-machine","pc","-m","64","-boot","a",
               "-drive","if=floppy,file=$BootImage,format=raw",
               "-drive","file=fat:$hddDir,format=raw,if=ide,media=disk,snapshot=on",
               "-display","none",
               "-monitor","tcp:127.0.0.1:$port,server=on,wait=off") + $case.Args
    Write-Host ("[{0}/{1}] {2} ({3})" -f ($index+1),$cases.Count,$case.Name,$case.Batch)
    $process = Start-Process -FilePath $Qemu -ArgumentList $qargs -PassThru -WindowStyle Hidden -RedirectStandardError $stderrPath
    $client = $null; $writer = $null
    try {
        $client = Connect-Monitor $port
        $writer = New-Object IO.StreamWriter($client.GetStream())
        if (-not (Wait-Vga $writer $vgaPath "[A-Z]:\\>" 45)) {
            throw "guest never reached a DOS prompt"
        }
        Send-Text $writer "c:"
        Send-Text $writer "cd \"
        Send-Text $writer $case.Batch
        $hit = Wait-Vga $writer $vgaPath ($case.Success + "|" + $case.Fail) $TimeoutSeconds
        if ($null -eq $hit) {
            $failures += ("{0}: timed out; last screen:`n{1}" -f $case.Name,(Read-Vga $writer $vgaPath))
            Write-Host "  FAIL (timeout)"
        } elseif ($hit -match $case.Success) {
            Write-Host "  PASS"
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
