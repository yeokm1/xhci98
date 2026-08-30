# QEMU discovery and launch for the Phase 10 device matrix.
#
# WHY THIS FILE EXISTS AT ALL.  Every launcher in scripts\local\ hard-codes one
# host's QEMU path (`C:\Users\<someone>\scoop\apps\qemu\current\...`), and this
# repository is synced across several Windows machines that do not agree on it -
# the machine this file was written on has QEMU from winget under
# `C:\Program Files\qemu` and no scoop package at all.  Task 10.5 requires the
# committed harness to take the QEMU path and the image locations as parameters,
# so resolution happens here and nowhere else.
#
# The search order is: an explicit -Qemu argument, then $env:XHCI98_QEMU, then
# PATH, then the two install layouts this project has actually met.  A guess is
# never silent: Resolve-QemuBinary throws with the list it tried.

function Resolve-QemuBinary {
    param(
        [string]$Hint = "",
        [string]$ToolName = "qemu-system-x86_64.exe"
    )
    $tried = @()

    if ($Hint -ne "") {
        # Accept either the executable itself or the directory holding it.
        if (Test-Path -LiteralPath $Hint -PathType Leaf) { return (Resolve-Path -LiteralPath $Hint).Path }
        $candidate = Join-Path $Hint $ToolName
        $tried += $Hint, $candidate
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }

    if ($env:XHCI98_QEMU) {
        $e = $env:XHCI98_QEMU
        if (Test-Path -LiteralPath $e -PathType Leaf) { return (Resolve-Path -LiteralPath $e).Path }
        $candidate = Join-Path $e $ToolName
        $tried += $e, $candidate
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }

    $onPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $tried += "$ToolName on PATH"

    # Each root is tested before Join-Path sees it: an empty root (a 32-bit
    # host has no ProgramFiles(x86)) makes Join-Path throw under Stop, and a
    # guard after the join runs too late.
    $wellKnown = @()
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not [string]::IsNullOrWhiteSpace($root)) { $wellKnown += (Join-Path $root "qemu\$ToolName") }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $wellKnown += (Join-Path $env:USERPROFILE "scoop\apps\qemu\current\$ToolName")
    }
    $wellKnown += "C:\qemu\$ToolName"
    foreach ($w in $wellKnown) {
        $tried += $w
        if (Test-Path -LiteralPath $w) { return $w }
    }

    throw ("Could not find {0}. Pass -Qemu <path>, or set XHCI98_QEMU. Tried:{1}{2}" -f `
        $ToolName, [Environment]::NewLine, (($tried | ForEach-Object { "  $_" }) -join [Environment]::NewLine))
}

# Run a native executable and collect BOTH its streams as one string.
#
# `2>&1` on a native command is not the shell redirection it looks like: in
# Windows PowerShell 5.1 each stderr line becomes an ErrorRecord, and under
# `$ErrorActionPreference = 'Stop'` - which every script in this directory sets -
# the first one aborts the script with an EMPTY message, whatever the exe's exit
# code was.  `scripts\import-gate\check-imports.ps1` documented and relaxed this
# for dumpbin; the vm-matrix callers had the same trap and no relaxation (repo
# audit D4).  QEMU writes to stderr for ordinary things, so this is not
# hypothetical.
#
# The preference is restored in `finally`, so a caller's own error handling is
# unchanged past this call.
function Invoke-NativeText {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $false)][string[]]$Arguments = @()
    )
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $Exe @Arguments 2>&1
    } finally {
        $ErrorActionPreference = $saved
    }
    return ($out | Out-String)
}

function Get-QemuVersion {
    param([Parameter(Mandatory = $true)][string]$Qemu)
    $text = Invoke-NativeText -Exe $Qemu -Arguments @("--version")
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match 'QEMU emulator version (.+)$') { return $Matches[1].Trim() }
    }
    return "unknown"
}

# Does this QEMU build carry the device model the matrix is about to ask for?
# A `device_add` of a model this build does not have is a monitor error late in
# a boot; asking here turns it into one line before anything is launched.
function Get-QemuUsbModels {
    param([Parameter(Mandatory = $true)][string]$Qemu)
    $helpText = Invoke-NativeText -Exe $Qemu -Arguments @("-device", "help")
    $models = @()
    foreach ($line in ($helpText -split "`r?`n")) {
        if ($line -match '^\s*name\s+"([^"]+)",\s*bus usb-bus') { $models += $Matches[1] }
    }
    return ($models | Sort-Object -Unique)
}

# Start QEMU, capturing its stderr, with the arguments quoted.
#
# TWO THINGS Start-Process GETS WRONG IF YOU LET IT.
#
# 1. `-ArgumentList @(...)` joins the array with spaces and quotes NOTHING, so
#    an argument that itself contains a space is split.  `-name "xhci98 device
#    matrix - 2a hid"` arrived at QEMU as six arguments and it died on its
#    command line.  Measured on the harness's first boot attempt.
# 2. Without -RedirectStandardError, QEMU's reason for dying goes nowhere.  The
#    harness then reports "the monitor never answered", which is true, useless,
#    and points at the wrong thing.  Note also that QEMU has two diagnostic
#    channels - `error_vprintf` goes to the CURRENT MONITOR for monitor-caused
#    messages and to stderr only otherwise (task 9-V.2) - so this file catches
#    the startup half and Send-Checked catches the other.
function Start-Qemu {
    param(
        [Parameter(Mandatory = $true)][string]$Qemu,
        [Parameter(Mandatory = $true)][string[]]$QemuArgs,
        [Parameter(Mandatory = $true)][string]$StderrFile
    )
    $quoted = @()
    foreach ($a in $QemuArgs) {
        if ($a -match '[\s"]') { $quoted += ('"' + ($a -replace '"', '\"') + '"') } else { $quoted += $a }
    }
    if (Test-Path -LiteralPath $StderrFile) { Remove-Item -LiteralPath $StderrFile -Force }
    return Start-Process -FilePath $Qemu -ArgumentList $quoted -PassThru -RedirectStandardError $StderrFile
}

function Get-QemuStderr {
    param([Parameter(Mandatory = $true)][string]$StderrFile)
    if (-not (Test-Path -LiteralPath $StderrFile)) { return "" }
    $text = (Get-Content -LiteralPath $StderrFile -Raw)
    if ($null -eq $text) { return "" }
    return $text.Trim()
}

# -----------------------------------------------------------------------------
# THE TRAPS THIS PROJECT HAS ALREADY PAID FOR, enforced rather than documented.
# Task 10.3 names four; each has a function here so the harness fails loudly.
# -----------------------------------------------------------------------------

# TRAP 1: a second -trace argument does not add to the first.  QEMU takes the
# LAST -trace, so `-trace events=X -trace file=Y` silently drops the event list
# and writes nothing useful; the log then looks empty and reads as "the driver
# did nothing".  One -trace with both keys, or none.
function Assert-SingleTraceArg {
    param([Parameter(Mandatory = $true)][string[]]$QemuArgs)
    $n = 0
    for ($i = 0; $i -lt $QemuArgs.Count; $i++) {
        if ($QemuArgs[$i] -eq "-trace") { $n++ }
    }
    if ($n -gt 1) {
        throw ("{0} -trace arguments on one command line. QEMU keeps only the last one, so the events= and the file= must be in the SAME argument." -f $n)
    }
}

# TRAP 2: `screendump foo.png` writes a PPM whatever the extension says.  Name
# the file .ppm and convert, or a later reader opens a "PNG" that is not one.
function Get-ScreendumpPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ($Path -match '\.ppm$') { return $Path }
    return ([IO.Path]::ChangeExtension($Path, ".ppm"))
}

