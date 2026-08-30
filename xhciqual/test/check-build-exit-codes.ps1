<#
.SYNOPSIS
Regression check for build.cmd's exit-code contract.

.DESCRIPTION
build.cmd gates the DOS build on the host unit tests. A guard that cannot fail
the script is worse than no guard, and that is exactly what happened once:
measured, an earlier if/else form of the guard stopped the build
(no XHCIQUAL.EXE) while exiting 0, so any caller reading the exit code saw a
failing test suite as a passing build. The current top-level goto flow fixes
it, but the reason is a subtle cmd.exe behaviour that could not be reduced to
a rule anyone should be asked to remember - so it is asserted here instead of
being explained in a comment. This check does not care why; it only requires
that build.cmd's exit code tells the truth.

Not called from build.cmd (that would recurse). Run it after touching
build.cmd's control flow, or the host-test wiring.

Each case measures the OS-level process exit code via Start-Process
-PassThru. Do not "simplify" this to `cmd /c ... | Select-String`: a
PowerShell pipeline reports the pipeline's status, not cmd's, which is what
hid the original defect.

The failing-build cases mutate a source file. Originals are restored in a
finally block and verified byte-for-byte against their pre-run snapshots.
The snapshots are what the files looked like when this script started, not
HEAD, so uncommitted work in them survives a run untouched.

If the process is killed outright rather than failing, the finally block does
not run and a mutation can survive. Both mutations are self-identifying: grep
for "mutated-by-regression-check" in xhciqual\mmiodiag.c and for "stub runner
failing on purpose" in test\run-host-tests.cmd, and restore from git if either
turns up.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File xhciqual\test\check-build-exit-codes.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$testDir  = $PSScriptRoot
$qualDir  = Split-Path -Parent $testDir
$buildCmd = Join-Path $qualDir "build.cmd"
$mmioSrc  = Join-Path $qualDir "mmiodiag.c"
$runner   = Join-Path $testDir "run-host-tests.cmd"
$exePath  = Join-Path $qualDir "xhciqual.exe"

# A string mmiodiag.c prints and test_mmiodiag.c asserts on. Changing it makes
# exactly one host check fail while the file still compiles. If it ever
# disappears this script fails loudly rather than silently passing.
$mutationAnchor = "  PCI PM: capability absent"

foreach ($required in @($buildCmd, $mmioSrc, $runner)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "missing $required"
    }
}
$watcomRoot = $env:WATCOM
if ([string]::IsNullOrWhiteSpace($watcomRoot)) { $watcomRoot = "C:\WATCOM" }
if ($null -eq (Get-Command "wmake.exe" -ErrorAction SilentlyContinue) -and
    -not (Test-Path -LiteralPath (Join-Path $watcomRoot "BINNT64\wmake.exe"))) {
    throw "Open Watcom not found - this check builds the tool for real."
}

$failures = @()

