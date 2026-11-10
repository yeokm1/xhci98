<#
.SYNOPSIS
Before a cut: everything make-release.ps1 reads rebuilt in this tree, then
its -CheckOnly (roadmap-usb4 task 48.10).

.DESCRIPTION
Run in the cut's tree after the version bump is committed, before
make-release.ps1. The 3.4.2.0 cut stopped twice on what another worktree
had built: a stale amd64 stor98.sys and a stale xhciqual.exe here, each a
rebuild in this tree (the owner, 2026-11-08). So this script builds every
input of the cut in this tree, in this order, stopping at the first
failure:

  1. the text checks (scripts\check-text.ps1)
  2. XHCIPFW (src\pfw\build.cmd), XHCIQUAL (xhciqual\build.cmd), the mark
     service (xhcimark\build.cmd), the installer (xhciinst\build.cmd) and
     its helper (xhciinst\di\build.cmd): the tools the gated builds'
     self-tests then read as they are
  3. both gated builds: scripts\build-driver.cmd all here and then
     scripts\build-driver.cmd all -amd64 here, one after the other; or with
     -TwoTrees, scripts\package\cut-builds.ps1, the amd64 build in a second
     worktree and imported here (releases\README.md, "Cutting a release")
  4. make-release.ps1 -CheckOnly, with -WdmFreeRoot when one is given

and then prints the cut's command. It refuses before building anything
unless the tree is clean, the version src\xhci_version.h names has no
directory under releases\ yet, no gated build lock is held here, and,
while scripts\package\wdmfree-pin.txt names a usb-wdm-free release, that
release's checkout is reachable: -WdmFreeRoot, or else
scripts\local\usb-wdm-free (git-ignored: a clone, or a junction to one,
mklink /J), holding the pinned tag at the pinned commit.

Each step's output goes to the console and to out\pre-cut\<step>.log, and
each step runs with TEMP and TMP in out\pre-cut\tmp\<step>\.

.PARAMETER WdmFreeRoot
The usb-wdm-free checkout, passed on to make-release.ps1; without it
scripts\local\usb-wdm-free.

.PARAMETER TwoTrees
The two gated builds at once through cut-builds.ps1, the amd64 one in a
second worktree, instead of both here one after the other.

.PARAMETER X64Tree
With -TwoTrees: cut-builds.ps1's -X64Tree.

.PARAMETER Plan
Check and print the steps; run none.

.PARAMETER SelfTest
Run the script's own checks over fixture trees and stand-in steps; build
nothing.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\pre-cut.ps1 -WdmFreeRoot <usb-wdm-free checkout>
#>

