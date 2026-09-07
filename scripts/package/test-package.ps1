<#
.SYNOPSIS
Regression tests for the install-media packager (roadmap Phase 3 task 7).

.DESCRIPTION
scripts\package\make-package.ps1 is the only supported way to build the media a
VM is installed from, and two of its jobs fail quietly if they are wrong:

  - **where** each file is staged. [SourceDisksFiles] may put a source file in
    a subdirectory. If the packager staged flat while the gate authenticated
    the subdirectory path (or the reverse), a package could be verified at one
    path and consumed at another. That is the same shape as the per-target
    usbd.sys bug the 1.0.0.0 media had to close, so the packager takes the
    layout from check-inf.ps1 -EmitMediaLayout instead of parsing
    [SourceDisksFiles] a second time - and this asserts it actually follows it.

  - **which directory** a relative -OutDir means. [Path]::GetFullPath resolves
    against the process directory, which Set-Location does not update, so a
    session that has changed directory could stage a package somewhere the
    caller never named.

Every case uses stand-in files - text "drivers" - so nothing here needs a
build, the git-ignored tools\ staging, a VM, or any Microsoft binary. One
stand-in driver carries the exact probe marker to prove diagnostic artifacts
are rejected before staging.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package\test-package.ps1
#>

[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "common.ps1")
. (Join-Path $PSScriptRoot "package-common.ps1")

$repo = Get-RepoRoot
$packager = Join-Path $PSScriptRoot "make-package.ps1"
$releaser = Join-Path $PSScriptRoot "make-release.ps1"
$prodInf = Join-Path $repo "src\xhci98.inf"

. (Join-Path (Split-Path -Parent $PSScriptRoot) "test-harness.ps1")

function Get-StandInSha {
    param([byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString("X2") }) -join "") }
    finally { $sha.Dispose() }
}

function Invoke-Packager {
    param([string[]]$Arguments, [switch]$WithBinaryGates)
    # The stand-in "driver" is a text file, so the packager's pre-staging host
    # test suite and import gate are skipped for every case except the one
    # that is about them.
    if (-not $WithBinaryGates) { $Arguments = $Arguments + @("-SkipBinaryGates") }
    $psArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $packager) + $Arguments
    # ErrorActionPreference relaxed across the call, as
    # `scripts\import-gate\check-imports.ps1` relaxes it for dumpbin (repo audit
    # D5): a native command's stderr line becomes an ErrorRecord in Windows
    # PowerShell 5.1, and under "Stop" the first one aborts this self-test with
    # an empty message - on a harness whose whole job is to drive the packager
    # into its refusal paths and read what it said.
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & powershell.exe @psArgs 2>&1 | Out-String
    } finally {
        $ErrorActionPreference = $saved
    }
    return @{ Output = $out; ExitCode = $LASTEXITCODE }
}

function Invoke-Releaser {
    # make-release.ps1 -UploadSetOnly, driven the same way and relaxed for the
    # same reason as Invoke-Packager above.
    param([string[]]$Arguments)
    $psArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $releaser) + $Arguments
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & powershell.exe @psArgs 2>&1 | Out-String
    } finally {
        $ErrorActionPreference = $saved
    }
    return @{ Output = $out; ExitCode = $LASTEXITCODE }
}

$tempBase = [System.IO.Path]::GetFullPath($env:TEMP)
$script:work = Join-Path $tempBase ("xhci98-package-test-" + [System.IO.Path]::GetRandomFileName())

