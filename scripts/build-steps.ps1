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
header check, Python on PATH and then the connection manager snapshot's
field tables, on the x86 run XHCITR and XHCIPERF rebuilt and their
self-tests, the XHCIINST, XHCIMARK and XHCIDI self-tests when their EXEs
are there, the host suite and on the x86 run Stor98's unit suite. With
-All (build-driver.cmd's "all" run) the tools are rebuilt first on both
architectures - XHCIMARK, XHCIINST, then XHCIDI on its own (its build's
exit 2, no Open Watcom, a failure there), and on amd64 XHCITR and XHCIPERF
as well - and a tool self-test whose EXE is still not there fails instead
of being passed over.

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

.PARAMETER All
build-driver.cmd's "all" run: every tool whose self-test the block runs is
built in it first, and a self-test whose EXE is missing fails, naming the
build to run (task 49.26). Without it such a step is passed over
("absent") and listed in absent.txt.

.PARAMETER Stor98Only
build-driver.cmd's Stor98-only run (stor98 in place of the flavour,
roadmap-usb4 task 48.9): only the steps Stor98's images and INFs need,
its INF gate and the interface header with their self-tests and, on the
x86 run, its unit suite.

.PARAMETER SelfTest
Run the runner over stand-in steps instead, and check what it does.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\build-steps.ps1 -Arch x86
#>

