#
# The check helper every PowerShell self-test suite in this repository counts
# with, and the two counters it keeps.
#
# There were five byte-identical copies - the INF gate's, the packager's, the
# launcher generators', and the import gate's two (the 2026-09-07 audit's J6).
# That is the same shape, one language over, as the twelve diverged copies of
# the C check helpers that `test\test_harness.h` replaced in the same audit,
# and it has the same failure mode: a suite whose private copy stops counting,
# or stops printing, reports "all passed" over checks that never ran.
#
# **`$script:` binds to the SOURCING script, not to this file.** A dot-sourced
# function looks its `$script:` variables up in the scope it was defined in,
# and dot-sourcing defines it in the caller's script scope - so each suite
# gets its own `$checks` and `$failures`, initialised here, and the counts
# stay per-suite exactly as they were when each kept its own copy.
#
# Each suite still prints its OWN summary line and chooses its own exit code:
# those differ in wording and in what they say about the work directory, and
# build-driver.cmd and the runbooks quote them.
#

$script:failures = @()
$script:checks = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:checks++
    if (-not $Condition) {
        $script:failures += $Message
        Write-Host "FAIL: $Message" -ForegroundColor Red
    }
}