function Invoke-Build {
    param([string[]]$BuildArgs = @())

    $stdout = [IO.Path]::GetTempFileName()
    $stderr = [IO.Path]::GetTempFileName()
    try {
        $argList = @("/c", "`"$buildCmd`"") + $BuildArgs
        $p = Start-Process -FilePath $env:ComSpec -ArgumentList $argList `
                           -Wait -PassThru -WindowStyle Hidden `
                           -RedirectStandardOutput $stdout `
                           -RedirectStandardError $stderr
        return $p.ExitCode
    } finally {
        Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    }
}

function Remove-BuildOutput {
    Remove-Item -LiteralPath $exePath -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $qualDir -Filter "*.obj" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

function Test-FileMatchesSnapshot {
    param(
        [string]$Path,
        [byte[]]$Expected
    )

    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $actual = [IO.File]::ReadAllBytes($Path)
    if ($actual.Length -ne $Expected.Length) { return $false }
    for ($i = 0; $i -lt $actual.Length; $i++) {
        if ($actual[$i] -ne $Expected[$i]) { return $false }
    }
    return $true
}

function Test-Case {
    param(
        [string]$Name,
        [int]$ExitCode,
        [switch]$WantSuccess,
        [Nullable[bool]]$WantExe
    )

    $ok = if ($WantSuccess) { $ExitCode -eq 0 } else { $ExitCode -ne 0 }
    $detail = "exit=$ExitCode"

    if ($null -ne $WantExe) {
        $haveExe = Test-Path -LiteralPath $exePath
        $detail += ", exe=$haveExe"
        if ($haveExe -ne $WantExe) { $ok = $false }
    }

    if ($ok) {
        Write-Host ("  PASS  {0} ({1})" -f $Name, $detail)
    } else {
        Write-Host ("  FAIL  {0} ({1})" -f $Name, $detail) -ForegroundColor Red
        $script:failures += $Name
    }
}

$mmioOriginal   = [IO.File]::ReadAllBytes($mmioSrc)
$runnerOriginal = [IO.File]::ReadAllBytes($runner)

try {
    Write-Host "build.cmd exit-code contract:"

    # Positive control first. Without it, a build.cmd broken in some unrelated
    # way would make every negative case "pass" for the wrong reason.
    Remove-BuildOutput
    Test-Case -Name "clean build succeeds and produces the EXE" `
              -ExitCode (Invoke-Build) -WantSuccess -WantExe $true

    Remove-BuildOutput
    Test-Case -Name "NOHOSTTEST skips the tests and still builds" `
              -ExitCode (Invoke-Build -BuildArgs @("NOHOSTTEST")) -WantSuccess -WantExe $true

    # A failing host test must fail the build, and must not leave an EXE.
    $text = [Text.Encoding]::UTF8.GetString($mmioOriginal)
    if (-not $text.Contains($mutationAnchor)) {
        throw "mutation anchor not found in mmiodiag.c: '$mutationAnchor'"
    }
    [IO.File]::WriteAllText($mmioSrc, $text.Replace($mutationAnchor, "  PCI PM: mutated-by-regression-check"))
    Remove-BuildOutput
    Test-Case -Name "failing host test fails the build" `
              -ExitCode (Invoke-Build) -WantExe $false
    [IO.File]::WriteAllBytes($mmioSrc, $mmioOriginal)

    # Same contract when the runner itself fails to run rather than a check
    # failing - the propagation path is what is being asserted.
    [IO.File]::WriteAllText($runner, "@echo off`r`necho stub runner failing on purpose`r`nexit /b 1`r`n")
    Remove-BuildOutput
    Test-Case -Name "failing test runner fails the build" -ExitCode (Invoke-Build) -WantExe $false
    [IO.File]::WriteAllBytes($runner, $runnerOriginal)

    # An unrelated wmake failure must still propagate.
    Remove-BuildOutput
    Test-Case -Name "invalid wmake target fails the build" `
              -ExitCode (Invoke-Build -BuildArgs @("no-such-target"))
} finally {
    [IO.File]::WriteAllBytes($mmioSrc, $mmioOriginal)
    [IO.File]::WriteAllBytes($runner, $runnerOriginal)
    Remove-BuildOutput

    # Compare with the pre-run bytes, not HEAD: legitimate edits that existed
    # before this check must survive it and must not be mistaken for a failed
    # restore.
    $restoreFailures = @()
    if (-not (Test-FileMatchesSnapshot -Path $mmioSrc -Expected $mmioOriginal)) {
        $restoreFailures += $mmioSrc
    }
    if (-not (Test-FileMatchesSnapshot -Path $runner -Expected $runnerOriginal)) {
        $restoreFailures += $runner
    }
    if ($restoreFailures.Count -ne 0) {
        Write-Host "ERROR: mutated sources were not restored byte-for-byte:" -ForegroundColor Red
        $restoreFailures | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        exit 2
    }
}

Write-Host ""
if ($failures.Count -eq 0) {
    Write-Host ("build.cmd exit codes: {0} cases passed" -f 5)
    exit 0
}
Write-Host ("build.cmd exit codes: {0} failed - {1}" -f $failures.Count, ($failures -join "; ")) -ForegroundColor Red
exit 1