[CmdletBinding()]
param(
    [ValidateSet("x86", "amd64")]
    [string]$Arch = "x86",
    [switch]$All,
    [switch]$Stor98Only,
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
    # Builder: the build that makes IfExists, named when a run that requires
    # it (Invoke-BuildSteps -RequireExe) finds it missing.
    param([string]$Id, [string]$Title, [string]$Run, [string]$Hint,
          [string[]]$After = @(), [string]$IfExists = "", [int]$Inconclusive = -1,
          [string]$Builder = "")
    return [pscustomobject]@{
        Id = $Id; Title = $Title; Run = $Run; Hint = $Hint; After = @($After)
        IfExists = $IfExists; Inconclusive = $Inconclusive; Builder = $Builder
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

$script:PythonHint = "no Python runs from PATH (none there, or only the Microsoft Store's python.exe alias, which installs nothing and exits 9009). A gated build needs it on both architectures: scripts\gen-cm-snap.py's check, and on the x86 run the link checks of S98VSD.VXD and XHCIPFW.VXD. Put Python's folder on PATH (a .cmd wrapper that sets its own PATH must add it) and run again (task 49.7)."
# The first python.exe on PATH, run; it names itself in the step's log.
$script:PythonProbe = 'python -I -c "import sys; print(sys.executable)"'

function Get-BuildSteps {
    # The block: the five longest first, then the rest in build-driver.cmd's
    # old order. The hints are its old failure labels' texts. -All: the
    # tools rebuilt first on both architectures (task 49.26).
    param([string]$Arch, [switch]$All)
    $infFail = "failed the setup-engine gate. Do not install it - Win98's setup engine has no log, and a Win2000 install that creates no service looks the same in Device Manager as a driver that loaded and failed."
    $infTest = "the INF gate's own self-tests failed, so its verdict on the INFs cannot be trusted either. Fix the gate first."
    $gateTest = "the import gate's self-tests failed."
    $s = @()
    $s += New-Step "import-manifests" "import gate self-tests: evidence manifests" (Get-PsRun "scripts\import-gate\test-evidence-manifests.ps1") $gateTest
    $s += New-Step "import-flavours" "import gate self-tests: the FLAVORS column" (Get-PsRun "scripts\import-gate\test-flavour-rules.ps1") $gateTest
    $s += New-Step "helper-build" "import gate self-tests: the helper's toolchain rules" (Get-PsRun "scripts\check-helper-build.ps1" "-SelfTest") $gateTest
    $s += New-Step "frame-pointers" "the frame-pointer check's self-tests" (Get-PsRun "scripts\check-frame-pointers.ps1" "-SelfTest") "the frame-pointer check's self-tests failed, so its verdict on the x86 release build logs cannot be trusted (task 45.45)."
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
    $s += New-Step "net-inf-selftest" "Thunderbolt networking INF gate self-tests" (Get-PsRun "scripts\inf-gate\check-net-inf.ps1" "-SelfTest") $infTest
    $s += New-Step "net-inf-x86" "Thunderbolt networking INF gate (x86)" (Get-PsRun "scripts\inf-gate\check-net-inf.ps1" "-Arch x86") ("src\net\xhcinet.inf " + $infFail) @("net-inf-selftest")
    $s += New-Step "net-inf-amd64" "Thunderbolt networking INF gate (amd64)" (Get-PsRun "scripts\inf-gate\check-net-inf.ps1" "-Arch amd64") ("src\net\xhcinet-amd64.inf " + $infFail) @("net-inf-selftest")
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
    $s += New-Step "precut-selftest" "pre-cut.ps1 self-tests" (Get-PsRun "scripts\package\pre-cut.ps1" "-SelfTest") "pre-cut.ps1's self-tests failed, so its refusals and its steps before a cut cannot be trusted (task 48.10)."
    $s += New-Step "stor98-header-selftest" "Stor98 interface header: self-tests" (Get-PsRun "scripts\check-stor98-header.ps1" "-SelfTest") $hdrFail
    $s += New-Step "stor98-header" "Stor98 interface header" (Get-PsRun "scripts\check-stor98-header.ps1") $hdrFail @("stor98-header-selftest")
    # Python is asked for once, by a step of its own, so a PATH without it
    # (a .cmd wrapper that set its own) fails naming Python, not as the
    # field tables' mismatch; the x86 run's VxD builds need it next.
    $s += New-Step "python" "Python on PATH" $script:PythonProbe $script:PythonHint
    $s += New-Step "cm-snap-fields" "connection manager snapshot: the field tables" ('python "' + (Join-Path $repo "scripts\gen-cm-snap.py") + '" "' + $repo + '" -Check') "src\cm\cm_snapf.c or cm_snapn.h is not what the structures give: run scripts\gen-cm-snap.py <repo> and review the change (record 21 section 8.4)." @("python")
    $toolFail = "xhcitr\build.cmd or xhciperf\build.cmd failed: XHCITR, which compiles the installer's pure half, or XHCIPERF is not current."
    $trAfter = @()
    $perfAfter = @()
    $instAfter = @()
    $markAfter = @()
    if ($Arch -eq "x86" -or $All) {
        $s += New-Step "xhcitr-build" "xhcitr, rebuilt" (Get-CmdRun "xhcitr\build.cmd") $toolFail
        $s += New-Step "xhciperf-build" "xhciperf, rebuilt" (Get-CmdRun "xhciperf\build.cmd") $toolFail
        $trAfter = @("xhcitr-build")
        $perfAfter = @("xhciperf-build")
    }
    $diAfter = @()
    if ($All) {
        # XHCIDI: xhciinst\build.cmd builds it too as its last part, but turns
        # di\build.cmd's exit 2 (no Open Watcom) into success and leaves an
        # older xhcidi.exe in place; so it is built again by a step of its own
        # whose exit 2 fails the run, after the installer's build (the two
        # never run side by side on its files). Its self-test waits for that.
        $s += New-Step "xhciinst-build" "xhciinst, rebuilt" (Get-CmdRun "xhciinst\build.cmd") "xhciinst\build.cmd failed: XHCIINST, XHCIWIZ, SETUP or the x64 pair is not current."
        $s += New-Step "xhcidi-build" "xhcidi, rebuilt" (Get-CmdRun "xhciinst\di\build.cmd") "xhciinst\di\build.cmd did not build XHCIDI (exit 2: no Open Watcom 16-bit compiler under WATCOM): an all build requires it, so an older xhcidi.exe is not self-tested in its place." @("xhciinst-build")
        $s += New-Step "xhcimark-build" "xhcimark, rebuilt" (Get-CmdRun "xhcimark\build.cmd") "xhcimark\build.cmd failed: XHCIMARK and XHCIMK64 are not current."
        $instAfter = @("xhciinst-build")
        $diAfter = @("xhcidi-build")
        $markAfter = @("xhcimark-build")
    }
    $s += New-Step "xhciinst-selftest" "xhciinst self-test" (Get-CmdRun "xhciinst\selftest.cmd") "xhciinst's self-test failed: its switches, its decisions or its import table cannot be trusted (xhciinst\selftest.cmd)." $instAfter "xhciinst\XHCIINST.EXE" -1 "xhciinst\build.cmd"
    $s += New-Step "xhcitr-selftest" "xhcitr self-test" (Get-CmdRun "xhcitr\selftest.cmd") "xhcitr's self-test failed: its switches, its pure half or its import table cannot be trusted (xhcitr\selftest.cmd)." $trAfter "xhcitr\XHCITR.EXE" -1 "xhcitr\build.cmd"
    $s += New-Step "xhcitr-snaptest" "xhcitr snapshot self-test" (Get-CmdRun "xhcitr\snaptest.cmd") "xhcitr's snapshot self-test failed, so a dump's ""send this"" line cannot be trusted to mean the report was written in full." @("xhcitr-selftest") "xhcitr\XHCITR.EXE" -1 "xhcitr\build.cmd"
    $s += New-Step "xhciperf-selftest" "xhciperf self-test" (Get-CmdRun "xhciperf\selftest.cmd") "xhciperf's self-test failed: its import table, its source rule or its pure half cannot be trusted (xhciperf\selftest.cmd)." $perfAfter "xhciperf\XHCIPERF.EXE" -1 "xhciperf\build.cmd"
    $s += New-Step "xhcimark-selftest" "xhcimark self-test" (Get-CmdRun "xhcimark\selftest.cmd") "xhcimark's self-test failed: its pure half, its switches or its import table cannot be trusted (xhcimark\selftest.cmd)." $markAfter "xhcimark\XHCIMARK.EXE" -1 "xhcimark\build.cmd"
    $s += New-Step "xhcidi-selftest" "xhcidi self-test" (Get-CmdRun "xhciinst\di\selftest.cmd") "xhcidi's self-test failed: its import table, the gate over it or its version stamp cannot be trusted (xhciinst\di\selftest.cmd)." $diAfter "xhciinst\di\xhcidi.exe" -1 "xhciinst\di\build.cmd (Open Watcom)"
    # In a gated build the host suite's amd64 legs are not optional: without
    # WDK 7.1's amd64 compiler (its own WDK71, not the build's WDKROOT) the
    # suite fails rather than passing "x86 only" (task 49.34).
    $s += New-Step "host-tests" "host tests" ('set "XHCI98_HOST_AMD64_REQUIRED=1" & ' + (Get-CmdRun "test\run-host-tests.cmd")) "the host suite failed - see its output." @() "" 2
    if ($Arch -eq "x86") {
        $s += New-Step "stor98-unit" "Stor98 unit tests" (Get-CmdRun "src\stor98\tests\unit\build.cmd") "Stor98's unit suite failed - see its output."
    }
    # The longest first, so the short ones fill in beside them: the
    # packager's self-tests alone took 20 minutes on 2026-10-29.
    $long = @("packager", "host-tests", "xhciinst-build", "xhcitr-build", "xhciperf-build", "xhcimark-build", "stor98-unit")
    $first = @()
    foreach ($id in $long) { $first += @($s | Where-Object { $_.Id -eq $id }) }
    return @($first + @($s | Where-Object { $long -notcontains $_.Id }))
}

function Get-Stor98Steps {
    # The Stor98-only run's block (task 48.9): Get-BuildSteps' Stor98 rows,
    # taken from that table so the two cannot drift, with what they wait for.
    param([string]$Arch)
    $ids = @("stor98-inf-selftest", "stor98-inf", "stor98-header-selftest", "stor98-header", "stor98-unit")
    return @(Get-BuildSteps -Arch $Arch | Where-Object { $ids -contains $_.Id })
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
    # -RequireExe (an "all" run): a step whose IfExists is not there fails,
    # naming its Builder, instead of being passed over.
    param([object[]]$Steps, [int]$Max, [string]$LogDir, [string]$Root, [scriptblock]$Write,
          [switch]$RequireExe)
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
                if ($RequireExe) {
                    $b = if ($s.Builder -ne "") { $s.Builder } else { "the build that makes it" }
                    $s.State = "failed"
                    $s.Code = -1
                    $s.Hint = ("{0} is not built: run {1}. An all build runs that build before this step, so it did not make the EXE (see its log). {2}" -f $s.IfExists, $b, $s.Hint)
                    $stop = $true
                    & $Write ""
                    & $Write ("--- {0}: failed, no {1}" -f $s.Title, $s.IfExists)
                    break
                }
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
        # (After a failure the pending steps are marked not run at the loop's
        # top, not here.)
        if ($running.Count -eq 0 -and -not $stop) {
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

        Write-Step "with -RequireExe (an all build) an absent EXE fails, names its build, and stops the rest"
        $script:lines.Clear()
        $s = @((New-Step "m2" "M2" "exit /b 0" "hint m2" @() "no-such.exe" -1 "tool\build.cmd"), (New-Step "n2" "N2" "exit /b 0" "hint n2" @("m2")))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l6b") -Root $work -Write $collect -RequireExe
        $text = $script:lines -join "`n"
        Assert-True ($rc -eq 1 -and $s[0].State -eq "failed" -and $s[1].State -eq "not run") ("an absent step under -RequireExe: rc {0}, {1} {2}" -f $rc, $s[0].State, $s[1].State)
        Assert-True ($text.Contains("--- M2: failed, no no-such.exe") -and $text.Contains("ERROR: M2: no-such.exe is not built") -and $text.Contains("run tool\build.cmd")) "an absent step under -RequireExe did not say its EXE and its build."
        $s = @((New-Step "m3" "M3" "exit /b 0" "hint m3" @() "wait.cmd" -1 "tool\build.cmd"))
        $rc = Invoke-BuildSteps -Steps $s -Max 1 -LogDir (Join-Path $work "l6c") -Root $work -Write $collect -RequireExe
        Assert-True ($rc -eq 0 -and $s[0].State -eq "passed") ("a present EXE under -RequireExe: rc {0}, {1}" -f $rc, $s[0].State)

        Write-Step "an all build with an older helper image there and no Open Watcom fails: the builder's exit 2 is a failure"
        $script:lines.Clear()
        $old = Join-Path $work "old-xhcidi.exe"
        [System.IO.File]::WriteAllText($old, "an older image of the same version")
        $s = @((New-Step "dib" "DIB" "echo XHCIDI.EXE was not built: no Open Watcom & exit /b 2" "the helper was not rebuilt"),
               (New-Step "dit" "DIT" "exit /b 0" "hint dit" @("dib") "old-xhcidi.exe" -1 "xhciinst\di\build.cmd"))
        $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l6d") -Root $work -Write $collect -RequireExe
        Assert-True ($rc -eq 1 -and $s[0].State -eq "failed" -and $s[0].Code -eq 2 -and $s[1].State -eq "not run") `
            ("an older helper image with its build's exit 2: rc {0}, {1} ({2}) {3}" -f $rc, $s[0].State, $s[0].Code, $s[1].State)
        Assert-True (($script:lines -join "`n").Contains("ERROR: DIB: the helper was not rebuilt")) "the helper build's exit 2 was not said as the failure."

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

        Write-Step "the all build's tables: every tool self-test waits for a build of its tool, on both architectures"
        $toolBuild = @{ "xhciinst-selftest" = "xhciinst-build"; "xhcidi-selftest" = "xhcidi-build"; "xhcimark-selftest" = "xhcimark-build";
                        "xhcitr-selftest" = "xhcitr-build"; "xhciperf-selftest" = "xhciperf-build" }
        foreach ($ar in @("x86", "amd64")) {
            $t = @(Get-BuildSteps -Arch $ar -All)
            $ids = @($t | ForEach-Object { $_.Id })
            Assert-True (@($ids | Select-Object -Unique).Count -eq $ids.Count) "an id is used twice in the $ar all table."
            foreach ($x in $t) { foreach ($a in $x.After) { Assert-True ($ids -contains $a) "$ar all step '$($x.Id)' waits for '$a', not in the table." } }
            foreach ($k in $toolBuild.Keys) {
                $x = @($t | Where-Object { $_.Id -eq $k })
                Assert-True ($x.Count -eq 1 -and ($x[0].After -contains $toolBuild[$k]) -and $x[0].Builder -ne "") "$ar all: '$k' does not wait for '$($toolBuild[$k])' or names no build."
            }
            $snap = @($t | Where-Object { $_.Id -eq "xhcitr-snaptest" })
            Assert-True ($snap.Count -eq 1 -and ($snap[0].After -contains "xhcitr-selftest")) "$ar all: the snapshot self-test does not wait for xhcitr's."
            $withExe = @($t | Where-Object { $_.IfExists -ne "" })
            Assert-True ($withExe.Count -eq 6) "$ar all: $($withExe.Count) steps read an EXE, not the six tool self-tests."
            $dib = @($t | Where-Object { $_.Id -eq "xhcidi-build" })
            Assert-True ($dib.Count -eq 1 -and ($dib[0].After -contains "xhciinst-build") -and $dib[0].Run.Contains("xhciinst\di\build.cmd")) "$ar all: XHCIDI's own build is missing, or does not wait for xhciinst\build.cmd, which writes the same files."
        }
        foreach ($ar in @("x86", "amd64")) {
            $t = @(Get-BuildSteps -Arch $ar)
            $ids = @($t | ForEach-Object { $_.Id })
            Assert-True (-not ($ids -contains "xhciinst-build") -and -not ($ids -contains "xhcimark-build") -and -not ($ids -contains "xhcidi-build")) "a plain $ar run builds the installer, its helper or the mark service."
        }

        Write-Step "the Python step, with no Python on PATH, fails naming Python, and the field tables' check is not run"
        $script:lines.Clear()
        $savedPath = $env:PATH
        try {
            $env:PATH = Join-Path $env:SystemRoot "System32"
            $s = @((New-Step "py" "PY" $script:PythonProbe $script:PythonHint), (New-Step "snap" "SNAP" "exit /b 0" "hint snap" @("py")))
            $rc = Invoke-BuildSteps -Steps $s -Max 2 -LogDir (Join-Path $work "l10") -Root $work -Write $collect
        } finally {
            $env:PATH = $savedPath
        }
        Assert-True ($rc -eq 1 -and $s[0].State -eq "failed" -and $s[1].State -eq "not run") ("the Python step without Python: rc {0}, {1} {2}" -f $rc, $s[0].State, $s[1].State)
        Assert-True (($script:lines -join "`n").Contains("ERROR: PY: no Python runs from PATH")) "the Python step's failure did not name Python."

        Write-Step "the build's tables: every wait names a step, every id once"
        foreach ($ar in @("x86", "amd64")) {
            foreach ($allRun in @($false, $true)) {
                $t = @(Get-BuildSteps -Arch $ar -All:$allRun)
                $snap = @($t | Where-Object { $_.Id -eq "cm-snap-fields" })
                $py = @($t | Where-Object { $_.Id -eq "python" })
                Assert-True ($snap.Count -eq 1 -and $py.Count -eq 1 -and ($snap[0].After -contains "python") -and $py[0].Run -eq $script:PythonProbe) "$ar (all: $allRun): the field tables' check does not wait for the Python step."
            }
            $t = @(Get-BuildSteps -Arch $ar)
            $ids = @($t | ForEach-Object { $_.Id })
            Assert-True (@($ids | Select-Object -Unique).Count -eq $ids.Count) "an id is used twice in the $ar table."
            foreach ($x in $t) { foreach ($a in $x.After) { Assert-True ($ids -contains $a) "$ar step '$($x.Id)' waits for '$a', not in the table." } }
            Assert-True ((($ids -contains "xhcitr-build") -and ($ids -contains "stor98-unit")) -eq ($ar -eq "x86")) "the x86-only steps are wrong in the $ar table."
            $t = @(Get-Stor98Steps -Arch $ar)
            $ids = @($t | ForEach-Object { $_.Id })
            $want = @("stor98-inf-selftest", "stor98-inf", "stor98-header-selftest", "stor98-header")
            if ($ar -eq "x86") { $want += "stor98-unit" }
            Assert-True ((@($ids | Sort-Object) -join ",") -eq (@($want | Sort-Object) -join ",")) "the $ar Stor98-only table is '$($ids -join ",")'."
            foreach ($x in $t) { foreach ($a in $x.After) { Assert-True ($ids -contains $a) "$ar Stor98-only step '$($x.Id)' waits for '$a', not in its table." } }
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
$steps = if ($Stor98Only) { @(Get-Stor98Steps -Arch $Arch) } else { @(Get-BuildSteps -Arch $Arch -All:$All) }
# The steps passed over because their EXE is not there, one line each in
# absent.txt, which build-driver.cmd prints again at the end of the run
# (task 49.34); none, no file. A step that ran and passed over a part of
# its own (an x64 companion EXE a tool's selftest.cmd checks only where it
# is) is not listed: that is the step's own output.
$absentList = Join-Path $logDir "absent.txt"
if (Test-Path -LiteralPath $absentList) { Remove-Item -LiteralPath $absentList -Force }
$rc = Invoke-BuildSteps -Steps $steps -Max $max -LogDir $logDir -Root $repo -Write { param($l) Write-Host $l } -RequireExe:($All -and -not $Stor98Only)
$absent = @($steps | Where-Object { $_.State -eq "absent" } | ForEach-Object { "  {0} (no {1})" -f $_.Title, $_.IfExists })
if ($absent.Count -gt 0) {
    if (-not (Test-Path -LiteralPath $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
    [System.IO.File]::WriteAllLines($absentList, [string[]]$absent, (New-Object System.Text.ASCIIEncoding))
}
exit $rc