[CmdletBinding()]
param(
    [string]$WdmFreeRoot = "",
    [switch]$TwoTrees,
    [string]$X64Tree = "",
    [switch]$Plan,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

function Get-PreCutSteps {
    # The steps, in order: Id (the log's name), Title, Kind (cmd: a batch
    # file called through cmd.exe; ps: a PowerShell script) and Path
    # (relative to the tree) with Args.
    param([string]$WdmFreeRoot, [bool]$TwoTrees, [string]$X64Tree)
    $s = @()
    $s += [pscustomobject]@{ Id = "text"; Title = "text checks"; Kind = "ps"; Path = "scripts\check-text.ps1"; Args = @() }
    $s += [pscustomobject]@{ Id = "pfw"; Title = "XHCIPFW"; Kind = "cmd"; Path = "src\pfw\build.cmd"; Args = @() }
    $s += [pscustomobject]@{ Id = "xhciqual"; Title = "XHCIQUAL"; Kind = "cmd"; Path = "xhciqual\build.cmd"; Args = @() }
    $s += [pscustomobject]@{ Id = "xhcimark"; Title = "the mark service"; Kind = "cmd"; Path = "xhcimark\build.cmd"; Args = @() }
    $s += [pscustomobject]@{ Id = "xhciinst"; Title = "the installer"; Kind = "cmd"; Path = "xhciinst\build.cmd"; Args = @() }
    $s += [pscustomobject]@{ Id = "xhcidi"; Title = "the installer's helper"; Kind = "cmd"; Path = "xhciinst\di\build.cmd"; Args = @() }
    if ($TwoTrees) {
        $a = @()
        if ($X64Tree -ne "") { $a = @("-X64Tree", $X64Tree) }
        $s += [pscustomobject]@{ Id = "cut-builds"; Title = "both gated builds (cut-builds.ps1)"; Kind = "ps"; Path = "scripts\package\cut-builds.ps1"; Args = $a }
    } else {
        $s += [pscustomobject]@{ Id = "build-x86"; Title = "the x86 gated build"; Kind = "cmd"; Path = "scripts\build-driver.cmd"; Args = @("all") }
        $s += [pscustomobject]@{ Id = "build-amd64"; Title = "the amd64 gated build"; Kind = "cmd"; Path = "scripts\build-driver.cmd"; Args = @("all", "-amd64") }
    }
    $a = @("-CheckOnly")
    if ($WdmFreeRoot -ne "") { $a += @("-WdmFreeRoot", $WdmFreeRoot) }
    $s += [pscustomobject]@{ Id = "checkonly"; Title = "make-release.ps1 -CheckOnly"; Kind = "ps"; Path = "scripts\package\make-release.ps1"; Args = $a }
    return $s
}

function Get-PreCutFaults {
    # Every reason not to start, or none. $PinPath: the usb-wdm-free pin;
    # $LocalRoot: the default checkout (scripts\local\usb-wdm-free).
    param([string]$Repo, [string]$WdmFreeRoot, [string]$PinPath, [string]$LocalRoot, [bool]$TwoTrees, [string]$X64Tree)
    $f = @()
    if ($X64Tree -ne "" -and -not $TwoTrees) { $f += "-X64Tree goes with -TwoTrees." }
    $tree = Get-GitTreeState -Repo $Repo
    if ($tree.Head -eq "none") { $f += "git could not say what '$Repo' is at." }
    elseif ($tree.Dirty) { $f += "'$Repo' has uncommitted changes: commit the version bump first, so the gated records are clean." }
    $ver = ""
    try { $ver = Get-PackageVersion -Repo $Repo } catch { $f += $_.Exception.Message }
    if ($ver -ne "" -and (Test-Path -LiteralPath (Join-Path $Repo "releases\$ver"))) {
        $f += "releases\$ver is there: $ver was cut already. Bump the version in src\xhci_version.h and the files its header lists first."
    }
    foreach ($arch in @("x86", "amd64")) {
        $why = Get-GatedRunFault -Root $Repo -Arch $arch -Flavor @("release", "qemu")
        if ($why -ne "") { $f += $why }
    }
    $pin = $null
    try { $pin = Read-WdmFreePin -Path $PinPath } catch { $f += $_.Exception.Message }
    if ($null -ne $pin) {
        $root = if ($WdmFreeRoot -ne "") { $WdmFreeRoot } else { $LocalRoot }
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            if ($WdmFreeRoot -ne "") {
                $f += "-WdmFreeRoot '$WdmFreeRoot' is not a folder."
            } else {
                $f += ("wdmfree-pin.txt names usb-wdm-free {0}, and there is no '{1}': pass -WdmFreeRoot <checkout>, or make the junction (mklink /J ""{1}"" <checkout>)." -f $pin.Tag, $LocalRoot)
            }
        } else {
            $savedEap = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $at = (& git -C $root rev-parse -q --verify ("refs/tags/" + $pin.Tag + "^{commit}") 2>$null | Out-String).Trim()
            } finally {
                $ErrorActionPreference = $savedEap
            }
            if ($at -ne $pin.Commit) {
                $f += ("'{0}' does not hold usb-wdm-free {1} at {2} (wdmfree-pin.txt){3}: fetch its tags, or name the right checkout." -f $root, $pin.Tag, $pin.Commit, $(if ($at -ne "") { ", the tag is at $at" } else { "" }))
            }
        }
    }
    return @($f)
}