# TRAP 3: a stale offsets.txt.  The counter reader walks the miniport extension
# by byte offset, and those offsets are generated from a build of the driver.
# If the driver has been rebuilt since, every counter read is silently off by
# however much the layout moved - which reads as a wrong VALUE, never as an
# error.  The trace prints the extension's real size; SIZEOF in offsets.txt must
# equal it or the run is void.
function Assert-OffsetsFresh {
    param(
        [Parameter(Mandatory = $true)][string]$OffsetsFile,
        [Parameter(Mandatory = $true)][int]$ExtensionSizeFromTrace
    )
    if (-not (Test-Path -LiteralPath $OffsetsFile)) {
        throw ("offsets file not found: {0}" -f $OffsetsFile)
    }
    $sizeof = $null
    foreach ($line in Get-Content -LiteralPath $OffsetsFile) {
        $parts = $line.Trim() -split '\s+'
        if ($parts.Count -eq 2 -and $parts[0] -eq "SIZEOF") { $sizeof = [int]$parts[1] }
    }
    if ($null -eq $sizeof) {
        throw ("{0} has no SIZEOF line, so its freshness cannot be checked. Regenerate it." -f $OffsetsFile)
    }
    if ($sizeof -ne $ExtensionSizeFromTrace) {
        throw ("STALE OFFSETS: {0} says SIZEOF={1} but the running driver reports MiniPortExtensionSize={2}. Every counter read would be off by the difference. Regenerate offsets before trusting this run." -f `
            $OffsetsFile, $sizeof, $ExtensionSizeFromTrace)
    }
    return $true
}

# TRAP 4: a healthy trace is not a living guest.  Batch 7b-V0 measured a guest
# whose CLOCK HAD STOPPED while its trace kept growing, so "the log is still
# moving" is not a liveness test.
#
# THIS FUNCTION IS CALIBRATED AGAINST BOTH REAL GUESTS RATHER THAN REASONED
# ABOUT, and it took three versions because the first two were wrong in ways
# only a boot could show:
#
#   1. `info rtc` DOES NOT EXIST in QEMU 11 - it answers `unknown command:
#      'info rtc'` - so the original "is the clock advancing" half had never
#      worked.  Worse, that reply is IDENTICAL on both samples, so it compared
#      equal and read as "the clock has stopped": a probe that always votes
#      dead.  The matrix's first row was reported as having killed a guest that
#      was merely idle.
#   2. Sampling EIP does not work EITHER, and it fails DIFFERENTLY ON THE TWO
#      TARGETS, which is why calibrating it on one was not enough.  A healthy
#      idle Windows 98 guest gives 2-6 distinct EIPs over 12 samples; a healthy
#      idle Windows 2000 guest gives **1**, because it idles in a tighter halt
#      loop.  A threshold tuned on 2a therefore declares every 2b row dead.
#      *A liveness probe calibrated on one target is not calibrated.*
#
# What works on both is the guest's INTERRUPTS.  `info irq` counts them per
# line, and IRQ 0 is the PIT at ~100 Hz - so on any live guest the total climbs
# by roughly 150 over 1.5 s, whether the CPU is spinning or halted.  It is also
# precisely the signal batch 7b-V0's failure was described in: a guest whose
# CLOCK HAD STOPPED while its trace kept growing.  A guest that is not taking
# timer interrupts is the wedge this project has actually met.
#
# Note the format: `info irq` prints `<line>: <count>` under one or more
# controller headings ("IRQ statistics for ioapic:", "... for isa-i8259:"), and
# the same interrupt is counted under each, so the SUM is not a physical
# interrupt count.  That does not matter - only that it advances - but it is
# why this returns the delta rather than a rate.
#
# WHAT IT DOES NOT CLAIM.  It says the guest is executing and taking
# interrupts.  It does not say the OS is responsive: a guest whose PnP tree is
# wedged (batch 7b-V0; batch 9-V run 5) takes timer interrupts perfectly well,
# and so does one sitting on a modal Add New Hardware Wizard.  That is why the
# matrix's evidence is counter movement and not this function, and why a row
# that fails takes a screenshot.

# WHAT BECAME OF THE PROCESS, for any probe that has found the monitor absent.
# Every caller of this had been inferring it: the counter reader called a dead
# process a short reply, the liveness probe called it a stopped guest, and the
# group-failure path recorded neither.  The answer is one property of a handle
# the runner holds throughout, so none of them has to guess.  It lives here
# rather than in counters.ps1 because it is a fact about QEMU, and because
# probe-devices.ps1 dot-sources this file without that one.
function Get-ProcessStateText {
    param($Process)
    if ($null -eq $Process) {
        return "no QEMU process handle was passed, so whether the process is alive was not established"
    }
    try {
        if ($Process.HasExited) {
            # AN EXIT CODE THAT IS NOT THERE MUST SAY SO.  `Start-Process
            # -PassThru` hands back a Process object whose ExitCode can come back
            # as $null - measured here on a process killed from outside this
            # session - and `-f $null` formats as an empty string, so the first
            # version of this line printed `(exit code )`.  A blank inside
            # parentheses reads as data.
            $code = $null
            try { $code = $Process.ExitCode } catch { }
            $codeText = if ($null -eq $code) {
                "its exit code was not available from the handle"
            } else {
                ("exit code {0}" -f $code)
            }
            return ("the QEMU process has EXITED ({0}), so the guest was not bugchecked or wedged - it stopped existing, which is a host-side failure" -f $codeText)
        }
        return "the QEMU process is still running, so this is a wedged or unreachable monitor rather than a dead process"
    } catch {
        return "the QEMU process handle could not be queried"
    }
}

#
# AND WHAT IT MUST NOT SAY.  "The guest stopped executing" is a claim about a
# guest, and it needs a guest to be about.  If the QEMU process has gone there is
# nothing on the monitor port, every probe below returns nothing, and this
# function used to report `Alive = $false` with an empty status and `from 0
# lines` - which the runner printed as "the guest stopped executing while this
# device was attached".  That is the same wrong reading the counter reader made
# on the 2a `audio` group, in a second place, and it was found by deliberately
# killing QEMU mid-row to demonstrate the fix to the first one.  The comment
# below about an unparseable reply being "an unknown, not a dead guest" was
# already the right rule and the return value did not keep it: an unknown and an
# absence both came out as death.  So the reason comes back with the verdict, and
# the caller states the reason rather than assuming the commonest one.
function Test-GuestAlive {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$SampleMs = 1500,
        $Process = $null
    )
    if (Test-MonitorPortFree -Port $Port) {
        return [pscustomobject]@{
            Alive     = $false
            Verdict   = "unreachable"
            Why       = ("the QEMU monitor is gone - nothing is listening on the port, so there is no guest to probe. {0}" -f (Get-ProcessStateText -Process $Process))
            Running   = $false
            Parseable = $false
            IrqDelta  = 0
            Detail    = "no probe was taken"
        }
    }

    $status = (Get-MonitorText -Port $Port -Command "info status") -join " "
    $running = ($status -match '(?i)running')

    function Get-IrqTotal {
        param([int]$P)
        $text = (Get-MonitorText -Port $P -Command "info irq")
        $sum = [int64]0
        $lines = 0
        foreach ($l in $text) {
            if ($l -match '^\s*(\d+):\s*(\d+)\s*$') { $sum += [int64]$Matches[2]; $lines++ }
        }
        return @($sum, $lines)
    }

    $a = Get-IrqTotal -P $Port
    Start-Sleep -Milliseconds $SampleMs
    $b = Get-IrqTotal -P $Port
    $delta = $b[0] - $a[0]

    # No parseable lines at all means `info irq` did not answer the way this
    # function expects - a QEMU that renamed it, a machine type with no PIC.
    # That is an unknown, not a dead guest, and it must not be reported as one.
    $parsed = ($a[1] -gt 0 -and $b[1] -gt 0)

    # Three ways to not be alive, and they are three different findings.
    $verdict = "alive"
    $why = "the guest is executing and taking timer interrupts"
    if (-not $parsed) {
        $verdict = "unknown"
        $why = "`info irq` did not answer in the form this probe reads, so whether the guest is executing was NOT established - this is an unknown, not a dead guest"
    } elseif (-not $running) {
        $verdict = "not-executing"
        $why = "QEMU reports the VM is not running - it is paused or stopped, not merely idle"
    } elseif ($delta -le 0) {
        $verdict = "not-executing"
        $why = "the guest took no timer interrupts across the sample, which is the wedge shape batch 7b-V0 met - a stopped clock behind a healthy-looking trace"
    }

    return [pscustomobject]@{
        Alive     = ($verdict -eq "alive")
        Verdict   = $verdict
        Why       = $why
        Running   = $running
        Parseable = $parsed
        IrqDelta  = $delta
        Detail    = ("status: {0}; irq total {1} -> {2} (delta {3}) over {4} ms from {5} lines" -f `
                     $status, $a[0], $b[0], $delta, $SampleMs, $b[1])
    }
}

# A screenshot of the guest, converted to PNG.
#
# WHY THE HARNESS TAKES ONE ON EVERY FAILURE.  A counter that did not move says
# the device did not work; it does not say why, and the two commonest whys on
# these targets are invisible to every probe above.  Windows 98 puts up a MODAL
# "Add New Hardware Wizard" for any device class it has not seen before, which
# blocks the bind indefinitely - measured here as `endpoints opened` frozen at 0
# for ten minutes with the CPU perfectly alive - and batch 9-V recorded that the
# same dialog "reads exactly like a driver that failed to load".  One picture
# separates those in a second.
#
# `screendump foo.png` writes a PPM WHATEVER the extension says, so the file is
# named .ppm and converted here.  A reader handed a "PNG" that is not one is the
# trap this project already paid for.
function Save-GuestScreenshot {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $ppm = Get-ScreendumpPath -Path $Path
    $png = [IO.Path]::ChangeExtension($ppm, ".png")
    if (Test-Path -LiteralPath $ppm) { Remove-Item -LiteralPath $ppm -Force }
    # Quoted when the path carries a space: HMP splits on whitespace, and the
    # tree this file lives in is synced across machines whose paths differ.
    Send-Mon -Port $Port -Command ("screendump " + (ConvertTo-HmpArgument -Text $ppm)) -Reply | Out-Null
    for ($i = 0; $i -lt 20; $i++) {
        if (Test-Path -LiteralPath $ppm) { break }
        Start-Sleep -Milliseconds 250
    }
    if (-not (Test-Path -LiteralPath $ppm)) { return $null }

    try {
        Add-Type -AssemblyName System.Drawing
        $fs = [IO.File]::OpenRead((Resolve-Path -LiteralPath $ppm).Path)
        $br = New-Object IO.BinaryReader($fs)
        $tok = {
            $s = ""
            while ($true) {
                $c = [char]$br.ReadByte()
                if ($c -match '\s') { if ($s.Length -gt 0) { return $s } } else { $s += $c }
            }
        }
        $null = & $tok            # P6
        $w = [int](& $tok); $h = [int](& $tok); $null = & $tok
        $data = $br.ReadBytes($w * $h * 3)
        $br.Close(); $fs.Close()

        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
        $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
                            [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $row = New-Object byte[] ($w * 3)
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $i = ($y * $w + $x) * 3
                # PPM is RGB; GDI+ Format24bppRgb is BGR.
                $row[$x * 3]     = $data[$i + 2]
                $row[$x * 3 + 1] = $data[$i + 1]
                $row[$x * 3 + 2] = $data[$i]
            }
            [Runtime.InteropServices.Marshal]::Copy($row, 0,
                [IntPtr]($bd.Scan0.ToInt64() + $y * $bd.Stride), $row.Length)
        }
        $bmp.UnlockBits($bd)
        $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Remove-Item -LiteralPath $ppm -Force -ErrorAction SilentlyContinue
        return $png
    } catch {
        # The PPM is still evidence even if the conversion failed; keep it.
        return $ppm
    }
}
