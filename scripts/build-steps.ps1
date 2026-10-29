<#
.SYNOPSIS
The checks of a gated build that need no DDK, run at once (roadmap-usb4
task 45.3).

.DESCRIPTION
docs\contributing\build-and-test.md, "A faster gated build", is the design.
scripts\build-driver.cmd runs its text checks first and then hands this
script the block that needs no DDK: the import gate's self-tests, the INF,
UAS, CM, Stor98 and txtsetup.oem gates with their self-tests, the
packager's, the QEMU launchers' and the VM matrix's self-tests, the Stor98
header check, on the x86 run XHCITR and XHCIPERF rebuilt and their
self-tests, the XHCIINST, XHCIMARK and XHCIDI self-tests when their EXEs
are there, the host suite and on the x86 run Stor98's unit suite.

Each step is a process of its own with its own TEMP and TMP
(out\steps-<arch>\tmp\<step>\) and its own log (out\steps-<arch>\
<step>.log), started from the repository root. A step that depends on
another waits for it to pass; at most XHCI98_BUILD_STEPS run at once
(default 4; 1 runs them one at a time in the table's order). When a step
ends its log is printed whole under its heading, with its result and
time; after the block, a table of every step in the table's order.

After a failure no further step starts and the running ones are left to
finish. Exit 0 when every step passed or was passed over, 1 when one
failed, 2 when the host suite alone was inconclusive (its exit 2: a
freshly linked EXE blocked, not a test failure).

.PARAMETER Arch
The build's architecture, x86 or amd64: the x86 run alone rebuilds XHCITR
and XHCIPERF and runs Stor98's unit suite.

.PARAMETER SelfTest
Run the runner over stand-in steps instead, and check what it does.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\build-steps.ps1 -Arch x86
#>

[CmdletBinding()]
param(
    [ValidateSet("x86", "amd64")]
    [string]$Arch = "x86",
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$psExe = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"

function New-Step {
    # Id: the log's and the temporary folder's name. Run: the command line
    # cmd.exe runs. After: the ids that must pass first. IfExists: a path,
    # under the repository, that must be there when the step is due, or the
    # step is passed over. Inconclusive: an exit code read as inconclusive.
    param([string]$Id, [string]$Title, [string]$Run, [string]$Hint,
          [string[]]$After = @(), [string]$IfExists = "", [int]$Inconclusive = -1)
    return [pscustomobject]@{
        Id = $Id; Title = $Title; Run = $Run; Hint = $Hint; After = @($After)
        IfExists = $IfExists; Inconclusive = $Inconclusive
        State = "pending"; Code = $null; Elapsed = [TimeSpan]::Zero
        Process = $null; Watch = $null; Log = ""
    }
}

function Get-PsRun {
    param([string]$Script, [string]$Arguments = "")
    $line = '"{0}" -NoProfile -ExecutionPolicy Bypass -File "{1}"' -f $psExe, (Join-Path $repo $Script)
    if ($Arguments -ne "") { $line += " " + $Arguments }
    return $line
}

function Get-CmdRun {
    param([string]$Script, [string]$Arguments = "")
    $line = 'call "{0}"' -f (Join-Path $repo $Script)
    if ($Arguments -ne "") { $line += " " + $Arguments }
    return $line
}

function Get-BuildSteps {
    # The block: the five longest first, then the rest in build-driver.cmd's
    # old order. The hints are its old failure labels' texts.
    param([string]$Arch)
    $infFail = "failed the setup-engine gate. Do not install it - Win98's setup engine has no log, and a Win2000 install that creates no service looks the same in Device Manager as a driver that loaded and failed."
    $infTest = "the INF gate's own self-tests failed, so its verdict on the INFs cannot be trusted either. Fix the gate first."
    $gateTest = "the import gate's self-tests failed."
    $s = @()
    $s += New-Step "import-manifests" "import gate self-tests: evidence manifests" (Get-PsRun "scripts\import-gate\test-evidence-manifests.ps1") $gateTest
    $s += New-Step "import-flavours" "import gate self-tests: the FLAVORS column" (Get-PsRun "scripts\import-gate\test-flavour-rules.ps1") $gateTest
    $s += New-Step "helper-build" "import gate self-tests: the helper's toolchain rules" (Get-PsRun "scripts\check-helper-build.ps1" "-SelfTest") $gateTest
    $s += New-Step "inf-selftest" "INF gate self-tests" (Get-PsRun "scripts\inf-gate\test-inf-checks.ps1") $infTest
    $s += New-Step "inf-x86" "INF gate (x86)" (Get-PsRun "scripts\inf-gate\check-inf.ps1" "-Arch x86") ("src\xhci98.inf " + $infFail) @("inf-selftest")
    $s += New-Step "inf-amd64" "INF gate (amd64)" (Get-PsRun "scripts\inf-gate\check-inf.ps1" "-Arch amd64") ("src\xhci98-amd64.inf " + $infFail) @("inf-selftest")
    $s += New-Step "uas-inf-selftest" "UAS INF gate self-tests" (Get-PsRun "scripts\inf-gate\check-uas-inf.ps1" "-SelfTest") $infTest
    $s += New-Step "uas-inf-x86" "UAS INF gate (x86)" (Get-PsRun "scripts\inf-gate\check-uas-inf.ps1" "-Arch x86") ("src\uas\xhciuas.inf " + $infFail) @("uas-inf-selftest")
    $s += New-Step "uas-inf-amd64" "UAS INF gate (amd64)" (Get-PsRun "scripts\inf-gate\check-uas-inf.ps1" "-Arch amd64") ("src\uas\xhciuas-amd64.inf " + $infFail) @("uas-inf-selftest")
    $s += New-Step "cm-inf-selftest" "CM INF gate self-tests" (Get-PsRun "scripts\inf-gate\check-cm-inf.ps1" "-SelfTest") $infTest
    $s += New-Step "cm-inf-x86" "CM INF gate (NHI, x86)" (Get-PsRun "scripts\inf-gate\check-cm-inf.ps1" "-Arch x86") ("src\cm\xhcicm.inf " + $infFail) @("cm-inf-selftest")
    $s += New-Step "cm-inf-amd64" "CM INF gate (NHI, amd64)" (Get-PsRun "scripts\inf-gate\check-cm-inf.ps1" "-Arch amd64") ("src\cm\xhcicm-amd64.inf " + $infFail) @("cm-inf-selftest")
    $s += New-Step "cmf-inf-x86" "CM INF gate (filter, x86)" (Get-PsRun "scripts\inf-gate\check-cm-inf.ps1" "-Filter -Arch x86") ("src\cm\xhcicmf.inf " + $infFail) @("cm-inf-selftest")
    $s += New-Step "cmf-inf-amd64" "CM INF gate (filter, amd64)" (Get-PsRun "scripts\inf-gate\check-cm-inf.ps1" "-Filter -Arch amd64") ("src\cm\xhcicmf-amd64.inf " + $infFail) @("cm-inf-selftest")
    $s += New-Step "stor98-inf-selftest" "Stor98 INF gate self-tests" (Get-PsRun "scripts\inf-gate\check-stor98-inf.ps1" "-SelfTest") $infTest
    $s += New-Step "stor98-inf" "Stor98 INF gate" (Get-PsRun "scripts\inf-gate\check-stor98-inf.ps1") ("src\stor98\inf\stor98-x86.inf and stor98-x64.inf " + $infFail) @("stor98-inf-selftest")
    $s += New-Step "oem-selftest" "txtsetup.oem gate self-tests" (Get-PsRun "scripts\inf-gate\check-txtsetup-oem.ps1" "-SelfTest") $infTest
    $s += New-Step "oem-x86" "txtsetup.oem gate (x86)" (Get-PsRun "scripts\inf-gate\check-txtsetup-oem.ps1" "-Arch x86") ("src\txtsetup.oem " + $infFail) @("oem-selftest")
    $s += New-Step "oem-amd64" "txtsetup.oem gate (amd64)" (Get-PsRun "scripts\inf-gate\check-txtsetup-oem.ps1" "-Arch amd64") ("src\txtsetup-amd64.oem " + $infFail) @("oem-selftest")
    $s += New-Step "packager" "packager self-tests" (Get-PsRun "scripts\package\test-package.ps1") "the install-media packager's self-tests failed, so any package it builds is untrustworthy - including where it puts each file and whether a Microsoft file has crept back onto the media."
    $s += New-Step "qemu-launchers" "QEMU launcher self-tests" (Get-PsRun "scripts\test-qemu-launchers.ps1") "the QEMU launcher self-tests failed. The generated launch command may lose or conflate qemu-driver trace evidence."
    $matrixFail = "the VM matrix verdict self-tests failed, so a matrix run's PASS, FAIL and NODRIVER words cannot be trusted. Fix scripts\vm-matrix first."
    $s += New-Step "vm-matrix" "VM matrix verdict self-tests" (Get-PsRun "scripts\vm-matrix\selftest.ps1") $matrixFail
    $s += New-Step "msi86-trace" "VM matrix: the M86 trace validator's self-tests" (Get-PsRun "scripts\vm-matrix\check-msi86-trace.ps1" "-SelfTest") $matrixFail
    $hdrFail = "src\cm\stor98cm.h is not src\stor98\include\stor98cm.h, the master, byte for byte - see the check's output."
    $s += New-Step "stor98-header-selftest" "Stor98 interface header: self-tests" (Get-PsRun "scripts\check-stor98-header.ps1" "-SelfTest") $hdrFail
    $s += New-Step "stor98-header" "Stor98 interface header" (Get-PsRun "scripts\check-stor98-header.ps1") $hdrFail @("stor98-header-selftest")
    $toolFail = "xhcitr\build.cmd or xhciperf\build.cmd failed: XHCITR, which compiles the installer's pure half, or XHCIPERF is not current."
    $trAfter = @()
    $perfAfter = @()
    if ($Arch -eq "x86") {
        $s += New-Step "xhcitr-build" "xhcitr, rebuilt" (Get-CmdRun "xhcitr\build.cmd") $toolFail
        $s += New-Step "xhciperf-build" "xhciperf, rebuilt" (Get-CmdRun "xhciperf\build.cmd") $toolFail
        $trAfter = @("xhcitr-build")
        $perfAfter = @("xhciperf-build")
    }
    $s += New-Step "xhciinst-selftest" "xhciinst self-test" (Get-CmdRun "xhciinst\selftest.cmd") "xhciinst's self-test failed: its switches, its decisions or its import table cannot be trusted (xhciinst\selftest.cmd)." @() "xhciinst\XHCIINST.EXE"
    $s += New-Step "xhcitr-selftest" "xhcitr self-test" (Get-CmdRun "xhcitr\selftest.cmd") "xhcitr's self-test failed: its switches, its pure half or its import table cannot be trusted (xhcitr\selftest.cmd)." $trAfter "xhcitr\XHCITR.EXE"
    $s += New-Step "xhcitr-snaptest" "xhcitr snapshot self-test" (Get-CmdRun "xhcitr\snaptest.cmd") "xhcitr's snapshot self-test failed, so a dump's ""send this"" line cannot be trusted to mean the report was written in full." @("xhcitr-selftest") "xhcitr\XHCITR.EXE"
    $s += New-Step "xhciperf-selftest" "xhciperf self-test" (Get-CmdRun "xhciperf\selftest.cmd") "xhciperf's self-test failed: its import table, its source rule or its pure half cannot be trusted (xhciperf\selftest.cmd)." $perfAfter "xhciperf\XHCIPERF.EXE"
    $s += New-Step "xhcimark-selftest" "xhcimark self-test" (Get-CmdRun "xhcimark\selftest.cmd") "xhcimark's self-test failed: its pure half, its switches or its import table cannot be trusted (xhcimark\selftest.cmd)." @() "xhcimark\XHCIMARK.EXE"
    $s += New-Step "xhcidi-selftest" "xhcidi self-test" (Get-CmdRun "xhciinst\di\selftest.cmd") "xhcidi's self-test failed: its import table, the gate over it or its version stamp cannot be trusted (xhciinst\di\selftest.cmd)." @() "xhciinst\di\xhcidi.exe"
    $s += New-Step "host-tests" "host tests" (Get-CmdRun "test\run-host-tests.cmd") "the host suite failed - see its output." @() "" 2
    if ($Arch -eq "x86") {
        $s += New-Step "stor98-unit" "Stor98 unit tests" (Get-CmdRun "src\stor98\tests\unit\build.cmd") "Stor98's unit suite failed - see its output."
    }
    # The longest first, so the short ones fill in beside them: the
    # packager's self-tests alone took 20 minutes on 2026-10-29.
    $long = @("packager", "host-tests", "xhcitr-build", "xhciperf-build", "stor98-unit")
    $first = @()
    foreach ($id in $long) { $first += @($s | Where-Object { $_.Id -eq $id }) }
    return @($first + @($s | Where-Object { $long -notcontains $_.Id }))
}

function Format-StepSpan {
    param([TimeSpan]$Span)
    return ("{0}:{1:00}" -f [int][Math]::Floor($Span.TotalMinutes), $Span.Seconds)
}

function Start-BuildStep {
    param($Step, [string]$LogDir, [string]$Root)
    $tmp = Join-Path $LogDir ("tmp\" + $Step.Id)
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    $Step.Log = Join-Path $LogDir ($Step.Id + ".log")
    if (Test-Path -LiteralPath $Step.Log) { Remove-Item -LiteralPath $Step.Log -Force }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    # /s: cmd takes the line between the outer quotes as it is; the
    # brackets put the whole line's output in the log, not its last command's.
    $psi.Arguments = '/d /s /c "(' + $Step.Run + ') > "' + $Step.Log + '" 2>&1"'
    $psi.WorkingDirectory = $Root
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["TEMP"] = $tmp
    $psi.EnvironmentVariables["TMP"] = $tmp
    # test\u4_fakefab.c writes its trace to U4_TRACE's path: two host suites
    # at once (the suite's own step and the one the packager's binary-gate
    # case starts) would write one file.
    if ($psi.EnvironmentVariables.ContainsKey("U4_TRACE")) { $psi.EnvironmentVariables.Remove("U4_TRACE") }
    $Step.Watch = [System.Diagnostics.Stopwatch]::StartNew()
    $Step.Process = [System.Diagnostics.Process]::Start($psi)
    $Step.State = "running"
}

function Invoke-BuildSteps {
    # Runs -Steps, at most -Max at once, each from -Root with its log and
    # temporary folder under -LogDir; -Write takes each line printed.
    # Returns the exit code (0, 1 or 2) and leaves each step's State
    # (passed, failed, inconclusive, absent, not run), Code and Elapsed.
    param([object[]]$Steps, [int]$Max, [string]$LogDir, [string]$Root, [scriptblock]$Write)
    if ($Max -lt 1) { $Max = 1 }
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    $byId = @{}
    foreach ($s in $Steps) { $byId[$s.Id] = $s }
    foreach ($s in $Steps) {
        foreach ($a in $s.After) {
            if (-not $byId.ContainsKey($a)) { throw "step '$($s.Id)' waits for '$a', which is not a step." }
        }
    }
    $wall = [System.Diagnostics.Stopwatch]::StartNew()
    $stop = $false
    $peak = 0
    # Whatever ends this loop - a throw, Ctrl+C - every step started has
    # ended before it returns, so build-driver.cmd never drops its lock
    # under a step still writing.
    try {
    while ($true) {
        $running = @($Steps | Where-Object { $_.State -eq "running" })
        foreach ($s in $running) {
            if (-not $s.Process.HasExited) { continue }
            $s.Process.WaitForExit()
            $s.Watch.Stop()
            $s.Elapsed = $s.Watch.Elapsed
            $s.Code = $s.Process.ExitCode
            $s.Process.Dispose()
            $s.Process = $null
            if ($s.Code -eq 0) { $s.State = "passed" }
            elseif ($s.Inconclusive -ge 0 -and $s.Code -eq $s.Inconclusive) { $s.State = "inconclusive"; $stop = $true }
            else { $s.State = "failed"; $stop = $true }
            & $Write ""
            & $Write ("=== {0} ===" -f $s.Title)
            if (Test-Path -LiteralPath $s.Log) {
                foreach ($l in [System.IO.File]::ReadAllLines($s.Log)) { & $Write $l }
            }
            & $Write ("--- {0}: {1} (exit {2}, {3})" -f $s.Title, $s.State, $s.Code, (Format-StepSpan $s.Elapsed))
        }
        $pending = @($Steps | Where-Object { $_.State -eq "pending" })
        if ($stop) {
            foreach ($s in $pending) { $s.State = "not run" }
            $pending = @()
        }
        $running = @($Steps | Where-Object { $_.State -eq "running" })
        foreach ($s in $pending) {
            if ($running.Count -ge $Max) { break }
            $ready = $true
            foreach ($a in $s.After) {
                $st = $byId[$a].State
                if ($st -eq "passed" -or $st -eq "absent") { continue }
                $ready = $false
            }
            if (-not $ready) { continue }
            if ($s.IfExists -ne "" -and -not (Test-Path -LiteralPath (Join-Path $Root $s.IfExists) -PathType Leaf)) {
                $s.State = "absent"
                & $Write ""
                & $Write ("--- {0}: not run, no {1}" -f $s.Title, $s.IfExists)
                continue
            }
            try {
                Start-BuildStep -Step $s -LogDir $LogDir -Root $Root
            } catch {
                $s.State = "failed"
                $s.Code = -1
                $s.Hint = ("it could not be started ({0}). {1}" -f $_.Exception.Message, $s.Hint)
                $stop = $true
                & $Write ""
                & $Write ("--- {0}: failed to start: {1}" -f $s.Title, $_.Exception.Message)
                break
            }
            $running = @($Steps | Where-Object { $_.State -eq "running" })
            if ($running.Count -gt $peak) { $peak = $running.Count }
        }
        $left = @($Steps | Where-Object { $_.State -eq "pending" -or $_.State -eq "running" })
        if ($left.Count -eq 0) { break }
        # Nothing running and steps still pending, with no failure to have
        # stopped them: each waits, through the table, for itself. A check
        # left unrun is a failure, not a pass.
        $running = @($Steps | Where-Object { $_.State -eq "running" })
        if ($running.Count -eq 0) {
            foreach ($s in @($Steps | Where-Object { $_.State -eq "pending" })) {
                $s.State = "failed"
                $s.Code = -1
                $s.Hint = ("it waits for {0}, and the table's waits make a cycle: it could never start." -f ($s.After -join ", "))
            }
            $stop = $true
            continue
        }
        Start-Sleep -Milliseconds 200
    }
    } finally {
        foreach ($s in @($Steps | Where-Object { $_.State -eq "running" -and $null -ne $_.Process })) {
            if (-not $s.Process.HasExited) { $s.Process.WaitForExit() }
        }
    }
    $wall.Stop()
    & $Write ""
    & $Write ("=== the checks that need no DDK: {0} wall, at most {1} at once ===" -f (Format-StepSpan $wall.Elapsed), $peak)
    foreach ($s in $Steps) {
        $t = if ($s.State -eq "passed" -or $s.State -eq "failed" -or $s.State -eq "inconclusive") { Format-StepSpan $s.Elapsed } else { "" }
        & $Write ("  {0,-24} {1,-12} {2,6}" -f $s.Id, $s.State, $t)
    }
    $failed = @($Steps | Where-Object { $_.State -eq "failed" })
    $inconclusive = @($Steps | Where-Object { $_.State -eq "inconclusive" })
    foreach ($s in $failed) {
        & $Write ""
        & $Write ("ERROR: {0}: {1}" -f $s.Title, $s.Hint)
    }
    if ($failed.Count -ne 0) { return 1 }
    if ($inconclusive.Count -ne 0) { return 2 }
    return 0
}

function Get-StepMax {
    $v = "$env:XHCI98_BUILD_STEPS"
    if ($v -eq "") { return 4 }
    $n = 0
    if (-not [int]::TryParse($v, [ref]$n) -or $n -lt 1) {
        throw "XHCI98_BUILD_STEPS is '$v': give a number of steps at once, 1 or more."
    }
    return $n
}

function Invoke-RunnerSelfTest {
    $work = Join-Path ([System.IO.Path]::GetFullPath($env:TEMP)) ("xhci98-steps-test-" + [System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $script:lines = New-Object System.Collections.ArrayList
        $collect = { param($l) [void]$script:lines.Add($l) }
        $mk = Join-Path $work "mark"
        New-Item -ItemType Directory -Path $mk | Out-Null
        # cmd /c lines only: no PowerShell start-up in the stand-ins.
        # A batch file that marks its own start, then waits up to a minute for
        # the other's mark: two pass only when both ran at once.
        $waitCmd = Join-Path $work "wait.cmd"
        [System.IO.File]::WriteAllText($waitCmd, ("@echo off`r`necho.>""%~1""`r`nset n=0`r`n:loop`r`n" +
            "if exist ""%~2"" exit /b 0`r`nset /a n+=1`r`nif %n% geq 60 exit /b 9`r`n" +
            "ping -n 2 127.0.0.1 >nul`r`ngoto loop`r`n"))
        $waitFor = { param($Name, $Other) ('call "{0}" "{1}\{2}" "{1}\{3}"' -f $waitCmd, $mk, $Name, $Other) }

        Write-Step "two steps at once: each sees the other's mark before it ends"
        $script:lines.Clear()
        $s = @((New-Step "a" "A" (& $waitFor "a" "b") "hint a"), (New-Step "b" "B" (& $waitFor "b" "a") "hint b"))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l1") -Root $work -Write $collect
        Assert-True ($rc -eq 0 -and $s[0].State -eq "passed" -and $s[1].State -eq "passed") ("two steps did not run at once: rc {0}, {1} {2}" -f $rc, $s[0].State, $s[1].State)
        $text = $script:lines -join "`n"
        Assert-True ($text.Contains("=== A ===") -and $text.Contains("--- A: passed") -and $text.Contains("=== the checks that need no DDK")) "a step's heading, result line or the table is missing."

        Write-Step "one at a time with -Max 1, in the table's order; a dependency waits"
        $script:lines.Clear()
        $order = Join-Path $work "order.txt"
        $s = @((New-Step "c" "C" ('echo c>>"{0}"' -f $order) "hint c" @("d")),
               (New-Step "d" "D" ('echo d>>"{0}"' -f $order) "hint d"),
               (New-Step "e" "E" ('echo e>>"{0}"' -f $order) "hint e"))
        $rc = Invoke-BuildSteps -Steps $s -Max 1 -LogDir (Join-Path $work "l2") -Root $work -Write $collect
        $got = (@([System.IO.File]::ReadAllLines($order) | ForEach-Object { $_.Trim() }) -join ",")
        Assert-True ($rc -eq 0 -and $got -eq "d,c,e") ("the order was '{0}' (rc {1}), not d,c,e" -f $got, $rc)

        Write-Step "a failure: its dependant and every later step not run, its hint printed, exit 1"
        $script:lines.Clear()
        $s = @((New-Step "f" "F" "exit /b 3" "the f hint"),
               (New-Step "g" "G" "exit /b 0" "hint g" @("f")),
               (New-Step "h" "H" "exit /b 0" "hint h"))
        $rc = Invoke-BuildSteps -Steps $s -Max 1 -LogDir (Join-Path $work "l3") -Root $work -Write $collect
        $text = $script:lines -join "`n"
        Assert-True ($rc -eq 1 -and $s[0].State -eq "failed" -and $s[1].State -eq "not run" -and $s[2].State -eq "not run") `
            ("a failure did not stop the later steps: rc {0}, {1} {2} {3}" -f $rc, $s[0].State, $s[1].State, $s[2].State)
        Assert-True ($text.Contains("ERROR: F: the f hint") -and $text.Contains("exit 3")) "the failed step's hint or exit code was not printed."

        Write-Step "an inconclusive exit alone gives 2; with a failure, 1"
        $script:lines.Clear()
        $s = @((New-Step "i" "I" "exit /b 2" "hint i" @() "" 2))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l4") -Root $work -Write $collect
        Assert-True ($rc -eq 2 -and $s[0].State -eq "inconclusive") ("an inconclusive step gave {0} ({1})" -f $rc, $s[0].State)
        $s = @((New-Step "j" "J" "exit /b 2" "hint j" @() "" 2), (New-Step "k" "K" "exit /b 1" "hint k"))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l5") -Root $work -Write $collect
        Assert-True ($rc -eq 1) ("an inconclusive step beside a failed one gave {0}" -f $rc)

        Write-Step "a step whose EXE is absent is passed over, and its dependant runs"
        $script:lines.Clear()
        $s = @((New-Step "m" "M" "exit /b 1" "hint m" @() "no-such.exe"), (New-Step "n" "N" "exit /b 0" "hint n" @("m")))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l6") -Root $work -Write $collect
        Assert-True ($rc -eq 0 -and $s[0].State -eq "absent" -and $s[1].State -eq "passed") ("an absent step: rc {0}, {1} {2}" -f $rc, $s[0].State, $s[1].State)
        Assert-True (($script:lines -join "`n").Contains("--- M: not run, no no-such.exe")) "an absent step was not said."

        Write-Step "each step its own TEMP, TMP and log, and the working directory given"
        $script:lines.Clear()
        $s = @((New-Step "p" "P" "echo T=%TEMP% M=%TMP% & cd" "hint p"), (New-Step "q" "Q" "echo T=%TEMP% M=%TMP% & cd" "hint q"))
        $ld = Join-Path $work "l7"
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir $ld -Root $work -Write $collect
        $p = [System.IO.File]::ReadAllText((Join-Path $ld "p.log"))
        $q = [System.IO.File]::ReadAllText((Join-Path $ld "q.log"))
        $pt = Join-Path $ld "tmp\p"
        Assert-True ($rc -eq 0 -and $p.Contains("T=$pt M=$pt") -and $q.Contains((Join-Path $ld "tmp\q")) -and $p.Contains($work)) `
            ("a step's TEMP, TMP or directory was not its own: '{0}' / '{1}'" -f $p.Trim(), $q.Trim())

        Write-Step "a cycle of waits fails, it does not pass with its steps unrun"
        $script:lines.Clear()
        $s = @((New-Step "s" "S" "exit /b 0" "h" @("t")), (New-Step "t" "T" "exit /b 0" "h" @("s")), (New-Step "u" "U" "exit /b 0" "h"))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l9") -Root $work -Write $collect
        Assert-True ($rc -eq 1 -and $s[0].State -eq "failed" -and $s[1].State -eq "failed" -and $s[2].State -eq "passed" -and
                     ($script:lines -join "`n").Contains("make a cycle")) ("a cycle of waits gave {0} ({1} {2} {3})" -f $rc, $s[0].State, $s[1].State, $s[2].State)

        Write-Step "a wait for a step that is not in the table is refused"
        $threw = ""
        try { [void](Invoke-BuildSteps -Steps @((New-Step "r" "R" "exit /b 0" "h" @("zz"))) -Max 1 -LogDir (Join-Path $work "l8") -Root $work -Write $collect) } catch { $threw = $_.Exception.Message }
        Assert-True ($threw.Contains("which is not a step")) ("a dangling wait was not refused: '{0}'" -f $threw)

        Write-Step "the build's tables: every wait names a step, every id once"
        foreach ($ar in @("x86", "amd64")) {
            $t = @(Get-BuildSteps -Arch $ar)
            $ids = @($t | ForEach-Object { $_.Id })
            Assert-True (@($ids | Select-Object -Unique).Count -eq $ids.Count) "an id is used twice in the $ar table."
            foreach ($x in $t) { foreach ($a in $x.After) { Assert-True ($ids -contains $a) "$ar step '$($x.Id)' waits for '$a', not in the table." } }
            Assert-True ((($ids -contains "xhcitr-build") -and ($ids -contains "stor98-unit")) -eq ($ar -eq "x86")) "the x86-only steps are wrong in the $ar table."
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host ""
    if ($script:failures.Count -gt 0) {
        Write-Err ("build-steps self-tests FAILED: {0} of {1} check(s)." -f $script:failures.Count, $script:checks)
        return 1
    }
    Write-Ok ("build-steps self-tests passed ({0} checks)" -f $script:checks)
    return 0
}

if ($SelfTest) {
    . (Join-Path $PSScriptRoot "test-harness.ps1")
    exit (Invoke-RunnerSelfTest)
}

$max = Get-StepMax
$logDir = Join-Path $repo ("out\steps-" + $Arch)
$rc = Invoke-BuildSteps -Steps @(Get-BuildSteps -Arch $Arch) -Max $max -LogDir $logDir -Root $repo -Write { param($l) Write-Host $l }
exit $rc