function Invoke-PreCutStep {
    # Runs one step in $Repo with its own TEMP and TMP, its output to the
    # console and its log; returns the exit code. A cut runs every
    # self-test in full (task 48.34; owner, 2026-11-09): each step gets
    # XHCI98_FULL_SELFTESTS=1 and never the pre-merge builds'
    # XHCI98_SELFTEST_SKIP_OK, whatever the session that started it holds.
    param([string]$Repo, $Step)
    $dir = Join-Path $Repo "out\pre-cut"
    $tmp = Join-Path $dir ("tmp\" + $Step.Id)
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $log = Join-Path $dir ($Step.Id + ".log")
    $path = Join-Path $Repo $Step.Path
    $quoted = @($Step.Args | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_ + '"' } else { $_ } })
    if ($Step.Kind -eq "cmd") {
        $line = ('call "{0}" {1}' -f $path, ($quoted -join " ")).TrimEnd()
        $exe = $env:ComSpec
        $argv = @("/d", "/c", $line)
    } else {
        $exe = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
        $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $path) + @($Step.Args)
    }
    $savedTemp = $env:TEMP
    $savedTmp = $env:TMP
    $savedFull = [Environment]::GetEnvironmentVariable("XHCI98_FULL_SELFTESTS", "Process")
    $savedSkip = [Environment]::GetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", "Process")
    $savedEap = $ErrorActionPreference
    $here = Get-Location
    try {
        $env:TEMP = $tmp
        $env:TMP = $tmp
        [Environment]::SetEnvironmentVariable("XHCI98_FULL_SELFTESTS", "1", "Process")
        [Environment]::SetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", $null, "Process")
        $ErrorActionPreference = "Continue"
        Set-Location -LiteralPath $Repo
        & $exe @argv 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log | Write-Host
        return $LASTEXITCODE
    } finally {
        Set-Location $here
        $env:TEMP = $savedTemp
        $env:TMP = $savedTmp
        [Environment]::SetEnvironmentVariable("XHCI98_FULL_SELFTESTS", $savedFull, "Process")
        [Environment]::SetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", $savedSkip, "Process")
        $ErrorActionPreference = $savedEap
    }
}

function Invoke-PreCut {
    # The checks, then each step by $Runner (Repo, Step -> exit code) until
    # one fails. Throws with every reason when it refuses or a step fails;
    # returns the steps run.
    param([string]$Repo, [string]$WdmFreeRoot, [bool]$TwoTrees, [string]$X64Tree, [bool]$Plan,
          [string]$PinPath, [string]$LocalRoot, [scriptblock]$Runner)
    $faults = @(Get-PreCutFaults -Repo $Repo -WdmFreeRoot $WdmFreeRoot -PinPath $PinPath -LocalRoot $LocalRoot -TwoTrees $TwoTrees -X64Tree $X64Tree)
    if ($faults.Count -ne 0) { throw ("nothing was built:`n  " + ($faults -join "`n  ")) }
    $steps = @(Get-PreCutSteps -WdmFreeRoot $WdmFreeRoot -TwoTrees $TwoTrees -X64Tree $X64Tree)
    $done = @()
    $n = 0
    foreach ($st in $steps) {
        $n++
        Write-Step ("pre-cut {0}/{1}: {2} ({3} {4})" -f $n, $steps.Count, $st.Title, $st.Path, ($st.Args -join " "))
        if ($Plan) { continue }
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $rc = & $Runner $Repo $st
        $done += $st.Id
        Write-Host ("--- {0}: exit {1}, {2}:{3:00}" -f $st.Title, $rc, [int][Math]::Floor($watch.Elapsed.TotalMinutes), $watch.Elapsed.Seconds)
        if ($rc -ne 0) {
            throw ("{0} failed (exit {1}); its log is out\pre-cut\{2}.log. The steps after it were not run." -f $st.Title, $rc, $st.Id)
        }
    }
    return $done
}