try {
    New-Item -ItemType Directory -Path $script:work | Out-Null

    # --- stand-in inputs ----------------------------------------------------
    $srcDir = Join-Path $script:work "src"
    New-Item -ItemType Directory -Path $srcDir | Out-Null
    #
    # Every stand-in "driver" carries a flavour marker, because the packager
    # reads one out of the image and refuses a binary with none (task 13-L.1).
    # `debug` is make-package.ps1's default -Flavor, so that is what these say;
    # the qemu cases below deliberately say something else.
    #
    $driver = Join-Path $srcDir "xhci98.sys"
    Set-Content -LiteralPath $driver -Value `
        "not a real driver XHCI98_FLAVOUR_DEBUG" -Encoding ASCII

    function New-Inf {
        param([string]$Name, [scriptblock]$Mutate)
        $text = [System.IO.File]::ReadAllText($prodInf)
        if ($null -ne $Mutate) { $text = & $Mutate $text }
        $path = Join-Path $script:work "$Name.inf"
        [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.ASCIIEncoding))
        return $path
    }

    $plainInf = New-Inf -Name "plain" -Mutate $null

    # --- the baseline package must build ------------------------------------
    Write-Step "a flat package builds and gates"
    $flatOut = Join-Path $script:work "pkg-flat"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $flatOut)
    Assert-True ($r.ExitCode -eq 0) ("the baseline package was rejected:`n" + $r.Output)
    foreach ($f in @("xhci98.inf", "xhci98.sys")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $flatOut $f)) "'$f' is missing from the flat package."
    }

    # --- a diagnostic probe driver must never become install media ----------
    Write-Step "a diagnostic probe driver is refused"
    $probeDriver = Join-Path $srcDir "xhci98-probe.sys"
    Set-Content -LiteralPath $probeDriver -Value `
        "stand-in XHCI98_PROBE_BUILD_DO_NOT_DEPLOY XHCI98_FLAVOUR_DEBUG driver" `
        -Encoding ASCII
    $probeOut = Join-Path $script:work "pkg-probe"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $probeDriver,
        "-OutDir", $probeOut)
    Assert-True ($r.ExitCode -ne 0) "a diagnostic probe driver was accepted for packaging."
    Assert-True ($r.Output -match "diagnostic probe build") `
        ("expected the rejection to identify the probe build. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $probeOut)) `
        "the output directory was created for a rejected probe driver."

    # --- the flavour marker decides what a package is (task 13-L.1) --------
    #
    # There are three flavours and two of them are checked builds, so
    # VS_FF_DEBUG cannot separate `debug` from `qemu` and neither can a path -
    # "objchk_qemu" contains "objchk", and -DriverPath can name a binary
    # anywhere. The marker in the image is what decides, and these cases are
    # what make that true rather than intended: only qemu carries the port-0xE9
    # mirror, and that import is the sole delta between 0.0.0.4's two published
    # binaries, of which the debug one gave the ThinkPad E460 a Code 2 under
    # Windows 98 SE.
    #
    Write-Step "the flavour marker in the image decides, not the path"

    # 1. A binary with no marker at all is a build from before this mechanism
    #    existed. It is refused rather than assumed to be release: an image
    #    whose flavour cannot be read is one nobody can report against.
    $noFlavourDriver = Join-Path $srcDir "xhci98-noflavour.sys"
    Set-Content -LiteralPath $noFlavourDriver -Encoding ASCII -Value `
        "stand-in driver from before the flavour marker existed"
    $nfOut = Join-Path $script:work "pkg-noflavour"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $noFlavourDriver,
        "-OutDir", $nfOut)
    Assert-True ($r.ExitCode -ne 0) "a driver with no flavour marker was packaged."
    Assert-True ($r.Output -match "XHCI98_FLAVOUR") `
        ("expected the refusal to name the missing marker. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $nfOut)) `
        "the output directory was created for a driver with no flavour marker."

    # 2. A qemu binary staged under -Flavor debug is the failure this exists to
    #    prevent: media labelled `debug` carrying the import no published
    #    binary may have.
    $qemuDriver = Join-Path $srcDir "xhci98-qemu.sys"
    Set-Content -LiteralPath $qemuDriver -Encoding ASCII -Value `
        "stand-in XHCI98_FLAVOUR_QEMU driver"
    $qwOut = Join-Path $script:work "pkg-qemu-as-debug"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $qemuDriver,
        "-OutDir", $qwOut)
    Assert-True ($r.ExitCode -ne 0) "a qemu binary was staged as the debug flavour."
    Assert-True ($r.Output -match "qemu") `
        ("expected the refusal to name the flavour it found. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $qwOut)) `
        "the output directory was created for a mislabelled flavour."

    # 3. The same mistake the other way round: a debug binary asked for as qemu.
    $dqOut = Join-Path $script:work "pkg-debug-as-qemu"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", $dqOut, "-Flavor", "qemu")
    Assert-True ($r.ExitCode -ne 0) "a debug binary was staged as the qemu flavour."
    Assert-True (-not (Test-Path -LiteralPath $dqOut)) `
        "the output directory was created for a mislabelled flavour."

    # 4. And qemu DOES stage when it is asked for by name and the image agrees.
    #    It has to: that is how the flavour reaches a guest at all. What must
    #    never happen is publishing it, and make-release.ps1 owns that refusal.
    $qOut = Join-Path $script:work "pkg-qemu"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $qemuDriver,
        "-OutDir", $qOut, "-Flavor", "qemu")
    Assert-True ($r.ExitCode -eq 0) ("the qemu flavour was refused by make-package:`n" + $r.Output)
    Assert-True ($r.Output -match "NEVER PUBLISHED") `
        ("expected a banner saying the qemu flavour is never published. Output:`n" + $r.Output)
    foreach ($f in @("xhci98.inf", "xhci98.sys")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $qOut $f)) `
            "'$f' is missing from the qemu package."
    }

    # --- task 12.3's exception admits one artifact, and only one ------------
    #
    # The exception exists so a package that installs, loads and then fails
    # inside StartController can be staged at all. What it must NOT become is a
    # flag that packages any diagnostic build, so all four combinations are
    # driven here: the artifact with and without the switch, and the switch
    # with an image that is a probe build rather than the artifact.
    #
    Write-Step "the failed-start artifact is admitted only by name"
    $failStartDriver = Join-Path $srcDir "xhci98-failstart.sys"
    Set-Content -LiteralPath $failStartDriver -Encoding ASCII -Value `
        ("stand-in XHCI98_PROBE_BUILD_DO_NOT_DEPLOY " +
         "XHCI98_FAILSTART_ARTIFACT_TASK_12_3 XHCI98_FLAVOUR_DEBUG driver")

    # 1. Without the switch it is a diagnostic build like any other.
    $fsRefusedOut = Join-Path $script:work "pkg-failstart-refused"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $failStartDriver,
        "-OutDir", $fsRefusedOut)
    Assert-True ($r.ExitCode -ne 0) `
        "the failed-start artifact was packaged without -FailStartArtifact."
    Assert-True (-not (Test-Path -LiteralPath $fsRefusedOut)) `
        "the output directory was created for an artifact refused before staging."

    # 2. The switch does not widen to a probe build that is not the artifact.
    $fsWrongOut = Join-Path $script:work "pkg-failstart-wrong"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $probeDriver,
        "-OutDir", $fsWrongOut, "-FailStartArtifact")
    Assert-True ($r.ExitCode -ne 0) `
        "-FailStartArtifact packaged a probe build that is not task 12.3's artifact."
    Assert-True ($r.Output -match "XHCI98_FAILSTART_ARTIFACT_TASK_12_3") `
        ("expected the refusal to name the marker it wanted. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $fsWrongOut)) `
        "the output directory was created for a build the exception does not cover."

    # 3. An artifact that has lost the do-not-deploy marker is refused too -
    #    otherwise the switch would be admitting an image the gate could not
    #    have refused on its own.
    $fsBareDriver = Join-Path $srcDir "xhci98-failstart-bare.sys"
    Set-Content -LiteralPath $fsBareDriver -Encoding ASCII -Value `
        ("stand-in XHCI98_FAILSTART_ARTIFACT_TASK_12_3 XHCI98_FLAVOUR_DEBUG " +
         "driver with no probe marker")
    $fsBareOut = Join-Path $script:work "pkg-failstart-bare"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $fsBareDriver,
        "-OutDir", $fsBareOut, "-FailStartArtifact")
    Assert-True ($r.ExitCode -ne 0) `
        "an artifact with no do-not-deploy marker was staged."
    Assert-True ($r.Output -match "XHCI98_PROBE_BUILD_DO_NOT_DEPLOY") `
        ("expected the refusal to name the missing marker. Output:`n" + $r.Output)

    # 4. And with both markers and the switch it stages, loudly.
    $fsOut = Join-Path $script:work "pkg-failstart"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $failStartDriver,
        "-OutDir", $fsOut, "-FailStartArtifact")
    Assert-True ($r.ExitCode -eq 0) ("the failed-start artifact was rejected:`n" + $r.Output)
    Assert-True ($r.Output -match "FAILED-START ARTIFACT") `
        ("expected a banner naming the artifact. Output:`n" + $r.Output)
    foreach ($f in @("xhci98.inf", "xhci98.sys")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $fsOut $f)) `
            "'$f' is missing from the failed-start package; it has to be installable to be worth anything."
    }

    # --- task 12.4's experiment package -------------------------------------
    #
    # One variable: the DriverVer date's leading zeros. The cases below are the
    # two ways that could go wrong quietly - a package that differs in more than
    # the padding (then it measures nothing) and a switch that stages the
    # ordinary date anyway (then it measures nothing either, and says it did).
    #
    #
    # **Against a date this test owns, not against the live INF's.** The
    # packager refuses the switch when the source date is already unpadded,
    # because then there is nothing to vary - and the shipping date moves with
    # every release. A cut on, say, 15 October 2026 has no leading zero to
    # strip in either field, so the packager would throw and this case, which
    # asserts exit 0, would fail the BUILD from the first cut carrying it. The
    # experiment is about the padding, so the date it runs on has to be one
    # with padding, and that is this file's business rather than the release
    # calendar's. The 2026-09-07 audit's H12.
    #
    Write-Step "the unpadded-date experiment changes the date and nothing else"
    $paddedInf = New-Inf -Name "padded-date" -Mutate {
        param($t) $t -replace '(?m)^DriverVer=\d{1,2}/\d{1,2}/(\d{4}),', 'DriverVer=01/02/$1,'
    }
    Assert-True ([System.IO.File]::ReadAllText($paddedInf) -match '(?m)^DriverVer=01/02/\d{4},') `
        "the padded-date INF this case derives was not produced; fix the pattern, not the packager."
    $dfOut = Join-Path $script:work "pkg-datefmt"
    $r = Invoke-Packager @("-InfPath", $paddedInf, "-DriverPath", $driver,
        "-OutDir", $dfOut, "-UnpaddedDriverVerExperiment")
    Assert-True ($r.ExitCode -eq 0) ("the unpadded-date package was rejected:`n" + $r.Output)
    Assert-True ($r.Output -match "UNPADDED-DriverVer EXPERIMENT") `
        ("expected a banner naming the experiment. Output:`n" + $r.Output)
    if (Test-Path -LiteralPath (Join-Path $dfOut "xhci98.inf")) {
        $stagedText = [System.IO.File]::ReadAllText((Join-Path $dfOut "xhci98.inf"))
        $prodText = [System.IO.File]::ReadAllText($paddedInf)
        Assert-True ($stagedText -match '(?m)^DriverVer=\d{1,2}/\d{1,2}/\d{4},') `
            "the staged INF has no DriverVer date at all."
        Assert-True ($stagedText -notmatch '(?m)^DriverVer=0\d/') `
            "the staged INF's DriverVer month is still zero-padded, so nothing was varied."
        Assert-True ($stagedText -match '(?m)^DriverVer=1/2/\d{4},') `
            "the staged INF's date is not the unpadded form of the one this case supplied."
        # The single-difference property, checked rather than asserted: every
        # other line must be byte-identical to the INF it was derived from.
        $a = $prodText -split "`r`n"
        $b = $stagedText -split "`r`n"
        Assert-True ($a.Count -eq $b.Count) "the derived INF has a different number of lines."
        $differing = 0
        for ($i = 0; $i -lt [Math]::Min($a.Count, $b.Count); $i++) {
            if ($a[$i] -ne $b[$i]) { $differing++ }
        }
        Assert-True ($differing -eq 1) `
            ("the derived INF differs from src\xhci98.inf on $differing line(s); the experiment needs exactly one.")
    } else {
        Assert-True $false "no xhci98.inf was staged for the unpadded-date experiment."
    }

    # --- the two special modes are not composable ---------------------------
    #
    # Review finding 4. Each mode varies one thing against the
    # ordinary package; a package varying both answers neither question, and the
    # combination used to be accepted silently - both banners printed and the
    # output landed in the artifact's directory, so the run that produced it
    # looked like a successful task 12.3 build.
    #
    Write-Step "-FailStartArtifact and -UnpaddedDriverVerExperiment exclude each other"
    $bothOut = Join-Path $script:work "pkg-both-modes"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $failStartDriver,
        "-OutDir", $bothOut,
        "-FailStartArtifact", "-UnpaddedDriverVerExperiment")
    Assert-True ($r.ExitCode -ne 0) `
        "the packager accepted both special modes at once."
    Assert-True ($r.Output -match "mutually exclusive") `
        ("expected the refusal to say why. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $bothOut)) `
        "the output directory was created for a refused mode combination."

    # --- a failure after staging leaves the previous package alone ----------
    #
    # Review finding 1. The packager used to copy straight into
    # OutDir, so a run failing at the final gate left a directory holding a
    # mixture of the new files and the old - which passes HANDOFF's marker and
    # DriverVer preflight, and is what a VM would then be recovered from.
    #
    Write-Step "a failed run does not disturb an existing package"
    $keepOut = Join-Path $script:work "pkg-keep"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", $keepOut)
    Assert-True ($r.ExitCode -eq 0) ("the baseline package was rejected:`n" + $r.Output)
    $before = @{}
    foreach ($f in (Get-ChildItem -LiteralPath $keepOut -File -Recurse)) {
        $before[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
    }
    Assert-True ($before.Count -gt 0) "the baseline package staged no files."
    # Re-run onto the same directory with a driver the gate refuses. The refusal
    # happens before staging, but the assertion that matters is the same one a
    # later failure has to satisfy: what is already there is untouched.
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $probeDriver,
        "-OutDir", $keepOut)
    Assert-True ($r.ExitCode -ne 0) "a probe build was packaged over an existing package."
    $after = @{}
    foreach ($f in (Get-ChildItem -LiteralPath $keepOut -File -Recurse)) {
        $after[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
    }
    Assert-True ($after.Count -eq $before.Count) `
        "the failed run changed how many files the existing package holds."
    $changed = @($before.Keys | Where-Object { $after[$_] -ne $before[$_] })
    Assert-True ($changed.Count -eq 0) `
        ("the failed run modified " + $changed.Count + " file(s) of the existing package.")
    # And no staging directory is left beside it.
    $leftovers = @(Get-ChildItem -LiteralPath $script:work -Directory |
        Where-Object { $_.Name -like "pkg-keep.staging-*" })
    Assert-True ($leftovers.Count -eq 0) `
        "a staging directory was left behind by a failed run."

    # --- the destination is replaced, so a foreign one is refused -----------
    #
    # The staging swap above replaces OutDir wholesale. The packager it replaced
    # copied *into* the destination, and the script's own -OutDir example names
    # a transfer volume - so a destination holding a caller's other files must
    # be refused, never deleted. An empty directory is still fine.
    #
    Write-Step "a destination this script did not make is refused, not deleted"
    $foreignOut = Join-Path $script:work "pkg-foreign"
    Ensure-Directory $foreignOut
    $bystander = Join-Path $foreignOut "notes.txt"
    Set-Content -LiteralPath $bystander -Encoding ASCII -Value "someone else's file"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", $foreignOut)
    Assert-True ($r.ExitCode -ne 0) `
        "the packager replaced a destination holding files it did not make."
    Assert-True (Test-Path -LiteralPath $bystander) `
        "the packager deleted a bystander file in the destination."
    Assert-True ((Get-Content -LiteralPath $bystander -Raw) -match "someone else") `
        "the bystander file survived by name but not by content."

    # A published flavour directory holds only files the staged set also
    # holds, so the foreign-file check would let the packager replace it with
    # the full package, Microsoft binaries included, inside a tree git tracks.
    # The refusal is by path, before anything is staged.
    Write-Step "a destination under releases\ is refused"
    $releaseOut = Join-Path $repo "releases\9.9.9.9\release"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", $releaseOut)
    Assert-True ($r.ExitCode -ne 0) "the packager accepted an -OutDir under releases\."
    Assert-True ($r.Output -match "releases") ("the refusal did not name releases\:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $repo "releases\9.9.9.9"))) `
        "the packager created a directory under releases\ before refusing."
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", (Join-Path $repo "releases"))
    Assert-True ($r.ExitCode -ne 0) "the packager accepted releases\ itself as -OutDir."

    # An empty directory is not foreign - it is the ordinary first run.
    $emptyOut = Join-Path $script:work "pkg-empty-dest"
    Ensure-Directory $emptyOut
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", $emptyOut)
    Assert-True ($r.ExitCode -eq 0) `
        ("an existing empty destination was refused:`n" + $r.Output)
    Assert-True (Test-Path -LiteralPath (Join-Path $emptyOut "xhci98.inf")) `
        "nothing was staged into an existing empty destination."

    # --- a failure AFTER staging leaves the old package byte-for-byte -------
    #
    # Round 2 finding 5: the refusal above happens before anything is staged, so
    # it cannot tell a transactional packager from one that writes straight into
    # the destination. The ownership check can, because it runs *after* the
    # whole package is staged and gated - it compares the destination against
    # what was staged. So: a complete existing package, a second run carrying a
    # visibly different driver, and one foreign file to make that run fail at
    # the last possible moment. A packager that wrote directly into OutDir would
    # already have replaced the driver by then, and this fails.
    #
    Write-Step "a package survives a failure that happens after staging"
    $txOut = Join-Path $script:work "pkg-transactional"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
        "-OutDir", $txOut)
    Assert-True ($r.ExitCode -eq 0) ("the baseline package was rejected:`n" + $r.Output)
    $txBefore = @{}
    foreach ($f in (Get-ChildItem -LiteralPath $txOut -File -Recurse)) {
        $txBefore[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
    }
    $otherDriver = Join-Path $srcDir "xhci98-other.sys"
    Set-Content -LiteralPath $otherDriver -Encoding ASCII -Value `
        ("a different stand-in driver, visibly not the one already packaged " +
         "XHCI98_FLAVOUR_DEBUG")
    Assert-True ((Get-FileHash -LiteralPath $otherDriver).Hash -ne
                 (Get-FileHash -LiteralPath $driver).Hash) `
        "the two stand-in drivers are identical, so this vector proves nothing."
    # The foreign file is what turns the second run into a post-staging failure.
    $intruder = Join-Path $txOut "README.TXT"
    Set-Content -LiteralPath $intruder -Encoding ASCII -Value "notes from the operator"
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $otherDriver,
        "-OutDir", $txOut)
    Assert-True ($r.ExitCode -ne 0) `
        "a destination holding an unaccounted-for file was replaced anyway."
    Assert-True ($r.Output -match "README") `
        ("expected the refusal to name the file it could not account for. Output:`n" + $r.Output)
    foreach ($k in $txBefore.Keys) {
        Assert-True (Test-Path -LiteralPath $k) `
            ("the failed run removed '$k' from the existing package.")
        Assert-True ((Get-FileHash -LiteralPath $k).Hash -eq $txBefore[$k]) `
            ("the failed run rewrote '$k' - the destination was written before the run failed.")
    }
    Assert-True ((Get-Content -LiteralPath $intruder -Raw) -match "operator") `
        "the failed run damaged the file it refused to account for."
    $txLeft = @(Get-ChildItem -LiteralPath $script:work -Directory |
        Where-Object { $_.Name -like "pkg-transactional.*" })
    Assert-True ($txLeft.Count -eq 0) `
        "a staging or retired directory was left beside the destination."

    # --- a trailing separator on -OutDir is not a nested staging directory ---
    #
    # Round 2 finding 1. The staging directory is named by appending to the
    # destination's path, so an unnormalised trailing "\" made it a *child* of
    # the destination - and the swap then destroyed its own source. Both runs
    # here matter: the first creates the package, the second replaces it, and it
    # is the replacement that used to lose everything.
    #
    Write-Step "a trailing separator on -OutDir does not nest the staging directory"
    $slashOut = Join-Path $script:work "pkg-trailing"
    foreach ($pass in @("first", "second")) {
        $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver,
            "-OutDir", ($slashOut + "\"))
        Assert-True ($r.ExitCode -eq 0) `
            ("the $pass run with a trailing separator failed:`n" + $r.Output)
        foreach ($f in @("xhci98.inf", "xhci98.sys")) {
            Assert-True (Test-Path -LiteralPath (Join-Path $slashOut $f)) `
                "'$f' is missing after the $pass trailing-separator run."
        }
    }
    $nested = @(Get-ChildItem -LiteralPath $slashOut -Directory -Force)
    Assert-True ($nested.Count -eq 0) `
        ("the staging directory was created inside the destination: " +
         (($nested | ForEach-Object { $_.Name }) -join ", "))

    # --- the binary gates are on unless a caller opts out -------------------
    #
    # Roadmap Phase 4 task 1 requires the host suite and the import gate to run
    # before every VM deploy, and this is the deploy step. Everything else here
    # passes -SkipBinaryGates, so without this case the switch could default
    # the wrong way and no test would notice. The stand-in driver is a text
    # file, so whichever gate runs first refuses it - which one is not the
    # point; that the run is gated at all is.
    Write-Step "the binary gates run unless -SkipBinaryGates is passed"
    $gatedOut = Join-Path $script:work "pkg-gated"
    $r = Invoke-Packager -WithBinaryGates -Arguments @("-InfPath", $plainInf,
        "-DriverPath", $driver, "-OutDir", $gatedOut,
        "-NoTargetEvidence")
    Assert-True ($r.ExitCode -ne 0) `
        "a stand-in driver was packaged with the binary gates enabled."
    Assert-True ($r.Output -match "host test suite|import-compatibility gate") `
        ("expected the refusal to name the gate that ran. Output:`n" + $r.Output)
    # **"Gated" and "inconclusive" are different readings** (audit H18). Smart
    # App Control blocking a freshly linked unsigned exe makes the host suite
    # produce no result line, and the packager refuses on that too - with a
    # message that says to run it again. That refusal would satisfy the two
    # assertions above while proving nothing about whether the gates ran, so
    # the one message that must NOT be what fired is named here.
    Assert-True ($r.Output -notmatch "produced no result line") `
        ("the host suite did not run at all (Smart App Control), so this case " +
         "proved nothing about whether the gates are enabled by default. " +
         "Run it again. Output:`n" + $r.Output)
    Assert-True (-not (Test-Path -LiteralPath $gatedOut)) `
        "the output directory was created for a driver that failed a binary gate."

    # --- the output path refusals (audit H18) -------------------------------
    #
    # This script replaces its output directory wholesale, so where it is
    # pointed is a data-loss question rather than a tidiness one. The packager
    # refuses a volume root, the repository root, a path naming a file, and
    # anything under releases\ - and none of those four refusals had a test.
    # The volume-root one in particular is reached from two places in
    # make-package.ps1 (an early check added because the later one was
    # unreachable for `E:\`), and neither was ever produced.
    Write-Step "the output path refusals"

    $rootOut = [System.IO.Path]::GetPathRoot($script:work)
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $rootOut)
    Assert-True ($r.ExitCode -ne 0) "the packager accepted a volume root as -OutDir."
    Assert-True ($r.Output -match "volume or repository root") `
        ("expected the volume-root refusal. Output:`n" + $r.Output)

    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $repo)
    Assert-True ($r.ExitCode -ne 0) "the packager accepted the repository root as -OutDir."
    Assert-True ($r.Output -match "volume or repository root") `
        ("expected the repository-root refusal. Output:`n" + $r.Output)

    # A path that exists and is a FILE. The refusal has to name that, rather
    # than failing later in a directory operation with no explanation.
    $fileOut = Join-Path $script:work "not-a-directory"
    Set-Content -LiteralPath $fileOut -Value "occupied" -Encoding ASCII
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $fileOut)
    Assert-True ($r.ExitCode -ne 0) "the packager accepted a file as -OutDir."
    Assert-True ($r.Output -match "is a file, not a directory") `
        ("expected the not-a-directory refusal. Output:`n" + $r.Output)
    Assert-True ((Get-Content -LiteralPath $fileOut -Raw).Trim() -eq "occupied") `
        "the packager overwrote the file it was pointed at."

    # --- a foreign file in a SUBDIRECTORY (audit H18) -----------------------
    #
    # The foreign-file check is recursive, and both existing cases put their
    # bystander at the root - so the recursion was untested and a check written
    # against the top level only would have passed them both. A transfer
    # directory with a subdirectory of notes in it is the realistic shape.
    Write-Step "a foreign file below the output root is found too"
    $deepOut = Join-Path $script:work "pkg-foreign-deep"
    Ensure-Directory (Join-Path $deepOut "notes")
    $deepBystander = Join-Path $deepOut "notes\keep-me.txt"
    Set-Content -LiteralPath $deepBystander -Value "not ours" -Encoding ASCII
    $r = Invoke-Packager @("-InfPath", $plainInf, "-DriverPath", $driver, "-OutDir", $deepOut)
    Assert-True ($r.ExitCode -ne 0) `
        "the packager replaced a directory holding a foreign file one level down."
    Assert-True ($r.Output -match "did not stage") `
        ("expected the foreign-file refusal. Output:`n" + $r.Output)
    Assert-True (Test-Path -LiteralPath $deepBystander) `
        "the foreign file below the output root was deleted."

    # --- the missing-version-resource refusal (audit H18) -------------------
    #
    # NOT TESTED, and recorded here rather than left as a silent gap. That
    # refusal fires only with the binary gates ON, and with them on the host
    # test suite and the import gate run FIRST and refuse this harness's
    # stand-in driver - which is a text file - before the version resource is
    # ever looked at. Producing it needs a real linked driver built without
    # src\xhci98.rc, which is a build this repository has not been able to make
    # since task 8-A.4 put the .rc in src\sources. The negative control for the
    # rule is what exists instead: the -SkipBinaryGates warning path is driven
    # by every other case in this file, and the version COMPARISON is
    # deliberately not gated on the switch, so a stand-in that does carry a
    # version is still checked against the INF.

    # --- the two degenerate unpadded-date inputs (audit H18) ----------------
    #
    # make-package.ps1 refuses both, and neither refusal had a test: a source
    # whose date is already unpadded (the derived package would be
    # byte-identical, so it is not an experiment) and a source with more than
    # one DriverVer to rewrite (which of them varies is then unstated).
    Write-Step "the unpadded-date experiment refuses its two degenerate inputs"
    $alreadyUnpadded = New-Inf -Name "already-unpadded" -Mutate {
        param($t) $t -replace '(?m)^DriverVer=\d{1,2}/\d{1,2}/(\d{4}),', 'DriverVer=1/2/$1,'
    }
    $r = Invoke-Packager @("-InfPath", $alreadyUnpadded, "-DriverPath", $driver,
        "-OutDir", (Join-Path $script:work "pkg-already-unpadded"),
        "-UnpaddedDriverVerExperiment")
    Assert-True ($r.ExitCode -ne 0) `
        "the experiment was accepted on a source whose date is already unpadded."
    Assert-True ($r.Output -match "already unpadded") `
        ("expected the already-unpadded refusal. Output:`n" + $r.Output)

    $twoDriverVers = New-Inf -Name "two-driverver" -Mutate {
        param($t) $t -replace '(?m)^(DriverVer=\d{1,2}/\d{1,2}/\d{4},.*)$', "`$1`r`n`$1"
    }
    $r = Invoke-Packager @("-InfPath", $twoDriverVers, "-DriverPath", $driver,
        "-OutDir", (Join-Path $script:work "pkg-two-driverver"),
        "-UnpaddedDriverVerExperiment")
    Assert-True ($r.ExitCode -ne 0) `
        "the experiment was accepted on a source carrying two DriverVer lines."

    # --- a [SourceDisksFiles] subdirectory must be honoured -----------------
    #
    # "usbfiles" is exactly 8 characters, so the gate's 8.3 path rule does not
    # fire first and this case tests what it is meant to test.
    Write-Step "a [SourceDisksFiles] subdirectory is staged, not flattened"
    $subInf = New-Inf -Name "subdir" -Mutate { param($t) $t.Replace("xhci98.sys=1", "xhci98.sys=1,usbfiles") }
    $subOut = Join-Path $script:work "pkg-subdir"
    $r = Invoke-Packager @("-InfPath", $subInf, "-DriverPath", $driver, "-OutDir", $subOut)
    Assert-True ($r.ExitCode -eq 0) ("a package with a [SourceDisksFiles] subdirectory was rejected:`n" + $r.Output)
    Assert-True (Test-Path -LiteralPath (Join-Path $subOut "usbfiles\xhci98.sys")) `
        "xhci98.sys was not staged into the 'usbfiles' subdirectory [SourceDisksFiles] names."
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $subOut "xhci98.sys"))) `
        "xhci98.sys was also staged flat; a stray root copy is what let a substituted subdirectory file pass unnoticed."
    # And the staged package must satisfy the gate that reads the same INF.
    $gate = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) "inf-gate") "check-inf.ps1"
    # Same relaxation as Invoke-Packager above, and for the same reason: under
    # EAP=Stop a stderr line from the child aborts this self-test before the
    # exit-code assertion below, with an empty message in place of $out.
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $gate,
            "-InfPath", $subInf, "-PackageDir", $subOut) 2>&1 | Out-String
    } finally {
        $ErrorActionPreference = $savedEap
    }
    Assert-True ($LASTEXITCODE -eq 0) ("the subdirectory package failed the gate that derived its layout:`n" + $out)

    # --- a media file with no source is an error, not a silent gap ----------
    Write-Step "an unsourced [SourceDisksFiles] entry is refused"
    $extraInf = New-Inf -Name "extra" -Mutate { param($t) $t.Replace("xhci98.inf=1", "xhci98.inf=1`r`nextra.sys=1") }
    $r = Invoke-Packager @("-InfPath", $extraInf, "-DriverPath", $driver, "-OutDir", (Join-Path $script:work "pkg-extra"))
    Assert-True ($r.ExitCode -ne 0) "a [SourceDisksFiles] entry with no source was accepted."
    Assert-True ($r.Output -match "extra\.sys") ("expected the error to name the unsourced file. Output:`n" + $r.Output)

    # --- a broken INF stops the run before anything is staged ---------------
    Write-Step "a rejected INF stops the run before staging"
    $badInf = New-Inf -Name "bad" -Mutate { param($t) $t.Replace('Signature="$CHICAGO$"', 'Signature="$Windows NT$"') }
    $badOut = Join-Path $script:work "pkg-bad"
    $r = Invoke-Packager @("-InfPath", $badInf, "-DriverPath", $driver, "-OutDir", $badOut)
    Assert-True ($r.ExitCode -ne 0) "a package was built around an INF the gate rejects."
    Assert-True (-not (Test-Path -LiteralPath $badOut)) `
        "the output directory was created for an INF that failed the gate; nothing should be staged around a rejected INF."

    # --- a relative -OutDir means the caller's directory --------------------
    #
    # The child powershell.exe inherits this process's directory, so running it
    # with a *different* Set-Location is what distinguishes $PWD from
    # [Path]::GetFullPath's process directory. Without the fix the package
    # lands under the process directory instead.
    Write-Step "a relative -OutDir resolves against the caller's location"
    $cwd = Join-Path $script:work "cwd"
    New-Item -ItemType Directory -Path $cwd | Out-Null
    $script = @(
        "Set-Location -LiteralPath '$cwd'",
        "& '$packager' -InfPath '$plainInf' -DriverPath '$driver' -OutDir 'relpkg' -SkipBinaryGates | Out-Null",
        "exit `$LASTEXITCODE"
    ) -join "; "
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", $script) 2>&1 | Out-Null
    } finally {
        $ErrorActionPreference = $savedEap
    }
    Assert-True (Test-Path -LiteralPath (Join-Path $cwd "relpkg\xhci98.inf")) `
        "a relative -OutDir did not resolve against the caller's location; [Path]::GetFullPath uses the process directory, which Set-Location does not update."


    # --- the upload set is what actually gets downloaded --------------------
    #
    # `releases\<version>\` is the tracked half; the GitHub asset is the *other*
    # output, and it is the one a user installs from. It was assembled with no
    # self-test at all until that review, and the first cut anyone would have
    # shipped was malformed in a way every existing check passed: the published
    # tree was copied one level too deep, so `upload-<v>\<v>\release\` held this
    # project's two files and `upload-<v>\release\` held Microsoft's two, and no
    # directory in the download held all four. The SHA-256 gate did not see it -
    # it authenticates every manifest row at the paths the assembly just wrote
    # them to, which is a question about content and not about layout.
    #
    # So the assertions here are about *shape*: what is in each flavour
    # directory, that the published tree is not nested inside itself, and that
    # the archive's entry names are ones a non-Windows unzip can read.
    #
    # -UploadSetOnly is what makes this drivable at all - it assembles from a
    # tree already on disk instead of building anything - and it is the same
    # code path the tail of a real cut uses.
    #
    Write-Step "the upload set is complete install media in every flavour directory"

    $infText = [System.IO.File]::ReadAllText($prodInf)
    if ($infText -notmatch '(?m)^DriverVer\s*=\s*[^,\r\n]+,\s*([0-9.]+)\s*$') {
        Assert-True $false "src\xhci98.inf has no readable DriverVer version; the upload-set cases cannot name a release directory."
    } else {
        $relVersion = $Matches[1].Trim()

        # A stand-in published release: the two files this project may track, a
        # readme and a LICENSE beside them, and the qualifier subdirectory - the
        # shape make-release.ps1 publishes, with nothing in it that needs a
        # build.
        $relRoot = Join-Path $script:work "releases"
        $pubRoot = Join-Path $relRoot $relVersion
        $pkgRoot = Join-Path $script:work "pkgroot"
        $upRoot = Join-Path $script:work "upload"
        Ensure-Directory $upRoot

        $flavourBytes = @{}
        foreach ($fl in @("release", "debug")) {
            # Distinct per flavour, so a run that crossed the two would fail the
            # published-vs-package identity check rather than pass unnoticed.
            $flavourBytes[$fl] = [System.Text.Encoding]::ASCII.GetBytes(
                "stand-in xhci98.sys, $fl flavour, $relVersion")

            $pubFlavour = Join-Path $pubRoot $fl
            Ensure-Directory $pubFlavour
            [System.IO.File]::WriteAllBytes((Join-Path $pubFlavour "xhci98.sys"), $flavourBytes[$fl])
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $pubFlavour "xhci98.inf") -Force

            # The gated package: the same two files byte for byte, and since
            # 1.0.0.1 nothing else.
            $pkgFlavour = Join-Path $pkgRoot ("pkg-" + $fl)
            Ensure-Directory $pkgFlavour
            [System.IO.File]::WriteAllBytes((Join-Path $pkgFlavour "xhci98.sys"), $flavourBytes[$fl])
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $pkgFlavour "xhci98.inf") -Force
        }
        Set-Content -LiteralPath (Join-Path $pubRoot "readme.txt") -Encoding ASCII `
                    -Value "stand-in per-version readme"
        Copy-Item -LiteralPath (Join-Path $repo "LICENSE") `
                  -Destination (Join-Path $pubRoot "LICENSE") -Force
        Ensure-Directory (Join-Path $pubRoot "xhciqual")
        Set-Content -LiteralPath (Join-Path $pubRoot "xhciqual\XHCIQUAL.EXE") `
                    -Encoding ASCII -Value "stand-in qualifier"

        # What the published tree looks like before the run, so that a mode
        # whose whole promise is "writes nothing under releases\" is held to it.
        $pubBefore = @{}
        foreach ($f in (Get-ChildItem -LiteralPath $pubRoot -File -Recurse)) {
            $pubBefore[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
        }

        $relArgs = @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                     "-PackageRoot", $pkgRoot, "-UploadDir", $upRoot)
        $r = Invoke-Releaser $relArgs
        Assert-True ($r.ExitCode -eq 0) ("the upload set was not assembled:`n" + $r.Output)

        $uploadDir = Join-Path $upRoot ("upload-" + $relVersion)
        # The asset is named for the project rather than for the workspace it is
        # assembled in - see New-UploadSet. Asserting the name here is what stops
        # the two drifting back together silently.
        $uploadZip = Join-Path $upRoot ("xhci98-" + $relVersion + ".zip")

        # The files the INF names, in one directory, per flavour. This is the
        # assertion the malformed asset would have failed.
        foreach ($fl in @("release", "debug")) {
            foreach ($name in @("xhci98.sys", "xhci98.inf")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\$name")) `
                    "'$name' is missing from the upload set's $fl\ directory; a user pointing Windows at it gets an incomplete install."
            }
        }
        # And the published tree's own files are at the top level, not one
        # directory further down. A nested <version>\ directory here is the
        # exact defect: every file present, and none of them together.
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $uploadDir $relVersion))) `
            "the upload set nests the published tree as '$relVersion\'; the release root's contents belong at the top level."
        foreach ($name in @("readme.txt", "LICENSE", "xhciqual\XHCIQUAL.EXE")) {
            Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir $name)) `
                "'$name' is missing from the upload set root; the published tree was not copied whole."
        }

        # The archive a non-Windows machine has to be able to unpack.
        Assert-True (Test-Path -LiteralPath $uploadZip) "no upload archive was written."
        if (Test-Path -LiteralPath $uploadZip) {
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $zip = [System.IO.Compression.ZipFile]::OpenRead($uploadZip)
            try {
                $entries = @($zip.Entries | ForEach-Object { $_.FullName })
            } finally {
                $zip.Dispose()
            }
            $backslashed = @($entries | Where-Object { $_.Contains("\") })
            Assert-True ($backslashed.Count -eq 0) `
                ("the archive names " + $backslashed.Count + " entry/entries with backslashes, which unzip on a Linux or macOS host extracts as flat files: " +
                 (($backslashed | Select-Object -First 3) -join ", "))
            foreach ($want in @("release/xhci98.sys", "debug/xhci98.inf")) {
                Assert-True ($entries -contains $want) "the archive has no '$want' entry."
            }
        }

        # Nothing under releases\ was written to. That is the promise that makes
        # this mode a safe way to repair the asset of a published version.
        $pubAfter = @{}
        foreach ($f in (Get-ChildItem -LiteralPath $pubRoot -File -Recurse)) {
            $pubAfter[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
        }
        Assert-True ($pubAfter.Count -eq $pubBefore.Count) `
            "-UploadSetOnly changed how many files the published release holds."
        $touched = @($pubBefore.Keys | Where-Object { $pubAfter[$_] -ne $pubBefore[$_] })
        Assert-True ($touched.Count -eq 0) `
            ("-UploadSetOnly rewrote " + $touched.Count + " file(s) of the published release, which it must never write to.")

        # --- assembled from the tracked directory alone ---------------------
        #
        # roadmap Phase 20, F15. This mode used to require the gated
        # out\pkg-<flavour>\ directories to exist and to hash-match the
        # published binaries, a dependency left over from when the media
        # carried Microsoft files the package supplied. Since 1.0.0.1 nothing
        # from the package enters the asset, so a fresh clone with no out\ at
        # all - the machine a lost asset is most likely to be rebuilt on - has
        # to be able to rebuild it, and the run must not create the package
        # root it was told does not exist.
        Write-Step "the upload set is rebuilt from a clone with no out\ at all"
        $noOut = Join-Path $script:work "no-such-out"
        Remove-Item -LiteralPath $uploadDir -Recurse -Force
        Remove-Item -LiteralPath $uploadZip -Force
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -eq 0) ("the upload set was not assembled without a package root:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            foreach ($name in @("xhci98.sys", "xhci98.inf")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\$name")) `
                    "'$name' is missing from the upload set's $fl\ directory when assembled without a package root."
            }
        }
        Assert-True (Test-Path -LiteralPath $uploadZip) "no upload archive was written when assembled without a package root."
        Assert-True (-not (Test-Path -LiteralPath $noOut)) "the run created the package root it was told did not exist."

        # --- and only the current cut's asset -------------------------------
        #
        # The INF gate this mode runs encodes the current release's rules, and
        # an older cut fails the rules added since (the 1.0.0.1 INF fails six,
        # measured read-only on 2026-09-05). An older -Version is refused with
        # that reason before anything is assembled, rather than failing the
        # gate with a message about the media.
        Write-Step "-UploadSetOnly refuses a version other than the current cut, and says why"
        $olderVersion = "0.9.9.9"
        $olderRoot = Join-Path $relRoot $olderVersion
        foreach ($fl in @("release", "debug")) {
            Ensure-Directory (Join-Path $olderRoot $fl)
            Copy-Item -LiteralPath (Join-Path $pubRoot "$fl\xhci98.sys") -Destination (Join-Path $olderRoot "$fl\xhci98.sys") -Force
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $olderRoot "$fl\xhci98.inf") -Force
        }
        Set-Content -LiteralPath (Join-Path $olderRoot "readme.txt") -Encoding ASCII -Value "stand-in older cut"
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $olderVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "-UploadSetOnly assembled an asset for a version that is not the current cut."
        Assert-True ($r.Output -match "current cut only") `
            ("expected the refusal to say this mode rebuilds the current cut only. Output:`n" + $r.Output)
        Assert-True ($r.Output -match "check out the commit that cut it") `
            ("expected the refusal to name the way to rebuild an older cut. Output:`n" + $r.Output)
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $upRoot ("upload-" + $olderVersion)))) `
            "the refused older-version run assembled an upload directory anyway."

        # --- the switches that contradict -UploadSetOnly --------------------
        Write-Step "-UploadSetOnly refuses the switches that contradict it"
        foreach ($bad in @(
            @{ Switch = "-Force";          Says = "do not go together" },
            @{ Switch = "-SkipUploadSet";  Says = "opposite things" }
        )) {
            $r = Invoke-Releaser ($relArgs + @($bad.Switch))
            Assert-True ($r.ExitCode -ne 0) "-UploadSetOnly was accepted together with $($bad.Switch)."
            # The diagnostic, not just the exit code - see finding 6 above.
            Assert-True ($r.Output -match [regex]::Escape($bad.Says)) `
                ("expected $($bad.Switch) to be refused on its own terms. Output:`n" + $r.Output)
        }

        # --- a flavour the run was not asked to complete ---------------------
        #
        # Review finding 1. The published tree is copied whole, so
        # narrowing -Flavor completes some of its flavour directories and
        # leaves the rest holding this project's two files and neither of
        # Microsoft's - which is the incomplete-media defect again, reached by
        # a different route, and it used to exit 0.
        #
        Write-Step "-Flavor may not leave a published flavour directory incomplete"
        $r = Invoke-Releaser ($relArgs + @("-Flavor", "release"))
        Assert-True ($r.ExitCode -ne 0) `
            "-Flavor release completed only one of two published flavour directories and said nothing."
        Assert-True ($r.Output -match "was not asked to complete") `
            ("expected the refusal to name the flavour left behind. Output:`n" + $r.Output)

        # --- a relative -PackageRoot --------------------------------------
        #
        # Review finding 3. The relative path of each staged file
        # used to be derived by cutting the package directory's length off an
        # absolute FullName, so a relative root cut mid-path - and every check
        # downstream re-derived it the same wrong way and agreed. The run
        # exited 0 with files at paths like
        # release\Data\Local\Temp\...\usbd98.sys. -UploadSetOnly no longer
        # reads the package at all (F15, above), so what this case still holds
        # is narrower: a relative -PackageRoot is resolved against the caller's
        # location by Resolve-DirectoryArgument, as every directory argument is,
        # and the asset it produces is the same complete one as the absolute
        # form.
        #
        Write-Step "a relative -PackageRoot resolves against the caller's location"
        $relPkgParent = Split-Path -Parent $pkgRoot
        $relPkgLeaf = Split-Path -Leaf $pkgRoot
        $relScript = @(
            "Set-Location -LiteralPath '$relPkgParent'",
            ("& '$releaser' -UploadSetOnly -Version '$relVersion' -ReleasesDir '$relRoot' " +
             "-PackageRoot '$relPkgLeaf' -UploadDir '$upRoot' | Out-Null"),
            "exit `$LASTEXITCODE"
        ) -join "; "
        $savedEap2 = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & powershell.exe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", $relScript) 2>&1 | Out-Null
            $relExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $savedEap2
        }
        Assert-True ($relExit -eq 0) "a relative -PackageRoot was not accepted."
        foreach ($fl in @("release", "debug")) {
            foreach ($name in @("xhci98.sys", "xhci98.inf")) {
                Assert-True (Test-Path -LiteralPath (Join-Path $uploadDir "$fl\$name")) `
                    "'$name' is missing from $fl\ after a relative -PackageRoot run; the relative path derivation cut in the wrong place."
            }
        }
        $strays = @(Get-ChildItem -LiteralPath $uploadDir -Directory -Recurse |
            Where-Object { $_.Name -notin @("release", "debug", "xhciqual") })
        Assert-True ($strays.Count -eq 0) `
            ("a relative -PackageRoot created " + $strays.Count + " directory(ies) nobody named: " +
             (($strays | ForEach-Object { $_.Name } | Select-Object -First 3) -join ", "))

        # --- the upload set may not be assembled inside the release ---------
        #
        # Review finding 5. Assembling clears its own destination
        # first, so an -UploadDir at or below the version directory would
        # delete inside a written-once release and then copy that release into
        # its own descendant.
        #
        # --- a file the INF does not name may not be published --------------
        #
        # Review round 2, finding 1. The copy used to take everything
        # in the package that was not one of this project's own two files, and
        # check-inf.ps1 -PackageDir does not object to a file nobody declared -
        # it checks the declared ones are present. So anything left in
        # out\pkg-<flavour>\ went up in the release asset.
        #
        # **No longer drivable from here.** The two cases that held this rule
        # ran -UploadSetOnly against a package with a stray file and against one
        # missing a declared file; since the Phase 20 fix pass that mode does
        # not read the package (F15), so both would exit 0 for the wrong reason.
        # The rule itself stands in Assert-PackageMatchesDeclaredMedia, which the
        # ordinary cut still applies to each package before the publish swap,
        # and that path needs a build this suite does not have - the same limit
        # the structural check below states for the media-root refusal. What
        # replaced the coverage is narrower and is asserted in the F15 case
        # above: the asset holds exactly the published tree, and nothing under
        # the package root is read or created.
        #
        # -UploadSetOnly does still refuse an INF that names a media file beyond
        # this project's two, since the tracked directory has no source for it.
        # The mutation adds a third [SourceDisksFiles] entry; the current gate
        # would refuse the file's presence on the media anyway, so the refusal
        # this case pins is the one that fires with no package at all.
        Write-Step "-UploadSetOnly refuses an INF naming a media file the tracked directory cannot supply"
        $thirdInf = New-Inf -Name "third" -Mutate {
            param($t) $t.Replace("xhci98.inf=1", "xhci98.inf=1`r`nextra.bin=1")
        }
        foreach ($fl in @("release", "debug")) {
            Copy-Item -LiteralPath $thirdInf -Destination (Join-Path $pubRoot "$fl\xhci98.inf") -Force
        }
        $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion, "-ReleasesDir", $relRoot,
                               "-PackageRoot", $noOut, "-UploadDir", $upRoot)
        Assert-True ($r.ExitCode -ne 0) "an INF naming a third media file assembled an upload set from the tracked directory alone."
        Assert-True ($r.Output -match "extra\.bin") `
            ("expected the refusal to name the file the tracked directory cannot supply. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $pubRoot "$fl\xhci98.inf") -Force
        }

        # --- what is NOT covered here, and why -------------------------------
        #
        # Review round 6 found that the containment guard compares
        # strings, so a drive alias gives the published release a second
        # spelling sharing no prefix with the first.
        # Resolve-DirectoryArgument in make-release.ps1 expands a PSDrive's
        # DisplayRoot, which closes the *mapped network drive* form and - as
        # measured, and as that function now says - does **not** close `subst`,
        # which is indistinguishable from a local disk without a P/Invoke.
        #
        # **There is deliberately no case for either here.** Driving one means
        # creating a real drive alias inside the suite, and the attempt hung
        # this harness rather than failing: a self-test that can hang is worse
        # than the coverage it buys, because it stops every case after it from
        # running at all. The limit is written down in both files rather than
        # left as an untested branch nobody remembers is untested.
        #
        # --- a publishable file the INF declares in a subdirectory ----------
        # --- a publishable file the INF declares in a subdirectory ----------
        #
        # Review round 3. An INF saying `xhci98.inf=1,setup` is
        # valid and make-package.ps1 stages it there, but this script publishes
        # its own two files at the root of a flavour directory - three places
        # assume it. The assumption is now refused with its reason rather than
        # surfacing as "make-package.ps1 produced no 'xhci98.inf'", which is
        # true and says nothing about why.
        #
        Write-Step "a publishable file declared in a subdirectory is refused with its reason"
        $subPubInf = New-Inf -Name "subpub" -Mutate {
            param($t) $t.Replace("xhci98.inf=1", "xhci98.inf=1,setup")
        }
        foreach ($fl in @("release", "debug")) {
            Copy-Item -LiteralPath $subPubInf -Destination (Join-Path $pubRoot "$fl\xhci98.inf") -Force
            Copy-Item -LiteralPath $subPubInf -Destination (Join-Path $pkgRoot "pkg-$fl\xhci98.inf") -Force
        }
        $r = Invoke-Releaser $relArgs
        Assert-True ($r.ExitCode -ne 0) `
            "an INF declaring xhci98.inf in a subdirectory assembled an upload set."
        Assert-True ($r.Output -match "carries those two at its root") `
            ("expected the refusal to explain the release layout. Output:`n" + $r.Output)
        foreach ($fl in @("release", "debug")) {
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $pubRoot "$fl\xhci98.inf") -Force
            Copy-Item -LiteralPath $plainInf -Destination (Join-Path $pkgRoot "pkg-$fl\xhci98.inf") -Force
        }

        # --- the upload set may not be assembled inside the release ---------
        #
        # Review finding 5, tightened by round 2 finding 3: the
        # first version of this case asserted only a nonzero exit, which
        # copying a directory into its own descendant produces on its own - so
        # the guard could have been deleted and the case stayed green. Both
        # containment directions are driven, and both assert the diagnostic.
        #
        Write-Step "-UploadDir inside or around the published release, or anywhere under releases\, is refused"
        $containment = @(
            # The upload set would land inside the version directory.
            @{ Why = "inside"; UploadDir = $pubRoot },
            # ...and the reverse: an upload root that would *contain* the
            # published tree, which is the branch nothing exercised.
            @{ Why = "around"; UploadDir = (Split-Path -Parent $relRoot);
               Releases = (Join-Path (Join-Path $script:work "wrap") ("upload-" + $relVersion + "\releases")) },
            # roadmap Phase 20, F4: the guard compared with the version being
            # cut alone, so an -UploadDir under an OLDER cut was accepted and
            # would have written `upload-<v>\` and the zip into a written-once
            # directory .gitignore does not cover. The older cut staged for the
            # F15 case above is the destination here.
            @{ Why = "inside an older cut"; UploadDir = $olderRoot },
            # ...and the releases root itself, whose `upload-<v>\` and zip
            # would be siblings of every cut.
            @{ Why = "at the releases root"; UploadDir = $relRoot },
            # The repository's OWN releases\ while -ReleasesDir points at the
            # stand-in tree: the override moves the cut, not the protection
            # (Phase 20 review, finding 5). The destination is a probe name
            # under the canonical root that does not exist; the guard must
            # refuse before anything is written, and the case removes the
            # probe if a broken guard ever creates it.
            @{ Why = "under the repository's own releases directory with -ReleasesDir overridden";
               UploadDir = (Join-Path (Join-Path $repo "releases") (".selftest-probe-" + [System.IO.Path]::GetRandomFileName())) }
        )
        # The "around" case needs the release to sit under what would become
        # the upload root, so it is staged as a copy rather than by moving the
        # tree every other case depends on.
        $wrapReleases = $containment[1].Releases
        Ensure-Directory $wrapReleases
        Copy-Item -LiteralPath $pubRoot -Destination (Join-Path $wrapReleases $relVersion) -Recurse -Force
        $containment[1].UploadDir = Join-Path $script:work "wrap"

        foreach ($c in $containment) {
            $useReleases = if ($c.Why -eq "around") { $wrapReleases } else { $relRoot }
            # What must be untouched: the whole releases tree, not only the
            # version being assembled - the older cut is what F4 would have
            # written into.
            $useRoot = if ($c.Why -eq "around") { Join-Path $wrapReleases $relVersion } else { $relRoot }
            $before = @{}
            foreach ($f in (Get-ChildItem -LiteralPath $useRoot -File -Recurse)) {
                $before[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
            }
            $beforeDirs = @(Get-ChildItem -LiteralPath $useRoot -Directory -Recurse).Count

            $r = Invoke-Releaser @("-UploadSetOnly", "-Version", $relVersion,
                                   "-ReleasesDir", $useReleases, "-PackageRoot", $pkgRoot,
                                   "-UploadDir", $c.UploadDir)
            Assert-True ($r.ExitCode -ne 0) `
                "the upload set was assembled $($c.Why) the written-once release directory."
            # The diagnostic, not just the exit code: copying a tree into its
            # own descendant fails by itself, which would satisfy the exit
            # assertion with the guard deleted.
            Assert-True ($r.Output -match "written once and never") `
                ("expected the $($c.Why) case to be refused by the containment guard. Output:`n" + $r.Output)

            $after = @{}
            foreach ($f in (Get-ChildItem -LiteralPath $useRoot -File -Recurse)) {
                $after[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash
            }
            Assert-True ($after.Count -eq $before.Count) `
                "the refused $($c.Why) run changed how many files the published release holds."
            $hurt = @($before.Keys | Where-Object { $after[$_] -ne $before[$_] })
            Assert-True ($hurt.Count -eq 0) `
                ("the refused $($c.Why) run rewrote " + $hurt.Count + " file(s) of the published release.")
            # A directory created and left behind is invisible to a file hash
            # comparison, and is exactly what a guard that fires too late would
            # leave.
            Assert-True ((@(Get-ChildItem -LiteralPath $useRoot -Directory -Recurse).Count) -eq $beforeDirs) `
                "the refused $($c.Why) run left a directory behind inside the published release."
            if ($c.UploadDir.StartsWith((Join-Path $repo "releases"), [System.StringComparison]::OrdinalIgnoreCase)) {
                Assert-True (-not (Test-Path -LiteralPath $c.UploadDir)) `
                    "the refused $($c.Why) run created its destination under the repository's own releases directory."
                if (Test-Path -LiteralPath $c.UploadDir) {
                    Remove-Item -LiteralPath $c.UploadDir -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
        }
    }

    # --- a release cut refuses the qemu flavour, and says why ---------------
    #
    # Task 13-L.1's single most important row. `qemu` carries the port-0xE9
    # mirror - HAL.dll!WRITE_PORT_UCHAR, the sole import delta between the two
    # published 0.0.0.4 binaries, of which the debug one gave the ThinkPad E460
    # a Code 2 under Windows 98 SE - and it is never published. It is IN the ValidateSet on purpose,
    # so asking for it produces the reason rather than a parameter-binding error
    # about an unknown word: the fact worth reading is that the build exists and
    # is gated like the other two, not that the name is unrecognised.
    #
    # The refusal fires before anything is read or written, so this needs no
    # staged package, no build and no version directory.
    Write-Step "a release cut refuses the qemu flavour"
    $r = Invoke-Releaser @("-Flavor", "qemu", "-UploadSetOnly",
                           "-PackageRoot", $script:work, "-Version", "9.9.9.9")
    Assert-True ($r.ExitCode -ne 0) "a release cut accepted -Flavor qemu."
    Assert-True ($r.Output -match "never published") `
        ("expected the refusal to say the qemu flavour is never published. Output:`n" + $r.Output)
    Assert-True ($r.Output -match "WRITE_PORT_UCHAR") `
        ("expected the refusal to name the import that is the reason. Output:`n" + $r.Output)

    # Both flavours at once must be refused too - naming a publishable flavour
    # beside it must not launder the one that is not.
    $r = Invoke-Releaser @("-Flavor", "release", "-Flavor", "qemu",
                           "-UploadSetOnly", "-PackageRoot", $script:work,
                           "-Version", "9.9.9.9")
    Assert-True ($r.ExitCode -ne 0) `
        "-Flavor release,qemu was accepted: a publishable flavour beside qemu must not launder it."

    # --- and that refusal has to come before the publish, not after ---------
    #
    # A structural check, and the limits are stated rather than glossed: it
    # reads make-release.ps1 and asserts the order of four steps in the source.
    # It cannot observe a real cut - that needs a build, two flavours and the
    # git-ignored tools\ staging, none of which this suite has - so what it
    # locks is the property that was got wrong twice in one review loop.
    #
    # Why the ordering is the whole point: the upload set is assembled at the
    # end of a cut, so a refusal living only there fires with
    # releases\<version>\ already written, and that directory is written once.
    # The INF that provokes it (`xhci98.inf=1,setup`) does not fail earlier by
    # accident either - make-package.ps1 writes a root launcher copy when the
    # layout leaves the root empty, so the staging copy finds what it looks for
    # and the cut proceeds all the way to the publish.
    #
    Write-Step "the media-root refusal comes before the build and the publish"
    $releaserText = [System.IO.File]::ReadAllText($releaser)
    $marks = @(
        @{ Name = "the media-root assertion";  Find = 'Assert-PublishableAtMediaRoot -Layout $declaredLayout' },
        @{ Name = "the containment assertion"; Find = 'Assert-UploadSetOutsideRelease -UploadRoot $early.Root' },
        @{ Name = "the make-package call";     Find = '& powershell.exe @pkgArgs' },
        @{ Name = "the declared-media check";  Find = 'Assert-PackageMatchesDeclaredMedia -PkgDir $pkgDir -Expected $declaredExpected' },
        @{ Name = "the publish swap";          Find = 'Move-Item -LiteralPath $destRoot -Destination $finalRoot' },
        @{ Name = "the upload assembly";       Find = '$set = New-UploadSet -PublishedRoot $destRoot' }
    )
    # The qemu refusal is not in that list because it is not in that sequence:
    # it fires at parameter time, before the first thing the list names, and
    # the case above drives it for real rather than asserting a source order.
    $at = @()
    foreach ($m in $marks) {
        $i = $releaserText.IndexOf($m.Find)
        Assert-True ($i -ge 0) ("make-release.ps1 no longer contains " + $m.Name + " as this test recognises it; the ordering below is unchecked until the anchor is fixed.")
        $at += $i
    }
    if (@($at | Where-Object { $_ -lt 0 }).Count -eq 0) {
        for ($i = 1; $i -lt $marks.Count; $i++) {
            Assert-True ($at[$i - 1] -lt $at[$i]) `
                ("$($marks[$i - 1].Name) must come before $($marks[$i].Name) in make-release.ps1: a refusal that fires after the publish leaves a written-once release with no asset.")
        }
    }

    # --- the readme template may not carry the two claims 1.0.1.0 shipped ----
    #
    # roadmap Phase 20, F7. The rendered readme.txt is byte-identical to the
    # template apart from its placeholders, so the template is what this reads.
    # "WINDOWS 98 ONLY" described DisableSelectiveSuspend as a Windows 98
    # setting after 1.0.1.0 had made the NT path write it too, and the shipped
    # file contradicted itself; "redistributes nothing of Microsoft's" is the
    # sentence AGENTS.md forbids, because XHCISNAP.EXE statically links the
    # MSVC 6.0 runtime the release's own NOTICE.TXT attributes to Microsoft.
    # The defensible form is "No Microsoft file is in this download".
    Write-Step "the readme template carries neither of the two forbidden claims"
    foreach ($forbidden in @("WINDOWS 98 ONLY", "redistributes nothing")) {
        Assert-True ($releaserText -notmatch [regex]::Escape($forbidden)) `
            ("make-release.ps1 still says '" + $forbidden + "'; the readme it renders would repeat a claim 1.0.1.0 made false or AGENTS.md forbids.")
    }
    Assert-True ($releaserText -match "No Microsoft file is in this download") `
        "make-release.ps1 lost the one defensible form of the no-Microsoft-file statement."

    # And the third claim, which the 2026-09-07 audit's D9 fix put IN while
    # removing the other two: an introduction that named Windows 98 SE and
    # Windows 2000 SP4 together as "validated on, including on real hardware".
    # AGENTS.md, "Project Purpose", is explicit that Windows 2000 has never
    # run on real hardware in this project and that every Windows 2000
    # observation here is a virtual-machine one. A download that says
    # otherwise is the one file a user reads before trusting the driver with
    # a machine.
    #
    # Asserted as a pair - the wrong sentence absent, the right one present -
    # because the absence alone is satisfied by deleting the paragraph, and
    # the qualification is the point.
    Assert-True ($releaserText -notmatch "validated on, including on real hardware") `
        "make-release.ps1 tells the reader Windows 2000 SP4 is validated on real hardware; AGENTS.md says every Windows 2000 observation in this project is a virtual-machine one."
    Assert-True ($releaserText -match "Only Windows 98 SE has been validated on") `
        "make-release.ps1 no longer says which target the real-hardware validation belongs to, so the reader cannot tell that Windows 2000 SP4's is virtual-machine only."

    # --- the source stamp: what it can and cannot vouch for -----------------
    #
    # Three rounds of review went into this one gate and each defect was found
    # by reading rather than by a failing test, so here is the test. The
    # `.sys` is a stand-in - `source-stamp.ps1` hashes it as bytes and never
    # parses it - and the source half is hashed from the real `src\`, which is
    # what makes case 4 a genuine mismatch rather than a fabricated one.
    Write-Step "the source stamp: sources, binary identity, and the legacy format"
    $stampScript = Join-Path $repo "scripts\source-stamp.ps1"
    $stampWork = Join-Path $script:work "stamp"
    New-Item -ItemType Directory -Path $stampWork | Out-Null
    $stampSys = Join-Path $stampWork "xhci98.sys"
    $stampFile = Join-Path $stampWork "xhci98.srcstamp"

    # ErrorActionPreference is relaxed across the call, for the reason
    # make-release.ps1 records at its make-package.ps1 invocation (audit H17):
    # in Windows PowerShell 5.1 a native command's stderr line becomes an
    # ErrorRecord, so under "Stop" the refusal this helper exists to MEASURE
    # aborts the suite instead of being returned as an exit code. It bites
    # only when the child's stderr is redirected, which is why the first cut
    # of these tests passed until one of them made the script throw.
    function Invoke-Stamp {
        param([string]$Mode, [string]$Dir)
        $saved = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $null = & powershell -NoProfile -ExecutionPolicy Bypass -File $stampScript $Mode $Dir 2>&1
        } finally {
            $ErrorActionPreference = $saved
        }
        return $LASTEXITCODE
    }

    [System.IO.File]::WriteAllBytes($stampSys, [byte[]](1, 2, 3, 4))
    Assert-True ((Invoke-Stamp "-Write" $stampWork) -eq 0) `
        "source-stamp.ps1 -Write failed on a directory holding an xhci98.sys."
    # @(...) for the same strict-mode reason the script itself needed it: a
    # single matching line comes back as a String, and a String has no .Count.
    Assert-True (@(Get-Content -LiteralPath $stampFile | Where-Object { $_ -cmatch '^BINARY [0-9A-F]{64} xhci98\.sys$' }).Count -eq 1) `
        "the stamp carries no BINARY line, so it cannot say which .sys it was written for (audit H13, round 3)."
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 0) `
        "a stamp checked against the tree and binary it was just written from must pass."

    # The hole the first two H13 fixes both walked around: same sources, a
    # different binary restored beside them. make-release.ps1's own
    # staged-against-built comparison cannot see this - the restored file is on
    # both sides of it - so only the stamp can.
    [System.IO.File]::WriteAllBytes($stampSys, [byte[]](1, 2, 3, 9))
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 1) `
        "a binary swapped under an unchanged stamp must be refused (audit H13)."
    [System.IO.File]::WriteAllBytes($stampSys, [byte[]](1, 2, 3, 4))

    # A stamp written before the BINARY line existed. Sources agree, identity
    # is unavailable: exit 2, which is the ONLY case -AllowUnstampedDriver may
    # wave through.
    $legacy = @(Get-Content -LiteralPath $stampFile | Where-Object { $_ -notmatch '^BINARY ' })
    Set-Content -LiteralPath $stampFile -Value $legacy -Encoding ascii
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 2) `
        "a stamp with no BINARY line but matching sources must answer 2, not 0: it cannot vouch for the binary."

    # ...and the regression that ordering caused. A legacy stamp whose SOURCES
    # have changed must still be the unbypassable 1, not the waveable 2 - the
    # first cut answered 2 the moment it saw no BINARY line, before comparing
    # any sources at all, which made the oldest guarantee bypassable for
    # exactly the binaries least able to afford it.
    $mutated = @($legacy | ForEach-Object {
        if ($_ -match 'xhci_log\.h$') { ("0" * 64) + " xhci_log.h" } else { $_ }
    })
    Set-Content -LiteralPath $stampFile -Value $mutated -Encoding ascii
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 1) `
        "a legacy stamp whose sources have CHANGED must answer 1, not the 2 that -AllowUnstampedDriver can wave through."

    # A source ADDED and a source REMOVED, which are the two kinds of drift the
    # hash comparison reports differently from "contents differ" - a file the
    # build gained, and one it lost. Both must refuse, and from a legacy stamp
    # too, since that is the format this ordering is about.
    $dropped = @($legacy | Where-Object { $_ -notmatch 'xhci_log\.h$' })
    Set-Content -LiteralPath $stampFile -Value $dropped -Encoding ascii
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 1) `
        "a source file the stamp does not name - one added since the build - must be refused."
    $added = @($legacy) + @(("0" * 64) + " nosuchfile.h")
    Set-Content -LiteralPath $stampFile -Value $added -Encoding ascii
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 1) `
        "a source file the stamp names but src\ no longer has - one removed since the build - must be refused."

    # And the same source mismatch under a current-format stamp.
    [System.IO.File]::WriteAllBytes($stampSys, [byte[]](1, 2, 3, 4))
    Assert-True ((Invoke-Stamp "-Write" $stampWork) -eq 0) `
        "source-stamp.ps1 -Write failed on the second write."
    $mutated = @(Get-Content -LiteralPath $stampFile | ForEach-Object {
        if ($_ -match 'xhci_log\.h$') { ("0" * 64) + " xhci_log.h" } else { $_ }
    })
    Set-Content -LiteralPath $stampFile -Value $mutated -Encoding ascii
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 1) `
        "a current-format stamp whose sources have changed must be refused."

    # No stamp at all is 2, not a pass: the caller decides, and make-release.ps1
    # refuses unless -AllowUnstampedDriver says otherwise.
    Remove-Item -LiteralPath $stampFile -Force
    Assert-True ((Invoke-Stamp "-Check" $stampWork) -eq 2) `
        "a directory with no stamp must answer 2."

    # A directory with no binary at all is an ERROR, not a 2. It must not join
    # the one code -AllowUnstampedDriver can wave through: "there is nothing
    # here to publish" is not "this binary's provenance is unknown".
    $emptyDir = Join-Path $script:work "stamp-nobinary"
    New-Item -ItemType Directory -Path $emptyDir | Out-Null
    Assert-True ((Invoke-Stamp "-Write" $emptyDir) -ne 0) `
        "stamping a directory with no xhci98.sys in it must fail."
    Assert-True ((Invoke-Stamp "-Write" $emptyDir) -ne 2) `
        "...and must not fail with 2, which is the code -AllowUnstampedDriver accepts."

    Write-Step "the binary-vs-INF version comparison"
    #
    # Driven on strings rather than through a packaged binary, and that is the
    # only way it can be: there is no stand-in in the tree carrying a *chosen*
    # version resource - the reference binaries are gitignored and the driver's
    # own version is whatever the last build produced. The comparison is
    # therefore the unit, and the vectors are the shapes that decide whether a
    # media mismatch is caught.
    #
    # The suffixed case is the one this used to get wrong: it split on
    # separators and compared the first four tokens, so a binary reporting
    # "1.0.0.1 stale" passed as 1.0.0.1 while the Version tab a user reads said
    # something else.
    #
    $versionCases = @(
        @{ Reported = "1.0.0.1";      Declared = "1.0.0.1"; Want = $true;  Why = "the ordinary match" },
        @{ Reported = "1, 0, 0, 1";   Declared = "1.0.0.1"; Want = $true;  Why = "a resource may spell it with commas" },
        @{ Reported = " 1.0.0.1 ";    Declared = "1.0.0.1"; Want = $true;  Why = "surrounding whitespace is not a difference" },
        @{ Reported = "1.0.0.1";      Declared = "1.0";     Want = $false; Why = "a short DriverVer zero-extends to 1.0.0.0" },
        @{ Reported = "1.0.0.0";      Declared = "1.0";     Want = $true;  Why = "and matches when it really is 1.0.0.0" },
        @{ Reported = "1.0.0.1 stale"; Declared = "1.0.0.1"; Want = $false; Why = "trailing text is not the version" },
        @{ Reported = "1.0.0.1-dirty"; Declared = "1.0.0.1"; Want = $false; Why = "nor is a suffix without a space" },
        @{ Reported = "v1.0.0.1";     Declared = "1.0.0.1"; Want = $false; Why = "nor a prefix" },
        @{ Reported = "1.0.0";        Declared = "1.0.0.1"; Want = $false; Why = "a three-part reported version is not four" },
        @{ Reported = "1.0.0.2";      Declared = "1.0.0.1"; Want = $false; Why = "a real mismatch in the last field" },
        @{ Reported = "";             Declared = "1.0.0.1"; Want = $false; Why = "an empty version matches nothing" }
    )
    foreach ($c in $versionCases) {
        $got = Test-DriverVersionMatches -Reported $c.Reported -Declared $c.Declared
        Assert-True ($got -eq $c.Want) `
            ("version compare '{0}' vs '{1}' returned {2}, expected {3} - {4}." -f `
                $c.Reported, $c.Declared, $got, $c.Want, $c.Why)
    }

} finally {
    if (Test-Path -LiteralPath $script:work) {
        Remove-Item -LiteralPath $script:work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
if ($script:failures.Count -gt 0) {
    Write-Err ("packager self-tests FAILED: {0} of {1} check(s)." -f $script:failures.Count, $script:checks)
    exit 1
}
Write-Ok ("packager self-tests passed ({0} checks)" -f $script:checks)
exit 0
