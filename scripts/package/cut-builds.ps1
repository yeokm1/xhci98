<#
.SYNOPSIS
Run a cut's two gated builds at once in two worktrees, then take the amd64
build into the cut's tree (roadmap-usb4.md task 44.31).

.DESCRIPTION
docs\contributing\build-and-test.md, "The cut's two builds at once", is the
design. This tree is the cut's (tree A): it builds x86, and the tools and
make-release.ps1 run here. -X64Tree names tree B, a second worktree of the
same repository at this tree's commit, which builds amd64 and nothing else.

The script starts scripts\build-driver.cmd all here and
scripts\build-driver.cmd all -amd64 in tree B as two processes at once,
each with TEMP and TMP set to its own tree's out\tmp-<arch>\ and its output
in its tree's out\cut-build-<arch>.log, waits for both, prints each one's
exit code and time, and when both passed imports tree B's amd64 build:
its output directories and its gated records copied into this tree, each
record held to its binaries before the copy and to the copies after it
(package-common.ps1, Import-GatedBuild). Afterwards make-release.ps1
-CheckOnly, the cut and make-package.ps1 -SignOnly read the amd64 build
here as if this tree had made it.

Before any build it refuses unless tree B is another worktree of this
repository at this tree's HEAD, both trees are clean, and neither holds
either architecture's gated build lock.

.PARAMETER X64Tree
Tree B, the worktree that builds amd64.

.PARAMETER Serial
Run the two builds one after the other, x86 first, rather than at once:
the baseline the saving is measured against. The import is the same.

.PARAMETER ImportOnly
Build nothing: import the amd64 build tree B already holds, for builds
run by hand.

.PARAMETER NoTargetEvidence
With -ImportOnly alone: lets the import admit records whose import gates
ran without target evidence, for a host with no target binaries staged.
The imported record keeps saying so, and make-package.ps1 -SignOnly still
asks for its own -NoTargetEvidence. A run that builds refuses it: the x86
build a cut takes needs the evidence (the helper's import gate).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\cut-builds.ps1 -X64Tree E:\xhci98-wt\cut-x64
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$X64Tree,
    [switch]$Serial,
    [switch]$ImportOnly,
    [switch]$NoTargetEvidence
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

$rc = 0
try {
    # test-package.ps1 drives this script's refusals, and Invoke-CutBuilds
    # itself with stand-in builds; under it this script never starts one.
    $neverBuild = $null
    if ("$env:XHCI98_PACKAGER_SELFTEST" -eq "1") {
        $neverBuild = { param($Arch) throw "the builds were not started: XHCI98_PACKAGER_SELFTEST is 1 (a self-test run, which never builds)." }
    }
    [void](Invoke-CutBuilds -Repo (Get-RepoRoot) -X64Tree $X64Tree -Serial:$Serial -ImportOnly:$ImportOnly `
                            -NoTargetEvidence:$NoTargetEvidence -BeforeStart $neverBuild)
    Write-Host "  Next: make-release.ps1 -CheckOnly, then the cut (releases\README.md, ""Cutting a release"")."
} catch {
    Write-Err $_.Exception.Message
    $rc = 1
}
exit $rc