function Invoke-PreCutSelfTest {
    . (Join-Path (Split-Path -Parent $PSScriptRoot) "test-harness.ps1")
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ("xhci98-precut-" + [System.IO.Path]::GetRandomFileName())
    $savedEap = $ErrorActionPreference
    # The fixtures' git is the suite's, whatever the caller's configuration
    # (task 45.51's policy, common.ps1), and back as it was at the end.
    $gitPolicy = $null
    try {
        $gitPolicy = Set-SelfTestGitPolicy -ScratchRoot (Join-Path $work "policy")
        $fx = Join-Path $work "tree"
        foreach ($d in @("src", "releases\3.4.2.0", "scripts\package")) { New-Item -ItemType Directory -Force -Path (Join-Path $fx $d) | Out-Null }
        [System.IO.File]::WriteAllText((Join-Path $fx "src\xhci_version.h"), "#define XHCI_VER_STR            `"3.5.0.0`"`r`n")
        [System.IO.File]::WriteAllText((Join-Path $fx "releases\3.4.2.0\readme.txt"), "a")
        [System.IO.File]::WriteAllText((Join-Path $fx ".gitignore"), "out/`r`nlocal/`r`n")
        $wf = Join-Path $work "wdmfree"
        New-Item -ItemType Directory -Force -Path $wf | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $wf "a.txt"), "a")
        $ErrorActionPreference = "Continue"
        $gitRc = 0
        foreach ($r in @($fx, $wf)) {
            & git -C $r init -q 2>&1 | Out-Null
            $gitRc = $gitRc -bor $LASTEXITCODE
            & git -C $r add -A 2>&1 | Out-Null
            $gitRc = $gitRc -bor $LASTEXITCODE
            & git -C $r -c user.name=xhci98-test -c user.email=test@invalid commit -q -m a 2>&1 | Out-Null
            $gitRc = $gitRc -bor $LASTEXITCODE
        }
        & git -C $wf tag v9.9.9 2>&1 | Out-Null
        $gitRc = $gitRc -bor $LASTEXITCODE
        $wfHead = ((& git -C $wf rev-parse HEAD 2>$null) | Out-String).Trim()
        $ErrorActionPreference = $savedEap
        if ($gitRc -ne 0 -or $wfHead -notmatch '^[0-9a-f]{40}$') { throw "the self-test's fixture repositories could not be made (git exit $gitRc, HEAD '$wfHead')." }
        $noPin = Join-Path $work "pin-none.txt"
        [System.IO.File]::WriteAllText($noPin, "# no release`r`n")
        $pin = Join-Path $work "pin.txt"
        $pinText = "Tag: v9.9.9`r`nCommit: $wfHead`r`nPackage: audio/uac1 | W98SE`r`n"
        foreach ($x in $script:WdmFreeExtras) { $pinText += ("SHA256: {0} {1}`r`n" -f ("0" * 64), $x) }
        [System.IO.File]::WriteAllText($pin, $pinText)
        $local = Join-Path $fx "local\usb-wdm-free"
        $script:ran = @()
        $ok = { param($Repo, $Step) $script:ran += $Step.Id; return 0 }

        Write-Step "a clean tree at a new version: every step, in order"
        $r = Invoke-PreCut -Repo $fx -WdmFreeRoot "" -TwoTrees $false -X64Tree "" -Plan $false -PinPath $noPin -LocalRoot $local -Runner $ok
        Assert-True (($r -join ",") -eq "text,pfw,xhciqual,xhcimark,xhciinst,xhcidi,build-x86,build-amd64,checkonly" -and ($script:ran -join ",") -eq ($r -join ",")) `
            ("the steps were '{0}'." -f ($r -join ","))
        $st = @(Get-PreCutSteps -WdmFreeRoot "C:\w f" -TwoTrees $true -X64Tree "D:\x64")
        Assert-True ((($st | ForEach-Object { $_.Id }) -join ",") -eq "text,pfw,xhciqual,xhcimark,xhciinst,xhcidi,cut-builds,checkonly") "-TwoTrees does not run cut-builds.ps1 in place of the two builds."
        Assert-True (($st[6].Args -join "|") -eq "-X64Tree|D:\x64" -and ($st[7].Args -join "|") -eq "-CheckOnly|-WdmFreeRoot|C:\w f") "-X64Tree or -WdmFreeRoot is not passed on."
        $st = @(Get-PreCutSteps -WdmFreeRoot "" -TwoTrees $false -X64Tree "")
        Assert-True (($st[6].Args -join "|") -eq "all" -and ($st[7].Args -join "|") -eq "all|-amd64" -and $st[6].Path -eq "scripts\build-driver.cmd" -and ($st[8].Args -join "|") -eq "-CheckOnly") "the builds or -CheckOnly have the wrong arguments."

        Write-Step "a step that fails stops the run"
        $script:ran = @()
        $threw = ""
        try { [void](Invoke-PreCut -Repo $fx -WdmFreeRoot "" -TwoTrees $false -X64Tree "" -Plan $false -PinPath $noPin -LocalRoot $local -Runner { param($Repo, $Step) $script:ran += $Step.Id; if ($Step.Id -eq "xhcimark") { return 3 }; return 0 }) } catch { $threw = $_.Exception.Message }
        Assert-True ($threw.Contains("exit 3") -and ($script:ran -join ",") -eq "text,pfw,xhciqual,xhcimark") ("a failing step did not stop the run: '{0}' after {1}" -f $threw, ($script:ran -join ","))

        Write-Step "a step runs every self-test in full, whatever the session holds"
        New-Item -ItemType Directory -Force -Path (Join-Path $fx "out") | Out-Null
        $envStep = Join-Path $fx "out\env-step.ps1"
        $envOut = Join-Path $work "env-out.txt"
        [System.IO.File]::WriteAllText($envStep, ('[System.IO.File]::WriteAllText("{0}", ("full=" + $env:XHCI98_FULL_SELFTESTS + ";skip=" + $env:XHCI98_SELFTEST_SKIP_OK))' -f $envOut))
        $envSaved = @([Environment]::GetEnvironmentVariable("XHCI98_FULL_SELFTESTS", "Process"), [Environment]::GetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", "Process"))
        try {
            [Environment]::SetEnvironmentVariable("XHCI98_FULL_SELFTESTS", $null, "Process")
            [Environment]::SetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", "1", "Process")
            $envRc = Invoke-PreCutStep -Repo $fx -Step ([pscustomobject]@{ Id = "envprobe"; Title = "env"; Kind = "ps"; Path = "out\env-step.ps1"; Args = @() })
            $envGot = if (Test-Path -LiteralPath $envOut) { [System.IO.File]::ReadAllText($envOut) } else { "" }
            Assert-True ($envRc -eq 0 -and $envGot -eq "full=1;skip=") ("a pre-cut step saw '{0}' (exit {1}), not full=1 and no skip." -f $envGot, $envRc)
            Assert-True ([Environment]::GetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", "Process") -eq "1" -and $null -eq [Environment]::GetEnvironmentVariable("XHCI98_FULL_SELFTESTS", "Process")) "Invoke-PreCutStep did not give the session its values back."
        } finally {
            [Environment]::SetEnvironmentVariable("XHCI98_FULL_SELFTESTS", $envSaved[0], "Process")
            [Environment]::SetEnvironmentVariable("XHCI98_SELFTEST_SKIP_OK", $envSaved[1], "Process")
        }

        Write-Step "-Plan runs nothing"
        $script:ran = @()
        $r = Invoke-PreCut -Repo $fx -WdmFreeRoot "" -TwoTrees $false -X64Tree "" -Plan $true -PinPath $noPin -LocalRoot $local -Runner $ok
        Assert-True ($script:ran.Count -eq 0) "-Plan ran a step."

        Write-Step "the refusals, before any step"
        $refuse = {
            param([string]$Want, [hashtable]$P)
            $script:ran = @()
            $a = @{ Repo = $fx; WdmFreeRoot = ""; TwoTrees = $false; X64Tree = ""; Plan = $false; PinPath = $noPin; LocalRoot = $local; Runner = $ok }
            foreach ($k in $P.Keys) { $a[$k] = $P[$k] }
            $t = ""
            try { [void](Invoke-PreCut @a) } catch { $t = $_.Exception.Message }
            Assert-True ($t.Contains($Want) -and $script:ran.Count -eq 0) ("wanted a refusal naming '{0}' and no step run; got '{1}', {2} run." -f $Want, $t, $script:ran.Count)
        }
        & $refuse "-X64Tree goes with -TwoTrees" @{ X64Tree = "D:\x" }
        & $refuse "names usb-wdm-free v9.9.9, and there is no" @{ PinPath = $pin }
        & $refuse "is not a folder" @{ PinPath = $pin; WdmFreeRoot = (Join-Path $work "none") }
        [void](Invoke-PreCut -Repo $fx -WdmFreeRoot $wf -TwoTrees $false -X64Tree "" -Plan $true -PinPath $pin -LocalRoot $local -Runner $ok)
        Assert-True $true "the pinned tag at the pinned commit is admitted."
        $badPin = Join-Path $work "pin-bad.txt"
        [System.IO.File]::WriteAllText($badPin, $pinText.Replace($wfHead, ("1" * 40)))
        & $refuse "does not hold usb-wdm-free v9.9.9" @{ PinPath = $badPin; WdmFreeRoot = $wf }
        New-Item -ItemType Directory -Force -Path (Join-Path $fx "out\gated-amd64.lock") | Out-Null
        & $refuse "gated-amd64.lock' is held" @{}
        Remove-Item -LiteralPath (Join-Path $fx "out\gated-amd64.lock") -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $fx "releases\3.5.0.0") | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $fx "releases\3.5.0.0\readme.txt"), "a")
        & $refuse "uncommitted changes" @{}
        $ErrorActionPreference = "Continue"
        & git -C $fx add -A 2>&1 | Out-Null
        & git -C $fx -c user.name=xhci98-test -c user.email=test@invalid commit -q -m b 2>&1 | Out-Null
        $gitRc = $LASTEXITCODE
        $ErrorActionPreference = $savedEap
        Assert-True ($gitRc -eq 0) "the fixture's second commit failed (git exit $gitRc)."
        & $refuse "releases\3.5.0.0 is there" @{}
    } catch {
        Assert-True $false ("the self-test stopped: " + $_.Exception.Message)
    } finally {
        $ErrorActionPreference = $savedEap
        Restore-SelfTestGitPolicy -Policy $gitPolicy
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host ""
    if ($script:failures.Count -gt 0) {
        Write-Err ("pre-cut self-tests FAILED: {0} of {1} check(s)." -f $script:failures.Count, $script:checks)
        return 1
    }
    Write-Ok ("pre-cut self-tests passed ({0} checks)" -f $script:checks)
    return 0
}

if ($SelfTest) { exit (Invoke-PreCutSelfTest) }

$rc = 0
try {
    $repo = Get-RepoRoot
    $runner = { param($Repo, $Step) return (Invoke-PreCutStep -Repo $Repo -Step $Step) }
    [void](Invoke-PreCut -Repo $repo -WdmFreeRoot $WdmFreeRoot -TwoTrees ([bool]$TwoTrees) -X64Tree $X64Tree -Plan ([bool]$Plan) `
        -PinPath (Get-DefaultWdmFreePinPath) -LocalRoot (Get-DefaultWdmFreeRoot) -Runner $runner)
    if (-not $Plan) {
        $cut = "powershell -ExecutionPolicy Bypass -File scripts\package\make-release.ps1 -TestSignPfx <path to xhci98-test.pfx>"
        if ($WdmFreeRoot -ne "") { $cut += " -WdmFreeRoot `"$WdmFreeRoot`"" }
        Write-Ok "pre-cut: every input of the cut is built in this tree, and -CheckOnly passed."
        Write-Host "  Next, the cut:"
        Write-Host "  $cut"
    }
} catch {
    Write-Err $_.Exception.Message
    $rc = 1
}
exit $rc
